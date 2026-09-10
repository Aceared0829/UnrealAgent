// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPConversationModel.cpp
 * @brief MCP 会话文本纯逻辑实现。
 */

#include "Core/Conversation/UnrealAgentMCPConversationModel.h"
#include "Internationalization/Regex.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"

namespace
{
	FString EscapeComposerRichText(const FString& PlainText)
	{
		FString Escaped = PlainText;
		Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Escaped;
	}

	FString UnescapeComposerRichText(const FString& RichText)
	{
		FString PlainText = RichText;
		PlainText.ReplaceInline(TEXT("&quot;"), TEXT("\""));
		PlainText.ReplaceInline(TEXT("&lt;"), TEXT("<"));
		PlainText.ReplaceInline(TEXT("&gt;"), TEXT(">"));
		PlainText.ReplaceInline(TEXT("&amp;"), TEXT("&"));
		return PlainText;
	}

	bool TryReadComposerMarkupAttribute(const FString& OpeningTag, const FString& AttributeName, FString& OutValue)
	{
		const FString Prefix = FString::Printf(TEXT(" %s=\""), *AttributeName);
		const int32 ValueStart = OpeningTag.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (ValueStart == INDEX_NONE)
		{
			return false;
		}

		const int32 ContentStart = ValueStart + Prefix.Len();
		const int32 ContentEnd = OpeningTag.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
		if (ContentEnd == INDEX_NONE)
		{
			return false;
		}

		OutValue = OpeningTag.Mid(ContentStart, ContentEnd - ContentStart);
		return true;
	}

	FString MakeInlineAttachmentMarker(const FString& DisplayName, const bool bDirectory)
	{
		return (bDirectory ? FString(TEXT("@folder:")) : FString(TEXT("@file:"))) + DisplayName;
	}

	FString ReplaceRegexMatches(const FString& Source, const FString& PatternText, const FString& Replacement)
	{
		FRegexMatcher Matcher(FRegexPattern(PatternText), Source);
		FString Result;
		int32 CopyStart = 0;
		while (Matcher.FindNext())
		{
			const int32 MatchStart = Matcher.GetMatchBeginning();
			const int32 MatchEnd = Matcher.GetMatchEnding();
			Result += Source.Mid(CopyStart, MatchStart - CopyStart);
			Result += Replacement;
			CopyStart = MatchEnd;
		}
		Result += Source.Mid(CopyStart);
		return Result;
	}

	FString BuildConversationHistorySlice(const TArray<FWorldDataConversationMessage>& Messages, const int32 StartIndex)
	{
		FString Result;
		for (int32 Index = FMath::Clamp(StartIndex, 0, Messages.Num()); Index < Messages.Num(); ++Index)
		{
			const FWorldDataConversationMessage& Message = Messages[Index];
			if (Message.Role == EWorldDataConversationMessageRole::Status)
			{
				continue;
			}

			FString MessageText = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Message.Text.TrimStartAndEnd());
			if (Message.Role == EWorldDataConversationMessageRole::User && !Message.Attachments.IsEmpty())
			{
				TArray<FString> AttachmentPaths;
				AttachmentPaths.Reserve(Message.Attachments.Num());
				for (const FWorldDataConversationAttachment& Attachment : Message.Attachments)
				{
					if (!Attachment.Path.IsEmpty())
					{
						AttachmentPaths.Add(Attachment.Path);
					}
				}
				MessageText = UnrealAgentMCPConversationModel::BuildAttachmentAwarePrompt(MessageText, AttachmentPaths);
			}

			if (MessageText.IsEmpty())
			{
				continue;
			}

			FString RoleLabel;
			switch (Message.Role)
			{
			case EWorldDataConversationMessageRole::User:
				RoleLabel = TEXT("用户");
				break;
			case EWorldDataConversationMessageRole::System:
				RoleLabel = TEXT("系统");
				break;
			case EWorldDataConversationMessageRole::Tool:
				RoleLabel = TEXT("工具");
				break;
			case EWorldDataConversationMessageRole::Error:
				RoleLabel = TEXT("错误");
				break;
			case EWorldDataConversationMessageRole::Assistant:
			default:
				RoleLabel = TEXT("助手");
				break;
			}

			if (!Result.IsEmpty())
			{
				Result += TEXT("\n\n");
			}
			Result += FString::Printf(TEXT("%s：\n%s"), *RoleLabel, *MessageText);
		}
		return Result;
	}
}

