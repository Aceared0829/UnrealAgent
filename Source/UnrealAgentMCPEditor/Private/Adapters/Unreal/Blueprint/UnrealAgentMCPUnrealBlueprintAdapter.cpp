// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.cpp
 * @brief Blueprint 操作到四个独立实现单元的分派。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealBlueprintAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		static const TSet<FString> CoreActions{ TEXT("read"), TEXT("list_variables"), TEXT("list_functions"), TEXT("create"), TEXT("add_variable"), TEXT("set_variable_properties"),
			TEXT("create_function"), TEXT("delete_function"), TEXT("rename_function"), TEXT("set_class_default"), TEXT("delete_variable"), TEXT("add_function_parameter"),
			TEXT("set_variable_default"), TEXT("compile"), TEXT("create_interface"), TEXT("add_interface"), TEXT("override_function"), TEXT("list_overridable_functions"),
			TEXT("list_graphs"), TEXT("resolve_graph"), TEXT("add_event_dispatcher"), TEXT("add_local_variable"), TEXT("list_local_variables"), TEXT("validate"), TEXT("reparent"),
			TEXT("flush_ich") };
		if (CoreActions.Contains(Action))
			return ExecuteCore(Action, Args);
		static const TSet<FString> GraphActions{ TEXT("read_graph"), TEXT("read_graph_summary"), TEXT("get_execution_flow"), TEXT("add_node"), TEXT("delete_node"),
			TEXT("set_node_property"), TEXT("connect_pins"), TEXT("list_node_types"), TEXT("search_node_types"), TEXT("read_node_property"), TEXT("export_nodes_t3d"),
			TEXT("import_nodes_t3d"), TEXT("cleanup_graph"), TEXT("connect_pins_batch"), TEXT("set_node_position"), TEXT("auto_layout") };
		if (GraphActions.Contains(Action))
			return ExecuteGraph(Action, Args);
		static const TSet<FString> ComponentActions{ TEXT("add_component"), TEXT("remove_component"), TEXT("set_component_property"), TEXT("set_component_override_materials"),
			TEXT("add_timeline_track"), TEXT("set_capsule_size"), TEXT("get_component_property"), TEXT("read_component_properties"), TEXT("reparent_component"),
			TEXT("set_actor_tick_settings"), TEXT("run_construction_script") };
		if (ComponentActions.Contains(Action))
			return ExecuteComponent(Action, Args);
		return ExecuteAdvanced(Action, Args);
	}
}
