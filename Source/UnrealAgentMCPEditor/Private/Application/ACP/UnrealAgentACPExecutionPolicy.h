// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EWorldDataAgentMode : uint8
{
	Chat,
	Plan,
	Agent
};

enum class EWorldDataApprovalPolicy : uint8
{
	AlwaysAsk,
	RiskBased,
	FullProject
};

enum class EWorldDataSelfRepairPolicy : uint8
{
	Off,
	Suggest,
	Automatic
};

/** 面板策略层对一次真实 MCP 工具调用的处理结果。 */
enum class EUnrealAgentMCPToolApprovalAction : uint8
{
	Allow,
	Deny,
	Ask
};

/** 从 MCP 工具唯一描述符投影出的审批信息，不包含原始参数或敏感内容。 */
struct FUnrealAgentMCPToolApprovalRequest
{
	FString ToolName;
	FString DisplayToolName;
	FString Description;
	FString Risk;
	FString ConfirmationArgument;
	FString ConfirmationValue;
	bool bReadOnly = false;
	bool bHighRisk = false;
	bool bRequiresConfirmation = false;
};

using FUnrealAgentMCPToolApprovalCompletion = TFunction<void(bool)>;
DECLARE_DELEGATE_TwoParams(FUnrealAgentMCPToolApprovalHandler, const FUnrealAgentMCPToolApprovalRequest&, FUnrealAgentMCPToolApprovalCompletion);
