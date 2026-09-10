// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPPanelSettings.cpp
 * @brief 面板样式、设置持久化与 CLI 交互逻辑。
 */

#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Presentation/Panel/SUnrealAgentMCPPanelPrivate.h"
#include "Presentation/Widgets/UnrealAgentMCPToolTip.h"

#include "Application/CLI/WorldDataCliProcessRules.h"
#include "Async/Async.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace
{
	FString GetCodexAuthCommandInterpreterPath()
	{
#if PLATFORM_WINDOWS
		FString CommandInterpreter = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
		if (UnrealAgentMCP::PathExists(CommandInterpreter))
		{
			return CommandInterpreter;
		}

		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		CommandInterpreter = FPaths::Combine(SystemRoot, TEXT("System32"), TEXT("cmd.exe"));
		if (UnrealAgentMCP::PathExists(CommandInterpreter))
		{
			return CommandInterpreter;
		}
#endif
		return FString();
	}

	FString GetCodexAuthPowerShellPath()
	{
#if PLATFORM_WINDOWS
		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		const FString PowerShell = FPaths::Combine(SystemRoot, TEXT("System32"), TEXT("WindowsPowerShell"), TEXT("v1.0"), TEXT("powershell.exe"));
		if (UnrealAgentMCP::PathExists(PowerShell))
		{
			return PowerShell;
		}
#endif
		return FString();
	}

	FText GetInstallerStageText(const FString& Stage)
	{
		if (Stage == TEXT("detecting"))
		{
			return LOCTEXT("InstallerDetectingStage", "正在检测");
		}
		if (Stage == TEXT("resolving-version"))
		{
			return LOCTEXT("InstallerResolvingVersionStage", "正在获取版本");
		}
		if (Stage == TEXT("downloading"))
		{
			return LOCTEXT("InstallerDownloadingStage", "正在下载");
		}
		if (Stage == TEXT("extracting"))
		{
			return LOCTEXT("InstallerExtractingStage", "正在解压");
		}
		if (Stage == TEXT("ready"))
		{
			return LOCTEXT("InstallerReadyStage", "准备完成");
		}
		if (Stage == TEXT("installing"))
		{
			return LOCTEXT("InstallerInstallingStage", "正在安装");
		}
		if (Stage == TEXT("downloading-installing"))
		{
			return LOCTEXT("InstallerDownloadingInstallingStage", "正在下载并安装");
		}
		if (Stage == TEXT("verifying"))
		{
			return LOCTEXT("InstallerVerifyingStage", "正在验证");
		}
		if (Stage == TEXT("configuring"))
		{
			return LOCTEXT("InstallerConfiguringStage", "正在配置");
		}
		if (Stage == TEXT("complete"))
		{
			return LOCTEXT("InstallerCompleteStage", "部署完成");
		}
		return LOCTEXT("InstallerPreparingStage", "正在准备");
	}

	FText GetProviderInstallerFailureText(const FString& Output)
	{
		if (Output.Contains(TEXT("administrator-approval-canceled")))
		{
			return LOCTEXT("ProviderInstallerApprovalCanceled", "自动部署暂停：Windows 管理员授权被取消。请再次点击安装，并在 UAC 窗口选择“是”以启用 WSL。");
		}
		if (Output.Contains(TEXT("administrator-approval-required")))
		{
			return LOCTEXT("ProviderInstallerApprovalRequired", "自动部署需要 Windows 管理员权限来启用 WSL。请再次点击安装并批准 UAC 授权。");
		}
		if (Output.Contains(TEXT("windows-restart-required")))
		{
			return LOCTEXT("ProviderInstallerRestartRequired", "WSL 已自动启用，但 Windows 必须重启一次才能继续。重启后再次点击安装，Cursor 部署会自动续接。");
		}
		if (Output.Contains(TEXT("wsl-install-failed")))
		{
			return LOCTEXT("ProviderInstallerWslFailed", "WSL 自动安装失败。详细诊断已写入 Saved/UnrealAgent/Logs/provider-installer-error.json。");
		}
		return FText::GetEmpty();
	}
}

void SUnrealAgentMCPPanel::ConfigureLightTextBoxStyle()
{
	LightTextBoxStyle = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	const FSlateRoundedBoxBrush NormalBrush(UnrealAgentMCP::Palette::SurfaceRaised(), 10.0f);
	const FSlateRoundedBoxBrush HoveredBrush(UnrealAgentMCP::Palette::ControlHover(), 10.0f);
	const FSlateRoundedBoxBrush FocusedBrush(UnrealAgentMCP::Palette::SurfaceRaised(), 10.0f, UnrealAgentMCP::Palette::Primary(), 1.0f);

	LightTextBoxStyle.SetBackgroundImageNormal(NormalBrush).SetBackgroundImageHovered(HoveredBrush).SetBackgroundImageFocused(FocusedBrush).SetBackgroundImageReadOnly(NormalBrush);

	FTextBlockStyle TextStyle = LightTextBoxStyle.TextStyle;
	TextStyle.SetColorAndOpacity(FSlateColor(GetPanelTextColor()));
	LightTextBoxStyle.SetTextStyle(TextStyle)
		.SetForegroundColor(FSlateColor(GetPanelTextColor()))
		.SetFocusedForegroundColor(FSlateColor(GetPanelTextColor()))
		.SetReadOnlyForegroundColor(FSlateColor(GetPanelTextColor()))
		// 保持颜色乘数为白色；Brush 已经包含 UE 表面色调。
		.SetBackgroundColor(FSlateColor(FLinearColor::White))
		.SetPadding(FMargin(8.0f, 6.0f));
}

