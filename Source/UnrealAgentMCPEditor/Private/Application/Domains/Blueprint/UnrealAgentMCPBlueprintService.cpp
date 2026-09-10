// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPBlueprintService.cpp
 * @brief Blueprint 六十项操作白名单与应用层转发。
 */

#include "Application/Domains/Blueprint/UnrealAgentMCPBlueprintService.h"

#include "Application/Ports/UnrealAgentMCPBlueprintPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPBlueprintService::FUnrealAgentMCPBlueprintService(TSharedRef<IUnrealAgentMCPBlueprintPort> InBlueprintPort) : BlueprintPort(MoveTemp(InBlueprintPort))
	{
	}

	TArray<FString> FUnrealAgentMCPBlueprintService::GetImplementedActions()
	{
		return { TEXT("read"), TEXT("list_variables"), TEXT("list_functions"), TEXT("read_graph"), TEXT("read_graph_summary"), TEXT("get_execution_flow"), TEXT("get_dependencies"),
			TEXT("diff"), TEXT("create"), TEXT("add_variable"), TEXT("set_variable_properties"), TEXT("create_function"), TEXT("delete_function"), TEXT("rename_function"),
			TEXT("add_node"), TEXT("delete_node"), TEXT("set_node_property"), TEXT("connect_pins"), TEXT("add_component"), TEXT("remove_component"), TEXT("set_component_property"),
			TEXT("set_component_override_materials"), TEXT("add_timeline_track"), TEXT("set_capsule_size"), TEXT("get_component_property"), TEXT("set_class_default"),
			TEXT("delete_variable"), TEXT("add_function_parameter"), TEXT("set_variable_default"), TEXT("compile"), TEXT("list_node_types"), TEXT("search_node_types"),
			TEXT("create_interface"), TEXT("add_interface"), TEXT("override_function"), TEXT("list_overridable_functions"), TEXT("list_graphs"), TEXT("resolve_graph"),
			TEXT("add_event_dispatcher"), TEXT("duplicate"), TEXT("add_local_variable"), TEXT("list_local_variables"), TEXT("validate"), TEXT("read_component_properties"),
			TEXT("read_node_property"), TEXT("reparent_component"), TEXT("reparent"), TEXT("flush_ich"), TEXT("set_actor_tick_settings"), TEXT("export_nodes_t3d"),
			TEXT("import_nodes_t3d"), TEXT("set_cdo_property"), TEXT("get_cdo_properties"), TEXT("run_construction_script"), TEXT("compile_all"), TEXT("author"),
			TEXT("cleanup_graph"), TEXT("connect_pins_batch"), TEXT("set_node_position"), TEXT("auto_layout") };
	}

	FString FUnrealAgentMCPBlueprintService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TArray<FString> Actions = GetImplementedActions();
		if (!Actions.Contains(Action))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Name : Actions)
			{
				Values.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("blueprint"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Blueprint action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		return BlueprintPort->ExecuteAction(Action, Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>());
	}
}
