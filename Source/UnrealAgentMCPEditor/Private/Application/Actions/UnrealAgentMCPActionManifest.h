// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPActionManifest.h
 * @brief 全量兼容 action 的不可变运行时契约目录。
 */

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

class FJsonObject;

namespace UnrealAgentMCP::ActionContracts
{
	/** 一个 action 的稳定标识、Schema 与执行策略。 */
	struct FActionContract
	{
		FString Name;
		FString ToolId;
		FString Status;
		FString Implementation;
		FString Description;
		TSharedPtr<FJsonObject> InputSchema;
		TSharedPtr<FJsonObject> OutputSchema;
		EMcpToolRisk Risk = EMcpToolRisk::ContentMutation;
		EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;
		EMcpToolExecutionMode ExecutionMode = EMcpToolExecutionMode::Synchronous;
		bool bCancelable = false;
	};

	/** 一个分类入口及其 action 级契约。 */
	struct FDomainContract
	{
		FString Name;
		FString TransportTool;
		TSharedPtr<FJsonObject> InputSchema;
		TMap<FString, FActionContract> Actions;
	};

	/**
	 * 解析后只读的 Manifest；分类 Schema 由 action Schema 组合生成，
	 * 因而 tools/list 与运行时校验始终读取同一份事实。
	 */
	class FManifest final
	{
	public:
		static TSharedPtr<FManifest> Parse(const FString& Json, TArray<FString>& OutErrors);

		const FDomainContract* FindDomain(const FString& Domain) const;
		const FActionContract* FindAction(const FString& Domain, const FString& Action) const;

		int32 GetDomainCount() const
		{
			return Domains.Num();
		}
		int32 GetActionCount() const
		{
			return ActionCount;
		}
		const FString& GetContractHash() const
		{
			return ContractHash;
		}
		const TMap<FString, FDomainContract>& GetDomains() const
		{
			return Domains;
		}

	private:
		TMap<FString, FDomainContract> Domains;
		int32 ActionCount = 0;
		FString ContractHash;
	};
}
