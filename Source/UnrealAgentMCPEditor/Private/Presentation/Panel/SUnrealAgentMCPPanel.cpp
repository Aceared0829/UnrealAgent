// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPPanel.cpp
 * @brief 面板框架、顶栏、侧栏与通用控件布局。
 */

#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Presentation/Panel/SUnrealAgentMCPPanelPrivate.h"
#include "Presentation/Widgets/UnrealAgentMCPToolTip.h"
#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace
{
	// 侧栏与「选择代理」菜单共用，避免下拉比会话列表更宽、盖住主对话区。
	constexpr float SidebarPanelWidth = 260.0f;

	const FSlateBrush* GetPanelCardOuterBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 9.0f);
		return &Brush;
	}

	const FSlateBrush* GetPanelCardInnerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 8.0f);
		return &Brush;
	}

	const FSlateBrush* GetConversationItemBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.0f);
		return &Brush;
	}

	const FSlateBrush* GetConversationIndicatorBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 2.0f);
		return &Brush;
	}
}

TSharedPtr<FUnrealAgentCodexACPClient> SUnrealAgentMCPPanel::GetAcpClientForProvider(const EUnrealAgentACPProvider Provider) const
{
	return Provider == EUnrealAgentACPProvider::Cursor ? CursorAcpClient : CodexAcpClient;
}

void SUnrealAgentMCPPanel::ConfigureProviderAcpClient(const EUnrealAgentACPProvider Provider, const TSharedPtr<FUnrealAgentCodexACPClient>& Client, const FGuid& ConversationId)
{
	if (!Client.IsValid())
	{
		return;
	}

	Client->SetExecutionPolicy(CurrentAgentMode, CurrentApprovalPolicy, CurrentSelfRepairPolicy);
	Client->SetAgentProvider(Provider, Provider == EUnrealAgentACPProvider::Cursor ? GetCliEffectivePath(ECliTool::Cursor) : FString());
	if (Provider == EUnrealAgentACPProvider::Codex)
	{
		Client->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex), false);
	}

	Client->OnText.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpText, ConversationId, Provider);
	Client->OnStatus.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpStatus, ConversationId, Provider);
	Client->OnTurnStatus.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpTurnStatus, ConversationId, Provider);
	Client->OnToolCall.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpToolCall, ConversationId, Provider);
	Client->OnError.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpError, ConversationId, Provider);
	Client->OnPermission.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpPermission, ConversationId, Provider);
	Client->OnConfigOptionsChanged.BindSP(this, &SUnrealAgentMCPPanel::HandleProviderAcpConfigOptions, ConversationId, Provider);
}

TSharedPtr<FUnrealAgentCodexACPClient> SUnrealAgentMCPPanel::GetOrCreateConversationAcpClient(const FGuid& ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (!ConversationId.IsValid())
	{
		return nullptr;
	}
	TSharedPtr<FConversationAcpRuntime>& Runtime = ConversationAcpRuntimes.FindOrAdd(ConversationId);
	if (!Runtime.IsValid())
	{
		Runtime = MakeShared<FConversationAcpRuntime>();
	}
	TSharedPtr<FUnrealAgentCodexACPClient>& Client = Provider == EUnrealAgentACPProvider::Cursor ? Runtime->CursorClient : Runtime->CodexClient;
	if (!Client.IsValid())
	{
		Client = MakeShared<FUnrealAgentCodexACPClient>(ApplicationService.ToSharedRef());
		ConfigureProviderAcpClient(Provider, Client, ConversationId);
	}
	return Client;
}

void SUnrealAgentMCPPanel::ActivateConversationAcpClients(const FGuid& ConversationId, const bool bRefreshDependencyState)
{
	CodexAcpClient = GetOrCreateConversationAcpClient(ConversationId, EUnrealAgentACPProvider::Codex);
	CursorAcpClient = GetOrCreateConversationAcpClient(ConversationId, EUnrealAgentACPProvider::Cursor);
	SynchronizeControllerAcpRouting();
	if (bRefreshDependencyState)
	{
		RefreshProviderDependencyState();
	}
}

void SUnrealAgentMCPPanel::StopAllConversationAcpClients()
{
	for (TPair<FGuid, TSharedPtr<FConversationAcpRuntime>>& Pair : ConversationAcpRuntimes)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		if (Pair.Value->MediaLoadCancellation.IsValid())
		{
			++Pair.Value->MediaLoadGeneration;
			Pair.Value->MediaLoadCancellation->Request();
			Pair.Value->MediaLoadCancellation.Reset();
		}
		Pair.Value->PendingMediaPrompt.Reset();
		if (Pair.Value->CodexClient.IsValid())
		{
			Pair.Value->CodexClient->Stop();
		}
		if (Pair.Value->CursorClient.IsValid())
		{
			Pair.Value->CursorClient->Stop();
		}
	}
	ConversationAcpRuntimes.Empty();
	CodexAcpClient.Reset();
	CursorAcpClient.Reset();
	AcpClient.Reset();
}

void SUnrealAgentMCPPanel::ShutdownPanelSession()
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	bPanelSessionShuttingDown = true;
	StopAllConversationAcpClients();
	PersistConversationHistory(true);
}

void SUnrealAgentMCPPanel::ResetConversationAcpRuntimeForHistoryRewrite(const FGuid& ConversationId)
{
	TSharedPtr<FConversationAcpRuntime> Runtime;
	ConversationAcpRuntimes.RemoveAndCopyValue(ConversationId, Runtime);
	if (Runtime.IsValid())
	{
		if (Runtime->MediaLoadCancellation.IsValid())
		{
			++Runtime->MediaLoadGeneration;
			Runtime->MediaLoadCancellation->Request();
			Runtime->MediaLoadCancellation.Reset();
		}
		Runtime->PendingMediaPrompt.Reset();
		for (const TSharedPtr<FUnrealAgentCodexACPClient>& Client : { Runtime->CodexClient, Runtime->CursorClient })
		{
			if (!Client.IsValid())
			{
				continue;
			}
			if (Client->HasActiveTurn())
			{
				Client->CancelActivePrompt();
			}
			Client->Stop();
		}
		Runtime->PendingPermission.Reset();
		Runtime->PendingPermissionProvider.Reset();
	}

	const bool bIsActiveConversation = Conversations.IsValidIndex(ActiveConversationIndex) && Conversations[ActiveConversationIndex].Id == ConversationId;
	if (!bIsActiveConversation)
	{
		return;
	}

	CodexAcpClient.Reset();
	CursorAcpClient.Reset();
	AcpClient.Reset();
	ActivateConversationAcpClients(ConversationId, false);
}

void SUnrealAgentMCPPanel::PrewarmProviderAcpClient(const EUnrealAgentACPProvider Provider)
{
	const TSharedPtr<FUnrealAgentCodexACPClient> Client = GetAcpClientForProvider(Provider);
	if (Client.IsValid() && UnrealAgentACPProviderModel::GetProvider(Provider).bSupportsEmbeddedConversation && (Client->IsRunning() || Client->CanLaunchAgent()))
	{
		Client->Connect();
	}
}

void SUnrealAgentMCPPanel::ScheduleDeferredConversationStartup(const FGuid& ConversationId)
{
	// 实际检查延迟执行时，避免依赖状态卡片闪烁。
	bProviderDependencyAvailable = true;
	DeferredStartupConversationId = ConversationId;
	DeferredConversationStartupStage = 0;
	if (!DeferredConversationStartupTimer.IsValid())
	{
		DeferredConversationStartupTimer = RegisterActiveTimer(0.14f, FWidgetActiveTimerDelegate::CreateSP(this, &SUnrealAgentMCPPanel::HandleDeferredConversationStartup));
	}
}

EActiveTimerReturnType SUnrealAgentMCPPanel::HandleDeferredConversationStartup(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	if (!Conversations.IsValidIndex(ActiveConversationIndex) || Conversations[ActiveConversationIndex].Id != DeferredStartupConversationId)
	{
		DeferredConversationStartupTimer.Reset();
		DeferredStartupConversationId.Invalidate();
		DeferredConversationStartupStage = INDEX_NONE;
		return EActiveTimerReturnType::Stop;
	}

	if (DeferredConversationStartupStage == 0)
	{
		if (bDeferredCliRefreshPending)
		{
			RefreshCliDetections();
			bDeferredCliRefreshPending = false;
			if (CodexAcpClient.IsValid())
			{
				CodexAcpClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex), false);
			}
			if (CursorAcpClient.IsValid())
			{
				CursorAcpClient->SetAgentProvider(EUnrealAgentACPProvider::Cursor, GetCliEffectivePath(ECliTool::Cursor));
			}
		}
		PrewarmProviderAcpClient(ActiveProvider);
		RefreshProviderDependencyState();
		DeferredConversationStartupStage = 1;
		return EActiveTimerReturnType::Continue;
	}

	if (DeferredConversationStartupStage == 1)
	{
		PrewarmProviderAcpClient(ActiveProvider == EUnrealAgentACPProvider::Codex ? EUnrealAgentACPProvider::Cursor : EUnrealAgentACPProvider::Codex);
		DeferredConversationStartupStage = 2;
		return EActiveTimerReturnType::Continue;
	}

	if (DeferredConversationStartupStage == 2)
	{
		PersistConversationHistory();
		DeferredConversationStartupStage = 3;
		if (bDeferredAccountRefreshPending)
		{
			return EActiveTimerReturnType::Continue;
		}
	}

	if (bDeferredAccountRefreshPending)
	{
		bDeferredAccountRefreshPending = false;
		if (ActiveProvider == EUnrealAgentACPProvider::Codex)
		{
			RefreshCodexAccountState();
		}
		else
		{
			StartCursorAuthProcess(ECodexAuthAction::Status);
		}
	}

	DeferredConversationStartupTimer.Reset();
	DeferredStartupConversationId.Invalidate();
	DeferredConversationStartupStage = INDEX_NONE;
	return EActiveTimerReturnType::Stop;
}

void SUnrealAgentMCPPanel::PlayContentTransition()
{
	ContentTransitionAnimation.Play(AsShared());
}

bool SUnrealAgentMCPPanel::IsActiveConversationControllerBusy() const
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return false;
	}

	const FGuid& ConversationId = Conversations[ActiveConversationIndex].Id;
	return IsConversationRunning(ConversationId);
}

