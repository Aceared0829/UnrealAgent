// Copyright ZhaoZining. All Rights Reserved.

#include "Infrastructure/Audit/UnrealAgentMCPJsonlExecutionLedger.h"

#include "HAL/FileManager.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPExecutionLedgerTest, "WorldData.UnrealAgent.Infrastructure.ExecutionLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPExecutionLedgerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("Automation"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString LedgerPath = FPaths::Combine(TestDirectory, TEXT("execution-ledger.jsonl"));

	TSharedPtr<UnrealAgentMCP::Execution::IMcpExecutionLedger, ESPMode::ThreadSafe> Ledger = UnrealAgentMCP::Audit::CreateJsonlExecutionLedger(LedgerPath);
	FString Error;
	TestTrue(TEXT("New ledger is healthy"), Ledger->IsHealthy(Error));
	TestTrue(TEXT("New ledger error is empty"), Error.IsEmpty());

	UnrealAgentMCP::Policy::FMcpAuditRecord AuditRecord;
	AuditRecord.TraceId = FGuid::NewGuid();
	AuditRecord.RequestId = FGuid::NewGuid();
	AuditRecord.Timestamp = FDateTime::UtcNow();
	AuditRecord.ClientId = TEXT("automation");
	AuditRecord.SessionId = TEXT("session");
	AuditRecord.Source = TEXT("Automation");
	AuditRecord.Provider = TEXT("codex");
	AuditRecord.ToolName = TEXT("test.read");
	AuditRecord.Risk = UnrealAgentMCP::EMcpToolRisk::ReadOnly;
	AuditRecord.Outcome = UnrealAgentMCP::Policy::EMcpPolicyOutcome::Allowed;
	AuditRecord.DecisionCode = TEXT("allowed");
	AuditRecord.SchemaErrorPaths = { TEXT("$.spacing"), TEXT("$.countX") };
	AuditRecord.bAccepted = true;
	TestTrue(TEXT("Audit record appends"), Ledger->AppendAuditRecord(AuditRecord, Error));

	UnrealAgentMCP::Execution::FMcpTaskSnapshot Snapshot;
	Snapshot.Id = FGuid::NewGuid();
	Snapshot.TraceId = AuditRecord.TraceId;
	Snapshot.RequestId = AuditRecord.RequestId;
	Snapshot.ProtocolRequestId = TEXT("rpc-1");
	Snapshot.ClientId = TEXT("automation");
	Snapshot.SessionId = TEXT("session");
	Snapshot.Source = TEXT("Automation");
	Snapshot.Provider = TEXT("cursor");
	Snapshot.ToolName = TEXT("test.read");
	Snapshot.Owner = TEXT("test");
	Snapshot.State = UnrealAgentMCP::Execution::EMcpTaskState::Completed;
	Snapshot.CreatedAt = FDateTime::UtcNow();
	Snapshot.CompletedAt = Snapshot.CreatedAt;
	Snapshot.ValueJson = TEXT("SECRET_RESULT_MUST_NOT_BE_PERSISTED");
	Snapshot.Error = TEXT("SECRET_ERROR_MUST_NOT_BE_PERSISTED");
	TestTrue(TEXT("Task snapshot appends"), Ledger->AppendTaskSnapshot(Snapshot, Error));

	FString Content;
	TestTrue(TEXT("Ledger can be read"), FFileHelper::LoadFileToString(Content, *LedgerPath));
	TestFalse(TEXT("Result payload is redacted"), Content.Contains(TEXT("SECRET_RESULT_MUST_NOT_BE_PERSISTED")));
	TestFalse(TEXT("Error payload is redacted"), Content.Contains(TEXT("SECRET_ERROR_MUST_NOT_BE_PERSISTED")));
	TestTrue(TEXT("Trace identifier is persisted"), Content.Contains(AuditRecord.TraceId.ToString(EGuidFormats::DigitsWithHyphensLower)));
	TestTrue(TEXT("Audit provider and Schema 字段路径会持久化"),
		Content.Contains(TEXT("\"provider\":\"codex\"")) && Content.Contains(TEXT("\"schemaErrorPaths\":[\"$.spacing\",\"$.countX\"]")));
	TestTrue(TEXT("Task provider 会持久化"), Content.Contains(TEXT("\"provider\":\"cursor\"")));
	TestFalse(TEXT("Ledger 不记录原始 arguments"), Content.Contains(TEXT("SECRET_ARGUMENT_VALUE_MUST_NOT_BE_PERSISTED")));
	TestTrue(TEXT("Ledger ACL is owner-only"), UnrealAgentMCP::ServerEnvironment::IsFileAccessRestrictedToCurrentUser(LedgerPath, Error));

	AddExpectedError(TEXT("Execution ledger is already owned by another Editor instance."), EAutomationExpectedErrorFlags::Exact, 1);
	TSharedPtr<UnrealAgentMCP::Execution::IMcpExecutionLedger, ESPMode::ThreadSafe> CompetingLedger = UnrealAgentMCP::Audit::CreateJsonlExecutionLedger(LedgerPath);
	TestFalse(TEXT("并发的第二个账本写入者被拒绝"), CompetingLedger->IsHealthy(Error));
	CompetingLedger.Reset();
	Ledger.Reset();
	Ledger = UnrealAgentMCP::Audit::CreateJsonlExecutionLedger(LedgerPath);
	TestTrue(TEXT("Valid ledger survives restart"), Ledger->IsHealthy(Error));

	Ledger.Reset();
	Content.ReplaceInline(TEXT("\"eventHash\":\""), TEXT("\"eventHash\":\"0"), ESearchCase::CaseSensitive);
	TestTrue(TEXT("Tampered ledger can be written to isolated test file"), FFileHelper::SaveStringToFile(Content, *LedgerPath));
	AddExpectedError(TEXT("Execution ledger event hash validation failed."), EAutomationExpectedErrorFlags::Exact, 1);
	Ledger = UnrealAgentMCP::Audit::CreateJsonlExecutionLedger(LedgerPath);
	TestFalse(TEXT("Tampered ledger is rejected"), Ledger->IsHealthy(Error));
	TestTrue(TEXT("Tamper error is reported"), !Error.IsEmpty());

	IFileManager::Get().Delete(*LedgerPath, false, true, true);
	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

#endif
