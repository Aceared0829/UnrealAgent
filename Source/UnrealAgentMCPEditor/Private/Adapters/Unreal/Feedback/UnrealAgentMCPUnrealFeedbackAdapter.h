// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealFeedbackAdapter.h
 * @brief 将反馈安全归档到项目 Saved 目录并生成独立路由信息。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealFeedbackAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		FString Submit(const TSharedPtr<FJsonObject>& Args);
		FString Route(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

	private:
		static FString DetermineRoute(const TSharedPtr<FJsonObject>& Args);
		static TSharedRef<FJsonObject> MakeRouteJson(const TSharedPtr<FJsonObject>& Args);
	};
}
