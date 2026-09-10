// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPGASPort.h
 * @brief GAS 应用层访问宿主能力的抽象端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPGASPort
	{
	public:
		virtual ~IUnrealAgentMCPGASPort() = default;

		virtual FString AddASC(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateAttributeSet(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddAttribute(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateAbility(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetAbilityTags(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateEffect(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetEffectModifier(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateCue(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetASCDefaults(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ApplyEffect(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetAttribute(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetAttribute(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString InitASC(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetASCState(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