bool SUnrealAgentMCPPanel::IsConversationRunning(const FGuid& ConversationId) const
{
	const TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId);
	return Runtime && Runtime->IsValid() &&
		(((*Runtime)->CodexClient.IsValid() && (*Runtime)->CodexClient->HasActiveTurn()) || ((*Runtime)->CursorClient.IsValid() && (*Runtime)->CursorClient->HasActiveTurn()));
}

bool SUnrealAgentMCPPanel::HasConversationPendingWork(const FGuid& ConversationId) const
{
	const int32 ConversationIndex = FindConversationIndex(ConversationId);
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return false;
	}

	const FConversation& Conversation = Conversations[ConversationIndex];
	const TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId);
	const bool bRuntimeHasPendingPermission = Runtime && Runtime->IsValid() && (*Runtime)->PendingPermission.IsSet();
	return UnrealAgentMCPConversationModel::HasPendingConversationWork(Conversation.bIsRunning, IsConversationRunning(ConversationId), bRuntimeHasPendingPermission,
		!Conversation.QueuedPrompts.IsEmpty());
}

int32 SUnrealAgentMCPPanel::FindConversationIndex(const FGuid& ConversationId) const
{
	return Conversations.IndexOfByPredicate(
		[&ConversationId](const FConversation& Conversation)
		{
			return Conversation.Id == ConversationId;
		});
}

bool SUnrealAgentMCPPanel::IsProviderCallbackVisible(const FGuid& ConversationId) const
{
	return !bPanelSessionShuttingDown && Conversations.IsValidIndex(ActiveConversationIndex) && Conversations[ActiveConversationIndex].Id == ConversationId;
}

void SUnrealAgentMCPPanel::HandleProviderAcpText(const FString& Text, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	const int32 Index = FindConversationIndex(ConversationId);
	if (Index != INDEX_NONE)
	{
		ApplyAcpTextToConversation(Index, Text, Provider);
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpStatus(const FString& Text, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	const int32 Index = FindConversationIndex(ConversationId);
	if (Index != INDEX_NONE)
	{
		if (Provider == GetModelCatalogProvider() && IsProviderCallbackVisible(ConversationId))
		{
			RefreshProviderDependencyState();
		}
		if (IsProviderCallbackVisible(ConversationId))
		{
			SetLastAction(FText::FromString(Text));
		}
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpTurnStatus(const FWorldDataCodexTurnStatus& Status, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	const int32 Index = FindConversationIndex(ConversationId);
	if (Index != INDEX_NONE)
	{
		// Sending 仅表示面板开始提交；Received 才证明 session/prompt 已进入
		// 当前进程的发送队列，发送前拒绝时仍需在下一次请求重放上下文。
		if (WorldDataCodexAcpRules::ShouldConsumeContextReplay(Status.State))
		{
			if (TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId); Runtime && Runtime->IsValid())
			{
				bool& bContextReplayPending = Provider == EUnrealAgentACPProvider::Cursor ? (*Runtime)->bCursorContextReplayPending : (*Runtime)->bCodexContextReplayPending;
				bContextReplayPending = false;
			}
		}
		ApplyAcpTurnStatusToConversation(Index, Status, Provider);
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpToolCall(const FUnrealAgentAcpToolCallUpdate& Update, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	const int32 Index = FindConversationIndex(ConversationId);
	if (Index != INDEX_NONE)
	{
		ApplyAcpToolCallToConversation(Index, Update);
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpError(const FString& Text, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	const int32 Index = FindConversationIndex(ConversationId);
	if (Index != INDEX_NONE)
	{
		ApplyAcpErrorToConversation(Index, Text, Provider);
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpPermission(const FUnrealAgentAcpPermissionRequest& Request, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId);
	if (Runtime && Runtime->IsValid())
	{
		(*Runtime)->PendingPermission = Request;
		(*Runtime)->PendingPermissionProvider = Provider;
	}
	if (IsProviderCallbackVisible(ConversationId))
	{
		PendingPermissionProvider = Provider;
		CallbackProviderOverride = Provider;
		HandleAcpPermission(Request);
		CallbackProviderOverride.Reset();
	}
}

void SUnrealAgentMCPPanel::HandleProviderAcpConfigOptions(const TArray<FUnrealAgentAcpConfigOption>& Options, const FGuid ConversationId, const EUnrealAgentACPProvider Provider)
{
	if (bPanelSessionShuttingDown)
	{
		return;
	}
	TArray<FUnrealAgentAcpConfigOption> DisplayOptions = Options;
	bool bDesiredValuesChanged = false;
	if (TMap<FString, FString>* DesiredValues = ProviderDesiredConfigValues.Find(Provider))
	{
		for (FUnrealAgentAcpConfigOption& Option : DisplayOptions)
		{
			if (const FString* Desired = DesiredValues->Find(Option.Id))
			{
				const bool bDesiredValueAvailable = Option.Options.ContainsByPredicate(
					[Desired](const FUnrealAgentAcpConfigOptionValue& Value)
					{
						return Value.bEnabled && Value.Value.Equals(*Desired, ESearchCase::CaseSensitive);
					});
				if (bDesiredValueAvailable)
				{
					Option.CurrentValue = *Desired;
				}
				else if (!Option.Options.IsEmpty())
				{
					// 只有当前代理给出完整候选后，才清理不属于它的旧选择。
					DesiredValues->Remove(Option.Id);
					bDesiredValuesChanged = true;
				}
			}
		}
	}
	if (Provider == GetModelCatalogProvider() && IsProviderCallbackVisible(ConversationId))
	{
		HandleAcpConfigOptions(DisplayOptions);
	}
	if (!DisplayOptions.IsEmpty())
	{
		ProviderConfigCache.Add(Provider, DisplayOptions);
	}
	if (bDesiredValuesChanged)
	{
		SaveSettings();
	}
}

void SUnrealAgentMCPPanel::Construct(const FArguments& InArgs)
{
	ApplicationService = InArgs._ApplicationService;
	check(ApplicationService.IsValid());
	ConversationRepository = InArgs._ConversationRepository;
	check(ConversationRepository.IsValid());

	LastAction = LOCTEXT("InitialLastAction", "新会话已就绪。");
	LoadSettings();
	PanelIntroAnimation = FCurveSequence(
		0.0f, 0.12f, ECurveEaseFunction::CubicOut);
	ContentTransitionAnimation = FCurveSequence(
		0.0f, 0.12f, ECurveEaseFunction::CubicOut);
	SidebarTransitionAnimation = FCurveSequence(
		0.0f, 0.12f, ECurveEaseFunction::CubicOut);
	if (bSidebarCollapsed)
	{
		SidebarTransitionAnimation.JumpToStart();
	}
	else
	{
		SidebarTransitionAnimation.JumpToEnd();
	}
	bDeferredCliRefreshPending = true;
	bDeferredAccountRefreshPending = true;
	const FUnrealAgentACPAccountState InitialAccount =
		ActiveProvider == EUnrealAgentACPProvider::Cursor
			? FUnrealAgentACPAccountState{
				TEXT("Cursor 账户"),
				TEXT("正在读取官方 Agent 登录状态…"),
				false}
			: UnrealAgentACPProviderModel::DetectAccountState(
				ActiveProvider);
	DetectedAccountLabel = InitialAccount.DisplayLabel;
	DetectedAccountSecondaryLabel = InitialAccount.SecondaryLabel;
	bCodexAuthenticated =
		ActiveProvider == EUnrealAgentACPProvider::Codex
		&& InitialAccount.bAuthenticated;
	bCursorAuthenticated = ActiveProvider == EUnrealAgentACPProvider::Cursor
		&& InitialAccount.bAuthenticated;
	ServerPortText = FString::FromInt(ApplicationService->IsServerRunning()
			? ApplicationService->GetServerPort()
			: ApplicationService->LoadConfiguredPort());
		ConfigureLightTextBoxStyle();
		ConfigureComposerButtonStyle();

		ChildSlot
		[
			SAssignNew(PanelAnimationRoot, SBorder)
			.Padding(0.0f)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(GetPanelBackgroundColor()))
			[
				BuildUEBridgeStyleLayout()
			]
		];

		LoadConversationHistory();
		if (Conversations.IsEmpty())
		{
			ResetConversationView();
		}
		PanelIntroAnimation.Play(AsShared());
	}

void SUnrealAgentMCPPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (PanelAnimationRoot.IsValid())
	{
		const float Alpha = PanelIntroAnimation.GetLerp();
		PanelAnimationRoot->SetRenderOpacity(Alpha);
		PanelAnimationRoot->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.0f, FMath::Lerp(8.0f, 0.0f, Alpha)))));
	}
	if (ContentAnimationRoot.IsValid())
	{
		const float Alpha = ContentTransitionAnimation.GetLerp();
		ContentAnimationRoot->SetRenderOpacity(Alpha);
		ContentAnimationRoot->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.0f, FMath::Lerp(8.0f, 0.0f, Alpha)))));
	}
	if (SidebarAnimationBox.IsValid())
	{
		SidebarAnimationBox->SetRenderOpacity(SidebarTransitionAnimation.GetLerp());
	}
	if (SidebarAnimationSeparator.IsValid())
	{
		SidebarAnimationSeparator->SetRenderOpacity(SidebarTransitionAnimation.GetLerp());
	}
}

