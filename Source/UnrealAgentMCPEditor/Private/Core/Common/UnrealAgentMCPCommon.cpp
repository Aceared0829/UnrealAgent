// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPCommon.cpp
 * @brief UnrealAgentMCP 公共 JSON 与项目名辅助函数实现。
 */
#include "Core/Common/UnrealAgentMCPCommon.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"

#include "Policies/CondensedJsonPrintPolicy.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAgentMCP
{
	/** 将 JSON 对象序列化为字符串（可选 pretty）。 */

	// =============================================================================
	// JSON 序列化与统一响应
	// =============================================================================

	FString JsonObjectToString(const TSharedRef<FJsonObject>& Json, bool bPretty)
	{
		FString Out;
		if (bPretty)
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Json, Writer);
		}
		else
		{
			// 紧凑格式减少 MCP 响应体积。
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Json, Writer);
		}
		return Out;
	}

	/** 构造统一失败响应：success=false + error。 */
	FString ErrorJson(const FString& Message)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Message);
		return JsonObjectToString(Result);
	}

	/** 在结果对象上标记 success=true 后序列化。 */
	FString SuccessJson(const TSharedRef<FJsonObject>& Result)
	{
		Result->SetBoolField(TEXT("success"), true);
		return JsonObjectToString(Result);
	}

	/** 从 .uproject 或工程目录解析项目名。 */

	// =============================================================================
	// 工程名解析
	// =============================================================================

	FString GetProjectName()
	{
		const FString ProjectFile = FPaths::GetProjectFilePath();
		if (!ProjectFile.IsEmpty())
		{
			return FPaths::GetBaseFilename(ProjectFile);
		}

		FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(ProjectDir);
		const FString Name = FPaths::GetCleanFilename(ProjectDir);
		// 无 .uproject 时退化为目录名。
		return Name.IsEmpty() ? TEXT("UnrealProject") : Name;
	}
}
