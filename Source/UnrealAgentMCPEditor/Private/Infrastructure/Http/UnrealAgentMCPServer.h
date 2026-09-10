// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPServer.h
 * @brief WorldData MCP HTTP/JSON-RPC 服务器：工具分发、资源读取与客户端连接配置。
 */

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"
#include "HttpRouteHandle.h"
#include "IHttpRouter.h"
#include "Application/ACP/UnrealAgentACPExecutionPolicy.h"
#include "Application/UnrealAgentMCPApplicationService.h"
#include "Core/Protocol/UnrealAgentMCPSessionRegistry.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	struct FMcpToolExecutionOptions;
	struct FMcpToolInvocationResult;
	struct FMcpTaskSnapshot;
	enum class EMcpTaskCancelResult : uint8;
}

/**
 * MCP 服务器。
 * 职责：HTTP 路由、JSON-RPC 方法处理、工具/资源分发，以及 Codex/Cursor/Claude 客户端配置文件写入。
 */
class FUnrealAgentMCPServer
{
public:
	/** 在指定端口启动 HTTP MCP 服务。 */
	static void Start(int32 Port);
	/** 停止服务并注销路由。 */
	static void Stop();
	static void BindToolProviderHost();
	static void UnbindToolProviderHost();
	/** 模块卸载前取消并排空统一执行内核。 */
	static void ShutdownExecution();
	/** 服务是否在运行。 */
	static bool IsRunning();
	static FUnrealAgentMCPReadiness GetReadiness();
	static void VerifyReadinessAsync(FUnrealAgentMCPReadinessCallback Completion);
	/** 当前绑定端口；未运行时为上次配置端口语义由调用方决定。 */
	static int32 GetPort();
	/** 从 Saved 配置加载端口。 */
	static int32 LoadConfiguredPort();