SUnrealAgentMCPPanel::~SUnrealAgentMCPPanel()
{
	ShutdownPanelSession();
	if (CodexAuthProcess.IsValid())
	{
		CodexAuthProcess->Cancel(true);
		CodexAuthProcess.Reset();
	}
	if (CursorAuthProcess.IsValid())
	{
		CursorAuthProcess->Cancel(true);
		CursorAuthProcess.Reset();
	}
	if (ProviderDependencyInstallProcess.IsValid())
	{
		ProviderDependencyInstallProcess->Cancel(true);
		ProviderDependencyInstallProcess.Reset();
	}
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildUEBridgeStyleLayout()
{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(SidebarAnimationBox, SBox)
				.WidthOverride_Lambda([this]
				{
					return SidebarPanelWidth
						* SidebarTransitionAnimation.GetLerp();
				})
				.Clipping(EWidgetClipping::ClipToBounds)
				.Visibility_Lambda([this]
				{
					return bSidebarCollapsed
						&& SidebarTransitionAnimation.IsAtStart()
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					BuildUEBridgeSidebar()
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(SidebarAnimationSeparator, SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(1.0f)
				.Visibility_Lambda([this]
				{
					return bSidebarCollapsed
						&& SidebarTransitionAnimation.IsAtStart()
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildUEBridgeTopBar()
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildUEBridgeMainArea()
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildUEBridgeTopBar()
{
		return SNew(SBorder)
			.Padding(FMargin(14.0f, 6.0f))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SBox)
				.HeightOverride(34.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						BuildIconTextButton(
							LOCTEXT("SidebarToggleButton", "☰"),
							FOnClicked::CreateSP(
								this,
								&SUnrealAgentMCPPanel::OnToggleSidebarClicked),
							LOCTEXT(
								"SidebarToggleTooltip",
								"显示或隐藏对话侧栏"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]
						{
							return bShowSettings
								|| (bShowDetail
									&& !bDetailIsConversation)
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						[
							BuildIconTextButton(
								LOCTEXT("TopBackButton", "←"),
								FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnDetailBackClicked),
								LOCTEXT(
									"TopBackTooltip",
									"返回当前对话"))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return GetActiveConversationHeader(); })
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return GetProviderStatusText(); })
							.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 4.0f, 0.0f)
					[
						BuildToolbarButton(
							LOCTEXT("CopyUrlTopButton", "复制连接"),
							FOnClicked::CreateSP(
								this,
								&SUnrealAgentMCPPanel::OnCopyUrlClicked),
							TAttribute<bool>::CreateLambda([this]
							{
								return ApplicationService->IsServerRunning();
							}))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f)
					[
						BuildProviderContextControl()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f)
					[
						BuildIconTextButton(
							LOCTEXT("RefreshTopButton", "↻"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnRefreshClicked),
							LOCTEXT("RefreshTopTooltip", "刷新当前代理与 MCP 状态"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f)
					[
						BuildIconTextButton(
							LOCTEXT("SettingsTopButton", "⚙"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnSettingsClicked),
							LOCTEXT("SettingsTopTooltip", "控制台设置"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						BuildToolbarButton(
							LOCTEXT("ArchiveChatButton", "归档"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnArchiveConversationClicked),
							TAttribute<bool>::CreateSP(
								this,
								&SUnrealAgentMCPPanel::CanArchiveConversation))
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildUEBridgeSidebar()
{
		return SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SBox)
				.WidthOverride(SidebarPanelWidth)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(10.0f, 10.0f, 10.0f, 6.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							BuildProviderCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							BuildIconTextButton(
								LOCTEXT("SidebarNewTaskIcon", "+"),
								FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnNewConversationClicked),
								LOCTEXT("SidebarNewTaskTooltip", "新建对话"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							BuildIconTextButton(
								LOCTEXT("SidebarCollapseIcon", "‹"),
								FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnToggleSidebarClicked),
								LOCTEXT(
									"SidebarCollapseTooltip",
									"收起对话侧栏"))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(10.0f, 4.0f))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.HAlign(HAlign_Left)
						.ContentPadding(FMargin(8.0f, 6.0f))
						.ToolTip(UnrealAgentMCPToolTip::Make(
							LOCTEXT("NewTaskTooltip", "创建新的代理对话")))
						.OnClicked(FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnNewConversationClicked))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NewTaskLabel", "＋  新建任务"))
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(10.0f, 2.0f, 10.0f, 6.0f))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.HAlign(HAlign_Left)
						.ContentPadding(FMargin(8.0f, 6.0f))
						.ToolTip(UnrealAgentMCPToolTip::Make(
							LOCTEXT("McpEntryTooltip", "查看 Unreal Agent 的项目连接")))
						.OnClicked(FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnStatusClicked))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("McpEntryLabel", "连接 UE MCP"))
								.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ConnectionDot", "●"))
								.ColorAndOpacity_Lambda([this]
								{
									return UnrealAgentMCP::GetStatusColor(
										ApplicationService->IsServerRunning());
								})
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SSeparator)
						.Thickness(1.0f)
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(SidebarListScrollBox, SScrollBox)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(10.0f, 6.0f, 10.0f, 4.0f))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.HAlign(HAlign_Left)
						.ContentPadding(FMargin(8.0f, 5.0f))
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"ArchivedConversationsTooltip",
							"显示或隐藏已归档的对话，可从列表中恢复")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnToggleArchivedConversationsClicked))
						[
							SNew(STextBlock)
							.Text_Lambda([this]
							{
								return FText::Format(
									bShowArchivedConversations
										? LOCTEXT(
											"HideArchivedConversations",
											"⌄  隐藏已归档 ({0})")
										: LOCTEXT(
											"ShowArchivedConversations",
											"›  已归档 ({0})"),
									FText::AsNumber(
										GetArchivedConversationCount()));
							})
							.ColorAndOpacity(FSlateColor(
								GetPanelMutedTextColor()))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildProviderContextControl(false)
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildUEBridgeMainArea()
{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(28.0f, 16.0f, 28.0f, 0.0f))
			[
				SNew(SBox)
				.Visibility_Lambda([this]
				{
					return !IsProviderDependencyAvailable()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					BuildProviderDependencyCard()
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(28.0f, 20.0f, 28.0f, 0.0f))
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				[
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					.PhysicalSplitterHandleSize(5.0f)
					.HitDetectionSplitterHandleSize(12.0f)
					+ SSplitter::Slot()
					.Value(0.72f)
					.MinSize(120.0f)
					[
						SAssignNew(ContentAnimationRoot, SBorder)
						.Padding(0.0f)
						.BorderImage(FAppStyle::GetBrush("NoBorder"))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNew(SBox)
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.Visibility_Lambda([this]
								{
									return (!bShowDetail
										&& !bShowSettings
										&& !bShowTaskPanel)
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this]
									{
										return FText::Format(
											LOCTEXT(
												"EmptyChatPrompt",
												"{0} 已就绪，今天需要处理什么？"),
											GetProviderDisplayName());
									})
									.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
									.Font(FAppStyle::GetFontStyle("NormalFontBold"))
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNew(SBox)
								.Visibility_Lambda([this]
								{
									return (bShowDetail
										&& !bShowSettings
										&& !bShowTaskPanel)
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								[
									BuildConversationDetail()
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNew(SBox)
								.Visibility_Lambda([this]
								{
									return bShowSettings
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								[
									BuildSettingsPanel()
								]
							]
						]
					]
					+ SSplitter::Slot()
					.Value(0.28f)
					.MinSize(112.0f)
					[
						SNew(SBorder)
						.Visibility_Lambda([this]
						{
							return UnrealAgentMCPConversationModel::ShouldShowConversationComposer(
									bShowSettings,
									bShowDetail,
									bDetailIsConversation)
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.Padding(FMargin(
							20.0f,
							10.0f,
							20.0f,
							20.0f))
						.BorderImage(
							FAppStyle::GetBrush("NoBorder"))
						[
							SNew(SBox)
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							.MinDesiredHeight(92.0f)
							[
								BuildComposer()
							]
						]
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderDependencyCard()
{
	return SNew(SBorder)
		.Padding(1.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(UnrealAgentMCP::Palette::Warning()))
		[
			SNew(SBorder)
			.Padding(FMargin(14.0f, 10.0f))
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this]
			{
				return FSlateColor(GetAccentSurfaceColor());
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							return GetProviderDependencyTitle();
						})
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							return GetProviderDependencyDescription();
						})
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 5.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							return ProviderDependencyFeedback;
						})
						.Visibility_Lambda([this]
						{
							return ProviderDependencyFeedback.IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
						.AutoWrapText(true)
						.ColorAndOpacity(
							FSlateColor(UnrealAgentMCP::Palette::Warning()))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(14.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&ComposerButtonStyle)
					.ButtonColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetAccentButtonColor());
					})
					.ContentPadding(FMargin(12.0f, 5.0f))
					.IsEnabled_Lambda([this]
					{
						return !ProviderDependencyInstallProcess.IsValid()
							|| !ProviderDependencyInstallProcess->IsRunning();
					})
					.OnClicked(FOnClicked::CreateSP(
						this,
						&SUnrealAgentMCPPanel::OnProviderDependencyPrimaryClicked))
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							return GetProviderDependencyPrimaryActionText();
						})
						.ColorAndOpacity_Lambda([this]
						{
							return FSlateColor(GetAccentButtonTextColor());
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BuildPermissionActionButton(
						LOCTEXT("RefreshProviderDependencyButton", "重新检测"),
						false,
						FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnRefreshProviderDependencyClicked))
				]
			]
		];
}

FText SUnrealAgentMCPPanel::GetProviderDependencyTitle() const
{
	return ActiveProvider == EUnrealAgentACPProvider::Cursor ? LOCTEXT("CursorDependencyCardTitle", "需要安装 Cursor Agent CLI")
															 : LOCTEXT("CodexDependencyCardTitle", "需要安装 Codex ACP");
}

FText SUnrealAgentMCPPanel::GetProviderDependencyDescription() const
{
	if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
	{
		return LOCTEXT("CursorDependencyCardDescription", "点击后将自动通过 Windows WSL 下载、安装并配置 Cursor Agent CLI，完成后自动检测和连接；首次账号授权仍需在浏览器确认。");
	}
	if (DetectedNpmPath.IsEmpty())
	{
		return LOCTEXT("CodexNodeDependencyCardDescription",
			"未检测到 Node.js/npm。点击后将自动下载官方 Node.js LTS 到当前项目，再安装 Codex CLI 与 Codex ACP，无需手动配置 PATH。");
	}
	return LOCTEXT("CodexDependencyCardDescription", "点击后会把兼容的 Codex ACP 和 Codex CLI 安装到当前项目 Saved 目录；不会修改全局 npm 环境，完成后自动连接。");
}

FText SUnrealAgentMCPPanel::GetProviderDependencyPrimaryActionText() const
{
	if (ProviderDependencyInstallProcess.IsValid() && ProviderDependencyInstallProcess->IsRunning())
	{
		return LOCTEXT("CodexDependencyInstallingAction", "正在安装…");
	}
	if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
	{
		return LOCTEXT("InstallCursorDependencyAction", "安装 Cursor Agent");
	}
	return DetectedNpmPath.IsEmpty() ? LOCTEXT("OpenNodeDownloadAction", "一键部署 Codex") : LOCTEXT("InstallCodexDependencyAction", "一键安装 Codex ACP");
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderContextControl(const bool bCompact)
{
	return BuildProviderAccountCombo(bCompact);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderAccountCombo(bool bCompact)
{
	TSharedRef<SWidget> ButtonContent = bCompact
		? StaticCastSharedRef<SWidget>(
			SNew(STextBlock)
			.Text(LOCTEXT("ProviderAccountTopButton", "账户"))
			.ColorAndOpacity(FSlateColor(GetPanelTextColor())))
		: StaticCastSharedRef<SWidget>(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.Padding(8.0f, 2.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this]
				{
					return FSlateColor(GetAccentFillColor(0.22f));
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return FText::FromString(
							UnrealAgentACPProviderModel::MakeAccountInitials(
								DetectedAccountLabel));
					})
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return FText::FromString(DetectedAccountLabel);
					})
					.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return GetAccountSecondaryText();
					})
					.ColorAndOpacity(FSlateColor(
						GetPanelMutedTextColor()))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return GetProviderDisplayName(); })
				.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
			]);

	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ButtonStyle(bCompact
			? &ToolbarButtonStyle
			: &FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
				"SimpleButton"))
		.ContentPadding(bCompact
			? FMargin(10.0f, 3.0f)
			: FMargin(10.0f, 8.0f))
		.HasDownArrow(false)
		.ToolTip(UnrealAgentMCPToolTip::Make(
			TAttribute<FText>::CreateLambda([this]
			{
				return GetAccountHoverText();
			})))
		.OnGetMenuContent(FOnGetContent::CreateSP(
			this,
			&SUnrealAgentMCPPanel::BuildProviderAccountMenu))
		.ButtonContent()
		[
			ButtonContent
		]);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderAccountMenu()
{
	AccountActionFeedback = FText::GetEmpty();
	return UnrealAgentMCPPanelWidgets::AnimateMenu(
		SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SBox)
			.WidthOverride(390.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return FText::Format(
							LOCTEXT(
								"ProviderAccountMenuTitle",
								"{0} 账户"),
							GetProviderDisplayName());
					})
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return FText::FromString(DetectedAccountLabel);
					})
					.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					.AutoWrapText(true)
					.WrapTextAt(366.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this] { return GetAccountSecondaryText(); })
					.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					.AutoWrapText(true)
					.WrapTextAt(366.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this] { return GetAccountDetailText(); })
					.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					.AutoWrapText(true)
					.WrapTextAt(366.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 7.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return AccountActionFeedback;
					})
					.Visibility_Lambda([this]
					{
						return AccountActionFeedback.IsEmpty()
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetEffectiveAccentColor());
					})
					.AutoWrapText(true)
					.WrapTextAt(366.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					BuildToolbarButton(
						LOCTEXT(
							"RefreshProviderAccountButton",
							"刷新账户信息"),
						FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnRefreshProviderAccountClicked))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(UnrealAgentMCPPanelWidgets::SAnimatedButton)
					.Visibility_Lambda([this]
					{
						const bool bAuthenticated = ActiveProvider
							== EUnrealAgentACPProvider::Codex
								? bCodexAuthenticated
								: bCursorAuthenticated;
						return bAuthenticated
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
					.ButtonStyle(&ToolbarButtonStyle)
					.ContentPadding(FMargin(10.0f, 5.0f))
					.OnClicked_Lambda([this]
					{
						FSlateApplication::Get().DismissAllMenus();
						return ActiveProvider
							== EUnrealAgentACPProvider::Codex
								? OnCodexLoginClicked()
								: OnCursorLoginClicked();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"LoginProviderAccountButton",
							"登录账户"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(UnrealAgentMCPPanelWidgets::SAnimatedButton)
					.Visibility_Lambda([this]
					{
						const bool bAuthenticated = ActiveProvider
							== EUnrealAgentACPProvider::Codex
								? bCodexAuthenticated
								: bCursorAuthenticated;
						return bAuthenticated
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
					.ButtonStyle(&ToolbarButtonStyle)
					.ContentPadding(FMargin(10.0f, 5.0f))
					.OnClicked_Lambda([this]
					{
						FSlateApplication::Get().DismissAllMenus();
						return ActiveProvider
							== EUnrealAgentACPProvider::Codex
								? OnCodexLogoutClicked()
								: OnCursorLogoutClicked();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"LogoutProviderAccountButton",
							"退出登录"))
						.ColorAndOpacity(FSlateColor(
							UnrealAgentMCP::Palette::Danger()))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					BuildToolbarButton(
						LOCTEXT(
							"OpenAccountSettingsButton",
							"打开账户与 CLI 设置"),
						FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnProviderAccountClicked))
				]
			]
		]);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildBadge(const FText& Text) const
{
		return SNew(SBorder)
			.Padding(8.0f, 2.0f)
			.BorderImage(GetConversationItemBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.22f)); })
			[
				SNew(STextBlock)
				.Text(Text)
				.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderCombo()
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8.0f, 5.0f))
		.HasDownArrow(true)
		.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
			"ProviderComboTooltip",
			"选择 Codex 代理或 Cursor 代理")))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 7.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ProviderGlyph", "◆"))
				.ColorAndOpacity_Lambda([this]
				{
					return FSlateColor(GetEffectiveAccentColor());
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return GetProviderDisplayName(); })
				.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
		]
		.OnGetMenuContent(FOnGetContent::CreateSP(
			this,
			&SUnrealAgentMCPPanel::BuildProviderMenu)));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildProviderMenu()
{
	ControllerSelectionFeedback = FText::GetEmpty();
	const FText MenuDescription = LOCTEXT(
		"ControllerMenuDescription",
		"代理决定谁负责规划；Codex 与 Cursor 只通过 MCP 使用 Unreal Agent 工具。");
	return UnrealAgentMCPPanelWidgets::AnimateMenu(
		SNew(SBox)
		.WidthOverride(SidebarPanelWidth)
		[
			SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetPanelCardOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetPanelBorderColor()))
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(GetPanelCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetPanelSurfaceColor()))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ControllerMenuTitle", "选择代理"))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 10.0f)
					[
						SNew(SBox)
						.ToolTip(UnrealAgentMCPToolTip::Make(MenuDescription))
						[
							SNew(STextBlock)
							.Text(MenuDescription)
							.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
							.AutoWrapText(false)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildControllerChoiceCard(
							EUnrealAgentControllerMode::ExternalCodexAgent,
							LOCTEXT("CodexAgentChoiceTitle", "Codex 代理"),
							FText::GetEmpty(),
							LOCTEXT(
								"CodexAgentChoiceDescription",
								"Codex 自行规划，通过 Unreal Agent MCP 使用 UE 工具"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 7.0f, 0.0f, 0.0f)
					[
						BuildControllerChoiceCard(
							EUnrealAgentControllerMode::ExternalCursorAgent,
							LOCTEXT("CursorAgentChoiceTitle", "Cursor 代理"),
							FText::GetEmpty(),
							LOCTEXT(
								"CursorAgentChoiceDescription",
								"Cursor 自行规划，通过 Unreal Agent MCP 使用 UE 工具"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 9.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.Visibility_Lambda([this]
						{
							return ControllerSelectionFeedback.IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
						.Padding(FMargin(10.0f, 8.0f))
						.BorderImage(GetPanelCardInnerBrush())
						.BorderBackgroundColor(FSlateColor(
							UnrealAgentMCP::Palette::Danger().CopyWithNewOpacity(0.12f)))
						[
							SNew(STextBlock)
							.Text_Lambda([this]
							{
								return ControllerSelectionFeedback;
							})
							.ColorAndOpacity(FSlateColor(
								UnrealAgentMCP::Palette::Danger()))
							.AutoWrapText(true)
						]
					]
				]
			]
		]
	);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildControllerChoiceCard(const EUnrealAgentControllerMode Mode, const FText& Title, const FText& Badge, const FText& Description)
{
	return SNew(SBorder)
		.Padding(1.0f)
		.BorderImage(GetPanelCardOuterBrush())
		.BorderBackgroundColor_Lambda([this, Mode]
		{
			return FSlateColor(IsControllerModeSelected(Mode)
				? GetEffectiveAccentColor()
				: GetPanelBorderColor());
		})
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(GetPanelCardInnerBrush())
			.BorderBackgroundColor_Lambda([this, Mode]
			{
				return FSlateColor(IsControllerModeSelected(Mode)
					? GetAccentFillColor(0.14f)
					: UnrealAgentMCP::Palette::SurfaceRaised());
			})
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(10.0f, 8.0f))
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.ToolTip(UnrealAgentMCPToolTip::Make(
					TAttribute<FText>::CreateLambda(
						[this, Mode, Title, Description]
						{
							return FText::Format(
								LOCTEXT(
									"ControllerChoiceHoverFormat",
									"{0}\n{1}\n{2}"),
								Title,
								Description,
								GetControllerChoiceStatus(Mode));
						})))
				.OnClicked(FOnClicked::CreateSP(
					this,
					&SUnrealAgentMCPPanel::OnControllerModeSelected,
					Mode))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Fill)
					.Padding(0.0f, 1.0f, 10.0f, 1.0f)
					[
						SNew(SBorder)
						.Padding(0.0f)
						.BorderImage(GetConversationIndicatorBrush())
						.BorderBackgroundColor_Lambda([this, Mode]
						{
							return FSlateColor(IsControllerModeSelected(Mode)
								? GetEffectiveAccentColor()
								: GetPanelBorderColor());
						})
						[
							SNew(SBox).WidthOverride(3.0f)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(Title)
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
								.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
								.AutoWrapText(false)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.Visibility(Badge.IsEmpty()
									? EVisibility::Collapsed
									: EVisibility::Visible)
								[
									BuildBadge(Badge)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(Description)
							.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
							.AutoWrapText(false)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this, Mode]
							{
								return GetControllerChoiceStatus(Mode);
							})
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.AutoWrapText(false)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.ColorAndOpacity_Lambda([this, Mode]
							{
								return FSlateColor(IsControllerModeSelected(Mode)
									? GetEffectiveAccentColor()
									: GetPanelMutedTextColor());
							})
						]
					]
				]
			]
		];
}

