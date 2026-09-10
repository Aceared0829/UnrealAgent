// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPythonExecutionAudit.cpp
 * @brief Python 调用审计的线程安全内存实现。
 */

#include "Application/Telemetry/UnrealAgentMCPPythonExecutionAudit.h"

#include "HAL/CriticalSection.h"
#include "Hash/Blake3.h"
#include "Misc/ScopeLock.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FCriticalSection AuditMutex;
		TArray<FPythonExecutionAuditRecord> AuditRecords;
		constexpr int32 MaximumAuditRecords = 200;
	}

	void FUnrealAgentMCPPythonExecutionAudit::Record(const FString& TaskSummary, const FString& Code, const bool bSucceeded)
	{
		FScopeLock Lock(&AuditMutex);
		FPythonExecutionAuditRecord Record;
		Record.TimestampUtc = FDateTime::UtcNow();
		Record.TaskSummary = TaskSummary.IsEmpty() ? TEXT("python-execution") : TaskSummary.Left(240);
		const FTCHARToUTF8 Utf8(*Code);
		Record.CodeFingerprint = TEXT("blake3:") + LexToString(FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length()))).ToLower();
		Record.CodeCharacterCount = Code.Len();
		Record.bSucceeded = bSucceeded;
		AuditRecords.Add(MoveTemp(Record));
		if (AuditRecords.Num() > MaximumAuditRecords)
		{
			AuditRecords.RemoveAt(0, AuditRecords.Num() - MaximumAuditRecords, EAllowShrinking::No);
		}
	}

	TArray<FPythonExecutionAuditRecord> FUnrealAgentMCPPythonExecutionAudit::Snapshot()
	{
		FScopeLock Lock(&AuditMutex);
		return AuditRecords;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void FUnrealAgentMCPPythonExecutionAudit::ResetForTests()
	{
		FScopeLock Lock(&AuditMutex);
		AuditRecords.Reset();
	}
#endif
}