void SUnrealAgentMCPPanel::ConfigureComposerButtonStyle()
{
	ComposerButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
	const FSlateRoundedBoxBrush NormalBrush(UnrealAgentMCP::Palette::Background(), 7.0f);
	const FSlateRoundedBoxBrush HoveredBrush(UnrealAgentMCP::Palette::ControlHover(), 7.0f);
	const FSlateRoundedBoxBrush PressedBrush(UnrealAgentMCP::Palette::ControlPressed(), 7.0f);
	const FSlateRoundedBoxBrush DisabledBrush(UnrealAgentMCP::Palette::Surface(), 7.0f);

	ComposerButtonStyle.SetNormal(NormalBrush).SetHovered(HoveredBrush).SetPressed(PressedBrush).SetDisabled(DisabledBrush);

	const FSlateColor TextColor(UnrealAgentMCP::Palette::Text());
	ComposerButtonStyle.SetNormalForeground(TextColor)
		.SetHoveredForeground(TextColor)
		.SetPressedForeground(TextColor)
		.SetDisabledForeground(FSlateColor(UnrealAgentMCP::Palette::TextDisabled()))
		.SetNormalPadding(FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	ToolbarButtonStyle = ComposerButtonStyle;
	const FSlateRoundedBoxBrush ToolbarNormalBrush(UnrealAgentMCP::Palette::Control(), 6.0f);
	const FSlateRoundedBoxBrush ToolbarHoveredBrush(UnrealAgentMCP::Palette::Blend(UnrealAgentMCP::Palette::ControlHover(), UnrealAgentMCP::Palette::Primary(), 0.34f), 6.0f);
	const FSlateRoundedBoxBrush ToolbarPressedBrush(UnrealAgentMCP::Palette::PrimaryPressed(), 6.0f);

	ToolbarButtonStyle.SetNormal(ToolbarNormalBrush).SetHovered(ToolbarHoveredBrush).SetPressed(ToolbarPressedBrush);

	ComposerComboButtonStyle = FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");
	ComposerComboButtonStyle.SetButtonStyle(ComposerButtonStyle).SetContentPadding(FMargin(0.0f)).SetMenuBorderPadding(FMargin(0.0f));
	const FSlateRoundedBoxBrush MenuBorderBrush(UnrealAgentMCP::Palette::Border(), 8.0f);
	ComposerComboButtonStyle.SetMenuBorderBrush(MenuBorderBrush);
}

FLinearColor SUnrealAgentMCPPanel::GetAccentFillColor(float Alpha) const
{
	return UnrealAgentMCP::Palette::Blend(GetPanelBackgroundColor(), GetEffectiveAccentColor(), Alpha);
}

FLinearColor SUnrealAgentMCPPanel::GetAccentSurfaceColor() const
{
	return GetAccentFillColor(0.08f);
}

FLinearColor SUnrealAgentMCPPanel::GetAccentControlColor() const
{
	return GetAccentFillColor(0.12f);
}

FLinearColor SUnrealAgentMCPPanel::GetAccentBorderColor() const
{
	return UnrealAgentMCP::Palette::Blend(GetPanelBorderColor(), GetEffectiveAccentColor(), 0.40f);
}

FLinearColor SUnrealAgentMCPPanel::GetAccentButtonColor() const
{
	return GetEffectiveAccentColor();
}

FLinearColor SUnrealAgentMCPPanel::GetAccentButtonTextColor() const
{
	const FLinearColor ButtonColor = GetAccentButtonColor();
	const float Luma = ButtonColor.R * 0.2126f + ButtonColor.G * 0.7152f + ButtonColor.B * 0.0722f;
	return Luma > 0.60f ? GetPanelTextColor() : UnrealAgentMCP::Palette::OnPrimary();
}

FLinearColor SUnrealAgentMCPPanel::GetReadableAccentTextColor() const
{
	return GetPanelTextColor();
}

FLinearColor SUnrealAgentMCPPanel::GetPanelSubduedTextColor() const
{
	return GetPanelMutedTextColor();
}

FLinearColor SUnrealAgentMCPPanel::GetEffectiveAccentColor() const
{
	return ResolveAccentColor(SettingsColor);
}

FLinearColor SUnrealAgentMCPPanel::ResolveAccentColor(const FLinearColor& Color)
{
	const float MaxChannel = FMath::Max3(Color.R, Color.G, Color.B);
	const float MinChannel = FMath::Min3(Color.R, Color.G, Color.B);
	const float Luma = Color.R * 0.2126f + Color.G * 0.7152f + Color.B * 0.0722f;
	const bool bLooksLikeWhite = Luma > 0.92f && (MaxChannel - MinChannel) < 0.06f;
	return bLooksLikeWhite ? UnrealAgentMCP::Palette::Primary() : Color;
}

FLinearColor SUnrealAgentMCPPanel::GetPanelBackgroundColor()
{
	return UnrealAgentMCP::Palette::Background();
}

FLinearColor SUnrealAgentMCPPanel::GetPanelSurfaceColor()
{
	return UnrealAgentMCP::Palette::Surface();
}

FLinearColor SUnrealAgentMCPPanel::GetPanelBorderColor()
{
	return UnrealAgentMCP::Palette::Border();
}

FLinearColor SUnrealAgentMCPPanel::GetPanelTextColor()
{
	return UnrealAgentMCP::Palette::Text();
}

FLinearColor SUnrealAgentMCPPanel::GetPanelMutedTextColor()
{
	return UnrealAgentMCP::Palette::TextMuted();
}

bool SUnrealAgentMCPPanel::IsSettingsColorSelected(const FLinearColor& Color) const
{
	return FMath::IsNearlyEqual(SettingsColor.R, Color.R, 0.01f) && FMath::IsNearlyEqual(SettingsColor.G, Color.G, 0.01f) && FMath::IsNearlyEqual(SettingsColor.B, Color.B, 0.01f);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildColorPresetButton(const FLinearColor& Color, const FText& Tooltip)
{
		return SNew(SButton)
			.ContentPadding(FMargin(2.0f))
			.ToolTip(UnrealAgentMCPToolTip::Make(Tooltip))
			.ButtonColorAndOpacity_Lambda([this, Color]
			{
				const FLinearColor ResolvedAccent = ResolveAccentColor(Color);
				return IsSettingsColorSelected(Color)
					? FSlateColor(UnrealAgentMCP::Palette::Blend(GetPanelBackgroundColor(), ResolvedAccent, 0.30f))
					: FSlateColor(UnrealAgentMCP::Palette::Blend(GetPanelBackgroundColor(), ResolvedAccent, 0.08f));
			})
			.OnClicked_Lambda([this, Color]
			{
				ApplySettingsColor(Color);
				return FReply::Handled();
			})
			[
				SNew(SColorBlock)
				.Color(Color)
				.Size(FVector2D(26.0f, 18.0f))
				.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
				.OnMouseButtonDown_Lambda([this, Color](const FGeometry&, const FPointerEvent&)
				{
					ApplySettingsColor(Color);
					return FReply::Handled();
				})
			];
	}

FText SUnrealAgentMCPPanel::GetCliTitle(ECliTool Tool) const
{
	return Tool == ECliTool::Codex ? LOCTEXT("CodexCliTitle", "Codex CLI") : LOCTEXT("CursorCliTitle", "Cursor CLI");
}

FText SUnrealAgentMCPPanel::GetCliDescription(ECliTool Tool) const
{
	return Tool == ECliTool::Codex ? LOCTEXT("CodexCliDescription", "Codex CLI 提供官方账户登录；面板对话通过 codex-acp 直接注入 MCP，不依赖 ~/.codex/config.toml。")
								   : LOCTEXT("CursorCliDescription", "Cursor Agent CLI 通过官方 `agent acp` 接入面板；模型、模式和登录状态由 Agent 实时公布。");
}

FString SUnrealAgentMCPPanel::GetCliCommandName(ECliTool Tool) const
{
	return UnrealAgentMCPPanelSettings::GetCliCommandName(Tool);
}

FString SUnrealAgentMCPPanel::GetCliConfiguredPath(ECliTool Tool) const
{
	return Tool == ECliTool::Codex ? CodexCliPath : CursorCliPath;
}

FString SUnrealAgentMCPPanel::GetCliDetectedPath(ECliTool Tool) const
{
	return Tool == ECliTool::Codex ? DetectedCodexCliPath : DetectedCursorCliPath;
}

FString SUnrealAgentMCPPanel::GetCliEffectivePath(ECliTool Tool) const
{
	return UnrealAgentMCPPanelSettings::ResolveEffectiveCliPath(GetCliConfiguredPath(Tool), GetCliDetectedPath(Tool));
}

bool SUnrealAgentMCPPanel::IsCliAvailable(ECliTool Tool) const
{
	return !GetCliEffectivePath(Tool).IsEmpty();
}

FText SUnrealAgentMCPPanel::GetCliPathSummary(ECliTool Tool) const
{
	const FString EffectivePath = GetCliEffectivePath(Tool);
	if (!EffectivePath.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("当前有效路径：%s"), *EffectivePath));
	}

	const FString Configured = UnrealAgentMCP::StripOuterQuotes(GetCliConfiguredPath(Tool));
	if (!Configured.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("已填写路径但无法访问：%s"), *Configured));
	}
	return FText::FromString(FString::Printf(TEXT("未检测到 %s。可以自动检测、手动填写路径，或打开下载页。"), *GetCliCommandName(Tool)));
}

