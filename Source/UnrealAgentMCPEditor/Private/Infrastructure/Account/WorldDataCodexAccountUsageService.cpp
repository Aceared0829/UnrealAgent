// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file WorldDataCodexAccountUsageService.cpp
 * @brief Codex app-server 账户用量查询实现。
 */

#include "Infrastructure/Account/WorldDataCodexAccountUsageService.h"

#include "Application/CLI/WorldDataCliProcessRules.h"
#include "Async/Async.h"
#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/InteractiveProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString GetAccountUsageCommandInterpreterPath()
	{
#if PLATFORM_WINDOWS
		FString CommandInterpreter = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
		if (FPaths::FileExists(CommandInterpreter))
		{
			return CommandInterpreter;
		}

		CommandInterpreter = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("cmd.exe"));
		if (FPaths::FileExists(CommandInterpreter))
		{
			return CommandInterpreter;
		}
#endif
		return FString();
	}

	FString GetAccountUsagePowerShellPath()
	{
#if PLATFORM_WINDOWS
		const FString PowerShell =
			FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("WindowsPowerShell"), TEXT("v1.0"), TEXT("powershell.exe"));
		if (FPaths::FileExists(PowerShell))
		{
			return PowerShell;
		}
#endif
		return FString();
	}

	FString SerializeMessage(const TSharedRef<FJsonObject>& Message)
	{
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Message, Writer);
		return Json + TEXT("\n");
	}
}

FWorldDataCodexAccountUsageService::~FWorldDataCodexAccountUsageService()
{
	Cancel();
}

void FWorldDataCodexAccountUsageService::Cancel()
{
	TSharedPtr<FInteractiveProcess> ProcessToStop = MoveTemp(Process);
	if (ProcessToStop.IsValid())
	{
		ProcessToStop->Cancel(true);
	}
	StdoutBuffer.Empty();
}

void FWorldDataCodexAccountUsageService::Refresh(const FString& CodexCliPath)
{
	Cancel();
	bResultPublished = false;

	FWorldDataCliProcessLaunchSpec LaunchSpec;
	if (!WorldDataCliProcessRules::BuildLaunchSpec(CodexCliPath, TEXT("app-server --stdio"), GetAccountUsageCommandInterpreterPath(), GetAccountUsagePowerShellPath(), LaunchSpec))
	{
		PublishFailure(TEXT("未找到可启动的 Codex CLI。"));
		return;
	}

	FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDirectory);
	Process = MakeShared<FInteractiveProcess>(LaunchSpec.Executable, LaunchSpec.Arguments, WorkingDirectory, true, true);

	const TWeakPtr<FWorldDataCodexAccountUsageService> WeakSelf = AsShared();
	const TWeakPtr<FInteractiveProcess> WeakProcess = Process;
	Process->OnOutput().BindLambda(
		[WeakSelf, WeakProcess](const FString& Output)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, WeakProcess, Output]()
				{
					const TSharedPtr<FWorldDataCodexAccountUsageService> Self = WeakSelf.Pin();
					const TSharedPtr<FInteractiveProcess> SourceProcess = WeakProcess.Pin();
					if (Self.IsValid() && SourceProcess.IsValid() && Self->Process == SourceProcess)
					{
						Self->ConsumeOutput(Output);
					}
				});
		});
	Process->OnCompleted().BindLambda(
		[WeakSelf, WeakProcess](int32 ReturnCode, bool bCanceled)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, WeakProcess, ReturnCode, bCanceled]()
				{
					const TSharedPtr<FWorldDataCodexAccountUsageService> Self = WeakSelf.Pin();
					const TSharedPtr<FInteractiveProcess> CompletedProcess = WeakProcess.Pin();
					if (!Self.IsValid() || !CompletedProcess.IsValid() || Self->Process != CompletedProcess)
					{
						return;
					}
					Self->Process.Reset();
					if (!bCanceled && !Self->bResultPublished)
					{
						Self->PublishFailure(FString::Printf(TEXT("Codex 账户用量查询退出，返回码 %d。"), ReturnCode));
					}
				});
		});

	if (!Process->Launch())
	{
		Process.Reset();
		PublishFailure(TEXT("启动 Codex 账户用量查询失败。"));
		return;
	}

	TSharedRef<FJsonObject> Initialize = MakeShared<FJsonObject>();
	Initialize->SetNumberField(TEXT("id"), 1);
	Initialize->SetStringField(TEXT("method"), TEXT("initialize"));
	TSharedRef<FJsonObject> InitializeParams = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("worlddata-uebridge"));
	ClientInfo->SetStringField(TEXT("title"), UnrealAgentMCP::Brand::ProductName);
	ClientInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));
	InitializeParams->SetObjectField(TEXT("clientInfo"), ClientInfo);
	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetBoolField(TEXT("experimentalApi"), true);
	InitializeParams->SetObjectField(TEXT("capabilities"), Capabilities);
	Initialize->SetObjectField(TEXT("params"), InitializeParams);

	TSharedRef<FJsonObject> Initialized = MakeShared<FJsonObject>();
	Initialized->SetStringField(TEXT("method"), TEXT("initialized"));
	Initialized->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> RateLimits = MakeShared<FJsonObject>();
	RateLimits->SetNumberField(TEXT("id"), 2);
	RateLimits->SetStringField(TEXT("method"), TEXT("account/rateLimits/read"));
	RateLimits->SetField(TEXT("params"), MakeShared<FJsonValueNull>());

	Process->SendWhenReady(SerializeMessage(Initialize));
	Process->SendWhenReady(SerializeMessage(Initialized));
	Process->SendWhenReady(SerializeMessage(RateLimits));
}

