// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPPanelConversationLayout.cpp
 * @brief 对话消息、权限确认与输入区 Slate 布局。
 */

#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Presentation/Panel/SUnrealAgentMCPPanelPrivate.h"
#include "Presentation/Widgets/SUnrealAgentMCPContextUsageRing.h"
#include "Presentation/Widgets/SUnrealAgentMCPMarkdown.h"
#include "Presentation/Widgets/UnrealAgentMCPToolTip.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/TextDecorators.h"
#include "InputCoreTypes.h"
#include "Internationalization/BreakIterator.h"
#include "Misc/Base64.h"
#include "SDropTarget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace
{
	class SWorldDataUserMessageInteraction final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWorldDataUserMessageInteraction)
		{
		}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FPointerEventHandler, OnRightClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			RightClickedHandler = InArgs._OnRightClicked;
			ChildSlot[InArgs._Content.Widget];
		}

		virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && RightClickedHandler.IsBound())
			{
				return RightClickedHandler.Execute(Geometry, MouseEvent);
			}
			return FReply::Unhandled();
		}

	private:
		FPointerEventHandler RightClickedHandler;
	};

	const FSlateBrush* GetUserBubbleOuterBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 12.0f);
		return &Brush;
	}

	const FSlateBrush* GetUserBubbleInnerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 11.0f);
		return &Brush;
	}

	const FSlateBrush* GetAttachmentChipOuterBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 8.0f);
		return &Brush;
	}

	const FSlateBrush* GetAttachmentChipInnerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.0f);
		return &Brush;
	}

	const FSlateBrush* GetMessageCardOuterBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 9.0f);
		return &Brush;
	}

	const FSlateBrush* GetMessageCardInnerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 8.0f);
		return &Brush;
	}

	const FSlateBrush* GetComposerOuterBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 13.0f);
		return &Brush;
	}

	const FSlateBrush* GetComposerInnerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 12.0f);
		return &Brush;
	}
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationDetail()
{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]
				{
					return bDetailIsConversation
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(DetailTitleText, STextBlock)
						.Text(LOCTEXT("InitialDetailTitle", "项目信息"))
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]
						{
							return bDetailIsConversation
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
						[
							BuildToolbarButton(
								LOCTEXT("DetailBackButton", "← 返回对话"),
								FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnDetailBackClicked))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						BuildToolbarButton(
							LOCTEXT("CopyCurrentButton", "复制当前内容"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnCopyCurrentClicked))
					]
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ContentSwitcher, SWidgetSwitcher)
				.WidgetIndex_Lambda([this] { return bDetailIsConversation ? 0 : 1; })
				+ SWidgetSwitcher::Slot()
				[
					BuildConversationMessagesView()
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(DetailTextBox, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.AutoWrapText(false)
					.Style(&LightTextBoxStyle)
					.ForegroundColor(FSlateColor(GetPanelTextColor()))
					.ReadOnlyForegroundColor(FSlateColor(GetPanelTextColor()))
					.Font(FAppStyle::GetFontStyle("MonospacedText"))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]
				{
					return bHasPendingPermission
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					BuildPermissionRequestCard()
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationMessagesView()
{
		return SNew(SBorder)
			.Padding(FMargin(36.0f, 14.0f, 36.0f, 18.0f))
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SAssignNew(ConversationScrollBox, SScrollBox)
				.OnUserScrolled(
					this,
					&SUnrealAgentMCPPanel::OnConversationUserScrolled)
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationToolGroupWidget(const int32 FirstMessageIndex, const int32 LastMessageIndex,
	const TSharedPtr<FUnrealAgentMCPTextSelectionGroup>& SelectionGroup)
	{
		const int32 ToolCount =
			LastMessageIndex - FirstMessageIndex + 1;
		int32 RunningCount = 0;
		int32 CompletedCount = 0;
		int32 FailedCount = 0;
		for (int32 Index = FirstMessageIndex;
			Index <= LastMessageIndex;
			++Index)
		{
			const FConversationMessage& ToolMessage =
				ConversationMessages[Index];
			switch (ToolMessage.ToolState)
			{
			case EWorldDataConversationToolState::Completed:
				++CompletedCount;
				break;
			case EWorldDataConversationToolState::Failed:
				++FailedCount;
				break;
			case EWorldDataConversationToolState::Running:
				++RunningCount;
				break;
			case EWorldDataConversationToolState::None:
			default:
				if (ToolMessage.bFailed)
				{
					++FailedCount;
				}
				else if (ToolMessage.bCompleted)
				{
					++CompletedCount;
				}
				else if (ToolMessage.bStreaming)
				{
					++RunningCount;
				}
				break;
			}
		}

		const bool bDefaultExpanded =
			!UnrealAgentMCPConversationModel::ShouldAutoCollapseToolGroup(
				ConversationMessages,
				FirstMessageIndex);
		const FString GroupKey =
			GetConversationToolGroupKey(FirstMessageIndex);
		const bool* ExpansionOverride =
			ToolGroupExpansionOverrides.Find(GroupKey);
		const bool bExpanded = ExpansionOverride
			? *ExpansionOverride
			: bDefaultExpanded;
		TSharedRef<SVerticalBox> ToolMessagesBox = SNew(SVerticalBox);
		if (bExpanded)
		{
			for (int32 Index = FirstMessageIndex;
				Index <= LastMessageIndex;
				++Index)
			{
				ToolMessagesBox->AddSlot()
					.AutoHeight()
					.Padding(
						0.0f,
						0.0f,
						0.0f,
						Index < LastMessageIndex ? 7.0f : 0.0f)
					[
						BuildConversationMessageWidget(
							ConversationMessages[Index],
							Index,
							SelectionGroup)
					];
			}
		}

		FString Summary;
		if (FailedCount > 0)
		{
			Summary = FString::Printf(
				TEXT("%d 个失败 · %d 个完成"),
				FailedCount,
				CompletedCount);
		}
		else if (RunningCount > 0)
		{
			Summary = FString::Printf(
				TEXT("%d 个进行中 · %d 个完成"),
				RunningCount,
				CompletedCount);
		}
		else if (CompletedCount > 0)
		{
			Summary = FString::Printf(
				TEXT("%d 个已完成"),
				CompletedCount);
		}
		else
		{
			Summary = FString::Printf(TEXT("%d 条记录"), ToolCount);
		}

		return SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetMessageCardOuterBrush())
			.BorderBackgroundColor(FSlateColor(
				FailedCount > 0
					? UnrealAgentMCP::Palette::Danger()
					: GetAccentBorderColor()))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f))
				.BorderImage(GetMessageCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetPanelSurfaceColor()))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(9.0f, 5.0f))
						.IsFocusable(false)
						.ToolTip(UnrealAgentMCPToolTip::Make(bExpanded
							? LOCTEXT(
								"CollapseToolGroupTooltip",
								"收起本轮工具调用")
							: LOCTEXT(
								"ExpandToolGroupTooltip",
								"展开本轮工具调用"))
						)
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnToggleConversationToolGroupClicked,
							GroupKey,
							bDefaultExpanded))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 7.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(
									bExpanded ? TEXT("▼") : TEXT("▶")))
								.ColorAndOpacity(FSlateColor(
									GetReadableAccentTextColor()))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SUnrealAgentMCPMarkdown)
								.Markdown(FString::Printf(
									TEXT("**工具调用（%d）**"),
									ToolCount))
								.SelectionGroup(SelectionGroup)
								.TextColor(GetPanelTextColor())
								.MutedColor(GetPanelMutedTextColor())
								.AccentColor(GetEffectiveAccentColor())
								.SurfaceColor(GetPanelSurfaceColor())
								.BorderColor(GetPanelBorderColor())
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								SNew(SUnrealAgentMCPMarkdown)
								.Markdown(Summary)
								.SelectionGroup(SelectionGroup)
								.TextColor(FailedCount > 0
									? UnrealAgentMCP::Palette::Danger()
									: GetPanelMutedTextColor())
								.MutedColor(GetPanelMutedTextColor())
								.AccentColor(GetEffectiveAccentColor())
								.SurfaceColor(GetPanelSurfaceColor())
								.BorderColor(GetPanelBorderColor())
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility(bExpanded
							? EVisibility::Visible
							: EVisibility::Collapsed)
						[
							ToolMessagesBox
						]
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildQueuedPromptWidget(const FWorldDataQueuedPrompt& Prompt, const int32 QueueIndex)
	{
		FString Summary = Prompt.Text;
		Summary.ReplaceInline(TEXT("\r"), TEXT(" "));
		Summary.ReplaceInline(TEXT("\n"), TEXT(" "));
		Summary.TrimStartAndEndInline();
		if (Summary.IsEmpty())
		{
			Summary = TEXT("附件任务");
		}
		const FString ProviderLabel = Prompt.ProviderId.Equals(
			TEXT("cursor"), ESearchCase::IgnoreCase)
			? TEXT("Cursor")
			: TEXT("Codex");
		FString ConfigLabel = ProviderLabel;
		if (!Prompt.ModelId.IsEmpty())
		{
			ConfigLabel += TEXT(" · ") + Prompt.ModelId;
		}

		return SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetMessageCardOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetAccentBorderColor()))
			[
				SNew(SBorder)
				.Padding(FMargin(9.0f, 6.0f))
				.BorderImage(GetMessageCardInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetPanelBackgroundColor()))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::Format(
							LOCTEXT("QueuedPromptOrdinal", "队列 {0}"),
							FText::AsNumber(QueueIndex + 1)))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity(FSlateColor(GetReadableAccentTextColor()))
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
							.Text(FText::FromString(Summary))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(ConfigLabel))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(5.0f, 2.0f))
						.ToolTip(UnrealAgentMCPToolTip::Make(
							LOCTEXT("GuideQueuedPromptTooltip", "停止当前回合，并优先发送这条引导")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnGuideQueuedPromptClicked,
							Prompt.Id))
						[
							SNew(STextBlock).Text(LOCTEXT("GuideQueuedPrompt", "引导"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildIconTextButton(
							LOCTEXT("MoveQueuedPromptUp", "↑"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnMoveQueuedPromptClicked, Prompt.Id, -1),
							LOCTEXT("MoveQueuedPromptUpTooltip", "提前一位"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildIconTextButton(
							LOCTEXT("EditQueuedPrompt", "…"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnEditQueuedPromptClicked, Prompt.Id),
							LOCTEXT("EditQueuedPromptTooltip", "编辑这条待执行消息"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildIconTextButton(
							LOCTEXT("DeleteQueuedPrompt", "×"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnDeleteQueuedPromptClicked, Prompt.Id),
							LOCTEXT("DeleteQueuedPromptTooltip", "撤回这条待执行消息"))
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationMessageWidget(const FConversationMessage& Message, const int32 MessageIndex,
	const TSharedPtr<FUnrealAgentMCPTextSelectionGroup>& SelectionGroup)
{
		const bool bUser = Message.Role == EConversationMessageRole::User;
		const FString DisplayText = Message.Text.IsEmpty() && Message.bStreaming
			? FString(TEXT("正在思考..."))
			: Message.Text;

		if (Message.Role == EConversationMessageRole::Status)
		{
			FString StatusText = DisplayText;
			if (Message.bCompleted || Message.bFailed)
			{
				const FDateTime EndTime =
					Message.CompletedAt == FDateTime()
					? FDateTime::Now()
					: Message.CompletedAt;
				const int32 TotalSeconds = FMath::Max(
					0,
					static_cast<int32>(
						(EndTime - Message.CreatedAt)
							.GetTotalSeconds()));
				StatusText += TotalSeconds >= 60
					? FString::Printf(
						TEXT("  %dm %ds"),
						TotalSeconds / 60,
						TotalSeconds % 60)
					: FString::Printf(TEXT("  %ds"), TotalSeconds);
			}

			TSharedPtr<SUnrealAgentMCPMarkdown> StatusMarkdown;
			TSharedRef<SVerticalBox> StatusBox = SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 7.0f)
				[
					SAssignNew(StatusMarkdown, SUnrealAgentMCPMarkdown)
					.Markdown(StatusText)
					.SelectionGroup(SelectionGroup)
					.TextColor(Message.bFailed
						? UnrealAgentMCP::Palette::Danger()
						: GetPanelMutedTextColor())
					.MutedColor(GetPanelMutedTextColor())
					.AccentColor(GetEffectiveAccentColor())
					.SurfaceColor(GetPanelSurfaceColor())
					.BorderColor(GetPanelBorderColor())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(1.0f)
					[
						SNew(SBorder)
						.BorderImage(
							FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(
							FSlateColor(GetPanelBorderColor()))
					]
				];
			ConversationMessageMarkdownWidgets.Add(
				Message.Id,
				StatusMarkdown);
			return StatusBox;
		}

		if (bUser)
		{
			TSharedRef<SWrapBox> AttachmentsBox = SNew(SWrapBox)
				.UseAllottedSize(true);
			for (const FWorldDataConversationAttachment& Attachment :
				Message.Attachments)
			{
				AttachmentsBox->AddSlot()
					.Padding(FMargin(0.0f, 0.0f, 6.0f, 6.0f))
					[
						BuildConversationAttachmentChip(Attachment)
					];
			}

			TSharedPtr<SWorldDataUserMessageInteraction> InteractionWidget;
			TSharedRef<SVerticalBox> UserMessageBox = SNew(SVerticalBox);
			TSharedRef<SWidget> Interaction =
				SAssignNew(
					InteractionWidget,
					SWorldDataUserMessageInteraction)
				.OnRightClicked(FPointerEventHandler::CreateSP(
					this,
					&SUnrealAgentMCPPanel::OnUserMessageRightClicked,
					MessageIndex))
				[
					UserMessageBox
				];

			UserMessageBox->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SBox)
					.MaxDesiredWidth(520.0f)
					[
						SNew(SBorder)
						.Padding(1.0f)
						.BorderImage(GetUserBubbleOuterBrush())
						.BorderBackgroundColor(FSlateColor(
							EditingUserMessageIndex == MessageIndex
								? GetEffectiveAccentColor()
								: GetConversationMessageBorderColor(
									Message.Role)))
						[
							SNew(SBorder)
							.Padding(FMargin(13.0f, 9.0f))
							.BorderImage(GetUserBubbleInnerBrush())
							.BorderBackgroundColor(FSlateColor(
								GetConversationMessageBackgroundColor(
									Message.Role)))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SBox)
									.Visibility(DisplayText.IsEmpty()
										? EVisibility::Collapsed
										: EVisibility::Visible)
									[
										SNew(SUnrealAgentMCPMarkdown)
										.Markdown(DisplayText)
										.SelectionGroup(SelectionGroup)
										.TextColor(
											GetConversationMessageTextColor(
												Message.Role))
										.MutedColor(
											GetConversationMessageMutedColor(
												Message.Role))
										.AccentColor(GetEffectiveAccentColor())
										.SurfaceColor(GetPanelSurfaceColor())
										.BorderColor(GetPanelBorderColor())
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(
									0.0f,
									DisplayText.IsEmpty() ? 0.0f : 8.0f,
									0.0f,
									0.0f)
								[
									SNew(SBox)
									.Visibility(Message.Attachments.IsEmpty()
										? EVisibility::Collapsed
										: EVisibility::Visible)
									[
										AttachmentsBox
									]
								]
							]
						]
					]
				];

			UserMessageBox->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(0.0f, 4.0f, 2.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda(
						[WeakInteraction =
							TWeakPtr<SWorldDataUserMessageInteraction>(
								InteractionWidget)]
						{
							const TSharedPtr<SWorldDataUserMessageInteraction>
								PinnedInteraction = WeakInteraction.Pin();
							return PinnedInteraction.IsValid()
								&& PinnedInteraction->IsHovered()
									? EVisibility::Visible
									: EVisibility::Collapsed;
						})
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(5.0f, 3.0f))
						.IsFocusable(false)
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"CopyUserPromptTooltip",
							"复制这条提示词")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnCopyUserMessageClicked,
							MessageIndex))
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("GenericCommands.Copy"))
							.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(5.0f, 3.0f))
						.IsFocusable(false)
						.IsEnabled_Lambda([this, MessageIndex]
						{
							return CanEditUserMessage(MessageIndex);
						})
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"EditUserPromptTooltip",
							"中断当前回合，清除后续上下文并返回输入框编辑")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnEditUserMessageClicked,
							MessageIndex))
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Edit"))
							.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						]
					]
				];

			return SNew(SBox)
				.HAlign(HAlign_Right)
				[
					Interaction
				];
		}

		const bool bAssistant =
			Message.Role == EConversationMessageRole::Assistant;
		const EWorldDataConversationMessagePresentation Presentation =
			UnrealAgentMCPConversationModel::ResolveMessagePresentation(
				Message.Role);
		const bool bPlainText = Presentation
			== EWorldDataConversationMessagePresentation::PlainText;
		const bool bShowRoleLabel = bAssistant
			|| Presentation
				== EWorldDataConversationMessagePresentation::ToolCard
			|| Presentation
				== EWorldDataConversationMessagePresentation::ErrorCard;
		const bool bShowAssistantFooter =
			bAssistant
			&& (Message.bCompleted || Message.bFailed)
			&& Message.CompletedAt != FDateTime();
		TSharedPtr<SUnrealAgentMCPMarkdown> MessageMarkdown;
		SAssignNew(MessageMarkdown, SUnrealAgentMCPMarkdown)
			.Markdown(DisplayText)
			.SelectionGroup(SelectionGroup)
			.TextColor(GetConversationMessageTextColor(Message.Role))
			.MutedColor(GetConversationMessageMutedColor(Message.Role))
			.AccentColor(GetEffectiveAccentColor())
			.SurfaceColor(GetPanelSurfaceColor())
			.BorderColor(GetPanelBorderColor());
		if (bAssistant)
		{
			ConversationMessageMarkdownWidgets.Add(
				Message.Id,
				MessageMarkdown);
		}
		return SNew(SBox)
			.HAlign(HAlign_Fill)
			[
				SNew(SBorder)
				.Padding(bPlainText
					? FMargin(0.0f, 5.0f)
					: FMargin(1.0f))
				.BorderImage(bPlainText
					? FAppStyle::GetBrush("NoBorder")
					: GetMessageCardOuterBrush())
				.BorderBackgroundColor(FSlateColor(
					GetConversationMessageBorderColor(Message.Role)))
				[
					SNew(SBorder)
					.Padding(bPlainText
						? FMargin(0.0f)
						: FMargin(12.0f, 9.0f))
					.BorderImage(bPlainText
						? FAppStyle::GetBrush("NoBorder")
						: GetMessageCardInnerBrush())
					.BorderBackgroundColor(FSlateColor(
						GetConversationMessageBackgroundColor(
							Message.Role)))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.Visibility(bShowRoleLabel
								? EVisibility::Visible
								: EVisibility::Collapsed)
							[
								SNew(SUnrealAgentMCPMarkdown)
								.Markdown(FString::Printf(
									TEXT("**%s**"),
									*GetConversationRoleLabel(Message.Role).ToString()))
								.SelectionGroup(SelectionGroup)
								.TextColor(GetConversationMessageMutedColor(
									Message.Role))
								.MutedColor(GetConversationMessageMutedColor(
									Message.Role))
								.AccentColor(GetEffectiveAccentColor())
								.SurfaceColor(GetPanelSurfaceColor())
								.BorderColor(GetPanelBorderColor())
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(
							0.0f,
							bShowRoleLabel ? 5.0f : 0.0f,
							0.0f,
							0.0f)
						[
							MessageMarkdown.ToSharedRef()
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							.Visibility(bShowAssistantFooter
								? EVisibility::Visible
								: EVisibility::Collapsed)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(&ToolbarButtonStyle)
								.ContentPadding(FMargin(7.0f, 2.0f))
								.IsFocusable(false)
								.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
									"CopyAssistantReplyTooltip",
									"复制这条回复的原始 Markdown")))
								.OnClicked(FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnCopyAssistantMessageClicked,
									Message.Text))
								[
									SNew(STextBlock)
									.Text(LOCTEXT(
										"CopyAssistantReplyButton",
										"复制"))
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity(FSlateColor(
										GetPanelTextColor()))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(5.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&ToolbarButtonStyle)
								.ContentPadding(FMargin(7.0f, 2.0f))
								.IsFocusable(false)
								.IsEnabled_Lambda([this]
								{
									if (!Conversations.IsValidIndex(
										ActiveConversationIndex))
									{
										return false;
									}
									return !HasConversationPendingWork(
										Conversations[ActiveConversationIndex].Id);
								})
								.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
									"BranchAssistantReplyTooltip",
									"复制截至此回复的历史，并在独立会话中继续")))
								.OnClicked(FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnBranchConversationClicked,
									MessageIndex))
								[
									SNew(STextBlock)
									.Text(LOCTEXT(
										"BranchAssistantReplyButton",
										"从新任务开始"))
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity(FSlateColor(
										GetPanelTextColor()))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(
									UnrealAgentMCPConversationModel::FormatCompletionLabel(
											Message.CompletedAt,
											Message.bFailed,
											FDateTime::Now())))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(
									Message.bFailed
										? UnrealAgentMCP::Palette::Danger()
										: GetConversationMessageMutedColor(
											Message.Role)))
							]
						]
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationAttachmentChip(const FWorldDataConversationAttachment& Attachment) const
	{
		const FString DisplayName = Attachment.DisplayName.IsEmpty()
			? Attachment.Path
			: Attachment.DisplayName;
		return SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetAttachmentChipOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetAccentBorderColor()))
			.ToolTip(UnrealAgentMCPToolTip::Make(
				FText::FromString(Attachment.Path)))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 4.0f))
				.BorderImage(GetAttachmentChipInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetAccentSurfaceColor()))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Attachment.bDirectory
							? LOCTEXT(
								"SentFolderAttachmentType",
								"文件夹")
							: LOCTEXT(
								"SentFileAttachmentType",
								"文件"))
						.ColorAndOpacity(FSlateColor(
							GetReadableAccentTextColor()))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(DisplayName))
						.ColorAndOpacity(FSlateColor(
							GetConversationMessageTextColor(
								EConversationMessageRole::User)))
					]
				]
			];
	}

