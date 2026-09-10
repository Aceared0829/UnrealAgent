// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentCodexACPClient.h
 * @brief Codex ACP 适配器客户端：子进程通信、会话与权限确认。
 */

#include "CoreMinimal.h"
#include "Application/ACP/UnrealAgentACPExecutionPolicy.h"
#include "Application/ACP/UnrealAgentACPProviderModel.h"
#include "Application/UnrealAgentMCPApplicationService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/InteractiveProcess.h"

class IUnrealAgentMCPApplicationService;

/** ACP 权限选项（允许/拒绝等）。 */
struct FUnrealAgentAcpPermissionOption
{
	/** 选项稳定 ID（回传给适配器）。 */
	FString OptionId;
	/** 显示名。 */
	FString Name;
	/** 选项类型/Kind。 */
	FString Kind;
};

/** 待用户确认的 ACP 工具权限请求。 */
struct FUnrealAgentAcpPermissionRequest
{
	/** 面板侧本地请求序号。 */
	int32 RequestId = 0;
	/** 弹窗标题。 */
	FString Title;
	/** 工具名。 */
	FString ToolName;
	/** ACP tool call ID。 */
	FString ToolCallId;
	/** ACP session ID。 */
	FString SessionId;
	/** 允许选项 ID。 */
	FString AllowOptionId;
	/** 拒绝选项 ID。 */
	FString DenyOptionId;
	/** 全部可选项。 */
	TArray<FUnrealAgentAcpPermissionOption> Options;
};

/** codex-acp 适配器启动参数。 */
struct FWorldDataCodexAcpLaunchSpec
{
	/** 可执行文件路径。 */
	FString Executable;
	/** 命令行参数。 */
	FString Arguments;
	/** UI 显示用路径。 */
	FString DisplayPath;
};

/** ACP session config 选择项。 */
struct FUnrealAgentAcpConfigOptionValue
{
	FString Value;
	FString Name;
	FString Description;
	/** 当前 ACP 后端是否能够实际应用该值。 */
	bool bEnabled = true;
};

/** ACP session config；模型、模式和推理强度均由 Agent 动态提供。 */
struct FUnrealAgentAcpConfigOption
{
	FString Id;
	FString Name;
	FString Description;
	FString Category;
	FString CurrentValue;
	TArray<FUnrealAgentAcpConfigOptionValue> Options;
};

/** 当前 Codex CLI 实时模型目录中的一项。 */
struct FWorldDataCodexModelCatalogEntry
{
	FString Id;
	FString DisplayName;
	FString Description;
	FString DefaultReasoningEffort;
	bool bDefault = false;
	TArray<FUnrealAgentAcpConfigOptionValue> ReasoningEfforts;
	TArray<FUnrealAgentAcpConfigOptionValue> SpeedOptions;
};

/** 一次 Codex 请求的可观察生命周期。 */
enum class EWorldDataCodexTurnState : uint8
{
	Sending,
	Received,
	Thinking,
	Generating,
	RunningTool,
	Completed,
	Failed
};

struct FWorldDataCodexTurnStatus
{
	EWorldDataCodexTurnState State = EWorldDataCodexTurnState::Sending;
	FString Message;
	/** ACP session/prompt 返回的终止原因；非终态或适配器未提供时为空。 */
	FString StopReason;
	/** 当前独立推理 session 的累计输入 token；仅在 bHasUsage 为 true 时有效。 */
	int32 InputTokens = 0;
	/** 当前独立推理 session 的累计输出 token；仅在 bHasUsage 为 true 时有效。 */
	int32 OutputTokens = 0;
	/** 适配器是否显式返回了 usage。 */
	bool bHasUsage = false;
};

enum class EUnrealAgentAcpToolCallState : uint8
{
	Running,
	Completed,
	Failed
};

