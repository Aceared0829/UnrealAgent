// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Emitter.cpp
 * @brief Niagara 发射器、句柄与渲染器编辑实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"

namespace UnrealAgentMCP
{
	namespace
	{
		UClass* ResolveRendererClass(const FString& Type)
		{
			if (Type.Equals(TEXT("sprite"), ESearchCase::IgnoreCase))
			{
				return UNiagaraSpriteRendererProperties::StaticClass();
			}
			if (Type.Equals(TEXT("mesh"), ESearchCase::IgnoreCase))
			{
				return UNiagaraMeshRendererProperties::StaticClass();
			}
			if (Type.Equals(TEXT("ribbon"), ESearchCase::IgnoreCase))
			{
				return UNiagaraRibbonRendererProperties::StaticClass();
			}
			UClass* Class = LoadObject<UClass>(nullptr, *Type);
			if (!Class)
			{
				Class = FindFirstObject<UClass>(*Type, EFindFirstObjectOptions::NativeFirst);
			}
			return Class && Class->IsChildOf(UNiagaraRendererProperties::StaticClass()) ? Class : nullptr;
		}

		TSharedRef<FJsonObject> RendererEntry(UNiagaraRendererProperties* Renderer, const int32 Index)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), Index);
			if (Renderer)
			{
				Entry->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
				Entry->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
			}
			return Entry;
		}
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::ExecuteEmitterAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		using namespace NiagaraPrivate;
		if (Action == TEXT("get_emitter_info"))
		{
			FString AssetPath;
			if (const FString Error = RequireString(Args, TEXT("assetPath"), AssetPath); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(LoadAsset(AssetPath, UNiagaraEmitter::StaticClass()));
			if (!Emitter)
			{
				return Failure(FString::Printf(TEXT("找不到 NiagaraEmitter：%s"), *AssetPath));
			}
			FVersionedNiagaraEmitterData* Data = Emitter->GetEmitterData(Emitter->GetExposedVersion().VersionGuid);
			TArray<TSharedPtr<FJsonValue>> Renderers;
			if (Data)
			{
				for (int32 Index = 0; Index < Data->GetRenderers().Num(); ++Index)
				{
					Renderers.Add(MakeShared<FJsonValueObject>(RendererEntry(Data->GetRenderers()[Index], Index)));
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("path"), Emitter->GetPathName());
			Result->SetStringField(TEXT("name"), Emitter->GetName());
			Result->SetStringField(TEXT("simTarget"), Data && Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"));
			Result->SetArrayField(TEXT("renderers"), Renderers);
			Result->SetNumberField(TEXT("rendererCount"), Renderers.Num());
			return Serialize(Result);
		}

		FString SystemPath;
		if (const FString Error = RequireString(Args, TEXT("systemPath"), SystemPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadAsset(SystemPath, UNiagaraSystem::StaticClass()));
		if (!System)
		{
			return Failure(FString::Printf(TEXT("找不到 NiagaraSystem：%s"), *SystemPath));
		}

		if (Action == TEXT("add_emitter"))
		{
			FString EmitterPath;
			if (const FString Error = RequireString(Args, TEXT("emitterPath"), EmitterPath); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(LoadAsset(EmitterPath, UNiagaraEmitter::StaticClass()));
			if (!Emitter)
			{
				return Failure(FString::Printf(TEXT("找不到 NiagaraEmitter：%s"), *EmitterPath));
			}
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				if (Handle.GetInstance().Emitter == Emitter)
				{
					TSharedRef<FJsonObject> Existing = SuccessObject();
					Existing->SetBoolField(TEXT("created"), false);
					Existing->SetStringField(TEXT("emitterHandleName"), Handle.GetName().ToString());
					Existing->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
					return Serialize(Existing);
				}
			}
			const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid);
			if (!HandleId.IsValid())
			{
				return Failure(TEXT("添加发射器失败。"));
			}
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("emitterHandleId"), HandleId.ToString());
			Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
			return Serialize(Result);
		}

		if (Action == TEXT("list_emitters"))
		{
			TArray<TSharedPtr<FJsonValue>> Emitters;
			for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
			{
				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Index];
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("index"), Index);
				Entry->SetStringField(TEXT("name"), Handle.GetName().ToString());
				Entry->SetStringField(TEXT("uniqueName"), Handle.GetUniqueInstanceName());
				Entry->SetStringField(TEXT("id"), Handle.GetId().ToString());
				Entry->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
				if (Handle.GetInstance().Emitter)
				{
					Entry->SetStringField(TEXT("sourcePath"), Handle.GetInstance().Emitter->GetPathName());
				}
				Emitters.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("emitters"), Emitters);
			Result->SetNumberField(TEXT("count"), Emitters.Num());
			return Serialize(Result);
		}

		const FString EmitterName = OptionalString(Args, TEXT("emitterName"));
		const int32 EmitterIndex = OptionalInt(Args, TEXT("emitterIndex"), 0);
		FResolvedEmitter Resolved = ResolveEmitter(System, EmitterName, EmitterIndex);
		if (!Resolved.Data || !Resolved.Emitter)
		{
			return Failure(TEXT("无法解析目标发射器。"));
		}

		if (Action == TEXT("remove_emitter"))
		{
			const FGuid HandleId = System->GetEmitterHandles()[Resolved.HandleIndex].GetId();
			const FName HandleName = System->GetEmitterHandles()[Resolved.HandleIndex].GetName();
			{
				FNiagaraExternalEditContext EditContext(System);
				UNiagaraExternalEditUtilities::RemoveEmitter(FNiagaraExt_StackItemReference(System, HandleName), EditContext);
				if (EditContext.HasErrors())
				{
					TArray<FString> Errors;
					for (const FText& Error : EditContext.Errors)
					{
						Errors.Add(Error.ToString());
					}
					return Failure(FString::Join(Errors, TEXT("；")));
				}
			}
			System->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("removedHandleId"), HandleId.ToString());
			Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
			Result->SetBoolField(TEXT("packageDirty"), true);
			return Serialize(Result);
		}

		if (Action == TEXT("set_emitter_property"))
		{
			FString PropertyName;
			if (const FString Error = RequireString(Args, TEXT("propertyName"), PropertyName); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			const TSharedPtr<FJsonValue>* Value = Args->Values.Find(TEXT("value"));
			if (!Value)
			{
				return Failure(TEXT("缺少必填参数 'value'。"));
			}
			if (PropertyName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
			{
				bool bEnabled = false;
				if (!(*Value)->TryGetBool(bEnabled))
				{
					return Failure(TEXT("enabled 需要布尔值。"));
				}
				FNiagaraEmitterHandle& Handle = const_cast<FNiagaraEmitterHandle&>(System->GetEmitterHandles()[Resolved.HandleIndex]);
				Handle.SetIsEnabled(bEnabled, *System, true);
			}
			else
			{
				FString PropertyError;
				if (!SetReflectedProperty(Resolved.Data, FVersionedNiagaraEmitterData::StaticStruct(), PropertyName, *Value, PropertyError))
				{
					return Failure(PropertyError);
				}
			}
			Resolved.Emitter->PostEditChange();
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("property"), PropertyName);
			Result->SetBoolField(TEXT("updated"), true);
			return Serialize(Result);
		}

		if (Action == TEXT("list_renderers"))
		{
			TArray<TSharedPtr<FJsonValue>> Renderers;
			for (int32 Index = 0; Index < Resolved.Data->GetRenderers().Num(); ++Index)
			{
				Renderers.Add(MakeShared<FJsonValueObject>(RendererEntry(Resolved.Data->GetRenderers()[Index], Index)));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("emitter"), Resolved.Emitter->GetName());
			Result->SetArrayField(TEXT("renderers"), Renderers);
			Result->SetNumberField(TEXT("rendererCount"), Renderers.Num());
			return Serialize(Result);
		}

		if (Action == TEXT("add_renderer"))
		{
			FString RendererType;
			if (const FString Error = RequireString(Args, TEXT("rendererType"), RendererType); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			UClass* RendererClass = ResolveRendererClass(RendererType);
			if (!RendererClass)
			{
				return Failure(FString::Printf(TEXT("未知渲染器类型：%s"), *RendererType));
			}
			UNiagaraRendererProperties* Renderer = NewObject<UNiagaraRendererProperties>(Resolved.Emitter, RendererClass, NAME_None, RF_Transactional);
			Resolved.Emitter->Modify();
			Resolved.Emitter->AddRenderer(Renderer, Resolved.Version);
			Resolved.Emitter->PostEditChange();
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("rendererClass"), RendererClass->GetName());
			Result->SetNumberField(TEXT("rendererIndex"), Resolved.Data->GetRenderers().IndexOfByKey(Renderer));
			return Serialize(Result);
		}

		const int32 RendererIndex = OptionalInt(Args, TEXT("rendererIndex"), INDEX_NONE);
		if (!Resolved.Data->GetRenderers().IsValidIndex(RendererIndex) || !Resolved.Data->GetRenderers()[RendererIndex])
		{
			return Failure(TEXT("rendererIndex 超出范围。"));
		}
		UNiagaraRendererProperties* Renderer = Resolved.Data->GetRenderers()[RendererIndex];
		if (Action == TEXT("remove_renderer"))
		{
			Resolved.Emitter->Modify();
			Resolved.Emitter->RemoveRenderer(Renderer, Resolved.Version);
			Resolved.Emitter->PostEditChange();
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("removedIndex"), RendererIndex);
			return Serialize(Result);
		}

		FString PropertyName;
		if (const FString Error = RequireString(Args, TEXT("propertyName"), PropertyName); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		const TSharedPtr<FJsonValue>* Value = Args->Values.Find(TEXT("value"));
		FString PropertyError;
		Renderer->Modify();
		if (!Value || !SetReflectedProperty(Renderer, Renderer->GetClass(), PropertyName, Value ? *Value : nullptr, PropertyError))
		{
			return Failure(PropertyError.IsEmpty() ? TEXT("缺少必填参数 'value'。") : PropertyError);
		}
		Renderer->PostEditChange();
		Resolved.Emitter->PostEditChange();
		SaveAsset(System);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("property"), PropertyName);
		Result->SetBoolField(TEXT("updated"), true);
		return Serialize(Result);
	}
}
