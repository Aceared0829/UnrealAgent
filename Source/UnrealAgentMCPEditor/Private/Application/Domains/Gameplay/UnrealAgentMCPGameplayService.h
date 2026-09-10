#pragma once

/**
 * @file UnrealAgentMCPGameplayService.h
 * @brief Gameplay 工具域的应用服务与能力白名单。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPGameplayPort;

	class FUnrealAgentMCPGameplayService final
	{
	public:
		explicit FUnrealAgentMCPGameplayService(TSharedRef<IUnrealAgentMCPGameplayPort> InPort);
		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPGameplayPort> Port;
	};
}