bool SUnrealAgentMCPPanel::IsControllerModeSelected(const EUnrealAgentControllerMode Mode) const
{
	return ControllerMode == Mode;
}

FText SUnrealAgentMCPPanel::GetControllerChoiceStatus(const EUnrealAgentControllerMode Mode) const
{
	const EUnrealAgentACPProvider Provider = Mode == EUnrealAgentControllerMode::ExternalCursorAgent ? EUnrealAgentACPProvider::Cursor : EUnrealAgentACPProvider::Codex;
	const TSharedPtr<FUnrealAgentCodexACPClient> Client = GetAcpClientForProvider(Provider);
	if (Client.IsValid() && Client->IsReady())
	{
		return LOCTEXT("ExternalAgentChoiceConnected", "ACP 已连接");
	}
	if (Provider == EUnrealAgentACPProvider::Cursor)
	{
		return IsCliAvailable(ECliTool::Cursor) ? LOCTEXT("CursorAgentChoiceReady", "Cursor Agent CLI 已检测") : LOCTEXT("CursorAgentChoiceMissing", "需要安装 Cursor Agent CLI");
	}
	return IsCliAvailable(ECliTool::Codex) ? LOCTEXT("CodexAgentChoiceReady", "Codex CLI 已检测") : LOCTEXT("CodexAgentChoiceMissing", "需要安装 Codex ACP");
}

