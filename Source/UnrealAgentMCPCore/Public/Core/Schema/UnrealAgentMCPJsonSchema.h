// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPJsonSchema.h
 * @brief 统一工具契约使用的轻量 JSON Schema 校验器。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UnrealAgentMCP::JsonSchema
{
	struct UNREALAGENTMCPCORE_API FValidationError
	{
		FString Path;
		FString Message;
	};

	struct UNREALAGENTMCPCORE_API FValidationResult
	{
		TArray<FValidationError> Errors;

		bool IsValid() const
		{
			return Errors.IsEmpty();
		}
	};

	/** 校验任意 JSON 值，递归深度与错误数量均有硬上限。 */
	UNREALAGENTMCPCORE_API FValidationResult ValidateValue(const TSharedRef<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value);

	/** 校验工具 arguments 对象。 */
	UNREALAGENTMCPCORE_API FValidationResult ValidateObject(const TSharedRef<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Value);

	/** 将校验失败转换为统一的 success=false JSON 文本。 */
	UNREALAGENTMCPCORE_API FString MakeValidationErrorJson(const FString& ToolName, const FValidationResult& Result);

	/** 将工具成功载荷违反 outputSchema 转换为独立的契约错误。 */
	UNREALAGENTMCPCORE_API FString MakeOutputValidationErrorJson(const FString& ToolName, const FValidationResult& Result);
}
