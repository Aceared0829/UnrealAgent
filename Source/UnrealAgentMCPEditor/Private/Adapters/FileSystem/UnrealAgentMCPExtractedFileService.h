// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPExtractedFileService.h
 * @brief 扩展工具的 JSON 解析与受限路径解析服务。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::ExtractedFileService
{
	TSharedPtr<FJsonObject> ParseJsonObject(const FString& JsonText);
	TSharedPtr<FJsonObject> LoadJsonObjectFile(const FString& Path);

	FString NormalizeDirectoryForContainment(FString Directory);
	FString NormalizeFileForContainment(FString File);
	bool IsPathInsideDirectory(const FString& File, const FString& Directory);

	/** 将相对/绝对路径解析到指定根目录内；不访问文件系统。 */
	bool ResolvePathWithinRoot(const FString& InputPath, const FString& DefaultRoot, const FString& AllowedRoot, FString& OutPath, FString& OutError);

	bool ResolveProjectFilePath(const FString& InputPath, FString& OutPath, FString& OutError);

	/** 校验 PCG JSON 文件的 containment、扩展名、存在性与大小。 */
	bool ValidateReadableJsonFileWithinRoot(const FString& CandidatePath, const FString& AllowedRoot, FString& OutPath, FString& OutError);

	bool ResolvePcgJsonFilePath(const FString& FileArgument, const FString& FallbackRoot, const FString& AllowedRoot, FString& OutPath, FString& OutError);
}
