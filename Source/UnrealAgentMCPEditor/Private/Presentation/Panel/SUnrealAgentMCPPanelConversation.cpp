// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPPanelConversation.cpp
 * @brief 会话状态、ACP 回调、权限流程与面板操作逻辑。
 */

#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Presentation/Panel/SUnrealAgentMCPPanelPrivate.h"
#include "Presentation/Widgets/SUnrealAgentMCPMarkdown.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateWidgetRun.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Input/DragAndDrop.h"
#include "Application/Media/UnrealAgentPromptMedia.h"
#include "Misc/Base64.h"
#include "Misc/MessageDialog.h"
#include "Async/Async.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <ShlObj.h>
#include <Shellapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace
{
	EUnrealAgentACPProvider ProviderFromId(const FString& ProviderId)
	{
		return ProviderId.Equals(TEXT("cursor"), ESearchCase::IgnoreCase) ? EUnrealAgentACPProvider::Cursor : EUnrealAgentACPProvider::Codex;
	}

	FString ProviderToId(const EUnrealAgentACPProvider Provider)
	{
		return Provider == EUnrealAgentACPProvider::Cursor ? TEXT("cursor") : TEXT("codex");
	}

	bool TryReadClipboardAttachmentPaths(TArray<FString>& OutPaths)
	{
		OutPaths.Reset();
#if PLATFORM_WINDOWS
		if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(GetActiveWindow()))
		{
			return false;
		}

		struct FScopedClipboardClose
		{
			~FScopedClipboardClose()
			{
				CloseClipboard();
			}
		} ClipboardClose;

		const HDROP DropHandle = static_cast<HDROP>(GetClipboardData(CF_HDROP));
		if (!DropHandle)
		{
			return true;
		}

		const UINT PathCount = DragQueryFile(DropHandle, 0xFFFFFFFF, nullptr, 0);
		OutPaths.Reserve(static_cast<int32>(PathCount));
		for (UINT Index = 0; Index < PathCount; ++Index)
		{
			const UINT PathLength = DragQueryFile(DropHandle, Index, nullptr, 0);
			TArray<TCHAR> PathBuffer;
			PathBuffer.SetNumZeroed(static_cast<int32>(PathLength) + 1);
			if (DragQueryFile(DropHandle, Index, PathBuffer.GetData(), PathBuffer.Num()) > 0)
			{
				OutPaths.Emplace(PathBuffer.GetData());
			}
		}
		return true;
#else
		return false;
#endif
	}
}

void SUnrealAgentMCPPanel::SetDetail(const FText& Title, const FString& Text)
{
	const FString PrettyText = UnrealAgentMCP::PrettyJson(Text);
	bShowSettings = false;
	bShowDetail = true;
	bDetailIsConversation = false;
	CurrentDetailText = PrettyText;
	if (DetailTitleText.IsValid())
	{
		DetailTitleText->SetText(Title);
	}
	if (DetailTextBox.IsValid())
	{
		DetailTextBox->SetText(FText::FromString(PrettyText));
	}
	PlayContentTransition();
}

void SUnrealAgentMCPPanel::SetLastAction(const FText& Text)
{
	LastAction = Text;
}

void SUnrealAgentMCPPanel::RebuildConversationMessages(const bool bScrollToEnd)
{
	if (!ConversationScrollBox.IsValid())
	{
		return;
	}

	const float PreviousScrollOffset = ConversationScrollBox->GetScrollOffset();
	const bool bShouldFollowTail = bScrollToEnd && (bConversationTailFollowEnabled || IsConversationNearTail());
	bDeferredAssistantTextRefreshPending = false;
	ConversationMessageMarkdownWidgets.Reset();
	ConversationScrollBox->ClearChildren();
	TSharedPtr<FUnrealAgentMCPTextSelectionGroup> AgentSelectionGroup;
	constexpr int32 MaximumRenderedMessages = 400;
	int32 MessageIndex = FMath::Max(0, ConversationMessages.Num() - MaximumRenderedMessages);
	if (MessageIndex > 0)
		{
			ConversationScrollBox->AddSlot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT(
						"ConversationRenderWindowNotice",
						"为保持编辑器流畅，当前仅渲染最近 {0} 条消息；更早内容仍保存在会话文档中。"),
					FText::AsNumber(MaximumRenderedMessages)))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(GetPanelMutedTextColor()))
			];
		}
	while (MessageIndex < ConversationMessages.Num())
	{
		const bool bUserMessage = ConversationMessages[MessageIndex].Role == EConversationMessageRole::User;
		if (bUserMessage)
		{
			AgentSelectionGroup.Reset();
		}
		else if (!AgentSelectionGroup.IsValid())
		{
			AgentSelectionGroup = CreateUnrealAgentMCPTextSelectionGroup();
		}

		if (ConversationMessages[MessageIndex].Role == EConversationMessageRole::Tool)
		{
			int32 LastToolMessageIndex = MessageIndex;
			while (LastToolMessageIndex + 1 < ConversationMessages.Num() && ConversationMessages[LastToolMessageIndex + 1].Role == EConversationMessageRole::Tool)
			{
				++LastToolMessageIndex;
			}
			ConversationScrollBox->AddSlot().Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))[BuildConversationToolGroupWidget(MessageIndex, LastToolMessageIndex, AgentSelectionGroup)];
			MessageIndex = LastToolMessageIndex + 1;
			continue;
		}

		ConversationScrollBox->AddSlot().Padding(
			FMargin(0.0f, 0.0f, 0.0f, 10.0f))[BuildConversationMessageWidget(ConversationMessages[MessageIndex], MessageIndex, AgentSelectionGroup)];
		if (bUserMessage)
		{
			AgentSelectionGroup.Reset();
		}
		++MessageIndex;
	}
	if (bShouldFollowTail)
	{
		ScheduleConversationTailScroll();
	}
	else
	{
		ConversationScrollBox->SetScrollOffset(PreviousScrollOffset);
	}
}

bool SUnrealAgentMCPPanel::IsConversationNearTail() const
{
	if (!ConversationScrollBox.IsValid())
	{
		return true;
	}
	const float DistanceFromEnd = FMath::Max(0.0f, ConversationScrollBox->GetScrollOffsetOfEnd() - ConversationScrollBox->GetScrollOffset());
	return DistanceFromEnd <= 64.0f;
}

void SUnrealAgentMCPPanel::OnConversationUserScrolled(const float ScrollOffset)
{
	(void)ScrollOffset;
	bConversationTailFollowEnabled = IsConversationNearTail();
}

void SUnrealAgentMCPPanel::ScheduleConversationTailScroll()
{
	bConversationTailFollowEnabled = true;
	if (!DeferredConversationTailScrollTimer.IsValid())
	{
		DeferredConversationTailScrollTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SUnrealAgentMCPPanel::HandleDeferredConversationTailScroll));
	}
}

EActiveTimerReturnType SUnrealAgentMCPPanel::HandleDeferredConversationTailScroll(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	DeferredConversationTailScrollTimer.Reset();
	if (ConversationScrollBox.IsValid() && bConversationTailFollowEnabled)
	{
		ConversationScrollBox->ScrollToEnd();
	}
	return EActiveTimerReturnType::Stop;
}

void SUnrealAgentMCPPanel::RebuildComposerQueue()
{
	if (!ComposerQueueBox.IsValid())
	{
		return;
	}
	ComposerQueueBox->ClearChildren();
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return;
	}
	const TArray<FWorldDataQueuedPrompt>& Queue = Conversations[ActiveConversationIndex].QueuedPrompts;
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		ComposerQueueBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Index + 1 < Queue.Num() ? 5.0f : 0.0f)[BuildQueuedPromptWidget(Queue[Index], Index)];
	}
}

FWorldDataQueuedPrompt SUnrealAgentMCPPanel::BuildQueuedPrompt(const FString& Message, const TArray<FWorldDataConversationAttachment>& Attachments) const
{
	FWorldDataQueuedPrompt Prompt;
	Prompt.Text = Message;
	Prompt.Attachments = Attachments;
	Prompt.ProviderId = ProviderToId(GetModelCatalogProvider());
	Prompt.AgentMode = static_cast<uint8>(CurrentAgentMode);
	Prompt.ApprovalPolicy = static_cast<uint8>(CurrentApprovalPolicy);
	Prompt.SelfRepairPolicy = static_cast<uint8>(CurrentSelfRepairPolicy);
	if (const FUnrealAgentAcpConfigOption* Model = FindConfigOption(TEXT("model")))
	{
		Prompt.ModelId = Model->CurrentValue;
	}
	if (const FUnrealAgentAcpConfigOption* Reasoning = FindConfigOption(TEXT("model_reasoning_effort")))
	{
		Prompt.ReasoningEffort = Reasoning->CurrentValue;
	}
	if (const FUnrealAgentAcpConfigOption* Tier = FindConfigOption(TEXT("service_tier")))
	{
		Prompt.ServiceTier = Tier->CurrentValue;
	}
	return Prompt;
}

FReply SUnrealAgentMCPPanel::OnEditQueuedPromptClicked(const FGuid PromptId)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FReply::Handled();
	}
	FWorldDataQueuedPrompt* Prompt = Conversations[ActiveConversationIndex].QueuedPrompts.FindByPredicate(
		[&PromptId](const FWorldDataQueuedPrompt& Candidate)
		{
			return Candidate.Id == PromptId;
		});
	if (!Prompt)
	{
		return FReply::Handled();
	}
	EditingQueuedPromptId = PromptId;
	EditingUserMessageIndex = INDEX_NONE;
	PendingAttachmentPaths.Reset();
	for (const FWorldDataConversationAttachment& Attachment : Prompt->Attachments)
	{
		if (!Attachment.bInline)
		{
			PendingAttachmentPaths.Add(Attachment.Path);
		}
	}
	if (ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::FromString(UnrealAgentMCPConversationModel::BuildComposerRichTextForEditing(Prompt->Text, Prompt->Attachments)));
	}
	RebuildComposerAttachments();
	SetLastAction(LOCTEXT("EditingQueuedPromptAction", "正在编辑待执行消息；再次发送后保持原队列位置。"));
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnDeleteQueuedPromptClicked(const FGuid PromptId)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FReply::Handled();
	}
	FConversation& Conversation = Conversations[ActiveConversationIndex];
	const int32 Removed = Conversation.QueuedPrompts.RemoveAll(
		[&PromptId](const FWorldDataQueuedPrompt& Prompt)
		{
			return Prompt.Id == PromptId;
		});
	if (EditingQueuedPromptId == PromptId)
	{
		EditingQueuedPromptId.Invalidate();
		if (ComposerTextBox.IsValid())
		{
			ComposerTextBox->SetText(FText::GetEmpty());
		}
		ClearPendingAttachments();
	}
	if (Removed > 0)
	{
		Conversation.UpdatedAt = FDateTime::Now();
		RebuildComposerQueue();
		PersistConversationHistory();
		if (!Conversation.bIsRunning)
		{
			ScheduleQueuedPromptDispatch(Conversation.Id);
		}
		SetLastAction(LOCTEXT("QueuedPromptDeletedAction", "已撤回待执行消息。"));
	}
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnMoveQueuedPromptClicked(const FGuid PromptId, const int32 Direction)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FReply::Handled();
	}
	TArray<FWorldDataQueuedPrompt>& Queue = Conversations[ActiveConversationIndex].QueuedPrompts;
	const int32 From = Queue.IndexOfByPredicate(
		[&PromptId](const FWorldDataQueuedPrompt& Prompt)
		{
			return Prompt.Id == PromptId;
		});
	const int32 To = FMath::Clamp(From + Direction, 0, Queue.Num() - 1);
	if (From != INDEX_NONE && From != To)
	{
		Queue.Swap(From, To);
		RebuildComposerQueue();
		PersistConversationHistory();
	}
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnGuideQueuedPromptClicked(const FGuid PromptId)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FReply::Handled();
	}
	FConversation& Conversation = Conversations[ActiveConversationIndex];
	TArray<FWorldDataQueuedPrompt>& Queue = Conversation.QueuedPrompts;
	const int32 Index = Queue.IndexOfByPredicate(
		[&PromptId](const FWorldDataQueuedPrompt& Prompt)
		{
			return Prompt.Id == PromptId;
		});
	if (Index == INDEX_NONE)
	{
		return FReply::Handled();
	}
	if (Index > 0)
	{
		FWorldDataQueuedPrompt PriorityPrompt = MoveTemp(Queue[Index]);
		Queue.RemoveAt(Index);
		Queue.Insert(MoveTemp(PriorityPrompt), 0);
	}
	bool bCancelledAcpTurn = false;
	bool bCancelledMediaLoad = false;
	if (const TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id); Runtime && Runtime->IsValid())
	{
		if ((*Runtime)->MediaLoadCancellation.IsValid() || (*Runtime)->PendingMediaPrompt.IsSet())
		{
			CancelPromptMediaLoad(Conversation.Id, true);
			bCancelledMediaLoad = true;
		}
		for (const TSharedPtr<FUnrealAgentCodexACPClient>& Client : { (*Runtime)->CodexClient, (*Runtime)->CursorClient })
		{
			if (Client.IsValid() && Client->HasActiveTurn())
			{
				bCancelledAcpTurn = Client->CancelActivePrompt();
				break;
			}
		}
	}
	RebuildComposerQueue();
	PersistConversationHistory();
	if (!bCancelledAcpTurn)
	{
		TryDispatchNextQueuedPrompt(Conversation.Id);
	}
	SetLastAction(bCancelledAcpTurn || bCancelledMediaLoad ? LOCTEXT("QueuedPromptGuidingAction", "正在停止当前回合，随后优先发送引导消息。")
														   : LOCTEXT("QueuedPromptPrioritizedAction", "引导消息已移到队首。"));
	return HandledReplyWithComposerFocus();
}

FString SUnrealAgentMCPPanel::GetConversationToolGroupKey(const int32 FirstMessageIndex) const
{
	if (!ConversationMessages.IsValidIndex(FirstMessageIndex))
	{
		return FString();
	}

	const FConversationMessage& Message = ConversationMessages[FirstMessageIndex];
	const FString ConversationId =
		Conversations.IsValidIndex(ActiveConversationIndex) ? Conversations[ActiveConversationIndex].Id.ToString(EGuidFormats::DigitsWithHyphensLower) : TEXT("unsaved");
	const FString MessageKey = !Message.ToolCallId.IsEmpty() ? Message.ToolCallId : FString::Printf(TEXT("%lld-%d"), Message.CreatedAt.GetTicks(), FirstMessageIndex);
	return ConversationId + TEXT(":") + MessageKey;
}

FReply SUnrealAgentMCPPanel::OnToggleConversationToolGroupClicked(FString GroupKey, const bool bDefaultExpanded)
{
	const bool* Override = ToolGroupExpansionOverrides.Find(GroupKey);
	const bool bCurrentlyExpanded = Override ? *Override : bDefaultExpanded;
	ToolGroupExpansionOverrides.Add(MoveTemp(GroupKey), !bCurrentlyExpanded);
	RebuildConversationMessages(false);
	return FReply::Handled();
}

int32 SUnrealAgentMCPPanel::AddConversationMessage(EConversationMessageRole Role, const FString& Text, bool bStreaming, TArray<FWorldDataConversationAttachment> Attachments)
{
	FConversationMessage Message;
	Message.Role = Role;
	Message.Text = Text;
	Message.Attachments = MoveTemp(Attachments);
	Message.bStreaming = bStreaming;
	const int32 Index = ConversationMessages.Add(MoveTemp(Message));
	RebuildConversationMessages();
	return Index;
}

FString SUnrealAgentMCPPanel::TrimConversationEventText(const FString& Text)
{
	return UnrealAgentMCPConversationModel::TrimTaggedEventText(Text);
}

