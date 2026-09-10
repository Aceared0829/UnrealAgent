// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPServerEnvironment.h
 * @brief MCP 服务使用的工程身份、文件路径、令牌与 JSON 文件基础能力。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UnrealAgentMCP::ServerEnvironment
{
	FString GenerateAccessToken();
	bool IsStrongAccessToken(const FString& Token);

	FString GetProjectFilePath();
	FString GetProjectDirectory();
	FString SanitizeNamePart(const FString& Name);
	FString GetProjectHashString();
	int32 GetDefaultPort();

	FString GetSavedConfigPath();
	FString GetConnectionPath();
	FString GetCursorClientConfigPath();
	FString GetCodexClientConfigPath();

	TSharedPtr<FJsonObject> LoadJsonObjectFile(const FString& Path);
	TSharedPtr<FJsonObject> ParseJsonObject(const FString& JsonText);
	bool WriteStringAtomically(const FString& Content, const FString& TargetPath, bool bRestrictToCurrentUser = false);
	bool WriteStringAtomically(const FString& Content, const FString& TargetPath, bool bRestrictToCurrentUser, FString& OutError);
	bool IsFileAccessRestrictedToCurrentUser(const FString& Path, FString& OutError);
	bool RestrictFileAccessToCurrentUser(const FString& Path, FString& OutError);

	TArray<TSharedPtr<FJsonValue>> MakeSupportedProtocolVersionsArray();
	TSharedRef<FJsonObject> MakeAuthHeadersObject(const FString& AccessTokenHeaderName, const FString& AccessToken);

	/** 读取并脱敏本机 Codex config.toml 中允许公开的策略字段。 */
	FString GetCodexPolicySnapshotJson();
}
