// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServer.cpp
 * @brief MCP 服务生命周期、工程身份与顶层连接文件编排。
 */

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Core/Common/UnrealAgentMCPBrand.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

using namespace UnrealAgentMCP;

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentMCP, Log, All);

namespace
{
	TMap<FString, FUnrealAgentMCPToolApprovalHandler>& GetToolApprovalHandlers()
	{
		static TMap<FString, FUnrealAgentMCPToolApprovalHandler> Handlers;
		return Handlers;
	}

	TMap<FString, double>& GetLastMutationRequestSeconds()
	{
		static TMap<FString, double> LastRequests;
		return LastRequests;
	}
}

TSharedPtr<IHttpRouter> FUnrealAgentMCPServer::HttpRouter = nullptr;
TArray<FHttpRouteHandle> FUnrealAgentMCPServer::RouteHandles;
int32 FUnrealAgentMCPServer::BoundPort = 0;
bool FUnrealAgentMCPServer::bRunning = false;
FUnrealAgentMCPServer::ERunState FUnrealAgentMCPServer::RunState = FUnrealAgentMCPServer::ERunState::Stopped;
FTSTicker::FDelegateHandle FUnrealAgentMCPServer::HealthTickerHandle;
int32 FUnrealAgentMCPServer::ConsecutiveHealthCheckFailures = 0;
int32 FUnrealAgentMCPServer::HealthRecoveryAttempts = 0;
bool FUnrealAgentMCPServer::bHealthRecoveryInProgress = false;
TFuture<bool> FUnrealAgentMCPServer::HealthProbeFuture;
int32 FUnrealAgentMCPServer::HealthProbePort = 0;
FString FUnrealAgentMCPServer::InstanceId;
FDateTime FUnrealAgentMCPServer::LastHealthCheckAtUtc;
FDateTime FUnrealAgentMCPServer::LastHealthManifestWriteAtUtc;
FUnrealAgentMCPReadiness FUnrealAgentMCPServer::LastReadiness;
Protocol::FMcpSessionRegistry FUnrealAgentMCPServer::SessionRegistry(16);
TMap<FString, TSharedPtr<FHttpResultCallback>> FUnrealAgentMCPServer::SseCallbacks;
bool FUnrealAgentMCPServer::bToolsListChangedBroadcastScheduled = false;
uint64 FUnrealAgentMCPServer::ToolsListChangedScheduleSerial = 0;
FString FUnrealAgentMCPServer::NegotiatedProtocolVersion(Protocol::GetLatestProtocolVersion());
FString FUnrealAgentMCPServer::AccessToken;
FString FUnrealAgentMCPServer::LastError;
FDateTime FUnrealAgentMCPServer::StartedAtUtc;
FDateTime FUnrealAgentMCPServer::LastRefreshAtUtc;

FCriticalSection& FUnrealAgentMCPServer::GetStateMutex()
{
	static FCriticalSection StateMutex;
	return StateMutex;
}

void FUnrealAgentMCPServer::RegisterToolApprovalHandler(const FString& ClientId, FUnrealAgentMCPToolApprovalHandler Handler)
{
	if (ClientId.IsEmpty() || !Handler.IsBound())
	{
		return;
	}
	FScopeLock Lock(&GetStateMutex());
	GetToolApprovalHandlers().Add(ClientId, MoveTemp(Handler));
}

void FUnrealAgentMCPServer::UnregisterToolApprovalHandler(const FString& ClientId)
{
	if (ClientId.IsEmpty())
	{
		return;
	}
	FScopeLock Lock(&GetStateMutex());
	GetToolApprovalHandlers().Remove(ClientId);
	GetLastMutationRequestSeconds().Remove(ClientId);
}

int32 FUnrealAgentMCPServer::RevokeToolApprovalClient(const FString& ClientId, FString Reason)
{
	UnregisterToolApprovalHandler(ClientId);
	return CancelToolTasksByClientId(ClientId, MoveTemp(Reason));
}

bool FUnrealAgentMCPServer::TryAcquireAcpMutationPermit(const FString& ClientId)
{
	if (ClientId.IsEmpty())
	{
		return false;
	}
	constexpr double MinimumMutationIntervalSeconds = 0.1;
	const double NowSeconds = FPlatformTime::Seconds();
	FScopeLock Lock(&GetStateMutex());
	double& LastRequestSeconds = GetLastMutationRequestSeconds().FindOrAdd(ClientId);
	if (LastRequestSeconds > 0.0 && NowSeconds - LastRequestSeconds < MinimumMutationIntervalSeconds)
	{
		return false;
	}
	LastRequestSeconds = NowSeconds;
	return true;
}

