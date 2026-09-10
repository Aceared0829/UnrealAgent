// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAssetTools.cpp
 * @brief 资产查询、读取、通用创建与 Blueprint 创建工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/Factory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "UObject/Package.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"

namespace UnrealAgentMCP::Tools
{
	using namespace AssetSupport;
	using namespace JsonConversion;

	FString FindAssets(const TSharedPtr<FJsonObject>& Args)
	{
		FString SearchTerm;
		Args->TryGetStringField(TEXT("searchTerm"), SearchTerm);

		FString SearchRoot = TEXT("/Game");
		Args->TryGetStringField(TEXT("path"), SearchRoot);
		if (SearchRoot.IsEmpty())
		{
			SearchRoot = TEXT("/Game");
		}

		FString ClassFilter;
		Args->TryGetStringField(TEXT("classFilter"), ClassFilter);

		double MaxResultsNumber = 50.0;
		Args->TryGetNumberField(TEXT("maxResults"), MaxResultsNumber);
		const int32 MaxResults = FMath::Clamp(static_cast<int32>(MaxResultsNumber), 1, 500);

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*SearchRoot));
		Filter.bRecursivePaths = true;

		// classFilter 能解析为具体 UClass 时，把过滤条件下推到 AssetRegistry 索引查询，
		// 避免先遍历全部资产再由 C++ 做子串匹配。
		bool bClassPushedDown = false;
		if (!ClassFilter.IsEmpty())
		{
			if (const UClass* ResolvedClass = UClass::TryFindTypeSlow<UClass>(ClassFilter))
			{
				Filter.ClassPaths.Add(ResolvedClass->GetClassPathName());
				Filter.bRecursiveClasses = true;
				bClassPushedDown = true;
			}
		}

		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);

		TArray<TSharedPtr<FJsonValue>> Assets;
		int32 MatchedCount = 0;
		for (const FAssetData& AssetData : AssetDataList)
		{
			const FString AssetName = AssetData.AssetName.ToString();
			const FString ObjectPath = AssetData.GetObjectPathString();
			const FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();

			if (!SearchTerm.IsEmpty() && !AssetName.Contains(SearchTerm, ESearchCase::IgnoreCase) && !ObjectPath.Contains(SearchTerm, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!bClassPushedDown && !ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			++MatchedCount;
			if (Assets.Num() < MaxResults)
			{
				TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
				AssetJson->SetStringField(TEXT("name"), AssetName);
				AssetJson->SetStringField(TEXT("path"), ObjectPath);
				AssetJson->SetStringField(TEXT("packageName"), AssetData.PackageName.ToString());
				AssetJson->SetStringField(TEXT("class"), ClassName);
				Assets.Add(MakeShared<FJsonValueObject>(AssetJson));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Assets.Num());
		Result->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Result->SetBoolField(TEXT("truncated"), MatchedCount > Assets.Num());
		Result->SetArrayField(TEXT("assets"), Assets);
		return SuccessJson(Result);
	}

	FString ReadAsset(const TSharedPtr<FJsonObject>& Args)
	{
		FString Path;
		if (!Args->TryGetStringField(TEXT("assetPath"), Path) && !Args->TryGetStringField(TEXT("path"), Path))
		{
			return ErrorJson(TEXT("Missing required field 'assetPath'."));
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NormalizeAssetObjectPath(Path)));
		if (!AssetData.IsValid())
		{
			FString PackageName = Path;
			if (PackageName.Contains(TEXT(".")))
			{
				PackageName.Split(TEXT("."), &PackageName, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			}

			TArray<FAssetData> PackageAssets;
			AssetRegistry.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				AssetData = PackageAssets[0];
			}
		}

		if (!AssetData.IsValid())
		{
			return ErrorJson(FString::Printf(TEXT("Asset not found: %s"), *Path));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Result->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		Result->SetStringField(TEXT("packageName"), AssetData.PackageName.ToString());
		Result->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		Result->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		Result->SetStringField(TEXT("classPath"), AssetData.AssetClassPath.ToString());
		Result->SetBoolField(TEXT("isRedirector"), AssetData.IsRedirector());
		return SuccessJson(Result);
	}

	FString CreateAsset(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("path"), AssetPath))
		{
			return ErrorJson(TEXT("Missing required field 'assetPath'."));
		}

		FString ClassText = TEXT("DataAsset");
		Args->TryGetStringField(TEXT("class"), ClassText);
		UClass* AssetClass = ResolveObjectClass(ClassText);
		if (!AssetClass || AssetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return ErrorJson(FString::Printf(TEXT("Asset class not found or not instantiable: %s"), *ClassText));
		}

		FString PackageName;
		FString AssetName;
		FString Error;
		if (!SplitAssetPath(AssetPath, PackageName, AssetName, Error))
		{
			return ErrorJson(Error);
		}

		if (StaticFindObject(UObject::StaticClass(), nullptr, *NormalizeAssetObjectPath(PackageName)))
		{
			return ErrorJson(FString::Printf(TEXT("Asset already exists: %s"), *PackageName));
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "CreateAsset", "MCP Create Asset"));
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return ErrorJson(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
		}
		Package->FullyLoad();

		UObject* Asset = NewObject<UObject>(Package, AssetClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (!Asset)
		{
			return ErrorJson(FString::Printf(TEXT("Failed to create asset object: %s"), *AssetName));
		}

		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();

		bool bSave = false;
		Args->TryGetBoolField(TEXT("save"), bSave);
		bool bSaved = false;
		if (bSave)
		{
			TArray<UPackage*> PackagesToSave;
			PackagesToSave.Add(Package);
			bSaved = SavePackages(PackagesToSave);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Result->SetStringField(TEXT("packageName"), PackageName);
		Result->SetStringField(TEXT("class"), AssetClass->GetName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return SuccessJson(Result);
	}

	FString CreateBlueprintAsset(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("path"), AssetPath))
		{
			return ErrorJson(TEXT("Missing required field 'assetPath'."));
		}

		FString ParentClassText = TEXT("Actor");
		Args->TryGetStringField(TEXT("parentClass"), ParentClassText);
		UClass* ParentClass = ResolveObjectClass(ParentClassText);
		if (!ParentClass)
		{
			return ErrorJson(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassText));
		}

		FString PackageName;
		FString AssetName;
		FString Error;
		if (!SplitAssetPath(AssetPath, PackageName, AssetName, Error))
		{
			return ErrorJson(Error);
		}
		if (StaticFindObject(UObject::StaticClass(), nullptr, *NormalizeAssetObjectPath(PackageName)))
		{
			return ErrorJson(FString::Printf(TEXT("Asset already exists: %s"), *PackageName));
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "CreateBlueprintAsset", "MCP Create Blueprint Asset"));
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return ErrorJson(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
		}

		UBlueprint* Blueprint =
			FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*AssetName), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			return ErrorJson(TEXT("Failed to create Blueprint asset."));
		}

		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();

		bool bSave = false;
		Args->TryGetBoolField(TEXT("save"), bSave);
		bool bSaved = false;
		if (bSave)
		{
			TArray<UPackage*> PackagesToSave;
			PackagesToSave.Add(Package);
			bSaved = SavePackages(PackagesToSave);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("packageName"), PackageName);
		Result->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetStringField(TEXT("note"), TEXT("Blueprint asset creation is supported; graph node construction is not exposed yet."));
		return SuccessJson(Result);
	}
}
