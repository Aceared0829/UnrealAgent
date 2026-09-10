// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServerConfiguration.cpp
 * @brief MCP 服务持久化配置与客户端连接文件编排。
 */

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

#include "Infrastructure/Configuration/UnrealAgentMCPClientConfiguration.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

using namespace UnrealAgentMCP;

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentMCPConfiguration, Log, All);

namespace
{
	ClientConfiguration::FMcpClientConnectionDescriptor MakeConnectionDescriptor(const FString& ProtocolVersion)
	{
		ClientConfiguration::FMcpClientConnectionDescriptor Connection;
		Connection.ProjectName = GetProjectName();
		Connection.ProjectId = FUnrealAgentMCPServer::GetProjectId();
		Connection.ServerName = FUnrealAgentMCPServer::GetServerName();
		Connection.Url = FUnrealAgentMCPServer::GetMcpUrl();
		Connection.ProtocolVersion = ProtocolVersion;
		Connection.AccessTokenHeaderName = FUnrealAgentMCPServer::GetAccessTokenHeaderName();
		Connection.AccessToken = FUnrealAgentMCPServer::GetAccessToken();
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent"));
		if (Plugin.IsValid())
		{
#if PLATFORM_WINDOWS
			FString HostExecutable = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory(), TEXT("UnrealAgentMCPHost.exe"));
			HostExecutable = FPaths::ConvertRelativePathToFull(HostExecutable);
			FPaths::MakePlatformFilename(HostExecutable);
			if (FPaths::FileExists(HostExecutable))
			{
				Connection.Command = HostExecutable;
				Connection.Arguments.Add(TEXT("--project=") + ServerEnvironment::GetProjectFilePath());
			}
#endif
		}
		return Connection;
	}

	TSharedRef<FJsonObject> DescribeConfigurationFile(const FString& Path, const FString& Purpose, const TArray<FString>& ExpectedMarkers = {})
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Path);
		Item->SetStringField(TEXT("purpose"), Purpose);
		const bool bExists = !Path.IsEmpty() && FPaths::FileExists(Path);
		Item->SetBoolField(TEXT("exists"), bExists);
		if (!ExpectedMarkers.IsEmpty())
		{
			FString Content;
			const bool bReadable = bExists && FFileHelper::LoadFileToString(Content, *Path);
			const bool bMatches = bReadable &&
				Algo::AllOf(ExpectedMarkers,
					[&Content](const FString& Marker)
					{
						return Content.Contains(Marker);
					});
			Item->SetBoolField(TEXT("matchesCurrentConnection"), bMatches);
			Item->SetBoolField(TEXT("stale"), bExists && !bMatches);
		}
		return Item;
	}
}

void FUnrealAgentMCPServer::SaveConfiguredPort(const int32 Port)
{
	const FString Token = GetAccessToken();
	const FString ProtocolVersion = GetNegotiatedProtocolVersionSnapshot();

	TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetNumberField(TEXT("mcpPort"), Port);
	Config->SetStringField(TEXT("projectId"), GetProjectId());
	Config->SetStringField(TEXT("projectName"), GetProjectName());
	Config->SetStringField(TEXT("serverName"), GetServerName());
	Config->SetStringField(TEXT("instanceId"), InstanceId);
	Config->SetStringField(TEXT("loadedBuildId"), GetLoadedBuildId());
	Config->SetStringField(TEXT("url"), GetMcpUrl());
	Config->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
	Config->SetStringField(TEXT("protocol"), ProtocolVersion);
	Config->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
	Config->SetArrayField(TEXT("supportedProtocolVersions"), ServerEnvironment::MakeSupportedProtocolVersionsArray());
	Config->SetStringField(TEXT("accessToken"), Token);
	Config->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Config->SetObjectField(TEXT("headers"), ServerEnvironment::MakeAuthHeadersObject(GetAccessTokenHeaderName(), Token));
	Config->SetBoolField(TEXT("requiresAccessToken"), true);
	Config->SetBoolField(TEXT("requiresSessionHeader"), true);
	Config->SetStringField(TEXT("uproject"), ServerEnvironment::GetProjectFilePath());
	Config->SetStringField(TEXT("projectDir"), ServerEnvironment::GetProjectDirectory());
	Config->SetStringField(TEXT("writtenAtUtc"), FDateTime::UtcNow().ToIso8601());
	if (!ServerEnvironment::WriteStringAtomically(JsonObjectToString(Config, true), ServerEnvironment::GetSavedConfigPath(), true))
	{
		UE_LOG(LogUnrealAgentMCPConfiguration, Error, TEXT("Failed to persist MCP configuration with owner-only access."));
	}
}

