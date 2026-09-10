// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPEditorPort.h
 * @brief 编辑器领域访问 Unreal Editor 能力时使用的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPEditorPort
	{
	public:
		virtual ~IUnrealAgentMCPEditorPort() = default;

		/** 执行已经通过应用服务动作白名单校验的编辑器操作。 */
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) = 0;

		/** 为需要跨帧完成的编辑器操作创建持久步骤执行器。 */
		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
