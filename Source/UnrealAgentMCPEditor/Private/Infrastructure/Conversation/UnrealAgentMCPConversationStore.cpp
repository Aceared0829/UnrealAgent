// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPConversationStore.cpp
 * @brief 编辑器 ACP 会话历史的 JSON 持久化实现。
 */

#include "Infrastructure/Conversation/UnrealAgentMCPConversationStore.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FUnrealAgentMCPConversationStore::~FUnrealAgentMCPConversationStore()
{
	FlushPendingSaves();
}

bool FUnrealAgentMCPConversationStore::Load(const FString& HistoryPath, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex)
{
	return UnrealAgentMCPConversationStore::LoadHistoryFile(HistoryPath, OutConversations, OutActiveConversationIndex);
}

bool FUnrealAgentMCPConversationStore::Save(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, const int32 ActiveConversationIndex)
{
	FlushPendingSaves();
	return UnrealAgentMCPConversationStore::SaveHistoryFile(HistoryPath, Conversations, ActiveConversationIndex);
}

void FUnrealAgentMCPConversationStore::SaveAsync(const FString& HistoryPath, TArray<FWorldDataConversation> Conversations, const int32 ActiveConversationIndex)
{
	bool bShouldLaunchWorker = false;
	{
		FScopeLock Lock(&SaveRequestLock);
		FSaveRequest Request;
		Request.HistoryPath = HistoryPath;
		Request.Conversations = MoveTemp(Conversations);
		Request.ActiveConversationIndex = ActiveConversationIndex;
		PendingSaveRequest = MoveTemp(Request);
		if (!bSaveWorkerRunning)
		{
			bSaveWorkerRunning = true;
			bShouldLaunchWorker = true;
		}
	}
	if (!bShouldLaunchWorker)
	{
		return;
	}

	SavePipe.Launch(
		TEXT("保存 Unreal Agent 会话历史"),
		[this]()
		{
			DrainLatestSaveRequests();
		},
		UE::Tasks::ETaskPriority::BackgroundLow);
}

void FUnrealAgentMCPConversationStore::DrainLatestSaveRequests()
{
	while (true)
	{
		TOptional<FSaveRequest> Request;
		{
			FScopeLock Lock(&SaveRequestLock);
			if (!PendingSaveRequest.IsSet())
			{
				bSaveWorkerRunning = false;
				return;
			}
			Request = MoveTemp(PendingSaveRequest);
			PendingSaveRequest.Reset();
		}

		UnrealAgentMCPConversationStore::SaveHistoryFile(Request->HistoryPath, Request->Conversations, Request->ActiveConversationIndex);
	}
}

void FUnrealAgentMCPConversationStore::FlushPendingSaves()
{
	SavePipe.WaitUntilEmpty();
}

namespace
{
	constexpr int32 CurrentHistoryVersion = 8;

	FString RoleToString(EWorldDataConversationMessageRole Role)
	{
		switch (Role)
		{
		case EWorldDataConversationMessageRole::User:
			return TEXT("user");
		case EWorldDataConversationMessageRole::Status:
			return TEXT("status");
		case EWorldDataConversationMessageRole::System:
			return TEXT("system");
		case EWorldDataConversationMessageRole::Tool:
			return TEXT("tool");
		case EWorldDataConversationMessageRole::Error:
			return TEXT("error");
		case EWorldDataConversationMessageRole::Assistant:
		default:
			return TEXT("assistant");
		}
	}