bool FUnrealAgentMCPServer::FindToolApprovalHandler(const FString& ClientId, FUnrealAgentMCPToolApprovalHandler& OutHandler)
{
	OutHandler.Unbind();
	if (ClientId.IsEmpty())
	{
		return false;
	}
	FScopeLock Lock(&GetStateMutex());
	const FUnrealAgentMCPToolApprovalHandler* Handler = GetToolApprovalHandlers().Find(ClientId);
	if (!Handler || !Handler->IsBound())
	{
		return false;
	}
	OutHandler = *Handler;
	return true;
}

FString FUnrealAgentMCPServer::GetNegotiatedProtocolVersionSnapshot()
{
	FScopeLock Lock(&GetStateMutex());
	return NegotiatedProtocolVersion;
}

void FUnrealAgentMCPServer::Start(const int32 Port)
{
	if (bRunning)
	{
		if (HasBoundListenerState())
		{
			RunState = ERunState::Listening;
			LastError.Empty();
			return;
		}
		LastError = TEXT("MCP listener state was stale; rebuilding the loopback listener.");
		Stop();
	}
	else if (HttpRouter.IsValid() || !RouteHandles.IsEmpty())
	{
		Stop();
	}

	RunState = ERunState::Starting;
	LastReadiness = FUnrealAgentMCPReadiness();
	if (!bHealthRecoveryInProgress)
	{
		HealthRecoveryAttempts = 0;
	}
	LastError.Empty();
	CloseAllSseStreams(TEXT("server_restarting"));
	RouteHandles.Reset();
	BoundPort = 0;
	HttpRouter.Reset();
	IFileManager::Get().Delete(*ServerEnvironment::GetConnectionPath(), false, true, true);
	{
		FScopeLock Lock(&GetStateMutex());
		SessionRegistry.Reset();
		SseCallbacks.Reset();
		bToolsListChangedBroadcastScheduled = false;
		++ToolsListChangedScheduleSerial;
		NegotiatedProtocolVersion = Protocol::GetLatestProtocolVersion();
		AccessToken.Empty();
	}
	EnsureAccessToken();

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	for (int32 PortOffset = 0; PortOffset < 10; ++PortOffset)
	{
		const int32 CandidatePort = Port + PortOffset;
		HttpRouter = HttpModule.GetHttpRouter(CandidatePort, true);
		if (HttpRouter.IsValid())
		{
			BoundPort = CandidatePort;
			break;
		}
	}

	if (!HttpRouter.IsValid())
	{
		RunState = ERunState::Degraded;
		LastError = FString::Printf(TEXT("Failed to bind MCP HTTP server on ports %d-%d."), Port, Port + 9);
		UE_LOG(LogUnrealAgentMCP, Error, TEXT("%s"), *LastError);
		return;
	}

	RouteHandles.Add(HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateStatic(&FUnrealAgentMCPServer::HandleMCPPost)));
	RouteHandles.Add(HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateStatic(&FUnrealAgentMCPServer::HandleMCPGet)));
	RouteHandles.Add(
		HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_OPTIONS, FHttpRequestHandler::CreateStatic(&FUnrealAgentMCPServer::HandleMCPOptions)));
	RouteHandles.Add(
		HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_DELETE, FHttpRequestHandler::CreateStatic(&FUnrealAgentMCPServer::HandleMCPDelete)));

	const bool bAllRoutesBound = RouteHandles.Num() == 4 &&
		Algo::AllOf(RouteHandles,
			[](const FHttpRouteHandle& RouteHandle)
			{
				return RouteHandle.IsValid();
			});
	if (!bAllRoutesBound)
	{
		RunState = ERunState::Degraded;
		LastError = FString::Printf(TEXT("Failed to bind one or more /mcp routes on port %d."), BoundPort);
		UE_LOG(LogUnrealAgentMCP, Error, TEXT("%s"), *LastError);

		for (const FHttpRouteHandle& RouteHandle : RouteHandles)
		{
			if (HttpRouter.IsValid() && RouteHandle.IsValid())
			{
				HttpRouter->UnbindRoute(RouteHandle);
			}
		}
		RouteHandles.Reset();
		HttpRouter.Reset();
		BoundPort = 0;
		return;
	}

	HttpModule.StartAllListeners();
	bRunning = true;
	RunState = ERunState::Listening;
	InstanceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	LastHealthCheckAtUtc = FDateTime::UtcNow();
	LastHealthManifestWriteAtUtc = LastHealthCheckAtUtc;
	ConsecutiveHealthCheckFailures = 0;
	StartedAtUtc = FDateTime::UtcNow();
	RefreshConnectionFiles();
	if (!LastError.IsEmpty() || !IFileManager::Get().FileExists(*ServerEnvironment::GetConnectionPath()))
	{
		bRunning = false;
		RunState = ERunState::Degraded;
		if (LastError.IsEmpty())
		{
			LastError = TEXT("MCP listener started but Saved/UnrealAgent/mcp.json was not persisted.");
		}
		UE_LOG(LogUnrealAgentMCP, Error, TEXT("%s"), *LastError);
		return;
	}
	StartHealthMonitoring();
	UE_LOG(LogUnrealAgentMCP, Log, TEXT("%s MCP server '%s' listening at %s"), UnrealAgentMCP::Brand::ProductName, *GetServerName(), *GetMcpUrl());
}

