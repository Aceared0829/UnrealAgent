// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedFileService.cpp
 * @brief JSON 解析、工程路径 containment 与安全读取前置校验。
 */

#include "Adapters/FileSystem/UnrealAgentMCPExtractedFileService.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"

namespace UnrealAgentMCP::ExtractedFileService
{
	TSharedPtr<FJsonObject> ParseJsonObject(const FString& JsonText)
	{
		TSharedPtr<FJsonObject> ParsedObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, ParsedObject) || !ParsedObject.IsValid())
		{
			return nullptr;
		}
		return ParsedObject;
	}

	TSharedPtr<FJsonObject> LoadJsonObjectFile(const FString& Path)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			return nullptr;
		}
		return ParseJsonObject(Content);
	}

	FString NormalizeDirectoryForContainment(FString Directory)
	{
		Directory = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::NormalizeDirectoryName(Directory);
		return Directory;
	}

	FString NormalizeFileForContainment(FString File)
	{
		File = FPaths::ConvertRelativePathToFull(File);
		FPaths::NormalizeFilename(File);
		return File;
	}

	bool IsPathInsideDirectory(const FString& File, const FString& Directory)
	{
		const FString NormalizedFile = NormalizeFileForContainment(File);
		const FString NormalizedDirectory = NormalizeDirectoryForContainment(Directory);
		return NormalizedFile.StartsWith(NormalizedDirectory + TEXT("/"), ESearchCase::IgnoreCase);
	}

	bool ResolvePathWithinRoot(const FString& InputPath, const FString& DefaultRoot, const FString& AllowedRoot, FString& OutPath, FString& OutError)
	{
		if (InputPath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("file_path is required.");
			return false;
		}

		const FString CandidatePath = FPaths::IsRelative(InputPath) ? FPaths::Combine(DefaultRoot, InputPath) : InputPath;
		const FString NormalizedCandidate = NormalizeFileForContainment(CandidatePath);
		const FString NormalizedAllowedRoot = NormalizeDirectoryForContainment(AllowedRoot);
		const bool bIsRootItself = NormalizedCandidate.Equals(NormalizedAllowedRoot, ESearchCase::IgnoreCase);
		if (!bIsRootItself && !IsPathInsideDirectory(NormalizedCandidate, NormalizedAllowedRoot))
		{
			OutError = FString::Printf(TEXT("Path is outside the project directory: %s"), *InputPath);
			return false;
		}

		OutPath = NormalizedCandidate;
		return true;
	}

	bool ResolveProjectFilePath(const FString& InputPath, FString& OutPath, FString& OutError)
	{
		const FString ProjectDirectory = NormalizeDirectoryForContainment(FPaths::ProjectDir());
		return ResolvePathWithinRoot(InputPath, ProjectDirectory, ProjectDirectory, OutPath, OutError);
	}

	bool ValidateReadableJsonFileWithinRoot(const FString& CandidatePath, const FString& AllowedRoot, FString& OutPath, FString& OutError)
	{
		if (CandidatePath.IsEmpty())
		{
			OutError = TEXT("File path is empty.");
			return false;
		}

		const FString NormalizedFile = NormalizeFileForContainment(CandidatePath);
		if (!IsPathInsideDirectory(NormalizedFile, AllowedRoot))
		{
			OutError = FString::Printf(TEXT("Path is outside the allowed PCG recipe root: %s"), *ExtractedToolSupport::MakeProjectRelative(NormalizedFile));
			return false;
		}
		if (!FPaths::GetExtension(NormalizedFile, false).Equals(TEXT("json"), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("Only .json PCG recipe files can be read.");
			return false;
		}
		if (!IFileManager::Get().FileExists(*NormalizedFile))
		{
			OutError = FString::Printf(TEXT("File does not exist: %s"), *ExtractedToolSupport::MakeProjectRelative(NormalizedFile));
			return false;
		}

		const int64 FileSize = IFileManager::Get().FileSize(*NormalizedFile);
		if (FileSize < 0)
		{
			OutError = FString::Printf(TEXT("Unable to stat file: %s"), *ExtractedToolSupport::MakeProjectRelative(NormalizedFile));
			return false;
		}
		if (FileSize > ExtractedToolSupport::MaximumReadableFileBytes)
		{
			OutError = FString::Printf(TEXT("File is larger than the %d byte read limit."), ExtractedToolSupport::MaximumReadableFileBytes);
			return false;
		}

		OutPath = NormalizedFile;
		return true;
	}

	bool ResolvePcgJsonFilePath(const FString& FileArgument, const FString& FallbackRoot, const FString& AllowedRoot, FString& OutPath, FString& OutError)
	{
		if (FileArgument.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("file is required.");
			return false;
		}

		FString CandidatePath;
		FString IgnoredProjectPathError;
		if (!ResolveProjectFilePath(FileArgument, CandidatePath, IgnoredProjectPathError))
		{
			CandidatePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FallbackRoot, FileArgument));
		}

		return ValidateReadableJsonFileWithinRoot(CandidatePath, AllowedRoot, OutPath, OutError);
	}
}
