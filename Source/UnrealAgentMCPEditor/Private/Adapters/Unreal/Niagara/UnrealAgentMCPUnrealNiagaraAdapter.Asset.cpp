// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Asset.cpp
 * @brief Niagara 资产发现、创建、校验与系统参数实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IAssetTools.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/TopLevelAssetPath.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool MatchesDirectory(const FAssetData& Asset, const FString& Directory, const bool bRecursive)
		{
			if (Directory.IsEmpty())
			{
				return true;
			}
			const FString PackagePath = Asset.PackagePath.ToString();
			return bRecursive ? PackagePath.StartsWith(Directory) : PackagePath.Equals(Directory, ESearchCase::IgnoreCase);
		}

		TSharedRef<FJsonObject> AssetEntry(const FAssetData& Asset, const FString& Type)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			Entry->SetStringField(TEXT("type"), Type);
			return Entry;
		}
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		using namespace NiagaraPrivate;
		if (Action == TEXT("list") || Action == TEXT("list_modules"))
		{
			IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			const FString Directory = OptionalString(Args, TEXT("directory"), OptionalString(Args, TEXT("pathFilter")));
			const bool bRecursive = OptionalBool(Args, TEXT("recursive"), true);
			const int32 Limit = OptionalInt(Args, TEXT("limit"), 200);
			TArray<TSharedPtr<FJsonValue>> Entries;
			TArray<FAssetData> Assets;
			if (Action == TEXT("list_modules"))
			{
				Registry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraScript")), Assets, true);
				for (const FAssetData& Asset : Assets)
				{
					if (Entries.Num() >= Limit)
					{
						break;
					}
					if (MatchesDirectory(Asset, Directory, bRecursive))
					{
						Entries.Add(MakeShared<FJsonValueObject>(AssetEntry(Asset, TEXT("Module"))));
					}
				}
				TSharedRef<FJsonObject> Result = SuccessObject();
				Result->SetArrayField(TEXT("modules"), Entries);
				Result->SetNumberField(TEXT("count"), Entries.Num());
				Result->SetNumberField(TEXT("totalAvailable"), Assets.Num());
				return Serialize(Result);
			}

			for (const TPair<FTopLevelAssetPath, FString>& Type :
				{ TPair<FTopLevelAssetPath, FString>(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraSystem")), TEXT("System")),
					TPair<FTopLevelAssetPath, FString>(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraEmitter")), TEXT("Emitter")) })
			{
				Assets.Reset();
				Registry.GetAssetsByClass(Type.Key, Assets, true);
				for (const FAssetData& Asset : Assets)
				{
					if (MatchesDirectory(Asset, Directory, bRecursive))
					{
						Entries.Add(MakeShared<FJsonValueObject>(AssetEntry(Asset, Type.Value)));
					}
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("assets"), Entries);
			Result->SetNumberField(TEXT("count"), Entries.Num());
			return Serialize(Result);
		}

		if (Action == TEXT("get_info") || Action == TEXT("validate"))
		{
			FString AssetPath;
			FString Error = RequireString(Args, TEXT("assetPath"), AssetPath);
			if (!Error.IsEmpty())
			{
				AssetPath = OptionalString(Args, TEXT("systemPath"));
			}
			UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadAsset(AssetPath, UNiagaraSystem::StaticClass()));
			if (!System)
			{
				return Failure(FString::Printf(TEXT("找不到 NiagaraSystem：%s"), *AssetPath));
			}
			if (Action == TEXT("validate"))
			{
				const bool bRequested = System->RequestCompile(true);
				TSharedRef<FJsonObject> Result = SuccessObject();
				Result->SetStringField(TEXT("path"), System->GetPathName());
				Result->SetBoolField(TEXT("compileRequested"), bRequested);
				Result->SetBoolField(TEXT("readyToRun"), System->IsReadyToRun());
				const bool bHasOutstandingCompilation = System->HasOutstandingCompilationRequests();
				Result->SetBoolField(TEXT("hasOutstandingCompilation"), bHasOutstandingCompilation);
				Result->SetBoolField(TEXT("validationPending"), bHasOutstandingCompilation);
				Result->SetBoolField(TEXT("validated"), !bHasOutstandingCompilation && System->IsReadyToRun());
				return Serialize(Result);
			}
			TArray<TSharedPtr<FJsonValue>> Emitters;
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Handle.GetName().ToString());
				Entry->SetStringField(TEXT("uniqueName"), Handle.GetUniqueInstanceName());
				Entry->SetStringField(TEXT("id"), Handle.GetId().ToString());
				Entry->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
				Emitters.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("name"), System->GetName());
			Result->SetStringField(TEXT("path"), System->GetPathName());
			Result->SetNumberField(TEXT("emitterCount"), Emitters.Num());
			Result->SetArrayField(TEXT("emitters"), Emitters);
			Result->SetBoolField(TEXT("readyToRun"), System->IsReadyToRun());
			return Serialize(Result);
		}

		if (Action == TEXT("create") || Action == TEXT("create_emitter"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			const FString PackagePath = OptionalString(Args, TEXT("packagePath"), TEXT("/Game/VFX"));
			const FString OnConflict = OptionalString(Args, TEXT("onConflict"), TEXT("skip"));
			UClass* AssetClass = Action == TEXT("create") ? UNiagaraSystem::StaticClass() : UNiagaraEmitter::StaticClass();
			const TCHAR* FactoryPath = Action == TEXT("create") ? TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew") : TEXT("/Script/NiagaraEditor.NiagaraEmitterFactoryNew");
			bool bCreated = false;
			FString CreateError;
			UObject* Asset = nullptr;
			const FString TemplatePath = OptionalString(Args, TEXT("templatePath"));
			if (Action == TEXT("create_emitter") && !TemplatePath.IsEmpty())
			{
				UNiagaraEmitter* Template = Cast<UNiagaraEmitter>(LoadAsset(TemplatePath, UNiagaraEmitter::StaticClass()));
				if (!Template)
				{
					return Failure(FString::Printf(TEXT("找不到发射器模板：%s"), *TemplatePath));
				}
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
				Asset = AssetTools.DuplicateAsset(Name, PackagePath, Template);
				bCreated = Asset != nullptr;
				if (!Asset)
				{
					CreateError = TEXT("复制 Niagara 发射器模板失败。");
				}
			}
			else
			{
				Asset = CreateAsset(Name, PackagePath, AssetClass, FactoryPath, OnConflict, bCreated, CreateError);
			}
			if (!Asset)
			{
				return Failure(CreateError);
			}
			SaveAsset(Asset);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("path"), Asset->GetPathName());
			Result->SetStringField(TEXT("name"), Asset->GetName());
			Result->SetBoolField(TEXT("created"), bCreated);
			return Serialize(Result);
		}

		if (Action == TEXT("create_system_from_spec"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			bool bCreated = false;
			FString CreateError;
			UNiagaraSystem* System = Cast<UNiagaraSystem>(CreateAsset(Name, OptionalString(Args, TEXT("packagePath"), TEXT("/Game/VFX")), UNiagaraSystem::StaticClass(),
				TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"), OptionalString(Args, TEXT("onConflict"), TEXT("skip")), bCreated, CreateError));
			if (!System)
			{
				return Failure(CreateError);
			}
			int32 Added = 0;
			const TArray<TSharedPtr<FJsonValue>>* Emitters = nullptr;
			if (Args->TryGetArrayField(TEXT("emitters"), Emitters))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Emitters)
				{
					const TSharedPtr<FJsonObject>* Spec = nullptr;
					FString Path;
					if (Value.IsValid() && Value->TryGetObject(Spec) && (*Spec)->TryGetStringField(TEXT("path"), Path))
					{
						UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(LoadAsset(Path, UNiagaraEmitter::StaticClass()));
						if (Emitter && FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid).IsValid())
						{
							++Added;
						}
					}
				}
			}
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("path"), System->GetPathName());
			Result->SetBoolField(TEXT("created"), bCreated);
			Result->SetNumberField(TEXT("emittersAdded"), Added);
			return Serialize(Result);
		}

		if (Action == TEXT("inspect_data_interfaces") || Action == TEXT("list_system_parameters"))
		{
			const FString SystemPath = OptionalString(Args, TEXT("systemPath"), OptionalString(Args, TEXT("assetPath")));
			UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadAsset(SystemPath, UNiagaraSystem::StaticClass()));
			if (!System)
			{
				return Failure(FString::Printf(TEXT("找不到 NiagaraSystem：%s"), *SystemPath));
			}
			const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			TArray<FNiagaraVariable> Variables;
			Store.GetParameters(Variables);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FNiagaraVariable& Variable : Variables)
			{
				if (Action == TEXT("inspect_data_interfaces") && !Variable.IsDataInterface())
				{
					continue;
				}
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Variable.GetName().ToString());
				Entry->SetStringField(TEXT("type"), Variable.GetType().GetName());
				Entry->SetBoolField(TEXT("isDataInterface"), Variable.IsDataInterface());
				if (Variable.IsDataInterface())
				{
					if (UNiagaraDataInterface* Interface = Store.GetDataInterface(Variable))
					{
						Entry->SetStringField(TEXT("class"), Interface->GetClass()->GetName());
					}
				}
				Values.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("systemPath"), System->GetPathName());
			if (Action == TEXT("inspect_data_interfaces"))
			{
				Result->SetArrayField(TEXT("dataInterfaces"), Values);
				Result->SetNumberField(TEXT("count"), Values.Num());
			}
			else
			{
				Result->SetArrayField(TEXT("parameters"), Values);
				Result->SetNumberField(TEXT("parameterCount"), Values.Num());
			}
			return Serialize(Result);
		}

		return Failure(FString::Printf(TEXT("未实现的 Niagara 资产操作：%s"), *Action));
	}
}
