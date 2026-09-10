// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAnimationService.cpp
 * @brief 动画八十四项操作白名单与应用层转发。
 */

#include "Application/Domains/Animation/UnrealAgentMCPAnimationService.h"

#include "Application/Ports/UnrealAgentMCPAnimationPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPAnimationService::FUnrealAgentMCPAnimationService(TSharedRef<IUnrealAgentMCPAnimationPort> InPort) : Port(MoveTemp(InPort))
	{
	}

	TArray<FString> FUnrealAgentMCPAnimationService::GetImplementedActions()
	{
		return { TEXT("read_anim_blueprint"), TEXT("read_montage"), TEXT("read_sequence"), TEXT("scan_animation_tracks"), TEXT("read_blendspace"), TEXT("add_blend_sample"),
			TEXT("set_blend_sample"), TEXT("list"), TEXT("create_montage"), TEXT("create_anim_blueprint"), TEXT("create_blendspace"), TEXT("create_blendspace_1d"),
			TEXT("populate_blendspace"), TEXT("add_notify"), TEXT("remove_notify"), TEXT("get_skeleton_info"), TEXT("list_sockets"), TEXT("list_skeletal_meshes"),
			TEXT("get_physics_asset"), TEXT("create_sequence"), TEXT("set_bone_keyframes"), TEXT("bake_keyframes_batch"), TEXT("get_bone_transforms"), TEXT("inspect_anim_nodes"),
			TEXT("compare_curves_to_morph_targets"), TEXT("set_montage_sequence"), TEXT("set_montage_properties"), TEXT("create_state_machine"), TEXT("add_state"),
			TEXT("add_transition"), TEXT("set_state_animation"), TEXT("set_transition_blend"), TEXT("set_transition_condition"), TEXT("read_state_machine"),
			TEXT("read_anim_graph"), TEXT("add_curve"), TEXT("set_anim_curve_keys"), TEXT("apply_animation_modifier"), TEXT("set_montage_slot"), TEXT("add_montage_section"),
			TEXT("create_ik_rig"), TEXT("read_ik_rig"), TEXT("list_control_rig_variables"), TEXT("read_control_rig_graph"), TEXT("read_control_rig_hierarchy"),
			TEXT("set_root_motion"), TEXT("add_virtual_bone"), TEXT("remove_virtual_bone"), TEXT("create_composite"), TEXT("list_modifiers"), TEXT("create_ik_retargeter"),
			TEXT("read_ik_retargeter"), TEXT("set_ik_rig_mesh"), TEXT("set_ik_retargeter_rig"), TEXT("auto_align_retarget_pose"), TEXT("reset_retarget_pose"),
			TEXT("batch_retarget_animations"), TEXT("set_anim_blueprint_skeleton"), TEXT("read_bone_track"), TEXT("create_pose_search_database"), TEXT("set_pose_search_schema"),
			TEXT("add_pose_search_sequence"), TEXT("set_pose_search_clips"), TEXT("build_pose_search_index"), TEXT("read_pose_search_database"),
			TEXT("set_pose_search_database_settings"), TEXT("create_pose_search_schema"), TEXT("add_pose_search_schema_pose_channel"),
			TEXT("add_pose_search_schema_trajectory_channel"), TEXT("read_pose_search_schema"), TEXT("create_mirror_data_table"), TEXT("read_mirror_data_table"),
			TEXT("create_pose_search_normalization_set"), TEXT("add_motion_matching_node"), TEXT("add_pose_history_node"), TEXT("set_motion_matching_chooser"),
			TEXT("add_sequence_evaluator"), TEXT("bind_anim_node_function"), TEXT("set_sequence_properties"), TEXT("bake_root_motion_from_bone"), TEXT("get_bone_transform"),
			TEXT("list_bones"), TEXT("rebind_leader_pose"), TEXT("preview_animation") };
	}

	FString FUnrealAgentMCPAnimationService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args)
			Args->TryGetStringField(TEXT("action"), Action);
		const TArray<FString> Actions = GetImplementedActions();
		if (!Actions.Contains(Action))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("animation"));
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Animation action '%s' 尚未迁移。"), *Action));
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Name : Actions)
			{
				Values.Add(MakeShared<FJsonValueString>(Name));
			}
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		return Port->ExecuteAction(Action, Args ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>());
	}
}
