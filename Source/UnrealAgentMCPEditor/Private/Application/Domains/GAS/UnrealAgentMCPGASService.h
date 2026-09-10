// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPGASService.h
 * @brief GAS 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPGASPort;

	class FUnrealAgentMCPGASService
	{
	public:
		explicit FUnrealAgentMCPGASService(TSharedRef<IUnrealAgentMCPGASPort> InGASPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPGASPort> GASPort;
	};
}
