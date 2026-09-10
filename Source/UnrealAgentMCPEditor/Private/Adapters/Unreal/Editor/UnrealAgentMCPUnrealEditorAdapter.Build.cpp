// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Build.cpp
 * @brief 地图构建、资产验证、内容烹饪与自动化测试调度。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorBuildUtils.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool IsSafeCommandArgument(const FString& Value)
		{
			return !Value.Contains(TEXT("\r")) && !Value.Contains(TEXT("\n")) && !Value.Contains(TEXT(";")) && !Value.Contains(TEXT("&")) && !Value.Contains(TEXT("|"));
		}

		UWorld* ResolveBuildWorld()
		{
			return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		}

		FString BuildResultJson(const FString& Operation, const bool bSucceeded)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("operation"), Operation);
			Result->SetBoolField(TEXT("completed"), true);
			Result->SetBoolField(TEXT("succeeded"), bSucceeded);
			return bSucceeded ? SuccessJson(Result) : ErrorJson(FString::Printf(TEXT("%s 未完成或被取消。"), *Operation));
		}
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Build(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ResolveBuildWorld();
		if (!World)
		{
			return ErrorJson(TEXT("没有可用于构建的编辑器世界。"));
		}

		if (Action == TEXT("build_all"))
		{
			return BuildResultJson(TEXT("BuildAll"), FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildAll, false));
		}

		if (Action == TEXT("build_geometry"))
		{
			return BuildResultJson(TEXT("BuildGeometry"), FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildGeometry, false));
		}

		if (Action == TEXT("build_hlod"))
		{
			return BuildResultJson(TEXT("BuildHierarchicalLOD"), FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildHierarchicalLOD, false));
		}

		if (Action == TEXT("validate_assets"))
		{
			FString Directory = GetString(Args, { TEXT("directory"), TEXT("path") });
			if (Directory.IsEmpty())
			{
				Directory = TEXT("/Game");
			}
			if (!Directory.StartsWith(TEXT("/Game")) || !IsSafeCommandArgument(Directory))
			{
				return ErrorJson(TEXT("资产验证目录必须位于 /Game，且不能包含命令分隔符。"));
			}
			const FString Command = FString::Printf(TEXT("DataValidation.ValidateAssets %s"), *Directory);
			const bool bExecuted = GEditor->Exec(World, *Command);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("directory"), Directory);
			Result->SetStringField(TEXT("command"), Command);
			Result->SetBoolField(TEXT("queued"), bExecuted);
			return bExecuted ? SuccessJson(Result) : ErrorJson(TEXT("资产验证命令未被编辑器接受。"));
		}

		if (Action == TEXT("cook_content"))
		{
			FString Platform = GetString(Args, { TEXT("platform"), TEXT("targetPlatform") });
			if (Platform.IsEmpty())
			{
				Platform = TEXT("Windows");
			}
			if (!IsSafeCommandArgument(Platform))
			{
				return ErrorJson(TEXT("无效的 Cook 目标平台。"));
			}
			for (const TCHAR Character : Platform)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
				{
					return ErrorJson(TEXT("无效的 Cook 目标平台。"));
				}
			}

			const FString EditorCommandlet = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("UnrealEditor-Cmd.exe"));
			const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
			if (!FPaths::FileExists(EditorCommandlet) || !FPaths::FileExists(ProjectFile))
			{
				return ErrorJson(TEXT("未找到 UnrealEditor-Cmd 或当前项目文件。"));
			}
			const FString LogDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"));
			IFileManager::Get().MakeDirectory(*LogDirectory, true);
			const FString CookLogPath =
				FPaths::Combine(LogDirectory, FString::Printf(TEXT("UnrealAgent-Cook-%s-%s.log"), *Platform, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
			const FString Parameters = FString::Printf(TEXT("\"%s\" -run=Cook -TargetPlatform=%s ") TEXT("-unattended -nop4 -stdout -FullStdOutLogOutput ") TEXT("-abslog=\"%s\""),
				*ProjectFile, *Platform, *CookLogPath);
			uint32 ProcessId = 0;
			FProcHandle Process = FPlatformProcess::CreateProc(*EditorCommandlet, *Parameters, true, true, true, &ProcessId, 0, *FPaths::ProjectDir(), nullptr);
			if (!Process.IsValid())
			{
				return ErrorJson(TEXT("无法启动独立 Cook 进程。"));
			}
			FPlatformProcess::CloseProc(Process);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("platform"), Platform);
			Result->SetNumberField(TEXT("processId"), ProcessId);
			Result->SetStringField(TEXT("logPath"), CookLogPath);
			Result->SetBoolField(TEXT("launched"), true);
			Result->SetBoolField(TEXT("asynchronous"), true);
			return SuccessJson(Result);
		}

		if (Action == TEXT("run_automation_tests"))
		{
			FString Filter = GetString(Args, { TEXT("filter"), TEXT("test"), TEXT("testName") });
			if (Filter.IsEmpty())
			{
				Filter = TEXT("WorldData.UnrealAgent");
			}
			if (!IsSafeCommandArgument(Filter))
			{
				return ErrorJson(TEXT("自动化测试过滤器不能包含命令分隔符。"));
			}
			const FString Command = TEXT("Automation RunTests ") + Filter;
			const bool bQueued = GEditor->Exec(World, *Command);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("filter"), Filter);
			Result->SetStringField(TEXT("command"), Command);
			Result->SetBoolField(TEXT("queued"), bQueued);
			return bQueued ? SuccessJson(Result) : ErrorJson(TEXT("自动化测试请求未被编辑器接受。"));
		}

		return ErrorJson(TEXT("编辑器构建动作没有有效实现分支。"));
	}
}
