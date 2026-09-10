// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPGameplayService.cpp
 * @brief Gameplay 五十七项操作白名单与应用层转发。
 */

#include "Application/Domains/Gameplay/UnrealAgentMCPGameplayService.h"

#include "Application/Ports/UnrealAgentMCPGameplayPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPGameplayService::FUnrealAgentMCPGameplayService(TSharedRef<IUnrealAgentMCPGameplayPort> InPort) : Port(MoveTemp(InPort))
	{
	}

	TArray<FString> FUnrealAgentMCPGameplayService::GetImplementedActions()
	{
		return { TEXT("set_collision_profile"), TEXT("set_simulate_physics"), TEXT("add_impulse"), TEXT("set_collision_enabled"), TEXT("set_collision"),
			TEXT("set_physics_properties"), TEXT("rebuild_navigation"), TEXT("find_nav_path"), TEXT("list_nav_invokers"), TEXT("get_navmesh_info"), TEXT("project_to_nav"),
			TEXT("spawn_nav_modifier"), TEXT("create_input_action"), TEXT("create_input_mapping"), TEXT("list_input_assets"), TEXT("read_imc"), TEXT("get_applied_imcs"),
			TEXT("list_input_mappings"), TEXT("add_imc_mapping"), TEXT("set_mapping_modifiers"), TEXT("remove_imc_mapping"), TEXT("set_imc_mapping_key"),
			TEXT("set_imc_mapping_action"), TEXT("list_behavior_trees"), TEXT("get_behavior_tree_info"), TEXT("read_behavior_tree_graph"), TEXT("create_blackboard"),
			TEXT("add_blackboard_key"), TEXT("remove_blackboard_key"), TEXT("set_blackboard_parent"), TEXT("read_blackboard"), TEXT("list_bt_node_classes"),
			TEXT("set_behavior_tree_blackboard"), TEXT("create_behavior_tree"), TEXT("create_eqs_query"), TEXT("list_eqs_queries"), TEXT("add_perception"), TEXT("configure_sense"),
			TEXT("get_state_tree_runtime"), TEXT("create_state_tree"), TEXT("list_state_trees"), TEXT("add_state_tree_component"), TEXT("create_smart_object_def"),
			TEXT("add_smart_object_component"), TEXT("add_smart_object_slot"), TEXT("set_smart_object_slot"), TEXT("remove_smart_object_slot"), TEXT("list_smart_object_slots"),
			TEXT("add_smart_object_slot_behavior"), TEXT("create_game_mode"), TEXT("create_game_state"), TEXT("create_player_controller"), TEXT("create_player_state"),
			TEXT("create_hud"), TEXT("set_world_game_mode"), TEXT("get_framework_info"), TEXT("get_navmesh_details") };
	}

	FString FUnrealAgentMCPGameplayService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args)
			Args->TryGetStringField(TEXT("action"), Action);
		const TArray<FString> Actions = GetImplementedActions();
		if (!Actions.Contains(Action))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("gameplay"));
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Gameplay action '%s' 尚未迁移。"), *Action));
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Name : Actions)
				Values.Add(MakeShared<FJsonValueString>(Name));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		return Port->ExecuteAction(Action, Args ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>());
	}
}
