// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentCodexACPClientProcess.cpp
 * @brief Codex ACP 适配器发现、启动、回调绑定与进程停止。
 */

#include "Application/ACP/UnrealAgentCodexACPClient.h"

#include "Async/Async.h"
#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"

namespace
{
	bool PathExists(const FString& Path)
	{
		return !Path.IsEmpty() && (FPaths::FileExists(Path) || IFileManager::Get().FileSize(*Path) >= 0);
	}

	FString GetCommandInterpreterPath()
	{
#if PLATFORM_WINDOWS
		FString CommandInterpreter = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
		if (PathExists(CommandInterpreter))
		{
			return WorldDataCodexAcpRules::NormalizeLaunchPath(CommandInterpreter);
		}

		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		if (!SystemRoot.IsEmpty())
		{
			CommandInterpreter = FPaths::Combine(SystemRoot, TEXT("System32"), TEXT("cmd.exe"));
			if (PathExists(CommandInterpreter))
			{
				return WorldDataCodexAcpRules::NormalizeLaunchPath(CommandInterpreter);
			}
		}
#endif
		return FString();
	}

	FString GetPowerShellPath()
	{
#if PLATFORM_WINDOWS
		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		if (!SystemRoot.IsEmpty())
		{
			const FString PowerShell = FPaths::Combine(SystemRoot, TEXT("System32"), TEXT("WindowsPowerShell"), TEXT("v1.0"), TEXT("powershell.exe"));
			if (PathExists(PowerShell))
			{
				return WorldDataCodexAcpRules::NormalizeLaunchPath(PowerShell);
			}
		}
#endif
		return FString();
	}

	bool BuildLaunchSpecForExistingAdapterPath(const FString& AdapterPath, FWorldDataCodexAcpLaunchSpec& OutLaunchSpec)
	{
		if (!PathExists(AdapterPath))
		{
			return false;
		}

		const FString Extension = FPaths::GetExtension(AdapterPath).ToLower();
		const FString CommandInterpreterPath = Extension == TEXT("cmd") || Extension == TEXT("bat") ? GetCommandInterpreterPath() : FString();
		const FString PowerShellPath = Extension == TEXT("ps1") ? GetPowerShellPath() : FString();
		return WorldDataCodexAcpRules::BuildLaunchSpecForResolvedAdapterPath(AdapterPath, CommandInterpreterPath, PowerShellPath, OutLaunchSpec);
	}

	bool BuildCodexModelCatalogLaunch(const FString& CodexCliPath, FString& OutExecutable, FString& OutArguments)
	{
		if (!PathExists(CodexCliPath))
		{
			return false;
		}

		const FString FullPath = WorldDataCodexAcpRules::NormalizeLaunchPath(CodexCliPath);
		const FString Extension = FPaths::GetExtension(FullPath).ToLower();
#if PLATFORM_WINDOWS
		if (Extension == TEXT("cmd") || Extension == TEXT("bat"))
		{
			OutExecutable = GetCommandInterpreterPath();
			OutArguments = FString::Printf(TEXT("/d /s /c \"\"%s\" debug models\""), *FullPath);
			return !OutExecutable.IsEmpty();
		}
		if (Extension == TEXT("ps1"))
		{
			OutExecutable = GetPowerShellPath();
			OutArguments = FString::Printf(TEXT("-NoProfile -ExecutionPolicy Bypass -File \"%s\" debug models"), *FullPath);
			return !OutExecutable.IsEmpty();
		}
#endif

		OutExecutable = FullPath;
		OutArguments = TEXT("debug models");
		return true;
	}

	FString SanitizeConfigValue(const FString& Value)
	{
		for (const TCHAR Character : Value)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('-') && Character != TEXT('_') && Character != TEXT('.'))
			{
				return FString();
			}
		}
		return Value;
	}

	void AppendLaunchArguments(FWorldDataCodexAcpLaunchSpec& LaunchSpec, const FString& Arguments)
	{
		if (Arguments.IsEmpty())
		{
			return;
		}

		const FString Extension = FPaths::GetExtension(LaunchSpec.DisplayPath).ToLower();
		if ((Extension == TEXT("cmd") || Extension == TEXT("bat")) && LaunchSpec.Arguments.EndsWith(TEXT("\"")))
		{
			LaunchSpec.Arguments.LeftChopInline(1);
			LaunchSpec.Arguments += TEXT(" ") + Arguments + TEXT("\"");
			return;
		}

		if (!LaunchSpec.Arguments.IsEmpty())
		{
			LaunchSpec.Arguments += TEXT(" ");
		}
		LaunchSpec.Arguments += Arguments;
	}
}

