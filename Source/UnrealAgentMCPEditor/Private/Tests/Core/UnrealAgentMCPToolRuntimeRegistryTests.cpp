// Copyright ZhaoZining. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Dom/JsonObject.h"

#include <atomic>

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		TSharedRef<FJsonObject> MakeObjectSchema()
		{
			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			Schema->SetBoolField(TEXT("additionalProperties"), false);
			return Schema;
		}

		FMcpToolDescriptor MakeDescriptor(const FName ProviderName, const FString& ToolName)
		{
			FMcpToolDescriptor Descriptor;
			Descriptor.Provider = ProviderName;
			Descriptor.Toolset = TEXT("Tests");
			Descriptor.Name = ToolName;
			Descriptor.QualifiedName = ProviderName.ToString() + TEXT(".") + ToolName;
			Descriptor.Description = TEXT("统一 Provider 测试工具。");
			Descriptor.InputSchema = MakeObjectSchema();
			Descriptor.OutputSchema = MakeObjectSchema();
			Descriptor.Risk = EMcpToolRisk::ReadOnly;
			Descriptor.ThreadPolicy = EMcpToolThreadPolicy::Inline;
			Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::ReadOnly;
			Descriptor.bReadOnly = true;
			Descriptor.bIdempotent = true;
			Descriptor.Invoker = [ToolName](const TSharedPtr<FJsonObject>&)
			{
				return FString::Printf(TEXT("{\"provider\":true,\"tool\":\"%s\"}"), *ToolName);
			};
			return Descriptor;
		}

		class FRegistryTestProvider final : public IMcpToolProvider
		{
		public:
			explicit FRegistryTestProvider(FName InProviderName = TEXT("Tests.Provider"), FString InToolName = TEXT("provider_tool"),
				uint32 InApiVersion = McpToolProviderApiVersion, bool bInDuplicateTool = false, bool bInInvalidTool = false)
				: ProviderName(InProviderName), ToolName(MoveTemp(InToolName)), ApiVersion(InApiVersion), bDuplicateTool(bInDuplicateTool), bInvalidTool(bInInvalidTool)
			{
			}

			FName GetProviderName() const override
			{
				return ProviderName;
			}

			uint32 GetProviderApiVersion() const override
			{
				return ApiVersion;
			}

			void EnumerateTools(TArray<FMcpToolDescriptor>& OutTools, TArray<FString>& OutErrors) override
			{
				FMcpToolDescriptor Descriptor = MakeDescriptor(GetProviderName(), ToolName);
				if (bInvalidTool)
				{
					Descriptor.OutputSchema.Reset();
				}
				OutTools.Add(MoveTemp(Descriptor));
				if (bDuplicateTool)
				{
					OutTools.Add(MakeDescriptor(GetProviderName(), ToolName));
				}
			}

		private:
			FName ProviderName;
			FString ToolName;
			uint32 ApiVersion = McpToolProviderApiVersion;
			bool bDuplicateTool = false;
			bool bInvalidTool = false;
		};
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolRuntimeRegistryLifecycleTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.Lifecycle",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolRuntimeRegistryLifecycleTest::RunTest(const FString& Parameters)
	{
		FMcpToolRuntimeRegistry Registry;
		int32 ChangeNotificationCount = 0;
		uint64 LastNotifiedGeneration = 0;
		bool bPublishedBeforeNotification = true;
		const FDelegateHandle ChangeHandle = Registry.OnChanged().AddLambda(
			[&](const FMcpToolRegistrySnapshotRef Snapshot)
			{
				++ChangeNotificationCount;
				LastNotifiedGeneration = Snapshot->Generation;
				bPublishedBeforeNotification = bPublishedBeforeNotification && Registry.GetSnapshot()->Generation == Snapshot->Generation;
			});
		const FMcpToolRegistrySnapshotRef EmptySnapshot = Registry.GetSnapshot();
		TestEqual(TEXT("空 Registry 代次为零"), EmptySnapshot->Generation, uint64(0));
		TestTrue(TEXT("空 Registry 也具有确定性 Hash"), EmptySnapshot->CatalogHash.StartsWith(TEXT("blake3:")));
		TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
		Definition->SetStringField(TEXT("name"), TEXT("captured_tool"));
		Definition->SetStringField(TEXT("description"), TEXT("Runtime registry test."));
		TSharedRef<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		Definition->SetObjectField(TEXT("inputSchema"), InputSchema);

		int32 InvocationCount = 0;
		FString Error;
		TestTrue(TEXT("完整 Definition 与动态 Handler 可以原子注册"),
			Registry.RegisterTool(
				TEXT("Tests.Owner"), Definition,
				[&InvocationCount](const TSharedPtr<FJsonObject>&)
				{
					++InvocationCount;
					return FString(TEXT("{\"ok\":true}"));
				},
				Error));
		TestEqual(TEXT("注册中心工具数量"), Registry.Num(), 1);
		TestEqual(TEXT("注册后代次递增"), Registry.GetSnapshot()->Generation, uint64(1));
		TestEqual(TEXT("成功注册只广播一次"), ChangeNotificationCount, 1);
		TestEqual(TEXT("注册通知携带新代次"), LastNotifiedGeneration, uint64(1));
		TestTrue(TEXT("通知前新快照已经发布"), bPublishedBeforeNotification);
		const TArray<FString> RegisteredNames = Registry.GetRegisteredToolNames();
		TestEqual(TEXT("目录名称数量"), RegisteredNames.Num(), 1);
		if (RegisteredNames.Num() == 1)
		{
			TestEqual(TEXT("目录与调用共享相同名称快照"), RegisteredNames[0], FString(TEXT("captured_tool")));
		}
		TestTrue(TEXT("运行时目录由注册项生成"), Registry.GetToolDefinitionsJson().Contains(TEXT("\"captured_tool\"")));

		FString Result;
		TestTrue(TEXT("动态 Handler 可分发"), Registry.TryDispatch(TEXT("captured_tool"), MakeShared<FJsonObject>(), Result));
		TestEqual(TEXT("Handler 只调用一次"), InvocationCount, 1);
		TestTrue(TEXT("Handler 结果返回"), Result.Contains(TEXT("\"ok\":true")));

		FString DuplicateError;
		TestFalse(TEXT("重复名称被拒绝"),
			Registry.RegisterTool(
				TEXT("Tests.OtherOwner"), Definition,
				[](const TSharedPtr<FJsonObject>&)
				{
					return FString();
				},
				DuplicateError));
		TestEqual(TEXT("冲突注册不改变目录代次"), Registry.GetSnapshot()->Generation, uint64(1));
		TestTrue(TEXT("重复错误包含工具名"), DuplicateError.Contains(TEXT("captured_tool")));
		TestEqual(TEXT("失败注册不会广播变更"), ChangeNotificationCount, 1);

		TestEqual(TEXT("按 Owner 卸载一个工具"), Registry.UnregisterOwner(TEXT("Tests.Owner")), 1);
		TestEqual(TEXT("卸载后注册中心为空"), Registry.Num(), 0);
		TestEqual(TEXT("卸载后代次递增"), Registry.GetSnapshot()->Generation, uint64(2));
		TestEqual(TEXT("成功卸载广播一次"), ChangeNotificationCount, 2);
		TestEqual(TEXT("卸载通知携带新代次"), LastNotifiedGeneration, uint64(2));
		TestEqual(TEXT("空卸载不会广播"), Registry.UnregisterOwner(TEXT("Tests.Missing")), 0);
		TestEqual(TEXT("空卸载后通知数量不变"), ChangeNotificationCount, 2);
		TestFalse(TEXT("卸载后不可调用"), Registry.TryDispatch(TEXT("captured_tool"), MakeShared<FJsonObject>(), Result));
		Registry.OnChanged().Remove(ChangeHandle);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolRegistrySnapshotTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.SnapshotDeterminism",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolRegistrySnapshotTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FMcpToolRuntimeRegistry First;
		FMcpToolRuntimeRegistry Second;
		FString Error;
		TestTrue(TEXT("第一目录注册 beta"), First.RegisterDescriptor(MakeDescriptor(TEXT("Tests.B"), TEXT("beta")), Error));
		TestTrue(TEXT("第一目录注册 alpha"), First.RegisterDescriptor(MakeDescriptor(TEXT("Tests.A"), TEXT("alpha")), Error));
		TestTrue(TEXT("第二目录注册 alpha"), Second.RegisterDescriptor(MakeDescriptor(TEXT("Tests.A"), TEXT("alpha")), Error));
		TestTrue(TEXT("第二目录注册 beta"), Second.RegisterDescriptor(MakeDescriptor(TEXT("Tests.B"), TEXT("beta")), Error));

		const FMcpToolRegistrySnapshotRef FirstSnapshot = First.GetSnapshot();
		const FMcpToolRegistrySnapshotRef SecondSnapshot = Second.GetSnapshot();
		TestEqual(TEXT("注册顺序不影响规范化目录 JSON"), FirstSnapshot->ToolDefinitionsJson, SecondSnapshot->ToolDefinitionsJson);
		TestEqual(TEXT("注册顺序不影响 Catalog Hash"), FirstSnapshot->CatalogHash, SecondSnapshot->CatalogHash);
		TestEqual(TEXT("Provider 索引完整"), FirstSnapshot->ByProvider.Num(), 2);
		TestEqual(TEXT("Toolset 索引完整"), FirstSnapshot->ByToolset.Num(), 1);

		FMcpToolDescriptor OldDescriptor;
		TestTrue(TEXT("读取旧调用代次"), First.TryGetDescriptor(TEXT("alpha"), OldDescriptor));
		TestEqual(TEXT("卸载旧 Provider"), First.UnregisterOwner(TEXT("Tests.A")), 1);
		TestTrue(TEXT("重新注册同名工具"), First.RegisterDescriptor(MakeDescriptor(TEXT("Tests.A"), TEXT("alpha")), Error));
		FString Result;
		TestFalse(TEXT("旧代次调用稳定返回 unavailable"), First.TryDispatch(TEXT("alpha"), MakeShared<FJsonObject>(), Result, OldDescriptor.RegistrationGeneration));
		TestTrue(TEXT("当前代次仍可调用"), First.TryDispatch(TEXT("alpha"), MakeShared<FJsonObject>(), Result));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolRegistryAtomicFailureTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.AtomicFailure",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolRegistryAtomicFailureTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FMcpToolRuntimeRegistry Registry;
		TArray<FString> Errors;
		TestFalse(TEXT("Provider 内重复工具整批失败"),
			Registry.RegisterProvider(MakeShared<FRegistryTestProvider>(TEXT("Tests.Duplicate"), TEXT("duplicate"), McpToolProviderApiVersion, true), Errors));
		TestEqual(TEXT("重复失败不留下工具"), Registry.Num(), 0);
		TestEqual(TEXT("重复失败不改变代次"), Registry.GetSnapshot()->Generation, uint64(0));

		Errors.Reset();
		TestFalse(TEXT("非法 Descriptor 整批失败"),
			Registry.RegisterProvider(MakeShared<FRegistryTestProvider>(TEXT("Tests.Invalid"), TEXT("invalid"), McpToolProviderApiVersion, false, true), Errors));
		TestEqual(TEXT("非法失败不留下工具"), Registry.Num(), 0);

		Errors.Reset();
		TestFalse(TEXT("不兼容 SDK 主版本被拒绝"),
			Registry.RegisterProvider(MakeShared<FRegistryTestProvider>(TEXT("Tests.Future"), TEXT("future"), McpToolProviderApiVersion + 1), Errors));
		TestEqual(TEXT("版本失败不改变代次"), Registry.GetSnapshot()->Generation, uint64(0));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolRegistryEnduranceTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.ProviderEndurance",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolRegistryEnduranceTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FMcpToolRuntimeRegistry Registry;
		const TSharedRef<FRegistryTestProvider> Provider = MakeShared<FRegistryTestProvider>();
		std::atomic<bool> bStarted(false);
		std::atomic<bool> bFinished(false);
		std::atomic<int32> FailureCount(0);
		FString StableRegisteredHash;

		TFuture<void> Writer = Async(EAsyncExecution::ThreadPool,
			[&]()
			{
				while (!bStarted.load())
				{
					FPlatformProcess::SleepNoStats(0.0f);
				}
				for (int32 Index = 0; Index < 1000; ++Index)
				{
					TArray<FString> Errors;
					if (!Registry.RegisterProvider(Provider, Errors))
					{
						++FailureCount;
						continue;
					}
					const FMcpToolRegistrySnapshotRef Snapshot = Registry.GetSnapshot();
					if (Snapshot->Tools.Num() != 1 || !Snapshot->ByName.Contains(TEXT("provider_tool")))
					{
						++FailureCount;
					}
					if (StableRegisteredHash.IsEmpty())
					{
						StableRegisteredHash = Snapshot->CatalogHash;
					}
					else if (StableRegisteredHash != Snapshot->CatalogHash)
					{
						++FailureCount;
					}
					FString Result;
					if (!Registry.TryDispatch(TEXT("provider_tool"), MakeShared<FJsonObject>(), Result))
					{
						++FailureCount;
					}
					if (Registry.UnregisterOwner(TEXT("Tests.Provider")) != 1 || Registry.TryDispatch(TEXT("provider_tool"), MakeShared<FJsonObject>(), Result))
					{
						++FailureCount;
					}
				}
				bFinished.store(true);
			});

		bStarted.store(true);
		while (!bFinished.load())
		{
			const FMcpToolRegistrySnapshotRef Snapshot = Registry.GetSnapshot();
			if (Snapshot->Tools.Num() > 1 || !Snapshot->CatalogHash.StartsWith(TEXT("blake3:")))
			{
				++FailureCount;
			}
			FPlatformProcess::SleepNoStats(0.0f);
		}
		Writer.Wait();
		TestEqual(TEXT("1000 次并发 register/list/find/call/unregister 无异常"), FailureCount.load(), 0);
		TestEqual(TEXT("耐久结束后无悬空工具"), Registry.Num(), 0);
		TestEqual(TEXT("每次注册和卸载均推进代次"), Registry.GetSnapshot()->Generation, uint64(2000));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPUnifiedToolProviderTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.UnifiedProvider",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPUnifiedToolProviderTest::RunTest(const FString& Parameters)
	{
		FMcpToolRuntimeRegistry Registry;
		TArray<FString> Errors;
		TestTrue(TEXT("Provider 工具可原子注册"), Registry.RegisterProvider(MakeShared<FRegistryTestProvider>(), Errors));
		TestTrue(TEXT("Provider 注册无错误"), Errors.IsEmpty());
		TestEqual(TEXT("Provider 工具数量"), Registry.Num(), 1);

		FMcpToolDescriptor Descriptor;
		TestTrue(TEXT("Provider 描述符可查询"), Registry.TryGetDescriptor(TEXT("provider_tool"), Descriptor));
		TestEqual(TEXT("风险等级保持"), Descriptor.Risk, EMcpToolRisk::ReadOnly);
		TestEqual(TEXT("线程策略保持"), Descriptor.ThreadPolicy, EMcpToolThreadPolicy::Inline);
		TestTrue(TEXT("输出 Schema 为显式契约"), Descriptor.bOutputSchemaExplicit);

		const FString DefinitionsJson = Registry.GetToolDefinitionsJson();
		TestTrue(TEXT("目录包含 outputSchema"), DefinitionsJson.Contains(TEXT("\"outputSchema\"")));
		TestTrue(TEXT("目录包含统一契约元数据"), DefinitionsJson.Contains(TEXT("\"threadPolicy\"")));

		FString Result;
		TestTrue(TEXT("Provider 工具通过统一 Registry 调用"), Registry.TryDispatch(TEXT("provider_tool"), MakeShared<FJsonObject>(), Result));
		TestTrue(TEXT("Provider 调用结果正确"), Result.Contains(TEXT("\"provider\":true")));

		TArray<FString> DuplicateErrors;
		TestFalse(TEXT("重复 Provider 注册整体失败"), Registry.RegisterProvider(MakeShared<FRegistryTestProvider>(), DuplicateErrors));
		TestEqual(TEXT("失败后原 Registry 数量不变"), Registry.Num(), 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolRegistryLookupDurabilityTest, "WorldData.UnrealAgent.Core.ToolRuntimeRegistry.Lookup10000",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolRegistryLookupDurabilityTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FMcpToolRuntimeRegistry Registry;
		FString Error;
		TestTrue(TEXT("查找耐久测试工具注册成功"), Registry.RegisterDescriptor(MakeDescriptor(TEXT("Tests.Lookup"), TEXT("lookup_target")), Error));
		int32 Failures = 0;
		const double StartedAt = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < 10000; ++Index)
		{
			FMcpToolDescriptor Descriptor;
			if (!Registry.TryGetDescriptor(TEXT("lookup_target"), Descriptor) || Descriptor.Name != TEXT("lookup_target"))
			{
				++Failures;
			}
		}
		const double DurationMilliseconds = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
		TestEqual(TEXT("10000 次 Registry Find 全部命中"), Failures, 0);
		TestTrue(TEXT("10000 次 Registry Find 不超过 2000ms"), DurationMilliseconds <= 2000.0);
		return true;
	}
}

#endif
