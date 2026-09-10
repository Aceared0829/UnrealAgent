// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPBlueprintPort.h
 * @brief Blueprint 资产、图、变量、函数与组件能力的应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPBlueprintPort
	{
	public:
		virtual ~IUnrealAgentMCPBlueprintPort() = default;
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