void SUnrealAgentMCPPanel::RefreshCliDetections()
{
	DetectedCodexCliPath = UnrealAgentMCPPanelSettings::DetectCliPath(ECliTool::Codex);
	DetectedCursorCliPath = UnrealAgentMCPPanelSettings::DetectCliPath(ECliTool::Cursor);
	DetectedNpmPath = UnrealAgentMCPPanelSettings::DetectNpmPath();
}

bool SUnrealAgentMCPPanel::IsProviderDependencyAvailable() const
{
	const TSharedPtr<FUnrealAgentCodexACPClient> ControllerClient = GetModelConfigClient();
	return bProviderDependencyAvailable || (ControllerClient.IsValid() && (ControllerClient->IsReady() || ControllerClient->IsRunning()));
}

void SUnrealAgentMCPPanel::RefreshProviderDependencyState()
{
	const TSharedPtr<FUnrealAgentCodexACPClient> ControllerClient = GetModelConfigClient();
	bProviderDependencyAvailable = ControllerClient.IsValid() && (ControllerClient->IsReady() || ControllerClient->IsRunning() || ControllerClient->CanLaunchAgent());
}

void SUnrealAgentMCPPanel::SetCliConfiguredPath(ECliTool Tool, const FString& NewPath)
{
	const FString Normalized = UnrealAgentMCPPanelSettings::NormalizeConfiguredCliPath(NewPath);

	if (Tool == ECliTool::Codex)
	{
		CodexCliPath = Normalized;
	}
	else
	{
		CursorCliPath = Normalized;
	}

	SaveSettings();
	SetLastAction(FText::FromString(FString::Printf(TEXT("%s 路径已保存。"), *GetCliTitle(Tool).ToString())));
	RefreshCliDetections();
	const TSharedPtr<FUnrealAgentCodexACPClient> ConfiguredClient =
		GetAcpClientForProvider(Tool == ECliTool::Codex ? EUnrealAgentACPProvider::Codex : EUnrealAgentACPProvider::Cursor);
	if (ConfiguredClient.IsValid())
	{
		if (Tool == ECliTool::Codex)
		{
			ConfiguredClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex));
		}
		else
		{
			ConfiguredClient->SetAgentProvider(EUnrealAgentACPProvider::Cursor, GetCliEffectivePath(ECliTool::Cursor));
		}
		if (ConfiguredClient->CanLaunchAgent())
		{
			ConfiguredClient->Connect();
		}
	}
	RefreshProviderDependencyState();
	if (Tool == ECliTool::Codex)
	{
		RefreshCodexAccountState();
	}
}

FReply SUnrealAgentMCPPanel::OnDetectCliClicked(ECliTool Tool)
{
	RefreshCliDetections();
	const FString DetectedPath = GetCliDetectedPath(Tool);
	if (DetectedPath.IsEmpty())
	{
		SetLastAction(FText::FromString(FString::Printf(TEXT("未在 PATH 中找到 %s。"), *GetCliCommandName(Tool))));
		return FReply::Handled();
	}

	SetCliConfiguredPath(Tool, DetectedPath);
	SetLastAction(FText::FromString(FString::Printf(TEXT("已检测到 %s：%s"), *GetCliCommandName(Tool), *DetectedPath)));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnClearCliClicked(ECliTool Tool)
{
	if (Tool == ECliTool::Codex)
	{
		CodexCliPath.Empty();
	}
	else
	{
		CursorCliPath.Empty();
	}

	RefreshCliDetections();
	SaveSettings();
	const TSharedPtr<FUnrealAgentCodexACPClient> ConfiguredClient =
		GetAcpClientForProvider(Tool == ECliTool::Codex ? EUnrealAgentACPProvider::Codex : EUnrealAgentACPProvider::Cursor);
	if (ConfiguredClient.IsValid())
	{
		if (Tool == ECliTool::Codex)
		{
			ConfiguredClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex));
		}
		else
		{
			ConfiguredClient->SetAgentProvider(EUnrealAgentACPProvider::Cursor, GetCliEffectivePath(ECliTool::Cursor));
		}
		if (ConfiguredClient->CanLaunchAgent())
		{
			ConfiguredClient->Connect();
		}
	}
	RefreshProviderDependencyState();
	if (Tool == ECliTool::Codex)
	{
		RefreshCodexAccountState();
	}
	SetLastAction(FText::FromString(FString::Printf(TEXT("%s 路径已清空，将重新使用 PATH 自动检测。"), *GetCliTitle(Tool).ToString())));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnDownloadCliClicked(ECliTool Tool)
{
	const FString Url = Tool == ECliTool::Codex ? UnrealAgentMCP::GetCodexCliInstallUrl() : UnrealAgentMCP::GetCursorCliInstallUrl();
	FString Error;
	if (UnrealAgentMCP::OpenExternalUrl(Url, Error))
	{
		SetLastAction(FText::FromString(FString::Printf(TEXT("已打开 %s 下载/安装页面。"), *GetCliTitle(Tool).ToString())));
	}
	else
	{
		SetLastAction(FText::FromString(FString::Printf(TEXT("打开下载页面失败：%s"), *Error)));
	}
	return FReply::Handled();
}

