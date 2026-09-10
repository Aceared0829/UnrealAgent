// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealProjectAdapter.cpp
 * @brief Project 端口的配置、源码、文件与模块查询实现。
 */

#include "Adapters/Unreal/Project/UnrealAgentMCPUnrealProjectAdapter.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizeFullPath(FString Path)
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			FPaths::CollapseRelativeDirectories(Path);
			return Path;
		}

		bool ResolveProjectPath(const FString& Requested, FString& OutPath)
		{
			const FString Root = NormalizeFullPath(FPaths::ProjectDir());
			OutPath = NormalizeFullPath(FPaths::IsRelative(Requested) ? FPaths::Combine(Root, Requested) : Requested);
			const FString RootWithSlash = Root.EndsWith(TEXT("/")) ? Root : Root + TEXT("/");
			return OutPath.Equals(Root, ESearchCase::IgnoreCase) || OutPath.StartsWith(RootWithSlash, ESearchCase::IgnoreCase);
		}

		FString ToProjectRelative(const FString& FullPath)
		{
			FString Relative = NormalizeFullPath(FullPath);
			FPaths::MakePathRelativeTo(Relative, *NormalizeFullPath(FPaths::ProjectDir()));
			FPaths::MakeStandardFilename(Relative);
			return Relative;
		}

		bool IsSupportedSourceExtension(const FString& Path)
		{
			const FString Extension = FPaths::GetExtension(Path, true).ToLower();
			return Extension == TEXT(".h") || Extension == TEXT(".hpp") || Extension == TEXT(".cpp") || Extension == TEXT(".inl") || Extension == TEXT(".cs");
		}

		bool IsAllowedSourcePath(const TSharedPtr<FJsonObject>& Args, const bool bAllowPluginSource, FString& OutError)
		{
			FString RequestedPath;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("file_path"), RequestedPath))
			{
				OutError = TEXT("Missing required file_path.");
				return false;
			}
			FString FullPath;
			if (!ResolveProjectPath(RequestedPath, FullPath) || !IsSupportedSourceExtension(FullPath))
			{
				OutError = TEXT("Source writes require a project-contained C++ source path.");
				return false;
			}
			const FString RelativePath = ToProjectRelative(FullPath);
			const bool bProjectSource = RelativePath.StartsWith(TEXT("Source/"), ESearchCase::IgnoreCase);
			const bool bPluginSource =
				bAllowPluginSource && RelativePath.StartsWith(TEXT("Plugins/"), ESearchCase::IgnoreCase) && RelativePath.Contains(TEXT("/Source/"), ESearchCase::IgnoreCase);
			if (!bProjectSource && !bPluginSource)
			{
				OutError = bAllowPluginSource ? TEXT("Source writes are limited to Source or Plugins/*/Source.") : TEXT("C++ writes are limited to the project Source directory.");
				return false;
			}
			return true;
		}

		FString ResolveConfigPath(const TSharedPtr<FJsonObject>& Args)
		{
			FString ConfigName = TEXT("Engine");
			if (Args.IsValid())
			{
				Args->TryGetStringField(TEXT("configName"), ConfigName);
			}
			if (ConfigName.Contains(TEXT("/")) || ConfigName.Contains(TEXT("\\")) || ConfigName.EndsWith(TEXT(".ini")))
			{
				FString Resolved;
				return ResolveProjectPath(ConfigName, Resolved) ? Resolved : FString();
			}
			return NormalizeFullPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Default") + ConfigName + TEXT(".ini")));
		}

		TArray<FString> FindProjectFiles(const FString& Directory, const TArray<FString>& Extensions)
		{
			FString FullDirectory;
			if (!ResolveProjectPath(Directory, FullDirectory) || !IFileManager::Get().DirectoryExists(*FullDirectory))
			{
				return {};
			}
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *FullDirectory, TEXT("*"), true, false, false);
			if (!Extensions.IsEmpty())
			{
				Files.RemoveAll(
					[&Extensions](const FString& File)
					{
						FString Extension = FPaths::GetExtension(File, false).ToLower();
						return !Extensions.ContainsByPredicate(
							[&Extension](FString Expected)
							{
								Expected.RemoveFromStart(TEXT("."));
								return Extension.Equals(Expected, ESearchCase::IgnoreCase);
							});
					});
			}
			Files.Sort();
			return Files;
		}

		TArray<FString> FindProjectModuleBuildFiles()
		{
			TArray<FString> BuildFiles;
			IFileManager::Get().FindFilesRecursive(BuildFiles, *NormalizeFullPath(FPaths::ProjectDir()), TEXT("*.Build.cs"), true, false, true);
			BuildFiles.Sort();
			return BuildFiles;
		}

		FString MakeFilesResult(const TArray<FString>& Files, const int32 Limit)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Files.Num() && Index < Limit; ++Index)
			{
				Values.Add(MakeShared<FJsonValueString>(ToProjectRelative(Files[Index])));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("matched"), Files.Num());
			Result->SetNumberField(TEXT("count"), Values.Num());
			Result->SetBoolField(TEXT("truncated"), Files.Num() > Values.Num());
			Result->SetArrayField(TEXT("files"), Values);
			return SuccessJson(Result);
		}

		FString SearchFiles(const TArray<FString>& Files, const FString& Query, const int32 Limit)
		{
			if (Query.IsEmpty())
			{
				return ErrorJson(TEXT("缺少非空 query。"));
			}
			TArray<TSharedPtr<FJsonValue>> Matches;
			int32 MatchedCount = 0;
			for (const FString& File : Files)
			{
				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *File))
				{
					continue;
				}
				TArray<FString> Lines;
				Text.ParseIntoArrayLines(Lines, false);
				for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
				{
					if (!Lines[LineIndex].Contains(Query, ESearchCase::IgnoreCase))
					{
						continue;
					}
					++MatchedCount;
					if (Matches.Num() < Limit)
					{
						TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
						Match->SetStringField(TEXT("file"), ToProjectRelative(File));
						Match->SetNumberField(TEXT("line"), LineIndex + 1);
						Match->SetStringField(TEXT("text"), Lines[LineIndex].Left(1000));
						Matches.Add(MakeShared<FJsonValueObject>(Match));
					}
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("query"), Query);
			Result->SetNumberField(TEXT("matched"), MatchedCount);
			Result->SetNumberField(TEXT("count"), Matches.Num());
			Result->SetBoolField(TEXT("truncated"), MatchedCount > Matches.Num());
			Result->SetArrayField(TEXT("matches"), Matches);
			return SuccessJson(Result);
		}

		int32 GetLimit(const TSharedPtr<FJsonObject>& Args, const int32 DefaultValue = 500)
		{
			double Value = DefaultValue;
			if (Args.IsValid())
			{
				Args->TryGetNumberField(TEXT("maxResults"), Value);
				Args->TryGetNumberField(TEXT("limit"), Value);
			}
			return FMath::Clamp(static_cast<int32>(Value), 1, 5000);
		}
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::GetStatus(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::GetProjectInfo(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::GetInfo(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::GetProjectInfo(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadConfig(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Path = ResolveConfigPath(Args);
		if (Path.IsEmpty())
		{
			return ErrorJson(TEXT("配置路径超出项目目录。"));
		}
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			return ErrorJson(FString::Printf(TEXT("无法读取配置文件：%s"), *Path));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), ToProjectRelative(Path));
		Result->SetStringField(TEXT("content"), Content);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SearchConfig(const TSharedPtr<FJsonObject>& Args)
	{
		FString Query;
		Args->TryGetStringField(TEXT("query"), Query);
		const TArray<FString> Files = FindProjectFiles(TEXT("Config"), { TEXT("ini") });
		return SearchFiles(Files, Query, GetLimit(Args));
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListConfigTags(const TSharedPtr<FJsonObject>& Args)
	{
		const TArray<FString> Files = FindProjectFiles(TEXT("Config"), { TEXT("ini") });
		TSet<FString> UniqueTags;
		const FRegexPattern Pattern(TEXT("Tag\\s*=\\s*\"([^\"]+)\""));
		for (const FString& File : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File))
				continue;
			FRegexMatcher Matcher(Pattern, Text);
			while (Matcher.FindNext())
			{
				UniqueTags.Add(Matcher.GetCaptureGroup(1));
			}
		}
		TArray<FString> SortedTags = UniqueTags.Array();
		SortedTags.Sort();
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FString& Tag : SortedTags)
		{
			Tags.Add(MakeShared<FJsonValueString>(Tag));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Tags.Num());
		Result->SetArrayField(TEXT("tags"), Tags);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadCppHeader(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::ReadFile(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadModule(const TSharedPtr<FJsonObject>& Args)
	{
		FString ModuleName;
		if (!Args->TryGetStringField(TEXT("moduleName"), ModuleName) || ModuleName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 moduleName。"));
		}
		const TArray<FString> BuildFiles = FindProjectModuleBuildFiles();
		FString ModuleDirectory;
		FString BuildFile;
		for (const FString& File : BuildFiles)
		{
			if (FPaths::GetCleanFilename(File).Equals(ModuleName + TEXT(".Build.cs"), ESearchCase::IgnoreCase))
			{
				BuildFile = File;
				ModuleDirectory = FPaths::GetPath(File);
				break;
			}
		}
		if (ModuleDirectory.IsEmpty())
		{
			return ErrorJson(FString::Printf(TEXT("未找到模块：%s"), *ModuleName));
		}
		FString BuildContent;
		FFileHelper::LoadFileToString(BuildContent, *BuildFile);
		TArray<FString> Sources;
		IFileManager::Get().FindFilesRecursive(Sources, *ModuleDirectory, TEXT("*"), true, false, false);
		Sources.RemoveAll(
			[](const FString& File)
			{
				const FString Extension = FPaths::GetExtension(File).ToLower();
				return Extension != TEXT("h") && Extension != TEXT("hpp") && Extension != TEXT("cpp") && Extension != TEXT("inl");
			});
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), ModuleName);
		Result->SetStringField(TEXT("directory"), ToProjectRelative(ModuleDirectory));
		Result->SetStringField(TEXT("buildFile"), ToProjectRelative(BuildFile));
		Result->SetStringField(TEXT("buildRules"), BuildContent);
		TArray<TSharedPtr<FJsonValue>> SourceValues;
		for (const FString& Source : Sources)
		{
			SourceValues.Add(MakeShared<FJsonValueString>(ToProjectRelative(Source)));
		}
		Result->SetArrayField(TEXT("sourceFiles"), SourceValues);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListModules(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FString& BuildFile : FindProjectModuleBuildFiles())
		{
			const FString FileName = FPaths::GetCleanFilename(BuildFile);
			FString ModuleName = FileName;
			ModuleName.RemoveFromEnd(TEXT(".Build.cs"));
			TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
			Module->SetStringField(TEXT("name"), ModuleName);
			Module->SetStringField(TEXT("directory"), ToProjectRelative(FPaths::GetPath(BuildFile)));
			Module->SetStringField(TEXT("buildFile"), ToProjectRelative(BuildFile));
			Modules.Add(MakeShared<FJsonValueObject>(Module));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Modules.Num());
		Result->SetArrayField(TEXT("modules"), Modules);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SearchCpp(const TSharedPtr<FJsonObject>& Args)
	{
		FString Query;
		Args->TryGetStringField(TEXT("query"), Query);
		FString Directory = TEXT(".");
		Args->TryGetStringField(TEXT("directory"), Directory);
		const TArray<FString> Files = FindProjectFiles(Directory, { TEXT("h"), TEXT("hpp"), TEXT("cpp"), TEXT("inl") });
		return SearchFiles(Files, Query, GetLimit(Args));
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListFiles(const TSharedPtr<FJsonObject>& Args)
	{
		FString Directory = TEXT(".");
		Args->TryGetStringField(TEXT("directory"), Directory);
		TArray<FString> Extensions;
		if (!Args->TryGetStringArrayField(TEXT("extensions"), Extensions))
		{
			FString Extension;
			if (Args->TryGetStringField(TEXT("extension"), Extension) && !Extension.IsEmpty())
			{
				Extensions.Add(Extension);
			}
		}
		return MakeFilesResult(FindProjectFiles(Directory, Extensions), GetLimit(Args, 1000));
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListProjectModules(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::ListProjectModules(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ListLoadedModules(const TSharedPtr<FJsonObject>& Args)
	{
		FString Filter;
		Args->TryGetStringField(TEXT("filter"), Filter);
		bool bLoadedOnly = false;
		Args->TryGetBoolField(TEXT("loadedOnly"), bLoadedOnly);
		TArray<FModuleStatus> Statuses;
		FModuleManager::Get().QueryModules(Statuses);
		Statuses.Sort(
			[](const FModuleStatus& Left, const FModuleStatus& Right)
			{
				return Left.Name < Right.Name;
			});
		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FModuleStatus& Status : Statuses)
		{
			const FString& Text = Status.Name;
			if (!Filter.IsEmpty() && !Text.Contains(Filter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (bLoadedOnly && !Status.bIsLoaded)
			{
				continue;
			}
			TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
			Module->SetStringField(TEXT("name"), Text);
			Module->SetBoolField(TEXT("loaded"), Status.bIsLoaded);
			Module->SetBoolField(TEXT("gameModule"), Status.bIsGameModule);
			Module->SetStringField(TEXT("filePath"), Status.FilePath);
			Modules.Add(MakeShared<FJsonValueObject>(Module));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Modules.Num());
		Result->SetArrayField(TEXT("modules"), Modules);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::IsModuleLoaded(const TSharedPtr<FJsonObject>& Args)
	{
		FString ModuleName;
		if (!Args->TryGetStringField(TEXT("moduleName"), ModuleName) || ModuleName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 moduleName。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("moduleName"), ModuleName);
		Result->SetBoolField(TEXT("loaded"), FModuleManager::Get().IsModuleLoaded(FName(*ModuleName)));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::LiveCodingStatus(const TSharedPtr<FJsonObject>& Args)
	{
		const bool bLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding"));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("available"), bLoaded);
		Result->SetBoolField(TEXT("moduleLoaded"), bLoaded);
		Result->SetBoolField(TEXT("compiling"), false);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::WriteCppFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		if (!IsAllowedSourcePath(Args, false, Error))
		{
			return ErrorJson(Error);
		}
		return ExtractedTools::WriteFile(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadCppSource(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::ReadFile(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::WriteSourceFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		if (!IsAllowedSourcePath(Args, true, Error))
		{
			return ErrorJson(Error);
		}
		return ExtractedTools::WriteFile(Args);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::ReadSourceFile(const TSharedPtr<FJsonObject>& Args)
	{
		return ExtractedTools::ReadFile(Args);
	}
}
