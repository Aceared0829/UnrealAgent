// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPEpicService.h
 * @brief Epic 元工具分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPEpicPort;

	class FUnrealAgentMCPEpicService
	{
	public:
		explicit FUnrealAgentMCPEpicService(TSharedRef<IUnrealAgentMCPEpicPort> InEpicPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPEpicPort> EpicPort;
	};
}
