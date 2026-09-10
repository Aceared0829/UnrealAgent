// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealWhiteboxAdapter.h
 * @brief 语义白膜工作流的 Unreal 实现。
 */

#include "Application/Ports/UnrealAgentMCPWhiteboxPort.h"

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPLandscapePort;

	class FUnrealAgentMCPUnrealWhiteboxAdapter final : public IUnrealAgentMCPWhiteboxPort
	{
	public:
		explicit FUnrealAgentMCPUnrealWhiteboxAdapter(TSharedRef<IUnrealAgentMCPLandscapePort> InLandscapePort);

		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) override;

	private:
		TSharedPtr<Execution::IMcpTaskStepper> CreatePatchTaskStepper(const TSharedPtr<FJsonObject>& Args);

		TSharedRef<IUnrealAgentMCPLandscapePort> LandscapePort;
	};
}
