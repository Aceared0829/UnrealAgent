// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPCGService.h
 * @brief PCG 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPPCGPort;

	class FUnrealAgentMCPPCGService
	{
	public:
		explicit FUnrealAgentMCPPCGService(TSharedRef<IUnrealAgentMCPPCGPort> InPCGPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPPCGPort> PCGPort;
	};
}
