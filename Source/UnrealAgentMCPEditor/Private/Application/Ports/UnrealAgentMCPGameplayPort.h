#pragma once

/**
 * @file UnrealAgentMCPGameplayPort.h
 * @brief Gameplay 物理、导航、输入、AI 与框架能力的应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPGameplayPort
	{
	public:
		virtual ~IUnrealAgentMCPGameplayPort() = default;
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