FUnrealAgentCodexACPClient::~FUnrealAgentCodexACPClient()
{
	Stop();
}

void FUnrealAgentCodexACPClient::Stop()
{
	if (bToolApprovalHandlerRegistered)
	{
		FUnrealAgentMCPServer::RevokeToolApprovalClient(McpApprovalClientId, TEXT("The UnrealAgent ACP client stopped."));
		bToolApprovalHandlerRegistered = false;
	}
	TSharedPtr<FInteractiveProcess> ProcessToStop = MoveTemp(Process);
	if (ProcessToStop.IsValid())
	{
		ProcessToStop->Cancel(true);
	}

	StdoutBuffer.Empty();
	SessionId.Empty();
	AppliedModelId.Empty();
	PendingPrompt.Empty();
	PendingPromptImages.Empty();
	ActivePrompt.Empty();
	ActivePromptImages.Empty();
	PromptModelDiagnostic.Empty();
	ActiveAdapterDisplayPath.Empty();
	InitRpcId = 0;
	SessionRpcId = 0;
	PromptRpcId = 0;
	ConfigOptionRpcIds.Empty();
	SessionConfigOptions.Empty();
	ConfigOptions.Empty();
	PendingPermissionIds.Empty();
	PendingLocalMcpPermissions.Empty();
	DenyPendingHttpMcpPermissions();
	ToolCallTitles.Empty();
	bInitialized = false;
	bCreatingSession = false;
	bMcpReadinessCheckInFlight = false;
	bMcpServerReady = false;
	bPromptInFlight = false;
	bRetriedAfterMissingModelMetadata = false;
	bAgentSupportsImagePrompts = false;
	RebuildConfigOptions();
}

void FUnrealAgentCodexACPClient::RefreshModelCatalog()
{
	if (AgentProvider != EUnrealAgentACPProvider::Codex || bCatalogRefreshInFlight || CodexCliPath.IsEmpty())
	{
		return;
	}

	FString Executable;
	FString Arguments;
	if (!BuildCodexModelCatalogLaunch(CodexCliPath, Executable, Arguments))
	{
		EmitStatus(TEXT("无法启动当前 Codex CLI，暂时使用 ACP 模型目录。"));
		return;
	}

	bCatalogRefreshInFlight = true;
	const FString RequestedCliPath = CodexCliPath;
	const TWeakPtr<FUnrealAgentCodexACPClient> WeakSelf = AsShared();
	Async(EAsyncExecution::ThreadPool,
		[WeakSelf, RequestedCliPath, Executable, Arguments]()
		{
			int32 ReturnCode = -1;
			FString StandardOutput;
			FString StandardError;
			FPlatformProcess::ExecProcess(*Executable, *Arguments, &ReturnCode, &StandardOutput, &StandardError);

			TArray<FWorldDataCodexModelCatalogEntry> Catalog;
			FString ParseError;
			const bool bParsed = ReturnCode == 0 && WorldDataCodexAcpRules::ParseCodexModelCatalog(StandardOutput, Catalog, ParseError);
			if (ReturnCode != 0 && ParseError.IsEmpty())
			{
				ParseError = StandardError.TrimStartAndEnd();
				if (ParseError.IsEmpty())
				{
					ParseError = FString::Printf(TEXT("codex debug models 返回码 %d"), ReturnCode);
				}
			}

			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, RequestedCliPath, bParsed, Catalog = MoveTemp(Catalog), ParseError]() mutable
				{
					const TSharedPtr<FUnrealAgentCodexACPClient> Self = WeakSelf.Pin();
					if (!Self.IsValid() || Self->CodexCliPath != RequestedCliPath)
					{
						return;
					}

					Self->bCatalogRefreshInFlight = false;
					if (bParsed)
					{
						Self->ApplyModelCatalog(MoveTemp(Catalog));
					}
					else
					{
						Self->EmitStatus(FString::Printf(TEXT("实时模型目录刷新失败，继续使用 ACP 目录：%s"), *ParseError));
					}
				});
		});
}

