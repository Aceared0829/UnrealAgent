// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.h
 * @brief StateTree action execution.
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealStateTreeAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

	private:
		FString ExecuteStateAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteNodeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteTransitionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteBindingAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteParameterAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