FReply SUnrealAgentMCPPanel::OnControllerModeSelected(const EUnrealAgentControllerMode Mode)
{
	if (IsControllerModeSelected(Mode))
	{
		ControllerSelectionFeedback = FText::GetEmpty();
		SynchronizeControllerAcpRouting();
		SaveSettings();
		PersistConversationHistory();
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}
	if (IsActiveConversationControllerBusy())
	{
		ControllerSelectionFeedback = LOCTEXT("ControllerModeBusy", "当前对话仍在处理任务。请先停止当前回复，或等待完成后再切换代理。");
		SetLastAction(ControllerSelectionFeedback);
		return FReply::Handled();
	}
	ControllerSelectionFeedback = FText::GetEmpty();
	if (Mode == EUnrealAgentControllerMode::ExternalCodexAgent)
	{
		return OnProviderMenuItemClicked(EUnrealAgentACPProvider::Codex);
	}
	if (Mode == EUnrealAgentControllerMode::ExternalCursorAgent)
	{
		return OnProviderMenuItemClicked(EUnrealAgentACPProvider::Cursor);
	}

	return OnProviderMenuItemClicked(EUnrealAgentACPProvider::Codex);
}

FReply SUnrealAgentMCPPanel::OnProviderMenuItemClicked(EUnrealAgentACPProvider Provider)
{
	ControllerSelectionFeedback = FText::GetEmpty();
	const EUnrealAgentControllerMode DesiredControllerMode =
		Provider == EUnrealAgentACPProvider::Cursor ? EUnrealAgentControllerMode::ExternalCursorAgent : EUnrealAgentControllerMode::ExternalCodexAgent;
	if (ControllerMode == DesiredControllerMode && Provider == ActiveProvider)
	{
		SynchronizeControllerAcpRouting();
		SaveSettings();
		PersistConversationHistory();
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}

	SaveActiveConversation();
	ControllerMode = DesiredControllerMode;
	ActiveProvider = Provider;
	SynchronizeControllerAcpRouting();
	const FUnrealAgentACPAccountState Account = Provider == EUnrealAgentACPProvider::Cursor
		? FUnrealAgentACPAccountState{ TEXT("Cursor 账户"), TEXT("正在读取官方 Agent 登录状态…"), false }
		: UnrealAgentACPProviderModel::DetectAccountState(Provider);
	DetectedAccountLabel = Account.DisplayLabel;
	DetectedAccountSecondaryLabel = Account.SecondaryLabel;
	if (Provider == EUnrealAgentACPProvider::Codex)
	{
		bCodexAuthenticated = Account.bAuthenticated;
		RefreshCodexAccountState();
	}
	else
	{
		bCursorAuthenticated = Account.bAuthenticated;
		StartCursorAuthProcess(ECodexAuthAction::Status);
	}
	FSlateApplication::Get().DismissAllMenus();
	bShowSettings = false;
	SaveSettings();
	if (AcpClient.IsValid())
	{
		ApplyExecutionPolicy();
		RefreshProviderDependencyState();
		if (IsProviderDependencyAvailable())
		{
			// 适配器进程已在面板打开时预热。这里只切换 UI 目标，并在
			// 同一进程内后台准备一个干净 session；用户可立即输入和发送。
			AcpClient->BeginNewSession();
			if (TSharedPtr<FConversationAcpRuntime>* Runtime =
					Conversations.IsValidIndex(ActiveConversationIndex) ? ConversationAcpRuntimes.Find(Conversations[ActiveConversationIndex].Id) : nullptr;
				Runtime && Runtime->IsValid())
			{
				if (Provider == EUnrealAgentACPProvider::Cursor)
				{
					(*Runtime)->bCursorContextReplayPending = true;
				}
				else
				{
					(*Runtime)->bCodexContextReplayPending = true;
				}
			}
		}
	}
	bReplayConversationContextOnNextPrompt = !ConversationMessages.IsEmpty();
	PersistConversationHistory();

	if (Provider == EUnrealAgentACPProvider::Codex)
	{
		SetLastAction(LOCTEXT("CodexProviderSelected", "已切换到 Codex 代理；Codex 将自行规划并通过 Unreal Agent MCP 使用 UE 工具。"));
	}
	else
	{
		if (!IsCliAvailable(ECliTool::Cursor))
		{
			bShowSettings = true;
			SetLastAction(LOCTEXT("CursorProviderMissing", "未检测到 Cursor Agent CLI；请在设置中选择 agent。"));
		}
		else
		{
			SetLastAction(LOCTEXT("CursorProviderSelected", "已切换到 Cursor 代理；Cursor 将自行规划并通过 Unreal Agent MCP 使用 UE 工具。"));
		}
	}
	return FReply::Handled();
}

FText SUnrealAgentMCPPanel::GetProviderDisplayName() const
{
	if (CallbackProviderOverride.IsSet())
	{
		return UnrealAgentACPProviderModel::GetProvider(CallbackProviderOverride.GetValue()).DisplayName;
	}

	switch (ControllerMode)
	{
	case EUnrealAgentControllerMode::ExternalCodexAgent:
		return LOCTEXT("CodexAgentDisplayName", "Codex 代理");
	case EUnrealAgentControllerMode::ExternalCursorAgent:
		return LOCTEXT("CursorAgentDisplayName", "Cursor 代理");
	case EUnrealAgentControllerMode::NativeUnrealAgent:
	default:
		return LOCTEXT("CodexAgentDisplayNameFallback", "Codex 代理");
	}
}

FText SUnrealAgentMCPPanel::GetProviderStatusText() const
{
	const EUnrealAgentACPProvider ControllerProvider = GetModelCatalogProvider();
	const TSharedPtr<FUnrealAgentCodexACPClient> ControllerClient = GetAcpClientForProvider(ControllerProvider);
	if (ControllerClient.IsValid() && ControllerClient->IsReady())
	{
		const FString AppliedModelId = ControllerClient->GetAppliedModelId();
		return AppliedModelId.IsEmpty() ? LOCTEXT("CodexAcpReadyStatus", "ACP 已连接 · 正在确认模型")
										: FText::Format(LOCTEXT("CodexAcpReadyModelStatus", "ACP 已连接 · 实际模型 {0}"), FText::FromString(AppliedModelId));
	}
	if (ControllerClient.IsValid() && ControllerClient->IsRunning())
	{
		return LOCTEXT("CodexAcpSessionPreparingStatus", "ACP 已预热 · 会话后台准备中");
	}
	if (!IsProviderDependencyAvailable())
	{
		return ControllerProvider == EUnrealAgentACPProvider::Cursor ? LOCTEXT("CursorDependencyMissingStatus", "需要安装 Cursor Agent CLI")
																	 : LOCTEXT("CodexDependencyMissingStatus", "需要安装 Codex ACP");
	}
	if (ControllerProvider == EUnrealAgentACPProvider::Cursor)
	{
		return IsCliAvailable(ECliTool::Cursor) ? LOCTEXT("CursorCliReadyStatus", "Cursor Agent 已检测 · ACP 待连接")
												: LOCTEXT("CursorCliMissingStatus", "未检测到 Cursor Agent CLI");
	}
	if (!bCodexAuthenticated)
	{
		return LOCTEXT("CodexLoginRequiredStatus", "Codex 未登录 · 可直接连接，或点击“Codex 账户”登录");
	}
	return LOCTEXT("CodexAcpOfflineStatus", "ACP 未连接");
}

FText SUnrealAgentMCPPanel::GetAccountHoverText() const
{
	return FText::Format(LOCTEXT("ProviderAccountHoverCard", "{0} 账户\n{1}\n{2}"), GetProviderDisplayName(), FText::FromString(DetectedAccountLabel), GetAccountDetailText());
}

FText SUnrealAgentMCPPanel::GetAccountDetailText() const
{
	if (ActiveProvider == EUnrealAgentACPProvider::Cursor)
	{
		const FText CursorAuthentication = IsCliAvailable(ECliTool::Cursor) ? bCursorAuthenticated ? LOCTEXT("CursorAccountAuthenticated", "已通过 Cursor Agent CLI 登录")
																								   : LOCTEXT("CursorAccountManagedByCli", "未登录（由 Cursor Agent CLI 管理）")
																			: LOCTEXT("CursorAccountCliMissing", "未检测到 Cursor Agent CLI");
		return FText::Format(LOCTEXT("CursorAccountDetails", "状态：{0}\n账户信息由 Cursor Agent CLI 管理。"), CursorAuthentication);
	}

	const FText Authentication = bCodexAuthenticated ? LOCTEXT("AccountAuthenticated", "已登录") : LOCTEXT("AccountNotAuthenticated", "未登录");
	return FText::Format(LOCTEXT("CodexAccountDetails", "状态：{0}\n账户信息由 Codex CLI 提供。"), Authentication);
}

FText SUnrealAgentMCPPanel::GetAccountSecondaryText() const
{
	if (!DetectedAccountSecondaryLabel.IsEmpty())
	{
		return FText::FromString(DetectedAccountSecondaryLabel);
	}
	return ActiveProvider == EUnrealAgentACPProvider::Cursor ? LOCTEXT("CursorAccountSecondaryFallback", "由 Cursor Agent CLI 提供")
		: bCodexAuthenticated                                ? LOCTEXT("CodexAccountSecondaryAuthenticated", "Codex 官方账户")
															 : LOCTEXT("CodexAccountSecondarySignedOut", "未登录");
}

