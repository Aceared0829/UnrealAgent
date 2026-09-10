// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.cpp
 * @brief 将 StateTree 操作分派到状态、节点、Transition、Binding 与参数实现。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealStateTreeAdapter::GetImplementedActions()
	{
		return { TEXT("read"), TEXT("list_states"), TEXT("add_state"), TEXT("remove_state"), TEXT("set_state_property"), TEXT("clear_state_nodes"), TEXT("add_task"),
			TEXT("add_enter_condition"), TEXT("remove_enter_condition"), TEXT("remove_task"), TEXT("set_task_instance_property"), TEXT("set_task_property"), TEXT("add_transition"),
			TEXT("add_transition_condition"), TEXT("remove_transition"), TEXT("add_binding"), TEXT("remove_binding"), TEXT("list_bindings"), TEXT("list_bindable_sources"),
			TEXT("add_evaluator"), TEXT("remove_evaluator"), TEXT("set_evaluator_instance_property"), TEXT("set_evaluator_property"), TEXT("add_global_task"),
			TEXT("remove_global_task"), TEXT("set_global_task_instance_property"), TEXT("set_global_task_property"), TEXT("list_colors"), TEXT("add_color"),
			TEXT("list_state_parameters"), TEXT("add_state_parameter"), TEXT("remove_state_parameter"), TEXT("set_state_parameter"), TEXT("set_root_parameters"), TEXT("compile"),
			TEXT("validate") };
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
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
			Result->SetStringField(TEXT("domain"), TEXT("statetree"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("StateTree action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		return ExecuteAction(Action, SafeArgs);
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		static const TSet<FString> StateActions{ TEXT("read"), TEXT("list_states"), TEXT("add_state"), TEXT("remove_state"), TEXT("set_state_property"), TEXT("clear_state_nodes"),
			TEXT("list_colors"), TEXT("add_color"), TEXT("compile"), TEXT("validate") };
		if (StateActions.Contains(Action))
		{
			return ExecuteStateAction(Action, Args);
		}
		static const TSet<FString> NodeActions{ TEXT("add_task"), TEXT("add_enter_condition"), TEXT("remove_enter_condition"), TEXT("remove_task"),
			TEXT("set_task_instance_property"), TEXT("set_task_property"), TEXT("add_evaluator"), TEXT("remove_evaluator"), TEXT("set_evaluator_instance_property"),
			TEXT("set_evaluator_property"), TEXT("add_global_task"), TEXT("remove_global_task"), TEXT("set_global_task_instance_property"), TEXT("set_global_task_property") };
		if (NodeActions.Contains(Action))
		{
			return ExecuteNodeAction(Action, Args);
		}
		if (Action == TEXT("add_transition") || Action == TEXT("add_transition_condition") || Action == TEXT("remove_transition"))
		{
			return ExecuteTransitionAction(Action, Args);
		}
		if (Action == TEXT("add_binding") || Action == TEXT("remove_binding") || Action == TEXT("list_bindings") || Action == TEXT("list_bindable_sources"))
		{
			return ExecuteBindingAction(Action, Args);
		}
		return ExecuteParameterAction(Action, Args);
	}
}
