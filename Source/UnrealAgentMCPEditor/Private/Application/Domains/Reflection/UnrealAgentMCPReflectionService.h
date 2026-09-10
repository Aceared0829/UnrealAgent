// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPReflectionService.h
 * @brief Reflection 分类的应用服务和 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPReflectionPort;

	class FUnrealAgentMCPReflectionService
	{
	public:
		explicit FUnrealAgentMCPReflectionService(TSharedRef<IUnrealAgentMCPReflectionPort> InReflectionPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPReflectionPort> ReflectionPort;
	};
}
