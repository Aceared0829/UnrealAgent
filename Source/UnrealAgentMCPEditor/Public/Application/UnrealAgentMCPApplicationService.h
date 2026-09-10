// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPApplicationService.h
 * @brief MCP 控制台使用的应用服务端口；隔离 Slate 与 HTTP Server 实现。
 */

#include "CoreMinimal.h"

/** Editor 内 MCP 服务供 ACP 创建会话前使用的硬性健康快照。 */
struct UNREALAGENTMCPEDITOR_API FUnrealAgentMCPReadiness
{
	bool bListening = false;
	bool bManifestReady = false;
	bool bInitializeReady = false;
	bool bToolsListReady = false;
	int32 Port = 0;
	int32 ToolCount = 0;
	FString ServerName;
	FString Error;

	bool IsReady() const
	{
		return bListening && bManifestReady && bInitializeReady && bToolsListReady && Port > 0 && ToolCount > 0 && Error.IsEmpty();
	}
};

using FUnrealAgentMCPReadinessCallback = TFunction<void(const FUnrealAgentMCPReadiness&)>;

/**
 * MCP 控制台的稳定应用入口。
 *
 * Presentation 只能通过该端口启动服务、读取状态和执行配置操作，
 * 不直接依赖 FUnrealAgentMCPServer 或具体 HTTP/文件系统实现。
 */
class UNREALAGENTMCPEDITOR_API IUnrealAgentMCPApplicationService
{
public:
	virtual ~IUnrealAgentMCPApplicationService() = default;

	virtual void StartServer(int32 Port) = 0;
	virtual void StopServer() = 0;
	/** 模块卸载专用：停止入口并排空统一执行内核。 */
	virtual void Shutdown()
	{
		StopServer();
	}
	virtual bool IsServerRunning() const = 0;
	/** 最近一次 ACP 硬性健康探测结果。 */
	virtual FUnrealAgentMCPReadiness GetServerReadiness() const = 0;
	/** 真实调用本地 HTTP initialize 与 tools/list，完成前不得创建 ACP session。 */
	virtual void VerifyServerReadinessAsync(FUnrealAgentMCPReadinessCallback Completion) = 0;
	virtual int32 GetServerPort() const = 0;
	virtual int32 LoadConfiguredPort() const = 0;
	/** 只刷新项目自身连接文件，不修改第三方客户端的全局配置。 */
	virtual void RefreshConnectionFiles() = 0;
	/** 用户显式请求时，才把连接同步到 Codex/Cursor/Claude 等外部客户端。 */
	virtual void ConfigureExternalClients() = 0;

	virtual FString GetProjectInfoJson() const = 0;
	virtual FString GetStatusJson() const = 0;
	virtual FString GetCliSetupReportJson() const = 0;
	virtual FString GetToolDefinitionsJson() const = 0;
	virtual FString GetResourceListJson() const = 0;
	virtual FString ReadResource(const FString& Uri) const = 0;

	virtual FString GetMcpUrl() const = 0;
	virtual FString GetServerName() const = 0;
	virtual FString GetAccessTokenHeaderName() const = 0;
	virtual FString GetAccessToken() const = 0;
	virtual FString GetClientConfigFilePath() const = 0;
	virtual FString GetConnectionFilePath() const = 0;
	virtual FString BuildClientConfigSnippet() const = 0;
};

/** 创建默认应用服务；具体 Server/配置适配器留在 Application 实现内部。 */
UNREALAGENTMCPEDITOR_API TSharedRef<IUnrealAgentMCPApplicationService> CreateUnrealAgentMCPApplicationService();