	/** 服务器显示名（写入客户端配置）。 */
	static FString GetServerName();
	/** 当前项目稳定 ID。 */
	static FString GetProjectId();
	/** 完整 MCP HTTP URL。 */
	static FString GetMcpUrl();
	/** Access Token 请求头名称。 */
	static FString GetAccessTokenHeaderName();
	/** 当前 Access Token；首次调用会 Ensure。 */
	static FString GetAccessToken();
	/** 项目信息 JSON。 */
	static FString GetProjectInfoJson();
	/** 运行状态 JSON。 */
	static FString GetStatusJson();
	/** 最近一次错误文案。 */
	static FString GetLastError();
	/** 服务启动 UTC 时间。 */
	static FDateTime GetStartedAtUtc();
	/** 最近刷新连接文件的 UTC 时间。 */
	static FDateTime GetLastRefreshAtUtc();
	/** Cursor 等通用客户端配置文件路径。 */
	static FString GetClientConfigFilePath();
	/** Codex 客户端配置文件路径。 */
	static FString GetCodexClientConfigFilePath();
	/** Claude Code 项目设置路径。 */
	static FString GetClaudeSettingsFilePath();
	/** 一键 CLI 配置结果报告 JSON。 */
	static FString GetCliSetupReportJson();
	/** Saved 侧服务器配置文件路径。 */
	static FString GetSavedConfigFilePath();
	/** 项目连接信息文件路径。 */
	static FString GetConnectionFilePath();
	/** 全部 MCP 工具定义 JSON。 */
	static FString GetToolDefinitionsJson();
	/** 从核心与扩展调度注册表导出的全部工具处理器名称。 */
	static TArray<FString> GetRegisteredToolHandlerNames();
	/** 资源列表 JSON。 */
	static FString GetResourceListJson();
	/** 按 URI 读取资源正文。 */
	static FString ReadResource(const FString& Uri);
	/** 刷新 Saved/项目连接文件，不写第三方客户端全局配置。 */
	static void RefreshConnectionFiles();
	/** 显式重写 Codex/Cursor/Claude 等外部客户端配置。 */
	static void ConfigureExternalClients();
	/** 处理一条 JSON-RPC 请求，返回响应对象。 */
	static TSharedPtr<FJsonObject> DispatchJsonRpcRequest(const TSharedPtr<FJsonObject>& Request);
	/** 为面板内 ACP 客户端注册真实 Streamable HTTP 工具调用审批入口。 */
	static void RegisterToolApprovalHandler(const FString& ClientId, FUnrealAgentMCPToolApprovalHandler Handler);
	static void UnregisterToolApprovalHandler(const FString& ClientId);
	/** 撤销客户端身份，并协作取消它尚未结束的全部任务。 */
	static int32 RevokeToolApprovalClient(const FString& ClientId, FString Reason = FString());
	/** 限制 ACP 突变请求速率，避免连续单体调用长期阻塞 Slate。 */
	static bool TryAcquireAcpMutationPermit(const FString& ClientId);
	/** 查找已注册的审批入口；Kernel 直连通道复用同一交互。 */
	static bool FindToolApprovalHandler(const FString& ClientId, FUnrealAgentMCPToolApprovalHandler& OutHandler);
	/** Kernel 直连通道读取的运行时工具目录，与 tools/list 同源。 */
	static TArray<UnrealAgentMCP::FMcpToolDescriptor> GetToolDescriptors();
	/**
	 * Kernel 直连通道的进程内工具执行入口。
	 * 与 HTTP 入口共用执行服务、策略引擎、事务协调器与执行账本。
	 * 工具未注册时返回 false。
	 */
	static bool TryExecuteToolInProcess(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, const UnrealAgentMCP::Execution::FMcpToolExecutionOptions& Options,
		FString& OutResultJson);
	/** Kernel 使用的强类型入口；明确区分未知、拒绝、异步接受和业务终态。 */
	static UnrealAgentMCP::Execution::FMcpToolInvocationResult ExecuteToolInProcessTyped(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
		const UnrealAgentMCP::Execution::FMcpToolExecutionOptions& Options);
	/** 执行账本是否健康；不健康时必须阻断突变工具。 */
	static bool IsExecutionLedgerHealthy(FString& OutError);
	/**
	 * 读取任务式工具的执行快照。
	 * Kernel 的感知端口据此等待截图任务落盘，不另建轮询通道。
	 */
	static bool TryReadToolTask(const FGuid& TaskId, UnrealAgentMCP::Execution::FMcpTaskSnapshot& OutSnapshot, bool bConsumeTerminal);
	/** 取消 Kernel 已取得回执的底层工具任务。 */
	static UnrealAgentMCP::Execution::EMcpTaskCancelResult CancelToolTask(const FGuid& TaskId, FString Reason);
	static int32 CancelToolTasksByClientId(const FString& ClientId, FString Reason);
	/** 从运行时工具目录读取可靠的只读、风险和确认元数据。 */
	static bool DescribeToolApprovalRequest(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FUnrealAgentMCPToolApprovalRequest& OutRequest);
	/** 合并当前帧内的目录变化，并在下一次 Core Tick 广播标准 MCP 通知。 */
	static void ScheduleToolsListChangedBroadcast();

private:
	enum class ERunState : uint8
	{
		Stopped,
		Starting,
		Listening,
		Degraded
	};

