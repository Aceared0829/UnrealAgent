// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.h
 * @brief Niagara action execution.
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealNiagaraAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

	private:
		FString ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteComponentAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteEmitterAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteStackAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
