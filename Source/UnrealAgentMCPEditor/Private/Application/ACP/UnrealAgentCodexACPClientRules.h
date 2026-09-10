// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentCodexACPClientRules.h
 * @brief Codex ACP 启动规格、JSON-RPC 帧与权限选择的可测试纯规则。
 */

#include "Application/ACP/UnrealAgentCodexACPClient.h"

namespace WorldDataCodexAcpRules
{
	/** 规范化适配器路径，不检查文件是否存在。 */
	FString NormalizeLaunchPath(FString Path);
	FString QuoteCommandLineArgument(FString Argument);
	bool IsSessionModeConfigId(const FString& ConfigId);

	/**
	 * 根据已解析的适配器路径生成启动规格。
	 * Windows 批处理和 PowerShell 脚本分别通过对应宿主启动；不访问文件系统。
	 */
	bool BuildLaunchSpecForResolvedAdapterPath(const FString& AdapterPath, const FString& CommandInterpreterPath, const FString& PowerShellPath,
		FWorldDataCodexAcpLaunchSpec& OutLaunchSpec);

	void AddUniqueNormalizedPath(TArray<FString>& Paths, const FString& Path);
	TArray<FString> GetAdapterCandidateNames();

	TSharedPtr<FJsonValue> ExtractRpcId(const TSharedPtr<FJsonObject>& Message);
	FString GetOptionalString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName);
	int32 GetOptionalInt(const TSharedPtr<FJsonObject>& Object, const FString& FieldName);
	bool ParseJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject);
	FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object);
	/** 将 ACP session/prompt 结果转换为保留终止原因与计量的完成状态。 */
	FWorldDataCodexTurnStatus BuildPromptCompletionStatus(const TSharedPtr<FJsonObject>& Result, const FString& Message);
	/** 将任意 image 内容块的 data 正文替换为占位符后供日志输出。 */
	FString BuildLogSafeJsonRpc(const FString& JsonText);
	/** 按 ACP v1 能力协商读取图片 Prompt 支持；字段缺失或类型错误时返回 false。 */
	bool DoesInitializeResultSupportImagePrompts(const TSharedPtr<FJsonObject>& InitializeResult);
	/** 构造 ACP v1 session/prompt 的 text 与 image 内容块。 */
	TArray<TSharedPtr<FJsonValue>> BuildPromptContentBlocks(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images);
	/** session/prompt JSON-RPC 文本字符上限，防止 Base64 复制撑爆编辑器内存。 */
	inline constexpr int32 MaximumSessionPromptJsonRpcChars = 48 * 1024 * 1024;
	/** 估算 session/prompt JSON-RPC 字符数；以正文和 Base64 为主。 */
	int32 EstimateSessionPromptJsonRpcChars(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images);
	/** 超过上限时返回 false；MaximumChars 默认使用 MaximumSessionPromptJsonRpcChars。 */
	bool IsSessionPromptJsonRpcWithinLimit(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images, int32 MaximumChars = MaximumSessionPromptJsonRpcChars);
	TSharedRef<FJsonObject> BuildHttpMcpServer(const FString& Name, const FString& Url, const FString& TokenHeaderName, const FString& Token, const FString& ApprovalClientId,
		const FString& ProviderId);
	/**
	 * 从 ACP tool_call_update 的 rawOutput 中提取服务器已返回的安全诊断。
	 * 只读取 canonical action、稳定错误码、schema 路径与 traceId，绝不读取 rawInput。
	 */
	void ExtractToolCallResultDetails(const TSharedPtr<FJsonObject>& Update, FUnrealAgentAcpToolCallUpdate& InOutDetails);
	bool TryGetMcpStartupFailure(const TSharedPtr<FJsonObject>& Update, const FString& ExpectedServerName, FString& OutError);

	/**
	 * 从累计 stdout 缓冲中取出完整 JSON-RPC 帧。
	 * 支持换行分帧，也兼容没有换行、直接拼接的 JSON 对象。
	 */
	void ExtractCompleteJsonRpcFrames(FString& InOutBuffer, TArray<FString>& OutFrames);

	TSharedPtr<FJsonObject> BuildJsonRpcErrorResponse(const TSharedPtr<FJsonObject>& Request, int32 Code, const FString& Message);

	FString GetExecutionPolicyInstruction(EWorldDataAgentMode AgentMode, EWorldDataApprovalPolicy ApprovalPolicy, EWorldDataSelfRepairPolicy SelfRepairPolicy);
	/** 将 Agent 与权限策略组合为真实 MCP 调用的确定性处理结果。 */
	EUnrealAgentMCPToolApprovalAction ResolveMcpToolApprovalAction(EWorldDataAgentMode AgentMode, EWorldDataApprovalPolicy ApprovalPolicy,
		const FUnrealAgentMCPToolApprovalRequest& Request);
	TArray<FUnrealAgentAcpPermissionOption> ExtractPermissionOptions(const TSharedPtr<FJsonObject>& Params);
	bool IsShellPermissionRequest(const TSharedPtr<FJsonObject>& ToolCall);
	FString SelectAllowPermissionOptionId(const TArray<FUnrealAgentAcpPermissionOption>& Options);
	FString SelectDenyPermissionOptionId(const TArray<FUnrealAgentAcpPermissionOption>& Options);
	FString GetPermissionRequestTitle(const TSharedPtr<FJsonObject>& ToolCall);
	bool IsAllowPermissionOption(const FString& OptionId);

	/** initialize/session 准备不算用户回合；仅真实消息和权限等待阻塞下一次发送。 */
	bool HasActiveTurnState(bool bHasPendingPrompt, bool bPromptInFlight, int32 PendingPermissionCount);
	/**
	 * 发送前拒绝也必须结束面板回合。
	 * 失败路径可能先清理传输字段，因此必须保留清理前捕获的用户回合事实。
	 */
	bool ShouldFailCloseUserTurn(bool bPromptInFlight, bool bHasPendingPrompt, bool bHasPendingPromptImages, bool bHadUserTurnBeforeCleanup);

	/** 只有 session/prompt 已进入发送队列，才可消费下一次上下文回放。 */
	bool ShouldConsumeContextReplay(EWorldDataCodexTurnState State);

	/** session 创建/initialize 不锁模型菜单；真实回复和权限请求才锁定。 */
	bool CanChangeConfigState(bool bPromptInFlight, int32 PendingPermissionCount);

	/** 已有配置 RPC 时只更新本地目标值，等待当前响应后提交最新选择。 */
	bool ShouldQueueLatestConfigSelection(int32 PendingConfigRequestCount);

	/** 复用进程切换 session 时，过滤旧 session 的迟到更新和权限请求。 */
	bool ShouldIgnoreSessionScopedMessage(const FString& CurrentSessionId, const FString& IncomingSessionId, bool bCreatingSession);

	/** 将 CLI 原始模型名转换为紧凑的选择器显示名。 */
	FString FormatModelDisplayName(const FString& DisplayName);

	/**
	 * 从 Cursor ACP 模型选择值中提取可展示的模式标签。
	 *
	 * Cursor 把 Fast、Max、Thinking 与推理强度编码在模型值的方括号参数中，
	 * 而不是发布独立配置项。本函数只返回界面标签，不改写原始选择值。
	 *
	 * @param ModelSelector Cursor ACP 发布的完整模型值，例如 `grok-4.6[effort=high,fast=true]`。
	 * @return 按稳定界面顺序排列且去重的模式标签；没有模式参数时返回空数组。
	 */
	TArray<FString> GetCursorModelModeLabels(const FString& ModelSelector);

	/**
	 * 构造注入每轮 Prompt 的已确认模型身份说明。
	 * 模型 ID 必须来自 ACP session config，而不是 UI 的乐观选择值。
	 */
	FString BuildConfirmedModelInstruction(const FString& AppliedModelId, const FString& DisplayName);

	/**
	 * codex-acp 0.16 及更早版本可安全用于启动参数的推理档位。
	 * 新档位仅在当前适配器通过 session config 明确公开后启用。
	 */
	bool IsBaselineReasoningEffort(const FString& Effort);

	/**
	 * 判断模型 ID 是否能由已连接的 codex-acp 版本真实执行。
	 * codex-acp 0.16.0 会接受任意模型字符串，但其内置 Codex Core
	 * 尚无 GPT-5.6 直连 ID 和 5.3 Spark 的模型元数据。
	 */
	bool IsModelCompatibleWithAdapter(const FString& ModelId, const FString& AdapterVersion);

	/** 返回指定适配器版本已知可执行的保守回退模型。 */
	FString GetSafeFallbackModelIdForAdapter(const FString& AdapterVersion);

	/**
	 * 解析当前 Codex CLI 的 `codex debug models` 输出。
	 * 只保留该版本标记为可见的模型，并完整保留其推理和速度能力。
	 */
	bool ParseCodexModelCatalog(const FString& JsonText, TArray<FWorldDataCodexModelCatalogEntry>& OutCatalog, FString& OutError);
}
