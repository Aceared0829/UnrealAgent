// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPanelSettings.cpp
 * @brief MCP 面板设置和 CLI 路径解析实现。
 */

#include "Presentation/Settings/UnrealAgentMCPPanelSettings.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Application/Settings/UnrealAgentMCPPanelSettingsStorage.h"
#include "Presentation/Panel/UnrealAgentMCPStyle.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winreg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	void AddUniqueNpmCandidate(TArray<FString>& CandidatePaths, const FString& CandidatePath)
	{
		FString NormalizedPath = UnrealAgentMCP::StripOuterQuotes(CandidatePath);
		if (NormalizedPath.IsEmpty())
		{
			return;
		}

		FPaths::NormalizeFilename(NormalizedPath);
		CandidatePaths.AddUnique(NormalizedPath);
	}

	void AddNpmCandidatesFromSearchPath(TArray<FString>& CandidatePaths, const FString& SearchPath)
	{
		TArray<FString> Directories;
		SearchPath.ParseIntoArray(Directories, TEXT(";"), true);
		for (FString Directory : Directories)
		{
			Directory.TrimStartAndEndInline();
			Directory.TrimQuotesInline();
			if (!Directory.IsEmpty() && !Directory.Contains(TEXT("%")))
			{
				AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(Directory, TEXT("npm.cmd")));
			}
		}
	}

#if PLATFORM_WINDOWS
	void AddWindowsNpmCandidates(TArray<FString>& CandidatePaths)
	{
		FString UserSearchPath;
		if (FWindowsPlatformMisc::QueryRegKey(HKEY_CURRENT_USER, TEXT("Environment"), TEXT("Path"), UserSearchPath))
		{
			AddNpmCandidatesFromSearchPath(CandidatePaths, UserSearchPath);
		}

		FString MachineSearchPath;
		if (FWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"), TEXT("Path"), MachineSearchPath))
		{
			AddNpmCandidatesFromSearchPath(CandidatePaths, MachineSearchPath);
		}

		static const TCHAR* InstallRootVariables[] = { TEXT("ProgramFiles"), TEXT("ProgramW6432"), TEXT("ProgramFiles(x86)") };
		for (const TCHAR* VariableName : InstallRootVariables)
		{
			const FString InstallRoot = FPlatformMisc::GetEnvironmentVariable(VariableName);
			if (!InstallRoot.IsEmpty())
			{
				AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(InstallRoot, TEXT("nodejs"), TEXT("npm.cmd")));
			}
		}

		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (!LocalAppData.IsEmpty())
		{
			AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(LocalAppData, TEXT("nodejs"), TEXT("npm.cmd")));
			AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(LocalAppData, TEXT("Programs"), TEXT("nodejs"), TEXT("npm.cmd")));
		}

		const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
		if (!AppData.IsEmpty())
		{
			AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(AppData, TEXT("npm"), TEXT("npm.cmd")));
		}

		const FString NvmSymlink = FPlatformMisc::GetEnvironmentVariable(TEXT("NVM_SYMLINK"));
		if (!NvmSymlink.IsEmpty())
		{
			AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(NvmSymlink, TEXT("npm.cmd")));
		}

		FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeFilename(ProjectDirectory);
		if (ProjectDirectory.Len() >= 3 && ProjectDirectory[1] == TCHAR(':'))
		{
			AddUniqueNpmCandidate(CandidatePaths, FPaths::Combine(ProjectDirectory.Left(3), TEXT("npm.cmd")));
		}
	}
#endif
}

namespace UnrealAgentMCPPanelSettings
{
	FString LexToString(EUnrealAgentControllerMode Mode)
	{
		switch (Mode)
		{
		case EUnrealAgentControllerMode::ExternalCodexAgent:
			return TEXT("external_codex_agent");
		case EUnrealAgentControllerMode::ExternalCursorAgent:
			return TEXT("external_cursor_agent");
		case EUnrealAgentControllerMode::NativeUnrealAgent:
		default:
			return TEXT("native_unreal_agent");
		}
	}

