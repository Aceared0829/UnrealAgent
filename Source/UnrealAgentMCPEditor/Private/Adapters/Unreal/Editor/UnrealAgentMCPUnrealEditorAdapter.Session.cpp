// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Session.cpp
 * @brief 编辑器会话、Python、事务、资产窗口与脏包操作。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Adapters/Unreal/Editor/UnrealAgentMCPPythonExecutionSafety.h"
#include "Application/Telemetry/UnrealAgentMCPPythonExecutionAudit.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "ISettingsModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool ResolveProjectPythonFile(const FString& Input, FString& OutPath, FString& OutError)
		{
			const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			OutPath = FPaths::IsRelative(Input) ? FPaths::Combine(ProjectRoot, Input) : Input;
			OutPath = FPaths::ConvertRelativePathToFull(OutPath);
			FPaths::NormalizeFilename(OutPath);
			FString Relative = OutPath;
			if (!FPaths::MakePathRelativeTo(Relative, *(ProjectRoot + TEXT("/"))) || Relative.StartsWith(TEXT("..")))
			{
				OutError = TEXT("Python 文件必须位于当前项目目录内。");
				return false;
			}
			if (!OutPath.EndsWith(TEXT(".py"), ESearchCase::IgnoreCase) || !IFileManager::Get().FileExists(*OutPath))
			{
				OutError = TEXT("未找到有效的项目内 Python 文件。");
				return false;
			}
			return true;
		}

		TSharedRef<FJsonObject> PythonResult(const FPythonCommandEx& Command, const bool bSuccess)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("executed"), bSuccess);
			Result->SetStringField(TEXT("result"), Command.CommandResult);
			TArray<TSharedPtr<FJsonValue>> Output;
			for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("type"), LexToString(Entry.Type));
				Item->SetStringField(TEXT("output"), Entry.Output);
				Output.Add(MakeShared<FJsonValueObject>(Item));
			}
			Result->SetArrayField(TEXT("logOutput"), Output);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Session(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (!GEditor)
		{
			return ErrorJson(TEXT("Unreal Editor 当前不可用。"));
		}

		if (Action == TEXT("execute_command"))
		{
			const FString Command = GetString(Args, { TEXT("command"), TEXT("consoleCommand") });
			if (Command.IsEmpty())
			{
				return ErrorJson(TEXT("缺少 command。"));
			}
			UWorld* World = GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World();
			const bool bExecuted = GEditor->Exec(World, *Command);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("command"), Command);
			Result->SetBoolField(TEXT("executed"), bExecuted);
			return SuccessJson(Result);
		}

		if (Action == TEXT("execute_python"))
		{
			return ErrorJson(TEXT(
				"editor.execute_python is disabled because arbitrary Unreal Python monopolizes the GameThread. Use a typed resumable action, or explicitly call the top-level execute_python_blocking escape hatch for short non-batch work."));
		}

		if (Action == TEXT("run_python_file") || Action == TEXT("purge_python_modules"))
		{
			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			if (!Python || !Python->IsPythonAvailable())
			{
				return ErrorJson(TEXT("PythonScriptPlugin 当前不可用。"));
			}

			FPythonCommandEx Command;
			Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
			Command.FileExecutionScope = EPythonFileExecutionScope::Public;
			FString AuditCode;
			FString SourceToValidate;
			if (Action == TEXT("run_python_file"))
			{
				FString Error;
				if (!ResolveProjectPythonFile(GetString(Args, { TEXT("path"), TEXT("filePath") }), Command.Command, Error))
				{
					return ErrorJson(Error);
				}
				AuditCode = FString::Printf(TEXT("执行项目脚本：%s"), *Command.Command);
				if (!FFileHelper::LoadFileToString(SourceToValidate, *Command.Command))
				{
					return ErrorJson(TEXT("无法读取项目内 Python 文件。"));
				}
			}
			else
			{
				const FString Prefix = GetString(Args, { TEXT("prefix"), TEXT("modulePrefix") });
				const FString EffectivePrefix = Prefix.IsEmpty() ? TEXT("ue_mcp") : Prefix;
				Command.Command = FString::Printf(TEXT("import sys\n") TEXT("_prefix=%s\n") TEXT("_removed=[n for n in list(sys.modules) ")
													  TEXT("if n == _prefix or n.startswith(_prefix + '.')]\n") TEXT("[sys.modules.pop(n, None) for n in _removed]\n")
														  TEXT("print(len(_removed))"),
					*FString::Printf(TEXT("r'%s'"), *EffectivePrefix.Replace(TEXT("'"), TEXT("\\'"))));
				AuditCode = FString::Printf(TEXT("清理 Python 模块前缀：%s"), *EffectivePrefix);
			}

			const FString* ValidationSource = Action == TEXT("purge_python_modules") ? nullptr : &SourceToValidate;
			const PythonExecutionSafety::FExecutionResult Execution = PythonExecutionSafety::Execute(Python, Command, ValidationSource);
			if (!Execution.bDispatched)
			{
				return ErrorJson(Execution.Error);
			}
			const bool bSuccess = Execution.bSucceeded;
			FString TaskSummary = GetString(Args, { TEXT("taskSummary") });
			if (TaskSummary.IsEmpty())
			{
				TaskSummary = Action;
			}
			FUnrealAgentMCPPythonExecutionAudit::Record(TaskSummary, AuditCode, bSuccess);
			return bSuccess ? SuccessJson(PythonResult(Command, true)) : ErrorJson(Command.CommandResult.IsEmpty() ? TEXT("Python 执行失败。") : Command.CommandResult);
		}

		if (Action == TEXT("close_sequence"))
		{
			UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!Subsystem)
			{
				return ErrorJson(TEXT("AssetEditorSubsystem 不可用。"));
			}
			const FString AssetPath = GetString(Args, { TEXT("assetPath"), TEXT("sequencePath"), TEXT("path") });
			int32 ClosedCount = 0;
			if (AssetPath.IsEmpty())
			{
				Subsystem->CloseAllAssetEditors();
				ClosedCount = 1;
			}
			else if (UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath))
			{
				ClosedCount = Subsystem->CloseAllEditorsForAsset(Asset) ? 1 : 0;
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("closedCount"), ClosedCount);
			return SuccessJson(Result);
		}

		if (Action == TEXT("open_tab"))
		{
			const FString TabId = GetString(Args, { TEXT("tabId"), TEXT("name") });
			if (TabId.IsEmpty())
			{
				return ErrorJson(TEXT("缺少 tabId。"));
			}
			const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(*TabId)));
			if (!Tab.IsValid())
			{
				return ErrorJson(FString::Printf(TEXT("无法打开编辑器标签页：%s"), *TabId));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("tabId"), TabId);
			return SuccessJson(Result);
		}

		if (Action == TEXT("open_settings"))
		{
			ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings"));
			if (!Settings)
			{
				return ErrorJson(TEXT("Settings 模块不可用。"));
			}
			const FName Container(*GetString(Args, { TEXT("container"), TEXT("containerName") }));
			const FName Category(*GetString(Args, { TEXT("category"), TEXT("categoryName") }));
			const FName Section(*GetString(Args, { TEXT("section"), TEXT("sectionName") }));
			Settings->ShowViewer(Container.IsNone() ? TEXT("Project") : Container, Category, Section);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("container"), (Container.IsNone() ? FName(TEXT("Project")) : Container).ToString());
			Result->SetStringField(TEXT("category"), Category.ToString());
			Result->SetStringField(TEXT("section"), Section.ToString());
			return SuccessJson(Result);
		}

		if (Action == TEXT("hot_reload"))
		{
			UWorld* World = GEditor->GetEditorWorldContext().World();
			const bool bRequested = GEditor->Exec(World, TEXT("LiveCoding.Compile"));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("requested"), bRequested);
			return SuccessJson(Result);
		}

		if (Action == TEXT("reload_bridge"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("moduleLoaded"), FModuleManager::Get().IsModuleLoaded(TEXT("UnrealAgentMCPEditor")));
			Result->SetBoolField(TEXT("reloaded"), false);
			Result->SetStringField(TEXT("registryMode"), TEXT("Unreal Agent 原生静态注册表"));
			Result->SetStringField(TEXT("message"),
				TEXT("独立 C++ Toolset 随模块构造，无外部脚本缓存需要重载；"
					 "若二进制已更新，请使用 hot_reload 或重启编辑器。"));
			return SuccessJson(Result);
		}

		if (Action == TEXT("undo") || Action == TEXT("redo"))
		{
			const bool bSucceeded = Action == TEXT("undo") ? GEditor->UndoTransaction() : GEditor->RedoTransaction();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("changed"), bSucceeded);
			return SuccessJson(Result);
		}

		if (Action == TEXT("open_asset"))
		{
			const FString AssetPath = GetString(Args, { TEXT("assetPath"), TEXT("path") });
			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
			UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!Asset || !Subsystem || !Subsystem->OpenEditorForAsset(Asset))
			{
				return ErrorJson(TEXT("资产不存在或无法打开编辑器。"));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
			Result->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
			return SuccessJson(Result);
		}

		if (Action == TEXT("save_dirty"))
		{
			bool bIncludeMaps = true;
			bool bIncludeContent = true;
			Args->TryGetBoolField(TEXT("includeMaps"), bIncludeMaps);
			Args->TryGetBoolField(TEXT("includeContent"), bIncludeContent);
			const bool bSaved = UEditorLoadingAndSavingUtils::SaveDirtyPackages(bIncludeMaps, bIncludeContent);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("saved"), bSaved);
			return bSaved ? SuccessJson(Result) : ErrorJson(TEXT("保存脏包失败。"));
		}

		if (Action == TEXT("list_dirty_packages"))
		{
			TArray<TSharedPtr<FJsonValue>> Content;
			TArray<TSharedPtr<FJsonValue>> Maps;
			for (TObjectIterator<UPackage> It; It; ++It)
			{
				UPackage* Package = *It;
				if (!Package || !Package->IsDirty() || !FPackageName::IsValidLongPackageName(Package->GetName()) || Package->GetName().StartsWith(TEXT("/Script/")) ||
					Package->GetName().StartsWith(TEXT("/Temp/")))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("package"), Package->GetName());
				(Package->ContainsMap() ? Maps : Content).Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("contentCount"), Content.Num());
			Result->SetNumberField(TEXT("mapCount"), Maps.Num());
			Result->SetArrayField(TEXT("content"), Content);
			Result->SetArrayField(TEXT("maps"), Maps);
			return SuccessJson(Result);
		}

		return ErrorJson(TEXT("编辑器会话动作没有有效实现分支。"));
	}
}