void SUnrealAgentMCPPanel::StartProviderDependencyInstallProcess()
{
	if (ProviderDependencyInstallProcess.IsValid() && ProviderDependencyInstallProcess->IsRunning())
	{
		return;
	}

	const FString PowerShellPath = GetCodexAuthPowerShellPath();
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent"));
	if (PowerShellPath.IsEmpty() || !Plugin.IsValid())
	{
		ProviderDependencyFeedback = LOCTEXT("ProviderInstallerUnavailable", "无法启动自动安装器：未找到 PowerShell 或 Unreal Agent 插件目录。");
		return;
	}

	FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Scripts"), TEXT("Install-AgentCli.ps1")));
	FString SavedDirectory = UnrealAgentMCPPanelSettings::NormalizeInstallerDirectoryArgument(FPaths::ProjectSavedDir());
	FPaths::MakePlatformFilename(ScriptPath);
	if (!FPaths::FileExists(ScriptPath))
	{
		ProviderDependencyFeedback = FText::Format(LOCTEXT("ProviderInstallerScriptMissing", "无法启动自动安装器：缺少脚本 {0}"), FText::FromString(ScriptPath));
		return;
	}

	InstallingProvider = ActiveProvider;
	const TCHAR* ProviderName = InstallingProvider == EUnrealAgentACPProvider::Cursor ? TEXT("Cursor") : TEXT("Codex");
	FString Arguments = FString::Printf(TEXT("-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -Provider %s -ProjectSavedDirectory \"%s\""), *ScriptPath,
		ProviderName, *SavedDirectory);
	if (InstallingProvider == EUnrealAgentACPProvider::Codex && !DetectedNpmPath.IsEmpty())
	{
		Arguments += FString::Printf(TEXT(" -NpmPath \"%s\""), *DetectedNpmPath);
	}

	ProviderDependencyInstallProcessOutput.Empty();
	ProviderDependencyFeedback = FText::Format(LOCTEXT("ProviderInstallStarting", "正在部署 {0}：正在准备（1%）"), FText::FromString(ProviderName));
	ProviderDependencyInstallProcess = MakeShared<FInteractiveProcess>(PowerShellPath, Arguments, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), true, true);

	const TWeakPtr<SUnrealAgentMCPPanel> WeakSelf = SharedThis(this);
	ProviderDependencyInstallProcess->OnOutput().BindLambda(
		[WeakSelf](const FString& Output)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, Output]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->ApplyProviderDependencyInstallOutput(Output);
					}
				});
		});
	ProviderDependencyInstallProcess->OnCompleted().BindLambda(
		[WeakSelf](int32 ReturnCode, bool bCanceled)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, ReturnCode, bCanceled]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->CompleteProviderDependencyInstallProcess(ReturnCode, bCanceled);
					}
				});
		});

	if (!ProviderDependencyInstallProcess->Launch())
	{
		ProviderDependencyInstallProcess.Reset();
		ProviderDependencyFeedback = LOCTEXT("ProviderInstallerLaunchFailed", "无法启动自动安装器，请检查 PowerShell 是否可用。");
	}
}

void SUnrealAgentMCPPanel::ApplyProviderDependencyInstallOutput(const FString& Output)
{
	ProviderDependencyInstallProcessOutput += Output;
	constexpr int32 MaxOutputCharacters = 65536;
	if (ProviderDependencyInstallProcessOutput.Len() > MaxOutputCharacters)
	{
		ProviderDependencyInstallProcessOutput.RightChopInline(ProviderDependencyInstallProcessOutput.Len() - MaxOutputCharacters);
	}

	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		int32 Percent = 0;
		FString Stage;
		FString Component;
		if (UnrealAgentMCPPanelSettings::IsInstallerProgressLine(Line, Percent, Stage, Component) && Stage != TEXT("failed"))
		{
			ProviderDependencyFeedback = FText::Format(LOCTEXT("ProviderInstallProgress", "正在部署 {0}：{1}（{2}%）"), FText::FromString(Component), GetInstallerStageText(Stage),
				FText::AsNumber(Percent));
		}
	}
}

void SUnrealAgentMCPPanel::CompleteProviderDependencyInstallProcess(int32 ReturnCode, bool bCanceled)
{
	ProviderDependencyInstallProcess.Reset();
	if (bCanceled)
	{
		ProviderDependencyFeedback = LOCTEXT("ProviderInstallCanceledFeedback", "自动部署已取消。");
		SetLastAction(ProviderDependencyFeedback);
		UnrealAgentMCP::Notify(ProviderDependencyFeedback);
		return;
	}

	RefreshCliDetections();
	if (InstallingProvider == EUnrealAgentACPProvider::Codex && !CodexCliPath.IsEmpty() && GetCliEffectivePath(ECliTool::Codex).IsEmpty() && !DetectedCodexCliPath.IsEmpty())
	{
		CodexCliPath.Empty();
		SaveSettings();
	}
	if (InstallingProvider == EUnrealAgentACPProvider::Cursor && !CursorCliPath.IsEmpty() && GetCliEffectivePath(ECliTool::Cursor).IsEmpty() && !DetectedCursorCliPath.IsEmpty())
	{
		CursorCliPath.Empty();
		SaveSettings();
	}
	if (AcpClient.IsValid())
	{
		AcpClient->SetAgentProvider(ActiveProvider, ActiveProvider == EUnrealAgentACPProvider::Cursor ? GetCliEffectivePath(ECliTool::Cursor) : FString());
		AcpClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex));
	}
	RefreshProviderDependencyState();

	const bool bInstalledDependencyAvailable = InstallingProvider == EUnrealAgentACPProvider::Codex ? !DetectedCodexCliPath.IsEmpty() : !DetectedCursorCliPath.IsEmpty();
	if (ReturnCode == 0 && bInstalledDependencyAvailable)
	{
		ProviderDependencyFeedback = FText::Format(LOCTEXT("ProviderInstallCompletedAction", "{0} 已部署并验证完成（100%），正在自动检测和连接。"),
			InstallingProvider == EUnrealAgentACPProvider::Cursor ? LOCTEXT("InstalledCursorProviderName", "Cursor Agent CLI")
																  : LOCTEXT("InstalledCodexProviderName", "Codex CLI 与 Codex ACP"));
		SetLastAction(ProviderDependencyFeedback);
		UnrealAgentMCP::Notify(ProviderDependencyFeedback);
		if (InstallingProvider == ActiveProvider && AcpClient.IsValid())
		{
			AcpClient->Stop();
			AcpClient->Connect();
		}
		if (InstallingProvider == EUnrealAgentACPProvider::Codex)
		{
			RefreshCodexAccountState();
		}
		else
		{
			StartCursorAuthProcess(ECodexAuthAction::Status);
		}
		return;
	}

	const FText KnownFailure = GetProviderInstallerFailureText(ProviderDependencyInstallProcessOutput);
	if (!KnownFailure.IsEmpty())
	{
		ProviderDependencyFeedback = KnownFailure;
		SetLastAction(ProviderDependencyFeedback);
		UnrealAgentMCP::Notify(ProviderDependencyFeedback);
		return;
	}

	FString Diagnostic = ProviderDependencyInstallProcessOutput;
	TArray<FString> DiagnosticLines;
	Diagnostic.ParseIntoArrayLines(DiagnosticLines, true);
	Diagnostic.Empty();
	for (const FString& Line : DiagnosticLines)
	{
		if (!Line.StartsWith(TEXT("UEBRIDGE_PROGRESS|")))
		{
			Diagnostic += Line + TEXT(" ");
		}
	}
	Diagnostic.TrimStartAndEndInline();
	if (Diagnostic.Len() > 320)
	{
		Diagnostic = Diagnostic.Right(320);
	}
	ProviderDependencyFeedback =
		FText::FromString(FString::Printf(TEXT("自动部署失败（退出码 %d）：%s"), ReturnCode, Diagnostic.IsEmpty() ? TEXT("请检查网络连接和系统组件后重试。") : *Diagnostic));
	SetLastAction(ProviderDependencyFeedback);
	UnrealAgentMCP::Notify(ProviderDependencyFeedback);
}

