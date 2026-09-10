// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLevelService.cpp
 * @brief Level 应用服务实现；不直接依赖 Unreal Editor 场景类型。
 */

#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"

#include "Application/Ports/UnrealAgentMCPLevelPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> NormalizeArguments(const TSharedPtr<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Normalized = MakeShared<FJsonObject>();
			if (Args.IsValid())
			{
				Normalized->Values = Args->Values;
			}

			auto CopyAlias = [&Normalized](const TCHAR* Target, std::initializer_list<const TCHAR*> Aliases)
			{
				if (Normalized->HasField(Target))
				{
					return;
				}
				for (const TCHAR* Alias : Aliases)
				{
					const TSharedPtr<FJsonValue> Value = Normalized->TryGetField(Alias);
					if (Value.IsValid())
					{
						Normalized->SetField(Target, Value);
						return;
					}
				}
			};

			CopyAlias(TEXT("name"), { TEXT("actorLabel"), TEXT("actorName"), TEXT("label") });
			CopyAlias(TEXT("class"), { TEXT("className"), TEXT("actorClass") });
			CopyAlias(TEXT("names"), { TEXT("actors"), TEXT("actorLabels"), TEXT("actorNames") });
			return Normalized;
		}

		FString UnsupportedAction(const FString& Action)
		{
			TArray<TSharedPtr<FJsonValue>> Actions;
			for (const FString& Name : FUnrealAgentMCPLevelService::GetImplementedActions())
			{
				Actions.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("level"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Level action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Actions);
			return JsonObjectToString(Result);
		}
	}

	FUnrealAgentMCPLevelService::FUnrealAgentMCPLevelService(TSharedRef<IUnrealAgentMCPLevelPort> InLevelPort) : LevelPort(MoveTemp(InLevelPort))
	{
	}

	TArray<FString> FUnrealAgentMCPLevelService::GetImplementedActions()
	{
		return { TEXT("get_outliner"), TEXT("set_editor_visibility"), TEXT("place_actor"), TEXT("delete_actor"), TEXT("delete_actors"), TEXT("delete_by_folder"),
			TEXT("get_actor_details"), TEXT("move_actor"), TEXT("select"), TEXT("get_selected"), TEXT("get_current"), TEXT("save"), TEXT("list"), TEXT("get_actors_by_class"),
			TEXT("count_actors_by_class"), TEXT("get_actor_bounds"), TEXT("get_component_tree"), TEXT("get_relative_transform"), TEXT("resolve_actor"), TEXT("set_actor_property"),
			TEXT("set_actor_folder_path"), TEXT("add_actor_tag"), TEXT("remove_actor_tag"), TEXT("set_actor_tags"), TEXT("list_actor_tags"), TEXT("attach_actor"),
			TEXT("detach_actor"), TEXT("set_actor_mobility"), TEXT("aim_actor_at"), TEXT("nav_project_point"), TEXT("add_component"), TEXT("remove_component"),
			TEXT("set_component_property"), TEXT("get_component_details"), TEXT("get_actors_by_component_class"), TEXT("get_spline_info"), TEXT("set_spline_points"),
			TEXT("set_actor_material"), TEXT("read_actor_motion"), TEXT("add_hismc_instances"), TEXT("get_instance_transforms"), TEXT("update_instance_transform"),
			TEXT("remove_instance"), TEXT("line_trace"), TEXT("snap_actor_to_floor"), TEXT("load"), TEXT("create"), TEXT("spawn_volume"), TEXT("list_volumes"),
			TEXT("set_volume_properties"), TEXT("spawn_light"), TEXT("set_light_properties"), TEXT("set_fog_properties"), TEXT("get_runtime_virtual_texture_summary"),
			TEXT("set_water_body_property"), TEXT("build_lighting"), TEXT("get_world_settings"), TEXT("set_world_settings"), TEXT("set_nanite_settings"), TEXT("get_nanite_info"),
			TEXT("add_post_process_blendable"), TEXT("export_actor_fbx"), TEXT("spawn_skeletal_mesh_actor"), TEXT("list_actor_descs"), TEXT("load_actor_descs"),
			TEXT("get_current_edit_level"), TEXT("set_current_edit_level"), TEXT("list_streaming_sublevels"), TEXT("add_streaming_sublevel"), TEXT("remove_streaming_sublevel"),
			TEXT("set_streaming_sublevel_properties"), TEXT("spawn_grid"), TEXT("batch_translate"), TEXT("place_actors_batch") };
	}

	FString FUnrealAgentMCPLevelService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> Normalized = NormalizeArguments(Args);

		if (Action == TEXT("get_outliner") || Action == TEXT("get_current") || Action == TEXT("list"))
			return LevelPort->ListActors(Normalized);
		if (Action == TEXT("get_selected"))
			return LevelPort->GetSelectedActors(Normalized);
		if (Action == TEXT("get_actor_details"))
			return LevelPort->GetActorDetails(Normalized);
		if (Action == TEXT("place_actor"))
			return LevelPort->PlaceActor(Normalized);
		if (Action == TEXT("delete_actor"))
			return LevelPort->DeleteActor(Normalized);
		if (Action == TEXT("delete_actors") || Action == TEXT("delete_by_folder"))
		{
			return ErrorJson(FString::Printf(TEXT("%s 必须通过可恢复任务执行器运行。"), *Action));
		}
		if (Action == TEXT("move_actor"))
			return LevelPort->MoveActor(Normalized);
		if (Action == TEXT("select"))
			return LevelPort->SelectActor(Normalized);
		if (Action == TEXT("attach_actor"))
			return LevelPort->AttachActor(Normalized);
		if (Action == TEXT("detach_actor"))
			return LevelPort->DetachActor(Normalized);
		if (Action == TEXT("set_actor_property"))
			return LevelPort->SetActorProperty(Normalized);
		if (Action == TEXT("save"))
			return LevelPort->SaveLevel(Normalized);
		if (Action == TEXT("set_editor_visibility"))
			return LevelPort->SetEditorVisibility(Normalized);
		if (Action == TEXT("get_actors_by_class"))
			return LevelPort->GetActorsByClass(Normalized);
		if (Action == TEXT("count_actors_by_class"))
			return LevelPort->CountActorsByClass(Normalized);
		if (Action == TEXT("get_actor_bounds"))
			return LevelPort->GetActorBounds(Normalized);
		if (Action == TEXT("get_component_tree"))
			return LevelPort->GetComponentTree(Normalized);
		if (Action == TEXT("get_relative_transform"))
			return LevelPort->GetRelativeTransform(Normalized);
		if (Action == TEXT("resolve_actor"))
			return LevelPort->ResolveActor(Normalized);
		if (Action == TEXT("set_actor_folder_path"))
			return LevelPort->SetActorFolderPath(Normalized);
		if (Action == TEXT("add_actor_tag"))
			return LevelPort->AddActorTag(Normalized);
		if (Action == TEXT("remove_actor_tag"))
			return LevelPort->RemoveActorTag(Normalized);
		if (Action == TEXT("set_actor_tags"))
			return LevelPort->SetActorTags(Normalized);
		if (Action == TEXT("list_actor_tags"))
			return LevelPort->ListActorTags(Normalized);
		if (Action == TEXT("set_actor_mobility"))
			return LevelPort->SetActorMobility(Normalized);
		if (Action == TEXT("aim_actor_at"))
			return LevelPort->AimActorAt(Normalized);
		if (Action == TEXT("nav_project_point"))
			return LevelPort->NavProjectPoint(Normalized);
		if (Action == TEXT("add_component"))
			return LevelPort->AddComponent(Normalized);
		if (Action == TEXT("remove_component"))
			return LevelPort->RemoveComponent(Normalized);
		if (Action == TEXT("set_component_property"))
			return LevelPort->SetComponentProperty(Normalized);
		if (Action == TEXT("get_component_details"))
			return LevelPort->GetComponentDetails(Normalized);
		if (Action == TEXT("get_actors_by_component_class"))
			return LevelPort->GetActorsByComponentClass(Normalized);
		if (Action == TEXT("get_spline_info"))
			return LevelPort->GetSplineInfo(Normalized);
		if (Action == TEXT("set_spline_points"))
			return LevelPort->SetSplinePoints(Normalized);
		if (Action == TEXT("set_actor_material"))
			return LevelPort->SetActorMaterial(Normalized);
		if (Action == TEXT("read_actor_motion"))
			return LevelPort->ReadActorMotion(Normalized);
		if (Action == TEXT("add_hismc_instances"))
			return LevelPort->AddHISMCInstances(Normalized);
		if (Action == TEXT("get_instance_transforms"))
			return LevelPort->GetInstanceTransforms(Normalized);
		if (Action == TEXT("update_instance_transform"))
			return LevelPort->UpdateInstanceTransform(Normalized);
		if (Action == TEXT("remove_instance"))
			return LevelPort->RemoveInstance(Normalized);
		if (Action == TEXT("line_trace"))
			return LevelPort->LineTrace(Normalized);
		if (Action == TEXT("snap_actor_to_floor"))
			return LevelPort->SnapActorToFloor(Normalized);
		if (Action == TEXT("load"))
			return LevelPort->LoadLevel(Normalized);
		if (Action == TEXT("create"))
			return LevelPort->CreateLevel(Normalized);
		if (Action == TEXT("spawn_volume"))
			return LevelPort->SpawnVolume(Normalized);
		if (Action == TEXT("list_volumes"))
			return LevelPort->ListVolumes(Normalized);
		if (Action == TEXT("set_volume_properties"))
			return LevelPort->SetVolumeProperties(Normalized);
		if (Action == TEXT("spawn_light"))
			return LevelPort->SpawnLight(Normalized);
		if (Action == TEXT("set_light_properties"))
			return LevelPort->SetLightProperties(Normalized);
		if (Action == TEXT("set_fog_properties"))
			return LevelPort->SetFogProperties(Normalized);
		if (Action == TEXT("get_runtime_virtual_texture_summary"))
			return LevelPort->GetRuntimeVirtualTextureSummary(Normalized);
		if (Action == TEXT("set_water_body_property"))
			return LevelPort->SetWaterBodyProperty(Normalized);
		if (Action == TEXT("build_lighting"))
			return LevelPort->BuildLighting(Normalized);
		if (Action == TEXT("get_world_settings"))
			return LevelPort->GetWorldSettings(Normalized);
		if (Action == TEXT("set_world_settings"))
			return LevelPort->SetWorldSettings(Normalized);
		if (Action == TEXT("set_nanite_settings"))
			return LevelPort->SetNaniteSettings(Normalized);
		if (Action == TEXT("get_nanite_info"))
			return LevelPort->GetNaniteInfo(Normalized);
		if (Action == TEXT("add_post_process_blendable"))
			return LevelPort->AddPostProcessBlendable(Normalized);
		if (Action == TEXT("export_actor_fbx"))
			return LevelPort->ExportActorFbx(Normalized);
		if (Action == TEXT("spawn_skeletal_mesh_actor"))
			return LevelPort->SpawnSkeletalMeshActor(Normalized);
		if (Action == TEXT("list_actor_descs"))
			return LevelPort->ListActorDescs(Normalized);
		if (Action == TEXT("load_actor_descs"))
			return LevelPort->LoadActorDescs(Normalized);
		if (Action == TEXT("get_current_edit_level"))
			return LevelPort->GetCurrentEditLevel(Normalized);
		if (Action == TEXT("set_current_edit_level"))
			return LevelPort->SetCurrentEditLevel(Normalized);
		if (Action == TEXT("list_streaming_sublevels"))
			return LevelPort->ListStreamingSublevels(Normalized);
		if (Action == TEXT("add_streaming_sublevel"))
			return LevelPort->AddStreamingSublevel(Normalized);
		if (Action == TEXT("remove_streaming_sublevel"))
			return LevelPort->RemoveStreamingSublevel(Normalized);
		if (Action == TEXT("set_streaming_sublevel_properties"))
			return LevelPort->SetStreamingSublevelProperties(Normalized);
		if (Action == TEXT("spawn_grid"))
			return LevelPort->SpawnGrid(Normalized);
		if (Action == TEXT("batch_translate"))
			return LevelPort->BatchTranslate(Normalized);
		if (Action == TEXT("place_actors_batch"))
			return LevelPort->PlaceActorsBatch(Normalized);
		return UnsupportedAction(Action);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPLevelService::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const
	{
		return LevelPort->CreateTaskStepper(NormalizeArguments(Args));
	}
}
