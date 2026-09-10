// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPLevelService.h
 * @brief Level 分类命令的应用层路由、参数兼容与能力声明。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPLevelPort;

	class FUnrealAgentMCPLevelService
	{
	public:
		explicit FUnrealAgentMCPLevelService(TSharedRef<IUnrealAgentMCPLevelPort> InLevelPort);

		/** 执行单个 Level action，并拒绝未迁移的空壳 action。 */
		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const;

		/** 返回当前已迁移的外部兼容 action 名称。 */
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPLevelPort> LevelPort;
	};
}
