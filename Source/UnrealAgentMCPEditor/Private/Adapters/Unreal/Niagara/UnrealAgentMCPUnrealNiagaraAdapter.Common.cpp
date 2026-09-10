// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Common.cpp
 * @brief Niagara 适配器的 JSON、资产、反射与发射器解析公共实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"

#include "AssetToolsModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EdGraph/EdGraphPin.h"
#include "EngineUtils.h"
#include "Factories/Factory.h"
#include "GameFramework/Actor.h"
#include "IAssetTools.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::NiagaraPrivate
{
	TSharedRef<FJsonObject> SuccessObject()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("niagara"));
		return Result;
	}

	FString Serialize(const TSharedRef<FJsonObject>& Object)
	{
		return JsonObjectToString(Object);
	}

	FString Failure(const FString& Error)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("niagara"));
		Result->SetStringField(TEXT("error"), Error);
		return Serialize(Result);
	}

	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue)
	{
		if (!Args.IsValid() || !Args->TryGetStringField(Name, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			return FString::Printf(TEXT("缺少必填参数 '%s'。"), *Name);
		}
		return FString();
	}

	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue)
	{
		FString Value;
		return Args.IsValid() && Args->TryGetStringField(Name, Value) ? Value : DefaultValue;
	}

	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, const int32 DefaultValue)
	{
		double Value = DefaultValue;
		return Args.IsValid() && Args->TryGetNumberField(Name, Value) ? static_cast<int32>(Value) : DefaultValue;
	}

	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		return Args.IsValid() && Args->TryGetBoolField(Name, Value) ? Value : DefaultValue;
	}

	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass)
	{
		if (Path.IsEmpty() || !ExpectedClass)
		{
			return nullptr;
		}
		UObject* Asset = StaticLoadObject(ExpectedClass, nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			const FString Name = FPackageName::GetShortName(Path);
			Asset = StaticLoadObject(ExpectedClass, nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name));
		}
		return Asset && Asset->IsA(ExpectedClass) ? Asset : nullptr;
	}

	UObject* CreateAsset(const FString& Name, const FString& PackagePath, UClass* AssetClass, const TCHAR* FactoryClassPath, const FString& OnConflict, bool& bOutCreated,
		FString& OutError)
	{
		bOutCreated = false;
		const FString CleanPath = PackagePath.IsEmpty() ? TEXT("/Game/VFX") : PackagePath;
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *CleanPath, *Name, *Name);
		if (UObject* Existing = LoadAsset(ObjectPath, AssetClass))
		{
			if (OnConflict.Equals(TEXT("error"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("资产已存在：%s"), *ObjectPath);
				return nullptr;
			}
			return Existing;
		}

		UFactory* Factory = nullptr;
		if (UClass* FactoryClass = LoadObject<UClass>(nullptr, FactoryClassPath))
		{
			if (FactoryClass->IsChildOf(UFactory::StaticClass()))
			{
				Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
			}
		}
		if (!Factory)
		{
			OutError = FString::Printf(TEXT("无法创建 Niagara Factory：%s"), FactoryClassPath);
			return nullptr;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Asset = AssetTools.CreateAsset(Name, CleanPath, AssetClass, Factory);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("创建资产失败：%s"), *ObjectPath);
			return nullptr;
		}
		bOutCreated = true;
		SaveAsset(Asset);
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		Asset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	}

	bool SetReflectedProperty(void* Container, UStruct* Struct, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Container || !Struct || !Value.IsValid())
		{
			OutError = TEXT("属性写入参数无效。");
			return false;
		}
		FProperty* Property = Struct->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("找不到属性：%s"), *PropertyName);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Container);
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool BoolValue = false;
			if (!Value->TryGetBool(BoolValue))
			{
				OutError = TEXT("目标属性需要布尔值。");
				return false;
			}
			BoolProperty->SetPropertyValue(Address, BoolValue);
			return true;
		}
		if (FNumericProperty* NumberProperty = CastField<FNumericProperty>(Property))
		{
			double NumberValue = 0.0;
			if (!Value->TryGetNumber(NumberValue))
			{
				OutError = TEXT("目标属性需要数值。");
				return false;
			}
			if (NumberProperty->IsInteger())
			{
				NumberProperty->SetIntPropertyValue(Address, static_cast<int64>(NumberValue));
			}
			else
			{
				NumberProperty->SetFloatingPointPropertyValue(Address, NumberValue);
			}
			return true;
		}
		FString Text;
		if (!Value->TryGetString(Text))
		{
			OutError = FString::Printf(TEXT("属性 %s 需要字符串形式的值。"), *PropertyName);
			return false;
		}
		if (!Property->ImportText_Direct(*Text, Address, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("无法将 '%s' 导入属性 %s。"), *Text, *PropertyName);
			return false;
		}
		return true;
	}

	FResolvedEmitter ResolveEmitter(UNiagaraSystem* System, const FString& EmitterName, const int32 EmitterIndex)
	{
		FResolvedEmitter Result;
		if (!System)
		{
			return Result;
		}
		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		if (!EmitterName.IsEmpty())
		{
			for (int32 Index = 0; Index < Handles.Num(); ++Index)
			{
				if (Handles[Index].GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase) ||
					Handles[Index].GetUniqueInstanceName().Equals(EmitterName, ESearchCase::IgnoreCase))
				{
					Result.HandleIndex = Index;
					break;
				}
			}
		}
		else if (Handles.IsValidIndex(EmitterIndex))
		{
			Result.HandleIndex = EmitterIndex;
		}
		if (!Handles.IsValidIndex(Result.HandleIndex))
		{
			return Result;
		}
		const FVersionedNiagaraEmitter Versioned = Handles[Result.HandleIndex].GetInstance();
		Result.Emitter = Versioned.Emitter;
		Result.Version = Versioned.Version;
		Result.Data = Versioned.GetEmitterData();
		return Result;
	}

	void CollectEmitterScripts(FVersionedNiagaraEmitterData* Data, const FString& StackContext, TArray<FScriptSlot>& OutScripts)
	{
		if (!Data)
		{
			return;
		}
		const bool bAll = StackContext.IsEmpty() || StackContext.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		auto Add = [&OutScripts, &StackContext, bAll](const TCHAR* Context, UNiagaraScript* Script)
		{
			if (bAll || StackContext.Equals(Context, ESearchCase::IgnoreCase))
			{
				OutScripts.Add({ Context, Script });
			}
		};
		Add(TEXT("ParticleSpawn"), Data->SpawnScriptProps.Script);
		Add(TEXT("ParticleUpdate"), Data->UpdateScriptProps.Script);
		Add(TEXT("EmitterSpawn"), Data->EmitterSpawnScriptProps.Script);
		Add(TEXT("EmitterUpdate"), Data->EmitterUpdateScriptProps.Script);
	}

	UNiagaraGraph* GraphOfScript(UNiagaraScript* Script)
	{
		if (!Script)
		{
			return nullptr;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		return Source ? Source->NodeGraph : nullptr;
	}

	TSharedRef<FJsonObject> PinToJson(const UEdGraphPin* Pin)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (Pin)
		{
			Result->SetStringField(TEXT("name"), Pin->PinName.ToString());
			Result->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			Result->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			Result->SetBoolField(TEXT("linked"), !Pin->LinkedTo.IsEmpty());
		}
		return Result;
	}

	UNiagaraComponent* FindComponent(const FString& ActorLabel)
	{
		if (!GEditor || !GEditor->GetEditorWorldContext().World())
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
		{
			if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) || It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			{
				return It->FindComponentByClass<UNiagaraComponent>();
			}
		}
		return nullptr;
	}
}
