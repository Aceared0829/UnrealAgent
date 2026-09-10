// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAssetImportCompensation.h
 * @brief 为自动资产导入建立仅覆盖本次新建资产的失败补偿。
 */

#include "CoreMinimal.h"

class UObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPAssetImportCompensation final
	{
	public:
		explicit FUnrealAgentMCPAssetImportCompensation(const FString& DestinationPath);

		bool IsEnabled() const;

		/** Compensating 调用必须禁止覆盖既有资产，避免作出虚假恢复承诺。 */
		bool ValidateReplacePolicy(bool bReplaceExisting, FString& OutError) const;

		/** 在导入成功后登记删除本次新建资产的补偿。 */
		bool RegisterCreatedAssets(const TArray<UObject*>& ImportedAssets, FString& OutError) const;

	private:
		bool bEnabled = false;
		TSet<FString> ExistingAssetPaths;
	};
}
