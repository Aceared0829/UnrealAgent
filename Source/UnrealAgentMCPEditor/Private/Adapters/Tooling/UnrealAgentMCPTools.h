#pragma once

/**
 * @file UnrealAgentMCPTools.h
 * @brief 编辑器内 MCP 工具：关卡 Actor、资产操作与 PCG 配方等。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	/** MCP 工具实现；由服务器调度至游戏线程，返回 JSON 字符串。 */
	namespace Tools
	{
		/** 列出当前关卡 Actor。 */
		FString ListLevelActors(const TSharedPtr<FJsonObject>& Args);
		/** 获取编辑器选中 Actor。 */
		FString GetSelectedActors(const TSharedPtr<FJsonObject>& Args);
		/** 查询单个 Actor 详情。 */
		FString GetActorDetails(const TSharedPtr<FJsonObject>& Args);
		/** 按类/路径搜索资产。 */
		FString FindAssets(const TSharedPtr<FJsonObject>& Args);
		/** 内容目录摘要。 */
		FString GetContentSummary(const TSharedPtr<FJsonObject>& Args);
		/** 读取资产元数据。 */
		FString ReadAsset(const TSharedPtr<FJsonObject>& Args);
		/** 选中指定 Actor。 */
		FString SelectActor(const TSharedPtr<FJsonObject>& Args);
		/** 在关卡中生成 Actor。 */
		FString SpawnActor(const TSharedPtr<FJsonObject>& Args);
		/** 变换 Actor Transform。 */
		FString TransformActor(const TSharedPtr<FJsonObject>& Args);
		/** 重命名 Actor，并在同一调用中读回验证。 */
		FString RenameActor(const TSharedPtr<FJsonObject>& Args);
		/** 删除 Actor。 */
		FString DeleteActor(const TSharedPtr<FJsonObject>& Args);
		/** 附加 Actor 到父级。 */
		FString AttachActor(const TSharedPtr<FJsonObject>& Args);
		/** 设置 Actor 属性。 */
		FString SetActorProperty(const TSharedPtr<FJsonObject>& Args);
		/** 保存当前关卡。 */
		FString SaveCurrentLevel(const TSharedPtr<FJsonObject>& Args);
		/** 创建普通资产。 */
		FString CreateAsset(const TSharedPtr<FJsonObject>& Args);
		/** 创建 Blueprint 资产。 */
		FString CreateBlueprintAsset(const TSharedPtr<FJsonObject>& Args);
		/** 修改材质实例参数。 */
		FString ModifyMaterialInstance(const TSharedPtr<FJsonObject>& Args);
		/** 按配方创建 PCG Graph。 */
		FString CreatePcgGraphFromRecipe(const TSharedPtr<FJsonObject>& Args);
		/** bootstrap 上下文 JSON。 */
		FString GetBootstrapContextJson();
	}
}
