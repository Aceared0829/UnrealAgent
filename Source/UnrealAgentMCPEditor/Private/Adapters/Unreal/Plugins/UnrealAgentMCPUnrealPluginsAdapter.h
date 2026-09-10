// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealPluginsAdapter.h
 * @brief 通过 UE IPluginManager 实现独立插件目录查询。
 */

#include "CoreMinimal.h"

class IPlugin;
class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealPluginsAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		FString List(const TSharedPtr<FJsonObject>& Args);
		FString Describe(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

	private:
		static TSharedRef<FJsonObject> MakeSummaryJson(const TSharedRef<IPlugin>& Plugin);
		static TSharedRef<FJsonObject> MakeDetailJson(const TSharedRef<IPlugin>& Plugin);
	};
}
