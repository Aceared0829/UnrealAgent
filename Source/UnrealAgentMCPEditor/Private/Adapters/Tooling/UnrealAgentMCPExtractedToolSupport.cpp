// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedToolSupport.cpp
 * @brief 扩展工具无业务含义的共享辅助实现。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

#include "Core/Common/UnrealAgentMCPCommon.h"

namespace UnrealAgentMCP::ExtractedToolSupport
{
	FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		return JsonObjectToString(Object);
	}

	FString MakeProjectRelative(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		if (FPaths::MakePathRelativeTo(Path, *ProjectDirectory))
		{
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			return Path;
		}
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path;
	}

	double GetNumberField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, const double DefaultValue)
	{
		double Value = DefaultValue;
		if (Arguments.IsValid())
		{
			Arguments->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, const FString& DefaultValue)
	{
		FString Value = DefaultValue;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool GetBoolField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		if (Arguments.IsValid())
		{
			Arguments->TryGetBoolField(FieldName, Value);
		}
		return Value;
	}

	void AddStringArray(const TSharedRef<FJsonObject>& Object, const FString& FieldName, const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		JsonValues.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueString>(Value));
		}
		Object->SetArrayField(FieldName, JsonValues);
	}
}