namespace UnrealAgentMCPConversationModel
{
	bool ShouldUseClarificationDefaults(const FString& UserMessage)
	{
		FString Normalized = UserMessage.TrimStartAndEnd().ToLower();
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("，"), TEXT(""));
		Normalized.ReplaceInline(TEXT(","), TEXT(""));
		Normalized.ReplaceInline(TEXT("。"), TEXT(""));
		Normalized.ReplaceInline(TEXT("！"), TEXT(""));
		Normalized.ReplaceInline(TEXT("!"), TEXT(""));
		Normalized.ReplaceInline(TEXT("？"), TEXT(""));
		Normalized.ReplaceInline(TEXT("?"), TEXT(""));

		if (Normalized.IsEmpty())
		{
			return false;
		}

		static const TCHAR* ExactIntents[] = { TEXT("采用默认"), TEXT("默认"), TEXT("按默认"), TEXT("直接开始"), TEXT("开始吧"), TEXT("继续吧"), TEXT("你决定") };
		for (const TCHAR* Intent : ExactIntents)
		{
			if (Normalized.Equals(Intent, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		static const TCHAR* ProceedPhrases[] = { TEXT("先分析"), TEXT("先扫描"), TEXT("先了解"), TEXT("先看看"), TEXT("按你的方案"), TEXT("按你说的"), TEXT("使用默认") };
		for (const TCHAR* Phrase : ProceedPhrases)
		{
			if (Normalized.Contains(Phrase, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	int32 ClampContextTokenCapacity(const int32 TokenCapacity)
	{
		const int32 ClampedCapacity = FMath::Clamp(TokenCapacity, MinimumContextTokenCapacity, MaximumContextTokenCapacity);
		const int32 StepIndex = FMath::RoundToInt(static_cast<double>(ClampedCapacity - MinimumContextTokenCapacity) / static_cast<double>(ContextTokenCapacityStep));
		return MinimumContextTokenCapacity + StepIndex * ContextTokenCapacityStep;
	}

	int32 SliderValueToContextTokenCapacity(const float SliderValue)
	{
		const float ClampedValue = FMath::Clamp(SliderValue, 0.0f, 1.0f);
		if (ClampedValue <= 0.5f)
		{
			const int32 StepIndex = FMath::RoundToInt((ClampedValue / 0.5f) * ((DefaultContextTokenCapacity - MinimumContextTokenCapacity) / ContextTokenCapacityStep));
			return MinimumContextTokenCapacity + StepIndex * ContextTokenCapacityStep;
		}

		const int32 StepIndex = FMath::RoundToInt(((ClampedValue - 0.5f) / 0.5f) * ((MaximumContextTokenCapacity - DefaultContextTokenCapacity) / ContextTokenCapacityStep));
		return DefaultContextTokenCapacity + StepIndex * ContextTokenCapacityStep;
	}

	float ContextTokenCapacityToSliderValue(const int32 TokenCapacity)
	{
		const int32 ClampedCapacity = ClampContextTokenCapacity(TokenCapacity);
		if (ClampedCapacity <= DefaultContextTokenCapacity)
		{
			return 0.5f * static_cast<float>(ClampedCapacity - MinimumContextTokenCapacity) / static_cast<float>(DefaultContextTokenCapacity - MinimumContextTokenCapacity);
		}

		return 0.5f + 0.5f * static_cast<float>(ClampedCapacity - DefaultContextTokenCapacity) / static_cast<float>(MaximumContextTokenCapacity - DefaultContextTokenCapacity);
	}

	FString FormatContextTokenCapacity(const int32 TokenCapacity)
	{
		const int32 ClampedCapacity = ClampContextTokenCapacity(TokenCapacity);
		return ClampedCapacity == MaximumContextTokenCapacity ? TEXT("1M") : FString::Printf(TEXT("%dK"), ClampedCapacity / 1024);
	}

	double GetContextUsageRatio(const FWorldDataConversation& Conversation)
	{
		const int32 Capacity = FMath::Max(1, Conversation.ContextTokenCapacity);
		return FMath::Clamp(static_cast<double>(Conversation.EstimatedContextTokens) / static_cast<double>(Capacity), 0.0, 1.0);
	}

	FString FormatEstimatedTokenCount(const int32 TokenCount)
	{
		const int32 SafeTokenCount = FMath::Max(0, TokenCount);
		if (SafeTokenCount >= 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1fM"), static_cast<double>(SafeTokenCount) / (1024.0 * 1024.0));
		}
		if (SafeTokenCount >= 1024)
		{
			return FString::Printf(TEXT("%.1fK"), static_cast<double>(SafeTokenCount) / 1024.0);
		}
		return FString::FromInt(SafeTokenCount);
	}

	FString SanitizeSensitiveText(const FString& Text)
	{
		FString Result = ReplaceRegexMatches(Text,
			TEXT("(?i)(authorization|x-unrealagent-token|access[_-]?token|session[_-]?(header|token|id)|x-unrealagent-session)(\\s*[:=]\\s*)(bearer\\s+)?[^\\s,;\\\"'}]+"),
			TEXT("[REDACTED_CREDENTIAL]"));
		Result = ReplaceRegexMatches(Result, TEXT("(?i)\\b[0-9a-f]{64}\\b"), TEXT("[REDACTED_TOKEN]"));
		Result = ReplaceRegexMatches(Result, TEXT("(?i)\\beyJ[a-z0-9_-]{12,}\\.[a-z0-9_-]{12,}(\\.[a-z0-9_-]{8,})?\\b"), TEXT("[REDACTED_JWT]"));
		return Result;
	}

	FString SanitizeToolDisplayName(const FString& ToolName, const int32 MaximumLength)
	{
		FString Result = SanitizeSensitiveText(ToolName);
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.TrimStartAndEndInline();
		const int32 SafeMaximumLength = FMath::Max(16, MaximumLength);
		if (Result.Len() > SafeMaximumLength)
		{
			Result = Result.Left(SafeMaximumLength - 1) + TEXT("…");
		}
		return Result.IsEmpty() ? TEXT("tool") : Result;
	}

	int32 EstimateTextTokens(const FString& Text)
	{
		return Text.IsEmpty() ? 0 : FMath::Max(1, FMath::DivideAndRoundUp(Text.Len(), 3));
	}

	void RefreshConversationContextMetrics(FWorldDataConversation& Conversation)
	{
		const FString RecentHistory = BuildConversationHistorySlice(Conversation.Messages, Conversation.ContextSummaryThroughMessageIndex);
		Conversation.EstimatedContextTokens = EstimateTextTokens(Conversation.ContextContinuitySnapshot + Conversation.ContextSummary + RecentHistory);
	}

	bool ShouldAutoCompactContext(const FWorldDataConversation& Conversation, const double CompactThreshold)
	{
		const int32 Capacity = FMath::Max(1, Conversation.ContextTokenCapacity);
		return static_cast<double>(Conversation.EstimatedContextTokens) / static_cast<double>(Capacity) >= FMath::Clamp(CompactThreshold, 0.1, 0.95);
	}

	bool ShouldShowConversationComposer(const bool bShowSettings, const bool bShowDetail, const bool bDetailIsConversation)
	{
		return !bShowSettings && (!bShowDetail || bDetailIsConversation);
	}

	EWorldDataConversationMessagePresentation ResolveMessagePresentation(const EWorldDataConversationMessageRole Role)
	{
		switch (Role)
		{
		case EWorldDataConversationMessageRole::User:
			return EWorldDataConversationMessagePresentation::UserBubble;
		case EWorldDataConversationMessageRole::Status:
			return EWorldDataConversationMessagePresentation::StatusLine;
		case EWorldDataConversationMessageRole::Tool:
			return EWorldDataConversationMessagePresentation::ToolCard;
		case EWorldDataConversationMessageRole::Error:
			return EWorldDataConversationMessagePresentation::ErrorCard;
		case EWorldDataConversationMessageRole::Assistant:
		case EWorldDataConversationMessageRole::System:
		default:
			return EWorldDataConversationMessagePresentation::PlainText;
		}
	}

	bool CompactConversationContext(FWorldDataConversation& Conversation, const int32 RecentUserTurns, const int32 MaximumSummaryCharacters, const FString& ContinuitySnapshot)
	{
		const int32 EstimatedTokensBefore = Conversation.EstimatedContextTokens;
		int32 KeepStartIndex = Conversation.Messages.Num();
		int32 UserTurnsFound = 0;
		for (int32 Index = Conversation.Messages.Num() - 1; Index >= 0; --Index)
		{
			if (Conversation.Messages[Index].Role == EWorldDataConversationMessageRole::User)
			{
				++UserTurnsFound;
				if (UserTurnsFound >= FMath::Max(1, RecentUserTurns))
				{
					KeepStartIndex = Index;
					break;
				}
			}
		}
		KeepStartIndex = FMath::Max(KeepStartIndex, Conversation.ContextSummaryThroughMessageIndex);
		if (KeepStartIndex <= Conversation.ContextSummaryThroughMessageIndex)
		{
			RefreshConversationContextMetrics(Conversation);
			return false;
		}

		TArray<FWorldDataConversationMessage> SummaryMessages;
		SummaryMessages.Append(Conversation.Messages.GetData() + Conversation.ContextSummaryThroughMessageIndex, KeepStartIndex - Conversation.ContextSummaryThroughMessageIndex);
		FString NewSummary = BuildConversationHistorySlice(SummaryMessages, 0);
		const int32 MaximumCharacters = FMath::Max(1000, MaximumSummaryCharacters);
		if (NewSummary.Len() > MaximumCharacters)
		{
			NewSummary = NewSummary.Right(MaximumCharacters);
		}
		if (!Conversation.ContextSummary.IsEmpty() && !NewSummary.IsEmpty())
		{
			Conversation.ContextSummary += TEXT("\n\n");
		}
		Conversation.ContextSummary += NewSummary;
		if (Conversation.ContextSummary.Len() > MaximumCharacters)
		{
			Conversation.ContextSummary = Conversation.ContextSummary.Right(MaximumCharacters);
		}
		Conversation.ContextSummaryThroughMessageIndex = KeepStartIndex;
		++Conversation.ContextGeneration;
		if (!ContinuitySnapshot.IsEmpty())
		{
			Conversation.ContextContinuitySnapshot = SanitizeSensitiveText(ContinuitySnapshot.Left(32000));
		}
		RefreshConversationContextMetrics(Conversation);

		FWorldDataContextCompactionRecord Record;
		Record.Generation = Conversation.ContextGeneration;
		Record.CompactedAtUtc = FDateTime::UtcNow();
		Record.ContextTokenCapacity = Conversation.ContextTokenCapacity;
		Record.EstimatedTokensBefore = EstimatedTokensBefore;
		Record.EstimatedTokensAfter = Conversation.EstimatedContextTokens;
		Record.SummaryThroughMessageIndex = Conversation.ContextSummaryThroughMessageIndex;
		Conversation.ContextCompactionHistory.Add(MoveTemp(Record));
		constexpr int32 MaximumCompactionRecords = 32;
		if (Conversation.ContextCompactionHistory.Num() > MaximumCompactionRecords)
		{
			Conversation.ContextCompactionHistory.RemoveAt(0, Conversation.ContextCompactionHistory.Num() - MaximumCompactionRecords);
		}
		return true;
	}

	FString BuildConversationContextStatus(const FWorldDataConversation& Conversation)
	{
		const int32 Capacity = FMath::Max(1, Conversation.ContextTokenCapacity);
		const double UsagePercent = 100.0 * static_cast<double>(Conversation.EstimatedContextTokens) / static_cast<double>(Capacity);
		return FString::Printf(TEXT("上下文：约 %d / %d tokens（%.1f%%），压缩代次 %d，摘要覆盖前 %d 条消息。"), Conversation.EstimatedContextTokens, Capacity, UsagePercent,
			Conversation.ContextGeneration, Conversation.ContextSummaryThroughMessageIndex);
	}

	EWorldDataConversationIndicator ResolveConversationIndicator(const bool bRunning, const bool bHasUnreadCompletion, const bool bIsActiveConversation)
	{
		if (bRunning)
		{
			return EWorldDataConversationIndicator::Running;
		}
		return bHasUnreadCompletion && !bIsActiveConversation ? EWorldDataConversationIndicator::UnreadCompletion : EWorldDataConversationIndicator::None;
	}

	bool HasPendingConversationWork(const bool bConversationMarkedRunning, const bool bRuntimeHasActiveTurn, const bool bHasPendingPermission, const bool bHasQueuedPrompts)
	{
		return bConversationMarkedRunning || bRuntimeHasActiveTurn || bHasPendingPermission || bHasQueuedPrompts;
	}

	bool ShouldDispatchQueuedPrompt(const bool bConversationMarkedRunning, const bool bRuntimeHasActiveTurn, const bool bQueueEmpty, const bool bEditingQueueHead)
	{
		return !bConversationMarkedRunning && !bRuntimeHasActiveTurn && !bQueueEmpty && !bEditingQueueHead;
	}

	bool ShouldMarkCompletionUnread(const bool bIsActiveConversation, const bool bHasQueuedPrompts)
	{
		return !bIsActiveConversation && !bHasQueuedPrompts;
	}

	EWorldDataComposerKeyAction ResolveComposerKeyAction(const bool bIsEnter, const bool bControlDown, const bool bShiftDown, const bool bInputMethodComposing)
	{
		if (!bIsEnter)
		{
			return EWorldDataComposerKeyAction::PassThrough;
		}
		if (bInputMethodComposing)
		{
			return EWorldDataComposerKeyAction::Consume;
		}
		if (bShiftDown)
		{
			return EWorldDataComposerKeyAction::InsertNewLine;
		}
		return bControlDown ? EWorldDataComposerKeyAction::PassThrough : EWorldDataComposerKeyAction::Submit;
	}

	bool ShouldSubmitComposerEnter(const double SecondsSinceLastTextChange, const double ImeCommitGuardSeconds)
	{
		return SecondsSinceLastTextChange < 0.0 || SecondsSinceLastTextChange >= ImeCommitGuardSeconds;
	}

	FString TrimTaggedEventText(const FString& Text)
	{
		FString Trimmed = Text;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.StartsWith(TEXT("[")))
		{
			int32 CloseIndex = INDEX_NONE;
			if (Trimmed.FindChar(TCHAR(']'), CloseIndex))
			{
				const FString EventTag = Trimmed.Mid(1, CloseIndex - 1);
				FString Remainder = Trimmed.Mid(CloseIndex + 1);
				Remainder.TrimStartAndEndInline();
				return Remainder.IsEmpty() ? EventTag : Remainder;
			}
		}
		return Trimmed;
	}

	bool TryParseTaggedEvent(const FString& Text, EWorldDataConversationMessageRole& OutRole, FString& OutText)
	{
		FString Trimmed = Text;
		Trimmed.TrimStartAndEndInline();
		if (!Trimmed.StartsWith(TEXT("[")))
		{
			return false;
		}

		int32 CloseIndex = INDEX_NONE;
		if (!Trimmed.FindChar(TCHAR(']'), CloseIndex))
		{
			return false;
		}

		const FString EventTag = Trimmed.Mid(1, CloseIndex - 1);
		if (EventTag.Contains(TEXT("错误")))
		{
			OutRole = EWorldDataConversationMessageRole::Error;
		}
		else if (EventTag.Contains(TEXT("工具")))
		{
			OutRole = EWorldDataConversationMessageRole::Tool;
		}
		else if (EventTag.Contains(TEXT("系统")))
		{
			OutRole = EWorldDataConversationMessageRole::System;
		}
		else
		{
			return false;
		}

		OutText = TrimTaggedEventText(Trimmed);
		return true;
	}

	FString BuildConversationTitleCandidate(const FString& Message, int32 MaximumLength)
	{
		FString Title = Message;
		Title.TrimStartAndEndInline();
		int32 NewlineIndex = INDEX_NONE;
		if (Title.FindChar(TCHAR('\n'), NewlineIndex))
		{
			Title.LeftInline(NewlineIndex);
			Title.TrimEndInline();
		}

		const int32 SafeMaximumLength = FMath::Max(0, MaximumLength);
		if (Title.Len() > SafeMaximumLength)
		{
			Title = Title.Left(SafeMaximumLength) + TEXT("…");
		}
		return Title;
	}

	FString BuildAttachmentDisplayMessage(const FString& UserMessage, const TArray<FString>& AttachmentPaths)
	{
		FString Result = UserMessage.TrimStartAndEnd();
		if (AttachmentPaths.IsEmpty())
		{
			return Result;
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("请查看并处理附件。");
		}
		Result += TEXT("\n\n附件：");
		for (const FString& AttachmentPath : AttachmentPaths)
		{
			Result += TEXT("\n• ");
			Result += FPaths::GetCleanFilename(AttachmentPath);
		}
		return Result;
	}

	FString BuildAttachmentAwarePrompt(const FString& UserMessage, const TArray<FString>& AttachmentPaths)
	{
		FString Result = UserMessage.TrimStartAndEnd();
		if (AttachmentPaths.IsEmpty())
		{
			return Result;
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("请查看并处理我添加的附件。");
		}
		Result += TEXT("\n\n---\n"
					   "用户通过附件功能为本条消息明确添加了以下本地文件或文件夹。"
					   "请先使用合适的工具读取实际路径；文件夹应按用户需求检查其中的相关内容，"
					   "再结合用户需求处理，不要只根据名称猜测内容。附件绝对路径：");
		for (int32 Index = 0; Index < AttachmentPaths.Num(); ++Index)
		{
			Result += FString::Printf(TEXT("\n%d. \"%s\""), Index + 1, *AttachmentPaths[Index]);
		}
		return Result;
	}

	FString BuildComposerInlineAttachmentMarkup(const FString& AttachmentId, const FString& AttachmentPath, const bool bDirectory)
	{
		return FString::Printf(TEXT("<attachment id=\"%s\" path64=\"%s\" directory=\"%d\">\u200B</>"), *AttachmentId, *FBase64::Encode(AttachmentPath, EBase64Mode::UrlSafe),
			bDirectory ? 1 : 0);
	}

	void ParseComposerRichText(const FString& ComposerRichText, FString& OutUserMessage, TArray<FWorldDataComposerInlineAttachment>& OutAttachments)
	{
		OutUserMessage.Reset();
		OutAttachments.Reset();

		int32 CopyStart = 0;
		int32 SearchStart = 0;
		while (SearchStart < ComposerRichText.Len())
		{
			const int32 TagStart = ComposerRichText.Find(TEXT("<attachment"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
			if (TagStart == INDEX_NONE)
			{
				break;
			}

			const int32 TagNameEnd = TagStart + 11;
			if (TagNameEnd >= ComposerRichText.Len() || (ComposerRichText[TagNameEnd] != TCHAR(' ') && ComposerRichText[TagNameEnd] != TCHAR('>')))
			{
				SearchStart = TagNameEnd;
				continue;
			}

			const int32 OpeningTagEnd = ComposerRichText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagNameEnd);
			const int32 ClosingTagStart =
				OpeningTagEnd == INDEX_NONE ? INDEX_NONE : ComposerRichText.Find(TEXT("</>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpeningTagEnd + 1);
			if (OpeningTagEnd == INDEX_NONE || ClosingTagStart == INDEX_NONE)
			{
				break;
			}

			const FString OpeningTag = ComposerRichText.Mid(TagStart, OpeningTagEnd - TagStart + 1);
			FString EncodedPath;
			FString AttachmentPath;
			if (!TryReadComposerMarkupAttribute(OpeningTag, TEXT("path64"), EncodedPath) || !FBase64::Decode(EncodedPath, AttachmentPath, EBase64Mode::UrlSafe) ||
				AttachmentPath.IsEmpty())
			{
				SearchStart = OpeningTagEnd + 1;
				continue;
			}

			OutUserMessage += UnescapeComposerRichText(ComposerRichText.Mid(CopyStart, TagStart - CopyStart));

			FString DirectoryValue;
			TryReadComposerMarkupAttribute(OpeningTag, TEXT("directory"), DirectoryValue);
			FWorldDataComposerInlineAttachment Attachment;
			TryReadComposerMarkupAttribute(OpeningTag, TEXT("id"), Attachment.Id);
			Attachment.Path = MoveTemp(AttachmentPath);
			Attachment.DisplayName = FPaths::GetCleanFilename(Attachment.Path);
			if (Attachment.DisplayName.IsEmpty())
			{
				Attachment.DisplayName = Attachment.Path;
			}
			Attachment.bDirectory = DirectoryValue == TEXT("1") || DirectoryValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
			OutUserMessage += MakeInlineAttachmentMarker(Attachment.DisplayName, Attachment.bDirectory);
			OutAttachments.Add(MoveTemp(Attachment));

			CopyStart = ClosingTagStart + 3;
			SearchStart = CopyStart;
		}

		OutUserMessage += UnescapeComposerRichText(ComposerRichText.Mid(CopyStart));
		OutUserMessage.TrimStartAndEndInline();
	}

	FString BuildComposerRichTextForEditing(const FString& UserMessage, const TArray<FWorldDataConversationAttachment>& Attachments)
	{
		FString Result;
		int32 CopyStart = 0;
		for (const FWorldDataConversationAttachment& Attachment : Attachments)
		{
			if (!Attachment.bInline || Attachment.Path.IsEmpty())
			{
				continue;
			}

			const FString DisplayName = Attachment.DisplayName.IsEmpty() ? FPaths::GetCleanFilename(Attachment.Path) : Attachment.DisplayName;
			const FString Marker = MakeInlineAttachmentMarker(DisplayName, Attachment.bDirectory);
			const int32 MarkerIndex = UserMessage.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, CopyStart);
			if (MarkerIndex == INDEX_NONE)
			{
				continue;
			}

			Result += EscapeComposerRichText(UserMessage.Mid(CopyStart, MarkerIndex - CopyStart));
			Result += BuildComposerInlineAttachmentMarkup(FGuid::NewGuid().ToString(EGuidFormats::Digits), Attachment.Path, Attachment.bDirectory);
			CopyStart = MarkerIndex + Marker.Len();
		}

		Result += EscapeComposerRichText(UserMessage.Mid(CopyStart));
		return Result;
	}

	FString BuildToolCallDisplayText(const FString& ToolName, const EWorldDataConversationToolState State)
	{
		const FString SafeToolName = ToolName.TrimStartAndEnd().IsEmpty() ? FString(TEXT("未命名工具")) : ToolName.TrimStartAndEnd();
		switch (State)
		{
		case EWorldDataConversationToolState::Completed:
			return FString::Printf(TEXT("调用完成：%s"), *SafeToolName);
		case EWorldDataConversationToolState::Failed:
			return FString::Printf(TEXT("调用失败：%s"), *SafeToolName);
		case EWorldDataConversationToolState::Running:
		case EWorldDataConversationToolState::None:
		default:
			return FString::Printf(TEXT("正在调用：%s"), *SafeToolName);
		}
	}

	FString BuildToolCallFailureDetails(const FString& CanonicalAction, const FString& Code, const TArray<FString>& SchemaErrorPaths, const FString& TraceId)
	{
		FString Details;
		if (!CanonicalAction.IsEmpty())
		{
			Details += FString::Printf(TEXT("\ncanonical action: %s"), *SanitizeToolDisplayName(CanonicalAction));
		}
		if (!Code.IsEmpty())
		{
			Details += FString::Printf(TEXT("\ncode: %s"), *SanitizeToolDisplayName(Code));
		}
		if (!SchemaErrorPaths.IsEmpty())
		{
			TArray<FString> SafePaths;
			SafePaths.Reserve(SchemaErrorPaths.Num());
			for (const FString& Path : SchemaErrorPaths)
			{
				SafePaths.Add(SanitizeToolDisplayName(Path, 256));
			}
			Details += FString::Printf(TEXT("\nschemaErrors: %s"), *FString::Join(SafePaths, TEXT(", ")));
		}
		if (!TraceId.IsEmpty())
		{
			Details += FString::Printf(TEXT("\ntraceId: %s"), *SanitizeToolDisplayName(TraceId));
		}
		return Details;
	}

	bool ShouldAutoCollapseToolGroup(const TArray<FWorldDataConversationMessage>& Messages, const int32 ToolMessageIndex)
	{
		if (!Messages.IsValidIndex(ToolMessageIndex) || Messages[ToolMessageIndex].Role != EWorldDataConversationMessageRole::Tool)
		{
			return false;
		}

		int32 TurnStartIndex = ToolMessageIndex;
		while (TurnStartIndex > 0 && Messages[TurnStartIndex].Role != EWorldDataConversationMessageRole::User)
		{
			--TurnStartIndex;
		}
		int32 TurnEndIndex = Messages.Num();
		for (int32 Index = ToolMessageIndex + 1; Index < Messages.Num(); ++Index)
		{
			if (Messages[Index].Role == EWorldDataConversationMessageRole::User)
			{
				TurnEndIndex = Index;
				break;
			}
		}

		for (int32 Index = TurnStartIndex; Index < TurnEndIndex; ++Index)
		{
			const FWorldDataConversationMessage& Message = Messages[Index];
			if (Message.Role == EWorldDataConversationMessageRole::Status && (Message.bCompleted || Message.bFailed))
			{
				return true;
			}
		}

		// 兼容旧历史：早期版本可能没有独立的回合状态消息。
		for (int32 Index = ToolMessageIndex + 1; Index < TurnEndIndex; ++Index)
		{
			const FWorldDataConversationMessage& Message = Messages[Index];
			if (Message.Role == EWorldDataConversationMessageRole::Assistant && (Message.bCompleted || Message.bFailed))
			{
				return true;
			}
		}
		return false;
	}

	FString BuildConversationHistorySnapshot(const TArray<FWorldDataConversationMessage>& Messages)
	{
		return BuildConversationHistorySlice(Messages, 0);
	}

	FString BuildConversationContextReplayPrompt(const TArray<FWorldDataConversationMessage>& Messages, const FString& NewPrompt)
	{
		const FString History = BuildConversationHistorySnapshot(Messages);
		const FString TrimmedPrompt = NewPrompt.TrimStartAndEnd();
		if (History.IsEmpty())
		{
			return TrimmedPrompt;
		}

		return FString::Printf(TEXT("这是一个从已有对话处分出的新任务。请继承下面的历史上下文，"
									"但只处理历史之后的新请求，不要重复回答旧问题。\n\n"
									"<conversation_history>\n%s\n</conversation_history>\n\n"
									"当前用户的新请求：\n%s"),
			*History, *TrimmedPrompt);
	}

	FString BuildConversationContextReplayPrompt(const FWorldDataConversation& Conversation, const FString& NewPrompt)
	{
		const FString RecentHistory = BuildConversationHistorySlice(Conversation.Messages, Conversation.ContextSummaryThroughMessageIndex);
		FString History = Conversation.ContextSummary;
		if (!History.IsEmpty() && !RecentHistory.IsEmpty())
		{
			History += TEXT("\n\n--- 最近对话 ---\n\n");
		}
		History += RecentHistory;
		FString ContinuityBlock;
		if (!Conversation.ContextContinuitySnapshot.IsEmpty())
		{
			ContinuityBlock = FString::Printf(TEXT("<project_continuity_snapshot>\n%s\n</project_continuity_snapshot>\n\n"), *Conversation.ContextContinuitySnapshot);
		}
		const FString TrimmedPrompt = NewPrompt.TrimStartAndEnd();
		if (History.IsEmpty() && ContinuityBlock.IsEmpty())
		{
			return TrimmedPrompt;
		}

		return FString::Printf(TEXT("这是一个恢复或压缩后的连续任务。请继承下面的上下文，"
									"只处理上下文之后的新请求，不要重复回答旧问题。\n\n"
									"%s<conversation_history generation=\"%d\">\n%s\n"
									"</conversation_history>\n\n当前用户的新请求：\n%s"),
			*ContinuityBlock, Conversation.ContextGeneration, *History, *TrimmedPrompt);
	}

	bool TryRemoveConversationFromUserMessage(TArray<FWorldDataConversationMessage>& Messages, const int32 UserMessageIndex)
	{
		if (!Messages.IsValidIndex(UserMessageIndex) || Messages[UserMessageIndex].Role != EWorldDataConversationMessageRole::User)
		{
			return false;
		}

		Messages.SetNum(UserMessageIndex, EAllowShrinking::Yes);
		return true;
	}

	EWorldDataConversationRewriteResult PrepareConversationForUserMessageRewrite(FWorldDataConversation& Conversation, const int32 UserMessageIndex)
	{
		if (!Conversation.Messages.IsValidIndex(UserMessageIndex) || Conversation.Messages[UserMessageIndex].Role != EWorldDataConversationMessageRole::User)
		{
			return EWorldDataConversationRewriteResult::InvalidUserMessage;
		}

		Conversation.Messages.SetNum(UserMessageIndex, EAllowShrinking::Yes);
		Conversation.QueuedPrompts.Reset();
		Conversation.Transcript = BuildConversationHistorySnapshot(Conversation.Messages);
		Conversation.bIsRunning = false;
		Conversation.bHasUnreadCompletion = false;
		Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
		Conversation.ActiveTurnStatusMessageIndex = INDEX_NONE;
		return EWorldDataConversationRewriteResult::Prepared;
	}

	bool TryCreateConversationBranch(const FWorldDataConversation& Source, const int32 AssistantMessageIndex, const FDateTime& CreatedAt, FWorldDataConversation& OutBranch)
	{
		if (!Source.Messages.IsValidIndex(AssistantMessageIndex))
		{
			return false;
		}

		const FWorldDataConversationMessage& BranchPoint = Source.Messages[AssistantMessageIndex];
		if (BranchPoint.Role != EWorldDataConversationMessageRole::Assistant || BranchPoint.bStreaming || (!BranchPoint.bCompleted && !BranchPoint.bFailed))
		{
			return false;
		}

		FWorldDataConversation Branch;
		Branch.Id = FGuid::NewGuid();
		Branch.Title = FText::FromString(Source.Title.ToString() + TEXT("（分支）"));
		Branch.CreatedAt = CreatedAt;
		Branch.UpdatedAt = CreatedAt;
		Branch.ControllerId = Source.ControllerId;
		Branch.ProviderId = Source.ProviderId;
		Branch.AgentMode = Source.AgentMode;
		Branch.ApprovalPolicy = Source.ApprovalPolicy;
		Branch.SelfRepairPolicy = Source.SelfRepairPolicy;
		Branch.bHasCustomTitle = true;
		Branch.bArchived = false;
		Branch.Messages.Append(Source.Messages.GetData(), AssistantMessageIndex + 1);
		for (FWorldDataConversationMessage& Message : Branch.Messages)
		{
			Message.bStreaming = false;
		}
		Branch.Transcript = BuildConversationHistorySnapshot(Branch.Messages);
		Branch.ActiveAssistantMessageIndex = INDEX_NONE;
		Branch.ActiveTurnStatusMessageIndex = INDEX_NONE;
		OutBranch = MoveTemp(Branch);
		return true;
	}

	FString FormatCompletionLabel(const FDateTime& CompletedAt, const bool bFailed, const FDateTime& ReferenceTime)
	{
		if (CompletedAt == FDateTime())
		{
			return FString();
		}

		const bool bSameDay =
			CompletedAt.GetYear() == ReferenceTime.GetYear() && CompletedAt.GetMonth() == ReferenceTime.GetMonth() && CompletedAt.GetDay() == ReferenceTime.GetDay();
		return FString::Printf(TEXT("%s %s"), *CompletedAt.ToString(bSameDay ? TEXT("%H:%M") : TEXT("%m-%d %H:%M")), bFailed ? TEXT("失败") : TEXT("完成"));
	}

	int32 FindFirstUnarchivedConversation(const TArray<FWorldDataConversation>& Conversations, int32 ExcludedIndex)
	{
		for (int32 Index = 0; Index < Conversations.Num(); ++Index)
		{
			if (Index != ExcludedIndex && !Conversations[Index].bArchived)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}
