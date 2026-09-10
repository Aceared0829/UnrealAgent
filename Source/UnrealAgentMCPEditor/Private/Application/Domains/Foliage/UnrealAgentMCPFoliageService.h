// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPFoliageService.h
 * @brief Foliage 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPFoliagePort;

	class FUnrealAgentMCPFoliageService
	{
	public:
		explicit FUnrealAgentMCPFoliageService(TSharedRef<IUnrealAgentMCPFoliagePort> InFoliagePort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPFoliagePort> FoliagePort;
	};
}
