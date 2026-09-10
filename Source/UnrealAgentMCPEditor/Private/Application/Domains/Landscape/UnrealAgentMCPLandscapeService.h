// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPLandscapeService.h
 * @brief Landscape 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPLandscapePort;

	class FUnrealAgentMCPLandscapeService
	{
	public:
		explicit FUnrealAgentMCPLandscapeService(TSharedRef<IUnrealAgentMCPLandscapePort> InLandscapePort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPLandscapePort> LandscapePort;
	};
}