void FWorldDataCodexAccountUsageService::ConsumeOutput(const FString& Output)
{
	StdoutBuffer += Output;

	FString Line;
	while (StdoutBuffer.Split(TEXT("\n"), &Line, &StdoutBuffer, ESearchCase::CaseSensitive, ESearchDir::FromStart))
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Message;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
		{
			continue;
		}

		double RpcId = 0.0;
		if (!Message->TryGetNumberField(TEXT("id"), RpcId) || FMath::RoundToInt(RpcId) != 2)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (Message->TryGetObjectField(TEXT("result"), Result) && Result && Result->IsValid())
		{
			PublishUsage(*Result);
		}
		else
		{
			PublishFailure(TEXT("Codex 未返回账户用量。"));
		}
		return;
	}
}

void FWorldDataCodexAccountUsageService::PublishFailure(const FString& Error)
{
	bResultPublished = true;
	FWorldDataCodexAccountUsage Usage;
	Usage.Error = Error;
	OnUsageChanged.ExecuteIfBound(Usage);
}

void FWorldDataCodexAccountUsageService::PublishUsage(const TSharedPtr<FJsonObject>& Result)
{
	bResultPublished = true;
	FWorldDataCodexAccountUsage Usage;

	const TSharedPtr<FJsonObject>* RateLimits = nullptr;
	if (!Result.IsValid() || !Result->TryGetObjectField(TEXT("rateLimits"), RateLimits) || !RateLimits || !RateLimits->IsValid())
	{
		Usage.Error = TEXT("Codex 返回的账户用量格式无效。");
		OnUsageChanged.ExecuteIfBound(Usage);
		return;
	}

	Usage.bAvailable = true;
	(*RateLimits)->TryGetStringField(TEXT("planType"), Usage.PlanType);

	const TSharedPtr<FJsonObject>* Credits = nullptr;
	if ((*RateLimits)->TryGetObjectField(TEXT("credits"), Credits) && Credits && Credits->IsValid())
	{
		(*Credits)->TryGetBoolField(TEXT("hasCredits"), Usage.bHasCredits);
		(*Credits)->TryGetBoolField(TEXT("unlimited"), Usage.bUnlimitedCredits);
		(*Credits)->TryGetStringField(TEXT("balance"), Usage.CreditBalance);
	}

	const TSharedPtr<FJsonObject>* Primary = nullptr;
	if ((*RateLimits)->TryGetObjectField(TEXT("primary"), Primary) && Primary && Primary->IsValid())
	{
		double Number = 0.0;
		if ((*Primary)->TryGetNumberField(TEXT("usedPercent"), Number))
		{
			Usage.UsedPercent = FMath::Clamp(FMath::RoundToInt(Number), 0, 100);
		}
		if ((*Primary)->TryGetNumberField(TEXT("windowDurationMins"), Number))
		{
			Usage.WindowDurationMinutes = FMath::RoundToInt(Number);
		}
		if ((*Primary)->TryGetNumberField(TEXT("resetsAt"), Number))
		{
			Usage.ResetsAtUnixSeconds = static_cast<int64>(Number);
		}
	}

	OnUsageChanged.ExecuteIfBound(Usage);

	TSharedPtr<FInteractiveProcess> ProcessToStop = MoveTemp(Process);
	if (ProcessToStop.IsValid())
	{
		ProcessToStop->Cancel(true);
	}
}
