// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEditorTransaction.cpp
 * @brief UE Editor 事务创建、加入、提交与按 GUID 安全回滚实现。
 */

#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "ScopedTransaction.h"

namespace UnrealAgentMCP::Transactions
{
	namespace
	{
		bool IsEditorTransactionActive()
		{
			return GEditor && GEditor->Trans && GEditor->Trans->IsActive();
		}

		bool UndoEditorTransaction(const FGuid& TransactionId, const FString& PolicyName, FString& OutError)
		{
			if (!GEditor || !GEditor->Trans)
			{
				OutError = FString::Printf(TEXT("%s 工具失败，但 UE 事务系统不可用。"), *PolicyName);
				return false;
			}
			if (GEditor->Trans->FindTransactionIndex(TransactionId) == INDEX_NONE)
			{
				// 空事务不会进入 Undo 队列，不需要执行回滚。
				return true;
			}
			const FTransactionContext UndoContext = GEditor->Trans->GetUndoContext(false);
			if (!UndoContext.TransactionId.IsValid())
			{
				OutError = FString::Printf(TEXT("%s 工具事务存在，但当前不能执行 Undo。"), *PolicyName);
				return false;
			}
			if (UndoContext.TransactionId != TransactionId)
			{
				OutError = FString::Printf(TEXT("%s 工具事务已不在 Undo 栈顶，拒绝撤销其他编辑操作。"), *PolicyName);
				return false;
			}
			if (!GEditor->UndoTransaction(false))
			{
				OutError = FString::Printf(TEXT("%s 工具事务回滚失败。"), *PolicyName);
				return false;
			}
			return true;
		}

		bool UndoEditorTransactionAndRestore(const FGuid& TransactionId, const FString& PolicyName, const Execution::FMcpCompensationHandler& PostRollback, FString& OutError)
		{
			if (!UndoEditorTransaction(TransactionId, PolicyName, OutError))
			{
				return false;
			}
			if (!PostRollback)
			{
				return true;
			}
			FString RestoreError;
			if (!PostRollback(RestoreError))
			{
				OutError = FString::Printf(TEXT("%s 工具已 Undo，但恢复后的持久化失败：%s"), *PolicyName, *RestoreError);
				return false;
			}
			return true;
		}

		class FEditorToolTransactionScope final : public Execution::IMcpToolTransactionScope
		{
		public:
			FEditorToolTransactionScope(FText Description, const EMcpToolTransactionPolicy InPolicy) : Policy(InPolicy), Transaction(MoveTemp(Description))
			{
			}

			virtual ~FEditorToolTransactionScope() override
			{
				if (!bFinalized)
				{
					FString IgnoredError;
					Finalize(false, IgnoredError);
				}
			}

			bool OwnsTransaction() const
			{
				return Transaction.OwnsTransaction();
			}

			virtual bool Finalize(const bool bInvocationSucceeded, FString& OutError) override
			{
				OutError.Reset();
				if (bFinalized)
				{
					OutError = TEXT("工具事务已经完成，不能重复收尾。");
					return false;
				}
				bFinalized = true;

				const bool bOwnedTransaction = Transaction.OwnsTransaction();
				const FGuid TransactionId = Transaction.GetTransactionId();
				Transaction.Close();
				if (bInvocationSucceeded || Policy != EMcpToolTransactionPolicy::Atomic || !bOwnedTransaction)
				{
					return true;
				}

				return UndoEditorTransaction(TransactionId, TEXT("Atomic"), OutError);
			}

		private:
			EMcpToolTransactionPolicy Policy;
			FUnrealAgentMCPScopedEditorTransaction Transaction;
			bool bFinalized = false;
		};

		class FEditorToolTransactionCoordinator final : public Execution::IMcpToolTransactionCoordinator
		{
		public:
			virtual TUniquePtr<Execution::IMcpToolTransactionScope> Begin(const FString& ToolId, const EMcpToolTransactionPolicy Policy, FString& OutError) override
			{
				OutError.Reset();
				if (!IsInGameThread())
				{
					OutError = TEXT("UE Editor 事务只能在游戏线程创建。");
					return nullptr;
				}
				if (Policy != EMcpToolTransactionPolicy::ScopedTransaction && Policy != EMcpToolTransactionPolicy::Atomic)
				{
					OutError = TEXT("UE Editor 事务协调器不支持该事务策略。");
					return nullptr;
				}
				if (GIsTransacting)
				{
					OutError = TEXT("UE 正在执行 Undo/Redo，不能启动工具事务。");
					return nullptr;
				}
				const bool bJoinExistingTransaction = IsEditorTransactionActive();
				if (Policy == EMcpToolTransactionPolicy::Atomic && bJoinExistingTransaction)
				{
					OutError = TEXT("Atomic 工具不能加入调用方正在进行的 UE 事务。");
					return nullptr;
				}
				if (!GEditor || !GEditor->CanTransact())
				{
					OutError = TEXT("当前编辑器状态不能创建 UE 事务。");
					return nullptr;
				}

				TUniquePtr<FEditorToolTransactionScope> Scope =
					MakeUnique<FEditorToolTransactionScope>(FText::FromString(FString::Printf(TEXT("Unreal Agent %s"), *ToolId)), Policy);
				if (!bJoinExistingTransaction && !Scope->OwnsTransaction())
				{
					OutError = TEXT("UE Editor 事务创建失败。");
					return nullptr;
				}
				return Scope;
			}
		};
	}