FString FUnrealAgentCodexACPClient::BuildLaunchConfigArguments() const
{
	if (AgentProvider != EUnrealAgentACPProvider::Codex)
	{
		return FString();
	}

	TArray<FString> Overrides;
	FString LaunchModel = SelectedModelId;
	if (!WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(LaunchModel, ActiveAdapterVersion))
	{
		LaunchModel = GetCompatibleFallbackModelId(LaunchModel);
	}
	const FString Model = SanitizeConfigValue(LaunchModel);
	const FString Reasoning = SanitizeConfigValue(SelectedReasoningEffort);
	if (!Model.IsEmpty())
	{
		Overrides.Add(FString::Printf(TEXT("-c model=%s"), *Model));
	}
	if (!Reasoning.IsEmpty())
	{
		Overrides.Add(FString::Printf(TEXT("-c model_reasoning_effort=%s"), *Reasoning));
	}
	if (SelectedSpeed == TEXT("fast"))
	{
		Overrides.Add(TEXT("-c service_tier=fast"));
		Overrides.Add(TEXT("-c features.fast_mode=true"));
	}
	return FString::Join(Overrides, TEXT(" "));
}

bool FUnrealAgentCodexACPClient::EnsureProcess()
{
	if (Process.IsValid() && Process->IsRunning())
	{
		return true;
	}

	FWorldDataCodexAcpLaunchSpec LaunchSpec;
	if (CachedLaunchSpec.IsSet())
	{
		LaunchSpec = CachedLaunchSpec.GetValue();
		CachedLaunchSpec.Reset();
	}
	else if (!FindAdapterLaunch(LaunchSpec))
	{
		Fail(AgentProvider == EUnrealAgentACPProvider::Cursor
				? TEXT("没有找到 Cursor Agent ACP。请安装 Cursor CLI，登录后确认 agent acp 可用，或在设置中选择 agent.cmd/agent.ps1。")
				: TEXT("没有找到可启动的 Codex ACP adapter。请在 Unreal Agent 面板点击“一键安装 Codex ACP”，或把可用的 codex-acp.exe/.cmd/.ps1 加入 PATH。"));
		return false;
	}

	const FString ConfigArguments = BuildLaunchConfigArguments();
	AppendLaunchArguments(LaunchSpec, ConfigArguments);
	// 能力只对返回它的 ACP 进程有效；重启期间必须回到 fail-closed。
	bAgentSupportsImagePrompts = false;

	FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDirectory);

	if (GEngine)
	{
		GEngine->Exec(nullptr, TEXT("Log LogInteractiveProcess Error"));
	}

	ActiveAdapterDisplayPath = LaunchSpec.DisplayPath;
	Process = MakeShared<FInteractiveProcess>(LaunchSpec.Executable, LaunchSpec.Arguments, WorkingDirectory, true, true);
	const TWeakPtr<FUnrealAgentCodexACPClient> WeakSelf = AsShared();
	const TWeakPtr<FInteractiveProcess> WeakLaunchedProcess = Process;

	Process->OnOutput().BindLambda(
		[WeakSelf, WeakLaunchedProcess](const FString& Output)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, WeakLaunchedProcess, Output]()
				{
					const TSharedPtr<FUnrealAgentCodexACPClient> Self = WeakSelf.Pin();
					const TSharedPtr<FInteractiveProcess> SourceProcess = WeakLaunchedProcess.Pin();
					if (Self.IsValid() && SourceProcess.IsValid() && Self->Process == SourceProcess)
					{
						Self->ConsumeOutput(Output);
					}
				});
		});

	Process->OnCompleted().BindLambda(
		[WeakSelf, WeakLaunchedProcess](int32 ReturnCode, bool bCanceled)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, WeakLaunchedProcess, ReturnCode, bCanceled]()
				{
					if (const TSharedPtr<FUnrealAgentCodexACPClient> Self = WeakSelf.Pin())
					{
						const TSharedPtr<FInteractiveProcess> CompletedProcess = WeakLaunchedProcess.Pin();
						if (!CompletedProcess.IsValid() || Self->Process != CompletedProcess)
						{
							return;
						}
						const bool bHadUserTurnBeforeCleanup = Self->bPromptInFlight;
						Self->Process.Reset();
						Self->bInitialized = false;
						Self->bAgentSupportsImagePrompts = false;
						Self->bCreatingSession = false;
						Self->bPromptInFlight = false;
						Self->PendingPermissionIds.Empty();
						Self->PendingLocalMcpPermissions.Empty();
						Self->DenyPendingHttpMcpPermissions();
						const FString AdapterName = Self->ActiveAdapterDisplayPath.IsEmpty() ? TEXT("unknown adapter") : Self->ActiveAdapterDisplayPath;
						const FString AgentName = Self->GetAgentDisplayName();
						Self->ActiveAdapterDisplayPath.Empty();
						Self->Fail(FString::Printf(TEXT("%s ACP 已退出（%s），返回码=%d，取消=%s。"), *AgentName, *AdapterName, ReturnCode,
									   bCanceled ? TEXT("true") : TEXT("false")),
							bHadUserTurnBeforeCleanup);
					}
				});
		});

	const bool bOverrideCodexPath = AgentProvider == EUnrealAgentACPProvider::Codex && PathExists(CodexCliPath);
	const FString PreviousCodexPath = bOverrideCodexPath ? FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_PATH")) : FString();
	const bool bInjectProjectMcp = AgentProvider == EUnrealAgentACPProvider::Codex;
	const FString PreviousMcpFiltering = bInjectProjectMcp ? FPlatformMisc::GetEnvironmentVariable(TEXT("DISABLE_MCP_CONFIG_FILTERING")) : FString();
	if (bOverrideCodexPath)
	{
		// FInteractiveProcess 会在 Launch() 时继承当前进程环境。
		// 只在创建进程期间覆盖环境，使 App Server 适配器使用
		// 与 UI 模型目录来源一致的 Codex CLI。
		FPlatformMisc::SetEnvironmentVar(TEXT("CODEX_PATH"), *CodexCliPath);
	}
	if (bInjectProjectMcp)
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("DISABLE_MCP_CONFIG_FILTERING"), TEXT("true"));
	}
	const bool bLaunched = Process->Launch();
	if (bOverrideCodexPath)
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("CODEX_PATH"), *PreviousCodexPath);
	}
	if (bInjectProjectMcp)
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("DISABLE_MCP_CONFIG_FILTERING"), *PreviousMcpFiltering);
	}

	if (!bLaunched)
	{
		Process.Reset();
		Fail(FString::Printf(TEXT("启动 %s ACP 失败：%s"), *GetAgentDisplayName(), *ActiveAdapterDisplayPath));
		ActiveAdapterDisplayPath.Empty();
		return false;
	}

	EmitStatus(FString::Printf(TEXT("已启动 %s ACP：%s"), *GetAgentDisplayName(), *ActiveAdapterDisplayPath));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("protocolVersion"), 1);

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> FileSystemCapabilities = MakeShared<FJsonObject>();
	FileSystemCapabilities->SetBoolField(TEXT("readTextFile"), true);
	FileSystemCapabilities->SetBoolField(TEXT("writeTextFile"), false);
	Capabilities->SetObjectField(TEXT("fs"), FileSystemCapabilities);
	Capabilities->SetBoolField(TEXT("terminal"), false);
	TSharedPtr<FJsonObject> SessionCapabilities = MakeShared<FJsonObject>();
	SessionCapabilities->SetObjectField(TEXT("configOptions"), MakeShared<FJsonObject>());
	Capabilities->SetObjectField(TEXT("session"), SessionCapabilities);
	Params->SetObjectField(TEXT("clientCapabilities"), Capabilities);

	TSharedPtr<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("worlddata"));
	ClientInfo->SetStringField(TEXT("title"), UnrealAgentMCP::Brand::ProductName);
	ClientInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));
	Params->SetObjectField(TEXT("clientInfo"), ClientInfo);

	InitRpcId = SendRpc(TEXT("initialize"), Params);
	return true;
}