bool SUnrealAgentMCPPanel::TryExtractConversationEvent(const FString& Text, EConversationMessageRole& OutRole, FString& OutText) const
{
	return UnrealAgentMCPConversationModel::TryParseTaggedEvent(Text, OutRole, OutText);
}

void SUnrealAgentMCPPanel::StartConversationTurn(const FString& UserMessage, const TArray<FWorldDataConversationAttachment>& Attachments)
{
	if (ActiveAssistantMessageIndex != INDEX_NONE && ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
	{
		ConversationMessages[ActiveAssistantMessageIndex].bStreaming = false;
	}
	ActiveAssistantMessageIndex = INDEX_NONE;
	if (ActiveTurnStatusMessageIndex != INDEX_NONE && ConversationMessages.IsValidIndex(ActiveTurnStatusMessageIndex))
	{
		FConversationMessage& PreviousStatus = ConversationMessages[ActiveTurnStatusMessageIndex];
		PreviousStatus.bStreaming = false;
		if (!PreviousStatus.bCompleted && !PreviousStatus.bFailed)
		{
			PreviousStatus.bCompleted = true;
			PreviousStatus.CompletedAt = FDateTime::Now();
		}
	}
	ActiveTurnStatusMessageIndex = INDEX_NONE;

	TArray<FString> AttachmentPaths;
	AttachmentPaths.Reserve(Attachments.Num());
	for (const FWorldDataConversationAttachment& Attachment : Attachments)
	{
		AttachmentPaths.Add(Attachment.Path);
	}
	const FString TranscriptUserMessage = UnrealAgentMCPConversationModel::BuildAttachmentDisplayMessage(UserMessage, AttachmentPaths);

	AddConversationMessage(EConversationMessageRole::User, UserMessage, false, Attachments);
	ActiveTurnStatusMessageIndex = AddConversationMessage(EConversationMessageRole::Status, FString::Printf(TEXT("正在发送到 %s…"), *GetProviderDisplayName().ToString()), true);

	if (Conversations.IsValidIndex(ActiveConversationIndex) && !Conversations[ActiveConversationIndex].bHasCustomTitle)
	{
		Conversations[ActiveConversationIndex].Title = MakeConversationTitle(TranscriptUserMessage);
		Conversations[ActiveConversationIndex].bHasCustomTitle = true;
		RebuildSidebar();
	}

	if (!ConversationTranscript.IsEmpty())
	{
		ConversationTranscript += TEXT("\n\n");
	}
	ConversationTranscript += FString::Printf(TEXT("你：%s"), *TranscriptUserMessage);
	RefreshConversationText();
	PersistConversationHistory();
}

void SUnrealAgentMCPPanel::AppendAssistantText(const FString& Text)
{
	if (ActiveAssistantMessageIndex == INDEX_NONE || !ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
	{
		if (!ConversationTranscript.IsEmpty())
		{
			ConversationTranscript += FString::Printf(TEXT("\n\n%s："), *GetProviderDisplayName().ToString());
		}
		ActiveAssistantMessageIndex = AddConversationMessage(EConversationMessageRole::Assistant, FString(), true);
	}

	ConversationMessages[ActiveAssistantMessageIndex].Text += Text;
	ConversationMessages[ActiveAssistantMessageIndex].bStreaming = true;
	ConversationTranscript += Text;
	ScheduleAssistantTextRefresh();
}

void SUnrealAgentMCPPanel::AppendConversationEvent(EConversationMessageRole Role, const FString& Text)
{
	if (ActiveAssistantMessageIndex != INDEX_NONE && ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
	{
		ConversationMessages[ActiveAssistantMessageIndex].bStreaming = false;
		ActiveAssistantMessageIndex = INDEX_NONE;
	}

	AddConversationMessage(Role, Text);
	const FString RoleLabel = GetConversationRoleLabel(Role).ToString();
	ConversationTranscript += FString::Printf(TEXT("\n\n[%s] %s\n"), *RoleLabel, *Text);
	RefreshConversationText();
}

void SUnrealAgentMCPPanel::AppendConversationText(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}

	EConversationMessageRole EventRole = EConversationMessageRole::System;
	FString EventText;
	if (TryExtractConversationEvent(Text, EventRole, EventText))
	{
		AppendConversationEvent(EventRole, EventText);
		return;
	}

	AppendAssistantText(Text);
}

void SUnrealAgentMCPPanel::CompleteActiveAssistantMessage(const bool bFailed, const FDateTime& CompletedAt)
{
	if (ActiveAssistantMessageIndex == INDEX_NONE || !ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
	{
		return;
	}

	FConversationMessage& AssistantMessage = ConversationMessages[ActiveAssistantMessageIndex];
	AssistantMessage.bStreaming = false;
	AssistantMessage.bCompleted = !bFailed;
	AssistantMessage.bFailed = bFailed;
	if (AssistantMessage.CompletedAt == FDateTime())
	{
		AssistantMessage.CompletedAt = CompletedAt;
	}
	ActiveAssistantMessageIndex = INDEX_NONE;
}

void SUnrealAgentMCPPanel::RefreshConversationText()
{
	bDeferredToolActivityRefreshPending = false;
	bShowSettings = false;
	bShowDetail = true;
	bDetailIsConversation = true;
	CurrentDetailText = ConversationTranscript;
	if (DetailTitleText.IsValid())
	{
		DetailTitleText->SetText(FText::Format(LOCTEXT("ProviderConversationTitle", "{0} 会话"), GetProviderDisplayName()));
	}
	RebuildConversationMessages();
}

void SUnrealAgentMCPPanel::ScheduleAssistantTextRefresh()
{
	bDeferredAssistantTextRefreshPending = true;
	if (!DeferredAssistantTextRefreshTimer.IsValid())
	{
		DeferredAssistantTextRefreshTimer = RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SUnrealAgentMCPPanel::HandleDeferredAssistantTextRefresh));
	}
}

EActiveTimerReturnType SUnrealAgentMCPPanel::HandleDeferredAssistantTextRefresh(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	DeferredAssistantTextRefreshTimer.Reset();
	if (!bDeferredAssistantTextRefreshPending)
	{
		return EActiveTimerReturnType::Stop;
	}

	bDeferredAssistantTextRefreshPending = false;
	CurrentDetailText = ConversationTranscript;
	if (ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
	{
		const bool bShouldFollowTail = bConversationTailFollowEnabled && IsConversationNearTail();
		const TWeakPtr<SUnrealAgentMCPMarkdown>* MarkdownWidget = ConversationMessageMarkdownWidgets.Find(ConversationMessages[ActiveAssistantMessageIndex].Id);
		if (MarkdownWidget)
		{
			if (const TSharedPtr<SUnrealAgentMCPMarkdown> Widget = MarkdownWidget->Pin())
			{
				Widget->SetMarkdown(ConversationMessages[ActiveAssistantMessageIndex].Text);
				if (bShouldFollowTail)
				{
					ScheduleConversationTailScroll();
				}
				return EActiveTimerReturnType::Stop;
			}
		}
		RebuildConversationMessages();
	}
	return EActiveTimerReturnType::Stop;
}

void SUnrealAgentMCPPanel::ScheduleConversationSave()
{
	bDeferredConversationSavePending = true;
	if (!DeferredConversationSaveTimer.IsValid())
	{
		DeferredConversationSaveTimer = RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SUnrealAgentMCPPanel::HandleDeferredConversationSave));
	}
}

EActiveTimerReturnType SUnrealAgentMCPPanel::HandleDeferredConversationSave(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	DeferredConversationSaveTimer.Reset();
	if (bDeferredConversationSavePending)
	{
		SaveActiveConversation();
	}
	return EActiveTimerReturnType::Stop;
}

void SUnrealAgentMCPPanel::ScheduleToolActivityRefresh()
{
	bDeferredToolActivityRefreshPending = true;
	if (!DeferredToolActivityRefreshTimer.IsValid())
	{
		DeferredToolActivityRefreshTimer = RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SUnrealAgentMCPPanel::HandleDeferredToolActivityRefresh));
	}
}

EActiveTimerReturnType SUnrealAgentMCPPanel::HandleDeferredToolActivityRefresh(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	DeferredToolActivityRefreshTimer.Reset();
	if (!bDeferredToolActivityRefreshPending)
	{
		return EActiveTimerReturnType::Stop;
	}

	bDeferredToolActivityRefreshPending = false;
	SaveActiveConversation();
	RefreshConversationText();
	return EActiveTimerReturnType::Stop;
}

void SUnrealAgentMCPPanel::HandleAcpText(const FString& Text)
{
	AppendConversationText(Text);
}

void SUnrealAgentMCPPanel::HandleAcpToolCall(const FUnrealAgentAcpToolCallUpdate& Update)
{
	int32 ToolMessageIndex = INDEX_NONE;
	for (int32 Index = ConversationMessages.Num() - 1; Index >= 0; --Index)
	{
		const FConversationMessage& Candidate = ConversationMessages[Index];
		if (Candidate.Role != EConversationMessageRole::Tool)
		{
			continue;
		}
		if (!Update.ToolCallId.IsEmpty() && Candidate.ToolCallId == Update.ToolCallId)
		{
			ToolMessageIndex = Index;
			break;
		}
		if (Update.ToolCallId.IsEmpty() && Candidate.bStreaming && Candidate.ToolName == UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Update.Title))
		{
			ToolMessageIndex = Index;
			break;
		}
	}

	if (ToolMessageIndex == INDEX_NONE)
	{
		if (ActiveAssistantMessageIndex != INDEX_NONE && ConversationMessages.IsValidIndex(ActiveAssistantMessageIndex))
		{
			ConversationMessages[ActiveAssistantMessageIndex].bStreaming = false;
			ActiveAssistantMessageIndex = INDEX_NONE;
		}

		FConversationMessage ToolMessage;
		ToolMessage.Role = EConversationMessageRole::Tool;
		ToolMessage.ToolCallId = Update.ToolCallId;
		ToolMessage.ToolName = UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Update.Title.IsEmpty() ? Update.ToolCallId : Update.Title);
		ToolMessageIndex = ConversationMessages.Add(MoveTemp(ToolMessage));
	}

	FConversationMessage& ToolMessage = ConversationMessages[ToolMessageIndex];
	if (ToolMessage.ToolState != EWorldDataConversationToolState::None && ToolMessage.ToolState != EWorldDataConversationToolState::Running &&
		Update.State == EUnrealAgentAcpToolCallState::Running)
	{
		return;
	}
	if (!Update.ToolCallId.IsEmpty())
	{
		ToolMessage.ToolCallId = Update.ToolCallId;
	}
	if (!Update.Title.IsEmpty())
	{
		ToolMessage.ToolName = UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Update.Title);
	}

	switch (Update.State)
	{
	case EUnrealAgentAcpToolCallState::Completed:
		ToolMessage.ToolState = EWorldDataConversationToolState::Completed;
		ToolMessage.bStreaming = false;
		ToolMessage.bCompleted = true;
		ToolMessage.bFailed = false;
		ToolMessage.CompletedAt = FDateTime::Now();
		break;
	case EUnrealAgentAcpToolCallState::Failed:
		ToolMessage.ToolState = EWorldDataConversationToolState::Failed;
		ToolMessage.bStreaming = false;
		ToolMessage.bCompleted = false;
		ToolMessage.bFailed = true;
		ToolMessage.CompletedAt = FDateTime::Now();
		break;
	case EUnrealAgentAcpToolCallState::Running:
	default:
		ToolMessage.ToolState = EWorldDataConversationToolState::Running;
		ToolMessage.bStreaming = true;
		ToolMessage.bCompleted = false;
		ToolMessage.bFailed = false;
		break;
	}

	ToolMessage.Text = UnrealAgentMCPConversationModel::BuildToolCallDisplayText(ToolMessage.ToolName, ToolMessage.ToolState);
	if (ToolMessage.bFailed)
	{
		ToolMessage.Text += UnrealAgentMCPConversationModel::BuildToolCallFailureDetails(Update.CanonicalAction, Update.Code, Update.SchemaErrorPaths, Update.TraceId);
	}
	ConversationTranscript += FString::Printf(TEXT("\n\n[工具] %s"), *ToolMessage.Text);
	ScheduleToolActivityRefresh();
}

void SUnrealAgentMCPPanel::HandleAcpPermission(const FUnrealAgentAcpPermissionRequest& Request)
{
	bShowSettings = false;
	bShowDetail = true;
	bHasPendingPermission = true;
	PendingPermissionId = Request.RequestId;
	PendingPermissionTitle = Request.Title;
	PendingPermissionToolName = !Request.ToolName.IsEmpty() ? Request.ToolName : Request.ToolCallId;
	PendingAllowOptionId = Request.AllowOptionId.IsEmpty() ? TEXT("allow") : Request.AllowOptionId;
	PendingDenyOptionId = Request.DenyOptionId.IsEmpty() ? TEXT("deny") : Request.DenyOptionId;

	if (DetailTitleText.IsValid())
	{
		DetailTitleText->SetText(FText::Format(LOCTEXT("ProviderConversationTitle", "{0} 会话"), GetProviderDisplayName()));
	}

	SetLastAction(FText::Format(LOCTEXT("PermissionWaitingAction", "等待权限确认：{0}"), FText::FromString(PendingPermissionTitle)));
}

void SUnrealAgentMCPPanel::HandleAcpStatus(const FString& Text)
{
	SetLastAction(FText::FromString(Text));
}

void SUnrealAgentMCPPanel::HandleAcpTurnStatus(const FWorldDataCodexTurnStatus& Status)
{
	if (ActiveTurnStatusMessageIndex == INDEX_NONE || !ConversationMessages.IsValidIndex(ActiveTurnStatusMessageIndex))
	{
		ActiveTurnStatusMessageIndex = AddConversationMessage(EConversationMessageRole::Status, Status.Message, true);
	}

	FConversationMessage& StatusMessage = ConversationMessages[ActiveTurnStatusMessageIndex];
	StatusMessage.Text = Status.Message;
	StatusMessage.bCompleted = Status.State == EWorldDataCodexTurnState::Completed;
	StatusMessage.bFailed = Status.State == EWorldDataCodexTurnState::Failed;
	StatusMessage.bStreaming = !StatusMessage.bCompleted && !StatusMessage.bFailed;
	if (StatusMessage.bCompleted || StatusMessage.bFailed)
	{
		bDeferredToolActivityRefreshPending = false;
		const FDateTime CompletedAt = FDateTime::Now();
		StatusMessage.CompletedAt = CompletedAt;
		CompleteActiveAssistantMessage(StatusMessage.bFailed, CompletedAt);
	}

	SetLastAction(FText::FromString(Status.Message));
}

void SUnrealAgentMCPPanel::HandleAcpError(const FString& Text)
{
	ClearPendingPermission();
	CompleteActiveAssistantMessage(true, FDateTime::Now());
	AppendConversationText(FString::Printf(TEXT("\n\n[错误] %s\n"), *Text));
	SetLastAction(FText::FromString(Text));
	PersistConversationHistory();
}