	FUnrealAgentMCPScopedEditorTransaction::FUnrealAgentMCPScopedEditorTransaction(FText Description)
	{
		if (!IsInGameThread() || GIsTransacting || IsEditorTransactionActive() || !GEditor || !GEditor->CanTransact())
		{
			return;
		}
		Transaction = MakeUnique<FScopedTransaction>(MoveTemp(Description));
		if (Transaction->IsOutstanding() && GUndo)
		{
			TransactionId = GUndo->GetContext().TransactionId;
		}
	}

	FUnrealAgentMCPScopedEditorTransaction::~FUnrealAgentMCPScopedEditorTransaction() = default;

	bool FUnrealAgentMCPScopedEditorTransaction::OwnsTransaction() const
	{
		return Transaction.IsValid() && Transaction->IsOutstanding();
	}

	FGuid FUnrealAgentMCPScopedEditorTransaction::GetTransactionId() const
	{
		if (OwnsTransaction() && GUndo)
		{
			return GUndo->GetContext().TransactionId;
		}
		return TransactionId;
	}

	void FUnrealAgentMCPScopedEditorTransaction::Close()
	{
		Transaction.Reset();
	}

	FUnrealAgentMCPCompensatingEditorTransaction::FUnrealAgentMCPCompensatingEditorTransaction(FText Description, Execution::FMcpCompensationHandler InPostRollback)
		: PostRollback(MoveTemp(InPostRollback))
	{
		const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
		bEnabled = Context && Context->IsCompensationEnabled();
		if (bEnabled)
		{
			Transaction = MakeUnique<FUnrealAgentMCPScopedEditorTransaction>(MoveTemp(Description));
		}
	}

	FUnrealAgentMCPCompensatingEditorTransaction::~FUnrealAgentMCPCompensatingEditorTransaction()
	{
		if (bEnabled && !bFinalized)
		{
			FString IgnoredError;
			CloseAndRollback(IgnoredError);
		}
	}

	bool FUnrealAgentMCPCompensatingEditorTransaction::IsReady(FString& OutError) const
	{
		OutError.Reset();
		if (!bEnabled)
		{
			return true;
		}
		if (Transaction && Transaction->OwnsTransaction())
		{
			return true;
		}
		OutError = TEXT("Compensating 工具无法创建独立 UE Editor 事务。");
		return false;
	}

	bool FUnrealAgentMCPCompensatingEditorTransaction::Register(FString& OutError)
	{
		OutError.Reset();
		if (!bEnabled)
		{
			bFinalized = true;
			return true;
		}
		if (bFinalized || !Transaction || !Transaction->OwnsTransaction())
		{
			OutError = TEXT("Compensating UE Editor 事务不能重复登记。");
			return false;
		}

		const FGuid TransactionId = Transaction->GetTransactionId();
		Transaction->Close();
		Transaction.Reset();
		bFinalized = true;

		const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
		if (Context &&
			Context->RegisterCompensation(
				TEXT("撤销本次调用的 UE Editor 事务"),
				[TransactionId, PostRollback = PostRollback](FString& Error)
				{
					return UndoEditorTransactionAndRestore(TransactionId, TEXT("Compensating"), PostRollback, Error);
				},
				OutError))
		{
			return true;
		}

		FString RollbackError;
		if (!UndoEditorTransactionAndRestore(TransactionId, TEXT("Compensating"), PostRollback, RollbackError))
		{
			OutError += FString::Printf(TEXT("；登记失败后的即时回滚也失败：%s"), *RollbackError);
		}
		return false;
	}

	bool FUnrealAgentMCPCompensatingEditorTransaction::CloseAndRollback(FString& OutError)
	{
		if (bFinalized)
		{
			return true;
		}
		bFinalized = true;
		if (!Transaction || !Transaction->OwnsTransaction())
		{
			Transaction.Reset();
			return true;
		}
		const FGuid TransactionId = Transaction->GetTransactionId();
		Transaction->Close();
		Transaction.Reset();
		return UndoEditorTransactionAndRestore(TransactionId, TEXT("Compensating"), PostRollback, OutError);
	}

	TSharedRef<Execution::IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> CreateEditorTransactionCoordinator()
	{
		return MakeShared<FEditorToolTransactionCoordinator, ESPMode::ThreadSafe>();
	}
}
