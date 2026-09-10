// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file SUnrealAgentMCPPanel.h
 * @brief WorldData MCP 编辑器控制台：Codex 对话、服务器设置与 CLI 配置 UI。
 */

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Application/Conversation/UnrealAgentMCPConversationRepository.h"
#include "Application/UnrealAgentMCPApplicationService.h"
#include "Styling/SlateTypes.h"
#include "Presentation/Panel/UnrealAgentMCPStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"
#include "Application/ACP/UnrealAgentACPProviderModel.h"
#include "Application/ACP/UnrealAgentCodexACPClient.h"
#include "Application/Media/UnrealAgentPromptMedia.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"
#include "Presentation/Panel/UnrealAgentMCPPanelNavigation.h"
#include "Presentation/Settings/UnrealAgentMCPPanelSettings.h"

class SMultiLineEditableTextBox;
class SUnrealAgentMCPComposerEditableText;
class SUnrealAgentMCPMarkdown;
class SScrollBox;
class SScrollBar;
class SBorder;
class SBox;
class SSeparator;
class STextBlock;
class SWidgetSwitcher;
class SWrapBox;
class SVerticalBox;
class FDragDropEvent;
class FDragDropOperation;
class FInteractiveProcess;
class FUnrealAgentMCPTextSelectionGroup;
class FSlateTextLayout;
class SWindow;
struct FGeometry;
struct FKeyEvent;
struct FPointerEvent;

/** MCP 控制台主面板（会话列表、Codex ACP 对话与服务器管理）。 */
class SUnrealAgentMCPPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAgentMCPPanel) {}
		SLATE_ARGUMENT(TSharedPtr<IUnrealAgentMCPApplicationService>, ApplicationService)
		SLATE_ARGUMENT(TSharedPtr<IUnrealAgentMCPConversationRepository>, ConversationRepository)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual ~SUnrealAgentMCPPanel() override;
	/** Dock Tab 与控件析构共同调用的幂等关闭边界。 */
	void ShutdownPanelSession();

