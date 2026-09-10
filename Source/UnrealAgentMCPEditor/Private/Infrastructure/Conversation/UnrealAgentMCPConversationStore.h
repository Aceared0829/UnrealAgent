// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPConversationStore.h
 * @brief 位于私有 Infrastructure 边界后的 ACP 会话历史持久化实现。
 */

#include "CoreMinimal.h"
#include "Tasks/Pipe.h"
#include "Application/Conversation/UnrealAgentMCPConversationRepository.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"

/** Application 会话仓储端口的 JSON 文件适配器。 */
class FUnrealAgentMCPConversationStore final : public IUnrealAgentMCPConversationRepository
{
public:
	virtual ~FUnrealAgentMCPConversationStore() override;

	virtual bool Load(const FString& HistoryPath, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex) override;

	virtual bool Save(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex) override;

	virtual void SaveAsync(const FString& HistoryPath, TArray<FWorldDataConversation> Conversations, int32 ActiveConversationIndex) override;

	virtual void FlushPendingSaves() override;

private:
	struct FSaveRequest
	{
		FString HistoryPath;
		TArray<FWorldDataConversation> Conversations;
		int32 ActiveConversationIndex = INDEX_NONE;
	};

	void DrainLatestSaveRequests();

	UE::Tasks::FPipe SavePipe{ TEXT("UnrealAgentMCPConversationSave") };
	FCriticalSection SaveRequestLock;
	TOptional<FSaveRequest> PendingSaveRequest;
	bool bSaveWorkerRunning = false;
};

namespace UnrealAgentMCPConversationStore
{
	/** 在不依赖 Slate 控件的前提下序列化会话历史。 */
	bool TrySerializeHistory(const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex, FString& OutJsonText);

	/** 解析会话历史，并依据稳定 GUID 恢复当前会话。 */
	bool TryParseHistory(const FString& JsonText, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex);

	bool LoadHistoryFile(const FString& HistoryPath, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex);

	/** 先写入同目录临时文件，再替换已保存的会话历史。 */
	bool SaveHistoryFile(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex);
}
