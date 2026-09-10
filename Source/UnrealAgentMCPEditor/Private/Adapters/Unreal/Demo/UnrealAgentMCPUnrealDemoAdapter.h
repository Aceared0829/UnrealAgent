// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealDemoAdapter.h
 * @brief Neon Shrine 示例场景的 UE 5.8 独立实现。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealDemoAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();
	};
}
