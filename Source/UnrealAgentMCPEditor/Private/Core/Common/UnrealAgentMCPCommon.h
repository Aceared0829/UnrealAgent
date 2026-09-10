#pragma once

/**
 * @file UnrealAgentMCPCommon.h
 * @brief MCP 服务器与工具共用的 JSON 序列化及项目标识辅助函数。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	/** 将 JsonObject 序列化为字符串；bPretty 控制缩进。 */
	FString JsonObjectToString(const TSharedRef<FJsonObject>& Json, bool bPretty = false);
	/** 构造带 success=false 的标准错误 JSON。 */
	FString ErrorJson(const FString& Message);
	/** 构造带 success=true 的标准成功 JSON。 */
	FString SuccessJson(const TSharedRef<FJsonObject>& Result);
	/** 当前 UE 项目名。 */
	FString GetProjectName();
}