FText SUnrealAgentMCPPanel::GetActiveConversationHeader() const
{
	if (bShowSettings)
	{
		return LOCTEXT("SettingsHeader", "Unreal Agent 设置");
	}
	return Conversations.IsValidIndex(ActiveConversationIndex) ? Conversations[ActiveConversationIndex].Title : LOCTEXT("NewConversationHeader", "新对话");
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildToolbarButton(const FText& Label, const FOnClicked& OnClicked, TAttribute<bool> bIsEnabled) const
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SButton)
			.HAlign(HAlign_Center)
			.ButtonStyle(&ToolbarButtonStyle)
			.ForegroundColor(FSlateColor(GetPanelTextColor()))
			.Text(Label)
			.ToolTip(UnrealAgentMCPToolTip::Make(Label))
			.IsEnabled(bIsEnabled)
			.OnClicked(OnClicked));
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildIconTextButton(const FText& Label, const FOnClicked& OnClicked, const FText& Tooltip) const
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SButton)
		.HAlign(HAlign_Center)
		.ButtonStyle(&ToolbarButtonStyle)
		.ForegroundColor(FSlateColor(GetPanelTextColor()))
		.ContentPadding(FMargin(10.0f, 3.0f))
		.Text(Label)
		.ToolTip(UnrealAgentMCPToolTip::Make(Tooltip))
		.OnClicked(OnClicked));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModeCombo()
{
		return UnrealAgentMCPPanelWidgets::AnimateControl(
			SNew(SComboButton)
			.ToolTip(UnrealAgentMCPToolTip::Make(
				LOCTEXT("ModeComboTooltip", "选择 Codex 执行模式")))
			.ComboButtonStyle(&ComposerComboButtonStyle)
			.ButtonStyle(&ComposerButtonStyle)
			.ButtonColorAndOpacity_Lambda([this] { return FSlateColor(GetAccentControlColor()); })
			.ForegroundColor(FSlateColor(GetPanelTextColor()))
			.ContentPadding(FMargin(8.0f, 3.0f))
			.HasDownArrow(false)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return GetCachedGeometry().GetLocalSize().X < 900.0f
							? LOCTEXT("AgentModeCompact", "A")
							: GetModeLabel(CurrentAgentMode);
					})
					.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ModeComboChevron", "v"))
					.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
				]
			]
			.OnGetMenuContent(FOnGetContent::CreateSP(
				this,
				&SUnrealAgentMCPPanel::BuildModeMenu)));
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModeMenu()
{
		return UnrealAgentMCPPanelWidgets::AnimateMenu(
			SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetPanelCardOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetPanelBorderColor()))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 7.0f))
				.BorderImage(GetPanelCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetPanelBackgroundColor()))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 2.0f, 4.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ModeMenuTitle", "模式"))
						.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildModeMenuItem(EWorldDataAgentMode::Chat, LOCTEXT("ModeChatLabel", "对话"), LOCTEXT("ModeChatDesc", "仅回答问题，不调用 MCP 工具"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						BuildModeMenuItem(EWorldDataAgentMode::Plan, LOCTEXT("ModePlanLabel", "计划"), LOCTEXT("ModePlanDesc", "允许只读工具，不修改项目"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						BuildModeMenuItem(EWorldDataAgentMode::Agent, LOCTEXT("ModeAgentLabel", "Agent"), LOCTEXT("ModeAgentDesc", "使用 MCP 工具执行任务"))
					]
				]
			]);
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModeMenuItem(EWorldDataAgentMode Mode, const FText& Label, const FText& Description)
{
		return SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetPanelCardOuterBrush())
			.BorderBackgroundColor_Lambda([this, Mode]
			{
				return CurrentAgentMode == Mode
					? FSlateColor(GetAccentBorderColor())
					: FSlateColor(UnrealAgentMCP::Palette::Background());
			})
			.OnMouseButtonDown_Lambda([this, Mode](const FGeometry&, const FPointerEvent&)
			{
				CurrentAgentMode = Mode;
				ApplyExecutionPolicy();
				SaveSettings();
				PersistConversationHistory();
				SetLastAction(FText::Format(LOCTEXT("ModeChangedAction", "已切换到：{0}"), GetModeLabel(CurrentAgentMode)));
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.Padding(FMargin(10.0f, 7.0f))
				.BorderImage(GetPanelCardInnerBrush())
				.BorderBackgroundColor_Lambda([this, Mode]
				{
					return CurrentAgentMode == Mode
						? FSlateColor(GetAccentFillColor(0.18f))
						: FSlateColor(UnrealAgentMCP::Palette::Background());
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(Label)
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Description)
						.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					]
				]
			];
	}

FText SUnrealAgentMCPPanel::GetModeLabel(EWorldDataAgentMode Mode) const
{
	switch (Mode)
	{
	case EWorldDataAgentMode::Plan:
		return LOCTEXT("ModePlanShort", "计划模式");
	case EWorldDataAgentMode::Agent:
		return LOCTEXT("ModeAgentShort", "Agent");
	case EWorldDataAgentMode::Chat:
	default:
		return LOCTEXT("ModeChatShort", "对话");
	}
}

void SUnrealAgentMCPPanel::ApplyExecutionPolicy()
{
	if (Conversations.IsValidIndex(ActiveConversationIndex))
	{
		FConversation& Conversation = Conversations[ActiveConversationIndex];
		Conversation.AgentMode = static_cast<uint8>(CurrentAgentMode);
		Conversation.ApprovalPolicy = static_cast<uint8>(CurrentApprovalPolicy);
		Conversation.SelfRepairPolicy = static_cast<uint8>(CurrentSelfRepairPolicy);
		const TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id);
		if (Runtime && Runtime->IsValid())
		{
			for (const TSharedPtr<FUnrealAgentCodexACPClient>& Client : { (*Runtime)->CodexClient, (*Runtime)->CursorClient })
			{
				if (Client.IsValid())
				{
					Client->SetExecutionPolicy(CurrentAgentMode, CurrentApprovalPolicy, CurrentSelfRepairPolicy);
				}
			}
		}
	}
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildApprovalCombo()
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SComboButton)
		.ComboButtonStyle(&ComposerComboButtonStyle)
		.ButtonStyle(&ComposerButtonStyle)
		.ToolTip(UnrealAgentMCPToolTip::Make(
			LOCTEXT("ApprovalComboTooltip", "权限批准策略")))
		.ContentPadding(FMargin(8.0f, 3.0f))
		.ButtonContent()
		[
			SNew(STextBlock)
		.Text_Lambda([this]
			{
				return GetCachedGeometry().GetLocalSize().X < 900.0f
					? LOCTEXT("ApprovalCompact", "✓")
					: GetApprovalLabel(CurrentApprovalPolicy);
			})
		.ColorAndOpacity_Lambda([this]
		{
			return FSlateColor(
				CurrentApprovalPolicy ==
					EWorldDataApprovalPolicy::FullProject
					? UnrealAgentMCP::Palette::Danger()
					: GetPanelTextColor());
		})
		]
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SUnrealAgentMCPPanel::BuildApprovalMenu)));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildApprovalMenu()
{
	return UnrealAgentMCPPanelWidgets::AnimateMenu(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildApprovalMenuItem(EWorldDataApprovalPolicy::AlwaysAsk, LOCTEXT("ApprovalAlwaysAsk", "请求批准"), LOCTEXT("ApprovalAlwaysAskDesc", "修改项目之前始终询问"))]
		+ SVerticalBox::Slot().AutoHeight()[BuildApprovalMenuItem(EWorldDataApprovalPolicy::RiskBased, LOCTEXT("ApprovalRiskBased", "风险审批"), LOCTEXT("ApprovalRiskBasedDesc", "普通项目操作自动通过，高风险操作询问"))]
		+ SVerticalBox::Slot().AutoHeight()[BuildApprovalMenuItem(EWorldDataApprovalPolicy::FullProject, LOCTEXT("ApprovalFullProject", "完全访问（项目内）"), LOCTEXT("ApprovalFullProjectDesc", "普通项目操作自动批准；强制确认仍询问，Shell 仍禁用"))]);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildApprovalMenuItem(const EWorldDataApprovalPolicy Policy, const FText& Label, const FText& Description)
{
	return SNew(SButton)
		.ButtonStyle(&ComposerButtonStyle)
		.ContentPadding(FMargin(10.0f, 6.0f))
		.OnClicked_Lambda([this, Policy]
		{
			CurrentApprovalPolicy = Policy;
			ApplyExecutionPolicy();
			SaveSettings();
			PersistConversationHistory();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::Format(LOCTEXT("SelectedPolicyFormat", "{0}{1}"), Policy == CurrentApprovalPolicy ? LOCTEXT("SelectedPolicyMark", "✓ ") : FText::GetEmpty(), Label)).ColorAndOpacity(Policy == EWorldDataApprovalPolicy::FullProject ? FSlateColor(UnrealAgentMCP::Palette::Danger()) : FSlateColor(GetPanelTextColor()))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Description).ColorAndOpacity(Policy == EWorldDataApprovalPolicy::FullProject ? FSlateColor(UnrealAgentMCP::Palette::Danger()) : FSlateColor(GetPanelMutedTextColor()))]
		];
}