void FUnrealAgentMCPServer::Stop()
{
	StopHealthMonitoring();
	CloseAllSseStreams(TEXT("server_stopping"));
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& RouteHandle : RouteHandles)
		{
			if (RouteHandle.IsValid())
			{
				HttpRouter->UnbindRoute(RouteHandle);
			}
		}
	}

	RouteHandles.Reset();
	HttpRouter.Reset();
	BoundPort = 0;
	bRunning = false;
	RunState = ERunState::Stopped;
	ConsecutiveHealthCheckFailures = 0;
	InstanceId.Empty();
	LastHealthCheckAtUtc = FDateTime();
	LastHealthManifestWriteAtUtc = FDateTime();
	LastReadiness = FUnrealAgentMCPReadiness();
	StartedAtUtc = FDateTime();
	{
		FScopeLock Lock(&GetStateMutex());
		SessionRegistry.Reset();
		SseCallbacks.Reset();
		bToolsListChangedBroadcastScheduled = false;
		++ToolsListChangedScheduleSerial;
		NegotiatedProtocolVersion = Protocol::GetLatestProtocolVersion();
		AccessToken.Empty();
	}
	IFileManager::Get().Delete(*ServerEnvironment::GetConnectionPath(), false, true, true);
}

bool FUnrealAgentMCPServer::IsRunning()
{
	return bRunning && RunState == ERunState::Listening;
}

int32 FUnrealAgentMCPServer::GetPort()
{
	return BoundPort;
}

int32 FUnrealAgentMCPServer::LoadConfiguredPort()
{
	const TSharedPtr<FJsonObject> Json = ServerEnvironment::LoadJsonObjectFile(ServerEnvironment::GetSavedConfigPath());
	double SavedPort = 0.0;
	FString SavedProjectId;
	if (Json->TryGetNumberField(TEXT("mcpPort"), SavedPort) && SavedPort > 0.0 && Json->TryGetStringField(TEXT("projectId"), SavedProjectId) && SavedProjectId == GetProjectId())
	{
		return static_cast<int32>(SavedPort);
	}
	return ServerEnvironment::GetDefaultPort();
}

FString FUnrealAgentMCPServer::GetServerName()
{
	return FString::Printf(TEXT("world_data_%s_%s"), *ServerEnvironment::SanitizeNamePart(GetProjectName()), *ServerEnvironment::GetProjectHashString());
}

FString FUnrealAgentMCPServer::GetProjectId()
{
	return FString::Printf(TEXT("%s_%s"), *ServerEnvironment::SanitizeNamePart(GetProjectName()), *ServerEnvironment::GetProjectHashString());
}

FString FUnrealAgentMCPServer::GetMcpUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), BoundPort);
}

FString FUnrealAgentMCPServer::GetAccessTokenHeaderName()
{
	return TEXT("X-WorldData-MCP-Token");
}

