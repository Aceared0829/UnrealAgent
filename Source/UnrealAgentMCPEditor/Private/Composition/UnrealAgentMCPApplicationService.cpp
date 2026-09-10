// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPApplicationService.cpp
 * @brief 默认 MCP 应用服务，将稳定端口适配到当前嵌入式 Server。
 */

#include "UnrealAgentMCPEditor/Public/Application/UnrealAgentMCPApplicationService.h"

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

namespace
{
	class FUnrealAgentMCPApplicationService final : public IUnrealAgentMCPApplicationService
	{
	public:
		virtual void StartServer(const int32 Port) override
		{
			FUnrealAgentMCPServer::Start(Port);
		}

		virtual void StopServer() override
		{
			FUnrealAgentMCPServer::Stop();
		}

		virtual void Shutdown() override
		{
			FUnrealAgentMCPServer::Stop();
			FUnrealAgentMCPServer::ShutdownExecution();
		}

		virtual bool IsServerRunning() const override
		{
			return FUnrealAgentMCPServer::IsRunning();
		}

		virtual FUnrealAgentMCPReadiness GetServerReadiness() const override
		{
			return FUnrealAgentMCPServer::GetReadiness();
		}

		virtual void VerifyServerReadinessAsync(FUnrealAgentMCPReadinessCallback Completion) override
		{
			FUnrealAgentMCPServer::VerifyReadinessAsync(MoveTemp(Completion));
		}

		virtual int32 GetServerPort() const override
		{
			return FUnrealAgentMCPServer::GetPort();
		}

		virtual int32 LoadConfiguredPort() const override
		{
			return FUnrealAgentMCPServer::LoadConfiguredPort();
		}

		virtual void RefreshConnectionFiles() override
		{
			FUnrealAgentMCPServer::RefreshConnectionFiles();
		}

		virtual void ConfigureExternalClients() override
		{
			FUnrealAgentMCPServer::ConfigureExternalClients();
		}

		virtual FString GetProjectInfoJson() const override
		{
			return FUnrealAgentMCPServer::GetProjectInfoJson();
		}

		virtual FString GetStatusJson() const override
		{
			return FUnrealAgentMCPServer::GetStatusJson();
		}

		virtual FString GetCliSetupReportJson() const override
		{
			return FUnrealAgentMCPServer::GetCliSetupReportJson();
		}

		virtual FString GetToolDefinitionsJson() const override
		{
			return FUnrealAgentMCPServer::GetToolDefinitionsJson();
		}

		virtual FString GetResourceListJson() const override
		{
			return FUnrealAgentMCPServer::GetResourceListJson();
		}

		virtual FString ReadResource(const FString& Uri) const override
		{
			return FUnrealAgentMCPServer::ReadResource(Uri);
		}

		virtual FString GetMcpUrl() const override
		{
			return FUnrealAgentMCPServer::GetMcpUrl();
		}

		virtual FString GetServerName() const override
		{
			return FUnrealAgentMCPServer::GetServerName();
		}

		virtual FString GetAccessTokenHeaderName() const override
		{
			return FUnrealAgentMCPServer::GetAccessTokenHeaderName();
		}

		virtual FString GetAccessToken() const override
		{
			return FUnrealAgentMCPServer::GetAccessToken();
		}

		virtual FString GetClientConfigFilePath() const override
		{
			return FUnrealAgentMCPServer::GetClientConfigFilePath();
		}

		virtual FString GetConnectionFilePath() const override
		{
			return FUnrealAgentMCPServer::GetConnectionFilePath();
		}

		virtual FString BuildClientConfigSnippet() const override
		{
			const FString Token = FUnrealAgentMCPServer::GetAccessToken();
			return FString::Printf(TEXT("{\n") TEXT("  \"mcpServers\": {\n") TEXT("    \"%s\": {\n") TEXT("      \"type\": \"http\",\n") TEXT("      \"url\": \"%s\",\n")
									   TEXT("      \"headers\": {\n") TEXT("        \"%s\": \"%s\"\n") TEXT("      },\n") TEXT("      \"tool_timeout_sec\": 120,\n")
										   TEXT("      \"generatedBy\": \"UnrealAgent\",\n") TEXT("      \"projectId\": \"%s\"\n") TEXT("    }\n") TEXT("  }\n") TEXT("}"),
				*FUnrealAgentMCPServer::GetServerName(), *FUnrealAgentMCPServer::GetMcpUrl(), *FUnrealAgentMCPServer::GetAccessTokenHeaderName(), *Token,
				*FUnrealAgentMCPServer::GetProjectId());
		}
	};
}

TSharedRef<IUnrealAgentMCPApplicationService> CreateUnrealAgentMCPApplicationService()
{
	return MakeShared<FUnrealAgentMCPApplicationService>();
}
