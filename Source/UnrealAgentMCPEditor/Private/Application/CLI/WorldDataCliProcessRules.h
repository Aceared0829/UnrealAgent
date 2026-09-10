// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file WorldDataCliProcessRules.h
 * @brief 本机 CLI 子进程启动参数的纯规则，不访问 Slate 或文件系统。
 */

#include "CoreMinimal.h"

struct FWorldDataCliProcessLaunchSpec
{
	FString Executable;
	FString Arguments;
	FString DisplayPath;
};

namespace WorldDataCliProcessRules
{
	/**
	 * 为 exe、cmd/bat 或 PowerShell 脚本生成可传给 FInteractiveProcess 的启动规格。
	 */
	bool BuildLaunchSpec(const FString& ResolvedCliPath, const FString& CliArguments, const FString& CommandInterpreterPath, const FString& PowerShellPath,
		FWorldDataCliProcessLaunchSpec& OutLaunchSpec);
}
