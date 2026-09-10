// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPythonExecutionAudit.h
 * @brief 记录当前编辑器会话中的受控 Python 调用，供工具重叠度审计使用。
 */

#include "CoreMinimal.h"

namespace UnrealAgentMCP
{
	struct FPythonExecutionAuditRecord
	{
		FDateTime TimestampUtc;
		FString TaskSummary;
		FString CodeFingerprint;
		int32 CodeCharacterCount = 0;
		bool bSucceeded = false;
	};

	class FUnrealAgentMCPPythonExecutionAudit
	{
	public:
		static void Record(const FString& TaskSummary, const FString& Code, bool bSucceeded);

		static TArray<FPythonExecutionAuditRecord> Snapshot();

#if WITH_DEV_AUTOMATION_TESTS
		static void ResetForTests();
#endif
	};
}
