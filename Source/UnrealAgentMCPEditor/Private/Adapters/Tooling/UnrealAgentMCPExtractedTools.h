// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPExtractedTools.h
 * @brief 扩展 MCP 工具集：文件 I/O、Python、PIE 与 PCG 配方查询等（自研迁移实现）。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FMcpToolRuntimeRegistry;

	/** 扩展工具实现；Dispatch 按 ToolName 路由，失败时由调用方回退到核心 Tools。 */
	namespace ExtractedTools
	{
		bool RegisterTools(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors);
		/** 本命名空间工具的 JSON Schema 定义。 */
		FString GetToolDefinitionsJson();
		/** 从实际调度注册表导出扩展工具名称。 */
		TArray<FString> GetRegisteredToolNames();
		/**
	 * 尝试分发工具。
	 * @return true 表示已处理（无论成败），OutResult 为 JSON 响应。
	 */
		bool Dispatch(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FString& OutResult);

		FString ListResources();
		FString ReadResource(const TSharedPtr<FJsonObject>& Args);
		FString ReadLog(const TSharedPtr<FJsonObject>& Args);
		FString RejectLegacyExecutePython(const TSharedPtr<FJsonObject>& Args);
		FString ExecutePython(const TSharedPtr<FJsonObject>& Args);

		FString SearchAssets(const TSharedPtr<FJsonObject>& Args);
		FString FindStaticMeshes(const TSharedPtr<FJsonObject>& Args);
		FString GetLevelActors(const TSharedPtr<FJsonObject>& Args);
		FString GetProjectInfo(const TSharedPtr<FJsonObject>& Args);
		FString ListProjectModules(const TSharedPtr<FJsonObject>& Args);
		FString GetBuildConfiguration(const TSharedPtr<FJsonObject>& Args);

		FString ReadFile(const TSharedPtr<FJsonObject>& Args);
		FString WriteFile(const TSharedPtr<FJsonObject>& Args);
		FString DeleteFile(const TSharedPtr<FJsonObject>& Args);
		FString RenameFile(const TSharedPtr<FJsonObject>& Args);

		FString PlayInEditor(const TSharedPtr<FJsonObject>& Args);
		FString StopPIE(const TSharedPtr<FJsonObject>& Args);

		FString PcgRecipeLibraryStatus(const TSharedPtr<FJsonObject>& Args);
		FString SearchPcgRecipes(const TSharedPtr<FJsonObject>& Args);
		FString ReadPcgRecipe(const TSharedPtr<FJsonObject>& Args);
		FString ReadPcgSceneBinding(const TSharedPtr<FJsonObject>& Args);
	}
}
