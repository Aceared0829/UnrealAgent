// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.h
 * @brief Widget action execution.
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealWidgetAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

	private:
		FString ExecuteQueryAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteTreeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteRuntimeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
