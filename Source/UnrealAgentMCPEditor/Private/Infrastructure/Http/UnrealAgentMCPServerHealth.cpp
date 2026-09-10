// Copyright ZhaoZining. All Rights Reserved.

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "IPAddress.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

namespace
{
	constexpr float HealthCheckIntervalSeconds = 5.0f;
	constexpr int32 FailedChecksBeforeRecovery = 2;
	const FTimespan HealthManifestWriteInterval = FTimespan::FromSeconds(30.0);
}

FString FUnrealAgentMCPServer::GetRunStateName()
{
	switch (RunState)
	{
	case ERunState::Starting:
		return TEXT("starting");
	case ERunState::Listening:
		return TEXT("listening");
	case ERunState::Degraded:
		return TEXT("degraded");
	default:
		return TEXT("stopped");
	}
}

FString FUnrealAgentMCPServer::GetLoadedBuildId()
{
	const FString ModuleFilename = FModuleManager::Get().GetModuleFilename(TEXT("UnrealAgentMCPEditor"));
	if (ModuleFilename.IsEmpty())
	{
		return TEXT("unknown");
	}
	const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*ModuleFilename);
	const int64 FileSize = IFileManager::Get().FileSize(*ModuleFilename);
	return FString::Printf(TEXT("%s:%lld:%lld"), *FPaths::GetCleanFilename(ModuleFilename), Timestamp.GetTicks(), FileSize);
}

bool FUnrealAgentMCPServer::HasBoundListenerState()
{
	return BoundPort > 0 && HttpRouter.IsValid() && RouteHandles.Num() == 4;
}

bool FUnrealAgentMCPServer::ProbeLoopbackPort(const int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}
	TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
	bool bAddressValid = false;
	Address->SetIp(TEXT("127.0.0.1"), bAddressValid);
	Address->SetPort(Port);
	if (!bAddressValid)
	{
		return false;
	}

	FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealAgentMCPHealthCheck"), Address->GetProtocolType());
	if (!Socket)
	{
		return false;
	}
	Socket->SetNonBlocking(true);
	bool bConnected = Socket->Connect(*Address);
	if (!bConnected && Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(50.0)))
	{
		bConnected = Socket->GetConnectionState() == SCS_Connected;
	}
	SocketSubsystem->DestroySocket(Socket);
	return bConnected;
}

bool FUnrealAgentMCPServer::ApplyHealthProbeResult(const int32 ProbedPort, const bool bReachable)
{
	LastHealthCheckAtUtc = FDateTime::UtcNow();
	if (ProbedPort != BoundPort)
	{
		return true;
	}
	if (bReachable)
	{
		ConsecutiveHealthCheckFailures = 0;
		HealthRecoveryAttempts = 0;
		bRunning = true;
		RunState = ERunState::Listening;
		LastError.Empty();
		if (LastHealthManifestWriteAtUtc.GetTicks() <= 0 || LastHealthCheckAtUtc - LastHealthManifestWriteAtUtc >= HealthManifestWriteInterval)
		{
			FString PersistenceError;
			if (!WriteProjectConnectionFile(PersistenceError))
			{
				bRunning = false;
				RunState = ERunState::Degraded;
				LastError = PersistenceError;
				return true;
			}
			LastHealthManifestWriteAtUtc = LastHealthCheckAtUtc;
		}
		return true;
	}

	++ConsecutiveHealthCheckFailures;
	if (ConsecutiveHealthCheckFailures < FailedChecksBeforeRecovery)
	{
		return true;
	}

	bRunning = false;
	RunState = ERunState::Degraded;
	LastError = FString::Printf(TEXT("MCP listener on 127.0.0.1:%d failed %d consecutive health checks."), BoundPort, ConsecutiveHealthCheckFailures);
	IFileManager::Get().Delete(*UnrealAgentMCP::ServerEnvironment::GetConnectionPath(), false, true, true);

	if (HealthRecoveryAttempts >= 1 || BoundPort <= 0)
	{
		return true;
	}

	const int32 RecoveryPort = BoundPort;
	++HealthRecoveryAttempts;
	bHealthRecoveryInProgress = true;
	Stop();
	Start(RecoveryPort);
	bHealthRecoveryInProgress = false;
	return false;
}

bool FUnrealAgentMCPServer::HandleHealthTick(const float)
{
	if (HealthProbeFuture.IsValid())
	{
		if (!HealthProbeFuture.IsReady())
		{
			return true;
		}
		const bool bReachable = HealthProbeFuture.Get();
		HealthProbeFuture = TFuture<bool>();
		return ApplyHealthProbeResult(HealthProbePort, bReachable);
	}

	if (!HasBoundListenerState())
	{
		return ApplyHealthProbeResult(BoundPort, false);
	}

	HealthProbePort = BoundPort;
	HealthProbeFuture = Async(EAsyncExecution::ThreadPool,
		[Port = HealthProbePort]()
		{
			return ProbeLoopbackPort(Port);
		});
	return true;
}

void FUnrealAgentMCPServer::StartHealthMonitoring()
{
	StopHealthMonitoring();
	HealthTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FUnrealAgentMCPServer::HandleHealthTick), HealthCheckIntervalSeconds);
}

void FUnrealAgentMCPServer::StopHealthMonitoring()
{
	if (HealthTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HealthTickerHandle);
		HealthTickerHandle = FTSTicker::FDelegateHandle();
	}
	if (HealthProbeFuture.IsValid())
	{
		HealthProbeFuture.Wait();
		HealthProbeFuture = TFuture<bool>();
	}
	HealthProbePort = 0;
}