void SUnrealAgentMCPPanel::SyncVisibleConversationFromModel(const bool bScrollToEnd)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return;
	}
	const FConversation& Conversation = Conversations[ActiveConversationIndex];
	bool bRequiresMessageRebuild = ConversationMessages.Num() != Conversation.Messages.Num();
	if (!bRequiresMessageRebuild)
	{
		for (int32 Index = 0; Index < ConversationMessages.Num(); ++Index)
		{
			const FConversationMessage& Previous = ConversationMessages[Index];
			const FConversationMessage& Current = Conversation.Messages[Index];
			if (Previous.Id != Current.Id || Previous.Role != Current.Role || Previous.bCompleted != Current.bCompleted || Previous.bFailed != Current.bFailed ||
				(Current.Role == EConversationMessageRole::Tool && (Previous.Text != Current.Text || Previous.ToolState != Current.ToolState)))
			{
				bRequiresMessageRebuild = true;
				break;
			}
		}
	}
	bDeferredToolActivityRefreshPending = false;
	ConversationMessages = Conversation.Messages;
	ConversationTranscript = Conversation.Transcript;
	ActiveAssistantMessageIndex = Conversation.ActiveAssistantMessageIndex;
	ActiveTurnStatusMessageIndex = Conversation.ActiveTurnStatusMessageIndex;
	CurrentDetailText = Conversation.Transcript;
	bShowSettings = false;
	bShowDetail = !Conversation.Messages.IsEmpty();
	bDetailIsConversation = true;
	if (bRequiresMessageRebuild)
	{
		RebuildConversationMessages(bScrollToEnd);
	}
	else
	{
		for (const FConversationMessage& Message : ConversationMessages)
		{
			if (Message.Role != EConversationMessageRole::Assistant && Message.Role != EConversationMessageRole::Status)
			{
				continue;
			}
			const TWeakPtr<SUnrealAgentMCPMarkdown>* MarkdownWidget = ConversationMessageMarkdownWidgets.Find(Message.Id);
			if (MarkdownWidget)
			{
				if (const TSharedPtr<SUnrealAgentMCPMarkdown> Widget = MarkdownWidget->Pin())
				{
					Widget->SetMarkdown(Message.Text);
				}
			}
		}
		if (bScrollToEnd && bConversationTailFollowEnabled)
		{
			ScheduleConversationTailScroll();
		}
	}
	RebuildComposerQueue();
	RebuildSidebar();
}

void SUnrealAgentMCPPanel::ApplyAcpTextToConversation(const int32 ConversationIndex, const FString& Text, const TOptional<EUnrealAgentACPProvider> Provider)
{
	if (!Conversations.IsValidIndex(ConversationIndex) || Text.IsEmpty())
	{
		return;
	}
	const FString SafeText = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Text);
	if (ConversationIndex == ActiveConversationIndex)
	{
		CallbackProviderOverride = Provider;
		HandleAcpText(SafeText);
		CallbackProviderOverride.Reset();
		ScheduleConversationSave();
		return;
	}

	FConversation& Conversation = Conversations[ConversationIndex];
	EConversationMessageRole EventRole = EConversationMessageRole::System;
	FString EventText;
	if (UnrealAgentMCPConversationModel::TryParseTaggedEvent(SafeText, EventRole, EventText))
	{
		if (Conversation.Messages.IsValidIndex(Conversation.ActiveAssistantMessageIndex))
		{
			Conversation.Messages[Conversation.ActiveAssistantMessageIndex].bStreaming = false;
		}
		Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
		FConversationMessage Message;
		Message.Role = EventRole;
		Message.Text = EventText;
		Conversation.Messages.Add(MoveTemp(Message));
		Conversation.Transcript += FString::Printf(TEXT("\n\n[%s] %s\n"),
			EventRole == EConversationMessageRole::Error      ? TEXT("错误")
				: EventRole == EConversationMessageRole::Tool ? TEXT("工具")
															  : TEXT("系统"),
			*EventText);
	}
	else
	{
		if (!Conversation.Messages.IsValidIndex(Conversation.ActiveAssistantMessageIndex))
		{
			if (!Conversation.Transcript.IsEmpty())
			{
				Conversation.Transcript += FString::Printf(TEXT("\n\n%s："),
					*(Provider.IsSet() ? UnrealAgentACPProviderModel::GetProvider(Provider.GetValue()).DisplayName.ToString() : FString(TEXT("Unreal Agent"))));
			}
			FConversationMessage Message;
			Message.Role = EConversationMessageRole::Assistant;
			Message.bStreaming = true;
			Conversation.ActiveAssistantMessageIndex = Conversation.Messages.Add(MoveTemp(Message));
		}
		FConversationMessage& Message = Conversation.Messages[Conversation.ActiveAssistantMessageIndex];
		Message.Text += SafeText;
		Message.bStreaming = true;
		Conversation.Transcript += SafeText;
	}
	Conversation.UpdatedAt = FDateTime::Now();
}

void SUnrealAgentMCPPanel::ApplyAcpToolCallToConversation(const int32 ConversationIndex, const FUnrealAgentAcpToolCallUpdate& Update)
{
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return;
	}
	if (ConversationIndex == ActiveConversationIndex)
	{
		HandleAcpToolCall(Update);
		return;
	}
	FConversation& Conversation = Conversations[ConversationIndex];
	int32 ToolIndex = Conversation.Messages.IndexOfByPredicate(
		[&Update](const FConversationMessage& Message)
		{
			return Message.Role == EConversationMessageRole::Tool &&
				((!Update.ToolCallId.IsEmpty() && Message.ToolCallId == Update.ToolCallId) ||
					(Update.ToolCallId.IsEmpty() && Message.bStreaming && Message.ToolName == UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Update.Title)));
		});
	if (ToolIndex == INDEX_NONE)
	{
		if (Conversation.Messages.IsValidIndex(Conversation.ActiveAssistantMessageIndex))
		{
			Conversation.Messages[Conversation.ActiveAssistantMessageIndex].bStreaming = false;
		}
		Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
		FConversationMessage Message;
		Message.Role = EConversationMessageRole::Tool;
		ToolIndex = Conversation.Messages.Add(MoveTemp(Message));
	}
	FConversationMessage& Tool = Conversation.Messages[ToolIndex];
	Tool.ToolCallId = Update.ToolCallId;
	Tool.ToolName = UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Update.Title.IsEmpty() ? Update.ToolCallId : Update.Title);
	Tool.ToolState = Update.State == EUnrealAgentAcpToolCallState::Completed ? EWorldDataConversationToolState::Completed
		: Update.State == EUnrealAgentAcpToolCallState::Failed               ? EWorldDataConversationToolState::Failed
																			 : EWorldDataConversationToolState::Running;
	Tool.bStreaming = Update.State == EUnrealAgentAcpToolCallState::Running;
	Tool.bCompleted = Update.State == EUnrealAgentAcpToolCallState::Completed;
	Tool.bFailed = Update.State == EUnrealAgentAcpToolCallState::Failed;
	if (!Tool.bStreaming)
	{
		Tool.CompletedAt = FDateTime::Now();
	}
	Tool.Text = UnrealAgentMCPConversationModel::BuildToolCallDisplayText(Tool.ToolName, Tool.ToolState);
	if (Tool.bFailed)
	{
		Tool.Text += UnrealAgentMCPConversationModel::BuildToolCallFailureDetails(Update.CanonicalAction, Update.Code, Update.SchemaErrorPaths, Update.TraceId);
	}
	Conversation.Transcript += FString::Printf(TEXT("\n\n[工具] %s"), *Tool.Text);
	Conversation.UpdatedAt = FDateTime::Now();
}

void SUnrealAgentMCPPanel::ApplyAcpTurnStatusToConversation(const int32 ConversationIndex, const FWorldDataCodexTurnStatus& Status,
	const TOptional<EUnrealAgentACPProvider> Provider)
{
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return;
	}
	const bool bTerminal = Status.State == EWorldDataCodexTurnState::Completed || Status.State == EWorldDataCodexTurnState::Failed;
	if (ConversationIndex == ActiveConversationIndex)
	{
		CallbackProviderOverride = Provider;
		HandleAcpTurnStatus(Status);
		CallbackProviderOverride.Reset();
		SaveActiveConversation();
	}
	else
	{
		FConversation& Conversation = Conversations[ConversationIndex];
		if (!Conversation.Messages.IsValidIndex(Conversation.ActiveTurnStatusMessageIndex))
		{
			FConversationMessage Message;
			Message.Role = EConversationMessageRole::Status;
			Conversation.ActiveTurnStatusMessageIndex = Conversation.Messages.Add(MoveTemp(Message));
		}
		FConversationMessage& StatusMessage = Conversation.Messages[Conversation.ActiveTurnStatusMessageIndex];
		StatusMessage.Text = Status.Message;
		StatusMessage.bCompleted = Status.State == EWorldDataCodexTurnState::Completed;
		StatusMessage.bFailed = Status.State == EWorldDataCodexTurnState::Failed;
		StatusMessage.bStreaming = !bTerminal;
		if (bTerminal)
		{
			const FDateTime CompletedAt = FDateTime::Now();
			StatusMessage.CompletedAt = CompletedAt;
			if (Conversation.Messages.IsValidIndex(Conversation.ActiveAssistantMessageIndex))
			{
				FConversationMessage& Assistant = Conversation.Messages[Conversation.ActiveAssistantMessageIndex];
				Assistant.bStreaming = false;
				Assistant.bCompleted = !StatusMessage.bFailed;
				Assistant.bFailed = StatusMessage.bFailed;
				Assistant.CompletedAt = CompletedAt;
				Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
			}
		}
	}

	FConversation& Conversation = Conversations[ConversationIndex];
	if (bTerminal)
	{
		Conversation.bIsRunning = false;
		Conversation.bHasUnreadCompletion =
			UnrealAgentMCPConversationModel::ShouldMarkCompletionUnread(ConversationIndex == ActiveConversationIndex, !Conversation.QueuedPrompts.IsEmpty());
		Conversation.ActiveTurnStatusMessageIndex = INDEX_NONE;
		if (TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id); Runtime && Runtime->IsValid())
		{
			(*Runtime)->PendingPermission.Reset();
			(*Runtime)->PendingPermissionProvider.Reset();
		}
		ScheduleQueuedPromptDispatch(Conversation.Id);
	}
	Conversation.UpdatedAt = FDateTime::Now();
	if (ConversationIndex == ActiveConversationIndex)
	{
		SyncVisibleConversationFromModel();
		SetLastAction(FText::FromString(Status.Message));
	}
	else
	{
		RebuildSidebar();
	}
	ConversationRepository->SaveAsync(UnrealAgentMCP::GetConversationHistoryFilePath(), Conversations, ActiveConversationIndex);
}

void SUnrealAgentMCPPanel::ApplyAcpErrorToConversation(const int32 ConversationIndex, const FString& Text, const TOptional<EUnrealAgentACPProvider> Provider)
{
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return;
	}
	if (ConversationIndex == ActiveConversationIndex)
	{
		CallbackProviderOverride = Provider;
		HandleAcpError(Text);
		CallbackProviderOverride.Reset();
		SaveActiveConversation();
		return;
	}
	ApplyAcpTextToConversation(ConversationIndex, FString::Printf(TEXT("\n\n[错误] %s\n"), *Text), Provider);
}

void SUnrealAgentMCPPanel::ScheduleQueuedPromptDispatch(const FGuid& ConversationId)
{
	TWeakPtr<SUnrealAgentMCPPanel> WeakThis = SharedThis(this);
	AsyncTask(ENamedThreads::GameThread,
		[WeakThis, ConversationId]()
		{
			if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = WeakThis.Pin())
			{
				Panel->TryDispatchNextQueuedPrompt(ConversationId);
			}
		});
}

void SUnrealAgentMCPPanel::TryDispatchNextQueuedPrompt(const FGuid& ConversationId)
{
	const int32 Index = FindConversationIndex(ConversationId);
	if (!Conversations.IsValidIndex(Index))
	{
		return;
	}
	FConversation& Conversation = Conversations[Index];
	const bool bEditingQueueHead = !Conversation.QueuedPrompts.IsEmpty() && Conversation.QueuedPrompts[0].Id == EditingQueuedPromptId;
	if (!UnrealAgentMCPConversationModel::ShouldDispatchQueuedPrompt(Conversation.bIsRunning, IsConversationRunning(ConversationId), Conversation.QueuedPrompts.IsEmpty(),
			bEditingQueueHead))
	{
		return;
	}
	FWorldDataQueuedPrompt Prompt = MoveTemp(Conversation.QueuedPrompts[0]);
	Conversation.QueuedPrompts.RemoveAt(0);
	if (!DispatchPrompt(Index, Prompt))
	{
		Conversation.QueuedPrompts.Insert(MoveTemp(Prompt), 0);
	}
	if (Index == ActiveConversationIndex)
	{
		RebuildComposerQueue();
	}
	PersistConversationHistory();
}

FString SUnrealAgentMCPPanel::BuildContextContinuitySnapshot() const
{
	const FString ProjectInfo = ApplicationService->GetProjectInfoJson();
	const FString Bootstrap = ApplicationService->ReadResource(TEXT("worlddata://context/bootstrap"));
	return FString::Printf(TEXT("capturedAtUtc: %s\n"
								"architecture: UnrealAgentMCPCore owns protocol, policy, execution and task state; "
								"UnrealAgentProviderSDK owns the stable provider registration contract; "
								"UnrealAgentMCPEditor owns Editor adapters, ACP/HTTP infrastructure and Slate presentation.\n"
								"continuityRules: Preserve established decisions, active task state, explicit user scope and unresolved blockers. "
								"Treat dynamic Editor facts as a captured snapshot and refresh the recommended read-only resources before mutation.\n"
								"projectInfo: %s\n"
								"liveBootstrap: %s"),
		*FDateTime::UtcNow().ToIso8601(), *ProjectInfo, *Bootstrap);
}

void SUnrealAgentMCPPanel::ApplyPanelNavigation(const EUnrealAgentMCPPanelNavigationEvent Event)
{
	const FUnrealAgentMCPPanelNavigationState NextState = UnrealAgentMCPPanelNavigation::Resolve({ bShowTaskPanel, bShowSettings }, Event);
	bShowTaskPanel = NextState.bShowTaskPanel;
	bShowSettings = NextState.bShowSettings;
}

void SUnrealAgentMCPPanel::CancelPromptMediaLoad(const FGuid& ConversationId, const bool bRequeuePrompt)
{
	TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId);
	if (!Runtime || !Runtime->IsValid())
	{
		return;
	}

	const bool bHadMediaLoad = (*Runtime)->MediaLoadCancellation.IsValid() || (*Runtime)->PendingMediaPrompt.IsSet();
	++(*Runtime)->MediaLoadGeneration;
	if ((*Runtime)->MediaLoadCancellation.IsValid())
	{
		(*Runtime)->MediaLoadCancellation->Request();
		(*Runtime)->MediaLoadCancellation.Reset();
	}
	const int32 ConversationIndex = FindConversationIndex(ConversationId);
	if (bRequeuePrompt && (*Runtime)->PendingMediaPrompt.IsSet())
	{
		if (Conversations.IsValidIndex(ConversationIndex))
		{
			const int32 InsertIndex = Conversations[ConversationIndex].QueuedPrompts.IsEmpty() ? 0 : 1;
			Conversations[ConversationIndex].QueuedPrompts.Insert(MoveTemp((*Runtime)->PendingMediaPrompt.GetValue()), InsertIndex);
		}
	}
	(*Runtime)->PendingMediaPrompt.Reset();
	if (!bHadMediaLoad || !Conversations.IsValidIndex(ConversationIndex))
	{
		return;
	}

	// 旧异步回调会因 generation 不匹配直接返回，必须在这里结束回合，
	// 否则引导取消后 bIsRunning 一直为 true，队列无法继续。
	FConversation& Conversation = Conversations[ConversationIndex];
	Conversation.bIsRunning = false;
	if (Conversation.Messages.IsValidIndex(Conversation.ActiveTurnStatusMessageIndex))
	{
		FConversationMessage& StatusMessage = Conversation.Messages[Conversation.ActiveTurnStatusMessageIndex];
		if (StatusMessage.bStreaming)
		{
			StatusMessage.bStreaming = false;
			StatusMessage.bCompleted = true;
			StatusMessage.Text = TEXT("已取消媒体准备。");
			StatusMessage.CompletedAt = FDateTime::Now();
		}
		Conversation.ActiveTurnStatusMessageIndex = INDEX_NONE;
	}
	if (ConversationIndex == ActiveConversationIndex)
	{
		SyncVisibleConversationFromModel();
	}
}