void FUnrealAgentMCPServer::WriteClientConfig()
{
	if (BoundPort <= 0)
	{
		return;
	}

	const ClientConfiguration::FMcpClientConnectionDescriptor Connection = MakeConnectionDescriptor(GetNegotiatedProtocolVersionSnapshot());
	auto WriteConfigAtPath = [&Connection](const FString& ConfigurationPath)
	{
		const TSharedPtr<FJsonObject> ExistingRoot = ServerEnvironment::LoadJsonObjectFile(ConfigurationPath);
		const TSharedRef<FJsonObject> MergedRoot = ClientConfiguration::MergeJsonClientConfiguration(ExistingRoot, Connection);
		if (!ServerEnvironment::WriteStringAtomically(JsonObjectToString(MergedRoot, true), ConfigurationPath, true))
		{
			UE_LOG(LogUnrealAgentMCPConfiguration, Warning, TEXT("Failed to write MCP client config: %s"), *ConfigurationPath);
		}
	};

	WriteConfigAtPath(GetClientConfigFilePath());
	WriteConfigAtPath(ServerEnvironment::GetCursorClientConfigPath());
}

void FUnrealAgentMCPServer::WriteCodexClientConfig()
{
	if (BoundPort <= 0)
	{
		return;
	}

	const FString ConfigPath = GetCodexClientConfigFilePath();
	if (ConfigPath.IsEmpty())
	{
		UE_LOG(LogUnrealAgentMCPConfiguration, Warning, TEXT("Cannot locate the user home directory; skipped Codex CLI config."));
		return;
	}

	FString ExistingConfiguration;
	FFileHelper::LoadFileToString(ExistingConfiguration, *ConfigPath);
	const ClientConfiguration::FMcpClientConnectionDescriptor Connection = MakeConnectionDescriptor(GetNegotiatedProtocolVersionSnapshot());
	const FString MergedConfiguration = ClientConfiguration::MergeCodexTomlConfiguration(ExistingConfiguration, Connection);
	if (!ServerEnvironment::WriteStringAtomically(MergedConfiguration, ConfigPath, true))
	{
		UE_LOG(LogUnrealAgentMCPConfiguration, Warning, TEXT("Failed to write Codex CLI MCP config: %s"), *ConfigPath);
	}
}

void FUnrealAgentMCPServer::WriteClaudeProjectSettings()
{
	if (BoundPort <= 0)
	{
		return;
	}

	const FString SettingsPath = GetClaudeSettingsFilePath();
	const TSharedPtr<FJsonObject> ExistingRoot = ServerEnvironment::LoadJsonObjectFile(SettingsPath);
	bool bAlreadyEnabled = false;
	if (ExistingRoot->TryGetBoolField(TEXT("enableAllProjectMcpServers"), bAlreadyEnabled) && bAlreadyEnabled)
	{
		return;
	}

	const TSharedRef<FJsonObject> MergedRoot = ClientConfiguration::MergeClaudeProjectSettings(ExistingRoot);
	if (!ServerEnvironment::WriteStringAtomically(JsonObjectToString(MergedRoot, true), SettingsPath))
	{
		UE_LOG(LogUnrealAgentMCPConfiguration, Warning, TEXT("Failed to write Claude Code project settings: %s"), *SettingsPath);
	}
}

