// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPProjectService.h
 * @brief Project 分类命令的应用层路由与参数兼容服务。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPProjectPort;

	class FUnrealAgentMCPProjectService
	{
	public:
		explicit FUnrealAgentMCPProjectService(TSharedRef<IUnrealAgentMCPProjectPort> InProjectPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPProjectPort> ProjectPort;
	};
}
