/**
 * @file UnrealAgentMCPSystemToolset.cpp
 * @brief Unreal Agent 系统 Toolset 的生产实现。
 */

#include "Core/Reflection/UnrealAgentMCPSystemToolset.h"

FString UUnrealAgentMCPSystemToolset::Ping() const
{
	return TEXT("pong");
}