FReply SUnrealAgentMCPPanel::OnProviderDependencyPrimaryClicked()
{
	StartProviderDependencyInstallProcess();
	SetLastAction(ProviderDependencyFeedback);
	UnrealAgentMCP::Notify(ProviderDependencyFeedback);
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnRefreshProviderDependencyClicked()
{
	RefreshCliDetections();
	if (AcpClient.IsValid())
	{
		AcpClient->SetAgentProvider(ActiveProvider, ActiveProvider == EUnrealAgentACPProvider::Cursor ? GetCliEffectivePath(ECliTool::Cursor) : FString());
		AcpClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex));
	}
	RefreshProviderDependencyState();

	if (IsProviderDependencyAvailable() && AcpClient.IsValid())
	{
		ProviderDependencyFeedback = LOCTEXT("ProviderDependencyDetectedAction", "依赖已检测到，正在自动连接。");
		SetLastAction(ProviderDependencyFeedback);
		UnrealAgentMCP::Notify(ProviderDependencyFeedback);
		AcpClient->Stop();
		AcpClient->Connect();
	}
	else
	{
		ProviderDependencyFeedback = FText::Format(LOCTEXT("ProviderDependencyStillMissingAction", "重新检测完成：仍未发现 {0}。可点击“一键部署”自动完成环境安装和配置。"),
			ActiveProvider == EUnrealAgentACPProvider::Cursor ? LOCTEXT("MissingCursorProviderName", "Cursor Agent CLI")
															  : LOCTEXT("MissingCodexProviderName", "Codex CLI / Codex ACP"));
		SetLastAction(ProviderDependencyFeedback);
		UnrealAgentMCP::Notify(ProviderDependencyFeedback);
	}
	return FReply::Handled();
}

void SUnrealAgentMCPPanel::RefreshCodexAccountState()
{
	const FUnrealAgentACPAccountState LocalState = UnrealAgentACPProviderModel::DetectAccountState(EUnrealAgentACPProvider::Codex);
	bCodexAuthenticated = LocalState.bAuthenticated;
	DetectedAccountLabel = LocalState.DisplayLabel;
	DetectedAccountSecondaryLabel = LocalState.SecondaryLabel;

	if (IsCliAvailable(ECliTool::Codex))
	{
		StartCodexAuthProcess(ECodexAuthAction::Status);
	}
}

bool SUnrealAgentMCPPanel::StartCodexAuthProcess(ECodexAuthAction Action)
{
	AuthProcessStartError.Reset();
	if (CodexAuthProcess.IsValid() && CodexAuthProcess->IsRunning())
	{
		AuthProcessStartError = TEXT("Codex 账户操作正在进行，请稍候。");
		return false;
	}

	const FString CodexPath = GetCliEffectivePath(ECliTool::Codex);
	if (CodexPath.IsEmpty())
	{
		AuthProcessStartError = TEXT("未找到 Codex CLI，无法启动账户操作。");
		return false;
	}

	FString Arguments;
	switch (Action)
	{
	case ECodexAuthAction::Login:
		Arguments = TEXT("login");
		break;
	case ECodexAuthAction::Logout:
		Arguments = TEXT("logout");
		break;
	case ECodexAuthAction::Status:
	default:
		Arguments = TEXT("login status");
		break;
	}

	FWorldDataCliProcessLaunchSpec LaunchSpec;
	if (!WorldDataCliProcessRules::BuildLaunchSpec(CodexPath, Arguments, GetCodexAuthCommandInterpreterPath(), GetCodexAuthPowerShellPath(), LaunchSpec))
	{
		AuthProcessStartError = TEXT("无法为当前 Codex CLI 构造安全的启动命令。");
		return false;
	}

	ActiveCodexAuthAction = Action;
	CodexAuthProcessOutput.Empty();
	FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDirectory);

	CodexAuthProcess = MakeShared<FInteractiveProcess>(LaunchSpec.Executable, LaunchSpec.Arguments, WorkingDirectory, true, true);
	const TWeakPtr<SUnrealAgentMCPPanel> WeakSelf = SharedThis(this);
	CodexAuthProcess->OnOutput().BindLambda(
		[WeakSelf, Action](const FString& Output)
		{
			if (Action != ECodexAuthAction::Status)
			{
				return;
			}
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, Action, Output]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->CodexAuthProcessOutput += Output;
					}
				});
		});
	CodexAuthProcess->OnCompleted().BindLambda(
		[WeakSelf, Action](int32 ReturnCode, bool bCanceled)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, Action, ReturnCode, bCanceled]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->HandleCodexAuthProcessCompleted(Action, ReturnCode, bCanceled);
					}
				});
		});

	if (!CodexAuthProcess->Launch())
	{
		CodexAuthProcess.Reset();
		AuthProcessStartError = TEXT("Codex CLI 进程启动失败，请检查路径和系统权限。");
		return false;
	}
	return true;
}

void SUnrealAgentMCPPanel::HandleCodexAuthProcessCompleted(ECodexAuthAction Action, int32 ReturnCode, bool bCanceled)
{
	CodexAuthProcess.Reset();
	if (bCanceled)
	{
		return;
	}

	if (Action == ECodexAuthAction::Status)
	{
		const bool bWasAuthenticated = bCodexAuthenticated;
		const FUnrealAgentACPAccountState LocalState = UnrealAgentACPProviderModel::DetectAccountState(EUnrealAgentACPProvider::Codex);
		bCodexAuthenticated = LocalState.bAuthenticated || ReturnCode == 0;
		const FString AccountLabel = LocalState.bAuthenticated ? LocalState.DisplayLabel : (bCodexAuthenticated ? FString(TEXT("Codex 已登录")) : FString(TEXT("Codex 未登录")));
		if (ActiveProvider == EUnrealAgentACPProvider::Codex)
		{
			DetectedAccountLabel = AccountLabel;
			DetectedAccountSecondaryLabel = LocalState.SecondaryLabel;
		}
		if (!bWasAuthenticated && bCodexAuthenticated && CodexAcpClient.IsValid())
		{
			CodexAcpClient->Connect();
		}
		if (ActiveProvider == EUnrealAgentACPProvider::Codex)
		{
			SetAccountActionFeedback(bCodexAuthenticated ? LOCTEXT("CodexAccountRefreshedAction", "Codex 账户信息已刷新。")
														 : LOCTEXT("CodexAccountSignedOutAction", "Codex 当前未登录。"),
				false);
		}
		return;
	}

	if (ReturnCode != 0)
	{
		SetLastAction(Action == ECodexAuthAction::Login ? LOCTEXT("CodexLoginFailedAction", "Codex 登录未完成；请检查浏览器登录页或 CLI 状态。")
														: LOCTEXT("CodexLogoutFailedAction", "Codex 退出登录失败。"));
		return;
	}

	if (Action == ECodexAuthAction::Login)
	{
		SetLastAction(LOCTEXT("CodexLoginCompletedAction", "Codex 登录已完成，正在刷新账户和 ACP 会话。"));
		if (CodexAcpClient.IsValid())
		{
			CodexAcpClient->Stop();
			CodexAcpClient->Connect();
		}
	}
	else
	{
		bCodexAuthenticated = false;
		if (ActiveProvider == EUnrealAgentACPProvider::Codex)
		{
			DetectedAccountLabel = TEXT("Codex 未登录");
			DetectedAccountSecondaryLabel.Empty();
		}
		SetLastAction(LOCTEXT("CodexLogoutCompletedAction", "已退出 Codex 登录。"));
		if (CodexAcpClient.IsValid())
		{
			CodexAcpClient->Stop();
		}
	}
	RefreshCodexAccountState();
}

