// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPEditorService.h
 * @brief 编辑器 Toolset 的应用服务、动作契约与端口调度。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPEditorPort;

	class FUnrealAgentMCPEditorService
	{
	public:
		explicit FUnrealAgentMCPEditorService(TSharedRef<IUnrealAgentMCPEditorPort> InEditorPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPEditorPort> EditorPort;
	};
}
