// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPNetworkingService.h
 * @brief Networking 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPNetworkingPort;

	class FUnrealAgentMCPNetworkingService
	{
	public:
		explicit FUnrealAgentMCPNetworkingService(TSharedRef<IUnrealAgentMCPNetworkingPort> InNetworkingPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPNetworkingPort> NetworkingPort;
	};
}