/** ACP 工具调用的结构化状态；使用 ToolCallId 将完成事件对应回具体工具。 */
struct FUnrealAgentAcpToolCallUpdate
{
	FString ToolCallId;
	FString Title;
	/** MCP 服务端返回的 canonical action；不从 rawInput 推断。 */
	FString CanonicalAction;
	/** MCP 服务端返回的稳定错误码，例如 invalid_arguments。 */
	FString Code;
	/** 仅保留 schemaErrors 的字段路径，不复制原始请求参数。 */
	TArray<FString> SchemaErrorPaths;
	/** MCP 响应 _meta 中的关联 TraceId。 */
	FString TraceId;
	EUnrealAgentAcpToolCallState State = EUnrealAgentAcpToolCallState::Running;
};

/** 随 ACP session/prompt 发送的内存图片；正文不得写入日志或 Saved。 */
struct FUnrealAgentAcpPromptImage
{
	/** 图片 MIME 类型；空值由协议编码器归一化为 image/png。 */
	FString MimeType;
	/** 不含 data URI 前缀的 Base64 图片正文。 */
	FString Base64Data;
};

DECLARE_DELEGATE_OneParam(FUnrealAgentAcpTextDelegate, const FString&);
DECLARE_DELEGATE_OneParam(FUnrealAgentAcpStatusDelegate, const FString&);
DECLARE_DELEGATE_OneParam(FUnrealAgentAcpErrorDelegate, const FString&);
DECLARE_DELEGATE_OneParam(FUnrealAgentAcpPermissionDelegate, const FUnrealAgentAcpPermissionRequest&);
DECLARE_DELEGATE_OneParam(FWorldDataCodexTurnStatusDelegate, const FWorldDataCodexTurnStatus&);
DECLARE_DELEGATE_OneParam(FUnrealAgentAcpToolCallDelegate, const FUnrealAgentAcpToolCallUpdate&);
DECLARE_DELEGATE_OneParam(FUnrealAgentAcpConfigOptionsDelegate, const TArray<FUnrealAgentAcpConfigOption>&);

/** Codex 执行模式（默认确认 / 仅计划 / 绕过 MCP 确认）。 */
/**
 * 通过 stdio JSON-RPC 与 Codex ACP 适配器交互的客户端。
 * 管理子进程生命周期、会话初始化、流式文本与权限回调。
 */
class FUnrealAgentCodexACPClient : public TSharedFromThis<FUnrealAgentCodexACPClient>
{
public:
	explicit FUnrealAgentCodexACPClient(TSharedRef<IUnrealAgentMCPApplicationService> InApplicationService);
	~FUnrealAgentCodexACPClient();

	/** 主动建立 initialize + session，用于在发送首条消息前读取模型配置。 */
	void Connect();
	/**
	 * 为新的界面对话创建独立 ACP session，但复用已经预热的适配器进程。
	 * session 尚在后台创建时仍可 SendPrompt，消息会在就绪后自动发送。
	 */
	bool BeginNewSession();
	/** 发送纯文本用户提示词并等待流式回复。 */
	void SendPrompt(const FString& Prompt);
	/**
	 * 发送文本与图片内容块并等待流式回复。
	 *
	 * 图片只在 initialize 明确广告 promptCapabilities.image 后发送；
	 * 未协商、重连中或正文无效时会 fail closed 并发出失败状态。
	 *
	 * @param Prompt 作为首个 ACP text 内容块发送的提示词。
	 * @param Images 追加到同一用户消息的图片快照；调用时复制到内存队列。
	 */
	void SendPrompt(const FString& Prompt, const TArray<FUnrealAgentAcpPromptImage>& Images);
	/** 请求 ACP 取消当前回合；终止回调到达后可立即发送队列中的引导消息。 */
	bool CancelActivePrompt();
	/** 停止子进程并清理会话状态。 */
	void Stop();
	/** 设置权限/执行模式。 */
	void SetExecutionPolicy(EWorldDataAgentMode InAgentMode, EWorldDataApprovalPolicy InApprovalPolicy, EWorldDataSelfRepairPolicy InSelfRepairPolicy);
	EWorldDataAgentMode GetAgentMode() const;
	EWorldDataApprovalPolicy GetApprovalPolicy() const;
	EWorldDataSelfRepairPolicy GetSelfRepairPolicy() const;
	/** 指定当前 Codex CLI，并从该版本实时刷新模型能力目录。 */
	void SetCodexCliPath(const FString& InCodexCliPath, bool bRefreshCatalog = true);
	/** 切换实际 ACP Agent；Cursor 使用官方 `agent acp`。 */
	void SetAgentProvider(EUnrealAgentACPProvider InProvider, const FString& InAgentCliPath = FString());
	void RefreshModelCatalog();
	/** 修改 Agent 暴露的 session config option。 */
	bool SetConfigOption(const FString& ConfigId, const FString& Value);
	const TArray<FUnrealAgentAcpConfigOption>& GetConfigOptions() const;
	/** 用户选择权限选项后回传适配器。 */
	void RespondToPermission(int32 RequestId, const FString& OptionId);