FText SUnrealAgentMCPPanel::GetApprovalLabel(const EWorldDataApprovalPolicy Policy) const
{
	return Policy == EWorldDataApprovalPolicy::FullProject ? LOCTEXT("ApprovalFullShort", "完全访问")
		: Policy == EWorldDataApprovalPolicy::RiskBased    ? LOCTEXT("ApprovalRiskShort", "风险审批")
														   : LOCTEXT("ApprovalAskShort", "请求批准");
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildSelfRepairCombo()
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SComboButton)
		.ComboButtonStyle(&ComposerComboButtonStyle)
		.ButtonStyle(&ComposerButtonStyle)
		.ToolTip(UnrealAgentMCPToolTip::Make(
			LOCTEXT("SelfRepairComboTooltip", "自我修复策略")))
		.ContentPadding(FMargin(8.0f, 3.0f))
		.ButtonContent()[SNew(STextBlock).Text_Lambda([this]
		{
			return GetCachedGeometry().GetLocalSize().X < 900.0f
				? LOCTEXT("SelfRepairCompact", "↻")
				: GetSelfRepairLabel(CurrentSelfRepairPolicy);
		}).ColorAndOpacity_Lambda([this]
		{
			return FSlateColor(
				CurrentSelfRepairPolicy ==
					EWorldDataSelfRepairPolicy::Automatic
					? UnrealAgentMCP::Palette::Danger()
					: GetPanelTextColor());
		})]
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SUnrealAgentMCPPanel::BuildSelfRepairMenu)));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildSelfRepairMenu()
{
	return UnrealAgentMCPPanelWidgets::AnimateMenu(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildSelfRepairMenuItem(EWorldDataSelfRepairPolicy::Off, LOCTEXT("SelfRepairOff", "关闭"), LOCTEXT("SelfRepairOffDesc", "不启动自我修复"))]
		+ SVerticalBox::Slot().AutoHeight()[BuildSelfRepairMenuItem(EWorldDataSelfRepairPolicy::Suggest, LOCTEXT("SelfRepairSuggest", "建议修复"), LOCTEXT("SelfRepairSuggestDesc", "诊断并给出方案，等待确认"))]
		+ SVerticalBox::Slot().AutoHeight()[BuildSelfRepairMenuItem(EWorldDataSelfRepairPolicy::Automatic, LOCTEXT("SelfRepairAutomatic", "自动修复"), LOCTEXT("SelfRepairAutomaticDesc", "项目内诊断、修补、验证并在失败时回滚"))]);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildSelfRepairMenuItem(const EWorldDataSelfRepairPolicy Policy, const FText& Label, const FText& Description)
{
	return SNew(SButton)
		.ButtonStyle(&ComposerButtonStyle)
		.ContentPadding(FMargin(10.0f, 6.0f))
		.OnClicked_Lambda([this, Policy]
		{
			CurrentSelfRepairPolicy = Policy;
			ApplyExecutionPolicy();
			SaveSettings();
			PersistConversationHistory();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::Format(LOCTEXT("SelectedRepairFormat", "{0}{1}"), Policy == CurrentSelfRepairPolicy ? LOCTEXT("SelectedRepairMark", "✓ ") : FText::GetEmpty(), Label)).ColorAndOpacity(Policy == EWorldDataSelfRepairPolicy::Automatic ? FSlateColor(UnrealAgentMCP::Palette::Danger()) : FSlateColor(GetPanelTextColor()))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Description).ColorAndOpacity(Policy == EWorldDataSelfRepairPolicy::Automatic ? FSlateColor(UnrealAgentMCP::Palette::Danger()) : FSlateColor(GetPanelMutedTextColor()))]
		];
}

FText SUnrealAgentMCPPanel::GetSelfRepairLabel(const EWorldDataSelfRepairPolicy Policy) const
{
	return Policy == EWorldDataSelfRepairPolicy::Automatic ? LOCTEXT("SelfRepairAutomaticShort", "自动修复")
		: Policy == EWorldDataSelfRepairPolicy::Suggest    ? LOCTEXT("SelfRepairSuggestShort", "建议修复")
														   : LOCTEXT("SelfRepairOffShort", "修复关闭");
}

EUnrealAgentACPProvider SUnrealAgentMCPPanel::GetModelCatalogProvider() const
{
	const FString FallbackProviderId = ActiveProvider == EUnrealAgentACPProvider::Cursor ? TEXT("cursor") : TEXT("codex");
	return UnrealAgentMCPPanelSettings::ResolveControllerAcpProviderId(ControllerMode, FallbackProviderId).Equals(TEXT("cursor"), ESearchCase::IgnoreCase)
		? EUnrealAgentACPProvider::Cursor
		: EUnrealAgentACPProvider::Codex;
}

void SUnrealAgentMCPPanel::SynchronizeControllerAcpRouting()
{
	const EUnrealAgentACPProvider ControllerProvider = GetModelCatalogProvider();
	ActiveProvider = ControllerProvider;
	AcpClient = GetAcpClientForProvider(ControllerProvider);

	const TArray<FUnrealAgentAcpConfigOption> LiveOptions = AcpClient.IsValid() ? AcpClient->GetConfigOptions() : TArray<FUnrealAgentAcpConfigOption>();
	const TArray<FUnrealAgentAcpConfigOption>* CachedOptions = ProviderConfigCache.Find(ControllerProvider);
	AcpConfigOptions = !LiveOptions.IsEmpty() ? LiveOptions : CachedOptions ? *CachedOptions : TArray<FUnrealAgentAcpConfigOption>();
}

TSharedPtr<FUnrealAgentCodexACPClient> SUnrealAgentMCPPanel::GetModelConfigClient() const
{
	return GetAcpClientForProvider(GetModelCatalogProvider());
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModelCombo()
{
	return UnrealAgentMCPPanelWidgets::AnimateControl(
		SNew(SComboButton)
		.ComboButtonStyle(&ComposerComboButtonStyle)
		.ButtonStyle(&ComposerButtonStyle)
		.ButtonColorAndOpacity_Lambda([this]
		{
			return FSlateColor(GetAccentControlColor());
		})
		.ForegroundColor(FSlateColor(GetPanelTextColor()))
		.ContentPadding(FMargin(8.0f, 3.0f))
		.HasDownArrow(true)
		.IsEnabled_Lambda([this]
		{
			// 目录首次后台加载时也保持正常按钮外观；菜单内显示同步提示，
			// 已有缓存时则可以立即选择，不因 session/new 变黑。
			return GetModelConfigClient().IsValid();
		})
		.ToolTip(UnrealAgentMCPToolTip::Make(TAttribute<FText>::CreateLambda(
			[this]
			{
				return LOCTEXT(
					"ModelComboTooltip",
					"模型目录由当前 Codex/Cursor 代理实时公布");
			})))
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				return GetCodexConfigSummary();
			})
			.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
		]
		.OnGetMenuContent(FOnGetContent::CreateLambda(
			[this]
			{
				return BuildModelMenu();
			})));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModelMenu()
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const FText AgentName = UnrealAgentACPProviderModel::GetProvider(
		GetModelCatalogProvider()).DisplayName;
	Menu->AddSlot()
		.AutoHeight()
		.Padding(FMargin(10.0f, 8.0f, 10.0f, 6.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("AgentScopedModelMenuTitle", "{0} 模型与推理"),
					AgentName))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT(
						"AgentScopedModelMenuDescription",
						"仅显示 {0} ACP 为当前会话实时发布的选项。"),
					AgentName))
				.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
				.AutoWrapText(true)
			]
		];

	bool bAddedSection = false;
	auto AddSection = [this, &Menu, &bAddedSection](
		const FUnrealAgentAcpConfigOption* Option,
		const FText& FallbackTitle)
	{
		if (!Option)
		{
			return;
		}
		bAddedSection = true;
		Menu->AddSlot()
			.AutoHeight()
			.Padding(FMargin(10.0f, 8.0f, 10.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(Option && !Option->Name.IsEmpty()
					? FText::FromString(Option->Name)
					: FallbackTitle)
				.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			];

		if (Option->Options.IsEmpty())
		{
			Menu->AddSlot()
				.AutoHeight()
				.Padding(FMargin(10.0f, 4.0f, 10.0f, 8.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"ConfigOptionsPending",
						"正在后台同步当前 Agent 模型配置…"))
					.ColorAndOpacity(
						FSlateColor(GetPanelMutedTextColor()))
				];
			return;
		}

		for (const FUnrealAgentAcpConfigOptionValue& Value :
			Option->Options)
		{
			Menu->AddSlot()
				.AutoHeight()
				.Padding(2.0f)
				[
					BuildModelMenuItem(
						Option->Id,
						Value,
						Value.Value == Option->CurrentValue)
				];
		}
	};

	if (AcpConfigOptions.IsEmpty())
	{
		bAddedSection = true;
		Menu->AddSlot()
			.AutoHeight()
			.Padding(FMargin(10.0f, 10.0f, 10.0f, 12.0f))
			[
				SNew(SBorder)
				.Padding(FMargin(10.0f, 9.0f))
				.BorderImage(GetPanelCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(
					GetAccentFillColor(0.10f)))
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT(
							"AgentModelCatalogUnavailable",
							"{0} ACP 尚未发布可选择的模型；当前会话由该代理自动选择。"),
						AgentName))
					.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
					.AutoWrapText(true)
				]
			];
	}

	AddSection(
		FindConfigOption(TEXT("model")),
		LOCTEXT("ModelSectionLabel", "模型"));
	AddSection(
		FindConfigOption(TEXT("model_reasoning_effort")),
		LOCTEXT("ReasoningSectionLabel", "推理强度"));
	AddSection(
		FindConfigOption(TEXT("service_tier")),
		LOCTEXT("SpeedSectionLabel", "速度"));
	if (!bAddedSection)
	{
		Menu->AddSlot()
			.AutoHeight()
			.Padding(FMargin(10.0f, 10.0f, 10.0f, 12.0f))
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT(
						"AgentModelOptionsUnavailable",
						"{0} ACP 当前未发布模型选择项；不会显示其他代理的目录。"),
					AgentName))
				.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
				.AutoWrapText(true)
			];
	}

	Menu->AddSlot()
		.AutoHeight()
		.Padding(FMargin(10.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT(
					"LiveCatalogFooter",
					"只使用 {0} ACP 当前会话公布的选项，不会复用其他代理的模型目录。"),
				AgentName))
			.ColorAndOpacity(FSlateColor(GetPanelSubduedTextColor()))
		];

	return UnrealAgentMCPPanelWidgets::AnimateMenu(
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SBox)
			.MinDesiredWidth(320.0f)
			.MaxDesiredHeight(560.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Menu
				]
			]
		]);
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildModelMenuItem(const FString& ConfigId, const FUnrealAgentAcpConfigOptionValue& Value, bool bSelected)
{
	const bool bCursorModel =
		GetModelCatalogProvider() == EUnrealAgentACPProvider::Cursor
		&& ConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase);
	const TArray<FString> CursorModeLabels = bCursorModel
		? WorldDataCodexAcpRules::GetCursorModelModeLabels(Value.Value)
		: TArray<FString>();
	TSharedRef<SHorizontalBox> ModelTitleRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				Value.Name.IsEmpty()
					? Value.Value
					: Value.Name))
			.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
		];
	for (const FString& ModeLabel : CursorModeLabels)
	{
		ModelTitleRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6.0f, 2.0f))
				.BorderImage(GetPanelCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(
					GetAccentFillColor(0.16f)))
				[
					SNew(STextBlock)
					.Text(FText::FromString(ModeLabel))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor(
						GetEffectiveAccentColor()))
				]
			];
	}

	return SNew(UnrealAgentMCPPanelWidgets::SAnimatedButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.IsEnabled(Value.bEnabled)
		.ContentPadding(FMargin(10.0f, 6.0f))
		.HAlign(HAlign_Fill)
		.ToolTip(UnrealAgentMCPToolTip::Make(
			FText::FromString(Value.Description)))
		.OnClicked(FOnClicked::CreateSP(
			this,
			&SUnrealAgentMCPPanel::OnModelSelected,
			ConfigId,
			Value.Value))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(bSelected
					? LOCTEXT("ModelSelectedGlyph", "●")
					: LOCTEXT("ModelUnselectedGlyph", "○"))
				.ColorAndOpacity(bSelected
					? FSlateColor(GetEffectiveAccentColor())
					: FSlateColor(GetPanelMutedTextColor()))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ModelTitleRow
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Value.Description))
					.ColorAndOpacity(
						FSlateColor(GetPanelMutedTextColor()))
					.AutoWrapText(true)
					.Visibility(Value.Description.IsEmpty()
						? EVisibility::Collapsed
						: EVisibility::Visible)
				]
			]
		];
}