void FUnrealAgentMCPServer::EnsureAccessToken()
{
	FScopeLock Lock(&GetStateMutex());
	if (!AccessToken.IsEmpty())
	{
		return;
	}

	const TSharedPtr<FJsonObject> Config = ServerEnvironment::LoadJsonObjectFile(ServerEnvironment::GetSavedConfigPath());
	FString SavedProjectId;
	FString SavedToken;
	if (Config->TryGetStringField(TEXT("projectId"), SavedProjectId) && SavedProjectId == GetProjectId() && Config->TryGetStringField(TEXT("accessToken"), SavedToken) &&
		ServerEnvironment::IsStrongAccessToken(SavedToken))
	{
		AccessToken = SavedToken;
	}
	else
	{
		AccessToken = ServerEnvironment::GenerateAccessToken();
	}
}

FString FUnrealAgentMCPServer::GetAccessToken()
{
	EnsureAccessToken();
	FScopeLock Lock(&GetStateMutex());
	return AccessToken;
}

FString FUnrealAgentMCPServer::GetProjectInfoJson()
{
	TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetBoolField(TEXT("success"), true);
	Info->SetStringField(TEXT("projectName"), GetProjectName());
	Info->SetStringField(TEXT("projectId"), GetProjectId());
	Info->SetStringField(TEXT("serverName"), GetServerName());
	Info->SetStringField(TEXT("url"), GetMcpUrl());
	Info->SetNumberField(TEXT("port"), BoundPort);
	Info->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
	Info->SetStringField(TEXT("protocolVersion"), GetNegotiatedProtocolVersionSnapshot());
	Info->SetArrayField(TEXT("supportedProtocolVersions"), ServerEnvironment::MakeSupportedProtocolVersionsArray());
	Info->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Info->SetBoolField(TEXT("requiresAccessToken"), true);
	Info->SetBoolField(TEXT("requiresSessionHeader"), true);
	Info->SetStringField(TEXT("uproject"), ServerEnvironment::GetProjectFilePath());
	Info->SetStringField(TEXT("projectDir"), ServerEnvironment::GetProjectDirectory());
	Info->SetBoolField(TEXT("running"), IsRunning());
	Info->SetStringField(TEXT("state"), GetRunStateName());
	Info->SetStringField(TEXT("instanceId"), InstanceId);
	Info->SetStringField(TEXT("loadedBuildId"), GetLoadedBuildId());
	Info->SetStringField(TEXT("lastHealthCheckAtUtc"), LastHealthCheckAtUtc.GetTicks() > 0 ? LastHealthCheckAtUtc.ToIso8601() : TEXT(""));
	Info->SetStringField(TEXT("startedAtUtc"), StartedAtUtc.GetTicks() > 0 ? StartedAtUtc.ToIso8601() : TEXT(""));
	Info->SetStringField(TEXT("lastRefreshAtUtc"), LastRefreshAtUtc.GetTicks() > 0 ? LastRefreshAtUtc.ToIso8601() : TEXT(""));
	Info->SetStringField(TEXT("lastError"), LastError);
	Info->SetStringField(TEXT("clientConfigFile"), GetClientConfigFilePath());
	Info->SetStringField(TEXT("cursorClientConfigFile"), ServerEnvironment::GetCursorClientConfigPath());
	Info->SetStringField(TEXT("savedConfigFile"), GetSavedConfigFilePath());
	Info->SetStringField(TEXT("connectionFile"), GetConnectionFilePath());
	return JsonObjectToString(Info);
}