void SUnrealAgentMCPPanel::SetAccountActionFeedback(const FText& Feedback, const bool bNotify)
{
	AccountActionFeedback = Feedback;
	SetLastAction(Feedback);
	if (bNotify)
	{
		UnrealAgentMCP::Notify(Feedback);
	}
}

FReply SUnrealAgentMCPPanel::OnCodexLoginClicked()
{
	if (!IsCliAvailable(ECliTool::Codex))
	{
		bShowSettings = true;
		SetAccountActionFeedback(LOCTEXT("CodexLoginCliMissingAction", "未检测到 Codex CLI；请先在设置中安装或选择 codex。"));
		return FReply::Handled();
	}

	if (StartCodexAuthProcess(ECodexAuthAction::Login))
	{
		SetAccountActionFeedback(LOCTEXT("CodexLoginStartedAction", "正在打开 Codex 官方浏览器登录流程…"));
	}
	else
	{
		SetAccountActionFeedback(FText::FromString(AuthProcessStartError.IsEmpty() ? TEXT("Codex 登录流程未能启动。") : AuthProcessStartError));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCodexLogoutClicked()
{
	if (StartCodexAuthProcess(ECodexAuthAction::Logout))
	{
		SetAccountActionFeedback(LOCTEXT("CodexLogoutStartedAction", "正在退出 Codex 登录…"));
	}
	else
	{
		SetAccountActionFeedback(FText::FromString(AuthProcessStartError.IsEmpty() ? TEXT("Codex 退出流程未能启动。") : AuthProcessStartError));
	}
	return FReply::Handled();
}

bool SUnrealAgentMCPPanel::StartCursorAuthProcess(ECodexAuthAction Action)
{
	AuthProcessStartError.Reset();
	if (CursorAuthProcess.IsValid() && CursorAuthProcess->IsRunning())
	{
		AuthProcessStartError = TEXT("Cursor 账户操作正在进行，请稍候。");
		return false;
	}

	const FString CursorPath = GetCliEffectivePath(ECliTool::Cursor);
	if (CursorPath.IsEmpty())
	{
		AuthProcessStartError = TEXT("未找到 Cursor Agent CLI，无法启动账户操作。");
		return false;
	}

	FString Arguments;
	switch (Action)
	{
	case ECodexAuthAction::Login:
		Arguments = TEXT("login");
		break;
	case ECodexAuthAction::Logout:
		Arguments = TEXT("logout");
		break;
	case ECodexAuthAction::Status:
	default:
		Arguments = TEXT("status --format json");
		break;
	}

	FWorldDataCliProcessLaunchSpec LaunchSpec;
	if (!WorldDataCliProcessRules::BuildLaunchSpec(CursorPath, Arguments, GetCodexAuthCommandInterpreterPath(), GetCodexAuthPowerShellPath(), LaunchSpec))
	{
		AuthProcessStartError = TEXT("无法为当前 Cursor CLI 构造安全的启动命令。");
		return false;
	}

	ActiveCursorAuthAction = Action;
	CursorAuthProcessOutput.Empty();
	if (Action == ECodexAuthAction::Login)
	{
		LastOpenedCursorLoginUrl.Empty();
	}
	FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDirectory);

	CursorAuthProcess = MakeShared<FInteractiveProcess>(LaunchSpec.Executable, LaunchSpec.Arguments, WorkingDirectory, true, true);
	const TWeakPtr<SUnrealAgentMCPPanel> WeakSelf = SharedThis(this);
	CursorAuthProcess->OnOutput().BindLambda(
		[WeakSelf, Action](const FString& Output)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, Action, Output]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->HandleCursorAuthProcessOutput(Action, Output);
					}
				});
		});
	CursorAuthProcess->OnCompleted().BindLambda(
		[WeakSelf, Action](int32 ReturnCode, bool bCanceled)
		{
			AsyncTask(ENamedThreads::GameThread,
				[WeakSelf, Action, ReturnCode, bCanceled]()
				{
					if (const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin())
					{
						Self->HandleCursorAuthProcessCompleted(Action, ReturnCode, bCanceled);
					}
				});
		});

	if (!CursorAuthProcess->Launch())
	{
		CursorAuthProcess.Reset();
		AuthProcessStartError = TEXT("Cursor CLI 进程启动失败，请检查路径和系统权限。");
		return false;
	}
	return true;
}

void SUnrealAgentMCPPanel::HandleCursorAuthProcessOutput(ECodexAuthAction Action, const FString& Output)
{
	CursorAuthProcessOutput += Output;
	if (Action != ECodexAuthAction::Login)
	{
		return;
	}

	const FString UrlPrefix = TEXT("https://cursor.com/");
	const int32 UrlStart = CursorAuthProcessOutput.Find(UrlPrefix);
	if (UrlStart == INDEX_NONE)
	{
		return;
	}

	int32 UrlEnd = UrlStart;
	while (UrlEnd < CursorAuthProcessOutput.Len() && !FChar::IsWhitespace(CursorAuthProcessOutput[UrlEnd]))
	{
		++UrlEnd;
	}
	const FString LoginUrl = CursorAuthProcessOutput.Mid(UrlStart, UrlEnd - UrlStart);
	if (LoginUrl.IsEmpty() || LoginUrl == LastOpenedCursorLoginUrl)
	{
		return;
	}

	LastOpenedCursorLoginUrl = LoginUrl;
	FString LaunchError;
	FPlatformProcess::LaunchURL(*LoginUrl, nullptr, &LaunchError);
	SetLastAction(LaunchError.IsEmpty() ? LOCTEXT("CursorLoginBrowserOpenedAction", "已打开 Cursor 官方登录页面；完成授权后将自动连接 ACP。")
										: FText::Format(LOCTEXT("CursorLoginBrowserOpenFailedAction", "无法打开 Cursor 登录页面：{0}"), FText::FromString(LaunchError)));
}

