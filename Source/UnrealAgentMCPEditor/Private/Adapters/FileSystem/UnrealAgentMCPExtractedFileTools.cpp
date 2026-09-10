// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedFileTools.cpp
 * @brief 受工程根目录 containment 约束的文件读写、删除与移动工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Adapters/FileSystem/UnrealAgentMCPExtractedFileService.h"
#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Infrastructure/Transactions/UnrealAgentMCPFileMutationCompensation.h"

namespace UnrealAgentMCP::ExtractedTools
{
	FString ReadFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString FullPath;
		FString Error;
		if (!ExtractedFileService::ResolveProjectFilePath(ExtractedToolSupport::GetStringField(Args, TEXT("file_path")), FullPath, Error))
		{
			return ErrorJson(Error);
		}
		if (!IFileManager::Get().FileExists(*FullPath))
		{
			return ErrorJson(FString::Printf(TEXT("File does not exist: %s"), *FullPath));
		}
		if (IFileManager::Get().FileSize(*FullPath) > ExtractedToolSupport::MaximumReadableFileBytes)
		{
			return ErrorJson(FString::Printf(TEXT("File is larger than the %d byte read limit."), ExtractedToolSupport::MaximumReadableFileBytes));
		}

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FullPath))
		{
			return ErrorJson(FString::Printf(TEXT("Failed to read file: %s"), *FullPath));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("file"), ExtractedToolSupport::MakeProjectRelative(FullPath));
		Result->SetStringField(TEXT("content"), Content);
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString WriteFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString FullPath;
		FString Error;
		if (!ExtractedFileService::ResolveProjectFilePath(ExtractedToolSupport::GetStringField(Args, TEXT("file_path")), FullPath, Error))
		{
			return ErrorJson(Error);
		}
		if (!Transactions::RegisterFileWriteCompensation(FullPath, ExtractedToolSupport::MaximumCompensatingFileBytes, Error))
		{
			return ErrorJson(Error);
		}

		const FString Content = ExtractedToolSupport::GetStringField(Args, TEXT("content"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
		const FString StagedPath = FullPath + TEXT(".uebridge-stage-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		if (!FFileHelper::SaveStringToFile(Content, *StagedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().Delete(*StagedPath, false, true, true);
			return ErrorJson(FString::Printf(TEXT("Failed to write file: %s"), *FullPath));
		}
		if (!IFileManager::Get().Move(*FullPath, *StagedPath, true, true))
		{
			IFileManager::Get().Delete(*StagedPath, false, true, true);
			return ErrorJson(FString::Printf(TEXT("Failed to atomically replace file: %s"), *FullPath));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("file"), ExtractedToolSupport::MakeProjectRelative(FullPath));
		Result->SetNumberField(TEXT("bytes"), Content.Len());
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString DeleteFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString FullPath;
		FString Error;
		if (!ExtractedFileService::ResolveProjectFilePath(ExtractedToolSupport::GetStringField(Args, TEXT("file_path")), FullPath, Error))
		{
			return ErrorJson(Error);
		}
		if (!IFileManager::Get().FileExists(*FullPath))
		{
			return ErrorJson(FString::Printf(TEXT("File does not exist: %s"), *FullPath));
		}
		if (!IFileManager::Get().Delete(*FullPath, false, true, true))
		{
			return ErrorJson(FString::Printf(TEXT("Failed to delete file: %s"), *FullPath));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("file"), ExtractedToolSupport::MakeProjectRelative(FullPath));
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString RenameFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString OldPath;
		FString NewPath;
		FString Error;
		if (!ExtractedFileService::ResolveProjectFilePath(ExtractedToolSupport::GetStringField(Args, TEXT("old_path")), OldPath, Error))
		{
			return ErrorJson(Error);
		}
		if (!ExtractedFileService::ResolveProjectFilePath(ExtractedToolSupport::GetStringField(Args, TEXT("new_path")), NewPath, Error))
		{
			return ErrorJson(Error);
		}
		if (!IFileManager::Get().FileExists(*OldPath))
		{
			return ErrorJson(FString::Printf(TEXT("Source file does not exist: %s"), *OldPath));
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(NewPath), true);
		if (!IFileManager::Get().Move(*NewPath, *OldPath, true, true))
		{
			return ErrorJson(FString::Printf(TEXT("Failed to move file: %s -> %s"), *OldPath, *NewPath));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("oldFile"), ExtractedToolSupport::MakeProjectRelative(OldPath));
		Result->SetStringField(TEXT("newFile"), ExtractedToolSupport::MakeProjectRelative(NewPath));
		return ExtractedToolSupport::SerializeObject(Result);
	}
}
