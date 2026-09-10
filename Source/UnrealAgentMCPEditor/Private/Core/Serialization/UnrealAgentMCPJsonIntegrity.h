// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPJsonIntegrity.h
 * @brief JSON 规范化与内容完整性校验接口。
 */

#include "CoreMinimal.h"

class FJsonValue;

namespace UnrealAgentMCP::JsonIntegrity
{
	/**
	 * 按字段名稳定排序并计算 UTF-8 JSON 的 SHA-256。
	 * 返回值包含 `sha256:` 前缀，便于直接与 Manifest 契约比较。
	 */
	bool TryComputeCanonicalSha256(const TSharedPtr<FJsonValue>& Value, FString& OutHash, FString& OutError);
}