FString FUnrealAgentMCPServer::GetStatusJson()
{
	bool bListChangedBroadcastScheduled = false;
	{
		FScopeLock Lock(&GetStateMutex());
		bListChangedBroadcastScheduled = bToolsListChangedBroadcastScheduled;
	}
	TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
	Status->SetBoolField(TEXT("running"), IsRunning());
	Status->SetStringField(TEXT("state"), GetRunStateName());
	Status->SetStringField(TEXT("instanceId"), InstanceId);
	Status->SetStringField(TEXT("loadedBuildId"), GetLoadedBuildId());
	Status->SetStringField(TEXT("serverName"), GetServerName());
	Status->SetStringField(TEXT("projectId"), GetProjectId());
	Status->SetNumberField(TEXT("port"), BoundPort);
	Status->SetStringField(TEXT("url"), IsRunning() ? GetMcpUrl() : TEXT(""));
	Status->SetNumberField(TEXT("routeCount"), RouteHandles.Num());
	Status->SetStringField(TEXT("protocolVersion"), GetNegotiatedProtocolVersionSnapshot());
	Status->SetArrayField(TEXT("supportedProtocolVersions"), ServerEnvironment::MakeSupportedProtocolVersionsArray());
	Status->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Status->SetBoolField(TEXT("requiresAccessToken"), true);
	Status->SetBoolField(TEXT("requiresSessionHeader"), true);
	Status->SetBoolField(TEXT("toolsListChangedBroadcastScheduled"), bListChangedBroadcastScheduled);
	Status->SetStringField(TEXT("startedAtUtc"), StartedAtUtc.GetTicks() > 0 ? StartedAtUtc.ToIso8601() : TEXT(""));
	Status->SetStringField(TEXT("lastRefreshAtUtc"), LastRefreshAtUtc.GetTicks() > 0 ? LastRefreshAtUtc.ToIso8601() : TEXT(""));
	Status->SetStringField(TEXT("lastHealthCheckAtUtc"), LastHealthCheckAtUtc.GetTicks() > 0 ? LastHealthCheckAtUtc.ToIso8601() : TEXT(""));
	Status->SetStringField(TEXT("lastError"), LastError);
	Status->SetStringField(TEXT("clientConfigFile"), GetClientConfigFilePath());
	Status->SetStringField(TEXT("cursorClientConfigFile"), ServerEnvironment::GetCursorClientConfigPath());
	Status->SetStringField(TEXT("codexClientConfigFile"), GetCodexClientConfigFilePath());
	Status->SetStringField(TEXT("claudeSettingsFile"), GetClaudeSettingsFilePath());
	Status->SetStringField(TEXT("savedConfigFile"), GetSavedConfigFilePath());
	Status->SetStringField(TEXT("connectionFile"), GetConnectionFilePath());
	return JsonObjectToString(Status);
}

FString FUnrealAgentMCPServer::GetLastError()
{
	return LastError;
}

FDateTime FUnrealAgentMCPServer::GetStartedAtUtc()
{
	return StartedAtUtc;
}

FDateTime FUnrealAgentMCPServer::GetLastRefreshAtUtc()
{
	return LastRefreshAtUtc;
}

FString FUnrealAgentMCPServer::GetClientConfigFilePath()
{
	FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json"));
	Path = FPaths::ConvertRelativePathToFull(Path);
	FPaths::MakePlatformFilename(Path);
	return Path;
}

FString FUnrealAgentMCPServer::GetCodexClientConfigFilePath()
{
	FString Path = ServerEnvironment::GetCodexClientConfigPath();
	if (!Path.IsEmpty())
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::MakePlatformFilename(Path);
	}
	return Path;
}

FString FUnrealAgentMCPServer::GetClaudeSettingsFilePath()
{
	FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT(".claude"), TEXT("settings.local.json"));
	Path = FPaths::ConvertRelativePathToFull(Path);
	FPaths::MakePlatformFilename(Path);
	return Path;
}

FString FUnrealAgentMCPServer::GetSavedConfigFilePath()
{
	FString Path = FPaths::ConvertRelativePathToFull(ServerEnvironment::GetSavedConfigPath());
	FPaths::MakePlatformFilename(Path);
	return Path;
}

FString FUnrealAgentMCPServer::GetConnectionFilePath()
{
	FString Path = FPaths::ConvertRelativePathToFull(ServerEnvironment::GetConnectionPath());
	FPaths::MakePlatformFilename(Path);
	return Path;
}

void FUnrealAgentMCPServer::RefreshConnectionFiles()
{
	if (!IsRunning() || BoundPort <= 0)
	{
		if (LastError.IsEmpty())
		{
			LastError = TEXT("Cannot refresh MCP files because the server is not bound to a port.");
		}
		return;
	}

	SaveConfiguredPort(BoundPort);
	FString PersistenceError;
	if (!WriteProjectConnectionFile(PersistenceError))
	{
		LastError = PersistenceError;
		return;
	}
	LastRefreshAtUtc = FDateTime::UtcNow();
	LastError.Empty();
}

void FUnrealAgentMCPServer::ConfigureExternalClients()
{
	if (!IsRunning() || BoundPort <= 0)
	{
		if (LastError.IsEmpty())
		{
			LastError = TEXT("Cannot configure external MCP clients because the server is not bound to a port.");
		}
		return;
	}

	WriteClientConfig();
	WriteCodexClientConfig();
	WriteClaudeProjectSettings();
	LastRefreshAtUtc = FDateTime::UtcNow();
	LastError.Empty();
}