void SUnrealAgentMCPPanel::CompletePromptMediaLoad(const FGuid ConversationId, const int32 Generation, FWorldDataQueuedPrompt Prompt, const bool bSucceeded,
	FUnrealAgentPreparedPromptMedia Media, FString Error)
{
	const int32 ConversationIndex = FindConversationIndex(ConversationId);
	TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId);
	if (!Conversations.IsValidIndex(ConversationIndex) || !Runtime || !Runtime->IsValid() || (*Runtime)->MediaLoadGeneration != Generation)
	{
		return;
	}

	(*Runtime)->MediaLoadCancellation.Reset();
	(*Runtime)->PendingMediaPrompt.Reset();
	if (!bSucceeded)
	{
		Conversations[ConversationIndex].bIsRunning = false;
		ApplyAcpErrorToConversation(ConversationIndex, Error, ProviderFromId(Prompt.ProviderId));
		ScheduleQueuedPromptDispatch(ConversationId);
		return;
	}

	if (!FinishDispatchPreparedPrompt(ConversationIndex, Prompt, MoveTemp(Media)))
	{
		Conversations[ConversationIndex].bIsRunning = false;
		Conversations[ConversationIndex].QueuedPrompts.Insert(MoveTemp(Prompt), 0);
		if (ConversationIndex == ActiveConversationIndex)
		{
			RebuildComposerQueue();
		}
		PersistConversationHistory();
	}
}

bool SUnrealAgentMCPPanel::DispatchPrompt(const int32 ConversationIndex, const FWorldDataQueuedPrompt& Prompt)
{
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return false;
	}
	FConversation& Conversation = Conversations[ConversationIndex];
	const EUnrealAgentACPProvider Provider = ProviderFromId(Prompt.ProviderId);
	const TSharedPtr<FUnrealAgentCodexACPClient> Client = GetOrCreateConversationAcpClient(Conversation.Id, Provider);
	if (!Client.IsValid() || !Client->CanLaunchAgent())
	{
		return false;
	}

	TArray<FString> AttachmentPaths;
	for (const FWorldDataConversationAttachment& Attachment : Prompt.Attachments)
	{
		AttachmentPaths.Add(Attachment.Path);
	}
	const bool bNeedsBackgroundMediaLoad = AttachmentPaths.ContainsByPredicate(
		[](const FString& Path)
		{
			return FUnrealAgentPromptMediaLoader::IsSupportedImagePath(Path) || FUnrealAgentPromptMediaLoader::IsSupportedVideoPath(Path);
		});
	if (bNeedsBackgroundMediaLoad)
	{
		TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id);
		if (!Runtime || !Runtime->IsValid())
		{
			return false;
		}

		ApplyPanelNavigation(EUnrealAgentMCPPanelNavigationEvent::PromptDispatchStarted);
		Conversation.bIsRunning = true;
		if (Conversation.Messages.IsValidIndex(Conversation.ActiveTurnStatusMessageIndex))
		{
			FConversationMessage& PreviousStatus = Conversation.Messages[Conversation.ActiveTurnStatusMessageIndex];
			PreviousStatus.bStreaming = false;
			PreviousStatus.bCompleted = true;
			PreviousStatus.CompletedAt = FDateTime::Now();
		}
		FConversationMessage StatusMessage;
		StatusMessage.Role = EConversationMessageRole::Status;
		StatusMessage.Text = TEXT("正在准备媒体附件…");
		StatusMessage.bStreaming = true;
		Conversation.ActiveTurnStatusMessageIndex = Conversation.Messages.Add(MoveTemp(StatusMessage));
		Conversation.UpdatedAt = FDateTime::Now();
		if (ConversationIndex == ActiveConversationIndex)
		{
			SyncVisibleConversationFromModel();
		}
		else
		{
			RebuildSidebar();
		}

		const int32 Generation = ++(*Runtime)->MediaLoadGeneration;
		(*Runtime)->PendingMediaPrompt = Prompt;
		TWeakPtr<SUnrealAgentMCPPanel> WeakThis = SharedThis(this);
		(*Runtime)->MediaLoadCancellation = FUnrealAgentPromptMediaLoader::LoadAsync(AttachmentPaths,
			[WeakThis, ConversationId = Conversation.Id, Generation, Prompt](const bool bSucceeded, FUnrealAgentPreparedPromptMedia Media, FString Error)
			{
				if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = WeakThis.Pin())
				{
					Panel->CompletePromptMediaLoad(ConversationId, Generation, Prompt, bSucceeded, MoveTemp(Media), MoveTemp(Error));
				}
			});
		return true;
	}

	FUnrealAgentPreparedPromptMedia PreparedMedia;
	return FinishDispatchPreparedPrompt(ConversationIndex, Prompt, MoveTemp(PreparedMedia));
}

bool SUnrealAgentMCPPanel::FinishDispatchPreparedPrompt(const int32 ConversationIndex, const FWorldDataQueuedPrompt& Prompt, FUnrealAgentPreparedPromptMedia PreparedMedia)
{
	if (!Conversations.IsValidIndex(ConversationIndex))
	{
		return false;
	}
	FConversation& Conversation = Conversations[ConversationIndex];
	const EUnrealAgentACPProvider Provider = ProviderFromId(Prompt.ProviderId);
	const TSharedPtr<FUnrealAgentCodexACPClient> Client = GetOrCreateConversationAcpClient(Conversation.Id, Provider);
	if (!Client.IsValid() || !Client->CanLaunchAgent())
	{
		return false;
	}

	ApplyPanelNavigation(EUnrealAgentMCPPanelNavigationEvent::PromptDispatchStarted);
	TArray<FString> AttachmentPaths;
	for (const FWorldDataConversationAttachment& Attachment : Prompt.Attachments)
	{
		AttachmentPaths.Add(Attachment.Path);
	}
	FString AgentPrompt = UnrealAgentMCPConversationModel::BuildAttachmentAwarePrompt(Prompt.Text, AttachmentPaths);
	if (!PreparedMedia.ContextText.IsEmpty())
	{
		AgentPrompt += TEXT("\n\n") + PreparedMedia.ContextText;
	}
	UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
	const bool bCompacted = UnrealAgentMCPConversationModel::ShouldAutoCompactContext(Conversation) &&
		UnrealAgentMCPConversationModel::CompactConversationContext(Conversation, 8, 48000, BuildContextContinuitySnapshot());
	TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id);
	bool* bContextReplayPending = nullptr;
	if (Runtime && Runtime->IsValid())
	{
		bContextReplayPending = Provider == EUnrealAgentACPProvider::Cursor ? &(*Runtime)->bCursorContextReplayPending : &(*Runtime)->bCodexContextReplayPending;
	}
	const bool bModelChanged = !Prompt.ModelId.IsEmpty() && !Client->GetAppliedModelId().IsEmpty() && Prompt.ModelId != Client->GetAppliedModelId();
	const bool bNeedsNewSession = bCompacted || bModelChanged || !Client->HasSession();
	if (bNeedsNewSession)
	{
		Client->BeginNewSession();
		if (bContextReplayPending)
		{
			*bContextReplayPending = true;
		}
	}
	if (!Conversation.Messages.IsEmpty() && (bNeedsNewSession || (bContextReplayPending && *bContextReplayPending)))
	{
		AgentPrompt = UnrealAgentMCPConversationModel::BuildConversationContextReplayPrompt(Conversation, AgentPrompt);
	}
	Client->SetExecutionPolicy(static_cast<EWorldDataAgentMode>(FMath::Clamp(static_cast<int32>(Prompt.AgentMode), 0, 2)),
		static_cast<EWorldDataApprovalPolicy>(FMath::Clamp(static_cast<int32>(Prompt.ApprovalPolicy), 0, 2)),
		static_cast<EWorldDataSelfRepairPolicy>(FMath::Clamp(static_cast<int32>(Prompt.SelfRepairPolicy), 0, 2)));
	if (!Prompt.ModelId.IsEmpty())
	{
		Client->SetConfigOption(TEXT("model"), Prompt.ModelId);
	}
	if (!Prompt.ReasoningEffort.IsEmpty())
	{
		Client->SetConfigOption(TEXT("model_reasoning_effort"), Prompt.ReasoningEffort);
	}
	if (!Prompt.ServiceTier.IsEmpty())
	{
		Client->SetConfigOption(TEXT("service_tier"), Prompt.ServiceTier);
	}

	if (Conversation.Messages.IsValidIndex(Conversation.ActiveTurnStatusMessageIndex))
	{
		FConversationMessage& PreviousStatus = Conversation.Messages[Conversation.ActiveTurnStatusMessageIndex];
		PreviousStatus.bStreaming = false;
		PreviousStatus.bCompleted = true;
		PreviousStatus.CompletedAt = FDateTime::Now();
	}
	Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
	FConversationMessage UserMessage;
	UserMessage.Role = EConversationMessageRole::User;
	UserMessage.Text = Prompt.Text;
	UserMessage.Attachments = Prompt.Attachments;
	Conversation.Messages.Add(MoveTemp(UserMessage));
	FConversationMessage StatusMessage;
	StatusMessage.Role = EConversationMessageRole::Status;
	StatusMessage.Text = FString::Printf(TEXT("正在发送到 %s…"), *UnrealAgentACPProviderModel::GetProvider(Provider).DisplayName.ToString());
	StatusMessage.bStreaming = true;
	Conversation.ActiveTurnStatusMessageIndex = Conversation.Messages.Add(MoveTemp(StatusMessage));
	const FString TranscriptUserMessage = UnrealAgentMCPConversationModel::BuildAttachmentDisplayMessage(Prompt.Text, AttachmentPaths);
	if (!Conversation.bHasCustomTitle)
	{
		Conversation.Title = MakeConversationTitle(TranscriptUserMessage);
		Conversation.bHasCustomTitle = true;
	}
	if (!Conversation.Transcript.IsEmpty())
	{
		Conversation.Transcript += TEXT("\n\n");
	}
	Conversation.Transcript += FString::Printf(TEXT("你：%s"), *TranscriptUserMessage);
	Conversation.bIsRunning = true;
	Conversation.bHasUnreadCompletion = false;
	Conversation.UpdatedAt = FDateTime::Now();
	if (ConversationIndex == ActiveConversationIndex)
	{
		SyncVisibleConversationFromModel();
	}
	else
	{
		RebuildSidebar();
	}
	UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
	ConversationRepository->SaveAsync(UnrealAgentMCP::GetConversationHistoryFilePath(), Conversations, ActiveConversationIndex);
	TArray<FUnrealAgentAcpPromptImage> PromptImages;
	PromptImages.Reserve(PreparedMedia.Images.Num());
	for (FUnrealAgentPreparedPromptImage& Image : PreparedMedia.Images)
	{
		FUnrealAgentAcpPromptImage PromptImage;
		PromptImage.MimeType = MoveTemp(Image.MimeType);
		PromptImage.Base64Data = MoveTemp(Image.Base64Data);
		PromptImages.Add(MoveTemp(PromptImage));
	}
	Client->SendPrompt(AgentPrompt, PromptImages);
	return true;
}

FReply SUnrealAgentMCPPanel::OnAllowPermissionClicked()
{
	return ResolvePendingPermission(true);
}

FReply SUnrealAgentMCPPanel::OnDenyPermissionClicked()
{
	return ResolvePendingPermission(false);
}

FReply SUnrealAgentMCPPanel::ResolvePendingPermission(bool bAllow)
{
	if (!bHasPendingPermission)
	{
		return FReply::Handled();
	}

	const FString SelectedOptionId = bAllow ? PendingAllowOptionId : PendingDenyOptionId;
	const TSharedPtr<FUnrealAgentCodexACPClient> PermissionClient = PendingPermissionProvider.IsSet() ? GetAcpClientForProvider(PendingPermissionProvider.GetValue()) : AcpClient;
	if (PermissionClient.IsValid())
	{
		PermissionClient->RespondToPermission(PendingPermissionId, SelectedOptionId);
	}
	if (Conversations.IsValidIndex(ActiveConversationIndex))
	{
		if (TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversations[ActiveConversationIndex].Id); Runtime && Runtime->IsValid())
		{
			(*Runtime)->PendingPermission.Reset();
			(*Runtime)->PendingPermissionProvider.Reset();
		}
	}

	ClearPendingPermission();
	SetLastAction(bAllow ? LOCTEXT("PermissionAllowedAction", "已允许权限请求。") : LOCTEXT("PermissionDeniedAction", "已拒绝权限请求。"));
	return FReply::Handled();
}

void SUnrealAgentMCPPanel::ClearPendingPermission()
{
	bHasPendingPermission = false;
	PendingPermissionId = 0;
	PendingPermissionProvider.Reset();
	PendingPermissionTitle.Empty();
	PendingPermissionToolName.Empty();
	PendingAllowOptionId.Empty();
	PendingDenyOptionId.Empty();
}

void SUnrealAgentMCPPanel::ResetConversationView()
{
	SaveActiveConversation();
	EditingUserMessageIndex = INDEX_NONE;
	bEditingUserMessageBranchRemoved = false;
	EditingQueuedPromptId.Invalidate();
	ClearPendingAttachments();

	FConversation NewConversation;
	NewConversation.Title = LOCTEXT("NewConversationTitle", "新对话");
	NewConversation.CreatedAt = FDateTime::Now();
	NewConversation.UpdatedAt = NewConversation.CreatedAt;
	NewConversation.ControllerId = UnrealAgentMCPPanelSettings::LexToString(ControllerMode);
	NewConversation.ProviderId = ProviderToId(GetModelCatalogProvider());
	NewConversation.AgentMode = static_cast<uint8>(CurrentAgentMode);
	NewConversation.ApprovalPolicy = static_cast<uint8>(CurrentApprovalPolicy);
	NewConversation.SelfRepairPolicy = static_cast<uint8>(CurrentSelfRepairPolicy);
	NewConversation.ContextTokenCapacity = CurrentContextTokenCapacity;
	Conversations.Insert(NewConversation, 0);
	ActiveConversationIndex = 0;
	ActivateConversationAcpClients(NewConversation.Id, false);
	PlayContentTransition();
	ScheduleDeferredConversationStartup(NewConversation.Id);

	bShowSettings = false;
	bShowDetail = false;
	bDetailIsConversation = false;
	ClearPendingPermission();
	CurrentDetailText.Empty();
	ConversationTranscript.Empty();
	ConversationMessages.Empty();
	bReplayConversationContextOnNextPrompt = false;
	ActiveAssistantMessageIndex = INDEX_NONE;
	ActiveTurnStatusMessageIndex = INDEX_NONE;
	RebuildConversationMessages();
	RebuildComposerQueue();
	RebuildSidebar();
	if (DetailTitleText.IsValid())
	{
		DetailTitleText->SetText(LOCTEXT("NewConversationTitle", "新对话"));
	}
	if (DetailTextBox.IsValid())
	{
		DetailTextBox->SetText(FText::GetEmpty());
	}
	if (ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::GetEmpty());
	}
	SetLastAction(LOCTEXT("NewConversationAction", "新任务已就绪，可立即发送。"));
}

void SUnrealAgentMCPPanel::SaveActiveConversation()
{
	bDeferredConversationSavePending = false;
	if (Conversations.IsValidIndex(ActiveConversationIndex))
	{
		FConversation& Conversation = Conversations[ActiveConversationIndex];
		Conversation.Messages = ConversationMessages;
		Conversation.Transcript = ConversationTranscript;
		Conversation.ActiveAssistantMessageIndex = ActiveAssistantMessageIndex;
		Conversation.ActiveTurnStatusMessageIndex = ActiveTurnStatusMessageIndex;
		Conversation.UpdatedAt = FDateTime::Now();
		Conversation.ControllerId = UnrealAgentMCPPanelSettings::LexToString(ControllerMode);
		Conversation.ProviderId = ProviderToId(GetModelCatalogProvider());
		Conversation.AgentMode = static_cast<uint8>(CurrentAgentMode);
		Conversation.ApprovalPolicy = static_cast<uint8>(CurrentApprovalPolicy);
		Conversation.SelfRepairPolicy = static_cast<uint8>(CurrentSelfRepairPolicy);
	}
}

