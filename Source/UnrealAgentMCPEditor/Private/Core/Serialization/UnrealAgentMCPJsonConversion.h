// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPJsonConversion.h
 * @brief MCP 工具共享的 JSON 数值、向量、旋转和颜色转换规则。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UnrealAgentMCP::JsonConversion
{
	/** 将包路径补全为 Package.Asset 对象路径；已经完整的路径保持不变。 */
	FString NormalizeAssetObjectPath(FString Path);

	TSharedPtr<FJsonObject> MakeVectorObject(const FVector& Vector);
	TSharedPtr<FJsonObject> MakeRotatorObject(const FRotator& Rotator);

	bool TryGetNumberFieldCaseInsensitive(const TSharedPtr<FJsonObject>& Json, const TCHAR* LowerName, const TCHAR* UpperName, double& OutValue);
	bool TryGetVectorField(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FVector& OutVector);
	bool TryGetRotatorField(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FRotator& OutRotator);

	bool TryValueToNumber(const TSharedPtr<FJsonValue>& Value, double& OutNumber);
	bool TryValueToBool(const TSharedPtr<FJsonValue>& Value, bool& bOutValue);
	bool TryValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector);
	bool TryValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator);
	bool TryValueToLinearColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor);
}
