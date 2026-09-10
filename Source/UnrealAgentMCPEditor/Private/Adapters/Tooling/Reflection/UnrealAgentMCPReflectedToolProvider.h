// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPReflectedToolProvider.h
 * @brief 将 Unreal Agent 自有 UFunction 反射工具接入统一 Registry。
 */

#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPReflectedToolProvider final : public IMcpToolProvider
	{
	public:
		FName GetProviderName() const override;

		void EnumerateTools(TArray<FMcpToolDescriptor>& OutTools, TArray<FString>& OutErrors) override;
	};
}
