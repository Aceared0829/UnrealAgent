// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedEditorOperations.cpp
 * @brief 编辑器日志、受确认 Python 与 PIE 生命周期工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"
#include "Adapters/Unreal/Editor/UnrealAgentMCPPythonExecutionSafety.h"
#include "Application/Telemetry/UnrealAgentMCPPythonExecutionAudit.h"
#include "Core/Common/UnrealAgentMCPCommon.h"

namespace UnrealAgentMCP::ExtractedTools
{
	namespace
	{
		bool LoadLogTail(const FString& FilePath, const int64 MaximumBytes, FString& OutContent)
		{
			const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
			if (FileSize < 0)
			{
				return false;
			}
			if (FileSize <= MaximumBytes)
			{
				return FFileHelper::LoadFileToString(OutContent, *FilePath);
			}

			TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
			if (!Reader.IsValid())
			{
				return false;
			}
			const int64 BytesToRead = FMath::Min(MaximumBytes, FileSize);
			const int32 BufferSize = static_cast<int32>(BytesToRead);
			Reader->Seek(FileSize - BytesToRead);
			TArray<uint8> Buffer;
			Buffer.SetNumUninitialized(BufferSize);
			Reader->Serialize(Buffer.GetData(), BufferSize);
			if (Reader->IsError())
			{
				return false;
			}
			FFileHelper::BufferToString(OutContent, Buffer.GetData(), Buffer.Num());

			const int32 FirstCompleteLine = OutContent.Find(TEXT("\n"));
			if (FirstCompleteLine != INDEX_NONE)
			{
				OutContent.RightChopInline(FirstCompleteLine + 1);
			}
			return true;
		}

		bool ContainsBlockedBulkPython(const FString& Code, FString& OutPattern)
		{
			static const TArray<FString> BlockedPatterns = { TEXT("\nfor "), TEXT("\nwhile "), TEXT("add_instance"), TEXT("add_instances"), TEXT("spawn_actor"),
				TEXT("destroy_actor"), TEXT("duplicate_asset"), TEXT("delete_asset"), TEXT("import_asset_tasks"), TEXT("landscape_edit") };
			const FString Normalized = Code.ToLower();
			for (const FString& Pattern : BlockedPatterns)
			{
				if (Normalized.Contains(Pattern))
				{
					OutPattern = Pattern;
					return true;
				}
			}
			return false;
		}
	}

