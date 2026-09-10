// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.h
 * @brief Audio action execution.
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealAudioAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

	private:
		FString ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecutePlaybackAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteMetaSoundAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteCueAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteRoutingAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
