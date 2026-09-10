// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPWhiteboxPort.h
 * @brief 语义白膜可恢复任务的稳定应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPWhiteboxPort
	{
	public:
		virtual ~IUnrealAgentMCPWhiteboxPort() = default;

		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
