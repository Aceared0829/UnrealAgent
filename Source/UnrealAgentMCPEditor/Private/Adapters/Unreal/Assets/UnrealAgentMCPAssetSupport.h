// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAssetSupport.h
 * @brief MCP 资产工具共享的路径校验、类解析、保存与内容统计服务。
 */

#include "CoreMinimal.h"

class UClass;
class UPackage;

namespace UnrealAgentMCP::AssetSupport
{
	UClass* ResolveObjectClass(const FString& ClassText);

	/** 拆分并校验 /Game 下的资产路径，失败文本保持 MCP 协议兼容。 */
	bool SplitAssetPath(FString InPath, FString& OutPackageName, FString& OutAssetName, FString& OutError);

	bool SavePackages(const TArray<UPackage*>& Packages);

	/** 使用短期缓存统计指定内容根目录下的资产总数和类型分布。 */
	void ComputeContentCounts(const FString& SearchRoot, int32& OutTotalAssets, TMap<FString, int32>& OutCountsByClass);
}
