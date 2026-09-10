#pragma once

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.h
 * @brief 基于 Unreal 公共 Gameplay API 的独立适配器。
 */

#include "Application/Ports/UnrealAgentMCPGameplayPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealGameplayAdapter final : public IUnrealAgentMCPGameplayPort
	{
	public:
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) override;

	private:
		FString PhysicsAndNavigation(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Input(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString AIAndAssets(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Framework(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
