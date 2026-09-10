// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPWhiteboxService.h
 * @brief 语义白膜与体块工作流的应用服务。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPWhiteboxPort;

	class FUnrealAgentMCPWhiteboxService
	{
	public:
		explicit FUnrealAgentMCPWhiteboxService(TSharedRef<IUnrealAgentMCPWhiteboxPort> InWhiteboxPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPWhiteboxPort> WhiteboxPort;
	};
}