FText SUnrealAgentMCPPanel::GetConversationRoleLabel(EConversationMessageRole Role) const
{
	switch (Role)
	{
	case EConversationMessageRole::User:
		return LOCTEXT("ConversationUserRole", "你");
	case EConversationMessageRole::Status:
		return LOCTEXT("ConversationStatusRole", "状态");
	case EConversationMessageRole::System:
		return LOCTEXT("ConversationSystemRole", "系统");
	case EConversationMessageRole::Tool:
		return LOCTEXT("ConversationToolRole", "工具");
	case EConversationMessageRole::Error:
		return LOCTEXT("ConversationErrorRole", "错误");
	case EConversationMessageRole::Assistant:
	default:
		return GetProviderDisplayName();
	}
}

FLinearColor SUnrealAgentMCPPanel::GetConversationMessageBackgroundColor(EConversationMessageRole Role) const
{
	switch (Role)
	{
	case EConversationMessageRole::User:
		return GetAccentFillColor(0.16f);
	case EConversationMessageRole::System:
		return UnrealAgentMCP::Palette::Surface();
	case EConversationMessageRole::Tool:
		return UnrealAgentMCP::Palette::SurfaceRaised();
	case EConversationMessageRole::Error:
		return UnrealAgentMCP::Palette::Blend(UnrealAgentMCP::Palette::Surface(), UnrealAgentMCP::Palette::Danger(), 0.16f);
	case EConversationMessageRole::Assistant:
	default:
		return UnrealAgentMCP::Palette::Background();
	}
}

