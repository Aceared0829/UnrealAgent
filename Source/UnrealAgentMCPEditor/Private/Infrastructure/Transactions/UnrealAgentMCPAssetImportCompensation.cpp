// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAssetImportCompensation.cpp
 * @brief 自动资产导入补偿实现。
 */

#include "Infrastructure/Transactions/UnrealAgentMCPAssetImportCompensation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool DeleteImportedAssets(const TArray<FString>& AssetPaths, FString& OutError)
		{
			TArray<UObject*> AssetsToDelete;
			AssetsToDelete.Reserve(AssetPaths.Num());
			for (const FString& AssetPath : AssetPaths)
			{
				UObject* Asset = FindObject<UObject>(nullptr, *AssetPath);
				if (!Asset)
				{
					Asset = LoadObject<UObject>(nullptr, *AssetPath);
				}
				if (!Asset)
				{
					OutError = FString::Printf(TEXT("无法定位待补偿删除的导入资产：%s"), *AssetPath);
					return false;
				}
				AssetsToDelete.Add(Asset);
			}

			const int32 DeletedCount = ObjectTools::DeleteObjectsUnchecked(AssetsToDelete);
			if (DeletedCount != AssetsToDelete.Num())
			{
				OutError = FString::Printf(TEXT("导入资产补偿删除不完整：期望 %d，实际 %d。"), AssetsToDelete.Num(), DeletedCount);
				return false;
			}
			return true;
		}
	}

	FUnrealAgentMCPAssetImportCompensation::FUnrealAgentMCPAssetImportCompensation(const FString& DestinationPath)
	{
		const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
		bEnabled = Context && Context->IsCompensationEnabled();
		if (!bEnabled)
		{
			return;
		}

		FString NormalizedDestination = DestinationPath;
		NormalizedDestination.TrimStartAndEndInline();
		while (NormalizedDestination.EndsWith(TEXT("/")))
		{
			NormalizedDestination.LeftChopInline(1);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> ExistingAssets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*NormalizedDestination), ExistingAssets, true);
		for (const FAssetData& Asset : ExistingAssets)
		{
			ExistingAssetPaths.Add(Asset.GetObjectPathString());
		}
	}

	bool FUnrealAgentMCPAssetImportCompensation::IsEnabled() const
	{
		return bEnabled;
	}

	bool FUnrealAgentMCPAssetImportCompensation::ValidateReplacePolicy(const bool bReplaceExisting, FString& OutError) const
	{
		if (!bEnabled || !bReplaceExisting)
		{
			return true;
		}
		OutError = TEXT("Compensating 导入禁止 replaceExisting=true；"
						"请改用新资产路径，或等待既有包快照恢复能力。");
		return false;
	}

	bool FUnrealAgentMCPAssetImportCompensation::RegisterCreatedAssets(const TArray<UObject*>& ImportedAssets, FString& OutError) const
	{
		if (!bEnabled)
		{
			return true;
		}

		TArray<FString> CreatedAssetPaths;
		FString ExistingAssetPath;
		CreatedAssetPaths.Reserve(ImportedAssets.Num());
		for (UObject* Asset : ImportedAssets)
		{
			if (!IsValid(Asset))
			{
				continue;
			}
			const FString AssetPath = Asset->GetPathName();
			if (ExistingAssetPaths.Contains(AssetPath))
			{
				ExistingAssetPath = AssetPath;
				continue;
			}
			CreatedAssetPaths.AddUnique(AssetPath);
		}
		if (!ExistingAssetPath.IsEmpty())
		{
			FString CleanupError;
			DeleteImportedAssets(CreatedAssetPaths, CleanupError);
			OutError = FString::Printf(TEXT("导入器修改了既有资产，无法建立安全补偿：%s"), *ExistingAssetPath);
			if (!CleanupError.IsEmpty())
			{
				OutError += FString::Printf(TEXT("；同时清理新建资产失败：%s"), *CleanupError);
			}
			return false;
		}

		if (CreatedAssetPaths.IsEmpty())
		{
			OutError = TEXT("导入器没有返回可登记补偿的新建资产。");
			return false;
		}

		const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
		if (!Context)
		{
			OutError = TEXT("登记资产导入补偿时调用上下文已失效。");
			return false;
		}
		if (Context->RegisterCompensation(
				TEXT("删除本次调用新建的导入资产"),
				[CreatedAssetPaths](FString& Error)
				{
					return DeleteImportedAssets(CreatedAssetPaths, Error);
				},
				OutError))
		{
			return true;
		}

		FString CleanupError;
		if (!DeleteImportedAssets(CreatedAssetPaths, CleanupError))
		{
			OutError += FString::Printf(TEXT("；登记失败后的即时清理也失败：%s"), *CleanupError);
		}
		return false;
	}
}
