// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEditorTransactionTests.cpp
 * @brief 真实 UnrealEd 事务加入、失败保留与 Atomic 回滚测试。
 */

#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Tests/Adapters/UnrealAgentMCPAssetTestTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEditorTransactionTest, "WorldData.UnrealAgent.Core.Execution.EditorTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPEditorTransactionTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	using namespace UnrealAgentMCP::Transactions;
	(void)Parameters;

	if (!GEditor || !GEditor->CanTransact())
	{
		AddError(TEXT("自动化测试运行时 UE Editor 事务系统不可用。"));
		return false;
	}

	UUnrealAgentMCPAssetTestData* TestObject = NewObject<UUnrealAgentMCPAssetTestData>(GetTransientPackage(), NAME_None, RF_Transactional);
	TestNotNull(TEXT("创建临时事务对象"), TestObject);
	if (!TestObject)
	{
		return false;
	}

	const TSharedRef<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> Coordinator = CreateEditorTransactionCoordinator();
	FString Error;
	TUniquePtr<IMcpToolTransactionScope> AtomicScope = Coordinator->Begin(TEXT("worlddata.tests.atomic"), EMcpToolTransactionPolicy::Atomic, Error);
	TestTrue(TEXT("Atomic 事务创建成功"), AtomicScope.IsValid() && Error.IsEmpty());
	if (!AtomicScope.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Atomic 已建立录制中的 UE 事务"), GEditor->Trans->IsActive());

	TestTrue(TEXT("Atomic 对象进入事务"), TestObject->Modify());
	TestObject->bEnabled = true;
	TestTrue(TEXT("Atomic 失败调用完成安全回滚"), AtomicScope->Finalize(false, Error));
	TestTrue(TEXT("Atomic 回滚没有返回错误"), Error.IsEmpty());
	TestFalse(TEXT("Atomic 失败恢复 UObject 修改前状态"), TestObject->bEnabled);
	TestFalse(TEXT("Atomic 回滚后不再保留活动事务"), GEditor->Trans->IsActive());

	TUniquePtr<IMcpToolTransactionScope> ScopedScope = Coordinator->Begin(TEXT("worlddata.tests.scoped"), EMcpToolTransactionPolicy::ScopedTransaction, Error);
	TestTrue(TEXT("Scoped 事务创建成功"), ScopedScope.IsValid() && Error.IsEmpty());
	if (!ScopedScope.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Scoped 已建立录制中的 UE 事务"), GEditor->Trans->IsActive());
	TUniquePtr<IMcpToolTransactionScope> JoinedScope = Coordinator->Begin(TEXT("worlddata.tests.joined"), EMcpToolTransactionPolicy::ScopedTransaction, Error);
	TestTrue(TEXT("Scoped 协调器可加入已有事务"), JoinedScope.IsValid() && Error.IsEmpty());
	if (JoinedScope.IsValid())
	{
		TestTrue(TEXT("加入型事务可独立完成而不结束外层事务"), JoinedScope->Finalize(true, Error));
		TestTrue(TEXT("加入型事务完成后外层事务仍在录制"), GEditor->Trans->IsActive());
	}
	{
		FUnrealAgentMCPScopedEditorTransaction JoinedTransaction(FText::FromString(TEXT("不应创建的嵌套事务")));
		TestFalse(TEXT("本地事务加入中央事务而不嵌套创建"), JoinedTransaction.OwnsTransaction());
		TestTrue(TEXT("Scoped 对象进入事务"), TestObject->Modify());
		TestObject->Label = TEXT("已修改");
	}
	TestTrue(TEXT("Scoped 工具失败仍保留可 Undo 记录"), ScopedScope->Finalize(false, Error));
	TestEqual(TEXT("Scoped 失败不会静默丢弃已发生修改"), TestObject->Label, FString(TEXT("已修改")));
	TestTrue(TEXT("用户可撤销 Scoped 失败留下的修改"), GEditor->UndoTransaction(false));
	TestTrue(TEXT("Scoped Undo 恢复修改前状态"), TestObject->Label.IsEmpty());
	return true;
}