	EWorldDataConversationMessageRole StringToRole(const FString& Value)
	{
		if (Value.Equals(TEXT("user"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationMessageRole::User;
		}
		if (Value.Equals(TEXT("status"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationMessageRole::Status;
		}
		if (Value.Equals(TEXT("system"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationMessageRole::System;
		}
		if (Value.Equals(TEXT("tool"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationMessageRole::Tool;
		}
		if (Value.Equals(TEXT("error"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationMessageRole::Error;
		}
		return EWorldDataConversationMessageRole::Assistant;
	}

	FString ToolStateToString(const EWorldDataConversationToolState State)
	{
		switch (State)
		{
		case EWorldDataConversationToolState::Running:
			return TEXT("running");
		case EWorldDataConversationToolState::Completed:
			return TEXT("completed");
		case EWorldDataConversationToolState::Failed:
			return TEXT("failed");
		case EWorldDataConversationToolState::None:
		default:
			return TEXT("none");
		}
	}

	EWorldDataConversationToolState StringToToolState(const FString& Value)
	{
		if (Value.Equals(TEXT("running"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationToolState::Running;
		}
		if (Value.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationToolState::Completed;
		}
		if (Value.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
		{
			return EWorldDataConversationToolState::Failed;
		}
		return EWorldDataConversationToolState::None;
	}

	FDateTime ReadDateTime(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FDateTime& DefaultValue)
	{
		FString Value;
		FDateTime Parsed;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) && FDateTime::ParseIso8601(*Value, Parsed) ? Parsed : DefaultValue;
	}

	TSharedPtr<FJsonObject> SerializeMessage(const FWorldDataConversationMessage& Message)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Message.Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetStringField(TEXT("role"), RoleToString(Message.Role));
		Object->SetStringField(TEXT("text"), UnrealAgentMCPConversationModel::SanitizeSensitiveText(Message.Text));
		if (!Message.ToolCallId.IsEmpty())
		{
			Object->SetStringField(TEXT("toolCallId"), Message.ToolCallId);
		}
		if (!Message.ToolName.IsEmpty())
		{
			Object->SetStringField(TEXT("toolName"), UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Message.ToolName));
		}
		if (Message.ToolState != EWorldDataConversationToolState::None)
		{
			Object->SetStringField(TEXT("toolState"), ToolStateToString(Message.ToolState));
		}
		Object->SetBoolField(TEXT("completed"), Message.bCompleted);
		Object->SetBoolField(TEXT("failed"), Message.bFailed);
		Object->SetStringField(TEXT("createdAt"), Message.CreatedAt.ToIso8601());
		if (Message.CompletedAt != FDateTime())
		{
			Object->SetStringField(TEXT("completedAt"), Message.CompletedAt.ToIso8601());
		}

		TArray<TSharedPtr<FJsonValue>> AttachmentValues;
		AttachmentValues.Reserve(Message.Attachments.Num());
		for (const FWorldDataConversationAttachment& Attachment : Message.Attachments)
		{
			TSharedPtr<FJsonObject> AttachmentObject = MakeShared<FJsonObject>();
			AttachmentObject->SetStringField(TEXT("path"), Attachment.Path);
			AttachmentObject->SetStringField(TEXT("displayName"), Attachment.DisplayName);
			AttachmentObject->SetBoolField(TEXT("directory"), Attachment.bDirectory);
			AttachmentObject->SetBoolField(TEXT("inline"), Attachment.bInline);
			AttachmentValues.Add(MakeShared<FJsonValueObject>(MoveTemp(AttachmentObject)));
		}
		Object->SetArrayField(TEXT("attachments"), MoveTemp(AttachmentValues));
		return Object;
	}

	FWorldDataConversationMessage ParseMessage(const TSharedPtr<FJsonObject>& Object)
	{
		FWorldDataConversationMessage Message;
		if (!Object.IsValid())
		{
			return Message;
		}

		FString Role;
		FString IdText;
		if (!Object->TryGetStringField(TEXT("id"), IdText) || !FGuid::Parse(IdText, Message.Id))
		{
			Message.Id = FGuid::NewGuid();
		}
		Object->TryGetStringField(TEXT("role"), Role);
		Message.Role = StringToRole(Role);
		Object->TryGetStringField(TEXT("text"), Message.Text);
		Message.Text = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Message.Text);
		Object->TryGetStringField(TEXT("toolCallId"), Message.ToolCallId);
		Object->TryGetStringField(TEXT("toolName"), Message.ToolName);
		Message.ToolName = UnrealAgentMCPConversationModel::SanitizeToolDisplayName(Message.ToolName);
		FString ToolState;
		Object->TryGetStringField(TEXT("toolState"), ToolState);
		Message.ToolState = StringToToolState(ToolState);
		Object->TryGetBoolField(TEXT("completed"), Message.bCompleted);
		Object->TryGetBoolField(TEXT("failed"), Message.bFailed);
		Message.CreatedAt = ReadDateTime(Object, TEXT("createdAt"), FDateTime::Now());
		Message.CompletedAt = ReadDateTime(Object, TEXT("completedAt"), FDateTime());

		const TArray<TSharedPtr<FJsonValue>>* AttachmentValues = nullptr;
		if (Object->TryGetArrayField(TEXT("attachments"), AttachmentValues) && AttachmentValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& AttachmentValue : *AttachmentValues)
			{
				const TSharedPtr<FJsonObject> AttachmentObject = AttachmentValue.IsValid() ? AttachmentValue->AsObject() : nullptr;
				if (!AttachmentObject.IsValid())
				{
					continue;
				}

				FWorldDataConversationAttachment Attachment;
				AttachmentObject->TryGetStringField(TEXT("path"), Attachment.Path);
				AttachmentObject->TryGetStringField(TEXT("displayName"), Attachment.DisplayName);
				AttachmentObject->TryGetBoolField(TEXT("directory"), Attachment.bDirectory);
				AttachmentObject->TryGetBoolField(TEXT("inline"), Attachment.bInline);
				if (Attachment.DisplayName.IsEmpty())
				{
					Attachment.DisplayName = FPaths::GetCleanFilename(Attachment.Path);
				}
				if (!Attachment.Path.IsEmpty() || !Attachment.DisplayName.IsEmpty())
				{
					Message.Attachments.Add(MoveTemp(Attachment));
				}
			}
		}
		// 编辑器恢复后不可能继续持有生成该流的原进程。
		Message.bStreaming = false;
		return Message;
	}

	TSharedPtr<FJsonObject> SerializeAttachment(const FWorldDataConversationAttachment& Attachment)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("path"), Attachment.Path);
		Object->SetStringField(TEXT("displayName"), Attachment.DisplayName);
		Object->SetBoolField(TEXT("directory"), Attachment.bDirectory);
		Object->SetBoolField(TEXT("inline"), Attachment.bInline);
		return Object;
	}

	bool ParseAttachment(const TSharedPtr<FJsonObject>& Object, FWorldDataConversationAttachment& OutAttachment)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		Object->TryGetStringField(TEXT("path"), OutAttachment.Path);
		Object->TryGetStringField(TEXT("displayName"), OutAttachment.DisplayName);
		Object->TryGetBoolField(TEXT("directory"), OutAttachment.bDirectory);
		Object->TryGetBoolField(TEXT("inline"), OutAttachment.bInline);
		if (OutAttachment.DisplayName.IsEmpty())
		{
			OutAttachment.DisplayName = FPaths::GetCleanFilename(OutAttachment.Path);
		}
		return !OutAttachment.Path.IsEmpty() || !OutAttachment.DisplayName.IsEmpty();
	}

	TSharedPtr<FJsonObject> SerializeQueuedPrompt(const FWorldDataQueuedPrompt& Prompt)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Prompt.Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetStringField(TEXT("text"), UnrealAgentMCPConversationModel::SanitizeSensitiveText(Prompt.Text));
		Object->SetStringField(TEXT("provider"), Prompt.ProviderId);
		Object->SetStringField(TEXT("model"), Prompt.ModelId);
		Object->SetStringField(TEXT("reasoning"), Prompt.ReasoningEffort);
		Object->SetStringField(TEXT("serviceTier"), Prompt.ServiceTier);
		Object->SetNumberField(TEXT("agentMode"), Prompt.AgentMode);
		Object->SetNumberField(TEXT("approvalPolicy"), Prompt.ApprovalPolicy);
		Object->SetNumberField(TEXT("selfRepairPolicy"), Prompt.SelfRepairPolicy);
		Object->SetStringField(TEXT("createdAt"), Prompt.CreatedAt.ToIso8601());
		TArray<TSharedPtr<FJsonValue>> AttachmentValues;
		for (const FWorldDataConversationAttachment& Attachment : Prompt.Attachments)
		{
			AttachmentValues.Add(MakeShared<FJsonValueObject>(SerializeAttachment(Attachment)));
		}
		Object->SetArrayField(TEXT("attachments"), MoveTemp(AttachmentValues));
		return Object;
	}

	FWorldDataQueuedPrompt ParseQueuedPrompt(const TSharedPtr<FJsonObject>& Object)
	{
		FWorldDataQueuedPrompt Prompt;
		if (!Object.IsValid())
		{
			return Prompt;
		}
		FString IdText;
		if (!Object->TryGetStringField(TEXT("id"), IdText) || !FGuid::Parse(IdText, Prompt.Id))
		{
			Prompt.Id = FGuid::NewGuid();
		}
		Object->TryGetStringField(TEXT("text"), Prompt.Text);
		Prompt.Text = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Prompt.Text);
		Object->TryGetStringField(TEXT("provider"), Prompt.ProviderId);
		Object->TryGetStringField(TEXT("model"), Prompt.ModelId);
		Object->TryGetStringField(TEXT("reasoning"), Prompt.ReasoningEffort);
		Object->TryGetStringField(TEXT("serviceTier"), Prompt.ServiceTier);
		double AgentMode = 2.0;
		double ApprovalPolicy = 0.0;
		double SelfRepairPolicy = 0.0;
		if (!Object->TryGetNumberField(TEXT("agentMode"), AgentMode))
		{
			double LegacyMode = 0.0;
			Object->TryGetNumberField(TEXT("permissionMode"), LegacyMode);
			AgentMode = LegacyMode == 1.0 ? 1.0 : 2.0;
			ApprovalPolicy = LegacyMode == 2.0 ? 2.0 : 0.0;
		}
		Object->TryGetNumberField(TEXT("approvalPolicy"), ApprovalPolicy);
		Object->TryGetNumberField(TEXT("selfRepairPolicy"), SelfRepairPolicy);
		Prompt.AgentMode = static_cast<uint8>(FMath::Clamp(static_cast<int32>(AgentMode), 0, 2));
		Prompt.ApprovalPolicy = static_cast<uint8>(FMath::Clamp(static_cast<int32>(ApprovalPolicy), 0, 2));
		Prompt.SelfRepairPolicy = static_cast<uint8>(FMath::Clamp(static_cast<int32>(SelfRepairPolicy), 0, 2));
		Prompt.CreatedAt = ReadDateTime(Object, TEXT("createdAt"), FDateTime::Now());
		const TArray<TSharedPtr<FJsonValue>>* AttachmentValues = nullptr;
		if (Object->TryGetArrayField(TEXT("attachments"), AttachmentValues) && AttachmentValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *AttachmentValues)
			{
				FWorldDataConversationAttachment Attachment;
				if (ParseAttachment(Value.IsValid() ? Value->AsObject() : nullptr, Attachment))
				{
					Prompt.Attachments.Add(MoveTemp(Attachment));
				}
			}
		}
		if (Prompt.ProviderId.IsEmpty())
		{
			Prompt.ProviderId = TEXT("codex");
		}
		return Prompt;
	}

	bool AppendConversationAuditRecord(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, const int32 ActiveConversationIndex)
	{
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("event"), TEXT("snapshot_persisted"));
		Record->SetStringField(TEXT("recorded_at_utc"), FDateTime::UtcNow().ToIso8601());
		Record->SetNumberField(TEXT("conversation_count"), Conversations.Num());
		int32 MessageCount = 0;
		int32 MaximumContextGeneration = 0;
		for (const FWorldDataConversation& Conversation : Conversations)
		{
			MessageCount += Conversation.Messages.Num();
			MaximumContextGeneration = FMath::Max(MaximumContextGeneration, Conversation.ContextGeneration);
		}
		Record->SetNumberField(TEXT("message_count"), MessageCount);
		Record->SetNumberField(TEXT("maximum_context_generation"), MaximumContextGeneration);
		if (Conversations.IsValidIndex(ActiveConversationIndex))
		{
			Record->SetStringField(TEXT("active_conversation_id"), Conversations[ActiveConversationIndex].Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
		FString Line;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Line);
		if (!FJsonSerializer::Serialize(Record, Writer))
		{
			return false;
		}
		Line += LINE_TERMINATOR;
		const FString AuditPath = FPaths::Combine(FPaths::GetPath(HistoryPath), TEXT("conversation-audit.jsonl"));
		if (!FFileHelper::SaveStringToFile(Line, *AuditPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
		{
			return false;
		}
		FString AccessError;
		return UnrealAgentMCP::ServerEnvironment::RestrictFileAccessToCurrentUser(AuditPath, AccessError);
	}
}

namespace UnrealAgentMCPConversationStore
{
	bool TrySerializeHistory(const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex, FString& OutJsonText)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), CurrentHistoryVersion);
		if (Conversations.IsValidIndex(ActiveConversationIndex))
		{
			Root->SetStringField(TEXT("activeConversationId"), Conversations[ActiveConversationIndex].Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		}

		TArray<TSharedPtr<FJsonValue>> ConversationValues;
		ConversationValues.Reserve(Conversations.Num());
		for (const FWorldDataConversation& Conversation : Conversations)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("id"), Conversation.Id.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("title"), Conversation.Title.ToString());
			Object->SetStringField(TEXT("createdAt"), Conversation.CreatedAt.ToIso8601());
			Object->SetStringField(TEXT("updatedAt"), Conversation.UpdatedAt.ToIso8601());
			Object->SetStringField(TEXT("controller"), Conversation.ControllerId);
			Object->SetStringField(TEXT("provider"), Conversation.ProviderId);
			Object->SetNumberField(TEXT("agentMode"), Conversation.AgentMode);
			Object->SetNumberField(TEXT("approvalPolicy"), Conversation.ApprovalPolicy);
			Object->SetNumberField(TEXT("selfRepairPolicy"), Conversation.SelfRepairPolicy);
			Object->SetBoolField(TEXT("hasCustomTitle"), Conversation.bHasCustomTitle);
			Object->SetBoolField(TEXT("archived"), Conversation.bArchived);
			Object->SetBoolField(TEXT("unreadCompletion"), Conversation.bHasUnreadCompletion);
			Object->SetStringField(TEXT("contextSummary"), UnrealAgentMCPConversationModel::SanitizeSensitiveText(Conversation.ContextSummary));
			Object->SetStringField(TEXT("contextContinuitySnapshot"), UnrealAgentMCPConversationModel::SanitizeSensitiveText(Conversation.ContextContinuitySnapshot));
			Object->SetNumberField(TEXT("contextSummaryThroughMessageIndex"), Conversation.ContextSummaryThroughMessageIndex);
			Object->SetNumberField(TEXT("contextGeneration"), Conversation.ContextGeneration);
			Object->SetNumberField(TEXT("contextTokenCapacity"), Conversation.ContextTokenCapacity);
			TArray<TSharedPtr<FJsonValue>> CompactionValues;
			for (const FWorldDataContextCompactionRecord& Record : Conversation.ContextCompactionHistory)
			{
				TSharedRef<FJsonObject> RecordObject = MakeShared<FJsonObject>();
				RecordObject->SetNumberField(TEXT("generation"), Record.Generation);
				RecordObject->SetStringField(TEXT("compactedAtUtc"), Record.CompactedAtUtc.ToIso8601());
				RecordObject->SetNumberField(TEXT("contextTokenCapacity"), Record.ContextTokenCapacity);
				RecordObject->SetNumberField(TEXT("estimatedTokensBefore"), Record.EstimatedTokensBefore);
				RecordObject->SetNumberField(TEXT("estimatedTokensAfter"), Record.EstimatedTokensAfter);
				RecordObject->SetNumberField(TEXT("summaryThroughMessageIndex"), Record.SummaryThroughMessageIndex);
				CompactionValues.Add(MakeShared<FJsonValueObject>(RecordObject));
			}
			Object->SetArrayField(TEXT("contextCompactions"), MoveTemp(CompactionValues));

			TArray<TSharedPtr<FJsonValue>> MessageValues;
			MessageValues.Reserve(Conversation.Messages.Num());
			for (const FWorldDataConversationMessage& Message : Conversation.Messages)
			{
				MessageValues.Add(MakeShared<FJsonValueObject>(SerializeMessage(Message)));
			}
			Object->SetArrayField(TEXT("messages"), MoveTemp(MessageValues));
			TArray<TSharedPtr<FJsonValue>> QueueValues;
			for (const FWorldDataQueuedPrompt& Prompt : Conversation.QueuedPrompts)
			{
				QueueValues.Add(MakeShared<FJsonValueObject>(SerializeQueuedPrompt(Prompt)));
			}
			Object->SetArrayField(TEXT("queue"), MoveTemp(QueueValues));
			ConversationValues.Add(MakeShared<FJsonValueObject>(MoveTemp(Object)));
		}
		Root->SetArrayField(TEXT("conversations"), MoveTemp(ConversationValues));

		OutJsonText.Reset();
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJsonText);
		return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}

	bool TryParseHistory(const FString& JsonText, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex)
	{
		OutConversations.Reset();
		OutActiveConversationIndex = INDEX_NONE;

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Root->TryGetArrayField(TEXT("conversations"), Values) || Values == nullptr)
		{
			return false;
		}

		FString ActiveIdText;
		Root->TryGetStringField(TEXT("activeConversationId"), ActiveIdText);
		FGuid ActiveId;
		FGuid::Parse(ActiveIdText, ActiveId);

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object.IsValid())
			{
				continue;
			}

			FWorldDataConversation Conversation;
			FString IdText;
			if (!Object->TryGetStringField(TEXT("id"), IdText) || !FGuid::Parse(IdText, Conversation.Id))
			{
				Conversation.Id = FGuid::NewGuid();
			}

			FString Title;
			Object->TryGetStringField(TEXT("title"), Title);
			Conversation.Title = FText::FromString(Title.IsEmpty() ? TEXT("新对话") : Title);
			Conversation.CreatedAt = ReadDateTime(Object, TEXT("createdAt"), FDateTime::Now());
			Conversation.UpdatedAt = ReadDateTime(Object, TEXT("updatedAt"), Conversation.CreatedAt);
			Object->TryGetStringField(TEXT("controller"), Conversation.ControllerId);
			Object->TryGetStringField(TEXT("provider"), Conversation.ProviderId);
			if (Conversation.ProviderId.IsEmpty())
			{
				Conversation.ProviderId = TEXT("codex");
			}
			double AgentMode = 2.0;
			double ApprovalPolicy = 0.0;
			double SelfRepairPolicy = 0.0;
			Object->TryGetNumberField(TEXT("agentMode"), AgentMode);
			Object->TryGetNumberField(TEXT("approvalPolicy"), ApprovalPolicy);
			Object->TryGetNumberField(TEXT("selfRepairPolicy"), SelfRepairPolicy);
			Conversation.AgentMode = static_cast<uint8>(FMath::Clamp(static_cast<int32>(AgentMode), 0, 2));
			Conversation.ApprovalPolicy = static_cast<uint8>(FMath::Clamp(static_cast<int32>(ApprovalPolicy), 0, 2));
			Conversation.SelfRepairPolicy = static_cast<uint8>(FMath::Clamp(static_cast<int32>(SelfRepairPolicy), 0, 2));
			Object->TryGetBoolField(TEXT("hasCustomTitle"), Conversation.bHasCustomTitle);
			Object->TryGetBoolField(TEXT("archived"), Conversation.bArchived);
			Object->TryGetBoolField(TEXT("unreadCompletion"), Conversation.bHasUnreadCompletion);
			Object->TryGetStringField(TEXT("contextSummary"), Conversation.ContextSummary);
			Conversation.ContextSummary = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Conversation.ContextSummary);
			Object->TryGetStringField(TEXT("contextContinuitySnapshot"), Conversation.ContextContinuitySnapshot);
			Conversation.ContextContinuitySnapshot = UnrealAgentMCPConversationModel::SanitizeSensitiveText(Conversation.ContextContinuitySnapshot.Left(32000));
			double SummaryThroughIndex = 0.0;
			double ContextGeneration = 0.0;
			double ContextTokenCapacity = UnrealAgentMCPConversationModel::DefaultContextTokenCapacity;
			Object->TryGetNumberField(TEXT("contextSummaryThroughMessageIndex"), SummaryThroughIndex);
			Object->TryGetNumberField(TEXT("contextGeneration"), ContextGeneration);
			Object->TryGetNumberField(TEXT("contextTokenCapacity"), ContextTokenCapacity);
			Conversation.ContextSummaryThroughMessageIndex = FMath::Max(0, static_cast<int32>(SummaryThroughIndex));
			Conversation.ContextGeneration = FMath::Max(0, static_cast<int32>(ContextGeneration));
			Conversation.ContextTokenCapacity = UnrealAgentMCPConversationModel::ClampContextTokenCapacity(static_cast<int32>(ContextTokenCapacity));

			const TArray<TSharedPtr<FJsonValue>>* CompactionValues = nullptr;
			if (Object->TryGetArrayField(TEXT("contextCompactions"), CompactionValues) && CompactionValues)
			{
				for (const TSharedPtr<FJsonValue>& CompactionValue : *CompactionValues)
				{
					const TSharedPtr<FJsonObject> RecordObject = CompactionValue.IsValid() ? CompactionValue->AsObject() : nullptr;
					if (!RecordObject.IsValid())
					{
						continue;
					}
					FWorldDataContextCompactionRecord Record;
					double NumericField = 0.0;
					RecordObject->TryGetNumberField(TEXT("generation"), NumericField);
					Record.Generation = FMath::Max(0, static_cast<int32>(NumericField));
					Record.CompactedAtUtc = ReadDateTime(RecordObject, TEXT("compactedAtUtc"), FDateTime());
					NumericField = 0.0;
					RecordObject->TryGetNumberField(TEXT("contextTokenCapacity"), NumericField);
					Record.ContextTokenCapacity = UnrealAgentMCPConversationModel::ClampContextTokenCapacity(static_cast<int32>(NumericField));
					NumericField = 0.0;
					RecordObject->TryGetNumberField(TEXT("estimatedTokensBefore"), NumericField);
					Record.EstimatedTokensBefore = FMath::Max(0, static_cast<int32>(NumericField));
					NumericField = 0.0;
					RecordObject->TryGetNumberField(TEXT("estimatedTokensAfter"), NumericField);
					Record.EstimatedTokensAfter = FMath::Max(0, static_cast<int32>(NumericField));
					NumericField = 0.0;
					RecordObject->TryGetNumberField(TEXT("summaryThroughMessageIndex"), NumericField);
					Record.SummaryThroughMessageIndex = FMath::Max(0, static_cast<int32>(NumericField));
					Conversation.ContextCompactionHistory.Add(MoveTemp(Record));
				}
				constexpr int32 MaximumCompactionRecords = 32;
				if (Conversation.ContextCompactionHistory.Num() > MaximumCompactionRecords)
				{
					Conversation.ContextCompactionHistory.RemoveAt(0, Conversation.ContextCompactionHistory.Num() - MaximumCompactionRecords);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* MessageValues = nullptr;
			if (Object->TryGetArrayField(TEXT("messages"), MessageValues) && MessageValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& MessageValue : *MessageValues)
				{
					Conversation.Messages.Add(ParseMessage(MessageValue.IsValid() ? MessageValue->AsObject() : nullptr));
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* QueueValues = nullptr;
			if (Object->TryGetArrayField(TEXT("queue"), QueueValues) && QueueValues)
			{
				for (const TSharedPtr<FJsonValue>& QueueValue : *QueueValues)
				{
					const FWorldDataQueuedPrompt Prompt = ParseQueuedPrompt(QueueValue.IsValid() ? QueueValue->AsObject() : nullptr);
					if (!Prompt.Text.IsEmpty() || !Prompt.Attachments.IsEmpty())
					{
						Conversation.QueuedPrompts.Add(Prompt);
					}
				}
			}
			Conversation.bIsRunning = false;
			Conversation.ContextSummaryThroughMessageIndex = FMath::Min(Conversation.ContextSummaryThroughMessageIndex, Conversation.Messages.Num());
			Conversation.Transcript = UnrealAgentMCPConversationModel::BuildConversationHistorySnapshot(Conversation.Messages);
			UnrealAgentMCPConversationModel::RefreshConversationContextMetrics(Conversation);
			Conversation.ActiveAssistantMessageIndex = INDEX_NONE;
			Conversation.ActiveTurnStatusMessageIndex = INDEX_NONE;
			const int32 AddedIndex = OutConversations.Add(MoveTemp(Conversation));
			if (ActiveId.IsValid() && OutConversations[AddedIndex].Id == ActiveId)
			{
				OutActiveConversationIndex = AddedIndex;
			}
		}

		if (!OutConversations.IsEmpty() && !OutConversations.IsValidIndex(OutActiveConversationIndex))
		{
			OutActiveConversationIndex = 0;
		}
		return true;
	}

	bool LoadHistoryFile(const FString& HistoryPath, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex)
	{
		FString JsonText;
		if (FFileHelper::LoadFileToString(JsonText, *HistoryPath) && TryParseHistory(JsonText, OutConversations, OutActiveConversationIndex))
		{
			return true;
		}

		const FString BackupPath = HistoryPath + TEXT(".bak");
		return FFileHelper::LoadFileToString(JsonText, *BackupPath) && TryParseHistory(JsonText, OutConversations, OutActiveConversationIndex);
	}

	bool SaveHistoryFile(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex)
	{
		FString JsonText;
		if (!TrySerializeHistory(Conversations, ActiveConversationIndex, JsonText))
		{
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(HistoryPath), true);
		const FString TemporaryPath = HistoryPath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(JsonText, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return false;
		}
		TArray<FWorldDataConversation> ValidationConversations;
		int32 ValidationActiveIndex = INDEX_NONE;
		if (!TryParseHistory(JsonText, ValidationConversations, ValidationActiveIndex))
		{
			IFileManager::Get().Delete(*TemporaryPath);
			return false;
		}

		FString AccessError;
		if (!UnrealAgentMCP::ServerEnvironment::RestrictFileAccessToCurrentUser(TemporaryPath, AccessError))
		{
			IFileManager::Get().Delete(*TemporaryPath);
			return false;
		}

		const FString BackupPath = HistoryPath + TEXT(".bak");
		FString ExistingJsonText;
		TArray<FWorldDataConversation> ExistingConversations;
		int32 ExistingActiveIndex = INDEX_NONE;
		if (FFileHelper::LoadFileToString(ExistingJsonText, *HistoryPath) && TryParseHistory(ExistingJsonText, ExistingConversations, ExistingActiveIndex))
		{
			if (IFileManager::Get().Copy(*BackupPath, *HistoryPath, true, true) != COPY_OK)
			{
				IFileManager::Get().Delete(*TemporaryPath);
				return false;
			}
			if (!UnrealAgentMCP::ServerEnvironment::RestrictFileAccessToCurrentUser(BackupPath, AccessError))
			{
				IFileManager::Get().Delete(*TemporaryPath);
				return false;
			}
		}

		const bool bMoved = IFileManager::Get().Move(*HistoryPath, *TemporaryPath, true, true, false, true);
		if (!bMoved)
		{
			return false;
		}
		return UnrealAgentMCP::ServerEnvironment::RestrictFileAccessToCurrentUser(HistoryPath, AccessError) &&
			AppendConversationAuditRecord(HistoryPath, Conversations, ActiveConversationIndex);
	}
}
