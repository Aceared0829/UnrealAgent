// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPythonExecutionSafety.h
 * @brief Unreal 内嵌 Python 的统一执行保护与已知原生崩溃入口拦截。
 */

#pragma once

#include "CoreMinimal.h"

class IPythonScriptPlugin;
struct FPythonCommandEx;

namespace UnrealAgentMCP::PythonExecutionSafety
{
	struct FExecutionResult
	{
		bool bDispatched = false;
		bool bSucceeded = false;
		double DurationMs = 0.0;
		FString Error;
	};

	/**
	 * 执行 Python 命令。SourceToValidate 为空时只做运行态保护；非空时还会检查
	 * UE 当前版本中已确认会触发原生空指针的 Python API。
	 */
	FExecutionResult Execute(IPythonScriptPlugin* Python, FPythonCommandEx& Command, const FString* SourceToValidate);
}
