// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPythonExecutionSafety.cpp
 * @brief Unreal 内嵌 Python 的统一执行保护与已知原生崩溃入口拦截。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPPythonExecutionSafety.h"

#include "IPythonScriptPlugin.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreMisc.h"
#include "PythonScriptTypes.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP::PythonExecutionSafety
{
	namespace
	{
		bool bExecutionActive = false;

		bool ValidateKnownNativeCrashCalls(const FString& Source, FString& OutError)
		{
			const FString Normalized = Source.ToLower();
			if (Normalized.Contains(TEXT("get_material_property_input_node")))
			{
				OutError = TEXT(
					"已拒绝执行：UE 5.8 的 MaterialEditingLibrary.get_material_property_input_node* 在材质属性没有对应输入时会触发 MaterialEditor 原生空指针。请改用 worlddata.material.read、worlddata.material.connect_to_property 或 worlddata.material.disconnect_property。");
				return false;
			}
			return true;
		}
	}

	FExecutionResult Execute(IPythonScriptPlugin* Python, FPythonCommandEx& Command, const FString* SourceToValidate)
	{
		FExecutionResult Result;
		if (!IsInGameThread())
		{
			Result.Error = TEXT("Python 只能在 Unreal Game Thread 执行。");
			return Result;
		}
		if (IsGarbageCollecting())
		{
			Result.Error = TEXT("Unreal 正在执行垃圾回收，请稍后重试 Python 请求。");
			return Result;
		}
		if (bExecutionActive)
		{
			Result.Error = TEXT("已有 Python 请求正在执行，已拒绝重入调用。");
			return Result;
		}
		if (Python == nullptr || !Python->IsPythonAvailable() || !Python->IsPythonInitialized())
		{
			Result.Error = TEXT("PythonScriptPlugin 尚未完成初始化或当前不可用。");
			return Result;
		}
		if (SourceToValidate != nullptr && !ValidateKnownNativeCrashCalls(*SourceToValidate, Result.Error))
		{
			return Result;
		}

		// MCP 请求不得弹出模态窗口；这与 Unreal 自带的 EditorPythonExecuter
		// 和 Python Commandlet 执行方式保持一致。
		Command.Flags |= EPythonCommandFlags::Unattended;
		TGuardValue<bool> ActiveGuard(bExecutionActive, true);
		Result.bDispatched = true;
		const double StartSeconds = FPlatformTime::Seconds();
		Result.bSucceeded = Python->ExecPythonCommandEx(Command);
		Result.DurationMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}
}