void SUnrealAgentMCPPanel::LoadConversationHistory()
{
	int32 RestoredIndex = INDEX_NONE;
	if (!ConversationRepository->Load(UnrealAgentMCP::GetConversationHistoryFilePath(), Conversations, RestoredIndex) || Conversations.IsEmpty())
	{
		Conversations.Reset();
		ActiveConversationIndex = INDEX_NONE;
		return;
	}

	for (FConversation& Conversation : Conversations)
	{
		Conversation.ContextTokenCapacity = CurrentContextTokenCapacity;
		UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
	}

	if (!Conversations.IsValidIndex(RestoredIndex) || Conversations[RestoredIndex].bArchived)
	{
		RestoredIndex = UnrealAgentMCPConversationModel::FindFirstUnarchivedConversation(Conversations);
	}
	if (RestoredIndex == INDEX_NONE)
	{
		bShowArchivedConversations = true;
		RestoredIndex = 0;
	}

	for (int32 Index = 0; Index < Conversations.Num(); ++Index)
	{
		FConversation& Conversation = Conversations[Index];
		const EUnrealAgentControllerMode ConversationController =
			UnrealAgentMCPPanelSettings::ResolveConversationControllerMode(Conversation.ControllerId, Conversation.ProviderId, ControllerMode, Index == RestoredIndex);
		Conversation.ControllerId = UnrealAgentMCPPanelSettings::LexToString(ConversationController);
		Conversation.ProviderId = UnrealAgentMCPPanelSettings::ResolveControllerAcpProviderId(ConversationController, Conversation.ProviderId);
	}

	ActiveConversationIndex = INDEX_NONE;
	LoadConversation(RestoredIndex);
	SetLastAction(LOCTEXT("ConversationHistoryRestoredAction", "已恢复上次关闭编辑器前的对话。"));
}

void SUnrealAgentMCPPanel::PersistConversationHistory(const bool bSynchronous)
{
	bDeferredToolActivityRefreshPending = false;
	SaveActiveConversation();
	if (bSynchronous)
	{
		ConversationRepository->Save(UnrealAgentMCP::GetConversationHistoryFilePath(), Conversations, ActiveConversationIndex);
	}
	else
	{
		ConversationRepository->SaveAsync(UnrealAgentMCP::GetConversationHistoryFilePath(), Conversations, ActiveConversationIndex);
	}
}

void SUnrealAgentMCPPanel::LoadConversation(int32 Index)
{
	if (!Conversations.IsValidIndex(Index))
	{
		return;
	}
	SaveActiveConversation();
	EditingUserMessageIndex = INDEX_NONE;
	bEditingUserMessageBranchRemoved = false;
	EditingQueuedPromptId.Invalidate();
	ClearPendingAttachments();
	ActiveConversationIndex = Index;

	FConversation& Conversation = Conversations[Index];
	ControllerMode = UnrealAgentMCPPanelSettings::ResolveConversationControllerMode(Conversation.ControllerId, Conversation.ProviderId, ControllerMode, true);
	SanitizeRetiredNativeController();
	Conversation.ControllerId = UnrealAgentMCPPanelSettings::LexToString(ControllerMode);
	Conversation.ProviderId = UnrealAgentMCPPanelSettings::ResolveControllerAcpProviderId(ControllerMode, Conversation.ProviderId);
	ActiveProvider = ProviderFromId(Conversation.ProviderId);
	const FUnrealAgentACPAccountState Account = ActiveProvider == EUnrealAgentACPProvider::Cursor
		? FUnrealAgentACPAccountState{ bCursorAuthenticated ? TEXT("Cursor 已登录") : TEXT("Cursor 账户"), TEXT("由 Cursor Agent CLI 提供"), bCursorAuthenticated }
		: UnrealAgentACPProviderModel::DetectAccountState(EUnrealAgentACPProvider::Codex);
	DetectedAccountLabel = Account.DisplayLabel;
	DetectedAccountSecondaryLabel = Account.SecondaryLabel;
	if (ActiveProvider == EUnrealAgentACPProvider::Codex)
	{
		bCodexAuthenticated = Account.bAuthenticated;
	}
	CurrentAgentMode = static_cast<EWorldDataAgentMode>(FMath::Clamp(static_cast<int32>(Conversation.AgentMode), 0, 2));
	CurrentApprovalPolicy = static_cast<EWorldDataApprovalPolicy>(FMath::Clamp(static_cast<int32>(Conversation.ApprovalPolicy), 0, 2));
	CurrentSelfRepairPolicy = static_cast<EWorldDataSelfRepairPolicy>(FMath::Clamp(static_cast<int32>(Conversation.SelfRepairPolicy), 0, 2));
	ActivateConversationAcpClients(Conversation.Id, false);
	ApplyExecutionPolicy();
	SaveSettings();
	PlayContentTransition();
	ScheduleDeferredConversationStartup(Conversation.Id);
	Conversation.bHasUnreadCompletion = false;
	ConversationMessages = Conversation.Messages;
	ConversationTranscript = Conversation.Transcript;
	bReplayConversationContextOnNextPrompt = !ConversationMessages.IsEmpty();
	ActiveAssistantMessageIndex = Conversation.ActiveAssistantMessageIndex;
	ActiveTurnStatusMessageIndex = Conversation.ActiveTurnStatusMessageIndex;
	CurrentDetailText = Conversation.Transcript;

	bShowSettings = false;
	bShowDetail = ConversationMessages.Num() > 0;
	bDetailIsConversation = true;
	ClearPendingPermission();
	if (const TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(Conversation.Id); Runtime && Runtime->IsValid() && (*Runtime)->PendingPermission.IsSet())
	{
		PendingPermissionProvider = (*Runtime)->PendingPermissionProvider;
		HandleAcpPermission((*Runtime)->PendingPermission.GetValue());
	}

	RebuildConversationMessages();
	RebuildComposerQueue();
	RebuildSidebar();
	if (DetailTitleText.IsValid())
	{
		DetailTitleText->SetText(
			ConversationMessages.Num() > 0 ? FText::Format(LOCTEXT("ProviderConversationTitle", "{0} 会话"), GetProviderDisplayName()) : LOCTEXT("NewConversationTitle", "新对话"));
	}
	if (ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::GetEmpty());
	}
	SetLastAction(FText::Format(LOCTEXT("SwitchedConversationAction", "已切换到对话：{0}，可立即发送。"), GetConversationTitle(Index)));
}

FReply SUnrealAgentMCPPanel::OnSelectConversation(int32 Index)
{
	if (Index != ActiveConversationIndex)
	{
		LoadConversation(Index);
	}
	else if (Conversations.IsValidIndex(Index))
	{
		const bool bReturnToConversation = bShowSettings || (bShowDetail && !bDetailIsConversation);
		const bool bClearUnread = Conversations[Index].bHasUnreadCompletion;
		if (bReturnToConversation)
		{
			bShowSettings = false;
			bDetailIsConversation = true;
			bShowDetail = !ConversationMessages.IsEmpty();
			if (bShowDetail)
			{
				RebuildConversationMessages();
			}
			PlayContentTransition();
			SetLastAction(LOCTEXT("CurrentConversationRestoredAction", "已返回当前对话。"));
		}
		if (bClearUnread)
		{
			Conversations[Index].bHasUnreadCompletion = false;
			RebuildSidebar();
			PersistConversationHistory();
		}
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCopyAssistantMessageClicked(FString MarkdownText)
{
	FPlatformApplicationMisc::ClipboardCopy(*MarkdownText);
	SetLastAction(LOCTEXT("AssistantReplyCopiedAction", "已复制这条回复的原始 Markdown。"));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCopyUserMessageClicked(const int32 UserMessageIndex)
{
	if (!ConversationMessages.IsValidIndex(UserMessageIndex) || ConversationMessages[UserMessageIndex].Role != EConversationMessageRole::User)
	{
		return FReply::Handled();
	}

	FPlatformApplicationMisc::ClipboardCopy(*ConversationMessages[UserMessageIndex].Text);
	SetLastAction(LOCTEXT("UserPromptCopiedAction", "已复制这条提示词。"));
	return FReply::Handled();
}

bool SUnrealAgentMCPPanel::CanEditUserMessage(const int32 UserMessageIndex) const
{
	return ConversationMessages.IsValidIndex(UserMessageIndex) && ConversationMessages[UserMessageIndex].Role == EConversationMessageRole::User &&
		Conversations.IsValidIndex(ActiveConversationIndex);
}

bool SUnrealAgentMCPPanel::CanModifyUserMessage(const int32 UserMessageIndex) const
{
	return CanEditUserMessage(UserMessageIndex) && !Conversations[ActiveConversationIndex].bIsRunning;
}

FReply SUnrealAgentMCPPanel::OnEditUserMessageClicked(const int32 UserMessageIndex)
{
	if (!CanEditUserMessage(UserMessageIndex))
	{
		return FReply::Handled();
	}

	const FConversationMessage UserMessage = ConversationMessages[UserMessageIndex];
	const FGuid ConversationId = Conversations[ActiveConversationIndex].Id;
	const bool bInterruptedRunningTurn = Conversations[ActiveConversationIndex].bIsRunning || IsConversationRunning(ConversationId);

	if (!RemoveConversationFromUserMessage(UserMessageIndex))
	{
		SetLastAction(LOCTEXT("EditUserPromptBranchChangedAction", "原提示词已发生变化，无法返回编辑。"));
		return FReply::Handled();
	}

	EditingUserMessageIndex = UserMessageIndex;
	bEditingUserMessageBranchRemoved = true;
	PendingAttachmentPaths.Reset();
	for (const FWorldDataConversationAttachment& Attachment : UserMessage.Attachments)
	{
		if (!Attachment.bInline && !Attachment.Path.IsEmpty() &&
			!PendingAttachmentPaths.ContainsByPredicate(
				[&Attachment](const FString& ExistingPath)
				{
					return ExistingPath.Equals(Attachment.Path, ESearchCase::IgnoreCase);
				}))
		{
			PendingAttachmentPaths.Add(Attachment.Path);
		}
	}
	RebuildComposerAttachments();
	if (ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::FromString(UnrealAgentMCPConversationModel::BuildComposerRichTextForEditing(UserMessage.Text, UserMessage.Attachments)));
		ComposerTextBox->GoTo(ETextLocation::EndOfDocument);
	}
	RebuildConversationMessages(false);
	RebuildComposerQueue();
	RebuildSidebar();
	PersistConversationHistory();
	SetLastAction(bInterruptedRunningTurn ? LOCTEXT("EditingUserPromptInterruptedAction", "已中断当前回复并清除这一轮及之后的上下文；提示词已返回输入框。")
										  : LOCTEXT("EditingUserPromptAction", "已清除这一轮及之后的上下文；提示词已返回输入框。"));
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnCancelUserMessageEditClicked()
{
	const bool bWasEditingUserMessage = EditingUserMessageIndex != INDEX_NONE;
	const bool bWasEditingQueuedPrompt = EditingQueuedPromptId.IsValid();
	EditingUserMessageIndex = INDEX_NONE;
	bEditingUserMessageBranchRemoved = false;
	EditingQueuedPromptId.Invalidate();
	if ((bWasEditingUserMessage || bWasEditingQueuedPrompt) && ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::GetEmpty());
	}
	if (bWasEditingUserMessage || bWasEditingQueuedPrompt)
	{
		ClearPendingAttachments();
	}
	RebuildConversationMessages(false);
	SetLastAction(bWasEditingUserMessage ? LOCTEXT("UserPromptEditCanceledAfterRewriteAction", "已取消编辑；被清除的对话分支不会恢复。")
										 : LOCTEXT("UserPromptEditCanceledAction", "已取消编辑，原消息保持不变。"));
	return HandledReplyWithComposerFocus();
}

bool SUnrealAgentMCPPanel::RemoveConversationFromUserMessage(const int32 UserMessageIndex)
{
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return false;
	}

	FConversation& Conversation = Conversations[ActiveConversationIndex];
	Conversation.Messages = ConversationMessages;
	if (UnrealAgentMCPConversationModel::PrepareConversationForUserMessageRewrite(Conversation, UserMessageIndex) != EWorldDataConversationRewriteResult::Prepared)
	{
		return false;
	}
	ConversationMessages = Conversation.Messages;
	ConversationTranscript = Conversation.Transcript;
	Conversation.UpdatedAt = FDateTime::Now();
	ResetConversationAcpRuntimeForHistoryRewrite(Conversation.Id);
	EditingQueuedPromptId.Invalidate();
	ClearPendingPermission();
	ActiveAssistantMessageIndex = INDEX_NONE;
	ActiveTurnStatusMessageIndex = INDEX_NONE;
	ToolGroupExpansionOverrides.Reset();
	CurrentDetailText = ConversationTranscript;
	bReplayConversationContextOnNextPrompt = !ConversationMessages.IsEmpty();

	const bool bHasEarlierUserMessage = ConversationMessages.ContainsByPredicate(
		[](const FConversationMessage& Message)
		{
			return Message.Role == EConversationMessageRole::User;
		});
	if (!bHasEarlierUserMessage && Conversations.IsValidIndex(ActiveConversationIndex))
	{
		Conversations[ActiveConversationIndex].Title = LOCTEXT("NewConversationTitle", "新对话");
		Conversations[ActiveConversationIndex].bHasCustomTitle = false;
	}
	return true;
}

FReply SUnrealAgentMCPPanel::OnDeleteUserMessageClicked(const int32 UserMessageIndex)
{
	if (!CanModifyUserMessage(UserMessageIndex))
	{
		SetLastAction(FText::Format(LOCTEXT("DeleteUserPromptWhileBusyAction", "{0} 正在处理消息，完成后才能删除历史提示词。"), GetProviderDisplayName()));
		return FReply::Handled();
	}

	const EAppReturnType::Type Confirmation =
		FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("DeleteUserPromptConfirmation", "删除这条提示词会同时删除它之后的所有回复、工具记录和后续对话，且无法撤销。是否继续？"),
			LOCTEXT("DeleteUserPromptConfirmationTitle", "删除这轮对话"));
	if (Confirmation != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const bool bWasEditingUserMessage = EditingUserMessageIndex != INDEX_NONE;
	EditingUserMessageIndex = INDEX_NONE;
	bEditingUserMessageBranchRemoved = false;
	if (bWasEditingUserMessage && ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::GetEmpty());
	}
	if (bWasEditingUserMessage)
	{
		ClearPendingAttachments();
	}
	if (!RemoveConversationFromUserMessage(UserMessageIndex))
	{
		return FReply::Handled();
	}

	bShowSettings = false;
	bDetailIsConversation = true;
	bShowDetail = !ConversationMessages.IsEmpty();
	RebuildConversationMessages(false);
	RebuildSidebar();
	if (DetailTitleText.IsValid() && ConversationMessages.IsEmpty())
	{
		DetailTitleText->SetText(LOCTEXT("NewConversationTitle", "新对话"));
	}
	PersistConversationHistory();
	SetLastAction(LOCTEXT("UserPromptDeletedAction", "已删除这条提示词及其后的全部对话。"));
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnUserMessageRightClicked(const FGeometry&, const FPointerEvent& MouseEvent, const int32 UserMessageIndex)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !ConversationMessages.IsValidIndex(UserMessageIndex) ||
		ConversationMessages[UserMessageIndex].Role != EConversationMessageRole::User)
	{
		return FReply::Unhandled();
	}

	const TWeakPtr<SUnrealAgentMCPPanel> WeakThis = SharedThis(this);
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(LOCTEXT("CopyUserPromptMenu", "复制"), LOCTEXT("CopyUserPromptMenuTooltip", "复制这条提示词"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateLambda(
			[WeakThis, UserMessageIndex]
			{
				if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = WeakThis.Pin())
				{
					Panel->OnCopyUserMessageClicked(UserMessageIndex);
				}
			})));
	MenuBuilder.AddMenuEntry(LOCTEXT("EditUserPromptMenu", "返回并编辑"), LOCTEXT("EditUserPromptMenuTooltip", "中断当前回合，清除后续上下文并将提示词和附件返回输入框"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateLambda(
					  [WeakThis, UserMessageIndex]
					  {
						  if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = WeakThis.Pin())
						  {
							  Panel->OnEditUserMessageClicked(UserMessageIndex);
						  }
					  }),
			FCanExecuteAction::CreateSP(this, &SUnrealAgentMCPPanel::CanEditUserMessage, UserMessageIndex)));
	MenuBuilder.AddMenuEntry(LOCTEXT("DeleteUserPromptMenu", "删除"), LOCTEXT("DeleteUserPromptMenuTooltip", "删除这条提示词及其后的全部对话"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(FExecuteAction::CreateLambda(
					  [WeakThis, UserMessageIndex]
					  {
						  if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = WeakThis.Pin())
						  {
							  Panel->OnDeleteUserMessageClicked(UserMessageIndex);
						  }
					  }),
			FCanExecuteAction::CreateSP(this, &SUnrealAgentMCPPanel::CanModifyUserMessage, UserMessageIndex)));

	const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnBranchConversationClicked(const int32 AssistantMessageIndex)
{
	if (Conversations.IsValidIndex(ActiveConversationIndex) && HasConversationPendingWork(Conversations[ActiveConversationIndex].Id))
	{
		SetLastAction(FText::Format(LOCTEXT("BranchConversationWhileBusyAction", "{0} 正在处理消息，完成后才能创建分支。"), GetProviderDisplayName()));
		return FReply::Handled();
	}

	SaveActiveConversation();
	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		return FReply::Handled();
	}

	FConversation Branch;
	if (!UnrealAgentMCPConversationModel::TryCreateConversationBranch(Conversations[ActiveConversationIndex], AssistantMessageIndex, FDateTime::Now(), Branch))
	{
		SetLastAction(LOCTEXT("ConversationBranchFailedAction", "只能从一条已完成的助手回复创建新任务。"));
		return FReply::Handled();
	}

	// 新分支显示在侧栏最上方；插入后先修正原会话下标，确保
	// LoadConversation 保存的仍是原会话，而不是覆盖相邻记录。
	Conversations.Insert(MoveTemp(Branch), 0);
	++ActiveConversationIndex;
	LoadConversation(0);
	SetLastAction(LOCTEXT("ConversationBranchedAction", "已从这条回复创建独立新任务，并继承此前对话历史。"));
	PersistConversationHistory();
	return HandledReplyWithComposerFocus();
}

