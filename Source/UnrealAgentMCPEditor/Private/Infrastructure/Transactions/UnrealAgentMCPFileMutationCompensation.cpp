// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFileMutationCompensation.cpp
 * @brief 受控文件写入前快照与失败恢复实现。
 */

#include "Infrastructure/Transactions/UnrealAgentMCPFileMutationCompensation.h"

#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP::Transactions
{
	bool RegisterFileWriteCompensation(const FString& FullPath, const int64 MaximumSnapshotBytes, FString& OutError)
	{
		const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
		if (!Context || !Context->IsCompensationEnabled())
		{
			return true;
		}

		const bool bFileExisted = IFileManager::Get().FileExists(*FullPath);
		const TSharedRef<TArray<uint8>, ESPMode::ThreadSafe> OriginalBytes = MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
		if (bFileExisted)
		{
			const int64 FileSize = IFileManager::Get().FileSize(*FullPath);
			if (FileSize < 0 || FileSize > MaximumSnapshotBytes)
			{
				OutError = FString::Printf(TEXT("现有文件超过 %lld 字节补偿上限，拒绝覆盖。"), MaximumSnapshotBytes);
				return false;
			}
			if (!FFileHelper::LoadFileToArray(*OriginalBytes, *FullPath))
			{
				OutError = TEXT("无法读取现有文件，不能建立补偿快照。");
				return false;
			}
		}

		return Context->RegisterCompensation(
			TEXT("恢复写入前文件状态"),
			[FullPath, bFileExisted, OriginalBytes](FString& Error)
			{
				if (!bFileExisted)
				{
					if (IFileManager::Get().FileExists(*FullPath) && !IFileManager::Get().Delete(*FullPath, false, true, true))
					{
						Error = TEXT("无法删除本次调用创建的文件。");
						return false;
					}
					return true;
				}

				IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
				if (!FFileHelper::SaveArrayToFile(*OriginalBytes, *FullPath))
				{
					Error = TEXT("无法恢复写入前的文件内容。");
					return false;
				}
				return true;
			},
			OutError);
	}
}