void SUnrealAgentMCPPanel::HandleCursorAuthProcessCompleted(ECodexAuthAction Action, int32 ReturnCode, bool bCanceled)
{
	CursorAuthProcess.Reset();
	if (bCanceled)
	{
		return;
	}

	if (Action == ECodexAuthAction::Status)
	{
		const FUnrealAgentACPAccountState State = ReturnCode == 0 ? UnrealAgentACPProviderModel::ParseCursorAccountStateJson(CursorAuthProcessOutput)
																  : FUnrealAgentACPAccountState{ TEXT("Cursor 未登录"), FString(), false };
		bCursorAuthenticated = State.bAuthenticated;
		if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
		{
			DetectedAccountLabel = State.DisplayLabel;
			DetectedAccountSecondaryLabel = State.SecondaryLabel;
		}
		if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
		{
			SetAccountActionFeedback(bCursorAuthenticated ? LOCTEXT("CursorAccountRefreshedAction", "Cursor 账户信息已刷新。")
														  : LOCTEXT("CursorAccountSignedOutAction", "Cursor Agent 当前未登录。"),
				false);
		}
		return;
	}

	if (ReturnCode != 0)
	{
		SetLastAction(Action == ECodexAuthAction::Login ? LOCTEXT("CursorLoginFailedAction", "Cursor 登录未完成；请检查浏览器认证页。")
														: LOCTEXT("CursorLogoutFailedAction", "Cursor 退出登录失败。"));
		return;
	}

	if (Action == ECodexAuthAction::Logout)
	{
		bCursorAuthenticated = false;
		if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
		{
			DetectedAccountLabel = TEXT("Cursor 未登录");
			DetectedAccountSecondaryLabel.Empty();
		}
		if (CursorAcpClient.IsValid())
		{
			CursorAcpClient->Stop();
		}
		SetLastAction(LOCTEXT("CursorLogoutCompletedAction", "已退出 Cursor 登录。"));
		return;
	}

	SetLastAction(LOCTEXT("CursorLoginCompletedAction", "Cursor 登录已完成，正在刷新账户和 ACP 会话。"));
	StartCursorAuthProcess(ECodexAuthAction::Status);
	if (CursorAcpClient.IsValid())
	{
		CursorAcpClient->Connect();
	}
}

