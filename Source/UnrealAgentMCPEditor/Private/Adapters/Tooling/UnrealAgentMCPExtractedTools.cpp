// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedTools.cpp
 * @brief 扩展 MCP 工具的 Schema、注册表与统一分发入口。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"

namespace UnrealAgentMCP::ExtractedTools
{
	FString GetToolDefinitionsJson()
	{
		return TEXT(R"JSON([
{"name":"list_resources","description":"List Unreal Agent standalone resources using ubridge:// URIs.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"List Resources","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_resource","description":"Read an Unreal Agent standalone resource by URI. Recommended first read: ubridge://context/bootstrap.","inputSchema":{"type":"object","properties":{"uri":{"type":"string"}},"required":["uri"]},"annotations":{"title":"Read Resource","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_log","description":"Read recent Unreal log lines from the project Saved/Logs folder. Supports lines, severity, and category filters.","inputSchema":{"type":"object","properties":{"lines":{"type":"number","description":"Number of lines. Default 50."},"severity":{"type":"string","description":"Filter: Error, Warning, Log."},"category":{"type":"string"}}},"annotations":{"title":"Read Log","readOnlyHint":true,"openWorldHint":false}},
{"name":"execute_python","description":"Deprecated compatibility alias. It never executes code and only returns an error directing callers to typed tools or execute_python_blocking.","inputSchema":{"type":"object","properties":{},"additionalProperties":true},"annotations":{"title":"Deprecated Execute Python","readOnlyHint":true,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"execute_python_blocking","description":"Explicit last-resort GameThread Python escape hatch for short, non-batch operations. It cannot yield or be cancelled after start. Bulk actor, HISM, asset, or landscape mutation is rejected; use typed resumable tools.","inputSchema":{"type":"object","properties":{"code":{"type":"string","description":"Short Python code to execute."},"unsafe_confirm":{"type":"string","description":"Must equal: I understand this runs arbitrary Unreal Python"},"expected_max_ms":{"type":"number","minimum":1,"maximum":250,"description":"Caller-declared expected GameThread duration. This is measured and reported, not a preemptive timeout."},"task_summary":{"type":"string","description":"Non-sensitive audit summary."}},"required":["code","unsafe_confirm","expected_max_ms","task_summary"]},"annotations":{"title":"Execute Blocking Python","readOnlyHint":false,"destructiveHint":true,"openWorldHint":true}},
{"name":"search_assets","description":"Search project assets by name, path, and optional class filter.","inputSchema":{"type":"object","properties":{"query":{"type":"string"},"searchTerm":{"type":"string"},"classFilter":{"type":"string"},"path":{"type":"string"},"maxResults":{"type":"number"}}},"annotations":{"title":"Search Assets","readOnlyHint":true,"openWorldHint":false}},
{"name":"find_static_meshes","description":"Search StaticMesh assets for placement or PCG use.","inputSchema":{"type":"object","properties":{"query":{"type":"string"},"searchTerm":{"type":"string"},"path":{"type":"string"},"maxResults":{"type":"number"}}},"annotations":{"title":"Find Static Meshes","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_level_actors","description":"Unreal Agent alias for listing current editor level actors.","inputSchema":{"type":"object","properties":{"classFilter":{"type":"string"},"nameContains":{"type":"string"},"selectedOnly":{"type":"boolean"},"maxResults":{"type":"number"}}},"annotations":{"title":"Get Level Actors","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_project_info","description":"Get project name, engine version, paths, and active MCP endpoint.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Get Project Info","readOnlyHint":true,"openWorldHint":false}},
{"name":"list_project_modules","description":"List project module source folders and Build.cs files.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"List Project Modules","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_build_configuration","description":"Get build/platform/editor configuration for the current session.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Get Build Configuration","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_file","description":"Read a text file inside the project directory.","inputSchema":{"type":"object","properties":{"file_path":{"type":"string"}},"required":["file_path"]},"annotations":{"title":"Read File","readOnlyHint":true,"openWorldHint":false}},
{"name":"write_file","description":"Write a text file inside the project directory.","inputSchema":{"type":"object","properties":{"file_path":{"type":"string"},"content":{"type":"string"}},"required":["file_path","content"]},"annotations":{"title":"Write File","readOnlyHint":false,"destructiveHint":true,"openWorldHint":false}},
{"name":"delete_file","description":"Delete a file inside the project directory.","inputSchema":{"type":"object","properties":{"file_path":{"type":"string"}},"required":["file_path"]},"annotations":{"title":"Delete File","readOnlyHint":false,"destructiveHint":true,"openWorldHint":false}},
{"name":"rename_file","description":"Rename or move a file inside the project directory.","inputSchema":{"type":"object","properties":{"old_path":{"type":"string"},"new_path":{"type":"string"}},"required":["old_path","new_path"]},"annotations":{"title":"Rename File","readOnlyHint":false,"destructiveHint":true,"openWorldHint":false}},
{"name":"play_in_editor","description":"Start Play in Editor.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Play In Editor","readOnlyHint":false,"destructiveHint":false,"openWorldHint":false}},
{"name":"stop_pie","description":"Stop Play in Editor.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Stop PIE","readOnlyHint":false,"destructiveHint":false,"openWorldHint":false}},
{"name":"pcg_recipe_library_status","description":"Read the Unreal Agent PCG recipe library status from Saved/UnrealAgent/pcg_tool/recipe_library.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"PCG Recipe Library Status","readOnlyHint":true,"openWorldHint":false}},
{"name":"search_pcg_recipes","description":"Search normalized PCG recipe JSON files by query, tags, scene inputs, or output layers.","inputSchema":{"type":"object","properties":{"query":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}},"required_scene_inputs":{"type":"array","items":{"type":"string"}},"output_layers":{"type":"array","items":{"type":"string"}},"limit":{"type":"number"},"include_recipe":{"type":"boolean"}}},"annotations":{"title":"Search PCG Recipes","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_pcg_recipe","description":"Read a normalized PCG recipe by id, recipe_id, or file.","inputSchema":{"type":"object","properties":{"id":{"type":"string"},"recipe_id":{"type":"string"},"file":{"type":"string"},"include_scene_binding":{"type":"boolean"}}},"annotations":{"title":"Read PCG Recipe","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_pcg_scene_binding","description":"Read a PCG scene-binding contract by recipe_id, binding_id, or file.","inputSchema":{"type":"object","properties":{"recipe_id":{"type":"string"},"binding_id":{"type":"string"},"file":{"type":"string"}}},"annotations":{"title":"Read PCG Scene Binding","readOnlyHint":true,"openWorldHint":false}}
])JSON");
	}

	namespace
	{
		FString ListResourcesTool(const TSharedPtr<FJsonObject>&)
		{
			return ListResources();
		}

		// 工具名称与处理器只维护一份，避免 Schema、分发和测试清单相互漂移。
		const FMcpToolRegistration ExtractedToolRegistrations[] = { { TEXT("list_resources"), &ListResourcesTool }, { TEXT("read_resource"), &ReadResource },
			{ TEXT("read_log"), &ReadLog }, { TEXT("execute_python"), &RejectLegacyExecutePython }, { TEXT("execute_python_blocking"), &ExecutePython },
			{ TEXT("search_assets"), &SearchAssets }, { TEXT("find_static_meshes"), &FindStaticMeshes }, { TEXT("get_level_actors"), &GetLevelActors },
			{ TEXT("get_project_info"), &GetProjectInfo }, { TEXT("list_project_modules"), &ListProjectModules }, { TEXT("get_build_configuration"), &GetBuildConfiguration },
			{ TEXT("read_file"), &ReadFile }, { TEXT("write_file"), &WriteFile }, { TEXT("delete_file"), &DeleteFile }, { TEXT("rename_file"), &RenameFile },
			{ TEXT("play_in_editor"), &PlayInEditor }, { TEXT("stop_pie"), &StopPIE }, { TEXT("pcg_recipe_library_status"), &PcgRecipeLibraryStatus },
			{ TEXT("search_pcg_recipes"), &SearchPcgRecipes }, { TEXT("read_pcg_recipe"), &ReadPcgRecipe }, { TEXT("read_pcg_scene_binding"), &ReadPcgSceneBinding } };

		TConstArrayView<FMcpToolRegistration> GetExtractedToolRegistrations()
		{
			return TConstArrayView<FMcpToolRegistration>(ExtractedToolRegistrations, UE_ARRAY_COUNT(ExtractedToolRegistrations));
		}
	}

	TArray<FString> GetRegisteredToolNames()
	{
		return ToolRegistry::GetRegisteredToolNames(GetExtractedToolRegistrations());
	}

	bool RegisterTools(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors)
	{
		return Registry.RegisterCatalog(TEXT("Adapters.ExtractedTools"), GetToolDefinitionsJson(), GetExtractedToolRegistrations(), OutErrors);
	}

	bool Dispatch(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FString& OutResult)
	{
		return ToolRegistry::TryDispatch(GetExtractedToolRegistrations(), ToolName, Args, OutResult);
	}
}
