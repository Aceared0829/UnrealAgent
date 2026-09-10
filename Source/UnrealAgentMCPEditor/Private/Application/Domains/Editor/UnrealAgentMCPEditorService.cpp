// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEditorService.cpp
 * @brief 编辑器应用服务实现；只负责动作契约，不依赖 Unreal 编辑器类型。
 */

#include "Application/Domains/Editor/UnrealAgentMCPEditorService.h"

#include "Application/Ports/UnrealAgentMCPEditorPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPEditorService::FUnrealAgentMCPEditorService(TSharedRef<IUnrealAgentMCPEditorPort> InEditorPort) : EditorPort(MoveTemp(InEditorPort))
	{
	}

	TArray<FString> FUnrealAgentMCPEditorService::GetImplementedActions()
	{
		return { TEXT("execute_command"), TEXT("execute_python"), TEXT("run_python_file"), TEXT("purge_python_modules"), TEXT("close_sequence"), TEXT("open_tab"),
			TEXT("open_settings"), TEXT("set_property"), TEXT("get_property"), TEXT("describe_object"), TEXT("play_in_editor"), TEXT("get_runtime_value"),
			TEXT("get_runtime_values"), TEXT("get_pie_pawn"), TEXT("list_pie_instances"), TEXT("read_bone_transforms"), TEXT("set_movement_mode"), TEXT("teleport_runtime_actor"),
			TEXT("invoke_object_function"), TEXT("get_object_properties"), TEXT("invoke_function"), TEXT("invoke_static_function"), TEXT("list_function_libraries"),
			TEXT("set_pie_time_scale"), TEXT("configure_pie"), TEXT("get_pie_config"), TEXT("pie_set_player_view"), TEXT("stage_game_input"), TEXT("hot_reload"), TEXT("undo"),
			TEXT("redo"), TEXT("get_perf_stats"), TEXT("run_stat"), TEXT("set_scalability"), TEXT("set_cvars"), TEXT("set_realtime"), TEXT("get_viewport"), TEXT("set_viewport"),
			TEXT("focus_on_actor"), TEXT("capture_screenshot"), TEXT("capture_scene_png"), TEXT("hit_test_viewport_pixel"), TEXT("get_log"), TEXT("search_log"),
			TEXT("get_message_log"), TEXT("list_crashes"), TEXT("get_crash_info"), TEXT("check_for_crashes"), TEXT("open_asset"), TEXT("reload_bridge"), TEXT("save_dirty"),
			TEXT("list_dirty_packages"), TEXT("get_build_status"), TEXT("build_all"), TEXT("build_geometry"), TEXT("build_hlod"), TEXT("validate_assets"), TEXT("cook_content"),
			TEXT("set_dialog_policy"), TEXT("clear_dialog_policy"), TEXT("get_dialog_policy"), TEXT("list_dialogs"), TEXT("respond_to_dialog"), TEXT("run_automation_tests"),
			TEXT("create_sequence"), TEXT("get_sequence_info"), TEXT("add_sequence_track"), TEXT("add_sequence_section"), TEXT("set_sequence_keyframes"),
			TEXT("set_sequence_playback_range"), TEXT("play_sequence") };
	}

	FString FUnrealAgentMCPEditorService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TArray<FString> Implemented = GetImplementedActions();
		if (Implemented.Contains(Action))
		{
			return EditorPort->ExecuteAction(Action, Args);
		}

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : Implemented)
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("editor"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("编辑器 action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPEditorService::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const
	{
		return EditorPort->CreateTaskStepper(Args);
	}
}
