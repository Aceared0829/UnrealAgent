// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAssetService.cpp
 * @brief 资产应用服务实现；负责动作契约，不依赖 Unreal 编辑器实现。
 */

#include "Application/Domains/Asset/UnrealAgentMCPAssetService.h"

#include "Application/Ports/UnrealAgentMCPAssetPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPAssetService::FUnrealAgentMCPAssetService(TSharedRef<IUnrealAgentMCPAssetPort> InAssetPort) : AssetPort(MoveTemp(InAssetPort))
	{
	}

	TArray<FString> FUnrealAgentMCPAssetService::GetImplementedActions()
	{
		return { TEXT("list"), TEXT("search"), TEXT("read"), TEXT("read_properties"), TEXT("list_properties"), TEXT("get_properties"), TEXT("duplicate"), TEXT("rename"),
			TEXT("bulk_rename"), TEXT("move"), TEXT("delete"), TEXT("delete_batch"), TEXT("create_data_asset"), TEXT("create_asset_by_class"), TEXT("save"), TEXT("save_all_dirty"),
			TEXT("set_property"), TEXT("reload_package"), TEXT("health_check"), TEXT("force_reload"), TEXT("get_referencers"), TEXT("get_dependencies"),
			TEXT("get_primary_asset_ids"), TEXT("diagnose_registry"), TEXT("move_folder"), TEXT("create_folder"), TEXT("delete_folder"), TEXT("lock"), TEXT("unlock"),
			TEXT("list_locks"), TEXT("unlock_all"), TEXT("read_datatable"), TEXT("create_datatable"), TEXT("reimport_datatable"), TEXT("set_datatable_row"),
			TEXT("add_datatable_row"), TEXT("update_datatable_row"), TEXT("remove_datatable_row"), TEXT("get_datatable_row"), TEXT("set_datatable_cell"),
			TEXT("rename_datatable_row"), TEXT("fill_datatable_from_json"), TEXT("create_curvetable"), TEXT("read_curvetable"), TEXT("list_curvetable_rows"),
			TEXT("import_curvetable"), TEXT("add_curvetable_row"), TEXT("remove_curvetable_row"), TEXT("rename_curvetable_row"), TEXT("get_curvetable_keys"),
			TEXT("set_curvetable_keys"), TEXT("add_curvetable_key"), TEXT("create_stringtable"), TEXT("read_stringtable"), TEXT("list_stringtable_keys"),
			TEXT("get_stringtable_entry"), TEXT("set_stringtable_entry"), TEXT("remove_stringtable_entry"), TEXT("import_stringtable"), TEXT("list_textures"),
			TEXT("get_texture_info"), TEXT("set_texture_settings"), TEXT("set_texture_settings_by_type"), TEXT("set_mesh_material"), TEXT("add_socket"), TEXT("remove_socket"),
			TEXT("list_sockets"), TEXT("set_socket_transform"), TEXT("list_skeleton_bones"), TEXT("set_sk_material_slots"), TEXT("get_mesh_bounds"), TEXT("get_mesh_info"),
			TEXT("read_import_sources"), TEXT("get_mesh_collision"), TEXT("set_mesh_nav"), TEXT("add_input_mapping"), TEXT("remove_input_mapping"), TEXT("list_input_mappings"),
			TEXT("create_user_defined_enum"), TEXT("list_enum_values"), TEXT("edit_user_defined_enum"), TEXT("create_user_defined_struct"), TEXT("list_struct_fields"),
			TEXT("edit_user_defined_struct"), TEXT("rename_struct_field"), TEXT("recenter_pivot"), TEXT("import_static_mesh"), TEXT("import_skeletal_mesh"),
			TEXT("import_animation"), TEXT("import_texture"), TEXT("import_texture_batch"), TEXT("reimport"), TEXT("export_texture"), TEXT("export"), TEXT("compare_textures"),
			TEXT("read_cloth_data"), TEXT("set_cloth_config"), TEXT("create_interchange_pipeline"), TEXT("search_fts"), TEXT("reindex_fts"), TEXT("migrate"), TEXT("diff") };
	}

	FString FUnrealAgentMCPAssetService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		if (GetImplementedActions().Contains(Action))
		{
			return AssetPort->ExecuteAction(Action, Args);
		}

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("asset"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("资产 action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