FLinearColor SUnrealAgentMCPPanel::GetConversationMessageBorderColor(EConversationMessageRole Role) const
{
	switch (Role)
	{
	case EConversationMessageRole::User:
		return GetAccentFillColor(0.32f);
	case EConversationMessageRole::Error:
		return UnrealAgentMCP::Palette::Blend(UnrealAgentMCP::Palette::Border(), UnrealAgentMCP::Palette::Danger(), 0.45f);
	case EConversationMessageRole::Tool:
		return UnrealAgentMCP::Palette::BorderStrong();
	case EConversationMessageRole::System:
	case EConversationMessageRole::Assistant:
	default:
		return UnrealAgentMCP::Palette::Border();
	}
}

FLinearColor SUnrealAgentMCPPanel::GetConversationMessageTextColor(EConversationMessageRole Role) const
{
	return Role == EConversationMessageRole::Error ? UnrealAgentMCP::Palette::Danger() : GetPanelTextColor();
}

FLinearColor SUnrealAgentMCPPanel::GetConversationMessageMutedColor(EConversationMessageRole Role) const
{
	switch (Role)
	{
	case EConversationMessageRole::Error:
		return UnrealAgentMCP::Palette::Danger();
	case EConversationMessageRole::Tool:
		return UnrealAgentMCP::Palette::Primary();
	default:
		return GetPanelMutedTextColor();
	}
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildPermissionRequestCard()
{
		return SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetMessageCardOuterBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentBorderColor()); })
			[
				SNew(SBorder)
				.Padding(FMargin(12.0f, 10.0f))
				.BorderImage(GetMessageCardInnerBrush())
				.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentSurfaceColor()); })
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
							.Text(LOCTEXT("PermissionCardTitle", "需要权限"))
							.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 3.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]
							{
								const FString DisplayTitle = PendingPermissionTitle.IsEmpty()
									? FString(TEXT("Codex 请求执行一个 MCP 工具。"))
									: PendingPermissionTitle;
								return FText::FromString(DisplayTitle);
							})
							.ColorAndOpacity(FSlateColor(UnrealAgentMCP::Palette::TextSoft()))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]
							{
								const FString DisplayTool = PendingPermissionToolName.IsEmpty()
									? FString(TEXT("点击允许后继续，点击拒绝则取消本次操作。"))
									: FString::Printf(TEXT("工具：%s"), *PendingPermissionToolName);
								return FText::FromString(DisplayTool);
							})
							.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(12.0f, 0.0f, 6.0f, 0.0f)
					[
						BuildPermissionActionButton(
							LOCTEXT("PermissionAllowButton", "允许"),
							true,
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnAllowPermissionClicked))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildPermissionActionButton(
							LOCTEXT("PermissionDenyButton", "拒绝"),
							false,
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnDenyPermissionClicked))
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildPermissionActionButton(const FText& Label, bool bPrimary, const FOnClicked& OnClicked) const
{
		return SNew(SButton)
			.ButtonStyle(&ComposerButtonStyle)
			.ButtonColorAndOpacity_Lambda([this, bPrimary]
			{
				return FSlateColor(bPrimary ? GetAccentButtonColor() : GetAccentControlColor());
			})
			.ForegroundColor_Lambda([this, bPrimary]
			{
				return FSlateColor(bPrimary ? GetAccentButtonTextColor() : GetPanelTextColor());
			})
			.ContentPadding(FMargin(12.0f, 4.0f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTip(UnrealAgentMCPToolTip::Make(Label))
			.OnClicked(OnClicked)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity_Lambda([this, bPrimary]
				{
					return FSlateColor(bPrimary ? GetAccentButtonTextColor() : GetPanelTextColor());
				})
			];
	}

float SUnrealAgentMCPPanel::GetActiveContextUsageRatio() const
{
	return Conversations.IsValidIndex(ActiveConversationIndex) ? static_cast<float>(UnrealAgentMCPConversationModel::GetContextUsageRatio(Conversations[ActiveConversationIndex]))
															   : 0.0f;
}

FSlateColor SUnrealAgentMCPPanel::GetContextUsageRingColor() const
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FSlateColor(GetPanelMutedTextColor());
	}

	const float UsageRatio = GetActiveContextUsageRatio();
	if (UsageRatio >= UnrealAgentMCPConversationModel::DefaultContextCompactionThreshold)
	{
		return FSlateColor(UnrealAgentMCP::Palette::Danger());
	}
	if (UsageRatio >= 0.75f)
	{
		return FSlateColor(UnrealAgentMCP::Palette::Warning());
	}
	return FSlateColor(GetEffectiveAccentColor());
}

