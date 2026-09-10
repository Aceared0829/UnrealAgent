// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.cpp
 * @brief Material action 到资产、实例、表达式图和检查实现的分组路由。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"

#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealMaterialAdapter::GetImplementedActions()
	{
		return { TEXT("read"), TEXT("list_parameters"), TEXT("set_parameter"), TEXT("read_instance"), TEXT("set_instance_parent"), TEXT("batch_set_instances"),
			TEXT("clear_instance_parameters"), TEXT("list_static_switches"), TEXT("set_static_switch"), TEXT("set_expression_value"), TEXT("set_custom_expression"),
			TEXT("disconnect_property"), TEXT("create_instance"), TEXT("create"), TEXT("create_function"), TEXT("add_function_expression"), TEXT("connect_function_expressions"),
			TEXT("list_function_expressions"), TEXT("create_simple"), TEXT("set_usage"), TEXT("set_shading_model"), TEXT("set_blend_mode"), TEXT("set_domain"),
			TEXT("set_base_color"), TEXT("connect_texture"), TEXT("add_expression"), TEXT("connect_expressions"), TEXT("connect_to_property"), TEXT("list_expressions"),
			TEXT("delete_expression"), TEXT("list_expression_types"), TEXT("recompile"), TEXT("duplicate"), TEXT("validate"), TEXT("get_shader_stats"), TEXT("export_graph"),
			TEXT("import_graph"), TEXT("build_graph"), TEXT("render_preview"), TEXT("begin_transaction"), TEXT("end_transaction") };
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
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
			Result->SetStringField(TEXT("domain"), TEXT("material"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Material action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		return ExecuteAction(Action, SafeArgs);
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("begin_transaction"))
		{
			if (ActiveTransaction.IsValid())
			{
				return ErrorJson(TEXT("已有材质事务正在进行。"));
			}
			FString Label = TEXT("Unreal Agent 材质编辑");
			Args->TryGetStringField(TEXT("label"), Label);
			ActiveTransaction = MakeUnique<Transactions::FUnrealAgentMCPScopedEditorTransaction>(FText::FromString(Label));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("active"), true);
			Result->SetStringField(TEXT("label"), Label);
			return SuccessJson(Result);
		}
		if (Action == TEXT("end_transaction"))
		{
			const bool bWasActive = ActiveTransaction.IsValid();
			ActiveTransaction.Reset();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("active"), false);
			Result->SetBoolField(TEXT("ended"), bWasActive);
			return SuccessJson(Result);
		}

		static const TSet<FString> InstanceActions{ TEXT("set_parameter"), TEXT("read_instance"), TEXT("set_instance_parent"), TEXT("batch_set_instances"),
			TEXT("clear_instance_parameters"), TEXT("list_static_switches"), TEXT("set_static_switch"), TEXT("create_instance") };
		if (InstanceActions.Contains(Action))
		{
			return ExecuteInstanceAction(Action, Args);
		}

		static const TSet<FString> FunctionActions{ TEXT("create_function"), TEXT("add_function_expression"), TEXT("connect_function_expressions"),
			TEXT("list_function_expressions") };
		if (FunctionActions.Contains(Action))
		{
			return ExecuteFunctionAction(Action, Args);
		}

		static const TSet<FString> GraphActions{ TEXT("set_expression_value"), TEXT("set_custom_expression"), TEXT("disconnect_property"), TEXT("set_base_color"),
			TEXT("connect_texture"), TEXT("add_expression"), TEXT("connect_expressions"), TEXT("connect_to_property"), TEXT("delete_expression"), TEXT("import_graph"),
			TEXT("build_graph") };
		if (GraphActions.Contains(Action))
		{
			return ExecuteGraphAction(Action, Args);
		}

		static const TSet<FString> InspectionActions{ TEXT("read"), TEXT("list_parameters"), TEXT("list_expressions"), TEXT("list_expression_types"), TEXT("validate"),
			TEXT("get_shader_stats"), TEXT("export_graph") };
		if (InspectionActions.Contains(Action))
		{
			return ExecuteInspectionAction(Action, Args);
		}

		return ExecuteAssetAction(Action, Args);
	}
}
