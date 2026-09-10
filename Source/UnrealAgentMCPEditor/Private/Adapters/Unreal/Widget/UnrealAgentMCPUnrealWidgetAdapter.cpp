// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.cpp
 * @brief Widget action 到查询、资产、树编辑和运行态实现的分组路由。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealWidgetAdapter::GetImplementedActions()
	{
		return { TEXT("read_tree"), TEXT("get_details"), TEXT("get_properties"), TEXT("list_bindings"), TEXT("clear_binding"), TEXT("set_property"), TEXT("set_style"),
			TEXT("reorder_child"), TEXT("bulk_set_properties"), TEXT("list"), TEXT("read_animations"), TEXT("create"), TEXT("create_utility_widget"), TEXT("run_utility_widget"),
			TEXT("create_utility_blueprint"), TEXT("run_utility_blueprint"), TEXT("add_widget"), TEXT("remove_widget"), TEXT("move_widget"), TEXT("set_root"), TEXT("wrap_root"),
			TEXT("list_classes"), TEXT("list_runtime"), TEXT("get_runtime"), TEXT("get_runtime_delegates"), TEXT("add_to_viewport"), TEXT("invoke_runtime_function") };
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
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
			Result->SetStringField(TEXT("domain"), TEXT("widget"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Widget action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		return ExecuteAction(Action, SafeArgs);
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		static const TSet<FString> QueryActions{ TEXT("read_tree"), TEXT("get_details"), TEXT("get_properties"), TEXT("list_bindings"), TEXT("list"), TEXT("read_animations"),
			TEXT("list_classes") };
		if (QueryActions.Contains(Action))
		{
			return ExecuteQueryAction(Action, Args);
		}

		static const TSet<FString> AssetActions{ TEXT("create"), TEXT("create_utility_widget"), TEXT("run_utility_widget"), TEXT("create_utility_blueprint"),
			TEXT("run_utility_blueprint") };
		if (AssetActions.Contains(Action))
		{
			return ExecuteAssetAction(Action, Args);
		}

		static const TSet<FString> RuntimeActions{ TEXT("list_runtime"), TEXT("get_runtime"), TEXT("get_runtime_delegates"), TEXT("add_to_viewport"),
			TEXT("invoke_runtime_function") };
		if (RuntimeActions.Contains(Action))
		{
			return ExecuteRuntimeAction(Action, Args);
		}

		return ExecuteTreeAction(Action, Args);
	}
}