	/** 子进程是否存活。 */
	bool IsRunning() const;
	/** 是否已完成 initialize + session。 */
	bool IsReady() const;
	bool HasSession() const;
	/** 是否有 prompt 在途。 */
	bool IsProcessing() const;
	/** 是否已有用户回合在途；单纯 initialize/session 创建不会阻塞输入。 */
	bool HasActiveTurn() const;
	/** 当前客户端是否可以立即接收或排队一条用户消息。 */
	bool CanAcceptPrompt() const;
	/** session 后台创建时仍可改配置；只在真实回合或配置 RPC 在途时锁定。 */
	bool CanChangeConfig() const;
	/** 当前 Provider 的 ACP Agent 是否能从已知位置启动。 */
	bool CanLaunchAgent(FString* OutDisplayPath = nullptr) const;
	FString GetLastError() const;
	/** 当前 ACP session 已确认实际应用的模型 ID。 */
	FString GetAppliedModelId() const;
	/** 当前 initialize 响应是否明确允许在 prompt 中发送图片。 */
	bool DoesAgentSupportImagePrompts() const;

	/** 流式助手文本。 */
	FUnrealAgentAcpTextDelegate OnText;
	/** 状态提示。 */
	FUnrealAgentAcpStatusDelegate OnStatus;
	/** 错误回调。 */
	FUnrealAgentAcpErrorDelegate OnError;
	/** 权限确认请求。 */
	FUnrealAgentAcpPermissionDelegate OnPermission;
	/** 发送、接收、思考、工具执行和完成等结构化回合状态。 */
	FWorldDataCodexTurnStatusDelegate OnTurnStatus;
	/** 带调用 ID、工具名和状态的结构化工具事件。 */
	FUnrealAgentAcpToolCallDelegate OnToolCall;
	/** 模型、模式等 ACP session config 发生变化。 */
	FUnrealAgentAcpConfigOptionsDelegate OnConfigOptionsChanged;

private:
	struct FPendingConfigOptionRequest
	{
		FString LocalConfigId;
		FString RequestedValue;
		FString PreviousValue;
	};

	struct FPendingLocalMcpPermission
	{
		TSharedPtr<FJsonValue> ResponseId;
		TSharedPtr<FJsonObject> McpMessage;
		FString ConfirmationArgument;
		FString ConfirmationValue;
		FString ToolName;
	};

	struct FPendingHttpMcpPermission
	{
		FUnrealAgentMCPToolApprovalCompletion Completion;
		FString ToolName;
	};

	/** 确保适配器子进程已启动。 */
	bool EnsureProcess();
	/** 初始化完成后创建 session。 */
	bool StartSessionIfReady();
	/** session 就绪后发送挂起的 Prompt。 */
	void SendPendingPromptIfReady();

