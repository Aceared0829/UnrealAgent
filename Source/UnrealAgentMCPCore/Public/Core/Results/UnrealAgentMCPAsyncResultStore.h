// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAsyncResultStore.h
 * @brief Unreal Agent 自有的异步调用结果生命周期。
 */

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	enum class EAsyncResultState : uint8
	{
		Pending,
		Succeeded,
		Failed,
		Cancelled
	};

	struct UNREALAGENTMCPCORE_API FAsyncResultSnapshot
	{
		FGuid Id;
		EAsyncResultState State = EAsyncResultState::Pending;
		FString ValueJson;
		FString Error;
		FDateTime CreatedAt;
		FDateTime CompletedAt;

		bool IsComplete() const
		{
			return State != EAsyncResultState::Pending;
		}

		TSharedRef<FJsonObject> ToJsonObject() const;
	};

	/** 结果首次进入终态后广播；重复完成与最终删除不会广播。 */
	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FMcpAsyncResultCompleted, FAsyncResultSnapshot);

	/**
	 * 线程安全的结果存储，支持显式完成、取消、过期清理，
	 * 以及可选的读取即消费语义。
	 */
	class UNREALAGENTMCPCORE_API FAsyncResultStore
	{
	public:
		explicit FAsyncResultStore(FTimespan InRetention = FTimespan::FromMinutes(10));

		FGuid Create();
		bool Complete(const FGuid& Id, FString ValueJson);
		bool Fail(const FGuid& Id, FString Error);
		bool Cancel(const FGuid& Id, FString Reason = FString());
		bool TryRead(const FGuid& Id, FAsyncResultSnapshot& OutSnapshot, bool bConsumeCompleted = false);
		int32 PurgeExpired(FDateTime Now = FDateTime::UtcNow());
		int32 Num() const;

		FMcpAsyncResultCompleted& OnCompleted()
		{
			return ResultCompleted;
		}

	private:
		bool Finish(const FGuid& Id, EAsyncResultState State, FString ValueJson, FString Error);

		mutable FCriticalSection Mutex;
		TMap<FGuid, FAsyncResultSnapshot> Results;
		FTimespan Retention;
		FMcpAsyncResultCompleted ResultCompleted;
	};

	UNREALAGENTMCPCORE_API FString AsyncResultStateToString(EAsyncResultState State);
}