FText SUnrealAgentMCPPanel::GetContextUsageToolTipText() const
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FText::Format(LOCTEXT("ContextUsageNoConversationTooltip", "尚无活动对话\n新对话容量：{0}\n约达到 90% 时自动压缩"),
			FText::FromString(UnrealAgentMCPConversationModel::FormatContextTokenCapacity(CurrentContextTokenCapacity)));
	}

	const FConversation& Conversation = Conversations[ActiveConversationIndex];
	const int32 Capacity = FMath::Max(1, Conversation.ContextTokenCapacity);
	const int32 EstimatedTokens = FMath::Max(0, Conversation.EstimatedContextTokens);
	const int32 RemainingTokens = FMath::Max(0, Capacity - EstimatedTokens);
	const int32 CompactionTokenThreshold = FMath::RoundToInt(Capacity * UnrealAgentMCPConversationModel::DefaultContextCompactionThreshold);
	const double UsagePercent = 100.0 * UnrealAgentMCPConversationModel::GetContextUsageRatio(Conversation);
	const FString LastCompaction = Conversation.ContextCompactionHistory.IsEmpty() ? TEXT("尚未压缩") : Conversation.ContextCompactionHistory.Last().CompactedAtUtc.ToIso8601();

	return FText::FromString(FString::Printf(TEXT("上下文使用：%.1f%%\n"
												  "约 %s / %s tokens\n"
												  "剩余约 %s\n"
												  "自动压缩：90%%（约 %s）\n"
												  "压缩代次：%d · 上次：%s\n"
												  "当前为字符估算，实际以 Provider usage 与模型硬上限为准。"),
		UsagePercent, *UnrealAgentMCPConversationModel::FormatEstimatedTokenCount(EstimatedTokens), *UnrealAgentMCPConversationModel::FormatContextTokenCapacity(Capacity),
		*UnrealAgentMCPConversationModel::FormatEstimatedTokenCount(RemainingTokens), *UnrealAgentMCPConversationModel::FormatEstimatedTokenCount(CompactionTokenThreshold),
		Conversation.ContextGeneration, *LastCompaction));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildComposer()
{
		ComposerTextLayout.Reset();
		TArray<TSharedRef<ITextDecorator>> ComposerDecorators;
		ComposerDecorators.Add(FWidgetDecorator::Create(
			TEXT("attachment"),
			FWidgetDecorator::FCreateWidget::CreateLambda(
				[this](const FTextRunInfo& RunInfo, const ISlateStyle*)
				{
					const FString* EncodedPath =
						RunInfo.MetaData.Find(TEXT("path64"));
					FString AttachmentPath;
					if (EncodedPath == nullptr
						|| !FBase64::Decode(
							*EncodedPath,
							AttachmentPath,
							EBase64Mode::UrlSafe))
					{
						AttachmentPath = TEXT("无法解析的文件引用");
					}
					const FString AttachmentId =
						RunInfo.MetaData.FindRef(TEXT("id"));
					const FString DirectoryValue =
						RunInfo.MetaData.FindRef(TEXT("directory"));
					const bool bDirectory = DirectoryValue == TEXT("1")
						|| DirectoryValue.Equals(
							TEXT("true"),
							ESearchCase::IgnoreCase);
					const TSharedRef<FSlateFontMeasure> FontMeasure =
						FSlateApplication::Get()
							.GetRenderer()
							->GetFontMeasureService();
					const int16 Baseline = FontMeasure->GetBaseline(
						LightTextBoxStyle.TextStyle.Font);
					return FSlateWidgetRun::FWidgetRunInfo(
						BuildInlineAttachmentChip(
							AttachmentId,
							AttachmentPath,
							bDirectory),
						Baseline - 2);
				})));
		const TSharedRef<FRichTextLayoutMarshaller> ComposerMarshaller =
			FRichTextLayoutMarshaller::Create(
				MoveTemp(ComposerDecorators),
				&FAppStyle::Get());

		SAssignNew(ComposerScrollBar, SScrollBar)
			.Orientation(Orient_Vertical)
			.AlwaysShowScrollbar(true)
			.Thickness(FVector2D(8.0f, 8.0f));

		return SNew(SDropTarget)
			.OnAllowDrop(SDropTarget::FVerifyDrag::CreateSP(
				this,
				&SUnrealAgentMCPPanel::CanAcceptAttachmentDrop))
			.OnDropped(FOnDrop::CreateSP(
				this,
				&SUnrealAgentMCPPanel::OnAttachmentPathsDropped))
			.OnDragEnter(SDropTarget::FOnDragAction::CreateSP(
				this,
				&SUnrealAgentMCPPanel::OnAttachmentDragEntered))
			.OnDragLeave(SDropTarget::FOnDragAction::CreateSP(
				this,
				&SUnrealAgentMCPPanel::OnAttachmentDragLeft))
			.bOnlyRecognizeOnDragEnter(true)
			.ValidColor(FSlateColor(GetEffectiveAccentColor()))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetComposerOuterBrush())
			.BorderBackgroundColor_Lambda([this]
			{
				return FSlateColor(GetPanelBorderColor());
			})
			[
				SNew(SBorder)
				.Padding(FMargin(12.0f, 10.0f))
				.BorderImage(GetComposerInnerBrush())
				.BorderBackgroundColor_Lambda([this]
				{
					return FSlateColor(GetPanelSurfaceColor());
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.MaxDesiredHeight(112.0f)
						.Visibility_Lambda([this]
						{
							return Conversations.IsValidIndex(
								ActiveConversationIndex)
								&& !Conversations[ActiveConversationIndex]
									.QueuedPrompts.IsEmpty()
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(ComposerQueueBox, SVerticalBox)
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBorder)
						.Visibility_Lambda([this]
						{
							return EditingUserMessageIndex != INDEX_NONE
								|| EditingQueuedPromptId.IsValid()
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.Padding(FMargin(8.0f, 5.0f))
						.BorderImage(GetAttachmentChipInnerBrush())
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
								SNew(STextBlock)
								.Text_Lambda([this]
								{
									return EditingQueuedPromptId.IsValid()
										? LOCTEXT(
											"EditingQueuedPromptNotice",
											"正在编辑待执行消息；发送后保持原队列位置")
										: LOCTEXT(
											"EditingUserPromptNotice",
											"这一轮及之后的上下文已清除；发送后从这里开始新分支");
								})
								.ColorAndOpacity_Lambda([this]
								{
									return FSlateColor(GetPanelTextColor());
								})
								.Font(FAppStyle::GetFontStyle("SmallFont"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&ToolbarButtonStyle)
								.ContentPadding(FMargin(6.0f, 2.0f))
								.IsFocusable(false)
								.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
									"CancelEditUserPromptTooltip",
									"取消重新编辑；已清除的对话分支不会恢复")))
								.OnClicked(FOnClicked::CreateSP(
									this,
									&SUnrealAgentMCPPanel::OnCancelUserMessageEditClicked))
								[
									SNew(STextBlock)
									.Text(LOCTEXT(
										"CancelEditUserPromptButton",
										"取消"))
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity_Lambda([this]
									{
										return FSlateColor(GetPanelTextColor());
									})
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]
						{
							return PendingAttachmentPaths.IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
						[
							SAssignNew(ComposerAttachmentsBox, SWrapBox)
							.UseAllottedSize(true)
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SBox)
						.MinDesiredHeight(72.0f)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SBorder)
							.Padding(LightTextBoxStyle.Padding)
							.Clipping(EWidgetClipping::ClipToBounds)
							.BorderImage_Lambda([this]
							{
								if (ComposerTextBox.IsValid()
									&& ComposerTextBox->HasKeyboardFocus())
								{
									return &LightTextBoxStyle
										.BackgroundImageFocused;
								}
								if (ComposerTextBox.IsValid()
									&& ComposerTextBox->IsHovered())
								{
									return &LightTextBoxStyle
										.BackgroundImageHovered;
								}
								return &LightTextBoxStyle
									.BackgroundImageNormal;
							})
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								[
									SAssignNew(
										ComposerTextBox,
										SUnrealAgentMCPComposerEditableText)
									.Marshaller(ComposerMarshaller)
									.CreateSlateTextLayout(
										FCreateSlateTextLayout::CreateSP(
											this,
											&SUnrealAgentMCPPanel::CreateComposerTextLayout))
									.TextStyle(&LightTextBoxStyle.TextStyle)
									.HintText_Lambda([this]
									{
										return FText::Format(
											LOCTEXT(
												"ComposerHint",
												"向 {0} 发送消息…  Enter 发送，Shift+Enter 换行，可粘贴文件"),
											GetProviderDisplayName());
									})
									.WrapTextAt(1.0f)
									.AutoWrapText(false)
									.Clipping(EWidgetClipping::ClipToBounds)
									.WrappingPolicy(
										ETextWrappingPolicy::AllowPerCharacterWrapping)
									.ModiferKeyForNewLine(
										EModifierKey::Control)
									.ClearKeyboardFocusOnCommit(false)
									.VScrollBar(ComposerScrollBar)
									.OnKeyDownHandler(
										this,
										&SUnrealAgentMCPPanel::OnComposerKeyDown)
									.OnTextChanged(
										this,
										&SUnrealAgentMCPPanel::OnComposerTextChanged)
									.OnTextCommitted(
										this,
										&SUnrealAgentMCPPanel::OnComposerTextCommitted)
									.IsEnabled_Lambda([this]
									{
										return UnrealAgentACPProviderModel::GetProvider(ActiveProvider)
											.bSupportsEmbeddedConversation;
									})
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(4.0f, 0.0f, 0.0f, 0.0f)
								[
									ComposerScrollBar.ToSharedRef()
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 12.0f, 0.0f)
						[
							BuildAttachmentCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							BuildModeCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							BuildApprovalCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							BuildSelfRepairCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("McpContextText", "UE MCP"))
							.ColorAndOpacity_Lambda([this]
							{
								return ApplicationService->IsServerRunning()
									? FSlateColor(UnrealAgentMCP::Palette::Success())
									: FSlateColor(GetPanelMutedTextColor());
							})
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT(
								"ComposerNewLineShortcutHint",
								"Shift+Enter 换行"))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FSlateColor(
								GetPanelMutedTextColor()))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(SUnrealAgentMCPContextUsageRing)
							.Usage_Lambda([this]
							{
								return GetActiveContextUsageRatio();
							})
							.ForegroundColor_Lambda([this]
							{
								return GetContextUsageRingColor();
							})
							.BackgroundColor_Lambda([this]
							{
								return FSlateColor(GetPanelBorderColor());
							})
							.Diameter(18.0f)
							.StrokeWidth(2.0f)
							.ToolTip(UnrealAgentMCPToolTip::Make(
								TAttribute<FText>::CreateLambda([this]
								{
									return GetContextUsageToolTipText();
								})))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							BuildModelCombo()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.ContentPadding(FMargin(9.0f, 3.0f))
							.ButtonStyle(&ComposerButtonStyle)
							.ButtonColorAndOpacity_Lambda([this] { return FSlateColor(GetAccentButtonColor()); })
							.ForegroundColor_Lambda([this] { return FSlateColor(GetAccentButtonTextColor()); })
							.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
								"SendTooltip",
								"发送到当前 ACP（Enter）")))
							.IsFocusable(false)
							.IsEnabled_Lambda([this] { return CanSendMessage(); })
							.OnClicked(FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnSendClicked))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SendButtonArrow", "↑"))
								.ColorAndOpacity_Lambda([this] { return FSlateColor(GetAccentButtonTextColor()); })
							]
						]
					]
			]
				]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.Visibility_Lambda([this]
					{
						return bAttachmentDragOver
							? EVisibility::HitTestInvisible
							: EVisibility::Collapsed;
					})
					.Padding(FMargin(18.0f, 10.0f))
					.BorderImage(GetMessageCardOuterBrush())
					.BorderBackgroundColor(GetAccentSurfaceColor())
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"AttachmentDropHint",
							"松开以在光标处插入文件或文件夹"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
				]
			];
	}

