// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPSessionRegistry.h
 * @brief 与传输实现无关的 MCP Session 状态机；HTTP 与协议测试共享同一套并发语义。
 */

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

namespace UnrealAgentMCP::Protocol
{
	enum class EMcpSessionValidation : uint8
	{
		Ready,
		MissingSessionId,
		UnknownSession,
		ProtocolMismatch,
		NotInitialized
	};

	struct FMcpSessionSnapshot
	{
		FString Id;
		FString ProtocolVersion;
		bool bInitialized = false;
		uint64 Ordinal = 0;
		uint64 ActivityOrdinal = 0;
		int32 RequestBindingCount = 0;
	};

	/**
	 * 有界、线程安全的 Session Registry。
	 * 每个 Session 独立保存协议版本、initialized 状态和 JSON-RPC 请求到 Task 的绑定。
	 */
	class UNREALAGENTMCPCORE_API FMcpSessionRegistry
	{
	public:
		explicit FMcpSessionRegistry(int32 InMaximumSessions = 16);

		FString Open(const FString& ProtocolVersion, FString* OutEvictedSessionId = nullptr);
		bool Close(const FString& SessionId);
		bool MarkInitialized(const FString& SessionId);
		EMcpSessionValidation Validate(const FString& SessionId, const FString& ProtocolVersion, bool bRequireInitialized, FMcpSessionSnapshot* OutSnapshot = nullptr) const;
		bool TryGet(const FString& SessionId, FMcpSessionSnapshot& OutSnapshot) const;

		bool BindRequestToTask(const FString& SessionId, const FString& RequestKey, const FString& TaskId);
		bool ResolveTaskForRequest(const FString& SessionId, const FString& RequestKey, FString& OutTaskId) const;

		void Reset();
		int32 Num() const;
		int32 GetMaximumSessions() const;

	private:
		struct FSessionState
		{
			FString ProtocolVersion;
			bool bInitialized = false;
			uint64 Ordinal = 0;
			uint64 ActivityOrdinal = 0;
			TMap<FString, FString> RequestTasks;
			TArray<FString> RequestBindingOrder;
		};

		FString RemoveLeastValuableLocked();
		void TouchLocked(FSessionState& State) const;
		static FMcpSessionSnapshot MakeSnapshot(const FString& SessionId, const FSessionState& State);

		mutable FCriticalSection Mutex;
		mutable TMap<FString, FSessionState> Sessions;
		TArray<FString> InsertionOrder;
		int32 MaximumSessions = 16;
		uint64 NextOrdinal = 1;
		mutable uint64 NextActivityOrdinal = 1;
		static constexpr int32 MaximumRequestBindingsPerSession = 256;
	};
}
