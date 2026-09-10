// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPFabService.h
 * @brief Fab 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPFabPort;

	class FUnrealAgentMCPFabService
	{
	public:
		explicit FUnrealAgentMCPFabService(TSharedRef<IUnrealAgentMCPFabPort> InFabPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPFabPort> FabPort;
	};
}