	FString ReadLog(const TSharedPtr<FJsonObject>& Args)
	{
		const int32 LineCount = FMath::Clamp(static_cast<int32>(ExtractedToolSupport::GetNumberField(Args, TEXT("lines"), 50)), 1, 1000);
		const FString Severity = ExtractedToolSupport::GetStringField(Args, TEXT("severity"));
		const FString Category = ExtractedToolSupport::GetStringField(Args, TEXT("category"));

		const FString LogDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
		TArray<FString> LogFiles;
		IFileManager::Get().FindFiles(LogFiles, *FPaths::Combine(LogDirectory, TEXT("*.log")), true, false);
		if (LogFiles.IsEmpty())
		{
			return ErrorJson(FString::Printf(TEXT("No log files found in %s"), *LogDirectory));
		}

		Algo::Sort(LogFiles,
			[](const FString& A, const FString& B)
			{
				return IFileManager::Get().GetTimeStamp(*A) > IFileManager::Get().GetTimeStamp(*B);
			});

		FString Content;
		constexpr int64 MaximumLogTailBytes = 2 * 1024 * 1024;
		if (!LoadLogTail(LogFiles[0], MaximumLogTailBytes, Content))
		{
			return ErrorJson(FString::Printf(TEXT("Failed to read log file: %s"), *LogFiles[0]));
		}

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines, false);
		TArray<TSharedPtr<FJsonValue>> OutputLines;
		for (int32 LineIndex = Lines.Num() - 1; LineIndex >= 0 && OutputLines.Num() < LineCount; --LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			if (!Category.IsEmpty() && !Line.Contains(Category, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!Severity.IsEmpty())
			{
				const bool bMatchesSeverity = Line.Contains(FString::Printf(TEXT("[%s]"), *Severity), ESearchCase::IgnoreCase) ||
					Line.Contains(FString::Printf(TEXT("%s:"), *Severity), ESearchCase::IgnoreCase) || Line.Contains(Severity, ESearchCase::IgnoreCase);
				if (!bMatchesSeverity)
				{
					continue;
				}
			}
			OutputLines.Insert(MakeShared<FJsonValueString>(Line), 0);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("logFile"), ExtractedToolSupport::MakeProjectRelative(LogFiles[0]));
		Result->SetNumberField(TEXT("count"), OutputLines.Num());
		Result->SetArrayField(TEXT("lines"), OutputLines);
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString RejectLegacyExecutePython(const TSharedPtr<FJsonObject>& Args)
	{
		(void)Args;
		return ErrorJson(TEXT(
			"execute_python is disabled because arbitrary Unreal Python monopolizes the GameThread. Use a typed resumable tool, or explicitly call execute_python_blocking for a short non-batch escape operation."));
	}

	FString ExecutePython(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Code = ExtractedToolSupport::GetStringField(Args, TEXT("code"));
		if (Code.IsEmpty())
		{
			return ErrorJson(TEXT("code is required."));
		}
		if (Code.Len() > ExtractedToolSupport::MaximumPythonCommandCharacters)
		{
			return ErrorJson(FString::Printf(TEXT("Python code exceeds the %d character limit."), ExtractedToolSupport::MaximumPythonCommandCharacters));
		}
		double ExpectedMaxMilliseconds = 0.0;
		if (!Args.IsValid() || !Args->TryGetNumberField(TEXT("expected_max_ms"), ExpectedMaxMilliseconds) || ExpectedMaxMilliseconds < 1.0 || ExpectedMaxMilliseconds > 250.0)
		{
			return ErrorJson(TEXT("expected_max_ms is required and must be between 1 and 250. Blocking Python has no preemptive timeout."));
		}
		FString BlockedPattern;
		if (ContainsBlockedBulkPython(Code, BlockedPattern))
		{
			return ErrorJson(FString::Printf(TEXT("Blocking Python rejected bulk-operation pattern '%s'. Use a typed resumable MCP tool."), *BlockedPattern));
		}

		const FString UnsafeConfirmation = ExtractedToolSupport::GetStringField(Args, TEXT("unsafe_confirm"));
		if (UnsafeConfirmation != TEXT("I understand this runs arbitrary Unreal Python"))
		{
			return ErrorJson(TEXT(
				"execute_python requires unsafe_confirm exactly equal to 'I understand this runs arbitrary Unreal Python'. Prefer structured Unreal Agent tools when possible."));
		}

		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Command.FileExecutionScope = EPythonFileExecutionScope::Private;
		Command.Command = Code;
		const PythonExecutionSafety::FExecutionResult Execution = PythonExecutionSafety::Execute(IPythonScriptPlugin::Get(), Command, &Code);
		if (!Execution.bDispatched)
		{
			return ErrorJson(Execution.Error);
		}

		const bool bSuccess = Execution.bSucceeded;
		FString TaskSummary;
		Args->TryGetStringField(TEXT("task_summary"), TaskSummary);
		if (TaskSummary.IsEmpty())
		{
			Args->TryGetStringField(TEXT("taskSummary"), TaskSummary);
		}
		FUnrealAgentMCPPythonExecutionAudit::Record(TaskSummary, Code, bSuccess);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetStringField(TEXT("message"), bSuccess ? TEXT("Python command executed.") : TEXT("Python command failed."));
		Result->SetBoolField(TEXT("blocking"), true);
		Result->SetStringField(TEXT("cancellation_mode"), TEXT("before_start_only"));
		Result->SetNumberField(TEXT("duration_ms"), Execution.DurationMs);
		Result->SetNumberField(TEXT("expected_max_ms"), ExpectedMaxMilliseconds);
		Result->SetBoolField(TEXT("budget_exceeded"), Execution.DurationMs > ExpectedMaxMilliseconds);
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString PlayInEditor(const TSharedPtr<FJsonObject>& Args)
	{
		if (GEditor == nullptr)
		{
			return ErrorJson(TEXT("No editor is available."));
		}
		if (GEditor->PlayWorld != nullptr)
		{
			return ErrorJson(TEXT("PIE is already running."));
		}

		FRequestPlaySessionParams Params;
		GEditor->RequestPlaySession(Params);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("Play in Editor started."));
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString StopPIE(const TSharedPtr<FJsonObject>& Args)
	{
		if (GEditor == nullptr)
		{
			return ErrorJson(TEXT("No editor is available."));
		}
		if (GEditor->PlayWorld == nullptr)
		{
			return ErrorJson(TEXT("PIE is not running."));
		}

		GEditor->RequestEndPlayMap();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("Play in Editor stopped."));
		return ExtractedToolSupport::SerializeObject(Result);
	}
}