	/** 发送 JSON-RPC 请求，返回本地 RpcId。 */
	int32 SendRpc(const FString& Method, const TSharedPtr<FJsonObject>& Params = nullptr);
	void SendRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
	void SendRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);
	void SendPermissionOutcome(const TSharedPtr<FJsonValue>& Id, const FString& OptionId);
	bool ResolveLocalMcpPermission(int32 RequestId, bool bAllow);
	bool ResolveHttpMcpPermission(int32 RequestId, bool bAllow);
	void DenyPendingHttpMcpPermissions();
	void RequestHttpMcpToolApproval(const FUnrealAgentMCPToolApprovalRequest& Request, FUnrealAgentMCPToolApprovalCompletion Completion);
	void EnsureToolApprovalHandlerRegistered();
	EUnrealAgentMCPToolApprovalAction ResolveToolApprovalAction(const FUnrealAgentMCPToolApprovalRequest& Request) const;
	EWorldDataAgentMode GetEffectiveAgentMode() const;
	EWorldDataApprovalPolicy GetEffectiveApprovalPolicy() const;
	EWorldDataSelfRepairPolicy GetEffectiveSelfRepairPolicy() const;
	void SendRaw(const FString& Json);

	/** 累积 stdout 并按行 ProcessLine。 */
	void ConsumeOutput(const FString& Output);
	void ProcessLine(const FString& Line);
	void HandleRpcResponse(int32 Id, const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error);
	void HandleMethod(const FString& Method, const TSharedPtr<FJsonObject>& Message);
	void HandleSessionUpdate(const FString& AcpSessionId, const TSharedPtr<FJsonObject>& Update);
	void UpdateConfigOptions(const TSharedPtr<FJsonObject>& Payload);
	void ApplyModelCatalog(TArray<FWorldDataCodexModelCatalogEntry>&& Catalog);
	void RebuildCompatibleModelCatalog();
	FString GetCompatibleFallbackModelId(const FString& ExcludedModelId = FString()) const;
	bool ReconcileSessionModel();
	bool TryRecoverFromMissingModelMetadata(const FString& ErrorMessage);
	void RebuildConfigOptions();
	const FUnrealAgentAcpConfigOption* FindSessionConfigOption(const FString& LocalConfigId) const;
	bool TrySetSessionConfigOption(const FString& LocalConfigId, const FString& Value, const FString& PreviousValue);
	void QueueSessionConfigOption(const FString& LocalConfigId, const FString& RemoteConfigId, const FString& Value, const FString& PreviousValue);
	/** 新 session 就绪后，依次应用 UI 在后台准备期间保存的配置。 */
	bool ApplyNextDeferredConfigOption();
	FString GetModelDisplayName(const FString& ModelId) const;
	FString BuildLaunchConfigArguments() const;
	FString GetAgentDisplayName() const;

	/** 在 PATH/已知位置查找适配器启动规格。 */
	bool FindAdapterLaunch(FWorldDataCodexAcpLaunchSpec& OutLaunchSpec) const;
	FString ResolveOnPath(const FString& Command) const;
	/**
	 * 记录错误；若已有待发送或在途用户回合，必须发出 Failed 终止状态。
	 * @param bHadUserTurnBeforeCleanup 调用方在清理传输字段前捕获的用户回合事实。
	 */
	void Fail(const FString& Message, bool bHadUserTurnBeforeCleanup = false);
	void EmitStatus(const FString& Message);
	void EmitText(const FString& Text);
	void EmitTurnStatus(EWorldDataCodexTurnState State, const FString& Message, const FString& StopReason = FString(), int32 InputTokens = 0, int32 OutputTokens = 0,
		bool bHasUsage = false);
	void EmitToolCall(const FString& ToolCallId, const FString& Title, EUnrealAgentAcpToolCallState State, const FUnrealAgentAcpToolCallUpdate* ResultDetails = nullptr);

	/** 交互式子进程。 */
	TSharedPtr<FInteractiveProcess> Process;
	/** MCP 会话配置端口；ACP 应用层不依赖具体 HTTP Server。 */
	TSharedRef<IUnrealAgentMCPApplicationService> ApplicationService;
	/** 未拆行的 stdout 缓冲。 */
	FString StdoutBuffer;
	/** ACP session ID。 */
	FString SessionId;
	/** 等待 session 就绪的挂起 Prompt。 */
	FString PendingPrompt;
	/** 与 PendingPrompt 同生命周期的内存图片，不落盘、不写日志。 */
	TArray<FUnrealAgentAcpPromptImage> PendingPromptImages;
	FString LastError;
	/** 当前适配器显示路径。 */
	FString ActiveAdapterDisplayPath;
	/** 复用 Connect 阶段由 CanLaunchAgent 完成的启动路径查找结果。 */
	mutable TOptional<FWorldDataCodexAcpLaunchSpec> CachedLaunchSpec;
	/** initialize.agentInfo 返回的实际适配器版本。 */
	FString ActiveAdapterVersion;
	/** 当前 UI 配置的 Codex CLI；目录只从这个版本读取。 */
	FString CodexCliPath;
	/** 当前 ACP Agent 及其官方 CLI 路径。 */
	EUnrealAgentACPProvider AgentProvider = EUnrealAgentACPProvider::Codex;
	FString AgentCliPath;
	FString McpApprovalClientId;
	bool bToolApprovalHandlerRegistered = false;
	EWorldDataAgentMode AgentMode = EWorldDataAgentMode::Agent;
	EWorldDataApprovalPolicy ApprovalPolicy = EWorldDataApprovalPolicy::AlwaysAsk;
	EWorldDataSelfRepairPolicy SelfRepairPolicy = EWorldDataSelfRepairPolicy::Off;
	EWorldDataAgentMode ActiveAgentMode = EWorldDataAgentMode::Agent;
	EWorldDataApprovalPolicy ActiveApprovalPolicy = EWorldDataApprovalPolicy::AlwaysAsk;
	EWorldDataSelfRepairPolicy ActiveSelfRepairPolicy = EWorldDataSelfRepairPolicy::Off;

	int32 NextRpcId = 1;
	int32 InitRpcId = 0;
	int32 SessionRpcId = 0;
	int32 PromptRpcId = 0;
	int32 NextPermissionRequestId = 1;
	TMap<int32, FPendingConfigOptionRequest> ConfigOptionRpcIds;
	/** ACP 自身返回的 session 配置；与 Codex CLI 实时目录分开保存。 */
	TArray<FUnrealAgentAcpConfigOption> SessionConfigOptions;
	/** 适配器实际公开过的推理档位；跨新会话保留用于能力协商。 */
	TSet<FString> AdapterReasoningEfforts;
	/** 已保存、等待下一会话或运行时配置确认的本地配置 ID。 */
	TSet<FString> DeferredConfigIds;
	TArray<FUnrealAgentAcpConfigOption> ConfigOptions;
	/** 当前 Codex CLI 返回的原始目录，供适配器升级后重新协商。 */
	TArray<FWorldDataCodexModelCatalogEntry> CliModelCatalog;
	/** 经当前 ACP 版本能力过滤后真正可执行的模型目录。 */
	TArray<FWorldDataCodexModelCatalogEntry> ModelCatalog;
	/** UI 目标模型；切换期间可领先于 ACP 实际值。 */
	FString SelectedModelId;
	/** 仅由 ACP session config currentValue 确认的实际模型。 */
	FString AppliedModelId;
	FString SelectedReasoningEffort;
	FString SelectedSpeed = TEXT("standard");
	/** 当前已发往 ACP 的原始用户消息，用于模型元数据失败后重试。 */
	FString ActivePrompt;
	/** 与 ActivePrompt 同生命周期的内存图片，用于同一次安全重试。 */
	TArray<FUnrealAgentAcpPromptImage> ActivePromptImages;
	/** ACP 在本回合输出的模型元数据诊断。 */
	FString PromptModelDiagnostic;
	/** 本地 RequestId → 远端 JSON-RPC Id。 */
	TMap<int32, TSharedPtr<FJsonValue>> PendingPermissionIds;
	TMap<int32, FPendingLocalMcpPermission> PendingLocalMcpPermissions;
	TMap<int32, FPendingHttpMcpPermission> PendingHttpMcpPermissions;
	/** 工具更新通常只带调用 ID；缓存标题用于完成/失败事件显示。 */
	TMap<FString, FString> ToolCallTitles;

	bool bInitialized = false;
	bool bCreatingSession = false;
	bool bMcpReadinessCheckInFlight = false;
	bool bMcpServerReady = false;
	bool bPromptInFlight = false;
	bool bCatalogRefreshInFlight = false;
	bool bRetriedAfterMissingModelMetadata = false;
	/** 仅来自当前进程 initialize 响应；缺失字段按 false 处理。 */
	bool bAgentSupportsImagePrompts = false;
};