FString FUnrealAgentMCPServer::GetCliSetupReportJson()
{
	const ClientConfiguration::FMcpClientConnectionDescriptor Connection = MakeConnectionDescriptor(GetNegotiatedProtocolVersionSnapshot());
	const FString TransportMarker = Connection.UsesStdioHost() ? FPaths::GetCleanFilename(Connection.Command) : Connection.Url;
	const TArray<FString> CurrentConnectionMarkers = { Connection.ServerName, TransportMarker };
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("serverRunning"), IsRunning());
	Report->SetStringField(TEXT("serverState"), GetRunStateName());
	Report->SetStringField(TEXT("instanceId"), InstanceId);
	Report->SetStringField(TEXT("loadedBuildId"), GetLoadedBuildId());
	Report->SetStringField(TEXT("serverName"), GetServerName());
	Report->SetStringField(TEXT("url"), IsRunning() ? GetMcpUrl() : TEXT(""));
	Report->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Report->SetStringField(TEXT("preferredTransport"), Connection.UsesStdioHost() ? TEXT("stdio-host") : TEXT("http"));
	Report->SetStringField(TEXT("hostExecutable"), Connection.Command);

	TSharedRef<FJsonObject> Clients = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> Claude = MakeShared<FJsonObject>();
	Claude->SetObjectField(TEXT("mcpConfig"), DescribeConfigurationFile(GetClientConfigFilePath(), TEXT("Project-scoped MCP servers (.mcp.json)."), CurrentConnectionMarkers));
	Claude->SetObjectField(TEXT("settings"), DescribeConfigurationFile(GetClaudeSettingsFilePath(), TEXT("enableAllProjectMcpServers pre-approval.")));
	Claude->SetStringField(TEXT("usage"), TEXT("Run `claude` inside the project directory; the server is picked up automatically."));
	Clients->SetObjectField(TEXT("claudeCode"), Claude);

	TSharedRef<FJsonObject> Cursor = MakeShared<FJsonObject>();
	Cursor->SetObjectField(TEXT("mcpConfig"),
		DescribeConfigurationFile(ServerEnvironment::GetCursorClientConfigPath(), TEXT("Cursor user MCP servers (~/.cursor/mcp.json)."), CurrentConnectionMarkers));
	Cursor->SetStringField(TEXT("usage"), TEXT("Run `cursor-agent` (or open Cursor) with this project as the workspace root."));
	Clients->SetObjectField(TEXT("cursor"), Cursor);

	TSharedRef<FJsonObject> Codex = MakeShared<FJsonObject>();
	Codex->SetObjectField(TEXT("mcpConfig"),
		DescribeConfigurationFile(GetCodexClientConfigFilePath(), TEXT("Global Codex MCP servers (~/.codex/config.toml)."), CurrentConnectionMarkers));
	Codex->SetStringField(TEXT("usage"), TEXT("Run `codex` anywhere; restart any running codex session after refresh so it reloads config.toml."));
	Clients->SetObjectField(TEXT("codex"), Codex);
	Report->SetObjectField(TEXT("clients"), Clients);

	TArray<TSharedPtr<FJsonValue>> Notes;
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Saved connection files are refreshed automatically; external Codex, Cursor, and Claude configuration changes only when Configure External Clients is requested.")));
	Notes.Add(MakeShared<FJsonValueString>(TEXT("The access token persists across editor restarts, so existing CLI sessions keep working after a relaunch.")));
	Notes.Add(MakeShared<FJsonValueString>(TEXT("Multiple CLI clients can stay connected at the same time; each gets its own MCP session.")));
	Report->SetArrayField(TEXT("notes"), Notes);
	return JsonObjectToString(Report, true);
}

bool FUnrealAgentMCPServer::WriteProjectConnectionFile(FString& OutError)
{
	OutError.Empty();
	if (BoundPort <= 0)
	{
		OutError = TEXT("MCP connection manifest cannot be written before a port is bound.");
		return false;
	}

	const FString Token = GetAccessToken();
	const FString ProtocolVersion = GetNegotiatedProtocolVersionSnapshot();
	TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("projectName"), GetProjectName());
	Info->SetStringField(TEXT("projectId"), GetProjectId());
	Info->SetStringField(TEXT("serverName"), GetServerName());
	Info->SetStringField(TEXT("instanceId"), InstanceId);
	Info->SetStringField(TEXT("loadedBuildId"), GetLoadedBuildId());
	Info->SetStringField(TEXT("state"), GetRunStateName());
	Info->SetStringField(TEXT("url"), GetMcpUrl());
	Info->SetNumberField(TEXT("port"), BoundPort);
	Info->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
	Info->SetStringField(TEXT("protocol"), ProtocolVersion);
	Info->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
	Info->SetArrayField(TEXT("supportedProtocolVersions"), ServerEnvironment::MakeSupportedProtocolVersionsArray());
	Info->SetStringField(TEXT("accessToken"), Token);
	Info->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Info->SetObjectField(TEXT("headers"), ServerEnvironment::MakeAuthHeadersObject(GetAccessTokenHeaderName(), Token));
	Info->SetBoolField(TEXT("requiresAccessToken"), true);
	Info->SetBoolField(TEXT("requiresSessionHeader"), true);
	Info->SetBoolField(TEXT("running"), IsRunning());
	Info->SetStringField(TEXT("uproject"), ServerEnvironment::GetProjectFilePath());
	Info->SetStringField(TEXT("projectDir"), ServerEnvironment::GetProjectDirectory());
	Info->SetStringField(TEXT("startedAtUtc"), StartedAtUtc.GetTicks() > 0 ? StartedAtUtc.ToIso8601() : TEXT(""));
	Info->SetStringField(TEXT("lastHealthCheckAtUtc"), LastHealthCheckAtUtc.GetTicks() > 0 ? LastHealthCheckAtUtc.ToIso8601() : TEXT(""));
	Info->SetStringField(TEXT("writtenAtUtc"), FDateTime::UtcNow().ToIso8601());
	if (!ServerEnvironment::WriteStringAtomically(JsonObjectToString(Info, true), ServerEnvironment::GetConnectionPath(), true, OutError))
	{
		UE_LOG(LogUnrealAgentMCPConfiguration, Error, TEXT("Failed to persist MCP connection file with owner-only access: %s"), *OutError);
		return false;
	}
	return true;
}
