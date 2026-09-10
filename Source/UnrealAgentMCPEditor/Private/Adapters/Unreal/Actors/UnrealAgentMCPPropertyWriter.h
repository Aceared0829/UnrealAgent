// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPropertyWriter.h
 * @brief 将 MCP JSON 值安全写入可编辑 Unreal 反射属性。
 */

#include "CoreMinimal.h"

class FJsonValue;
class FProperty;
class UObject;

namespace UnrealAgentMCP::PropertyWriter
{
	/**
	 * 按属性实际类型写入值。
	 * 不可编辑、瞬态或不支持的属性会失败，并返回与现有 MCP API 兼容的错误文本。
	 */
	bool SetPropertyFromJson(UObject* Target, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutError);
}
