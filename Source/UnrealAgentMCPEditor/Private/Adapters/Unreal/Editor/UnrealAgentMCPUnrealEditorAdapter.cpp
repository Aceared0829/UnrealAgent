// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.cpp
 * @brief 编辑器 Adapter 的动作分派、对象解析与反射调用公共实现。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealEditorAdapter::GetString(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names)
	{
		if (!Args.IsValid())
		{
			return FString();
		}
		for (const TCHAR* Name : Names)
		{
			FString Value;
			if (Args->TryGetStringField(Name, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	UObject* FUnrealAgentMCPUnrealEditorAdapter::ResolveObject(const TSharedPtr<FJsonObject>& Args, const bool bPreferPlayWorld, FString& OutError)
	{
		const FString ObjectPath = GetString(Args, { TEXT("objectPath"), TEXT("assetPath"), TEXT("path") });
		if (!ObjectPath.IsEmpty())
		{
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (!Object)
			{
				OutError = FString::Printf(TEXT("未找到对象：%s"), *ObjectPath);
				return nullptr;
			}
			if (UClass* Class = Cast<UClass>(Object))
			{
				return Class->GetDefaultObject();
			}
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
			{
				return Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : Object;
			}
			return Object;
		}

		const FString ActorLabel = GetString(Args, { TEXT("actorLabel"), TEXT("actorName") });
		UWorld* World = nullptr;
		if (GEditor)
		{
			World = bPreferPlayWorld && GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World();
		}
		if (!ActorLabel.IsEmpty() && World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) || It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
				{
					return *It;
				}
			}
			OutError = FString::Printf(TEXT("未找到 Actor：%s"), *ActorLabel);
			return nullptr;
		}
		OutError = TEXT("缺少 objectPath、assetPath、path 或 actorLabel。");
		return nullptr;
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::InvokeReflectedFunction(UObject* Target, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments)
	{
		if (!Target)
		{
			return ErrorJson(TEXT("反射调用目标为空。"));
		}
		UFunction* Function = Target->FindFunction(FName(*FunctionName));
		if (!Function)
		{
			return ErrorJson(FString::Printf(TEXT("对象 %s 上未找到函数 %s。"), *Target->GetPathName(), *FunctionName));
		}
		if (!Function->HasAnyFunctionFlags(FUNC_Public))
		{
			return ErrorJson(TEXT("只允许调用公开反射函数。"));
		}

		FStructOnScope ParameterStorage(Function);
		uint8* Parameters = ParameterStorage.GetStructMemory();
		Reflection::FPropertyTypeAdapterRegistry& Types = Reflection::FPropertyTypeAdapterRegistry::GetDefault();
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				continue;
			}
			const TSharedPtr<FJsonValue> Value = Arguments.IsValid() ? Arguments->TryGetField(Property->GetName()) : nullptr;
			if (!Value.IsValid())
			{
				continue;
			}
			FString Error;
			void* Address = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (!Types.Read(Value, Property, Address, Error))
			{
				return ErrorJson(FString::Printf(TEXT("参数 %s 转换失败：%s"), *Property->GetName(), *Error));
			}
		}

		Target->ProcessEvent(Function, Parameters);
		TSharedRef<FJsonObject> Outputs = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || !Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
			{
				continue;
			}
			TSharedPtr<FJsonValue> Value;
			FString Error;
			const void* Address = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (Types.Write(Property, Address, Value, Error) && Value.IsValid())
			{
				Outputs->SetField(Property->GetName(), Value);
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("target"), Target->GetPathName());
		Result->SetStringField(TEXT("function"), FunctionName);
		Result->SetObjectField(TEXT("outputs"), Outputs);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("execute_command") || Action == TEXT("execute_python") || Action == TEXT("run_python_file") || Action == TEXT("purge_python_modules") ||
			Action == TEXT("close_sequence") || Action == TEXT("open_tab") || Action == TEXT("open_settings") || Action == TEXT("hot_reload") || Action == TEXT("undo") ||
			Action == TEXT("redo") || Action == TEXT("open_asset") || Action == TEXT("reload_bridge") || Action == TEXT("save_dirty") || Action == TEXT("list_dirty_packages"))
		{
			return Session(Action, Args);
		}
		if (Action == TEXT("set_property") || Action == TEXT("get_property") || Action == TEXT("describe_object") || Action == TEXT("invoke_object_function") ||
			Action == TEXT("get_object_properties") || Action == TEXT("invoke_function") || Action == TEXT("invoke_static_function") || Action == TEXT("list_function_libraries"))
		{
			return Objects(Action, Args);
		}
		if (Action == TEXT("play_in_editor") || Action == TEXT("get_runtime_value") || Action == TEXT("get_runtime_values") || Action == TEXT("get_pie_pawn") ||
			Action == TEXT("list_pie_instances") || Action == TEXT("read_bone_transforms") || Action == TEXT("set_movement_mode") || Action == TEXT("teleport_runtime_actor") ||
			Action == TEXT("set_pie_time_scale") || Action == TEXT("configure_pie") || Action == TEXT("get_pie_config") || Action == TEXT("pie_set_player_view") ||
			Action == TEXT("stage_game_input"))
		{
			return Runtime(Action, Args);
		}
		if (Action == TEXT("set_realtime") || Action == TEXT("get_viewport") || Action == TEXT("set_viewport") || Action == TEXT("focus_on_actor") ||
			Action == TEXT("capture_screenshot") || Action == TEXT("capture_scene_png") || Action == TEXT("hit_test_viewport_pixel"))
		{
			return Viewport(Action, Args);
		}
		if (Action == TEXT("build_all") || Action == TEXT("build_geometry") || Action == TEXT("build_hlod") || Action == TEXT("validate_assets") ||
			Action == TEXT("cook_content") || Action == TEXT("run_automation_tests"))
		{
			return Build(Action, Args);
		}
		if (Action == TEXT("set_dialog_policy") || Action == TEXT("clear_dialog_policy") || Action == TEXT("get_dialog_policy") || Action == TEXT("list_dialogs") ||
			Action == TEXT("respond_to_dialog"))
		{
			return Dialogs(Action, Args);
		}
		if (Action == TEXT("create_sequence") || Action == TEXT("get_sequence_info") || Action == TEXT("add_sequence_track") || Action == TEXT("add_sequence_section") ||
			Action == TEXT("set_sequence_keyframes") || Action == TEXT("set_sequence_playback_range") || Action == TEXT("play_sequence"))
		{
			return Sequencer(Action, Args);
		}
		return Diagnostics(Action, Args);
	}
}
