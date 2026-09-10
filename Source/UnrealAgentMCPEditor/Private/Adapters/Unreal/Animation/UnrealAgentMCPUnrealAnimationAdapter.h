// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.h
 * @brief UE 5.8 动画领域独立适配器。
 */

#include "Application/Ports/UnrealAgentMCPAnimationPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealAnimationAdapter final : public IUnrealAgentMCPAnimationPort
	{
	public:
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) override;

	private:
		FString Assets(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Graphs(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Advanced(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Live(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