	bool TryParseControllerMode(const FString& Text, EUnrealAgentControllerMode& OutMode)
	{
		if (Text.Equals(TEXT("native_unreal_agent"), ESearchCase::IgnoreCase))
		{
			OutMode = EUnrealAgentControllerMode::NativeUnrealAgent;
			return true;
		}
		if (Text.Equals(TEXT("external_codex_agent"), ESearchCase::IgnoreCase))
		{
			OutMode = EUnrealAgentControllerMode::ExternalCodexAgent;
			return true;
		}
		if (Text.Equals(TEXT("external_cursor_agent"), ESearchCase::IgnoreCase))
		{
			OutMode = EUnrealAgentControllerMode::ExternalCursorAgent;
			return true;
		}
		return false;
	}

	bool UsesNativeUnrealAgentController(EUnrealAgentControllerMode Mode)
	{
		return Mode == EUnrealAgentControllerMode::NativeUnrealAgent;
	}

	EUnrealAgentControllerMode ResolveRetiredNativeControllerMode(const FString& FallbackProviderId)
	{
		return FallbackProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? EUnrealAgentControllerMode::ExternalCursorAgent
																				  : EUnrealAgentControllerMode::ExternalCodexAgent;
	}

	FString ResolveControllerAcpProviderId(const EUnrealAgentControllerMode ControllerMode, const FString& FallbackProviderId)
	{
		if (ControllerMode == EUnrealAgentControllerMode::ExternalCursorAgent)
		{
			return TEXT("cursor");
		}
		if (ControllerMode == EUnrealAgentControllerMode::ExternalCodexAgent)
		{
			return TEXT("codex");
		}
		return FallbackProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? TEXT("cursor") : TEXT("codex");
	}

	EUnrealAgentControllerMode ResolveConversationControllerMode(const FString& SerializedControllerId, const FString& LegacyProviderId,
		const EUnrealAgentControllerMode CurrentControllerMode, const bool bIsActiveLegacyConversation)
	{
		EUnrealAgentControllerMode ParsedControllerMode = EUnrealAgentControllerMode::ExternalCodexAgent;
		if (TryParseControllerMode(SerializedControllerId, ParsedControllerMode))
		{
			if (UsesNativeUnrealAgentController(ParsedControllerMode))
			{
				return ResolveRetiredNativeControllerMode(LegacyProviderId);
			}
			return ParsedControllerMode;
		}
		if (bIsActiveLegacyConversation)
		{
			return CurrentControllerMode;
		}
		return LegacyProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? EUnrealAgentControllerMode::ExternalCursorAgent : EUnrealAgentControllerMode::ExternalCodexAgent;
	}

	bool SupportsKernelMultiAgent(const EUnrealAgentControllerMode ControllerMode)
	{
		(void)ControllerMode;
		return false;
	}

	FString GetManagedCodexCliCandidatePath(const FString& ProjectSavedDirectory)
	{
		return FPaths::Combine(ProjectSavedDirectory, TEXT("UnrealAgent"), TEXT("ACPAdapters"), TEXT("Codex"), TEXT("node_modules"),
#if PLATFORM_WINDOWS
			TEXT("@openai"), TEXT("codex-win32-x64"), TEXT("vendor"), TEXT("x86_64-pc-windows-msvc"), TEXT("bin"), TEXT("codex.exe"));
#else
			TEXT(".bin"), TEXT("codex"));
#endif
	}

	FString GetManagedCursorCliCandidatePath(const FString& ProjectSavedDirectory)
	{
		return FPaths::Combine(ProjectSavedDirectory, TEXT("UnrealAgent"), TEXT("ACPAdapters"), TEXT("Cursor"),
#if PLATFORM_WINDOWS
			TEXT("cursor-agent.cmd"));
#else
			TEXT("cursor-agent"));
