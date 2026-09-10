// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.cpp
 * @brief 动画操作按资产、图、进阶系统与场景组件分派。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealAnimationAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("get_bone_transform") || Action == TEXT("list_bones") || Action == TEXT("rebind_leader_pose") || Action == TEXT("preview_animation"))
		{
			return Live(Action, Args);
		}
		if (Action.Contains(TEXT("state")) || Action.Contains(TEXT("transition")) || Action.Contains(TEXT("anim_graph")) || Action.Contains(TEXT("anim_nodes")) ||
			Action.Contains(TEXT("motion_matching")) || Action.Contains(TEXT("pose_history")) || Action.Contains(TEXT("sequence_evaluator")) || Action.Contains(TEXT("anim_node")))
		{
			return Graphs(Action, Args);
		}
		if (Action.Contains(TEXT("ik_")) || Action.Contains(TEXT("retarget")) || Action.Contains(TEXT("pose_search")) || Action.Contains(TEXT("mirror_data")) ||
			Action.Contains(TEXT("control_rig")) || Action.Contains(TEXT("modifier")))
		{
			return Advanced(Action, Args);
		}
		return Assets(Action, Args);
	}
}
