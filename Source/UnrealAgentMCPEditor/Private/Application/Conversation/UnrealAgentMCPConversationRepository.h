// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPConversationRepository.h
 * @brief 由 Application 层持有的会话持久化端口。
 */

#include "CoreMinimal.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"

/**
 * 供编辑器 Presentation 使用的稳定持久化契约。
 *
 * Application/UI 层无需了解会话最终存储在 JSON 文件、
 * 编辑器子系统还是未来的独立 Host 中。
 */
class IUnrealAgentMCPConversationRepository
{
public:
	virtual ~IUnrealAgentMCPConversationRepository() = default;

	virtual bool Load(const FString& HistoryPath, TArray<FWorldDataConversation>& OutConversations, int32& OutActiveConversationIndex) = 0;

	virtual bool Save(const FString& HistoryPath, const TArray<FWorldDataConversation>& Conversations, int32 ActiveConversationIndex) = 0;

	virtual void SaveAsync(const FString& HistoryPath, TArray<FWorldDataConversation> Conversations, int32 ActiveConversationIndex) = 0;

	virtual void FlushPendingSaves() = 0;
};
