// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Query.cpp
 * @brief 资产发现、属性检查、依赖关系与 Registry 诊断。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> CopyQueryArguments(const TSharedPtr<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			if (Args.IsValid())
			{
				Result->Values = Args->Values;
			}
			return Result;
		}

		void CopyQueryAlias(const TSharedRef<FJsonObject>& Args, const TCHAR* Target, std::initializer_list<const TCHAR*> Sources)
		{
			if (Args->HasField(Target))
			{
				return;
			}
			for (const TCHAR* Source : Sources)
			{
				const TSharedPtr<FJsonValue> Value = Args->TryGetField(Source);
				if (Value.IsValid())
				{
					Args->SetField(Target, Value);
					return;
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> NamesToJson(TArray<FName> Names)
		{
			Names.Sort(FNameLexicalLess());
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FName Name : Names)
			{
				Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
			}
			return Values;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Discover(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Normalized = CopyQueryArguments(Args);
		CopyQueryAlias(Normalized, TEXT("searchTerm"), { TEXT("query"), TEXT("name") });
		CopyQueryAlias(Normalized, TEXT("class"), { TEXT("className"), TEXT("assetClass") });
		return Tools::FindAssets(Normalized);
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Inspect(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Normalized = CopyQueryArguments(Args);
		CopyQueryAlias(Normalized, TEXT("assetPath"), { TEXT("path"), TEXT("asset"), TEXT("objectPath") });
		if (Action != TEXT("list_properties"))
		{
			return Tools::ReadAsset(Normalized);
		}

		const FString AssetPath = GetStringArgument(Normalized, { TEXT("assetPath"), TEXT("path") });
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		UObject* Asset = Subsystem ? Subsystem->LoadAsset(AssetPath) : nullptr;
		if (!Asset)
		{
			return ErrorJson(FString::Printf(TEXT("未找到资产：%s"), *AssetPath));
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Asset->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Property->GetName());
			Item->SetStringField(TEXT("type"), Property->GetCPPType());
			Item->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient));
			Properties.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Result->SetNumberField(TEXT("count"), Properties.Num());
		Result->SetArrayField(TEXT("properties"), Properties);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::QueryRegistry(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = Module.Get();

		if (Action == TEXT("health_check") || Action == TEXT("diagnose_registry"))
		{
			TArray<FAssetData> Assets;
			Registry.GetAllAssets(Assets, true);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("provider"), TEXT("Unreal AssetRegistry"));
			Result->SetBoolField(TEXT("loadingAssets"), Registry.IsLoadingAssets());
			Result->SetNumberField(TEXT("assetCount"), Assets.Num());
			Result->SetBoolField(TEXT("editorAssetSubsystemAvailable"), GetAssetSubsystem() != nullptr);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_primary_asset_ids"))
		{
			FString TypeName = GetStringArgument(Args, { TEXT("type"), TEXT("primaryAssetType") });
			if (TypeName.IsEmpty())
			{
				TypeName = TEXT("PrimaryAssetLabel");
			}
			TArray<FPrimaryAssetId> Ids;
			UAssetManager::Get().GetPrimaryAssetIdList(FPrimaryAssetType(*TypeName), Ids);
			Ids.Sort(
				[](const FPrimaryAssetId& Left, const FPrimaryAssetId& Right)
				{
					return Left.ToString() < Right.ToString();
				});
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FPrimaryAssetId& Id : Ids)
			{
				Values.Add(MakeShared<FJsonValueString>(Id.ToString()));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("type"), TypeName);
			Result->SetNumberField(TEXT("count"), Values.Num());
			Result->SetArrayField(TEXT("primaryAssetIds"), Values);
			return SuccessJson(Result);
		}

		const FString AssetPath = GetStringArgument(Args, { TEXT("assetPath"), TEXT("path"), TEXT("asset") });
		const FString PackageName = ToPackageName(AssetPath);
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			return ErrorJson(FString::Printf(TEXT("资产路径不是有效的包名：%s"), *AssetPath));
		}
		TArray<FName> Packages;
		const bool bFound = Action == TEXT("get_referencers") ? Registry.GetReferencers(FName(*PackageName), Packages) : Registry.GetDependencies(FName(*PackageName), Packages);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("packageName"), PackageName);
		Result->SetBoolField(TEXT("registryEntryFound"), bFound);
		Result->SetNumberField(TEXT("count"), Packages.Num());
		Result->SetArrayField(Action == TEXT("get_referencers") ? TEXT("referencers") : TEXT("dependencies"), NamesToJson(MoveTemp(Packages)));
		return SuccessJson(Result);
	}
}