FText SUnrealAgentMCPPanel::GetConversationTitle(int32 Index) const
{
	return Conversations.IsValidIndex(Index) ? Conversations[Index].Title : LOCTEXT("NewConversationTitle", "新对话");
}

FText SUnrealAgentMCPPanel::MakeConversationTitle(const FString& Message) const
{
	const FString Title = UnrealAgentMCPConversationModel::BuildConversationTitleCandidate(Message, 18);
	return Title.IsEmpty() ? LOCTEXT("NewConversationTitle", "新对话") : FText::FromString(Title);
}

FText SUnrealAgentMCPPanel::GetConversationAgeText(FDateTime CreatedAt) const
{
	const int32 Seconds = FMath::Max(0, static_cast<int32>((FDateTime::Now() - CreatedAt).GetTotalSeconds()));
	if (Seconds < 5)
	{
		return LOCTEXT("AgeJustNow", "刚刚");
	}
	if (Seconds < 60)
	{
		return FText::FromString(FString::Printf(TEXT("%ds"), Seconds));
	}
	const int32 Minutes = Seconds / 60;
	if (Minutes < 60)
	{
		return FText::FromString(FString::Printf(TEXT("%dm"), Minutes));
	}
	const int32 Hours = Minutes / 60;
	if (Hours < 24)
	{
		return FText::FromString(FString::Printf(TEXT("%dh"), Hours));
	}
	return FText::FromString(FString::Printf(TEXT("%dd"), Hours / 24));
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildConversationEntry(int32 Index)
	{
		const FConversation& Conversation = Conversations[Index];
		const FGuid ConversationId = Conversation.Id;
		const TAttribute<bool> bRunning = TAttribute<bool>::CreateLambda(
			[this, ConversationId]
			{
				return HasConversationPendingWork(ConversationId);
			});
		const EWorldDataConversationIndicator Indicator =
			UnrealAgentMCPConversationModel::ResolveConversationIndicator(
				bRunning.Get(),
				Conversation.bHasUnreadCompletion,
				Index == ActiveConversationIndex);
		const FDateTime CreatedAt = Conversation.CreatedAt;
		TSharedRef<SWidget> ConversationItem = BuildConversationItem(
				Conversation.Title,
				TAttribute<FText>::Create(
					TAttribute<FText>::FGetter::CreateSP(
						this,
						&SUnrealAgentMCPPanel::GetConversationAgeText,
						CreatedAt)),
				Index == ActiveConversationIndex,
				bRunning,
				Indicator == EWorldDataConversationIndicator::UnreadCompletion,
				CanArchiveConversationEntry(Index),
				FOnClicked::CreateSP(
					this,
					&SUnrealAgentMCPPanel::OnSelectConversation,
					Index),
				Conversation.bArchived
					? FOnClicked()
					: FOnClicked::CreateSP(
						this,
						&SUnrealAgentMCPPanel::OnArchiveConversationEntryClicked,
						Index));
		if (!Conversation.bArchived)
		{
			return ConversationItem;
		}

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				ConversationItem
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildIconTextButton(
					LOCTEXT("RestoreConversationIcon", "↥"),
					FOnClicked::CreateSP(
						this,
						&SUnrealAgentMCPPanel::OnRestoreConversationClicked,
						Index),
					LOCTEXT(
						"RestoreConversationTooltip",
						"恢复该归档对话"))
			];
	}

void SUnrealAgentMCPPanel::RebuildSidebar()
{
	if (!SidebarListScrollBox.IsValid())
	{
		return;
	}

	SidebarListScrollBox->ClearChildren();

	const FDateTime Now = FDateTime::Now();
	const FDateTime TodayStart(Now.GetYear(), Now.GetMonth(), Now.GetDay());

	int32 TodayCount = 0;
	int32 RecentCount = 0;
	for (int32 Index = 0; Index < Conversations.Num(); ++Index)
	{
		if (Conversations[Index].bArchived)
		{
			continue;
		}

		if (const bool bIsToday = Conversations[Index].CreatedAt >= TodayStart)
		{
			if (TodayCount == 0)
			{
				SidebarListScrollBox->AddSlot().Padding(8.0f, 10.0f, 8.0f, 0.0f)[BuildDateLabel(LOCTEXT("TodayLabel", "今天"))];
			}

			SidebarListScrollBox->AddSlot().Padding(6.0f, TodayCount == 0 ? 6.0f : 2.0f, 6.0f, 0.0f)[BuildConversationEntry(Index)];
			++TodayCount;
		}
		else
		{
			if (RecentCount == 0)
			{
				SidebarListScrollBox->AddSlot().Padding(8.0f, 14.0f, 8.0f, 0.0f)[BuildDateLabel(LOCTEXT("RecentLabel", "最近"))];
			}

			SidebarListScrollBox->AddSlot().Padding(6.0f, RecentCount == 0 ? 6.0f : 2.0f, 6.0f, 0.0f)[BuildConversationEntry(Index)];
			++RecentCount;
		}
	}

	if (bShowArchivedConversations && GetArchivedConversationCount() > 0)
	{
		SidebarListScrollBox->AddSlot().Padding(8.0f, 14.0f, 8.0f, 0.0f)[BuildDateLabel(LOCTEXT("ArchivedConversationSection", "已归档"))];
		for (int32 Index = 0; Index < Conversations.Num(); ++Index)
		{
			if (!Conversations[Index].bArchived)
			{
				continue;
			}
			SidebarListScrollBox->AddSlot().Padding(6.0f, 4.0f, 6.0f, 0.0f)[BuildConversationEntry(Index)];
		}
	}
}