	/** HTTP POST：JSON-RPC 主体。 */
	static bool HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	/** HTTP GET：健康/信息。 */
	static bool HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	/** HTTP OPTIONS：CORS 预检。 */
	static bool HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	/** HTTP DELETE：显式关闭调用方持有的 MCP Session。 */
	static bool HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	static TSharedPtr<FJsonObject> CreateInitializeResponse(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> CreateToolsListResponse(const TSharedPtr<FJsonObject>& Params, FString& OutError);
	static TSharedPtr<FJsonObject> ExecuteToolCall(const TSharedPtr<FJsonObject>& Params, const UnrealAgentMCP::Execution::FMcpToolExecutionOptions& Options);
	static TSharedPtr<FJsonObject> CreateResourcesListResponse(const TSharedPtr<FJsonObject>& Params, FString& OutError);
	static TSharedPtr<FJsonObject> CreateResourceReadResponse(const TSharedPtr<FJsonObject>& Params);

	/** 按工具名分发到 Tools / ExtractedTools。 */
	static FString DispatchTool(const FString& ToolName, const FString& ArgsJson, const UnrealAgentMCP::Execution::FMcpToolExecutionOptions& Options);
	/** 携带已认证传输身份处理 JSON-RPC；公开入口继续使用 Internal 默认上下文。 */
	static TSharedPtr<FJsonObject> DispatchJsonRpcRequestWithContext(const TSharedPtr<FJsonObject>& Request, const UnrealAgentMCP::Execution::FMcpToolExecutionOptions& Options);

	/** 服务器 instructions 文案。 */
	static FString GetServerInstructions();
	/** 确保 AccessToken 已生成并持久化。 */
	static void EnsureAccessToken();

	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Code, const FString& Body);
	static void SaveConfiguredPort(int32 Port);
	static void WriteClientConfig();
	static void WriteCodexClientConfig();
	static void WriteClaudeProjectSettings();
	static bool WriteProjectConnectionFile(FString& OutError);
	static FString GetRunStateName();
	static FString GetLoadedBuildId();
	static bool HasBoundListenerState();
	static bool ProbeLoopbackPort(int32 Port);
	static bool ApplyHealthProbeResult(int32 ProbedPort, bool bReachable);
	static bool HandleHealthTick(float DeltaTime);
	static void StartHealthMonitoring();
	static void StopHealthMonitoring();
	/** 返回跨实现文件共享的服务状态锁。 */
	static FCriticalSection& GetStateMutex();
	/** 在线程安全锁内复制当前协商协议版本。 */
	static FString GetNegotiatedProtocolVersionSnapshot();
	/** 结束全部活动 SSE 流；用于服务停止和热重启，不让客户端悬挂。 */
	static void CloseAllSseStreams(const FString& Reason);
	/** 在调度代次仍有效时向活动 SSE 会话广播 tools/list_changed。 */
	static void BroadcastToolsListChanged(uint64 ScheduleSerial);
	/** 注册新会话并返回 SessionId。 */
	static FString RegisterSession(const FString& ProtocolVersion);

	/** HTTP 路由器实例。 */
	static TSharedPtr<IHttpRouter> HttpRouter;
	/** 已注册路由句柄。 */
	static TArray<FHttpRouteHandle> RouteHandles;
	/** 绑定端口。 */
	static int32 BoundPort;
	/** 运行标志。 */
	static bool bRunning;
	static ERunState RunState;
	static FTSTicker::FDelegateHandle HealthTickerHandle;
	static int32 ConsecutiveHealthCheckFailures;
	static int32 HealthRecoveryAttempts;
	static bool bHealthRecoveryInProgress;
	static TFuture<bool> HealthProbeFuture;
	static int32 HealthProbePort;
	static FString InstanceId;
	static FDateTime LastHealthCheckAtUtc;
	static FDateTime LastHealthManifestWriteAtUtc;
	/** HTTP 与协议测试共同依赖的有界 Session 状态机。 */
	static UnrealAgentMCP::Protocol::FMcpSessionRegistry SessionRegistry;
	/** 每个 Session 至多持有一个活动 SSE 回调；重连时原流会被显式结束。 */
	static TMap<FString, TSharedPtr<FHttpResultCallback>> SseCallbacks;
	/** 目录变化广播只在下一 Tick 执行，并合并同一 Tick 内的重复变化。 */
	static bool bToolsListChangedBroadcastScheduled;
	/** 使服务重启前遗留的 Ticker 回调无法消费新一代广播。 */
	static uint64 ToolsListChangedScheduleSerial;
	/** 协商后的协议版本。 */
	static FString NegotiatedProtocolVersion;
	/** Access Token 明文。 */
	static FString AccessToken;
	/** 最近错误。 */
	static FString LastError;
	static FDateTime StartedAtUtc;
	static FDateTime LastRefreshAtUtc;
	static FUnrealAgentMCPReadiness LastReadiness;
};