FReply SUnrealAgentMCPPanel::OnCursorLoginClicked()
{
	if (!IsCliAvailable(ECliTool::Cursor))
	{
		bShowSettings = true;
		SetAccountActionFeedback(LOCTEXT("CursorLoginCliMissingAction", "未检测到 Cursor Agent CLI；请先在设置中安装或选择 agent。"));
		return FReply::Handled();
	}
	if (StartCursorAuthProcess(ECodexAuthAction::Login))
	{
		SetAccountActionFeedback(LOCTEXT("CursorLoginStartedAction", "正在打开 Cursor 官方浏览器登录流程…"));
	}
	else
	{
		SetAccountActionFeedback(FText::FromString(AuthProcessStartError.IsEmpty() ? TEXT("Cursor 登录流程未能启动。") : AuthProcessStartError));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCursorLogoutClicked()
{
	if (StartCursorAuthProcess(ECodexAuthAction::Logout))
	{
		SetAccountActionFeedback(LOCTEXT("CursorLogoutStartedAction", "正在退出 Cursor 登录…"));
	}
	else
	{
		SetAccountActionFeedback(FText::FromString(AuthProcessStartError.IsEmpty() ? TEXT("Cursor 退出流程未能启动。") : AuthProcessStartError));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnRefreshProviderAccountClicked()
{
	if (ActiveProvider == EUnrealAgentACPProvider::Codex)
	{
		if (IsCliAvailable(ECliTool::Codex))
		{
			SetAccountActionFeedback(LOCTEXT("RefreshingCodexAccountAction", "正在刷新 Codex 账户信息…"));
			RefreshCodexAccountState();
		}
		else
		{
			SetAccountActionFeedback(LOCTEXT("CodexAccountRefreshFailedAction", "未检测到 Codex CLI，无法刷新账户信息。"));
		}
	}
	else if (StartCursorAuthProcess(ECodexAuthAction::Status))
	{
		SetAccountActionFeedback(LOCTEXT("RefreshingCursorAccountAction", "正在刷新 Cursor 账户信息…"));
	}
	else
	{
		SetAccountActionFeedback(LOCTEXT("CursorAccountRefreshFailedAction", "无法启动 Cursor Agent 账户检查。"));
	}
	return FReply::Handled();
}

FText SUnrealAgentMCPPanel::GetCodexAccountStatusText() const
{
	if (CodexAuthProcess.IsValid() && CodexAuthProcess->IsRunning())
	{
		return ActiveCodexAuthAction == ECodexAuthAction::Login ? LOCTEXT("CodexAccountLoggingIn", "等待浏览器登录…") : LOCTEXT("CodexAccountChecking", "正在检查账户…");
	}
	return bCodexAuthenticated ? FText::Format(LOCTEXT("CodexAccountSignedIn", "已登录：{0}"), FText::FromString(DetectedAccountLabel))
							   : LOCTEXT("CodexAccountSignedOut", "未登录。点击登录后由 Codex CLI 打开官方浏览器认证页面。");
}

void SUnrealAgentMCPPanel::LoadSettings()
{
	FUnrealAgentMCPPanelSettings Settings;
	UnrealAgentMCPPanelSettings::LoadSettingsFile(UnrealAgentMCP::GetSettingsFilePath(), Settings);
	SettingsColor = Settings.AccentColor;
	CodexCliPath = MoveTemp(Settings.CodexCliPath);
	CursorCliPath = MoveTemp(Settings.CursorCliPath);
	ActiveProvider = Settings.ActiveProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? EUnrealAgentACPProvider::Cursor : EUnrealAgentACPProvider::Codex;
	bSidebarCollapsed = Settings.bSidebarCollapsed;
	CurrentAgentMode = Settings.DefaultAgentMode;
	CurrentApprovalPolicy = Settings.ApprovalPolicy;
	CurrentSelfRepairPolicy = Settings.SelfRepairPolicy;
	CurrentContextTokenCapacity = UnrealAgentMCPConversationModel::ClampContextTokenCapacity(Settings.ContextTokenCapacity);
	DirectProviderId = Settings.DirectProviderId;
	ControllerMode = Settings.ControllerMode;
	SanitizeRetiredNativeController();
	BudgetTierId = Settings.BudgetTierId;
	bMultiAgentEnabled = Settings.bMultiAgentEnabled;
	bMemoryEnabled = Settings.bMemoryEnabled;
	if (!Settings.CodexModelId.IsEmpty())
	{
		ProviderDesiredConfigValues.FindOrAdd(EUnrealAgentACPProvider::Codex).Add(TEXT("model"), Settings.CodexModelId);
	}
	if (!Settings.CursorModelId.IsEmpty())
	{
		ProviderDesiredConfigValues.FindOrAdd(EUnrealAgentACPProvider::Cursor).Add(TEXT("model"), Settings.CursorModelId);
	}
	if (Settings.bMigratedLegacySubscriptionRelay)
	{
		SetLastAction(LOCTEXT("MigratedLegacySubscriptionRelay", "旧订阅推理链路已移除，现已迁移到 Codex 或 Cursor 代理。"));
	}
	else if (CurrentApprovalPolicy == EWorldDataApprovalPolicy::FullProject || CurrentSelfRepairPolicy == EWorldDataSelfRepairPolicy::Automatic)
	{
		SetLastAction(LOCTEXT("RestoredElevatedPolicyWarning", "已恢复上次的高权限/自动修复设置；操作仍限制在当前项目内。"));
	}
	SaveSettings();
}

void SUnrealAgentMCPPanel::ApplySettingsColor(const FLinearColor& NewColor)
{
	SettingsColor = UnrealAgentMCPPanelSettings::ClampOpaqueAccentColor(NewColor);
	SaveSettings();
	SetLastAction(FText::Format(LOCTEXT("SettingsColorAppliedAction", "Color 已应用：{0}"), GetSettingsColorText()));
}

void SUnrealAgentMCPPanel::HandleContextCapacityChanged(const float SliderValue)
{
	const int32 NewCapacity = UnrealAgentMCPConversationModel::SliderValueToContextTokenCapacity(SliderValue);
	if (NewCapacity == CurrentContextTokenCapacity)
	{
		return;
	}
	CurrentContextTokenCapacity = NewCapacity;
	for (FConversation& Conversation : Conversations)
	{
		Conversation.ContextTokenCapacity = CurrentContextTokenCapacity;
		UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
	}
}

void SUnrealAgentMCPPanel::CommitContextCapacityChange()
{
	SaveSettings();
	PersistConversationHistory();
	SetLastAction(FText::Format(LOCTEXT("ContextCapacityAppliedAction", "上下文自动压缩容量已设为 {0}。"), GetContextCapacityText()));
}

FText SUnrealAgentMCPPanel::GetContextCapacityText() const
{
	return FText::FromString(UnrealAgentMCPConversationModel::FormatContextTokenCapacity(CurrentContextTokenCapacity));
}

void SUnrealAgentMCPPanel::SaveSettings() const
{
	FUnrealAgentMCPPanelSettings Settings;
	Settings.AccentColor = SettingsColor;
	Settings.CodexCliPath = CodexCliPath;
	Settings.CursorCliPath = CursorCliPath;
	Settings.ActiveProviderId = ActiveProvider == EUnrealAgentACPProvider::Cursor ? TEXT("cursor") : TEXT("codex");
	Settings.bSidebarCollapsed = bSidebarCollapsed;
	Settings.DefaultAgentMode = CurrentAgentMode;
	Settings.ApprovalPolicy = CurrentApprovalPolicy;
	Settings.SelfRepairPolicy = CurrentSelfRepairPolicy;
	Settings.ContextTokenCapacity = CurrentContextTokenCapacity;
	Settings.DirectProviderId = DirectProviderId;
	Settings.ControllerMode = ControllerMode;
	Settings.BudgetTierId = BudgetTierId;
	Settings.bMultiAgentEnabled = bMultiAgentEnabled;
	Settings.bMemoryEnabled = bMemoryEnabled;
	if (const TMap<FString, FString>* Values = ProviderDesiredConfigValues.Find(EUnrealAgentACPProvider::Codex))
	{
		Settings.CodexModelId = Values->FindRef(TEXT("model"));
	}
	if (const TMap<FString, FString>* Values = ProviderDesiredConfigValues.Find(EUnrealAgentACPProvider::Cursor))
	{
		Settings.CursorModelId = Values->FindRef(TEXT("model"));
	}
	UnrealAgentMCPPanelSettings::SaveSettingsFile(UnrealAgentMCP::GetSettingsFilePath(), Settings);
}

FText SUnrealAgentMCPPanel::GetSettingsColorText() const
{
	const FColor Color = SettingsColor.ToFColor(true);
	return FText::FromString(FString::Printf(TEXT("#%02X%02X%02X"), Color.R, Color.G, Color.B));
}

void SUnrealAgentMCPPanel::OpenSettingsColorPicker()
{
	FColorPickerArgs Args;
	Args.InitialColor = SettingsColor;
	Args.ParentWidget = AsShared();
	Args.bIsModal = true;
	Args.bUseAlpha = false;
	Args.bClampValue = true;
	Args.bOnlyRefreshOnMouseUp = false;
	Args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SUnrealAgentMCPPanel::HandleSettingsColorChanged);
	if (!OpenColorPicker(Args))
	{
		SetLastAction(LOCTEXT("SettingsColorPickerFailedAction", "颜色选择器打开失败。"));
	}
}

void SUnrealAgentMCPPanel::HandleSettingsColorChanged(FLinearColor NewColor)
{
	ApplySettingsColor(NewColor);
}

FReply SUnrealAgentMCPPanel::OnSettingsColorBlockClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	OpenSettingsColorPicker();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnPickSettingsColorClicked()
{
	OpenSettingsColorPicker();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnResetSettingsColorClicked()
{
	ApplySettingsColor(FLinearColor::White);
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnSettingsBackClicked()
{
	return OnDetailBackClicked();
}

FReply SUnrealAgentMCPPanel::OnProviderAccountClicked()
{
	FSlateApplication::Get().DismissAllMenus();
	bShowSettings = true;
	PlayContentTransition();
	SetAccountActionFeedback(ActiveProvider == EUnrealAgentACPProvider::Codex ? LOCTEXT("CodexAccountSettingsAction", "已打开 Codex 账户与 CLI 设置。")
																			  : LOCTEXT("CursorAccountSettingsAction", "已打开 Cursor 账户与 CLI 设置。"));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnToggleSidebarClicked()
{
	bSidebarCollapsed = !bSidebarCollapsed;
	SidebarTransitionAnimation.PlayRelative(AsShared(), !bSidebarCollapsed);
	SaveSettings();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnDetailBackClicked()
{
	bShowSettings = false;
	if (Conversations.IsValidIndex(ActiveConversationIndex) && !ConversationMessages.IsEmpty())
	{
		bShowDetail = true;
		bDetailIsConversation = true;
		RebuildConversationMessages();
	}
	else
	{
		bShowDetail = false;
		bDetailIsConversation = false;
	}
	PlayContentTransition();
	SetLastAction(LOCTEXT("DetailBackAction", "已返回对话。"));
	return FReply::Handled();
}

bool SUnrealAgentMCPPanel::IsNativeUnrealAgentSelected() const
{
	return UnrealAgentMCPPanelSettings::UsesNativeUnrealAgentController(ControllerMode);
}

void SUnrealAgentMCPPanel::SanitizeRetiredNativeController()
{
	// 旧 settings / 会话仍可能把 ControllerMode 留在 Native。
	// Kernel 模块已删除，这里把运行身份改到外部 ACP。
	if (!UnrealAgentMCPPanelSettings::UsesNativeUnrealAgentController(ControllerMode))
	{
		return;
	}

	const FString FallbackProviderId = ActiveProvider == EUnrealAgentACPProvider::Cursor ? TEXT("cursor") : TEXT("codex");
	ControllerMode = UnrealAgentMCPPanelSettings::ResolveRetiredNativeControllerMode(FallbackProviderId);
	SynchronizeControllerAcpRouting();
}

#undef LOCTEXT_NAMESPACE
