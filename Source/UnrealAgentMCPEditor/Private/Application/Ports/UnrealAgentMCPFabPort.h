// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPFabPort.h
 * @brief Fab 会话、缓存与本地内容导入的稳定应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPFabPort
	{
	public:
		virtual ~IUnrealAgentMCPFabPort() = default;

		virtual FString Status(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Login(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Logout(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SyncLibrary(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListCached(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CacheInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ClearCache(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ImportFile(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
