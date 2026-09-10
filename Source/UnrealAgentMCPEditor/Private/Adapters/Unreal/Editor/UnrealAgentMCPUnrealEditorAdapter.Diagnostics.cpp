// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Diagnostics.cpp
 * @brief 编辑器性能、控制台变量、构建状态与日志诊断操作。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Algo/Sort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

namespace UnrealAgentMCP
{
	namespace
	{
		class FEditorLogSnapshot final : public FOutputDevice
		{
		public:
			virtual void Serialize(const TCHAR* Data, const ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				FScopeLock Guard(&Mutex);
				Lines.Add(FString::Printf(TEXT("%s[%d]: %s"), *Category.ToString(), static_cast<int32>(Verbosity), Data));
			}

			virtual bool CanBeUsedOnAnyThread() const override
			{
				return true;
			}

			TArray<FString> TakeLines()
			{
				FScopeLock Guard(&Mutex);
				return Lines;
			}

		private:
			FCriticalSection Mutex;
			TArray<FString> Lines;
		};

		FEditorLogSnapshot& GetLiveLogSnapshot()
		{
			static FEditorLogSnapshot* Snapshot = []()
			{
				FEditorLogSnapshot* NewSnapshot = new FEditorLogSnapshot();
				if (FOutputDeviceRedirector* Redirector = FOutputDeviceRedirector::Get())
				{
					Redirector->AddOutputDevice(NewSnapshot);
				}
				return NewSnapshot;
			}();
			return *Snapshot;
		}

		FString JsonScalarText(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return FString();
			}
			FString Text;
			if (Value->TryGetString(Text))
			{
				return Text;
			}
			double Number = 0.0;
			if (Value->TryGetNumber(Number))
			{
				return FString::SanitizeFloat(Number);
			}
			bool bBoolean = false;
			if (Value->TryGetBool(bBoolean))
			{
				return bBoolean ? TEXT("1") : TEXT("0");
			}
			return FString();
		}

		bool FindLatestLog(FString& OutPath)
		{
			const FString LogDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
			TArray<FString> Files;
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.IterateDirectory(*LogDirectory,
				[&Files](const TCHAR* Path, const bool bIsDirectory)
				{
					const FString FilePath(Path);
					if (!bIsDirectory && FilePath.EndsWith(TEXT(".log"), ESearchCase::IgnoreCase))
					{
						Files.Add(FilePath);
					}
					return true;
				});
			if (Files.IsEmpty())
			{
				return false;
			}
			Algo::Sort(Files,
				[&PlatformFile](const FString& Left, const FString& Right)
				{
					return PlatformFile.GetTimeStamp(*Left) > PlatformFile.GetTimeStamp(*Right);
				});
			OutPath = Files[0];
			return true;
		}

		void ReadCurrentLog(TArray<FString>& OutLines, FString& OutSource)
		{
			FString LatestLogPath;
			FString Content;
			if (FindLatestLog(LatestLogPath) && FFileHelper::LoadFileToString(Content, *LatestLogPath))
			{
				Content.ParseIntoArrayLines(OutLines, false);
				OutSource = LatestLogPath;
				return;
			}

			if (FOutputDeviceRedirector* Redirector = FOutputDeviceRedirector::Get())
			{
				Redirector->FlushThreadedLogs();
			}
			OutLines = GetLiveLogSnapshot().TakeLines();
			OutSource = TEXT("内存日志回放");
		}

