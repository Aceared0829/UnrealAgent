// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealProjectAdapter.Engine.cpp
 * @brief Project 端口的引擎源码检索与自有工具目录查询实现。
 */

#include "Adapters/Unreal/Project/UnrealAgentMCPUnrealProjectAdapter.h"

#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Domains/Project/UnrealAgentMCPProjectService.h"
#include "Application/Telemetry/UnrealAgentMCPPythonExecutionAudit.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizeEnginePath(FString Path)
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			FPaths::CollapseRelativeDirectories(Path);
			return Path;
		}

		bool IsPathInside(const FString& Candidate, const FString& Root)
		{
			const FString NormalizedCandidate = NormalizeEnginePath(Candidate);
			const FString NormalizedRoot = NormalizeEnginePath(Root);
			const FString RootWithSlash = NormalizedRoot.EndsWith(TEXT("/")) ? NormalizedRoot : NormalizedRoot + TEXT("/");
			return NormalizedCandidate.Equals(NormalizedRoot, ESearchCase::IgnoreCase) || NormalizedCandidate.StartsWith(RootWithSlash, ESearchCase::IgnoreCase);
		}

		int32 ReadLimit(const TSharedPtr<FJsonObject>& Args, const int32 DefaultValue)
		{
			double Value = DefaultValue;
			if (Args.IsValid())
			{
				if (!Args->TryGetNumberField(TEXT("maxResults"), Value))
				{
					Args->TryGetNumberField(TEXT("limit"), Value);
				}
			}
			return FMath::Clamp(static_cast<int32>(Value), 1, 5000);
		}

		TArray<FString> FindEngineSourceFiles(const TArray<FString>& Roots, const TArray<FString>& Extensions)
		{
			TArray<FString> Files;
			for (const FString& Root : Roots)
			{
				if (!IFileManager::Get().DirectoryExists(*Root))
					continue;
				TArray<FString> RootFiles;
				IFileManager::Get().FindFilesRecursive(RootFiles, *Root, TEXT("*"), true, false, true);
				for (const FString& File : RootFiles)
				{
					const FString Extension = FPaths::GetExtension(File).ToLower();
					if (Extensions.Contains(Extension))
					{
						Files.Add(File);
					}
				}
			}
			Files.Sort();
			return Files;
		}

		FString SearchEngineFiles(const TArray<FString>& Files, const FString& Query, const int32 Limit)
		{
			if (Query.IsEmpty())
			{
				return ErrorJson(TEXT("缺少非空检索词。"));
			}
			TArray<TSharedPtr<FJsonValue>> Matches;
			bool bTruncated = false;
			for (const FString& File : Files)
			{
				FString Content;
				if (!FFileHelper::LoadFileToString(Content, *File))
					continue;
				TArray<FString> Lines;
				Content.ParseIntoArrayLines(Lines, false);
				for (int32 Index = 0; Index < Lines.Num(); ++Index)
				{
					if (!Lines[Index].Contains(Query, ESearchCase::IgnoreCase))
					{
						continue;
					}
					if (Matches.Num() >= Limit)
					{
						bTruncated = true;
						break;
					}
					TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
					FString Relative = File;
					FPaths::MakePathRelativeTo(Relative, *NormalizeEnginePath(FPaths::EngineDir()));
					FPaths::MakeStandardFilename(Relative);
					Match->SetStringField(TEXT("file"), Relative);
					Match->SetNumberField(TEXT("line"), Index + 1);
					Match->SetStringField(TEXT("text"), Lines[Index].Left(1000));
					Matches.Add(MakeShared<FJsonValueObject>(Match));
				}
				if (bTruncated)
					break;
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("query"), Query);
			Result->SetNumberField(TEXT("count"), Matches.Num());
			Result->SetBoolField(TEXT("truncated"), bTruncated);
			Result->SetArrayField(TEXT("matches"), Matches);
			return SuccessJson(Result);
		}

		TArray<FString> SplitQuery(const FString& Query)
		{
			TArray<FString> Tokens;
			Query.ToLower().ParseIntoArrayWS(Tokens);
			return Tokens;
		}

		int32 ScoreToolCandidate(const FString& Candidate, const TArray<FString>& Tokens)
		{
			const FString Haystack = Candidate.ToLower();
			int32 Score = 0;
			for (const FString& Token : Tokens)
			{
				if (Haystack.Contains(Token))
				{
					Score += Haystack.StartsWith(Token) ? 5 : 2;
				}
			}
			return Score;
		}
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadEngineHeader(const TSharedPtr<FJsonObject>& Args)
	{
		FString Requested;
		if (!Args->TryGetStringField(TEXT("headerPath"), Requested) || Requested.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 headerPath。"));
		}
		const FString SourceRoot = NormalizeEnginePath(FPaths::EngineSourceDir());
		const FString FullPath = NormalizeEnginePath(FPaths::IsRelative(Requested) ? FPaths::Combine(SourceRoot, Requested) : Requested);
		if (!IsPathInside(FullPath, SourceRoot))
		{
			return ErrorJson(TEXT("引擎头文件路径越出 Engine/Source。"));
		}
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FullPath))
		{
			return ErrorJson(FString::Printf(TEXT("无法读取引擎头文件：%s"), *FullPath));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), FullPath);
		Result->SetStringField(TEXT("content"), Content);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::FindEngineSymbol(const TSharedPtr<FJsonObject>& Args)
	{
		FString Symbol;
		Args->TryGetStringField(TEXT("symbol"), Symbol);
		return SearchEngineFiles(FindEngineSourceFiles({ NormalizeEnginePath(FPaths::EngineSourceDir()) }, { TEXT("h"), TEXT("hpp"), TEXT("inl") }), Symbol, ReadLimit(Args, 100));
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListEngineModules(const TSharedPtr<FJsonObject>& Args)
	{
		const FString RuntimeRoot = NormalizeEnginePath(FPaths::Combine(FPaths::EngineSourceDir(), TEXT("Runtime")));
		TArray<FString> Directories;
		IFileManager::Get().FindFiles(Directories, *FPaths::Combine(RuntimeRoot, TEXT("*")), false, true);
		Directories.Sort();
		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FString& Directory : Directories)
		{
			TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
			Module->SetStringField(TEXT("name"), Directory);
			Module->SetStringField(TEXT("sourcePath"), FPaths::Combine(TEXT("Source/Runtime"), Directory));
			Modules.Add(MakeShared<FJsonValueObject>(Module));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Modules.Num());
		Result->SetArrayField(TEXT("modules"), Modules);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SearchEngineCpp(const TSharedPtr<FJsonObject>& Args)
	{
		FString Query;
		Args->TryGetStringField(TEXT("query"), Query);
		FString Tree = TEXT("Runtime");
		Args->TryGetStringField(TEXT("tree"), Tree);
		FString Subdirectory;
		Args->TryGetStringField(TEXT("subdirectory"), Subdirectory);

		const FString SourceRoot = NormalizeEnginePath(FPaths::EngineSourceDir());
		TArray<FString> Roots;
		auto AddSourceTree = [&Roots, &SourceRoot, &Subdirectory](const FString& Name)
		{
			Roots.Add(NormalizeEnginePath(FPaths::Combine(SourceRoot, Name, Subdirectory)));
		};
		if (Tree.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			AddSourceTree(TEXT("Runtime"));
			AddSourceTree(TEXT("Editor"));
			AddSourceTree(TEXT("Developer"));
			Roots.Add(NormalizeEnginePath(FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins"), Subdirectory)));
		}
		else if (Tree.Equals(TEXT("Plugins"), ESearchCase::IgnoreCase))
		{
			Roots.Add(NormalizeEnginePath(FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins"), Subdirectory)));
		}
		else if (Tree.Equals(TEXT("Runtime"), ESearchCase::IgnoreCase) || Tree.Equals(TEXT("Editor"), ESearchCase::IgnoreCase) ||
			Tree.Equals(TEXT("Developer"), ESearchCase::IgnoreCase))
		{
			AddSourceTree(Tree);
		}
		else
		{
			return ErrorJson(TEXT("tree 仅支持 Runtime、Editor、Developer、Plugins 或 all。"));
		}
		return SearchEngineFiles(FindEngineSourceFiles(Roots, { TEXT("h"), TEXT("hpp"), TEXT("cpp"), TEXT("inl") }), Query, ReadLimit(Args, 500));
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SearchTools(const TSharedPtr<FJsonObject>& Args)
	{
		FString Query;
		Args->TryGetStringField(TEXT("query"), Query);
		const TArray<FString> Tokens = SplitQuery(Query);
		if (Tokens.IsEmpty())
		{
			return ErrorJson(TEXT("缺少非空 query。"));
		}

		struct FCandidate
		{
			FString Tool;
			FString Action;
			FString Description;
			int32 Score = 0;
		};
		TArray<FCandidate> Candidates;
		auto AddAction = [&Candidates, &Tokens](const FString& Tool, const FString& Action, const FString& Description)
		{
			FCandidate Candidate{ Tool, Action, Description, ScoreToolCandidate(Tool + TEXT(" ") + Action + TEXT(" ") + Description, Tokens) };
			if (Candidate.Score > 0)
				Candidates.Add(MoveTemp(Candidate));
		};
		for (const FString& Action : FUnrealAgentMCPProjectService::GetImplementedActions())
		{
			AddAction(TEXT("project"), Action, TEXT("工程、配置、源码、构建与模块操作"));
		}
		for (const FString& Action : FUnrealAgentMCPLevelService::GetImplementedActions())
		{
			AddAction(TEXT("level"), Action, TEXT("关卡与 Actor 操作"));
		}

		TArray<TSharedPtr<FJsonValue>> Definitions;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FUnrealAgentMCPServer::GetToolDefinitionsJson());
		if (FJsonSerializer::Deserialize(Reader, Definitions))
		{
			for (const TSharedPtr<FJsonValue>& Value : Definitions)
			{
				const TSharedPtr<FJsonObject> Definition = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Definition.IsValid())
					continue;
				FString Name;
				FString Description;
				Definition->TryGetStringField(TEXT("name"), Name);
				Definition->TryGetStringField(TEXT("description"), Description);
				AddAction(Name, FString(), Description);
			}
		}
		Candidates.Sort(
			[](const FCandidate& Left, const FCandidate& Right)
			{
				if (Left.Score != Right.Score)
					return Left.Score > Right.Score;
				if (Left.Tool != Right.Tool)
					return Left.Tool < Right.Tool;
				return Left.Action < Right.Action;
			});

		const int32 Limit = ReadLimit(Args, 20);
		TArray<TSharedPtr<FJsonValue>> Matches;
		for (int32 Index = 0; Index < Candidates.Num() && Index < Limit; ++Index)
		{
			const FCandidate& Candidate = Candidates[Index];
			TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
			Match->SetStringField(TEXT("tool"), Candidate.Tool);
			Match->SetStringField(TEXT("action"), Candidate.Action);
			Match->SetStringField(TEXT("description"), Candidate.Description);
			Match->SetNumberField(TEXT("score"), Candidate.Score);
			Matches.Add(MakeShared<FJsonValueObject>(Match));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("query"), Query);
		Result->SetNumberField(TEXT("count"), Matches.Num());
		Result->SetArrayField(TEXT("matches"), Matches);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ExecutePythonReport(const TSharedPtr<FJsonObject>& Args)
	{
		const TArray<FPythonExecutionAuditRecord> Records = FUnrealAgentMCPPythonExecutionAudit::Snapshot();
		TArray<TSharedPtr<FJsonValue>> Overlapping;
		for (const FPythonExecutionAuditRecord& Record : Records)
		{
			TSharedRef<FJsonObject> SearchArgs = MakeShared<FJsonObject>();
			SearchArgs->SetStringField(TEXT("query"), Record.TaskSummary);
			SearchArgs->SetNumberField(TEXT("limit"), 3);
			const FString SearchJson = SearchTools(SearchArgs);
			TSharedPtr<FJsonObject> SearchResult;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SearchJson);
			if (!FJsonSerializer::Deserialize(Reader, SearchResult) || !SearchResult.IsValid())
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* Matches = nullptr;
			if (!SearchResult->TryGetArrayField(TEXT("matches"), Matches) || Matches == nullptr || Matches->IsEmpty())
			{
				continue;
			}
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("timestampUtc"), Record.TimestampUtc.ToIso8601());
			Row->SetStringField(TEXT("taskSummary"), Record.TaskSummary);
			Row->SetStringField(TEXT("codeFingerprint"), Record.CodeFingerprint);
			Row->SetNumberField(TEXT("codeCharacterCount"), Record.CodeCharacterCount);
			Row->SetBoolField(TEXT("succeeded"), Record.bSucceeded);
			Row->SetArrayField(TEXT("suggestions"), *Matches);
			Overlapping.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("totalCalls"), Records.Num());
		Result->SetArrayField(TEXT("overlapping"), Overlapping);
		Result->SetNumberField(TEXT("overlapRate"), Records.IsEmpty() ? 0.0 : static_cast<double>(Overlapping.Num()) / Records.Num());
		return SuccessJson(Result);
	}
}