private:
	using EConversationMessageRole = EWorldDataConversationMessageRole;
	using FConversationMessage = FWorldDataConversationMessage;
	using FConversation = FWorldDataConversation;

	using ECliTool = EUnrealAgentMCPPanelCliTool;

	TSharedRef<SWidget> BuildUEBridgeStyleLayout();

	TSharedRef<SWidget> BuildUEBridgeTopBar();

	TSharedRef<SWidget> BuildUEBridgeSidebar();

	TSharedRef<SWidget> BuildUEBridgeMainArea();
	TSharedRef<SWidget> BuildBadge(const FText& Text) const;

	TSharedRef<SWidget> BuildProviderCombo();

	TSharedRef<SWidget> BuildProviderMenu();

	TSharedRef<SWidget> BuildProviderContextControl(bool bCompact = true);

	TSharedRef<SWidget> BuildProviderAccountCombo(bool bCompact = true);

	TSharedRef<SWidget> BuildProviderAccountMenu();

	TSharedRef<SWidget> BuildControllerChoiceCard(EUnrealAgentControllerMode Mode, const FText& Title, const FText& Badge, const FText& Description);
	FReply OnControllerModeSelected(EUnrealAgentControllerMode Mode);
	bool IsControllerModeSelected(EUnrealAgentControllerMode Mode) const;
	FText GetControllerChoiceStatus(EUnrealAgentControllerMode Mode) const;

	FReply OnProviderMenuItemClicked(EUnrealAgentACPProvider Provider);

	FText GetProviderDisplayName() const;

	FText GetProviderStatusText() const;

	FText GetAccountHoverText() const;

	FText GetAccountDetailText() const;

	FText GetAccountSecondaryText() const;

	FText GetActiveConversationHeader() const;

	TSharedRef<SWidget> BuildToolbarButton(const FText& Label, const FOnClicked& OnClicked, TAttribute<bool> bIsEnabled = TAttribute<bool>(true)) const;

	TSharedRef<SWidget> BuildIconTextButton(const FText& Label, const FOnClicked& OnClicked, const FText& Tooltip = FText::GetEmpty()) const;

	TSharedRef<SWidget> BuildModeCombo();

	TSharedRef<SWidget> BuildModeMenu();

	TSharedRef<SWidget> BuildModeMenuItem(EWorldDataAgentMode Mode, const FText& Label, const FText& Description);

	FText GetModeLabel(EWorldDataAgentMode Mode) const;

	TSharedRef<SWidget> BuildApprovalCombo();
	TSharedRef<SWidget> BuildApprovalMenu();
	TSharedRef<SWidget> BuildApprovalMenuItem(EWorldDataApprovalPolicy Policy, const FText& Label, const FText& Description);
	FText GetApprovalLabel(EWorldDataApprovalPolicy Policy) const;

	TSharedRef<SWidget> BuildSelfRepairCombo();
	TSharedRef<SWidget> BuildSelfRepairMenu();
	TSharedRef<SWidget> BuildSelfRepairMenuItem(EWorldDataSelfRepairPolicy Policy, const FText& Label, const FText& Description);
	FText GetSelfRepairLabel(EWorldDataSelfRepairPolicy Policy) const;

	void ApplyExecutionPolicy();

	TSharedRef<SWidget> BuildModelCombo();

	TSharedRef<SWidget> BuildModelMenu();
	EUnrealAgentACPProvider GetModelCatalogProvider() const;
	/** 依据左上角代理身份同步 Provider、ACP 客户端与模型目录。 */
	void SynchronizeControllerAcpRouting();
	TSharedPtr<FUnrealAgentCodexACPClient> GetModelConfigClient() const;

	TSharedRef<SWidget> BuildModelMenuItem(const FString& ConfigId, const FUnrealAgentAcpConfigOptionValue& Value, bool bSelected);

	FReply OnModelSelected(FString ConfigId, FString Value);

	FText GetModelLabel() const;

	FText GetCodexConfigSummary() const;

	void HandleAcpConfigOptions(const TArray<FUnrealAgentAcpConfigOption>& Options);

	const FUnrealAgentAcpConfigOption* FindModelConfigOption() const;

	const FUnrealAgentAcpConfigOption* FindConfigOption(const FString& ConfigId) const;

	TSharedRef<SWidget> BuildDateLabel(const FText& Label) const;

	TSharedRef<SWidget> BuildConversationItem(const FText& Title, TAttribute<FText> Age, bool bActive, TAttribute<bool> bRunning, bool bUnreadCompletion, bool bCanArchive,
		FOnClicked OnClicked = FOnClicked(), FOnClicked OnArchiveClicked = FOnClicked()) const;

	TSharedRef<SWidget> BuildConversationDetail();

	TSharedRef<SWidget> BuildConversationMessagesView();

	TSharedRef<SWidget> BuildConversationMessageWidget(const FConversationMessage& Message, int32 MessageIndex,
		const TSharedPtr<FUnrealAgentMCPTextSelectionGroup>& SelectionGroup = nullptr);

	TSharedRef<SWidget> BuildConversationToolGroupWidget(int32 FirstMessageIndex, int32 LastMessageIndex, const TSharedPtr<FUnrealAgentMCPTextSelectionGroup>& SelectionGroup);

	FString GetConversationToolGroupKey(int32 FirstMessageIndex) const;

	FReply OnToggleConversationToolGroupClicked(FString GroupKey, bool bDefaultExpanded);

	TSharedRef<SWidget> BuildConversationAttachmentChip(const FWorldDataConversationAttachment& Attachment) const;

	FText GetConversationRoleLabel(EConversationMessageRole Role) const;

	FLinearColor GetConversationMessageBackgroundColor(EConversationMessageRole Role) const;

	FLinearColor GetConversationMessageBorderColor(EConversationMessageRole Role) const;

	FLinearColor GetConversationMessageTextColor(EConversationMessageRole Role) const;

	FLinearColor GetConversationMessageMutedColor(EConversationMessageRole Role) const;

	TSharedRef<SWidget> BuildPermissionRequestCard();

	TSharedRef<SWidget> BuildPermissionActionButton(const FText& Label, bool bPrimary, const FOnClicked& OnClicked) const;

	TSharedRef<SWidget> BuildProviderDependencyCard();

	FText GetProviderDependencyTitle() const;

	FText GetProviderDependencyDescription() const;

	FText GetProviderDependencyPrimaryActionText() const;

	bool IsProviderDependencyAvailable() const;

	void RefreshProviderDependencyState();

	void StartProviderDependencyInstallProcess();

	void ApplyProviderDependencyInstallOutput(const FString& Output);

	void CompleteProviderDependencyInstallProcess(int32 ReturnCode, bool bCanceled);

	FReply OnProviderDependencyPrimaryClicked();

	FReply OnRefreshProviderDependencyClicked();

	TSharedRef<SWidget> BuildSettingsPanel();

	TSharedRef<SWidget> BuildSettingsContent();

	TSharedRef<SWidget> BuildMcpServerPanel();

	TSharedRef<SWidget> BuildContextSettingsCard();

	bool IsNativeUnrealAgentSelected() const;
	/** 把内存中残留的 Native 控制者落到 Codex/Cursor，发送与读档后必须调用。 */
	void SanitizeRetiredNativeController();

	TSharedRef<SWidget> BuildServerStatusCard();

	TSharedRef<SWidget> BuildServerClientsBanner();

	TSharedRef<SWidget> BuildServerPortCard();

	TSharedRef<SWidget> BuildRegisteredToolsCard();

	TSharedRef<SWidget> BuildToolChip(const FString& ToolName);

	FText GetServerStatusText() const;

	FText GetServerToggleText() const;

	int32 ParseServerPort() const;
	void VerifyServerReadinessForPanel(FText SuccessMessage, bool bConnectAcpAfterSuccess = false);

	const TArray<FString>& GetRegisteredToolNames();

	FReply OnToggleServerClicked();

	FReply OnApplyPortClicked();

	TSharedRef<SWidget> BuildCliSettingsPanel();

	TSharedRef<SWidget> BuildCodexAccountCard();

	TSharedRef<SWidget> BuildCliSettingsRow(ECliTool Tool);

	TSharedRef<SWidget> BuildCliStatusBadge(ECliTool Tool) const;

	TSharedRef<SWidget> BuildComposer();

	float GetActiveContextUsageRatio() const;

	FSlateColor GetContextUsageRingColor() const;

	FText GetContextUsageToolTipText() const;

	TSharedRef<SWidget> BuildQueuedPromptWidget(const FWorldDataQueuedPrompt& Prompt, int32 QueueIndex);
	void RebuildComposerQueue();

	TSharedRef<SWidget> BuildAttachmentChip(const FString& AttachmentPath);

	TSharedRef<SWidget> BuildInlineAttachmentChip(const FString& AttachmentId, const FString& AttachmentPath, bool bDirectory);

	TSharedRef<FSlateTextLayout> CreateComposerTextLayout(SWidget* OwningWidget, const FTextBlockStyle& DefaultTextStyle);

	void ConfigureLightTextBoxStyle();

	void ConfigureComposerButtonStyle();

	FLinearColor GetAccentFillColor(float Alpha) const;

	FLinearColor GetAccentSurfaceColor() const;

	FLinearColor GetAccentControlColor() const;

	FLinearColor GetAccentBorderColor() const;

	FLinearColor GetAccentButtonColor() const;

	FLinearColor GetAccentButtonTextColor() const;

	FLinearColor GetReadableAccentTextColor() const;

	FLinearColor GetPanelSubduedTextColor() const;

	FLinearColor GetEffectiveAccentColor() const;

	static FLinearColor ResolveAccentColor(const FLinearColor& Color);

	static FLinearColor GetPanelBackgroundColor();

	static FLinearColor GetPanelSurfaceColor();

	static FLinearColor GetPanelBorderColor();

	static FLinearColor GetPanelTextColor();

	static FLinearColor GetPanelMutedTextColor();

	bool IsSettingsColorSelected(const FLinearColor& Color) const;

	TSharedRef<SWidget> BuildColorPresetButton(const FLinearColor& Color, const FText& Tooltip);

	FText GetCliTitle(ECliTool Tool) const;

	FText GetCliDescription(ECliTool Tool) const;

	FString GetCliCommandName(ECliTool Tool) const;

	FString GetCliConfiguredPath(ECliTool Tool) const;

	FString GetCliDetectedPath(ECliTool Tool) const;

	FString GetCliEffectivePath(ECliTool Tool) const;

	bool IsCliAvailable(ECliTool Tool) const;

	FText GetCliPathSummary(ECliTool Tool) const;

	void RefreshCliDetections();

	void SetCliConfiguredPath(ECliTool Tool, const FString& NewPath);

	FReply OnDetectCliClicked(ECliTool Tool);

	FReply OnClearCliClicked(ECliTool Tool);

	FReply OnDownloadCliClicked(ECliTool Tool);

	enum class ECodexAuthAction : uint8
	{
		Status,
		Login,
		Logout
	};

	void RefreshCodexAccountState();

	bool StartCodexAuthProcess(ECodexAuthAction Action);

	void HandleCodexAuthProcessCompleted(ECodexAuthAction Action, int32 ReturnCode, bool bCanceled);

	bool StartCursorAuthProcess(ECodexAuthAction Action);

	void HandleCursorAuthProcessOutput(ECodexAuthAction Action, const FString& Output);

	void HandleCursorAuthProcessCompleted(ECodexAuthAction Action, int32 ReturnCode, bool bCanceled);

	FReply OnCodexLoginClicked();

	FReply OnCodexLogoutClicked();

	FReply OnRefreshProviderAccountClicked();

	FReply OnCursorLoginClicked();

	FReply OnCursorLogoutClicked();

	void SetAccountActionFeedback(const FText& Feedback, bool bNotify = true);

	FText GetCodexAccountStatusText() const;

	void LoadSettings();

	void ApplySettingsColor(const FLinearColor& NewColor);

	void HandleContextCapacityChanged(float SliderValue);

	void CommitContextCapacityChange();

	FText GetContextCapacityText() const;

	FString BuildContextContinuitySnapshot() const;

	void SaveSettings() const;

	FText GetSettingsColorText() const;

	void OpenSettingsColorPicker();

	void HandleSettingsColorChanged(FLinearColor NewColor);

	FReply OnSettingsColorBlockClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent);

	FReply OnPickSettingsColorClicked();

	FReply OnResetSettingsColorClicked();

	FReply OnSettingsBackClicked();

	FReply OnDetailBackClicked();

	FReply OnProviderAccountClicked();

	FReply OnToggleSidebarClicked();

	FReply OnToggleArchivedConversationsClicked();

	FReply OnRestoreConversationClicked(int32 Index);

	void SetDetail(const FText& Title, const FString& Text);

	void SetLastAction(const FText& Text);

	void RebuildConversationMessages(bool bScrollToEnd = true);
	bool IsConversationNearTail() const;
	void OnConversationUserScrolled(float ScrollOffset);
	void ScheduleConversationTailScroll();
	EActiveTimerReturnType HandleDeferredConversationTailScroll(double CurrentTime, float DeltaTime);

	int32 AddConversationMessage(EConversationMessageRole Role, const FString& Text, bool bStreaming = false,
		TArray<FWorldDataConversationAttachment> Attachments = TArray<FWorldDataConversationAttachment>());

	static FString TrimConversationEventText(const FString& Text);

	bool TryExtractConversationEvent(const FString& Text, EConversationMessageRole& OutRole, FString& OutText) const;

	void StartConversationTurn(const FString& UserMessage, const TArray<FWorldDataConversationAttachment>& Attachments);

	void AppendAssistantText(const FString& Text);

	void AppendConversationEvent(EConversationMessageRole Role, const FString& Text);

	void AppendConversationText(const FString& Text);

	void CompleteActiveAssistantMessage(bool bFailed, const FDateTime& CompletedAt);

	void RefreshConversationText();
	void ScheduleAssistantTextRefresh();
	EActiveTimerReturnType HandleDeferredAssistantTextRefresh(double CurrentTime, float DeltaTime);
	void ScheduleConversationSave();
	EActiveTimerReturnType HandleDeferredConversationSave(double CurrentTime, float DeltaTime);
	void ScheduleToolActivityRefresh();
	EActiveTimerReturnType HandleDeferredToolActivityRefresh(double CurrentTime, float DeltaTime);

	void HandleAcpText(const FString& Text);

	void HandleAcpPermission(const FUnrealAgentAcpPermissionRequest& Request);

	void HandleAcpStatus(const FString& Text);

	void HandleAcpTurnStatus(const FWorldDataCodexTurnStatus& Status);

	void HandleAcpToolCall(const FUnrealAgentAcpToolCallUpdate& Update);

	void HandleAcpError(const FString& Text);

	/** 返回 Provider 独占的常驻 ACP 客户端；AcpClient 只是当前 UI 的别名。 */
	TSharedPtr<FUnrealAgentCodexACPClient> GetAcpClientForProvider(EUnrealAgentACPProvider Provider) const;

	/** 初始化并绑定一个 Provider 客户端，回调只影响当前选中的 Provider。 */
	void ConfigureProviderAcpClient(EUnrealAgentACPProvider Provider, const TSharedPtr<FUnrealAgentCodexACPClient>& Client, const FGuid& ConversationId);

	/** 面板打开后并行保持 Codex/Cursor 适配器进程和空 session 为热状态。 */
	void PrewarmProviderAcpClient(EUnrealAgentACPProvider Provider);
	void ScheduleDeferredConversationStartup(const FGuid& ConversationId);
	EActiveTimerReturnType HandleDeferredConversationStartup(double CurrentTime, float DeltaTime);
	void PlayContentTransition();
	/** 判断当前对话是否仍有 Kernel 或 ACP 回合运行；后台对话不会阻止切换。 */
	bool IsActiveConversationControllerBusy() const;
	bool IsConversationRunning(const FGuid& ConversationId) const;
	bool HasConversationPendingWork(const FGuid& ConversationId) const;
	int32 FindConversationIndex(const FGuid& ConversationId) const;
	TSharedPtr<FUnrealAgentCodexACPClient> GetOrCreateConversationAcpClient(const FGuid& ConversationId, EUnrealAgentACPProvider Provider);
	void ActivateConversationAcpClients(const FGuid& ConversationId, bool bRefreshDependencyState = true);
	void StopAllConversationAcpClients();
	void ResetConversationAcpRuntimeForHistoryRewrite(const FGuid& ConversationId);
	bool IsProviderCallbackVisible(const FGuid& ConversationId) const;

	void HandleProviderAcpText(const FString& Text, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpStatus(const FString& Text, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpTurnStatus(const FWorldDataCodexTurnStatus& Status, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpToolCall(const FUnrealAgentAcpToolCallUpdate& Update, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpError(const FString& Text, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpPermission(const FUnrealAgentAcpPermissionRequest& Request, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void HandleProviderAcpConfigOptions(const TArray<FUnrealAgentAcpConfigOption>& Options, FGuid ConversationId, EUnrealAgentACPProvider Provider);
	void ApplyAcpTextToConversation(int32 ConversationIndex, const FString& Text, TOptional<EUnrealAgentACPProvider> Provider);
	void ApplyAcpToolCallToConversation(int32 ConversationIndex, const FUnrealAgentAcpToolCallUpdate& Update);
	void ApplyAcpTurnStatusToConversation(int32 ConversationIndex, const FWorldDataCodexTurnStatus& Status, TOptional<EUnrealAgentACPProvider> Provider);
	void ApplyAcpErrorToConversation(int32 ConversationIndex, const FString& Text, TOptional<EUnrealAgentACPProvider> Provider);
	void SyncVisibleConversationFromModel(bool bScrollToEnd = true);
	void ScheduleQueuedPromptDispatch(const FGuid& ConversationId);
	void TryDispatchNextQueuedPrompt(const FGuid& ConversationId);
	bool DispatchPrompt(int32 ConversationIndex, const FWorldDataQueuedPrompt& Prompt);
	void ApplyPanelNavigation(EUnrealAgentMCPPanelNavigationEvent Event);
	void CancelPromptMediaLoad(const FGuid& ConversationId, bool bRequeuePrompt);
	void CompletePromptMediaLoad(FGuid ConversationId, int32 Generation, FWorldDataQueuedPrompt Prompt, bool bSucceeded, FUnrealAgentPreparedPromptMedia Media, FString Error);
	bool FinishDispatchPreparedPrompt(int32 ConversationIndex, const FWorldDataQueuedPrompt& Prompt, FUnrealAgentPreparedPromptMedia PreparedMedia);
	FWorldDataQueuedPrompt BuildQueuedPrompt(const FString& Message, const TArray<FWorldDataConversationAttachment>& Attachments) const;
	FReply OnEditQueuedPromptClicked(FGuid PromptId);
	FReply OnDeleteQueuedPromptClicked(FGuid PromptId);
	FReply OnGuideQueuedPromptClicked(FGuid PromptId);
	FReply OnMoveQueuedPromptClicked(FGuid PromptId, int32 Direction);

	FReply OnAllowPermissionClicked();

	FReply OnDenyPermissionClicked();

	FReply ResolvePendingPermission(bool bAllow);

	void ClearPendingPermission();

	void ResetConversationView();

	void SaveActiveConversation();

	void LoadConversationHistory();

	void PersistConversationHistory(bool bSynchronous = false);

	void LoadConversation(int32 Index);

	FReply OnSelectConversation(int32 Index);

	FReply OnCopyAssistantMessageClicked(FString MarkdownText);

	FReply OnBranchConversationClicked(int32 AssistantMessageIndex);

	FReply OnCopyUserMessageClicked(int32 UserMessageIndex);

	FReply OnEditUserMessageClicked(int32 UserMessageIndex);

	FReply OnCancelUserMessageEditClicked();

	FReply OnDeleteUserMessageClicked(int32 UserMessageIndex);

	FReply OnUserMessageRightClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent, int32 UserMessageIndex);

	bool CanEditUserMessage(int32 UserMessageIndex) const;

	bool CanModifyUserMessage(int32 UserMessageIndex) const;

	bool RemoveConversationFromUserMessage(int32 UserMessageIndex);

	FText GetConversationTitle(int32 Index) const;

	FText MakeConversationTitle(const FString& Message) const;

	FText GetConversationAgeText(FDateTime CreatedAt) const;

	TSharedRef<SWidget> BuildConversationEntry(int32 Index);

	void RebuildSidebar();

	void ShowProjectInfo();

	FReply OnNewConversationClicked();

	FReply OnArchiveConversationClicked();

	FReply OnArchiveConversationEntryClicked(int32 Index);

	bool CanArchiveConversation() const;

	bool CanArchiveConversationEntry(int32 Index) const;

	int32 GetArchivedConversationCount() const;

	FReply OnSettingsClicked();

	FReply OnSendClicked();

	void OnComposerTextCommitted(const FText& Text, ETextCommit::Type CommitType);

	void OnComposerTextChanged(const FText& Text);

	FReply OnComposerKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	FReply HandledReplyWithComposerFocus();

	bool CanSendMessage() const;

	FReply OnStartClicked();

	FReply OnStopClicked();

	FReply OnRefreshClicked();

	FReply OnSetupCliClicked();

	FReply OnStatusClicked();

	FReply OnProjectInfoClicked();

	FReply OnBootstrapClicked();

	FReply OnPolicyClicked();

	FReply OnToolsClicked();

	FReply OnResourcesClicked();

	FReply OnCopyUrlClicked();

	FReply OnCopyConfigClicked();

	FReply OnCopyCurrentClicked();

	TSharedRef<SWidget> BuildAttachmentCombo();

	TSharedRef<SWidget> BuildAttachmentMenu();

	FReply OnAttachFilesClicked();

	FReply OnAttachFolderClicked();

	int32 AddPendingAttachmentPaths(const TArray<FString>& CandidatePaths, bool* bOutRejectedByMediaLimit = nullptr);

	int32 InsertPendingInlineAttachmentPaths(const TArray<FString>& CandidatePaths, bool* bOutRejectedByMediaLimit);

	bool CanAcceptAttachmentDrop(TSharedPtr<FDragDropOperation> DragDropOperation) const;

	FReply OnAttachmentPathsDropped(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent);

	void OnAttachmentDragEntered(const FDragDropEvent& DragDropEvent);

	void OnAttachmentDragLeft(const FDragDropEvent& DragDropEvent);

	FReply OnRemoveAttachmentClicked(FString AttachmentPath);

	FReply OnRemoveInlineAttachmentClicked(FString AttachmentId);

	void RebuildComposerAttachments();

	void ClearPendingAttachments();

	FReply OnOpenProjectFolderClicked();

	FReply OnOpenSavedFolderClicked();

	/** 最近一次操作状态文案（顶栏/状态区）。 */
	FText LastAction;
	/** 详情面板当前正文。 */
	FString CurrentDetailText;
	/** 当前会话 transcript 文本。 */
	FString ConversationTranscript;
	/** 当前会话消息列表（渲染用镜像）。 */
	TArray<FConversationMessage> ConversationMessages;
	/** 已构建的助手正文控件，用于流式增量更新。 */
	TMap<FGuid, TWeakPtr<SUnrealAgentMCPMarkdown>> ConversationMessageMarkdownWidgets;
	/** 当前 ACP session 尚未接收已加载会话的历史上下文。 */
	bool bReplayConversationContextOnNextPrompt = false;
	/** 用户对工具分组执行的手动展开/收起覆盖；键在会话内稳定。 */
	TMap<FString, bool> ToolGroupExpansionOverrides;
	int32 EditingUserMessageIndex = INDEX_NONE;
	bool bEditingUserMessageBranchRemoved = false;
	/** 设置页主题色。 */
	FLinearColor SettingsColor = FLinearColor::White;
	/** 全局上下文自动压缩容量；设置页为唯一用户配置入口。 */
	int32 CurrentContextTokenCapacity = UnrealAgentMCPConversationModel::DefaultContextTokenCapacity;
	/** 用户配置的 Codex CLI 路径。 */
	FString CodexCliPath;
	/** 用户配置的 Cursor CLI 路径。 */
	FString CursorCliPath;
	/** 自动检测到的 Codex CLI 路径。 */
	FString DetectedCodexCliPath;
	/** 自动检测到的 Cursor CLI 路径。 */
	FString DetectedCursorCliPath;
	/** 自动部署项目本地 Codex 时优先复用的已检测 npm 路径。 */
	FString DetectedNpmPath;
	/** 最近一次依赖操作向用户展示的反馈信息。 */
	FText ProviderDependencyFeedback;
	/** 当前 Provider 的 ACP Agent 是否可直接启动。 */
	bool bProviderDependencyAvailable = false;
	/** 为失败诊断保留的近期 Provider 安装器输出。 */
	FString ProviderDependencyInstallProcessOutput;
	/** 当前安装任务启动时锁定的 Provider。 */
	EUnrealAgentACPProvider InstallingProvider = EUnrealAgentACPProvider::Codex;
	/** 当前 Provider 及其本机账户显示名。 */
	EUnrealAgentACPProvider ActiveProvider = EUnrealAgentACPProvider::Codex;
	FString DetectedAccountLabel;
	FString DetectedAccountSecondaryLabel;
	bool bCodexAuthenticated = false;
	bool bCursorAuthenticated = false;
	FText AccountActionFeedback;
	TSharedPtr<IUnrealAgentMCPConversationRepository> ConversationRepository;
	FString CodexAuthProcessOutput;
	FString CursorAuthProcessOutput;
	FString AuthProcessStartError;
	FString LastOpenedCursorLoginUrl;
	ECodexAuthAction ActiveCodexAuthAction = ECodexAuthAction::Status;
	ECodexAuthAction ActiveCursorAuthAction = ECodexAuthAction::Status;
	TSharedPtr<FInteractiveProcess> CodexAuthProcess;
	TSharedPtr<FInteractiveProcess> CursorAuthProcess;
	TSharedPtr<FInteractiveProcess> ProviderDependencyInstallProcess;
	/** ACP Agent 动态公布的模型、模式和推理配置。 */
	TArray<FUnrealAgentAcpConfigOption> AcpConfigOptions;
	/** Provider 级模型目录快照；新对话后台建连期间继续显示，避免选择框短暂变黑。 */
	TMap<EUnrealAgentACPProvider, TArray<FUnrealAgentAcpConfigOption>> ProviderConfigCache;
	TMap<EUnrealAgentACPProvider, TMap<FString, FString>> ProviderDesiredConfigValues;
	FEditableTextBoxStyle LightTextBoxStyle;
	FButtonStyle ComposerButtonStyle;
	FButtonStyle ToolbarButtonStyle;
	FComboButtonStyle ComposerComboButtonStyle;
	/** Codex 权限/执行模式。 */
	EWorldDataAgentMode CurrentAgentMode = EWorldDataAgentMode::Agent;
	EWorldDataApprovalPolicy CurrentApprovalPolicy = EWorldDataApprovalPolicy::AlwaysAsk;
	EWorldDataSelfRepairPolicy CurrentSelfRepairPolicy = EWorldDataSelfRepairPolicy::Off;
	/** Codex/Cursor 各自持有常驻进程与 session，切换时不再互相销毁。 */
	TSharedPtr<FUnrealAgentCodexACPClient> CodexAcpClient;
	TSharedPtr<FUnrealAgentCodexACPClient> CursorAcpClient;
	/** 当前 Provider 客户端的 UI 别名。 */
	TSharedPtr<FUnrealAgentCodexACPClient> AcpClient;
	/** 控制台唯一应用入口；Presentation 不直接访问 HTTP Server。 */
	TSharedPtr<IUnrealAgentMCPApplicationService> ApplicationService;
	/** 是否显示详情面板。 */
	bool bShowDetail = false;
	/** 是否显示设置页。 */
	bool bShowSettings = false;
	/** 是否显示任务面板。 */
	bool bShowTaskPanel = false;
	/** 详情区是否在展示对话内容。 */
	bool bDetailIsConversation = false;

	/** 当前任务控制者；新安装默认 Codex ACP，不再进入 Native Kernel。 */
	EUnrealAgentControllerMode ControllerMode = EUnrealAgentControllerMode::ExternalCodexAgent;
	/** 代理卡片未能切换时显示在选择菜单内的即时反馈。 */
	FText ControllerSelectionFeedback;
	FString DirectProviderId;
	FString BudgetTierId = TEXT("standard");
	bool bMultiAgentEnabled = false;
	bool bMemoryEnabled = true;
	/** 是否隐藏会话导航栏。 */
	bool bSidebarCollapsed = false;
	/** 使用快速且可中断的过渡，确保稳定背景始终可见。 */
	FCurveSequence PanelIntroAnimation;
	FCurveSequence ContentTransitionAnimation;
	FCurveSequence SidebarTransitionAnimation;
	TSharedPtr<SBorder> PanelAnimationRoot;
	TSharedPtr<SBorder> ContentAnimationRoot;
	TSharedPtr<SBox> SidebarAnimationBox;
	TSharedPtr<SSeparator> SidebarAnimationSeparator;
	/** 快速导航重复调度启动时，始终以最新会话为准。 */
	FGuid DeferredStartupConversationId;
	int32 DeferredConversationStartupStage = INDEX_NONE;
	TWeakPtr<FActiveTimerHandle> DeferredConversationStartupTimer;
	bool bDeferredCliRefreshPending = false;
	bool bDeferredAccountRefreshPending = false;
	/** 高频工具事件合并到一次 Slate 重建，避免反复构造整棵消息控件树。 */
	TWeakPtr<FActiveTimerHandle> DeferredToolActivityRefreshTimer;
	bool bDeferredToolActivityRefreshPending = false;
	/** 合并高频文本 delta，避免每个 token 重新解析 Markdown。 */
	TWeakPtr<FActiveTimerHandle> DeferredAssistantTextRefreshTimer;
	bool bDeferredAssistantTextRefreshPending = false;
	TWeakPtr<FActiveTimerHandle> DeferredConversationTailScrollTimer;
	bool bConversationTailFollowEnabled = true;
	/** 限制流式回复的持久化频率，避免大会话副本排队。 */
	TWeakPtr<FActiveTimerHandle> DeferredConversationSaveTimer;
	bool bDeferredConversationSavePending = false;
	bool bPanelSessionShuttingDown = false;
	/** 侧边栏列表是否包含已归档会话。 */
	bool bShowArchivedConversations = false;
	/** 是否有待确认权限。 */
	bool bHasPendingPermission = false;
	/** 当前流式助手消息下标。 */
	int32 ActiveAssistantMessageIndex = INDEX_NONE;
	/** 当前回合独立状态节点下标。 */
	int32 ActiveTurnStatusMessageIndex = INDEX_NONE;
	/** 待确认权限本地 ID。 */
	int32 PendingPermissionId = 0;
	/** 权限可能来自切换前仍在完成回复的 Provider。 */
	TOptional<EUnrealAgentACPProvider> PendingPermissionProvider;
	FString PendingPermissionTitle;
	FString PendingPermissionToolName;
	FString PendingAllowOptionId;
	FString PendingDenyOptionId;
	/** 处理旧 Provider 的可见回调时，用它生成正确的角色标签。 */
	TOptional<EUnrealAgentACPProvider> CallbackProviderOverride;
	/** 主内容区切换器（对话/详情/设置）。 */
	TSharedPtr<SWidgetSwitcher> ContentSwitcher;
	TSharedPtr<SScrollBox> ConversationScrollBox;
	TSharedPtr<SScrollBox> SidebarListScrollBox;
	/** 全部会话。 */
	TArray<FConversation> Conversations;
	struct FConversationAcpRuntime
	{
		TSharedPtr<FUnrealAgentCodexACPClient> CodexClient;
		TSharedPtr<FUnrealAgentCodexACPClient> CursorClient;
		TOptional<FUnrealAgentAcpPermissionRequest> PendingPermission;
		TOptional<EUnrealAgentACPProvider> PendingPermissionProvider;
		TSharedPtr<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe> MediaLoadCancellation;
		TOptional<FWorldDataQueuedPrompt> PendingMediaPrompt;
		int32 MediaLoadGeneration = 0;
		bool bCodexContextReplayPending = true;
		bool bCursorContextReplayPending = true;
	};
	/** 每个对话各自拥有 Provider 客户端，允许同一代理跨对话并发执行。 */
	TMap<FGuid, TSharedPtr<FConversationAcpRuntime>> ConversationAcpRuntimes;
	/** 当前激活会话下标。 */
	int32 ActiveConversationIndex = INDEX_NONE;
	/** MCP 端口输入文本。 */
	FString ServerPortText;
	/** 缓存的工具名列表。 */
	TArray<FString> CachedToolNames;
	TSharedPtr<STextBlock> DetailTitleText;
	TSharedPtr<SMultiLineEditableTextBox> DetailTextBox;
	/** 支持精确 IME 组合态判断的会话输入控件。 */
	TSharedPtr<SUnrealAgentMCPComposerEditableText> ComposerTextBox;
	TSharedPtr<FSlateTextLayout> ComposerTextLayout;
	/** 当前输入消息中尚未发送的本地附件绝对路径。 */
	TArray<FString> PendingAttachmentPaths;
	/** 输入框上方的附件标签容器。 */
	TSharedPtr<SWrapBox> ComposerAttachmentsBox;
	/** 输入框上方的当前对话待执行队列。 */
	TSharedPtr<SVerticalBox> ComposerQueueBox;
	FGuid EditingQueuedPromptId;
	/** 文件或文件夹正在悬停于输入区时显示拖放提示。 */
	bool bAttachmentDragOver = false;
	/** 输入区独立的纵向滚动条。 */
	TSharedPtr<SScrollBar> ComposerScrollBar;
	/** 最近一次已提交的文本变更时间，用于区分 IME 候选词确认与发送 Enter。 */
	double LastComposerTextChangeSeconds = -1.0;
};
