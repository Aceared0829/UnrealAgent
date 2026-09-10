// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.cpp
 * @brief Gameplay 操作按物理导航、输入、AI 资产和框架进行分派。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealGameplayAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action.Contains(TEXT("collision")) || Action.Contains(TEXT("physics")) || Action == TEXT("add_impulse") || Action.Contains(TEXT("nav")))
			return PhysicsAndNavigation(Action, Args);
		if (Action.Contains(TEXT("input")) || Action.Contains(TEXT("imc")) || Action.Contains(TEXT("mapping")))
			return Input(Action, Args);
		if (Action.Contains(TEXT("behavior")) || Action.Contains(TEXT("bt_node")) || Action.Contains(TEXT("blackboard")) || Action.Contains(TEXT("eqs")) ||
			Action.Contains(TEXT("perception")) || Action.Contains(TEXT("sense")) || Action.Contains(TEXT("state_tree")) || Action.Contains(TEXT("smart_object")))
			return AIAndAssets(Action, Args);
		return Framework(Action, Args);
	}
}