FReply SUnrealAgentMCPPanel::OnToggleArchivedConversationsClicked()
{
	bShowArchivedConversations = !bShowArchivedConversations;
	RebuildSidebar();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnRestoreConversationClicked(int32 Index)
{
	if (!Conversations.IsValidIndex(Index) || !Conversations[Index].bArchived)
	{
		return FReply::Handled();
	}

	Conversations[Index].bArchived = false;
	Conversations[Index].UpdatedAt = FDateTime::Now();
	LoadConversation(Index);
	RebuildSidebar();
	PersistConversationHistory();
	SetLastAction(LOCTEXT("ConversationRestoredAction", "已恢复归档对话。"));
	return FReply::Handled();
}

int32 SUnrealAgentMCPPanel::GetArchivedConversationCount() const
{
	int32 Count = 0;
	for (const FConversation& Conversation : Conversations)
	{
		Count += Conversation.bArchived ? 1 : 0;
	}
	return Count;
}

void SUnrealAgentMCPPanel::ShowProjectInfo()
{
	SetDetail(LOCTEXT("ProjectInfoTitle", "项目信息"), ApplicationService->GetProjectInfoJson());
}

FReply SUnrealAgentMCPPanel::OnNewConversationClicked()
{
	ResetConversationView();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnArchiveConversationClicked()
{
	return OnArchiveConversationEntryClicked(ActiveConversationIndex);
}

FReply SUnrealAgentMCPPanel::OnArchiveConversationEntryClicked(const int32 Index)
{
	if (!CanArchiveConversationEntry(Index))
	{
		return FReply::Handled();
	}

	const FGuid ConversationId = Conversations[Index].Id;
	const bool bIsActiveConversation = Index == ActiveConversationIndex;
	const bool bWasBusy = Conversations[Index].bIsRunning || !Conversations[Index].QueuedPrompts.IsEmpty() || HasConversationPendingWork(ConversationId);

	// 未完成也可归档：先中止该对话上的 ACP 回合与队列。
	if (TSharedPtr<FConversationAcpRuntime>* Runtime = ConversationAcpRuntimes.Find(ConversationId); Runtime && Runtime->IsValid())
	{
		CancelPromptMediaLoad(ConversationId, false);
		for (const TSharedPtr<FUnrealAgentCodexACPClient>& Client : { (*Runtime)->CodexClient, (*Runtime)->CursorClient })
		{
			if (!Client.IsValid())
			{
				continue;
			}
			if (Client->HasActiveTurn())
			{
				Client->CancelActivePrompt();
			}
		}
		(*Runtime)->PendingPermission.Reset();
		(*Runtime)->PendingPermissionProvider.Reset();
	}

	Conversations[Index].QueuedPrompts.Reset();
	Conversations[Index].bIsRunning = false;
	if (bIsActiveConversation)
	{
		EditingQueuedPromptId.Invalidate();
		ClearPendingPermission();
		RebuildComposerQueue();
		bShowTaskPanel = false;
	}

	SaveActiveConversation();
	const int32 ArchivedIndex = Index;
	const FText ArchivedTitle = Conversations[ArchivedIndex].Title;
	Conversations[ArchivedIndex].bArchived = true;
	Conversations[ArchivedIndex].UpdatedAt = FDateTime::Now();

	if (ArchivedIndex == ActiveConversationIndex)
	{
		const int32 NextIndex = UnrealAgentMCPConversationModel::FindFirstUnarchivedConversation(Conversations, ArchivedIndex);
		if (NextIndex != INDEX_NONE)
		{
			LoadConversation(NextIndex);
		}
		else
		{
			ResetConversationView();
		}
	}

	RebuildSidebar();
	PersistConversationHistory();
	SetLastAction(FText::Format(bWasBusy ? LOCTEXT("ConversationArchivedAndAbortedAction", "已中止任务并归档对话：{0}") : LOCTEXT("ConversationArchivedAction", "已归档对话：{0}"),
		ArchivedTitle));
	return FReply::Handled();
}

bool SUnrealAgentMCPPanel::CanArchiveConversation() const
{
	return CanArchiveConversationEntry(ActiveConversationIndex);
}

bool SUnrealAgentMCPPanel::CanArchiveConversationEntry(const int32 Index) const
{
	return Conversations.IsValidIndex(Index) && !Conversations[Index].bArchived;
}

FReply SUnrealAgentMCPPanel::OnSettingsClicked()
{
	bShowSettings = true;
	PlayContentTransition();
	SetLastAction(LOCTEXT("SettingsOpenedAction", "已打开设置。"));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnSendClicked()
{
	FString Message;
	TArray<FWorldDataComposerInlineAttachment> InlineAttachments;
	if (ComposerTextBox.IsValid())
	{
		UnrealAgentMCPConversationModel::ParseComposerRichText(ComposerTextBox->GetText().ToString(), Message, InlineAttachments);
	}
	const FString TrimmedCommand = Message.TrimStartAndEnd();
	if (PendingAttachmentPaths.IsEmpty() && InlineAttachments.IsEmpty() &&
		(TrimmedCommand.Equals(TEXT("/context"), ESearchCase::IgnoreCase) || TrimmedCommand.Equals(TEXT("/compact"), ESearchCase::IgnoreCase)))
	{
		if (!Conversations.IsValidIndex(ActiveConversationIndex))
		{
			ResetConversationView();
		}
		SaveActiveConversation();
		FConversation& Conversation = Conversations[ActiveConversationIndex];
		UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
		if (TrimmedCommand.Equals(TEXT("/compact"), ESearchCase::IgnoreCase))
		{
			const bool bCompacted = UnrealAgentMCPConversationModel::CompactConversationContext(Conversation, 4, 48000, BuildContextContinuitySnapshot());
			ResetConversationAcpRuntimeForHistoryRewrite(Conversation.Id);
			FConversationMessage StatusMessage;
			StatusMessage.Role = EConversationMessageRole::System;
			StatusMessage.Text =
				bCompacted ? UnrealAgentMCPConversationModel::BuildConversationContextStatus(Conversation) : TEXT("当前历史仍在保留窗口内，无需进一步压缩；代理上下文已刷新。");
			StatusMessage.bCompleted = true;
			StatusMessage.CompletedAt = FDateTime::Now();
			Conversation.Messages.Add(MoveTemp(StatusMessage));
			Conversation.Transcript = UnrealAgentMCPConversationModel::BuildConversationHistorySnapshot(Conversation.Messages);
			SyncVisibleConversationFromModel();
			PersistConversationHistory();
			SetLastAction(LOCTEXT("ConversationContextCompactedAction", "上下文已压缩并切换到新的代理上下文。"));
		}
		else
		{
			SetLastAction(FText::FromString(UnrealAgentMCPConversationModel::BuildConversationContextStatus(Conversation)));
		}
		if (ComposerTextBox.IsValid())
		{
			ComposerTextBox->SetText(FText::GetEmpty());
		}
		return HandledReplyWithComposerFocus();
	}
	if (Message.IsEmpty() && PendingAttachmentPaths.IsEmpty() && InlineAttachments.IsEmpty())
	{
		SetLastAction(LOCTEXT("EmptyMessageAction", "请输入消息或添加附件后再发送。"));
		return HandledReplyWithComposerFocus();
	}

	SanitizeRetiredNativeController();

	if (!UnrealAgentACPProviderModel::GetProvider(GetModelCatalogProvider()).bSupportsEmbeddedConversation)
	{
		SetLastAction(LOCTEXT("ProviderDoesNotEmbedConversation", "当前 Provider 通过外部 CLI 使用 MCP，不提供内嵌会话。"));
		return HandledReplyWithComposerFocus();
	}

	TArray<FString> Attachments = PendingAttachmentPaths;
	for (const FWorldDataComposerInlineAttachment& InlineAttachment : InlineAttachments)
	{
		if (!Attachments.ContainsByPredicate(
				[&InlineAttachment](const FString& ExistingPath)
				{
					return ExistingPath.Equals(InlineAttachment.Path, ESearchCase::IgnoreCase);
				}))
		{
			Attachments.Add(InlineAttachment.Path);
		}
	}

	for (const FString& AttachmentPath : Attachments)
	{
		if (!IFileManager::Get().FileExists(*AttachmentPath) && !IFileManager::Get().DirectoryExists(*AttachmentPath))
		{
			SetLastAction(FText::Format(LOCTEXT("AttachmentMissingAction", "附件文件或文件夹不存在，或已被移动：{0}"), FText::FromString(AttachmentPath)));
			return HandledReplyWithComposerFocus();
		}
	}

	TArray<FWorldDataConversationAttachment> ConversationAttachments;
	ConversationAttachments.Reserve(PendingAttachmentPaths.Num() + InlineAttachments.Num());
	for (const FString& AttachmentPath : PendingAttachmentPaths)
	{
		FWorldDataConversationAttachment Attachment;
		Attachment.Path = AttachmentPath;
		Attachment.DisplayName = FPaths::GetCleanFilename(AttachmentPath);
		if (Attachment.DisplayName.IsEmpty())
		{
			Attachment.DisplayName = AttachmentPath;
		}
		Attachment.bDirectory = IFileManager::Get().DirectoryExists(*AttachmentPath);
		ConversationAttachments.Add(MoveTemp(Attachment));
	}
	for (const FWorldDataComposerInlineAttachment& InlineAttachment : InlineAttachments)
	{
		const bool bAlreadyAdded = ConversationAttachments.ContainsByPredicate(
			[&InlineAttachment](const FWorldDataConversationAttachment& ExistingAttachment)
			{
				return ExistingAttachment.Path.Equals(InlineAttachment.Path, ESearchCase::IgnoreCase);
			});
		if (bAlreadyAdded)
		{
			continue;
		}

		FWorldDataConversationAttachment Attachment;
		Attachment.Path = InlineAttachment.Path;
		Attachment.DisplayName = InlineAttachment.DisplayName;
		Attachment.bDirectory = InlineAttachment.bDirectory;
		Attachment.bInline = true;
		ConversationAttachments.Add(MoveTemp(Attachment));
	}

	if (EditingUserMessageIndex != INDEX_NONE)
	{
		const int32 UserMessageIndex = EditingUserMessageIndex;
		if (!bEditingUserMessageBranchRemoved && !RemoveConversationFromUserMessage(UserMessageIndex))
		{
			SetLastAction(LOCTEXT("EditedUserPromptMissingAction", "原提示词已发生变化，无法替换；请重新选择要编辑的提示词。"));
			return HandledReplyWithComposerFocus();
		}
		EditingUserMessageIndex = INDEX_NONE;
		bEditingUserMessageBranchRemoved = false;
	}

	if (!Conversations.IsValidIndex(ActiveConversationIndex))
	{
		ResetConversationView();
	}
	FConversation& Conversation = Conversations[ActiveConversationIndex];
	FWorldDataQueuedPrompt Prompt = BuildQueuedPrompt(Message, ConversationAttachments);
	if (ComposerTextBox.IsValid())
	{
		ComposerTextBox->SetText(FText::GetEmpty());
	}
	ClearPendingAttachments();

	if (EditingQueuedPromptId.IsValid())
	{
		FWorldDataQueuedPrompt* Existing = Conversation.QueuedPrompts.FindByPredicate(
			[this](const FWorldDataQueuedPrompt& Candidate)
			{
				return Candidate.Id == EditingQueuedPromptId;
			});
		if (Existing)
		{
			Prompt.Id = Existing->Id;
			Prompt.CreatedAt = Existing->CreatedAt;
			*Existing = MoveTemp(Prompt);
		}
		EditingQueuedPromptId.Invalidate();
		Conversation.UpdatedAt = FDateTime::Now();
		RebuildComposerQueue();
		PersistConversationHistory();
		if (!Conversation.bIsRunning)
		{
			ScheduleQueuedPromptDispatch(Conversation.Id);
		}
		SetLastAction(LOCTEXT("QueuedPromptUpdatedAction", "已更新待执行消息。"));
		return HandledReplyWithComposerFocus();
	}

	if (Conversation.bIsRunning || IsConversationRunning(Conversation.Id))
	{
		Conversation.QueuedPrompts.Add(MoveTemp(Prompt));
		Conversation.UpdatedAt = FDateTime::Now();
		RebuildComposerQueue();
		RebuildSidebar();
		PersistConversationHistory();
		SetLastAction(FText::Format(LOCTEXT("PromptQueuedAction", "当前对话正在执行，消息已加入队列（第 {0} 条）。"), FText::AsNumber(Conversation.QueuedPrompts.Num())));
		return HandledReplyWithComposerFocus();
	}
	if (!Conversation.QueuedPrompts.IsEmpty())
	{
		Conversation.QueuedPrompts.Add(MoveTemp(Prompt));
		Conversation.UpdatedAt = FDateTime::Now();
		RebuildComposerQueue();
		PersistConversationHistory();
		ScheduleQueuedPromptDispatch(Conversation.Id);
		SetLastAction(LOCTEXT("PromptQueuedBehindExistingQueueAction", "消息已加入现有队列，将按原顺序执行。"));
		return HandledReplyWithComposerFocus();
	}

	if (!DispatchPrompt(ActiveConversationIndex, Prompt))
	{
		Conversation.QueuedPrompts.Add(MoveTemp(Prompt));
		RebuildComposerQueue();
		PersistConversationHistory();
		SetLastAction(LOCTEXT("PromptWaitingForAgentAction", "ACP 正在后台准备；消息已保留在队列中。"));
		return HandledReplyWithComposerFocus();
	}
	SetLastAction(FText::Format(LOCTEXT("SendingToAgentAction", "正在发送到 {0} ACP..."), GetProviderDisplayName()));
	bReplayConversationContextOnNextPrompt = false;
	return HandledReplyWithComposerFocus();
}

void SUnrealAgentMCPPanel::OnComposerTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter && UnrealAgentMCPConversationModel::ShouldSubmitComposerEnter(FPlatformTime::Seconds() - LastComposerTextChangeSeconds))
	{
		OnSendClicked();
	}
}

void SUnrealAgentMCPPanel::OnComposerTextChanged(const FText&)
{
	LastComposerTextChangeSeconds = FPlatformTime::Seconds();
}

FReply SUnrealAgentMCPPanel::OnComposerKeyDown(const FGeometry&, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::V && KeyEvent.IsControlDown() && !KeyEvent.IsAltDown())
	{
		TArray<FString> ClipboardPaths;
		if (TryReadClipboardAttachmentPaths(ClipboardPaths))
		{
			bool bRejectedByMediaLimit = false;
			const int32 AddedCount = InsertPendingInlineAttachmentPaths(ClipboardPaths, &bRejectedByMediaLimit);
			SetLastAction(bRejectedByMediaLimit ? FText::Format(LOCTEXT("PastedAttachmentMediaLimitAction", "图片与视频附件最多 {0} 个，超出部分未添加。"),
													  FText::AsNumber(FUnrealAgentPromptMediaLoader::MaximumPromptImages))
					: AddedCount > 0            ? FText::Format(LOCTEXT("PastedAttachmentsAddedAction", "已在光标处粘贴 {0} 个文件或文件夹引用。"), FText::AsNumber(AddedCount))
												: LOCTEXT("PastedAttachmentsNotAddedAction", "剪贴板中的文件或文件夹已经添加，或路径无效。"));
			return HandledReplyWithComposerFocus();
		}
	}

	const bool bComposing = ComposerTextBox.IsValid() && ComposerTextBox->IsInputMethodComposing();
	switch (UnrealAgentMCPConversationModel::ResolveComposerKeyAction(KeyEvent.GetKey() == EKeys::Enter, KeyEvent.IsControlDown(), KeyEvent.IsShiftDown(), bComposing))
	{
	case EWorldDataComposerKeyAction::Consume:
		// 输入法已接收 Enter 确认候选，阻止 Slate 再次提交并重载文本。
		ComposerTextBox->EnsureInputMethodContext();
		return FReply::Handled();
	case EWorldDataComposerKeyAction::InsertNewLine:
		ComposerTextBox->InsertTextAtCursor(TEXT("\n"));
		ComposerTextBox->EnsureInputMethodContext();
		return FReply::Handled();
	case EWorldDataComposerKeyAction::Submit:
		if (!UnrealAgentMCPConversationModel::ShouldSubmitComposerEnter(FPlatformTime::Seconds() - LastComposerTextChangeSeconds))
		{
			// Windows 输入法可能先结束组合态并写入候选词，随后才把同一次
			// Enter 送到 Slate；短窗口保护可避免误发送，并立即恢复输入上下文。
			ComposerTextBox->EnsureInputMethodContext();
			return FReply::Handled();
		}
		return OnSendClicked();
	case EWorldDataComposerKeyAction::PassThrough:
	default:
		return FReply::Unhandled();
	}
}