TSharedRef<FSlateTextLayout> SUnrealAgentMCPPanel::CreateComposerTextLayout(SWidget* OwningWidget, const FTextBlockStyle& DefaultTextStyle)
{
	const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(OwningWidget, DefaultTextStyle);
	// 默认行迭代器可能把内联附件后的整段文本视为一个单词。
	// 字符簇候选项可让文本利用当前行剩余宽度，且不会拆分控件，也无需用硬换行修改底层消息。
	Layout->SetLineBreakIterator(FBreakIterator::CreateCharacterBoundaryIterator());
	// 同一 delegate 也会用于提示文字布局；首个实例才是可编辑正文。
	if (!ComposerTextLayout.IsValid())
	{
		ComposerTextLayout = Layout;
	}
	return Layout;
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildAttachmentCombo()
	{
		return UnrealAgentMCPPanelWidgets::AnimateControl(
			SNew(SComboButton)
			.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
				"AttachmentComboTooltip",
				"添加文件或文件夹")))
			.ComboButtonStyle(&ComposerComboButtonStyle)
			.ButtonStyle(&ToolbarButtonStyle)
			.ForegroundColor(FSlateColor(GetPanelTextColor()))
			.ContentPadding(FMargin(10.0f, 3.0f))
			.HasDownArrow(false)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AttachButton", "+"))
				.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
			]
			.OnGetMenuContent(FOnGetContent::CreateSP(
				this,
				&SUnrealAgentMCPPanel::BuildAttachmentMenu)));
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildAttachmentMenu()
	{
		return UnrealAgentMCPPanelWidgets::AnimateMenu(
			SNew(SBorder)
			.Padding(4.0f)
			.BorderImage(GetMessageCardOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetPanelSurfaceColor()))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(UnrealAgentMCPPanelWidgets::SAnimatedButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(12.0f, 7.0f))
					.HAlign(HAlign_Left)
					.OnClicked(FOnClicked::CreateSP(
						this,
						&SUnrealAgentMCPPanel::OnAttachFilesClicked))
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"ChooseFilesMenuItem",
							"选择文件（支持多选、任意格式）"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(UnrealAgentMCPPanelWidgets::SAnimatedButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(12.0f, 7.0f))
					.HAlign(HAlign_Left)
					.OnClicked(FOnClicked::CreateSP(
						this,
						&SUnrealAgentMCPPanel::OnAttachFolderClicked))
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"ChooseFolderMenuItem",
							"选择文件夹"))
						.ColorAndOpacity(FSlateColor(GetPanelTextColor()))
					]
				]
			]);
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildInlineAttachmentChip(const FString& AttachmentId, const FString& AttachmentPath, const bool bDirectory)
	{
		FString DisplayName = FPaths::GetCleanFilename(AttachmentPath);
		if (DisplayName.IsEmpty())
		{
			DisplayName = AttachmentPath;
		}

		return SNew(SBox)
			.MaxDesiredWidth_Lambda([this]() -> FOptionalSize
			{
				constexpr float MaximumChipWidth = 240.0f;
				constexpr float ChipBoundaryInset = 4.0f;
				if (!ComposerTextBox.IsValid())
				{
					return FOptionalSize(MaximumChipWidth);
				}

				const float ComposerWidth = ComposerTextBox
					->GetCachedGeometry()
					.GetLocalSize().X;
				if (ComposerWidth <= ChipBoundaryInset)
				{
					return FOptionalSize(MaximumChipWidth);
				}
				return FOptionalSize(FMath::Max(
					1.0f,
					FMath::Min(
						MaximumChipWidth,
						ComposerWidth - ChipBoundaryInset)));
			})
			.Clipping(EWidgetClipping::ClipToBounds)
			[
			SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetAttachmentChipOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetAccentBorderColor()))
			.ToolTip(UnrealAgentMCPToolTip::Make(
				FText::FromString(AttachmentPath)))
			[
				SNew(SBorder)
				.Padding(FMargin(7.0f, 2.0f, 2.0f, 2.0f))
				.BorderImage(GetAttachmentChipInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetAccentSurfaceColor()))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 5.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(bDirectory
							? LOCTEXT("InlineFolderAttachmentType", "文件夹")
							: LOCTEXT("InlineFileAttachmentType", "文件"))
						.ColorAndOpacity(GetReadableAccentTextColor())
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SBox)
						.MaxDesiredWidth(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(DisplayName))
							.ColorAndOpacity(GetPanelTextColor())
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(3.0f, 0.0f))
						.IsFocusable(false)
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"RemoveInlineAttachmentTooltip",
							"移除此行内引用")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnRemoveInlineAttachmentClicked,
							AttachmentId))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RemoveInlineAttachmentButton", "×"))
							.ColorAndOpacity(GetPanelMutedTextColor())
						]
					]
				]
			]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildAttachmentChip(const FString& AttachmentPath)
	{
		FString DisplayName = FPaths::GetCleanFilename(AttachmentPath);
		if (DisplayName.IsEmpty())
		{
			DisplayName = AttachmentPath;
		}
		const bool bDirectory =
			IFileManager::Get().DirectoryExists(*AttachmentPath);
		return SNew(SBox)
			.MaxDesiredWidth(240.0f)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
			SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(GetAttachmentChipOuterBrush())
			.BorderBackgroundColor(FSlateColor(GetAccentBorderColor()))
			.ToolTip(UnrealAgentMCPToolTip::Make(
				FText::FromString(AttachmentPath)))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 3.0f, 3.0f, 3.0f))
				.BorderImage(GetAttachmentChipInnerBrush())
				.BorderBackgroundColor(FSlateColor(GetAccentSurfaceColor()))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(bDirectory
							? LOCTEXT("FolderAttachmentType", "文件夹")
							: LOCTEXT("FileAttachmentType", "文件"))
						.ColorAndOpacity(GetReadableAccentTextColor())
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.MaxDesiredWidth(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(DisplayName))
							.ColorAndOpacity(GetPanelTextColor())
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&ToolbarButtonStyle)
						.ContentPadding(FMargin(4.0f, 0.0f))
						.IsFocusable(false)
						.ToolTip(UnrealAgentMCPToolTip::Make(LOCTEXT(
							"RemoveAttachmentTooltip",
							"移除此附件")))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnRemoveAttachmentClicked,
							AttachmentPath))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RemoveAttachmentButton", "×"))
							.ColorAndOpacity(GetPanelMutedTextColor())
						]
					]
				]
			]
			];
	}

#undef LOCTEXT_NAMESPACE