bool FUnrealAgentCodexACPClient::CanLaunchAgent(FString* OutDisplayPath) const
{
	if (CachedLaunchSpec.IsSet())
	{
		if (OutDisplayPath)
		{
			*OutDisplayPath = CachedLaunchSpec->DisplayPath;
		}
		return true;
	}

	FWorldDataCodexAcpLaunchSpec LaunchSpec;
	const bool bCanLaunch = FindAdapterLaunch(LaunchSpec);
	if (bCanLaunch)
	{
		CachedLaunchSpec = LaunchSpec;
	}
	if (OutDisplayPath)
	{
		*OutDisplayPath = bCanLaunch ? LaunchSpec.DisplayPath : FString();
	}
	return bCanLaunch;
}

bool FUnrealAgentCodexACPClient::FindAdapterLaunch(FWorldDataCodexAcpLaunchSpec& OutLaunchSpec) const
{
	if (AgentProvider == EUnrealAgentACPProvider::Cursor)
	{
		TArray<FString> CursorCandidates;
		if (!AgentCliPath.IsEmpty())
		{
			if (FPaths::GetExtension(AgentCliPath).Equals(TEXT("ps1"), ESearchCase::IgnoreCase))
			{
				const FString CmdSibling = FPaths::ChangeExtension(AgentCliPath, TEXT("cmd"));
				if (PathExists(CmdSibling))
				{
					CursorCandidates.Add(CmdSibling);
				}
			}
			CursorCandidates.Add(AgentCliPath);
		}
		for (const FString& Command : { FString(TEXT("agent")), FString(TEXT("cursor-agent")) })
		{
			const FString Resolved = ResolveOnPath(Command);
			if (!Resolved.IsEmpty())
			{
				CursorCandidates.AddUnique(Resolved);
			}
		}

		for (const FString& Candidate : CursorCandidates)
		{
			if (BuildLaunchSpecForExistingAdapterPath(Candidate, OutLaunchSpec))
			{
				AppendLaunchArguments(OutLaunchSpec, TEXT("acp"));
				return true;
			}
		}
		return false;
	}

	const FString EnvironmentPath = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_ACP_EXECUTABLE"));
	if (BuildLaunchSpecForExistingAdapterPath(EnvironmentPath, OutLaunchSpec))
	{
		return true;
	}
	if (!EnvironmentPath.IsEmpty() && BuildLaunchSpecForExistingAdapterPath(ResolveOnPath(EnvironmentPath), OutLaunchSpec))
	{
		return true;
	}

	const TArray<FString> CandidateNames = WorldDataCodexAcpRules::GetAdapterCandidateNames();
	TArray<FString> SearchDirectories;
	// 优先使用项目管理的 App Server 适配器，而不是旧版捆绑可执行文件。
	// 受管包会跟随已安装 Codex 的模型目录；codex-acp 0.16
	// 内嵌的则是较旧的 Codex Core 快照。
	WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories,
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("ACPAdapters"), TEXT("Codex"), TEXT("node_modules"), TEXT(".bin")));
	WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("ACPAdapters"), TEXT("Codex")));
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent")))
	{
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("Win64")));
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries")));
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty"), TEXT("codex-acp"), TEXT("Win64")));
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty"), TEXT("codex-acp")));
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Win64")));
		WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	}
	WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent")));
	WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Win64")));
	WorldDataCodexAcpRules::AddUniqueNormalizedPath(SearchDirectories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries")));

	for (const FString& Directory : SearchDirectories)
	{
		for (const FString& Name : CandidateNames)
		{
			if (BuildLaunchSpecForExistingAdapterPath(FPaths::Combine(Directory, Name), OutLaunchSpec))
			{
				return true;
			}
		}
	}

	for (const FString& Name : CandidateNames)
	{
		if (BuildLaunchSpecForExistingAdapterPath(ResolveOnPath(Name), OutLaunchSpec))
		{
			return true;
		}
	}
	return false;
}

FString FUnrealAgentCodexACPClient::ResolveOnPath(const FString& Command) const
{
#if PLATFORM_WINDOWS
	int32 ReturnCode = -1;
	FString StandardOutput;
	FString StandardError;
	FPlatformProcess::ExecProcess(TEXT("where.exe"), *Command, &ReturnCode, &StandardOutput, &StandardError);
	if (ReturnCode == 0)
	{
		TArray<FString> Lines;
		StandardOutput.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (PathExists(Line))
			{
				return Line;
			}
		}
	}
#endif
	return FString();
}
