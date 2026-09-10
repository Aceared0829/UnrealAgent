// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPExtractedToolSupport.h
 * @brief 扩展工具共享的字段读取、序列化与工程相对路径辅助。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UnrealAgentMCP::ExtractedToolSupport
{
	inline constexpr int32 MaximumReadableFileBytes = 1024 * 1024;
	inline constexpr int32 MaximumCompensatingFileBytes = 16 * 1024 * 1024;
	inline constexpr int32 MaximumPythonCommandCharacters = 64 * 1024;
	inline constexpr int32 MaximumSourceIndexFiles = 400;
	inline constexpr int32 MaximumResourceRows = 300;

	FString SerializeObject(const TSharedRef<FJsonObject>& Object);
	FString MakeProjectRelative(FString Path);

	double GetNumberField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, double DefaultValue);
	FString GetStringField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, const FString& DefaultValue = TEXT(""));
	bool GetBoolField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, bool DefaultValue = false);
	void AddStringArray(const TSharedRef<FJsonObject>& Object, const FString& FieldName, const TArray<FString>& Values);
}
