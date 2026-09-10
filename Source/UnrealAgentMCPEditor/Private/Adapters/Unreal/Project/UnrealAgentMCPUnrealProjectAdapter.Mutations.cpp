// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealProjectAdapter.Mutations.cpp
 * @brief Project 端口的配置写入、代码生成、构建与 Live Coding 实现。
 */

#include "Adapters/Unreal/Project/UnrealAgentMCPUnrealProjectAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizeMutationPath(FString Path)
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			FPaths::CollapseRelativeDirectories(Path);
			return Path;
		}

		bool IsMutationPathInside(const FString& Candidate, const FString& Root)
		{
			const FString FullCandidate = NormalizeMutationPath(Candidate);
			const FString FullRoot = NormalizeMutationPath(Root);
			const FString RootWithSlash = FullRoot.EndsWith(TEXT("/")) ? FullRoot : FullRoot + TEXT("/");
			return FullCandidate.Equals(FullRoot, ESearchCase::IgnoreCase) || FullCandidate.StartsWith(RootWithSlash, ESearchCase::IgnoreCase);
		}

		TArray<FString> FindMutationBuildFiles()
		{
			TArray<FString> BuildFiles;
			IFileManager::Get().FindFilesRecursive(BuildFiles, *NormalizeMutationPath(FPaths::ProjectDir()), TEXT("*.Build.cs"), true, false, true);
			BuildFiles.Sort();
			return BuildFiles;
		}

		bool ResolveModuleDirectory(const FString& ModuleName, FString& OutDirectory, FString& OutBuildFile)
		{
			for (const FString& BuildFile : FindMutationBuildFiles())
			{
				if (FPaths::GetCleanFilename(BuildFile).Equals(ModuleName + TEXT(".Build.cs"), ESearchCase::IgnoreCase))
				{
					OutBuildFile = BuildFile;
					OutDirectory = FPaths::GetPath(BuildFile);
					return true;
				}
			}
			return false;
		}

		FString ResolveWritableConfig(const TSharedPtr<FJsonObject>& Args)
		{
			FString ConfigName = TEXT("Engine");
			Args->TryGetStringField(TEXT("configName"), ConfigName);
			FString Path;
			if (ConfigName.Contains(TEXT("/")) || ConfigName.Contains(TEXT("\\")) || ConfigName.EndsWith(TEXT(".ini")))
			{
				Path = FPaths::IsRelative(ConfigName) ? FPaths::Combine(FPaths::ProjectDir(), ConfigName) : ConfigName;
			}
			else
			{
				Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Default") + ConfigName + TEXT(".ini"));
			}
			Path = NormalizeMutationPath(Path);
			return IsMutationPathInside(Path, FPaths::ProjectDir()) ? Path : FString();
		}

		FString RunUnrealBuildTool(const FString& Arguments)
		{
			const FString Executable = NormalizeMutationPath(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe")));
			if (!IFileManager::Get().FileExists(*Executable))
			{
				return ErrorJson(FString::Printf(TEXT("未找到 UnrealBuildTool：%s"), *Executable));
			}
			int32 ReturnCode = INDEX_NONE;
			FString StandardOutput;
			FString StandardError;
			const bool bStarted = FPlatformProcess::ExecProcess(*Executable, *Arguments, &ReturnCode, &StandardOutput, &StandardError);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("processStarted"), bStarted);
			Result->SetNumberField(TEXT("exitCode"), ReturnCode);
			Result->SetStringField(TEXT("stdout"), StandardOutput.Right(50000));
			Result->SetStringField(TEXT("stderr"), StandardError.Right(50000));
			if (!bStarted || ReturnCode != 0)
			{
				Result->SetStringField(TEXT("error"), TEXT("UnrealBuildTool 执行失败。"));
				return JsonObjectToString(Result);
			}
			return SuccessJson(Result);
		}

		bool IsValidCppIdentifier(const FString& Name)
		{
			if (Name.IsEmpty() || !(FChar::IsAlpha(Name[0]) || Name[0] == TEXT('_')))
			{
				return false;
			}
			for (const TCHAR Character : Name)
			{
				if (!(FChar::IsAlnum(Character) || Character == TEXT('_')))
				{
					return false;
				}
			}
			return true;
		}

		struct FParentClassTemplate
		{
			FString Prefix;
			FString ClassName;
			FString Header;
		};

		FParentClassTemplate ResolveParentTemplate(FString ParentName)
		{
			if (ParentName.IsEmpty())
				ParentName = TEXT("Object");
			if (ParentName.Contains(TEXT(".")))
			{
				ParentName = ParentName.RightChop(ParentName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
			}
			if (ParentName.Equals(TEXT("AActor")) || ParentName.Equals(TEXT("APawn")) || ParentName.Equals(TEXT("ACharacter")) || ParentName.Equals(TEXT("UObject")) ||
				ParentName.Equals(TEXT("UActorComponent")) || ParentName.Equals(TEXT("USceneComponent")))
			{
				ParentName.RightChopInline(1);
			}

			if (ParentName.Equals(TEXT("Actor"), ESearchCase::IgnoreCase))
				return { TEXT("A"), TEXT("Actor"), TEXT("GameFramework/Actor.h") };
			if (ParentName.Equals(TEXT("Pawn"), ESearchCase::IgnoreCase))
				return { TEXT("A"), TEXT("Pawn"), TEXT("GameFramework/Pawn.h") };
			if (ParentName.Equals(TEXT("Character"), ESearchCase::IgnoreCase))
				return { TEXT("A"), TEXT("Character"), TEXT("GameFramework/Character.h") };
			if (ParentName.Equals(TEXT("ActorComponent"), ESearchCase::IgnoreCase))
				return { TEXT("U"), TEXT("ActorComponent"), TEXT("Components/ActorComponent.h") };
			if (ParentName.Equals(TEXT("SceneComponent"), ESearchCase::IgnoreCase))
				return { TEXT("U"), TEXT("SceneComponent"), TEXT("Components/SceneComponent.h") };
			return { TEXT("U"), TEXT("Object"), TEXT("UObject/Object.h") };
		}

		FString MakeApiMacro(const FString& ModuleName)
		{
			FString Macro;
			for (const TCHAR Character : ModuleName)
			{
				Macro.AppendChar(FChar::IsAlnum(Character) ? FChar::ToUpper(Character) : TEXT('_'));
			}
			return Macro + TEXT("_API");
		}
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SetProject(const TSharedPtr<FJsonObject>& Args)
	{
		FString Requested;
		if (!Args->TryGetStringField(TEXT("projectPath"), Requested) || Requested.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 projectPath。"));
		}
		const FString FullPath = NormalizeMutationPath(Requested);
		if (!IFileManager::Get().FileExists(*FullPath) || !FPaths::GetExtension(FullPath).Equals(TEXT("uproject"), ESearchCase::IgnoreCase))
		{
			return ErrorJson(TEXT("projectPath 必须指向现有 .uproject 文件。"));
		}
		const FString Current = NormalizeMutationPath(FPaths::GetProjectFilePath());
		if (!FullPath.Equals(Current, ESearchCase::IgnoreCase))
		{
			return ErrorJson(TEXT("编辑器内进程不能热切换工程；请让独立 Host 重新连接目标工程。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("projectPath"), FullPath);
		Result->SetBoolField(TEXT("alreadyCurrent"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::SetConfig(const TSharedPtr<FJsonObject>& Args)
	{
		FString Section;
		FString Key;
		FString Value;
		if (!Args->TryGetStringField(TEXT("section"), Section) || !Args->TryGetStringField(TEXT("key"), Key) || !Args->TryGetStringField(TEXT("value"), Value) ||
			Section.IsEmpty() || Key.IsEmpty())
		{
			return ErrorJson(TEXT("section、key 与 value 均为必填。"));
		}
		const FString Path = ResolveWritableConfig(Args);
		if (Path.IsEmpty())
		{
			return ErrorJson(TEXT("配置路径越出项目目录。"));
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FConfigFile ConfigFile;
		if (IFileManager::Get().FileExists(*Path))
		{
			ConfigFile.Read(Path);
		}
		ConfigFile.SetString(*Section, *Key, *Value);
		if (!ConfigFile.Write(Path, false))
		{
			return ErrorJson(TEXT("配置文件写入失败。"));
		}
		GConfig->UnloadFile(Path);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), Path);
		Result->SetStringField(TEXT("section"), Section);
		Result->SetStringField(TEXT("key"), Key);
		Result->SetStringField(TEXT("value"), Value);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::Build(const TSharedPtr<FJsonObject>& Args)
	{
		FString Configuration = TEXT("Development");
		FString Platform = TEXT("Win64");
		Args->TryGetStringField(TEXT("configuration"), Configuration);
		Args->TryGetStringField(TEXT("platform"), Platform);
		bool bClean = false;
		Args->TryGetBoolField(TEXT("clean"), bClean);
		const FString Target = FString(FApp::GetProjectName()) + TEXT("Editor");
		const FString Arguments = FString::Printf(TEXT("%s %s %s -Project=\"%s\" -WaitMutex -NoHotReload%s"), *Target, *Platform, *Configuration,
			*NormalizeMutationPath(FPaths::GetProjectFilePath()), bClean ? TEXT(" -Clean") : TEXT(""));
		return RunUnrealBuildTool(Arguments);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::GenerateProjectFiles(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Arguments = FString::Printf(TEXT("-ProjectFiles -Project=\"%s\" -Game -Engine -Progress"), *NormalizeMutationPath(FPaths::GetProjectFilePath()));
		return RunUnrealBuildTool(Arguments);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::CreateCppClass(const TSharedPtr<FJsonObject>& Args)
	{
		FString ClassName;
		if (!Args->TryGetStringField(TEXT("className"), ClassName) || !IsValidCppIdentifier(ClassName))
		{
			return ErrorJson(TEXT("className 必须是合法的 C++ 标识符。"));
		}
		FString ModuleName;
		Args->TryGetStringField(TEXT("moduleName"), ModuleName);
		if (ModuleName.IsEmpty())
			Args->TryGetStringField(TEXT("module"), ModuleName);
		if (ModuleName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 moduleName。"));
		}
		FString ModuleDirectory;
		FString BuildFile;
		if (!ResolveModuleDirectory(ModuleName, ModuleDirectory, BuildFile))
		{
			return ErrorJson(FString::Printf(TEXT("未找到模块：%s"), *ModuleName));
		}

		FString ParentName = TEXT("Object");
		Args->TryGetStringField(TEXT("parentClass"), ParentName);
		const FParentClassTemplate Parent = ResolveParentTemplate(ParentName);
		if (ClassName.StartsWith(Parent.Prefix))
			ClassName.RightChopInline(1);

		FString Domain = TEXT("Public");
		Args->TryGetStringField(TEXT("classDomain"), Domain);
		if (Domain.Equals(TEXT("private"), ESearchCase::IgnoreCase))
			Domain = TEXT("Private");
		else if (Domain.Equals(TEXT("classes"), ESearchCase::IgnoreCase))
			Domain = TEXT("Classes");
		else
			Domain = TEXT("Public");
		FString SubPath;
		Args->TryGetStringField(TEXT("subPath"), SubPath);
		if (SubPath.Contains(TEXT("..")))
		{
			return ErrorJson(TEXT("subPath 不允许包含上级目录。"));
		}
		const FString OutputDirectory = NormalizeMutationPath(FPaths::Combine(ModuleDirectory, Domain, SubPath));
		if (!IsMutationPathInside(OutputDirectory, ModuleDirectory))
		{
			return ErrorJson(TEXT("生成目录越出目标模块。"));
		}
		const FString HeaderPath = FPaths::Combine(OutputDirectory, ClassName + TEXT(".h"));
		const FString SourcePath = FPaths::Combine(OutputDirectory, ClassName + TEXT(".cpp"));
		if (IFileManager::Get().FileExists(*HeaderPath) || IFileManager::Get().FileExists(*SourcePath))
		{
			return ErrorJson(TEXT("目标 C++ 类文件已存在。"));
		}

		const FString FullClassName = Parent.Prefix + ClassName;
		const FString FullParentName = Parent.Prefix + Parent.ClassName;
		const FString Header =
			FString::Printf(TEXT("#pragma once\n\n") TEXT("#include \"CoreMinimal.h\"\n") TEXT("#include \"%s\"\n") TEXT("#include \"%s.generated.h\"\n\n") TEXT("UCLASS()\n")
								TEXT("class %s %s : public %s\n") TEXT("{\n") TEXT("\tGENERATED_BODY()\n\n") TEXT("public:\n") TEXT("\t%s();\n") TEXT("};\n"),
				*Parent.Header, *ClassName, *MakeApiMacro(ModuleName), *FullClassName, *FullParentName, *FullClassName);
		const FString Source = FString::Printf(TEXT("#include \"%s.h\"\n\n") TEXT("%s::%s()\n") TEXT("{\n") TEXT("}\n"), *ClassName, *FullClassName, *FullClassName);
		IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		if (!FFileHelper::SaveStringToFile(Header, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
			!FFileHelper::SaveStringToFile(Source, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().Delete(*HeaderPath, false, true, true);
			IFileManager::Get().Delete(*SourcePath, false, true, true);
			return ErrorJson(TEXT("C++ 类文件写入失败。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("className"), FullClassName);
		Result->SetStringField(TEXT("headerPath"), HeaderPath);
		Result->SetStringField(TEXT("sourcePath"), SourcePath);
		Result->SetBoolField(TEXT("needsEditorRestart"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::LiveCodingCompile(const TSharedPtr<FJsonObject>& Args)
	{
		bool bWait = false;
		Args->TryGetBoolField(TEXT("wait"), bWait);
		const TCHAR* Command = bWait ? TEXT("LiveCoding.CompileSync") : TEXT("LiveCoding.Compile");
		const bool bHandled = IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, nullptr);
		if (!bHandled)
		{
			return ErrorJson(TEXT("当前平台或编辑器会话未提供 Live Coding。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), bWait ? TEXT("completed") : TEXT("in_progress"));
		Result->SetBoolField(TEXT("waited"), bWait);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealProjectAdapter::AddModuleDependency(const TSharedPtr<FJsonObject>& Args)
	{
		FString ModuleName;
		FString Dependency;
		if (!Args->TryGetStringField(TEXT("moduleName"), ModuleName) || !Args->TryGetStringField(TEXT("dependency"), Dependency) || ModuleName.IsEmpty() || Dependency.IsEmpty())
		{
			return ErrorJson(TEXT("moduleName 与 dependency 均为必填。"));
		}
		FString ModuleDirectory;
		FString BuildFile;
		if (!ResolveModuleDirectory(ModuleName, ModuleDirectory, BuildFile))
		{
			return ErrorJson(FString::Printf(TEXT("未找到模块：%s"), *ModuleName));
		}
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *BuildFile))
		{
			return ErrorJson(TEXT("无法读取模块 Build.cs。"));
		}
		const FString QuotedDependency = TEXT("\"") + Dependency + TEXT("\"");
		const bool bExisted = Content.Contains(QuotedDependency, ESearchCase::IgnoreCase);
		if (!bExisted)
		{
			FString Access = TEXT("private");
			Args->TryGetStringField(TEXT("access"), Access);
			const FString Marker =
				Access.Equals(TEXT("public"), ESearchCase::IgnoreCase) ? TEXT("PublicDependencyModuleNames.AddRange") : TEXT("PrivateDependencyModuleNames.AddRange");
			const int32 MarkerIndex = Content.Find(Marker);
			if (MarkerIndex != INDEX_NONE)
			{
				const int32 BraceIndex = Content.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, MarkerIndex);
				if (BraceIndex == INDEX_NONE)
					return ErrorJson(TEXT("Build.cs 依赖块格式无法识别。"));
				Content.InsertAt(BraceIndex + 1, TEXT("\n\t\t\t\"") + Dependency + TEXT("\","));
			}
			else
			{
				const int32 ClassClose = Content.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				const int32 ConstructorClose = Content.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, ClassClose - 1);
				if (ConstructorClose == INDEX_NONE)
					return ErrorJson(TEXT("Build.cs 构造函数格式无法识别。"));
				Content.InsertAt(ConstructorClose,
					FString::Printf(TEXT("\n\t\t%s.AddRange(new string[]\n") TEXT("\t\t{\n") TEXT("\t\t\t\"%s\"\n") TEXT("\t\t});\n"),
						*Marker.LeftChop(FString(TEXT(".AddRange")).Len()), *Dependency));
			}
			if (!FFileHelper::SaveStringToFile(Content, *BuildFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				return ErrorJson(TEXT("Build.cs 写入失败。"));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("buildFile"), BuildFile);
		Result->SetStringField(TEXT("dependency"), Dependency);
		Result->SetBoolField(TEXT("existed"), bExisted);
		Result->SetBoolField(TEXT("rebuildRequired"), true);
		return SuccessJson(Result);
	}
}
