// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAnimationService.h
 * @brief 动画领域应用服务声明。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPAnimationPort;

	class FUnrealAgentMCPAnimationService
	{
	public:
		explicit FUnrealAgentMCPAnimationService(TSharedRef<IUnrealAgentMCPAnimationPort> InPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPAnimationPort> Port;
	};
}
