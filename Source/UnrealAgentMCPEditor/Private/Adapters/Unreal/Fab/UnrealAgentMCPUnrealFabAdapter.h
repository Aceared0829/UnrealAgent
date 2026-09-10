// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealFabAdapter.h
 * @brief 通过动态模块探测、项目缓存与 AssetTools 实现 Fab 能力。
 */

#include "Application/Ports/UnrealAgentMCPFabPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealFabAdapter final : public IUnrealAgentMCPFabPort
	{
	public:
		virtual FString Status(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Login(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Logout(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SyncLibrary(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListCached(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CacheInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ClearCache(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ImportFile(const TSharedPtr<FJsonObject>& Args) override;

	private:
		static FString GetCacheRoot();
		static bool IsFabPluginAvailable(FString& OutReason);
	};
}
