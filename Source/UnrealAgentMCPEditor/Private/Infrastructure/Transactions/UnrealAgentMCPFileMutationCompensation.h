// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPFileMutationCompensation.h
 * @brief 为受控文件覆盖建立字节级失败补偿。
 */

#include "CoreMinimal.h"

namespace UnrealAgentMCP::Transactions
{
	bool RegisterFileWriteCompensation(const FString& FullPath, int64 MaximumSnapshotBytes, FString& OutError);
}
