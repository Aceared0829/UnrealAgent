// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Component.cpp
 * @brief Niagara 场景生成、重激活与运行时参数写入实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FVector ReadVector(const TSharedPtr<FJsonObject>& Args, const FString& Field, const FVector& DefaultValue)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(Field, Object))
			{
				return DefaultValue;
			}
			double X = DefaultValue.X;
			double Y = DefaultValue.Y;
			double Z = DefaultValue.Z;
			(*Object)->TryGetNumberField(TEXT("x"), X);
			(*Object)->TryGetNumberField(TEXT("y"), Y);
			(*Object)->TryGetNumberField(TEXT("z"), Z);
			return FVector(X, Y, Z);
		}

		FRotator ReadRotator(const TSharedPtr<FJsonObject>& Args)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(TEXT("rotation"), Object))
			{
				return FRotator::ZeroRotator;
			}
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			(*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*Object)->TryGetNumberField(TEXT("roll"), Roll);
			return FRotator(Pitch, Yaw, Roll);
		}

		bool ApplyParameter(UNiagaraComponent* Component, const FString& Name, const FString& Type, const TSharedPtr<FJsonValue>& Value, FString& OutError)
		{
			if (!Component || !Value.IsValid())
			{
				OutError = TEXT("组件或参数值无效。");
				return false;
			}
			const FName ParameterName(*Name);
			const FString Normalized = Type.ToLower();
			if (Normalized == TEXT("bool") || Value->Type == EJson::Boolean)
			{
				bool BoolValue = false;
				if (!Value->TryGetBool(BoolValue))
				{
					OutError = TEXT("参数需要布尔值。");
					return false;
				}
				Component->SetVariableBool(ParameterName, BoolValue);
				return true;
			}
			if (Normalized == TEXT("int") || Normalized == TEXT("integer"))
			{
				double Number = 0.0;
				if (!Value->TryGetNumber(Number))
				{
					OutError = TEXT("参数需要整数。");
					return false;
				}
				Component->SetVariableInt(ParameterName, static_cast<int32>(Number));
				return true;
			}
			if (Normalized == TEXT("float") || Normalized == TEXT("number") || Value->Type == EJson::Number)
			{
				double Number = 0.0;
				if (!Value->TryGetNumber(Number))
				{
					OutError = TEXT("参数需要数值。");
					return false;
				}
				Component->SetVariableFloat(ParameterName, Number);
				return true;
			}
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Value->TryGetObject(Object))
			{
				double X = 0.0;
				double Y = 0.0;
				double Z = 0.0;
				double W = 0.0;
				(*Object)->TryGetNumberField(TEXT("x"), X);
				(*Object)->TryGetNumberField(TEXT("y"), Y);
				(*Object)->TryGetNumberField(TEXT("z"), Z);
				if (Normalized == TEXT("vec2") || Normalized == TEXT("vector2"))
				{
					Component->SetVariableVec2(ParameterName, FVector2D(X, Y));
				}
				else if ((*Object)->TryGetNumberField(TEXT("w"), W) || Normalized == TEXT("vec4") || Normalized == TEXT("vector4"))
				{
					Component->SetVariableVec4(ParameterName, FVector4(X, Y, Z, W));
				}
				else
				{
					Component->SetVariableVec3(ParameterName, FVector(X, Y, Z));
				}
				return true;
			}
			OutError = FString::Printf(TEXT("不支持的参数类型：%s"), *Type);
			return false;
		}
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::ExecuteComponentAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		using namespace NiagaraPrivate;
		if (Action == TEXT("spawn") || Action == TEXT("spawn_actor"))
		{
			FString SystemPath;
			if (const FString Error = RequireString(Args, TEXT("systemPath"), SystemPath); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadAsset(SystemPath, UNiagaraSystem::StaticClass()));
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!System || !World)
			{
				return Failure(TEXT("NiagaraSystem 或编辑器世界不可用。"));
			}
			const FVector Location = ReadVector(Args, TEXT("location"), FVector::ZeroVector);
			const FRotator Rotation = ReadRotator(Args);
			const FString Label = OptionalString(Args, TEXT("label"), FString::Printf(TEXT("Niagara_%s"), *System->GetName()));
			UNiagaraComponent* Component = nullptr;
			AActor* Owner = nullptr;
			if (Action == TEXT("spawn_actor"))
			{
				FActorSpawnParameters SpawnParameters;
				SpawnParameters.Name = MakeUniqueObjectName(World, ANiagaraActor::StaticClass(), *Label);
				ANiagaraActor* Actor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Location, Rotation, SpawnParameters);
				if (Actor)
				{
					Actor->SetActorLabel(Label);
					Component = Actor->GetNiagaraComponent();
					Component->SetAsset(System);
					Component->Activate(true);
					Owner = Actor;
				}
			}
			else
			{
				Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation, FVector::OneVector, false, true, ENCPoolMethod::None, false);
				Owner = Component ? Component->GetOwner() : nullptr;
				if (Owner)
				{
					Owner->SetActorLabel(Label);
				}
				if (!Component)
				{
					FActorSpawnParameters SpawnParameters;
					SpawnParameters.Name = MakeUniqueObjectName(World, ANiagaraActor::StaticClass(), *Label);
					ANiagaraActor* Actor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Location, Rotation, SpawnParameters);
					if (Actor)
					{
						Actor->SetActorLabel(Label);
						Component = Actor->GetNiagaraComponent();
						Component->SetAsset(System);
						Component->Activate(true);
						Owner = Actor;
					}
				}
			}
			if (!Component)
			{
				return Failure(TEXT("生成 Niagara 组件失败。"));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("component"), Component->GetPathName());
			Result->SetStringField(TEXT("actorLabel"), Owner ? Owner->GetActorLabel() : Label);
			Result->SetStringField(TEXT("systemPath"), System->GetPathName());
			return Serialize(Result);
		}

		FString ActorLabel;
		if (const FString Error = RequireString(Args, TEXT("actorLabel"), ActorLabel); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UNiagaraComponent* Component = FindComponent(ActorLabel);
		if (!Component)
		{
			return Failure(FString::Printf(TEXT("找不到带 NiagaraComponent 的 Actor：%s"), *ActorLabel));
		}
		if (Action == TEXT("reactivate"))
		{
			Component->ReinitializeSystem();
			Component->Activate(true);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("actorLabel"), ActorLabel);
			Result->SetBoolField(TEXT("active"), Component->IsActive());
			return Serialize(Result);
		}

		FString ParameterName;
		if (const FString Error = RequireString(Args, TEXT("parameterName"), ParameterName); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		const TSharedPtr<FJsonValue>* Value = Args->Values.Find(TEXT("value"));
		FString ParameterError;
		if (!Value || !ApplyParameter(Component, ParameterName, OptionalString(Args, TEXT("parameterType")), *Value, ParameterError))
		{
			return Failure(ParameterError.IsEmpty() ? TEXT("缺少参数值。") : ParameterError);
		}
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("actorLabel"), ActorLabel);
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetBoolField(TEXT("updated"), true);
		return Serialize(Result);
	}
}
