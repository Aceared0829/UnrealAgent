// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAnimationPort.h
 * @brief 动画领域对 Unreal 实现层的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPAnimationPort
	{
	public:
		virtual ~IUnrealAgentMCPAnimationPort() = default;
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