FReply SUnrealAgentMCPPanel::HandledReplyWithComposerFocus()
{
	FReply Reply = FReply::Handled();
	if (!ComposerTextBox.IsValid() || !UnrealAgentACPProviderModel::GetProvider(GetModelCatalogProvider()).bSupportsEmbeddedConversation)
	{
		return Reply;
	}

	if (!ComposerTextBox->HasKeyboardFocus())
	{
		Reply.SetUserFocus(ComposerTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
	else
	{
		ComposerTextBox->EnsureInputMethodContext();
	}
	return Reply;
}

bool SUnrealAgentMCPPanel::CanSendMessage() const
{
	if (!ComposerTextBox.IsValid() || !UnrealAgentACPProviderModel::GetProvider(GetModelCatalogProvider()).bSupportsEmbeddedConversation || !GetModelConfigClient().IsValid())
	{
		return false;
	}

	FString Message;
	TArray<FWorldDataComposerInlineAttachment> InlineAttachments;
	UnrealAgentMCPConversationModel::ParseComposerRichText(ComposerTextBox->GetText().ToString(), Message, InlineAttachments);
	const bool bHasText = !Message.TrimStartAndEnd().IsEmpty();
	const bool bHasInput = bHasText || !PendingAttachmentPaths.IsEmpty() || !InlineAttachments.IsEmpty();
	return bHasInput && IsProviderDependencyAvailable();
}

FReply SUnrealAgentMCPPanel::OnStartClicked()
{
	if (!ApplicationService->IsServerRunning())
	{
		ApplicationService->StartServer(ApplicationService->LoadConfiguredPort());
	}
	if (ApplicationService->IsServerRunning())
	{
		ApplicationService->RefreshConnectionFiles();
		SetLastAction(LOCTEXT("StartVerifyingAction", "MCP Listener 已启动，正在执行健康检查..."));
		VerifyServerReadinessForPanel(LOCTEXT("StartedAction", "MCP 已就绪并刷新连接文件。"));
	}
	else
	{
		SetLastAction(LOCTEXT("StartFailedAction", "启动失败。"));
	}
	ShowProjectInfo();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnStopClicked()
{
	ApplicationService->StopServer();
	SetLastAction(LOCTEXT("StoppedAction", "已停止。"));
	ShowProjectInfo();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnRefreshClicked()
{
	ApplicationService->RefreshConnectionFiles();
	RefreshCliDetections();
	const EUnrealAgentACPProvider ControllerProvider = GetModelCatalogProvider();
	const TSharedPtr<FUnrealAgentCodexACPClient> ControllerClient = GetAcpClientForProvider(ControllerProvider);
	if (ControllerProvider == EUnrealAgentACPProvider::Codex)
	{
		if (ControllerClient.IsValid())
		{
			ControllerClient->SetCodexCliPath(GetCliEffectivePath(ECliTool::Codex));
			ControllerClient->RefreshModelCatalog();
		}
		RefreshCodexAccountState();
	}
	else
	{
		StartCursorAuthProcess(ECodexAuthAction::Status);
		if (ControllerClient.IsValid())
		{
			ControllerClient->SetAgentProvider(ControllerProvider, GetCliEffectivePath(ECliTool::Cursor));
		}
	}
	RefreshProviderDependencyState();
	if (!ApplicationService->IsServerRunning())
	{
		SetLastAction(LOCTEXT("RefreshServerStoppedAction", "连接文件已刷新，但 MCP 服务器未运行。"));
		ShowProjectInfo();
		return FReply::Handled();
	}
	SetLastAction(LOCTEXT("RefreshVerifyingAction", "连接文件已刷新，正在验证当前代理与 MCP..."));
	VerifyServerReadinessForPanel(LOCTEXT("RefreshedAction", "连接文件已刷新，MCP 已就绪。"), true);
	ShowProjectInfo();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnSetupCliClicked()
{
	if (!ApplicationService->IsServerRunning())
	{
		ApplicationService->StartServer(ApplicationService->LoadConfiguredPort());
	}

	if (!ApplicationService->IsServerRunning())
	{
		SetLastAction(LOCTEXT("SetupCliStartFailedAction", "MCP 服务器启动失败，无法配置 CLI。"));
		SetDetail(LOCTEXT("SetupCliFailTitle", "一键配置 CLI 失败"), ApplicationService->GetStatusJson());
		return FReply::Handled();
	}

	ApplicationService->ConfigureExternalClients();
	SetLastAction(LOCTEXT("SetupCliDoneAction", "已按用户请求同步 Claude Code、Cursor、Codex 外部客户端配置。"));
	UnrealAgentMCP::Notify(LOCTEXT("SetupCliDoneNotification", "外部 CLI 连接配置已同步（Codex/Cursor/Claude Code）。"));
	SetDetail(LOCTEXT("SetupCliReportTitle", "CLI 配置结果"), ApplicationService->GetCliSetupReportJson());
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnStatusClicked()
{
	SetLastAction(LOCTEXT("ViewedStatusAction", "已查看状态。"));
	SetDetail(LOCTEXT("StatusTitle", "服务状态"), ApplicationService->GetStatusJson());
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnProjectInfoClicked()
{
	SetLastAction(LOCTEXT("ViewedProjectInfoAction", "已查看项目信息。"));
	ShowProjectInfo();
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnBootstrapClicked()
{
	SetLastAction(LOCTEXT("ViewedBootstrapAction", "已查看启动上下文。"));
	SetDetail(LOCTEXT("BootstrapTitle", "启动上下文"), ApplicationService->ReadResource(TEXT("worlddata://context/bootstrap")));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnPolicyClicked()
{
	SetLastAction(LOCTEXT("ViewedPolicyAction", "已查看 Codex 策略快照。"));
	SetDetail(LOCTEXT("PolicyTitle", "Codex 策略快照"), ApplicationService->ReadResource(TEXT("worlddata://codex/policy-snapshot")));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnToolsClicked()
{
	SetLastAction(LOCTEXT("ViewedToolsAction", "已查看工具列表。"));
	SetDetail(LOCTEXT("ToolsTitle", "工具列表"), ApplicationService->GetToolDefinitionsJson());
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnResourcesClicked()
{
	SetLastAction(LOCTEXT("ViewedResourcesAction", "已查看资源列表。"));
	SetDetail(LOCTEXT("ResourcesTitle", "资源列表"), ApplicationService->GetResourceListJson());
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCopyUrlClicked()
{
	if (ApplicationService->IsServerRunning())
	{
		UnrealAgentMCP::CopyToClipboard(ApplicationService->GetMcpUrl());
		SetLastAction(LOCTEXT("CopiedUrlAction", "已复制 MCP 地址。"));
		UnrealAgentMCP::Notify(LOCTEXT("CopiedUrlNotification", "MCP 地址已复制。"));
	}
	else
	{
		SetLastAction(LOCTEXT("CopyUrlServerStopped", "无法复制 MCP 地址：服务器尚未运行。"));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCopyConfigClicked()
{
	if (ApplicationService->IsServerRunning())
	{
		UnrealAgentMCP::CopyToClipboard(ApplicationService->BuildClientConfigSnippet());
		SetLastAction(LOCTEXT("CopiedConfigAction", "已复制 MCP 配置。"));
		UnrealAgentMCP::Notify(LOCTEXT("CopiedConfigNotification", "MCP 配置已复制。"));
	}
	else
	{
		SetLastAction(LOCTEXT("CopyConfigServerStopped", "无法复制 MCP 配置：服务器尚未运行。"));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnCopyCurrentClicked()
{
	if (!CurrentDetailText.IsEmpty())
	{
		UnrealAgentMCP::CopyToClipboard(CurrentDetailText);
		SetLastAction(LOCTEXT("CopiedViewAction", "已复制当前内容。"));
		UnrealAgentMCP::Notify(LOCTEXT("CopiedViewNotification", "当前面板内容已复制。"));
	}
	else
	{
		SetLastAction(LOCTEXT("CopyCurrentEmpty", "当前详情为空，没有可复制的内容。"));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnAttachFilesClicked()
{
	FSlateApplication::Get().DismissAllMenus();
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		SetLastAction(LOCTEXT("AttachmentDialogUnavailableAction", "系统文件选择窗口不可用。"));
		return HandledReplyWithComposerFocus();
	}

	const FString InitialDirectory = PendingAttachmentPaths.IsEmpty()
		? FPaths::ProjectDir()
		: (IFileManager::Get().DirectoryExists(*PendingAttachmentPaths.Last()) ? PendingAttachmentPaths.Last() : FPaths::GetPath(PendingAttachmentPaths.Last()));
	TArray<FString> SelectedFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (!DesktopPlatform->OpenFileDialog(ParentWindowHandle, TEXT("添加文件到当前对话"), InitialDirectory, TEXT(""), TEXT("所有文件 (*.*)|*.*"), EFileDialogFlags::Multiple,
			SelectedFiles))
	{
		return HandledReplyWithComposerFocus();
	}

	bool bRejectedByMediaLimit = false;
	const int32 AddedCount = AddPendingAttachmentPaths(SelectedFiles, &bRejectedByMediaLimit);
	if (bRejectedByMediaLimit)
	{
		return HandledReplyWithComposerFocus();
	}
	SetLastAction(AddedCount > 0 ? FText::Format(LOCTEXT("AttachmentsAddedAction", "已向当前消息添加 {0} 个文件或文件夹。"), FText::AsNumber(AddedCount))
								 : LOCTEXT("NoAttachmentsAddedAction", "文件或文件夹已经添加，或路径无效。"));
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnAttachFolderClicked()
{
	FSlateApplication::Get().DismissAllMenus();
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		SetLastAction(LOCTEXT("AttachmentFolderDialogUnavailableAction", "系统文件夹选择窗口不可用。"));
		return HandledReplyWithComposerFocus();
	}

	const FString InitialDirectory = PendingAttachmentPaths.IsEmpty()
		? FPaths::ProjectDir()
		: (IFileManager::Get().DirectoryExists(*PendingAttachmentPaths.Last()) ? PendingAttachmentPaths.Last() : FPaths::GetPath(PendingAttachmentPaths.Last()));
	FString SelectedFolder;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("添加文件夹到当前对话"), InitialDirectory, SelectedFolder))
	{
		return HandledReplyWithComposerFocus();
	}

	TArray<FString> SelectedFolders;
	SelectedFolders.Add(MoveTemp(SelectedFolder));
	const int32 AddedCount = AddPendingAttachmentPaths(SelectedFolders);
	SetLastAction(
		AddedCount > 0 ? LOCTEXT("AttachmentFolderAddedAction", "已向当前消息添加文件夹。") : LOCTEXT("AttachmentFolderNotAddedAction", "该文件夹已经添加，或路径无效。"));
	return HandledReplyWithComposerFocus();
}

int32 SUnrealAgentMCPPanel::AddPendingAttachmentPaths(const TArray<FString>& CandidatePaths, bool* bOutRejectedByMediaLimit)
{
	TArray<FWorldDataComposerInlineAttachment> InlineAttachments;
	if (ComposerTextBox.IsValid())
	{
		FString IgnoredMessage;
		UnrealAgentMCPConversationModel::ParseComposerRichText(ComposerTextBox->GetText().ToString(), IgnoredMessage, InlineAttachments);
	}

	TArray<FString> KnownMediaPaths = PendingAttachmentPaths;
	for (const FWorldDataComposerInlineAttachment& Attachment : InlineAttachments)
	{
		KnownMediaPaths.AddUnique(Attachment.Path);
	}
	int32 MediaCount = FUnrealAgentPromptMediaLoader::CountSupportedMediaPaths(KnownMediaPaths);
	int32 AddedCount = 0;
	bool bRejectedByMediaLimit = false;
	for (FString CandidatePath : CandidatePaths)
	{
		CandidatePath = FPaths::ConvertRelativePathToFull(CandidatePath);
		FPaths::NormalizeFilename(CandidatePath);
		if (!IFileManager::Get().FileExists(*CandidatePath) && !IFileManager::Get().DirectoryExists(*CandidatePath))
		{
			continue;
		}
		if (FUnrealAgentPromptMediaLoader::IsSupportedMediaPath(CandidatePath) && MediaCount >= FUnrealAgentPromptMediaLoader::MaximumPromptImages)
		{
			bRejectedByMediaLimit = true;
			continue;
		}

		const bool bAlreadyAttached = PendingAttachmentPaths.ContainsByPredicate(
			[&CandidatePath](const FString& ExistingPath)
			{
				return ExistingPath.Equals(CandidatePath, ESearchCase::IgnoreCase);
			});
		const bool bAlreadyInline = InlineAttachments.ContainsByPredicate(
			[&CandidatePath](const FWorldDataComposerInlineAttachment& Attachment)
			{
				return Attachment.Path.Equals(CandidatePath, ESearchCase::IgnoreCase);
			});
		if (!bAlreadyAttached && !bAlreadyInline)
		{
			if (FUnrealAgentPromptMediaLoader::IsSupportedMediaPath(CandidatePath))
			{
				++MediaCount;
			}
			PendingAttachmentPaths.Add(MoveTemp(CandidatePath));
			++AddedCount;
		}
	}

	RebuildComposerAttachments();
	if (bOutRejectedByMediaLimit)
	{
		*bOutRejectedByMediaLimit = bRejectedByMediaLimit;
	}
	if (bRejectedByMediaLimit)
	{
		SetLastAction(FText::Format(LOCTEXT("AttachmentMediaLimitAction", "图片与视频附件最多 {0} 个，超出部分未添加。"),
			FText::AsNumber(FUnrealAgentPromptMediaLoader::MaximumPromptImages)));
	}
	return AddedCount;
}

int32 SUnrealAgentMCPPanel::InsertPendingInlineAttachmentPaths(const TArray<FString>& CandidatePaths, bool* bOutRejectedByMediaLimit)
{
	if (bOutRejectedByMediaLimit)
	{
		*bOutRejectedByMediaLimit = false;
	}
	if (!ComposerTextBox.IsValid() || !ComposerTextLayout.IsValid())
	{
		return 0;
	}

	FString ExistingMessage;
	TArray<FWorldDataComposerInlineAttachment> ExistingInlineAttachments;
	UnrealAgentMCPConversationModel::ParseComposerRichText(ComposerTextBox->GetText().ToString(), ExistingMessage, ExistingInlineAttachments);
	TArray<FString> KnownPaths = PendingAttachmentPaths;
	for (const FWorldDataComposerInlineAttachment& Attachment : ExistingInlineAttachments)
	{
		KnownPaths.AddUnique(Attachment.Path);
	}

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const int16 Baseline = FontMeasure->GetBaseline(LightTextBoxStyle.TextStyle.Font);
	int32 MediaCount = FUnrealAgentPromptMediaLoader::CountSupportedMediaPaths(KnownPaths);
	int32 AddedCount = 0;
	bool bRejectedByMediaLimit = false;
	for (FString CandidatePath : CandidatePaths)
	{
		CandidatePath = FPaths::ConvertRelativePathToFull(CandidatePath);
		FPaths::NormalizeFilename(CandidatePath);
		const bool bDirectory = IFileManager::Get().DirectoryExists(*CandidatePath);
		if (!IFileManager::Get().FileExists(*CandidatePath) && !bDirectory)
		{
			continue;
		}
		if (FUnrealAgentPromptMediaLoader::IsSupportedMediaPath(CandidatePath) && MediaCount >= FUnrealAgentPromptMediaLoader::MaximumPromptImages)
		{
			bRejectedByMediaLimit = true;
			continue;
		}

		const bool bAlreadyAttached = KnownPaths.ContainsByPredicate(
			[&CandidatePath](const FString& ExistingPath)
			{
				return ExistingPath.Equals(CandidatePath, ESearchCase::IgnoreCase);
			});
		if (bAlreadyAttached)
		{
			continue;
		}

		const FString AttachmentId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FRunInfo RunInfo(TEXT("attachment"));
		RunInfo.MetaData.Add(TEXT("id"), AttachmentId);
		RunInfo.MetaData.Add(TEXT("path64"), FBase64::Encode(CandidatePath, EBase64Mode::UrlSafe));
		RunInfo.MetaData.Add(TEXT("directory"), bDirectory ? TEXT("1") : TEXT("0"));
		const TSharedRef<FString> RunText = MakeShared<FString>(TEXT("\u200B"));
		const FSlateWidgetRun::FWidgetRunInfo WidgetRunInfo(BuildInlineAttachmentChip(AttachmentId, CandidatePath, bDirectory), Baseline - 2);
		ComposerTextBox->InsertRunAtCursor(FSlateWidgetRun::Create(ComposerTextLayout.ToSharedRef(), RunInfo, RunText, WidgetRunInfo));
		KnownPaths.Add(CandidatePath);
		if (FUnrealAgentPromptMediaLoader::IsSupportedMediaPath(CandidatePath))
		{
			++MediaCount;
		}
		++AddedCount;
	}
	if (bOutRejectedByMediaLimit)
	{
		*bOutRejectedByMediaLimit = bRejectedByMediaLimit;
	}
	return AddedCount;
}

bool SUnrealAgentMCPPanel::CanAcceptAttachmentDrop(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	if (!UnrealAgentACPProviderModel::GetProvider(GetModelCatalogProvider()).bSupportsEmbeddedConversation || !DragDropOperation.IsValid() ||
		!DragDropOperation->IsOfType<FExternalDragOperation>())
	{
		return false;
	}

	const TSharedPtr<FExternalDragOperation> ExternalOperation = StaticCastSharedPtr<FExternalDragOperation>(DragDropOperation);
	if (!ExternalOperation.IsValid() || !ExternalOperation->HasFiles())
	{
		return false;
	}

	for (const FString& CandidatePath : ExternalOperation->GetFiles())
	{
		if (IFileManager::Get().FileExists(*CandidatePath) || IFileManager::Get().DirectoryExists(*CandidatePath))
		{
			return true;
		}
	}
	return false;
}

FReply SUnrealAgentMCPPanel::OnAttachmentPathsDropped(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	bAttachmentDragOver = false;
	const TSharedPtr<FExternalDragOperation> ExternalOperation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (!ExternalOperation.IsValid() || !ExternalOperation->HasFiles())
	{
		return FReply::Unhandled();
	}

	bool bRejectedByMediaLimit = false;
	const int32 AddedCount = InsertPendingInlineAttachmentPaths(ExternalOperation->GetFiles(), &bRejectedByMediaLimit);
	SetLastAction(bRejectedByMediaLimit ? FText::Format(LOCTEXT("InlineAttachmentMediaLimitAction", "图片与视频附件最多 {0} 个，超出部分未添加。"),
											  FText::AsNumber(FUnrealAgentPromptMediaLoader::MaximumPromptImages))
			: AddedCount > 0            ? FText::Format(LOCTEXT("DroppedAttachmentsAddedAction", "已在光标处插入 {0} 个文件或文件夹引用。"), FText::AsNumber(AddedCount))
										: LOCTEXT("DroppedAttachmentsNotAddedAction", "拖入的文件或文件夹已经添加，或路径无效。"));
	return HandledReplyWithComposerFocus();
}

void SUnrealAgentMCPPanel::OnAttachmentDragEntered(const FDragDropEvent& DragDropEvent)
{
	bAttachmentDragOver = CanAcceptAttachmentDrop(DragDropEvent.GetOperation());
}

void SUnrealAgentMCPPanel::OnAttachmentDragLeft(const FDragDropEvent&)
{
	bAttachmentDragOver = false;
}

FReply SUnrealAgentMCPPanel::OnRemoveAttachmentClicked(FString AttachmentPath)
{
	PendingAttachmentPaths.RemoveAll(
		[&AttachmentPath](const FString& ExistingPath)
		{
			return ExistingPath.Equals(AttachmentPath, ESearchCase::IgnoreCase);
		});
	RebuildComposerAttachments();
	SetLastAction(LOCTEXT("AttachmentRemovedAction", "已从当前消息移除附件。"));
	return HandledReplyWithComposerFocus();
}

FReply SUnrealAgentMCPPanel::OnRemoveInlineAttachmentClicked(FString AttachmentId)
{
	if (!ComposerTextBox.IsValid() || AttachmentId.IsEmpty())
	{
		return HandledReplyWithComposerFocus();
	}

	FString RichText = ComposerTextBox->GetText().ToString();
	const FString IdAttribute = FString::Printf(TEXT(" id=\"%s\""), *AttachmentId);
	const int32 IdIndex = RichText.Find(IdAttribute, ESearchCase::CaseSensitive, ESearchDir::FromStart);
	const int32 TagStart = IdIndex == INDEX_NONE ? INDEX_NONE : RichText.Find(TEXT("<attachment"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, IdIndex);
	const int32 TagEnd = IdIndex == INDEX_NONE ? INDEX_NONE : RichText.Find(TEXT("</>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, IdIndex);
	if (TagStart == INDEX_NONE || TagEnd == INDEX_NONE)
	{
		return HandledReplyWithComposerFocus();
	}

	RichText.RemoveAt(TagStart, TagEnd + 3 - TagStart);
	ComposerTextBox->SetText(FText::FromString(RichText));
	ComposerTextBox->GoTo(ETextLocation::EndOfDocument);
	SetLastAction(LOCTEXT("InlineAttachmentRemovedAction", "已从当前光标内容移除行内引用。"));
	return HandledReplyWithComposerFocus();
}

void SUnrealAgentMCPPanel::RebuildComposerAttachments()
{
	if (!ComposerAttachmentsBox.IsValid())
	{
		return;
	}

	ComposerAttachmentsBox->ClearChildren();
	for (const FString& AttachmentPath : PendingAttachmentPaths)
	{
		ComposerAttachmentsBox->AddSlot().Padding(FMargin(0.0f, 0.0f, 6.0f, 6.0f))[BuildAttachmentChip(AttachmentPath)];
	}
}

void SUnrealAgentMCPPanel::ClearPendingAttachments()
{
	PendingAttachmentPaths.Reset();
	RebuildComposerAttachments();
}

FReply SUnrealAgentMCPPanel::OnOpenProjectFolderClicked()
{
	UnrealAgentMCP::ExploreFileParent(ApplicationService->GetClientConfigFilePath());
	SetLastAction(LOCTEXT("OpenedProjectFolderAction", "已打开项目目录。"));
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnOpenSavedFolderClicked()
{
	UnrealAgentMCP::ExploreFileParent(ApplicationService->GetConnectionFilePath());
	SetLastAction(LOCTEXT("OpenedSavedFolderAction", "已打开 Saved 目录。"));
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
