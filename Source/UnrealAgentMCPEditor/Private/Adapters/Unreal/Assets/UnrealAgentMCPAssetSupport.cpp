// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAssetSupport.cpp
 * @brief MCP 资产路径、类解析、保存与内容统计实现。
 */

#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP::AssetSupport
{
	namespace
	{
		struct FContentCountsCacheEntry
		{
			double CachedAtSeconds = 0.0;
			int32 TotalAssets = 0;
			TMap<FString, int32> CountsByClass;
		};

		// AssetRegistry 遍历成本与资产量线性相关，短期缓存避免资源摘要重复全量扫描。
		TMap<FString, FContentCountsCacheEntry> GContentCountsCache;
		constexpr double GContentCountsCacheTtlSeconds = 15.0;
	}

	UClass* ResolveObjectClass(const FString& ClassText)
	{
		if (ClassText.IsEmpty())
		{
			return nullptr;
		}

		UClass* ObjectClass = FindObject<UClass>(nullptr, *ClassText);
		if (!ObjectClass)
		{
			ObjectClass = LoadObject<UClass>(nullptr, *ClassText);
		}

		if (!ObjectClass && !ClassText.StartsWith(TEXT("/")))
		{
			for (const FString& ModulePath : { TEXT("/Script/Engine."), TEXT("/Script/CoreUObject."), TEXT("/Script/PCG.") })
			{
				ObjectClass = FindObject<UClass>(nullptr, *(ModulePath + ClassText));
				if (!ObjectClass)
				{
					ObjectClass = LoadObject<UClass>(nullptr, *(ModulePath + ClassText));
				}
				if (ObjectClass)
				{
					break;
				}
			}
		}

		return ObjectClass && ObjectClass->IsChildOf(UObject::StaticClass()) ? ObjectClass : nullptr;
	}

	bool SplitAssetPath(FString InPath, FString& OutPackageName, FString& OutAssetName, FString& OutError)
	{
		InPath.TrimStartAndEndInline();
		if (InPath.IsEmpty())
		{
			OutError = TEXT("assetPath is required.");
			return false;
		}
		if (!InPath.StartsWith(TEXT("/Game/")))
		{
			OutError = TEXT("assetPath must be under /Game.");
			return false;
		}

		if (InPath.Contains(TEXT(".")))
		{
			InPath.Split(TEXT("."), &InPath, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		}

		OutPackageName = InPath;
		OutAssetName = FPaths::GetBaseFilename(InPath);
		FText PackageNameError;
		if (OutAssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(OutPackageName, false, &PackageNameError))
		{
			if (OutError.IsEmpty())
			{
				OutError = PackageNameError.IsEmpty() ? TEXT("assetPath is not a valid long package name.") : PackageNameError.ToString();
			}
			return false;
		}
		return true;
	}

	bool SavePackages(const TArray<UPackage*>& Packages)
	{
		if (Packages.Num() == 0)
		{
			return true;
		}

		TArray<UPackage*> SaveList = Packages;
		return FEditorFileUtils::PromptForCheckoutAndSave(SaveList, false, false) == FEditorFileUtils::PR_Success;
	}

	void ComputeContentCounts(const FString& SearchRoot, int32& OutTotalAssets, TMap<FString, int32>& OutCountsByClass)
	{
		const double Now = FPlatformTime::Seconds();
		if (const FContentCountsCacheEntry* Cached = GContentCountsCache.Find(SearchRoot))
		{
			if (Now - Cached->CachedAtSeconds < GContentCountsCacheTtlSeconds)
			{
				OutTotalAssets = Cached->TotalAssets;
				OutCountsByClass = Cached->CountsByClass;
				return;
			}
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*SearchRoot));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);

		OutCountsByClass.Reset();
		for (const FAssetData& AssetData : AssetDataList)
		{
			OutCountsByClass.FindOrAdd(AssetData.AssetClassPath.GetAssetName().ToString())++;
		}
		OutTotalAssets = AssetDataList.Num();

		FContentCountsCacheEntry& Entry = GContentCountsCache.FindOrAdd(SearchRoot);
		Entry.CachedAtSeconds = Now;
		Entry.TotalAssets = OutTotalAssets;
		Entry.CountsByClass = OutCountsByClass;
	}
}
