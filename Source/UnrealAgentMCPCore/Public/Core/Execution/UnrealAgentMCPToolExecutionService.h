// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPToolExecutionService.h
 * @brief 统一工具 Registry 与任务状态机之间的执行编排服务。
 */

#include "CoreMinimal.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Execution/UnrealAgentMCPExecutionLedger.h"
#include "Core/Policy/UnrealAgentMCPPolicy.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FMcpToolRuntimeRegistry;

	namespace Execution
	{
		/** 单次工具调用持有的事务 scope；Finalize 必须且只能调用一次。 */
		class UNREALAGENTMCPCORE_API IMcpToolTransactionScope
		{
		public:
			virtual ~IMcpToolTransactionScope() = default;

			virtual bool Finalize(bool bInvocationSucceeded, FString& OutError) = 0;
		};

		/** Core 执行服务使用的事务端口；具体 UnrealEd 实现在 Editor 模块。 */
		class UNREALAGENTMCPCORE_API IMcpToolTransactionCoordinator
		{
		public:
			virtual ~IMcpToolTransactionCoordinator() = default;

			virtual TUniquePtr<IMcpToolTransactionScope> Begin(const FString& ToolId, EMcpToolTransactionPolicy Policy, FString& OutError) = 0;
		};

		/** 单次调用的执行约束。 */
		struct UNREALAGENTMCPCORE_API FMcpToolExecutionOptions
		{
			FString Owner;
			FTimespan Timeout = FTimespan::FromSeconds(60);
			bool bForceTask = false;
			Policy::FMcpPolicyRequestContext PolicyContext;

			/** 服务端生成的内部请求标识；无效时由执行服务补齐。 */
			FGuid RequestId;
			FGuid TraceId;
			/** JSON-RPC 等上层协议提供的原始请求标识规范化文本。 */
			FString ProtocolRequestId;
		};

		enum class EMcpToolInvocationOutcome : uint8
		{
			UnknownTool,
			Denied,
			AcceptedAsync,
			Succeeded,
			Failed,
			ContractViolation
		};

		/** 强类型工具调用结果；避免把“工具存在”误当成“业务成功”。 */
		struct UNREALAGENTMCPCORE_API FMcpToolInvocationResult
		{
			EMcpToolInvocationOutcome Outcome = EMcpToolInvocationOutcome::Failed;
			FString Code;
			FString Message;
			FString ResultJson;
			FGuid TaskId;
			FGuid TraceId;
			bool bRetryable = false;

			bool IsSucceeded() const
			{
				return Outcome == EMcpToolInvocationOutcome::Succeeded;
			}

			bool IsAcceptedAsync() const
			{
				return Outcome == EMcpToolInvocationOutcome::AcceptedAsync;
			}
		};

		/**
		 * 所有协议入口共用的工具执行服务。
		 *
		 * 同步短任务可等待终态；长任务、原生异步任务和显式 Task
		 * 立即返回任务标识，由同一任务管理器提供查询与取消。
		 */
		class UNREALAGENTMCPCORE_API FMcpToolExecutionService
		{
		public:
			explicit FMcpToolExecutionService(FMcpToolRuntimeRegistry& InRegistry, FTimespan TaskRetention = FTimespan::FromMinutes(10),
				Policy::FMcpServerPolicy InPolicy = Policy::FMcpServerPolicy());
			FMcpToolExecutionService(FMcpToolRuntimeRegistry& InRegistry, FTimespan TaskRetention, Policy::FMcpServerPolicy InPolicy,
				TSharedPtr<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> InTransactionCoordinator,
				TSharedPtr<IMcpExecutionLedger, ESPMode::ThreadSafe> InExecutionLedger = nullptr);

			/**
			 * 执行已注册工具。
			 * 未找到工具时返回 false；其他校验、执行与超时错误写入 JSON。
			 */
			bool Execute(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FString& OutResultJson,
				const FMcpToolExecutionOptions& Options = FMcpToolExecutionOptions());

			/** 执行并解释统一结果信封；Kernel 等进程内调用方应使用此接口。 */
			FMcpToolInvocationResult ExecuteTyped(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
				const FMcpToolExecutionOptions& Options = FMcpToolExecutionOptions());

			/** 将既有 JSON 结果解释为强类型语义，供契约测试复用。 */
			static FMcpToolInvocationResult InterpretResult(bool bToolFound, FString ResultJson);

			bool TryReadTask(const FGuid& TaskId, FMcpTaskSnapshot& OutSnapshot, bool bConsumeTerminal = false);
			EMcpTaskCancelResult CancelTask(const FGuid& TaskId, FString Reason = FString());
			int32 CancelTasksByClientId(const FString& ClientId, FString Reason = FString());
			TArray<FMcpTaskSnapshot> ListTasks();
			TArray<Policy::FMcpAuditRecord> ListAudit(int32 MaxResults = 200) const;
			const Policy::FMcpServerPolicy& GetPolicy() const;
			bool IsExecutionLedgerHealthy(FString& OutError) const;
			FString GetExecutionLedgerLocation() const;
			int32 Tick(FDateTime Now = FDateTime::UtcNow());
			void Shutdown(FString Reason = FString());
			bool ShutdownAndWait(FString Reason = FString(), FTimespan Timeout = FTimespan::FromSeconds(30));

		private:
			bool AppendAuditRecord(const Policy::FMcpAuditRecord& Record, FString& OutError);

			FMcpToolRuntimeRegistry& Registry;
			FMcpTaskManager TaskManager;
			Policy::FMcpPolicyEngine PolicyEngine;
			Policy::FMcpAuditLog AuditLog;
			TSharedPtr<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> TransactionCoordinator;
			TSharedPtr<IMcpExecutionLedger, ESPMode::ThreadSafe> ExecutionLedger;
		};
	}
}
