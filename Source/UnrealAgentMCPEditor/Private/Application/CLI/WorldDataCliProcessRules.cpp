// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file WorldDataCliProcessRules.cpp
 * @brief 本机 CLI 子进程启动参数纯规则实现。
 */

#include "Application/CLI/WorldDataCliProcessRules.h"

#include "Misc/Paths.h"

namespace
{
	FString NormalizePath(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::CollapseRelativeDirectories(Path);
		FPaths::MakePlatformFilename(Path);
		return Path;
	}

	FString QuotePowerShellArgument(FString Argument)
	{
		Argument.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Argument);
	}
}

namespace WorldDataCliProcessRules
{
	bool BuildLaunchSpec(const FString& ResolvedCliPath, const FString& CliArguments, const FString& CommandInterpreterPath, const FString& PowerShellPath,
		FWorldDataCliProcessLaunchSpec& OutLaunchSpec)
	{
		if (ResolvedCliPath.IsEmpty())
		{
			return false;
		}

		const FString FullPath = NormalizePath(ResolvedCliPath);
		const FString Extension = FPaths::GetExtension(FullPath).ToLower();
		OutLaunchSpec = FWorldDataCliProcessLaunchSpec();
		OutLaunchSpec.DisplayPath = FullPath;

#if PLATFORM_WINDOWS
		if (Extension == TEXT("cmd") || Extension == TEXT("bat"))
		{
			if (CommandInterpreterPath.IsEmpty())
			{
				return false;
			}
			OutLaunchSpec.Executable = CommandInterpreterPath;
			OutLaunchSpec.Arguments = FString::Printf(TEXT("/d /s /c \"\"%s\" %s\""), *FullPath, *CliArguments);
			return true;
		}

		if (Extension == TEXT("ps1"))
		{
			if (PowerShellPath.IsEmpty())
			{
				return false;
			}
			OutLaunchSpec.Executable = PowerShellPath;
			OutLaunchSpec.Arguments = FString::Printf(TEXT("-NoProfile -ExecutionPolicy Bypass -File %s %s"), *QuotePowerShellArgument(FullPath), *CliArguments);
			return true;
		}
#endif

		OutLaunchSpec.Executable = FullPath;
		OutLaunchSpec.Arguments = CliArguments;
		return true;
	}
}
