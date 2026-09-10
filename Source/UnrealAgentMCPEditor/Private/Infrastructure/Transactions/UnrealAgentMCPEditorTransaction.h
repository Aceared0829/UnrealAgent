// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPEditorTransaction.h
 * @brief 可加入上层事务的 UE 编辑器事务 scope 与 Core 事务端口适配。
 */

#include "CoreMinimal.h"
#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"

class FScopedTransaction;

namespace UnrealAgentMCP::Transactions
{
	/** 已有事务时加入，否则创建一笔独立 UE Editor 事务。 */
	class FUnrealAgentMCPScopedEditorTransaction final
	{
	public:
		explicit FUnrealAgentMCPScopedEditorTransaction(FText Description);
		~FUnrealAgentMCPScopedEditorTransaction();

		FUnrealAgentMCPScopedEditorTransaction(const FUnrealAgentMCPScopedEditorTransaction&) = delete;
		FUnrealAgentMCPScopedEditorTransaction& operator=(const FUnrealAgentMCPScopedEditorTransaction&) = delete;

		bool OwnsTransaction() const;
		FGuid GetTransactionId() const;
		void Close();

	private:
		TUniquePtr<FScopedTransaction> Transaction;
		FGuid TransactionId;
	};

	/**
	 * 为 Compensating 工具创建独立 UE 事务，并将安全 Undo 登记为补偿。
	 * 提前返回时析构函数会立即尝试回滚，避免留下半完成修改。
	 */
	class FUnrealAgentMCPCompensatingEditorTransaction final
	{
	public:
		explicit FUnrealAgentMCPCompensatingEditorTransaction(FText Description, Execution::FMcpCompensationHandler InPostRollback = {});
		~FUnrealAgentMCPCompensatingEditorTransaction();

		FUnrealAgentMCPCompensatingEditorTransaction(const FUnrealAgentMCPCompensatingEditorTransaction&) = delete;
		FUnrealAgentMCPCompensatingEditorTransaction& operator=(const FUnrealAgentMCPCompensatingEditorTransaction&) = delete;

		bool IsReady(FString& OutError) const;
		bool Register(FString& OutError);

	private:
		bool CloseAndRollback(FString& OutError);

		TUniquePtr<FUnrealAgentMCPScopedEditorTransaction> Transaction;
		Execution::FMcpCompensationHandler PostRollback;
		bool bEnabled = false;
		bool bFinalized = false;
	};

	/** 创建供统一执行服务持有的 UnrealEd 事务协调器。 */
	TSharedRef<Execution::IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> CreateEditorTransactionCoordinator();
}