FReply SUnrealAgentMCPPanel::OnModelSelected(FString ConfigId, FString Value)
{
	const TSharedPtr<FUnrealAgentCodexACPClient> ModelConfigClient = GetModelConfigClient();
	const bool bHasActiveTurn = ModelConfigClient.IsValid() && ModelConfigClient->HasActiveTurn();
	const bool bCanApplyNow = ModelConfigClient.IsValid() && ModelConfigClient->CanChangeConfig();
	if (bCanApplyNow && !ModelConfigClient->SetConfigOption(ConfigId, Value))
	{
		const FString Error = ModelConfigClient->GetLastError();
		SetLastAction(FText::FromString(Error.IsEmpty() ? TEXT("当前 Codex/Cursor 模型会话拒绝了该配置，选择未更改。") : Error));
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}
	if (ModelConfigClient.IsValid() && !bCanApplyNow && !bHasActiveTurn)
	{
		SetLastAction(LOCTEXT("ModelSelectionBlockedAction", "当前 ACP 正在处理权限或配置同步，模型选择未更改，请稍后重试。"));
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}
	if (!ModelConfigClient.IsValid())
	{
		SetLastAction(LOCTEXT("ModelSelectionClientUnavailable", "当前运行身份的 Codex/Cursor 模型目录尚未就绪，请稍后重试。"));
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}
	for (FUnrealAgentAcpConfigOption& Option : AcpConfigOptions)
	{
		if (Option.Id == ConfigId)
		{
			Option.CurrentValue = Value;
			break;
		}
	}
	const EUnrealAgentACPProvider ModelProvider = GetModelCatalogProvider();
	ProviderConfigCache.Add(ModelProvider, AcpConfigOptions);
	ProviderDesiredConfigValues.FindOrAdd(ModelProvider).Add(ConfigId, Value);
	SaveSettings();
	SetLastAction(
		bHasActiveTurn ? LOCTEXT("ModelSelectedForNextPrompt", "当前回合保持原模型；新选择已保存，将从下一条消息生效。") : LOCTEXT("ModelSelectedAction", "模型选择已更新。"));
	FSlateApplication::Get().DismissAllMenus();
	return FReply::Handled();
}

FText SUnrealAgentMCPPanel::GetModelLabel() const
{
	const FText AgentName = UnrealAgentACPProviderModel::GetProvider(GetModelCatalogProvider()).DisplayName;
	auto FormatAgentModel = [this, &AgentName](const FText& ModelName, const FString& ModelSelector)
	{
		const FText BaseLabel = FText::Format(LOCTEXT("AgentScopedModelLabel", "{0} · {1}"), AgentName, ModelName);
		if (GetModelCatalogProvider() != EUnrealAgentACPProvider::Cursor)
		{
			return BaseLabel;
		}
		const TArray<FString> ModeLabels = WorldDataCodexAcpRules::GetCursorModelModeLabels(ModelSelector);
		return ModeLabels.IsEmpty() ? BaseLabel
									: FText::Format(LOCTEXT("CursorModelLabelWithModes", "{0} · {1}"), BaseLabel, FText::FromString(FString::Join(ModeLabels, TEXT(" · "))));
	};
	const FUnrealAgentAcpConfigOption* Model = FindModelConfigOption();
	if (!Model)
	{
		return FormatAgentModel(LOCTEXT("ModelAutomaticLabel", "自动"), FString());
	}

	for (const FUnrealAgentAcpConfigOptionValue& Value : Model->Options)
	{
		if (Value.Value == Model->CurrentValue)
		{
			return FormatAgentModel(FText::FromString(Value.Name.IsEmpty() ? Value.Value : Value.Name), Value.Value);
		}
	}
	return FormatAgentModel(Model->CurrentValue.IsEmpty() ? LOCTEXT("ModelAutomaticLabel", "自动") : FText::FromString(Model->CurrentValue), Model->CurrentValue);
}

FText SUnrealAgentMCPPanel::GetCodexConfigSummary() const
{
	FString Summary = GetModelLabel().ToString();
	const FUnrealAgentAcpConfigOption* Reasoning = FindConfigOption(TEXT("model_reasoning_effort"));
	if (Reasoning)
	{
		for (const FUnrealAgentAcpConfigOptionValue& Value : Reasoning->Options)
		{
			if (Value.Value == Reasoning->CurrentValue)
			{
				Summary += TEXT(" · ");
				Summary += Value.Name.IsEmpty() ? Value.Value : Value.Name;
				break;
			}
		}
	}

	const FUnrealAgentAcpConfigOption* Speed = FindConfigOption(TEXT("service_tier"));
	if (Speed && Speed->CurrentValue == TEXT("fast"))
	{
		Summary += TEXT(" · 快速");
	}
	return FText::FromString(Summary);
}

void SUnrealAgentMCPPanel::HandleAcpConfigOptions(const TArray<FUnrealAgentAcpConfigOption>& Options)
{
	AcpConfigOptions = Options;
	SetLastAction(LOCTEXT("AcpConfigOptionsUpdated", "已同步 ACP 模型与会话配置。"));
}

const FUnrealAgentAcpConfigOption* SUnrealAgentMCPPanel::FindModelConfigOption() const
{
	return FindConfigOption(TEXT("model"));
}

const FUnrealAgentAcpConfigOption* SUnrealAgentMCPPanel::FindConfigOption(const FString& ConfigId) const
{
	for (const FUnrealAgentAcpConfigOption& Option : AcpConfigOptions)
	{
		if (Option.Id.Equals(ConfigId, ESearchCase::IgnoreCase))
		{
			return &Option;
		}
	}
	return nullptr;
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildDateLabel(const FText& Label) const
{
		return SNew(STextBlock)
			.Text(Label)
			.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); });
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationItem(const FText& Title, TAttribute<FText> Age, bool bActive, TAttribute<bool> bRunning, bool bUnreadCompletion,
	bool bCanArchive, FOnClicked OnClicked, FOnClicked OnArchiveClicked) const
{
		const bool bClickable = OnClicked.IsBound();
		const bool bHasArchiveAction = OnArchiveClicked.IsBound();
		const TSharedRef<TWeakPtr<SBorder>> ItemBorderWeak =
			MakeShared<TWeakPtr<SBorder>>();
		const TSharedRef<SBorder> ItemBorder = SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(GetConversationItemBrush())
			.Cursor(bClickable ? EMouseCursor::Hand : EMouseCursor::Default)
			.BorderBackgroundColor_Lambda([this, bActive] { return FSlateColor(bActive ? GetAccentFillColor(0.22f) : FLinearColor::Transparent); })
			.OnMouseButtonDown_Lambda([OnClicked](const FGeometry&, const FPointerEvent&)
			{
				return OnClicked.IsBound() ? OnClicked.Execute() : FReply::Unhandled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(3.0f)
					[
						SNew(SBorder)
						.Padding(0.0f)
						.BorderImage(GetConversationIndicatorBrush())
						.BorderBackgroundColor_Lambda([this, bActive] { return FSlateColor(bActive ? GetEffectiveAccentColor() : FLinearColor::Transparent); })
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8.0f, 7.0f, 8.0f, 7.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Title)
						.ColorAndOpacity_Lambda([this, bActive] { return FSlateColor(bActive ? GetEffectiveAccentColor() : GetReadableAccentTextColor()); })
						.Font(bActive ? FAppStyle::GetFontStyle("NormalFontBold") : FAppStyle::GetFontStyle("NormalFont"))
						.AutoWrapText(false)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(22.0f)
						.HeightOverride(22.0f)
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"ArchiveConversationEntryTooltip",
							"归档对话（未完成时将中止任务）")))
						.Visibility_Lambda([ItemBorderWeak, bHasArchiveAction]
						{
							const TSharedPtr<SBorder> PinnedBorder =
								ItemBorderWeak->Pin();
							return bHasArchiveAction
								&& PinnedBorder.IsValid()
								&& PinnedBorder->IsHovered()
								? EVisibility::Visible
								: EVisibility::Hidden;
						})
						[
							SNew(SButton)
							.ButtonStyle(&ToolbarButtonStyle)
							.ContentPadding(3.0f)
							.IsFocusable(false)
							.IsEnabled(bCanArchive)
							.OnClicked(OnArchiveClicked)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("HomeScreen.Archive"))
								.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
								.ColorAndOpacity_Lambda([this, bCanArchive]
								{
									return FSlateColor(bCanArchive
										? GetPanelMutedTextColor()
										: GetPanelSubduedTextColor());
								})
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(12.0f)
						.HeightOverride(12.0f)
						.Visibility_Lambda([bRunning]
						{
							return bRunning.Get()
								? EVisibility::HitTestInvisible
								: EVisibility::Collapsed;
						})
						[
							SNew(SCircularThrobber)
							.Radius(5.0f)
							.NumPieces(8)
							.Period(0.7f)
							.ColorAndOpacity(FSlateColor(
								GetPanelMutedTextColor()))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						.Visibility_Lambda([bRunning, bUnreadCompletion]
						{
							return !bRunning.Get() && bUnreadCompletion
								? EVisibility::HitTestInvisible
								: EVisibility::Collapsed;
						})
						[
							SNew(SBorder)
							.Padding(0.0f)
							.BorderImage(GetConversationIndicatorBrush())
							.BorderBackgroundColor(FSlateColor(
								FLinearColor(0.95f, 0.72f, 0.08f, 1.0f)))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Age)
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
					]
				]
			];

		*ItemBorderWeak = ItemBorder;
		return ItemBorder;
	}

#undef LOCTEXT_NAMESPACE