		FString CrashRootDirectory()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes")));
		}

		bool IsSafeCrashId(const FString& CrashId)
		{
			return !CrashId.IsEmpty() && CrashId != TEXT(".") && CrashId != TEXT("..") && !CrashId.Contains(TEXT("/")) && !CrashId.Contains(TEXT("\\")) &&
				!CrashId.Contains(TEXT(":"));
		}

		TArray<FString> ListCrashIds()
		{
			TArray<FString> CrashIds;
			const FString Root = CrashRootDirectory();
			IFileManager::Get().FindFiles(CrashIds, *FPaths::Combine(Root, TEXT("*")), false, true);
			CrashIds.RemoveAll(
				[](const FString& Name)
				{
					return !IsSafeCrashId(Name);
				});
			CrashIds.Sort(
				[&Root](const FString& Left, const FString& Right)
				{
					return IFileManager::Get().GetTimeStamp(*FPaths::Combine(Root, Left)) > IFileManager::Get().GetTimeStamp(*FPaths::Combine(Root, Right));
				});
			return CrashIds;
		}

		TSharedRef<FJsonObject> MakeCrashSummary(const FString& CrashId, const bool bIncludeContents)
		{
			const FString Directory = FPaths::Combine(CrashRootDirectory(), CrashId);
			TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
			Summary->SetStringField(TEXT("id"), CrashId);
			Summary->SetStringField(TEXT("directory"), Directory);
			Summary->SetStringField(TEXT("modifiedUtc"), IFileManager::Get().GetTimeStamp(*Directory).ToIso8601());

			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
			TArray<TSharedPtr<FJsonValue>> FileItems;
			for (const FString& File : Files)
			{
				FString Relative = File;
				FPaths::MakePathRelativeTo(Relative, *(Directory + TEXT("/")));
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("relativePath"), Relative);
				Item->SetNumberField(TEXT("sizeBytes"), static_cast<double>(IFileManager::Get().FileSize(*File)));
				if (bIncludeContents &&
					(File.EndsWith(TEXT("CrashContext.runtime-xml"), ESearchCase::IgnoreCase) || File.EndsWith(TEXT("Diagnostics.txt"), ESearchCase::IgnoreCase) ||
						File.EndsWith(TEXT(".log"), ESearchCase::IgnoreCase)))
				{
					FString Content;
					if (FFileHelper::LoadFileToString(Content, *File))
					{
						const int32 MaximumCharacters = 200000;
						Item->SetStringField(TEXT("content"), Content.Len() > MaximumCharacters ? Content.Right(MaximumCharacters) : Content);
						Item->SetBoolField(TEXT("truncated"), Content.Len() > MaximumCharacters);
					}
				}
				FileItems.Add(MakeShared<FJsonValueObject>(Item));
			}
			Summary->SetNumberField(TEXT("fileCount"), FileItems.Num());
			Summary->SetArrayField(TEXT("files"), FileItems);
			return Summary;
		}
	}

	FUnrealAgentMCPUnrealEditorAdapter::FUnrealAgentMCPUnrealEditorAdapter()
	{
		GetLiveLogSnapshot();
		UE_LOG(LogTemp, Verbose, TEXT("Unreal Agent 编辑器日志捕获已启用。"));
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Diagnostics(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("get_perf_stats"))
		{
			const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
			const double Delta = FApp::GetDeltaTime();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("deltaSeconds"), Delta);
			Result->SetNumberField(TEXT("fps"), Delta > UE_SMALL_NUMBER ? 1.0 / Delta : 0.0);
			Result->SetNumberField(TEXT("usedPhysicalMB"), static_cast<double>(Memory.UsedPhysical) / 1024.0 / 1024.0);
			Result->SetNumberField(TEXT("availablePhysicalMB"), static_cast<double>(Memory.AvailablePhysical) / 1024.0 / 1024.0);
			Result->SetNumberField(TEXT("usedVirtualMB"), static_cast<double>(Memory.UsedVirtual) / 1024.0 / 1024.0);
			return SuccessJson(Result);
		}

		if (Action == TEXT("run_stat"))
		{
			FString Command = GetString(Args, { TEXT("command"), TEXT("stat") });
			if (Command.IsEmpty())
			{
				Command = TEXT("stat fps");
			}
			if (!Command.StartsWith(TEXT("stat "), ESearchCase::IgnoreCase))
			{
				Command = TEXT("stat ") + Command;
			}
			UWorld* World = GEditor ? (GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World()) : nullptr;
			if (!GEditor || !GEditor->Exec(World, *Command))
			{
				return ErrorJson(TEXT("Stat 命令执行失败。"));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("command"), Command);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_scalability"))
		{
			const FString Level = GetString(Args, { TEXT("level"), TEXT("quality") });
			const TMap<FString, int32> Levels{ { TEXT("low"), 0 }, { TEXT("medium"), 1 }, { TEXT("high"), 2 }, { TEXT("epic"), 3 }, { TEXT("cinematic"), 4 } };
			const int32* Index = Levels.Find(Level.ToLower());
			if (!Index)
			{
				return ErrorJson(TEXT("level 必须是 Low、Medium、High、Epic 或 Cinematic。"));
			}
			const TCHAR* Groups[] = { TEXT("ViewDistanceQuality"), TEXT("AntiAliasingQuality"), TEXT("ShadowQuality"), TEXT("GlobalIlluminationQuality"), TEXT("ReflectionQuality"),
				TEXT("PostProcessQuality"), TEXT("TextureQuality"), TEXT("EffectsQuality"), TEXT("FoliageQuality"), TEXT("ShadingQuality") };
			for (const TCHAR* Group : Groups)
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*FString::Printf(TEXT("sg.%s"), Group)))
				{
					Variable->Set(*Index, ECVF_SetByCode);
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("level"), Level);
			Result->SetNumberField(TEXT("index"), *Index);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_cvars"))
		{
			const TSharedPtr<FJsonObject>* Cvars = nullptr;
			if (!Args->TryGetObjectField(TEXT("cvars"), Cvars) || !Cvars)
			{
				return ErrorJson(TEXT("缺少 cvars 对象。"));
			}
			TArray<TSharedPtr<FJsonValue>> Applied;
			TArray<TSharedPtr<FJsonValue>> Missing;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Cvars)->Values)
			{
				IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Pair.Key);
				if (!Variable)
				{
					Missing.Add(MakeShared<FJsonValueString>(Pair.Key));
					continue;
				}
				const FString Value = JsonScalarText(Pair.Value);
				Variable->Set(*Value, ECVF_SetByCode);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Pair.Key);
				Item->SetStringField(TEXT("value"), Value);
				Applied.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("appliedCount"), Applied.Num());
			Result->SetArrayField(TEXT("applied"), Applied);
			Result->SetArrayField(TEXT("missing"), Missing);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_build_status"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("status"), TEXT("idle"));
			Result->SetBoolField(TEXT("liveCodingModuleLoaded"), FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")));
			Result->SetBoolField(TEXT("playInEditorRunning"), GEditor && GEditor->PlayWorld);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_message_log"))
		{
			FString LogSource;
			TArray<FString> Lines;
			ReadCurrentLog(Lines, LogSource);
			const FString Query = GetString(Args, { TEXT("query"), TEXT("category") });
			double LimitNumber = 200.0;
			Args->TryGetNumberField(TEXT("limit"), LimitNumber);
			const int32 Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 2000);
			TArray<TSharedPtr<FJsonValue>> Messages;
			for (int32 Index = Lines.Num() - 1; Index >= 0 && Messages.Num() < Limit; --Index)
			{
				const FString& Line = Lines[Index];
				const bool bError = Line.Contains(TEXT("Error"), ESearchCase::IgnoreCase);
				const bool bWarning = Line.Contains(TEXT("Warning"), ESearchCase::IgnoreCase);
				if ((!bError && !bWarning) || (!Query.IsEmpty() && !Line.Contains(Query, ESearchCase::IgnoreCase)))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("severity"), bError ? TEXT("Error") : TEXT("Warning"));
				Item->SetStringField(TEXT("message"), Line);
				Messages.Insert(MakeShared<FJsonValueObject>(Item), 0);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("source"), LogSource);
			Result->SetNumberField(TEXT("count"), Messages.Num());
			Result->SetArrayField(TEXT("messages"), Messages);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_crashes") || Action == TEXT("check_for_crashes"))
		{
			const TArray<FString> CrashIds = ListCrashIds();
			TArray<TSharedPtr<FJsonValue>> Crashes;
			for (const FString& CrashId : CrashIds)
			{
				Crashes.Add(MakeShared<FJsonValueObject>(MakeCrashSummary(CrashId, false)));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("crashRoot"), CrashRootDirectory());
			Result->SetBoolField(TEXT("hasCrashes"), !CrashIds.IsEmpty());
			Result->SetNumberField(TEXT("count"), Crashes.Num());
			Result->SetArrayField(TEXT("crashes"), Crashes);
			if (!CrashIds.IsEmpty())
			{
				Result->SetStringField(TEXT("latestCrashId"), CrashIds[0]);
			}
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_crash_info"))
		{
			FString CrashId = GetString(Args, { TEXT("crashId"), TEXT("id") });
			const TArray<FString> CrashIds = ListCrashIds();
			if (CrashId.IsEmpty() && !CrashIds.IsEmpty())
			{
				CrashId = CrashIds[0];
			}
			if (!IsSafeCrashId(CrashId) || !CrashIds.Contains(CrashId))
			{
				return ErrorJson(TEXT("未找到有效的项目崩溃记录。"));
			}
			return SuccessJson(MakeCrashSummary(CrashId, true));
		}

		if (Action == TEXT("get_log") || Action == TEXT("search_log"))
		{
			FString LogSource;
			TArray<FString> Lines;
			ReadCurrentLog(Lines, LogSource);
			if (Lines.IsEmpty())
			{
				return ErrorJson(FString::Printf(TEXT("没有可读取的项目日志，搜索目录：%s"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir())));
			}
			const FString Query = GetString(Args, { TEXT("query"), TEXT("searchTerm") });
			const FString Category = GetString(Args, { TEXT("category") });
			const FString Severity = GetString(Args, { TEXT("severity") });
			double LimitNumber = 100.0;
			Args->TryGetNumberField(TEXT("lines"), LimitNumber);
			Args->TryGetNumberField(TEXT("limit"), LimitNumber);
			const int32 Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 2000);
			TArray<TSharedPtr<FJsonValue>> Matches;
			for (int32 Index = Lines.Num() - 1; Index >= 0 && Matches.Num() < Limit; --Index)
			{
				const FString& Line = Lines[Index];
				if ((!Query.IsEmpty() && !Line.Contains(Query, ESearchCase::IgnoreCase)) || (!Category.IsEmpty() && !Line.Contains(Category, ESearchCase::IgnoreCase)) ||
					(!Severity.IsEmpty() && !Line.Contains(Severity, ESearchCase::IgnoreCase)))
				{
					continue;
				}
				Matches.Insert(MakeShared<FJsonValueString>(Line), 0);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("logFile"), LogSource);
			Result->SetStringField(TEXT("query"), Query);
			Result->SetNumberField(TEXT("count"), Matches.Num());
			Result->SetArrayField(TEXT("lines"), Matches);
			return SuccessJson(Result);
		}

		return ErrorJson(TEXT("编辑器诊断动作没有有效实现分支。"));
	}
}