#endif
	}

	FString GetCliCommandName(EUnrealAgentMCPPanelCliTool Tool)
	{
		return Tool == EUnrealAgentMCPPanelCliTool::Codex ? TEXT("codex") : TEXT("agent");
	}

	FString DetectCliPath(EUnrealAgentMCPPanelCliTool Tool)
	{
		FString Detected;
		if (Tool == EUnrealAgentMCPPanelCliTool::Codex)
		{
			Detected = UnrealAgentMCP::ResolveLaunchableCliPath(GetManagedCodexCliCandidatePath(FPaths::ProjectSavedDir()));
			if (Detected.IsEmpty())
			{
				Detected = UnrealAgentMCP::ResolveLaunchableCliPath(UnrealAgentMCP::ResolveCommandOnPath(GetCliCommandName(Tool)));
#if PLATFORM_WINDOWS
				FString NormalizedDetected = Detected;
				FPaths::NormalizeFilename(NormalizedDetected);
				if (NormalizedDetected.Contains(TEXT("/WindowsApps/OpenAI.Codex_"), ESearchCase::IgnoreCase))
				{
					Detected.Empty();
				}
#endif
			}
		}
		else
		{
			Detected = UnrealAgentMCP::ResolveLaunchableCliPath(UnrealAgentMCP::ResolveCommandOnPath(GetCliCommandName(Tool)));
		}
		if (Detected.IsEmpty() && Tool == EUnrealAgentMCPPanelCliTool::Cursor)
		{
			Detected = UnrealAgentMCP::ResolveLaunchableCliPath(GetManagedCursorCliCandidatePath(FPaths::ProjectSavedDir()));
		}
		if (Detected.IsEmpty() && Tool == EUnrealAgentMCPPanelCliTool::Cursor)
		{
			Detected = UnrealAgentMCP::ResolveLaunchableCliPath(UnrealAgentMCP::ResolveCommandOnPath(TEXT("cursor-agent")));
		}
		return Detected;
	}

	FString DetectNpmPath()
	{
#if PLATFORM_WINDOWS
		TArray<FString> CandidatePaths;
		AddUniqueNpmCandidate(CandidatePaths, UnrealAgentMCP::ResolveCommandOnPath(TEXT("npm.cmd")));
		AddNpmCandidatesFromSearchPath(CandidatePaths, FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")));
		AddWindowsNpmCandidates(CandidatePaths);
		return ResolveNpmPathFromCandidates(CandidatePaths);
#else
		return UnrealAgentMCP::ResolveLaunchableCliPath(UnrealAgentMCP::ResolveCommandOnPath(TEXT("npm")));
#endif
	}

	FString ResolveNpmPathFromCandidates(const TArray<FString>& CandidatePaths)
	{
		for (const FString& CandidatePath : CandidatePaths)
		{
			const FString ResolvedPath = UnrealAgentMCP::ResolveLaunchableCliPath(CandidatePath);
			if (UnrealAgentMCP::PathExists(ResolvedPath))
			{
				return ResolvedPath;
			}
		}
		return FString();
	}

	bool IsInstallerProgressLine(const FString& Line, int32& OutPercent, FString& OutStage, FString& OutComponent)
	{
		static const FString Prefix = TEXT("UEBRIDGE_PROGRESS|");
		FString TrimmedLine = Line.TrimStartAndEnd();
		if (!TrimmedLine.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}

		TArray<FString> Fields;
		TrimmedLine.ParseIntoArray(Fields, TEXT("|"), false);
		if (Fields.Num() != 4 || !Fields[1].IsNumeric())
		{
			return false;
		}

		OutPercent = FMath::Clamp(FCString::Atoi(*Fields[1]), 0, 100);
		OutStage = Fields[2].TrimStartAndEnd();
		OutComponent = Fields[3].TrimStartAndEnd();
		return !OutStage.IsEmpty() && !OutComponent.IsEmpty();
	}

	FString NormalizeInstallerDirectoryArgument(const FString& Directory)
	{
		FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::CollapseRelativeDirectories(NormalizedDirectory);
		FPaths::MakePlatformFilename(NormalizedDirectory);
		while (NormalizedDirectory.Len() > 3 && (NormalizedDirectory.EndsWith(TEXT("\\")) || NormalizedDirectory.EndsWith(TEXT("/"))))
		{
			NormalizedDirectory.LeftChopInline(1);
		}
		return NormalizedDirectory;
	}

	FString NormalizeConfiguredCliPath(const FString& NewPath)
	{
		FString Normalized = UnrealAgentMCP::StripOuterQuotes(NewPath);
		if (!Normalized.IsEmpty() && !UnrealAgentMCP::PathExists(Normalized) && !Normalized.Contains(TEXT("\\")) && !Normalized.Contains(TEXT("/")))
		{
			const FString Resolved = UnrealAgentMCP::ResolveCommandOnPath(Normalized);
			if (!Resolved.IsEmpty())
			{
				Normalized = Resolved;
			}
		}
		return UnrealAgentMCP::ResolveLaunchableCliPath(Normalized);
	}

	FString ResolveEffectiveCliPath(const FString& ConfiguredPath, const FString& DetectedPath)
	{
		const FString Configured = UnrealAgentMCP::StripOuterQuotes(ConfiguredPath);
		if (!Configured.IsEmpty())
		{
			return UnrealAgentMCP::PathExists(Configured) ? UnrealAgentMCP::ResolveLaunchableCliPath(Configured) : FString();
		}
		return UnrealAgentMCP::ResolveLaunchableCliPath(DetectedPath);
	}

	FLinearColor ClampOpaqueAccentColor(const FLinearColor& Color)
	{
		return FLinearColor(FMath::Clamp(Color.R, 0.0f, 1.0f), FMath::Clamp(Color.G, 0.0f, 1.0f), FMath::Clamp(Color.B, 0.0f, 1.0f), 1.0f);
	}

	bool TryParseSettingsJson(const FString& JsonText, FUnrealAgentMCPPanelSettings& OutSettings)
	{
		OutSettings = FUnrealAgentMCPPanelSettings();

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		double SerializedSchemaVersion = 0.0;
		const bool bHasVersionedControllerContract =
			Root->TryGetNumberField(TEXT("schemaVersion"), SerializedSchemaVersion) && SerializedSchemaVersion >= ControllerContractSchemaVersion;
		OutSettings.SchemaVersion = CurrentSchemaVersion;

		if (bHasVersionedControllerContract)
		{
			const TSharedPtr<FJsonObject>* AgentObjectPointer = nullptr;
			if (Root->TryGetObjectField(TEXT("agent"), AgentObjectPointer) && AgentObjectPointer && AgentObjectPointer->IsValid())
			{
				FString ControllerText;
				if ((*AgentObjectPointer)->TryGetStringField(TEXT("controller"), ControllerText))
				{
					TryParseControllerMode(ControllerText, OutSettings.ControllerMode);
				}

				FString LegacyBrainTransport;
				if ((*AgentObjectPointer)->TryGetStringField(TEXT("brainTransport"), LegacyBrainTransport) &&
					(LegacyBrainTransport.Contains(TEXT("subscription_relay"), ESearchCase::IgnoreCase) ||
						LegacyBrainTransport.Equals(TEXT("acp_inference_relay"), ESearchCase::IgnoreCase)))
				{
					// 中继身份以 transport 为准，不能先折成 Native 再靠
					// ActiveProviderId 猜测；旧档常缺该字段或仍写 Codex。
					OutSettings.bMigratedLegacySubscriptionRelay = true;
					OutSettings.ControllerMode = LegacyBrainTransport.Equals(TEXT("cursor_subscription_relay"), ESearchCase::IgnoreCase)
						? EUnrealAgentControllerMode::ExternalCursorAgent
						: EUnrealAgentControllerMode::NativeUnrealAgent;
				}
			}
		}

		const TSharedPtr<FJsonObject>* ColorObjectPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("color"), ColorObjectPointer) && ColorObjectPointer && ColorObjectPointer->IsValid())
		{
			const TSharedPtr<FJsonObject> ColorObject = *ColorObjectPointer;
			auto ReadComponent = [ColorObject](const FString& Field, float DefaultValue)
			{
				double Value = DefaultValue;
				return ColorObject->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : DefaultValue;
			};

			OutSettings.AccentColor = ClampOpaqueAccentColor(FLinearColor(ReadComponent(TEXT("r"), 1.0f), ReadComponent(TEXT("g"), 1.0f), ReadComponent(TEXT("b"), 1.0f), 1.0f));
		}

		const TSharedPtr<FJsonObject>* CliObjectPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("cli"), CliObjectPointer) && CliObjectPointer && CliObjectPointer->IsValid())
		{
			(*CliObjectPointer)->TryGetStringField(TEXT("codexPath"), OutSettings.CodexCliPath);
			(*CliObjectPointer)->TryGetStringField(TEXT("cursorPath"), OutSettings.CursorCliPath);
			OutSettings.CodexCliPath = UnrealAgentMCP::StripOuterQuotes(OutSettings.CodexCliPath);
			OutSettings.CursorCliPath = UnrealAgentMCP::StripOuterQuotes(OutSettings.CursorCliPath);
		}

		const TSharedPtr<FJsonObject>* UiObjectPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("ui"), UiObjectPointer) && UiObjectPointer && UiObjectPointer->IsValid())
		{
			(*UiObjectPointer)->TryGetStringField(TEXT("activeProvider"), OutSettings.ActiveProviderId);
			(*UiObjectPointer)->TryGetBoolField(TEXT("sidebarCollapsed"), OutSettings.bSidebarCollapsed);
			double AgentMode = static_cast<uint8>(OutSettings.DefaultAgentMode);
			double ApprovalPolicy = static_cast<uint8>(OutSettings.ApprovalPolicy);
			double SelfRepairPolicy = static_cast<uint8>(OutSettings.SelfRepairPolicy);
			(*UiObjectPointer)->TryGetNumberField(TEXT("agentMode"), AgentMode);
			(*UiObjectPointer)->TryGetNumberField(TEXT("approvalPolicy"), ApprovalPolicy);
			(*UiObjectPointer)->TryGetNumberField(TEXT("selfRepairPolicy"), SelfRepairPolicy);
			OutSettings.DefaultAgentMode = static_cast<EWorldDataAgentMode>(FMath::Clamp(static_cast<int32>(AgentMode), 0, 2));
			OutSettings.ApprovalPolicy = static_cast<EWorldDataApprovalPolicy>(FMath::Clamp(static_cast<int32>(ApprovalPolicy), 0, 2));
			OutSettings.SelfRepairPolicy = static_cast<EWorldDataSelfRepairPolicy>(FMath::Clamp(static_cast<int32>(SelfRepairPolicy), 0, 2));
			(*UiObjectPointer)->TryGetStringField(TEXT("directProvider"), OutSettings.DirectProviderId);
			if (!bHasVersionedControllerContract)
			{
				bool bWasUsingDirectKernelChannel = false;
				(*UiObjectPointer)->TryGetBoolField(TEXT("useDirectKernelChannel"), bWasUsingDirectKernelChannel);
				OutSettings.ControllerMode = bWasUsingDirectKernelChannel                          ? EUnrealAgentControllerMode::NativeUnrealAgent
					: OutSettings.ActiveProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? EUnrealAgentControllerMode::ExternalCursorAgent
																								   : EUnrealAgentControllerMode::ExternalCodexAgent;
			}
			(*UiObjectPointer)->TryGetStringField(TEXT("budgetTier"), OutSettings.BudgetTierId);
			(*UiObjectPointer)->TryGetBoolField(TEXT("multiAgentEnabled"), OutSettings.bMultiAgentEnabled);
			(*UiObjectPointer)->TryGetBoolField(TEXT("memoryEnabled"), OutSettings.bMemoryEnabled);
		}
		const TSharedPtr<FJsonObject>* ModelsObjectPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("models"), ModelsObjectPointer) && ModelsObjectPointer && ModelsObjectPointer->IsValid())
		{
			(*ModelsObjectPointer)->TryGetStringField(TEXT("codex"), OutSettings.CodexModelId);
			(*ModelsObjectPointer)->TryGetStringField(TEXT("cursor"), OutSettings.CursorModelId);
		}

		const TSharedPtr<FJsonObject>* ContextObjectPointer = nullptr;
		if (Root->TryGetObjectField(TEXT("context"), ContextObjectPointer) && ContextObjectPointer && ContextObjectPointer->IsValid())
		{
			double TokenCapacity = OutSettings.ContextTokenCapacity;
			(*ContextObjectPointer)->TryGetNumberField(TEXT("tokenCapacity"), TokenCapacity);
			OutSettings.ContextTokenCapacity = UnrealAgentMCPConversationModel::ClampContextTokenCapacity(static_cast<int32>(TokenCapacity));
		}

		// 控制者是唯一身份来源；Native 仅保留合法的兼容回退字段，
		// 外部代理不能借该字段把模型目录切到另一个代理。
		OutSettings.ActiveProviderId = ResolveControllerAcpProviderId(OutSettings.ControllerMode, OutSettings.ActiveProviderId);
		if (UsesNativeUnrealAgentController(OutSettings.ControllerMode))
		{
			OutSettings.ControllerMode = ResolveRetiredNativeControllerMode(OutSettings.ActiveProviderId);
			OutSettings.ActiveProviderId = ResolveControllerAcpProviderId(OutSettings.ControllerMode, OutSettings.ActiveProviderId);
		}

		if (OutSettings.BudgetTierId != TEXT("light") && OutSettings.BudgetTierId != TEXT("fine"))
		{
			OutSettings.BudgetTierId = TEXT("standard");
		}

		return true;
	}

	bool TrySerializeSettingsJson(const FUnrealAgentMCPPanelSettings& Settings, FString& OutJsonText)
	{
		FUnrealAgentMCPPanelSettings Sanitized = Settings;
		if (UsesNativeUnrealAgentController(Sanitized.ControllerMode))
		{
			Sanitized.ControllerMode = ResolveRetiredNativeControllerMode(Sanitized.ActiveProviderId);
		}
		Sanitized.ActiveProviderId = ResolveControllerAcpProviderId(Sanitized.ControllerMode, Sanitized.ActiveProviderId);
		Sanitized.SchemaVersion = CurrentSchemaVersion;

		TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
		ColorObject->SetNumberField(TEXT("r"), Settings.AccentColor.R);
		ColorObject->SetNumberField(TEXT("g"), Settings.AccentColor.G);
		ColorObject->SetNumberField(TEXT("b"), Settings.AccentColor.B);

		TSharedPtr<FJsonObject> CliObject = MakeShared<FJsonObject>();
		CliObject->SetStringField(TEXT("codexPath"), Settings.CodexCliPath);
		CliObject->SetStringField(TEXT("cursorPath"), Settings.CursorCliPath);

		TSharedPtr<FJsonObject> UiObject = MakeShared<FJsonObject>();
		UiObject->SetStringField(TEXT("activeProvider"), Sanitized.ActiveProviderId);
		UiObject->SetBoolField(TEXT("sidebarCollapsed"), Settings.bSidebarCollapsed);
		UiObject->SetNumberField(TEXT("agentMode"), static_cast<uint8>(Settings.DefaultAgentMode));
		UiObject->SetNumberField(TEXT("approvalPolicy"), static_cast<uint8>(Settings.ApprovalPolicy));
		UiObject->SetNumberField(TEXT("selfRepairPolicy"), static_cast<uint8>(Settings.SelfRepairPolicy));
		UiObject->SetStringField(TEXT("directProvider"), Settings.DirectProviderId);
		// 旧读取器仍认这个派生字段；schema v5 起永远为 false。
		UiObject->SetBoolField(TEXT("useDirectKernelChannel"), false);
		UiObject->SetStringField(TEXT("budgetTier"), Settings.BudgetTierId);
		UiObject->SetBoolField(TEXT("multiAgentEnabled"), Settings.bMultiAgentEnabled);
		UiObject->SetBoolField(TEXT("memoryEnabled"), Settings.bMemoryEnabled);

		TSharedPtr<FJsonObject> ModelsObject = MakeShared<FJsonObject>();
		ModelsObject->SetStringField(TEXT("codex"), Settings.CodexModelId);
		ModelsObject->SetStringField(TEXT("cursor"), Settings.CursorModelId);

		TSharedPtr<FJsonObject> ContextObject = MakeShared<FJsonObject>();
		ContextObject->SetNumberField(TEXT("tokenCapacity"), UnrealAgentMCPConversationModel::ClampContextTokenCapacity(Settings.ContextTokenCapacity));

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> AgentObject = MakeShared<FJsonObject>();
		AgentObject->SetStringField(TEXT("controller"), LexToString(Sanitized.ControllerMode));
		Root->SetNumberField(TEXT("schemaVersion"), CurrentSchemaVersion);
		Root->SetObjectField(TEXT("agent"), AgentObject);
		Root->SetObjectField(TEXT("color"), ColorObject);
		Root->SetObjectField(TEXT("cli"), CliObject);
		Root->SetObjectField(TEXT("ui"), UiObject);
		Root->SetObjectField(TEXT("models"), ModelsObject);
		Root->SetObjectField(TEXT("context"), ContextObject);

		OutJsonText.Reset();
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJsonText);
		return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}

	bool LoadSettingsFile(const FString& SettingsPath, FUnrealAgentMCPPanelSettings& OutSettings)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *SettingsPath))
		{
			OutSettings = FUnrealAgentMCPPanelSettings();
			return false;
		}
		return TryParseSettingsJson(JsonText, OutSettings);
	}

	bool SaveSettingsFile(const FString& SettingsPath, const FUnrealAgentMCPPanelSettings& Settings)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(SettingsPath), true);

		FString JsonText;
		if (!TrySerializeSettingsJson(Settings, JsonText))
		{
			return false;
		}
		return UnrealAgentMCP::PanelSettingsStorage::WriteAtomically(SettingsPath, JsonText);
	}
}
