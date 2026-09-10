// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPProtocolTests.cpp
 * @brief Unreal Agent 纯协议、错误响应与工具目录契约测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include "Application/UnrealAgentMCPToolExtensions.h"
#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Core/Protocol/UnrealAgentMCPSessionRegistry.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <atomic>

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Object))
			{
				Object.Reset();
			}
			return Object;
		}

		const FJsonObject* GetRequiredObjectField(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Owner, const TCHAR* FieldName)
		{
			if (!Owner.IsValid())
			{
				Test.AddError(TEXT("响应对象无效。"));
				return nullptr;
			}

			const TSharedPtr<FJsonObject>* Value = nullptr;
			if (!Owner->TryGetObjectField(FieldName, Value) || Value == nullptr || !Value->IsValid())
			{
				Test.AddError(FString::Printf(TEXT("响应缺少对象字段：%s"), FieldName));
				return nullptr;
			}
			return Value->Get();
		}

		class FListChangedTestProvider final : public IMcpToolProvider
		{
		public:
			virtual FName GetProviderName() const override
			{
				return TEXT("Tests.ListChanged");
			}

			virtual void EnumerateTools(TArray<FMcpToolDescriptor>& OutTools, TArray<FString>& OutErrors) override
			{
				(void)OutErrors;
				auto MakeObjectSchema = []()
				{
					TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
					Schema->SetStringField(TEXT("type"), TEXT("object"));
					Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
					Schema->SetBoolField(TEXT("additionalProperties"), false);
					return Schema;
				};

				FMcpToolDescriptor Descriptor;
				Descriptor.Provider = GetProviderName();
				Descriptor.Toolset = TEXT("Tests.ListChanged");
				Descriptor.Name = TEXT("tests_list_changed_probe");
				Descriptor.QualifiedName = TEXT("Tests.ListChanged.probe");
				Descriptor.Description = TEXT("Temporary provider used by the list-changed integration test.");
				Descriptor.InputSchema = MakeObjectSchema();
				Descriptor.OutputSchema = MakeObjectSchema();
				Descriptor.Risk = EMcpToolRisk::ReadOnly;
				Descriptor.ThreadPolicy = EMcpToolThreadPolicy::Inline;
				Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::ReadOnly;
				Descriptor.bReadOnly = true;
				Descriptor.bIdempotent = true;
				Descriptor.Invoker = [](const TSharedPtr<FJsonObject>&)
				{
					return FString(TEXT("{\"success\":true}"));
				};
				OutTools.Add(MoveTemp(Descriptor));
			}
		};
	}

	class FVerifyListChangedBroadcastDrainedCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FVerifyListChangedBroadcastDrainedCommand(FAutomationTestBase* InTest) : Test(InTest), Deadline(FPlatformTime::Seconds() + 5.0)
		{
		}

		virtual bool Update() override
		{
			const TSharedPtr<FJsonObject> Status = ParseJsonObject(FUnrealAgentMCPServer::GetStatusJson());
			const bool bStillScheduled = Status.IsValid() && Status->GetBoolField(TEXT("toolsListChangedBroadcastScheduled"));
			if (bStillScheduled && FPlatformTime::Seconds() < Deadline)
			{
				return false;
			}
			Test->TestTrue(TEXT("工具目录变化广播在下一 Tick 被消费且不会重复驻留"), Status.IsValid() && !bStillScheduled);
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double Deadline = 0.0;
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPProtocolVersionTest, "WorldData.UnrealAgent.Protocol.VersionNegotiation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPProtocolVersionTest::RunTest(const FString& Parameters)
	{
		const TArray<FString>& SupportedVersions = Protocol::GetSupportedProtocolVersions();
		TestEqual(TEXT("协议版本数量"), SupportedVersions.Num(), 3);
		TestEqual(TEXT("最新协议版本"), Protocol::GetLatestProtocolVersion(), FString(TEXT("2025-06-18")));
		TestEqual(TEXT("受支持版本原样返回"), Protocol::NegotiateProtocolVersion(TEXT("2025-03-26")), FString(TEXT("2025-03-26")));
		TestEqual(TEXT("未知版本回退到最新版本"), Protocol::NegotiateProtocolVersion(TEXT("2099-01-01")), Protocol::GetLatestProtocolVersion());
		TestEqual(TEXT("空版本回退到最新版本"), Protocol::NegotiateProtocolVersion(FString()), Protocol::GetLatestProtocolVersion());

		TSharedRef<FJsonObject> InitializeParams = MakeShared<FJsonObject>();
		InitializeParams->SetStringField(TEXT("protocolVersion"), TEXT("2025-03-26"));
		TSharedRef<FJsonObject> InitializeRequest = MakeShared<FJsonObject>();
		InitializeRequest->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		InitializeRequest->SetNumberField(TEXT("id"), 1);
		InitializeRequest->SetStringField(TEXT("method"), TEXT("initialize"));
		InitializeRequest->SetObjectField(TEXT("params"), InitializeParams);

		const TSharedPtr<FJsonObject> Response = FUnrealAgentMCPServer::DispatchJsonRpcRequest(InitializeRequest);
		const FJsonObject* Result = GetRequiredObjectField(*this, Response, TEXT("result"));
		if (Result != nullptr)
		{
			TestEqual(TEXT("initialize 回显已协商版本"), Result->GetStringField(TEXT("protocolVersion")), FString(TEXT("2025-03-26")));
			TestEqual(TEXT("initialize 声明全部支持版本"), Result->GetArrayField(TEXT("supportedProtocolVersions")).Num(), SupportedVersions.Num());
			const TSharedPtr<FJsonObject>* ServerInfo = nullptr;
			const bool bHasServerInfo = Result->TryGetObjectField(TEXT("serverInfo"), ServerInfo) && ServerInfo != nullptr && ServerInfo->IsValid();
			TestTrue(TEXT("initialize 返回 MCP 服务信息"), bHasServerInfo);
			if (bHasServerInfo)
			{
				TestTrue(TEXT("MCP 服务标题使用 Unreal Agent 品牌"), (*ServerInfo)->GetStringField(TEXT("title")).StartsWith(Brand::ProductName));
				TestTrue(TEXT("兼容服务器名称继续保持无空格的项目唯一标识"), !(*ServerInfo)->GetStringField(TEXT("name")).Contains(TEXT(" ")));
			}
			const TSharedPtr<FJsonObject>* Capabilities = nullptr;
			const TSharedPtr<FJsonObject>* ToolsCapability = nullptr;
			TestTrue(TEXT("initialize 声明工具目录变化通知能力"),
				Result->TryGetObjectField(TEXT("capabilities"), Capabilities) && Capabilities && Capabilities->IsValid() &&
					(*Capabilities)->TryGetObjectField(TEXT("tools"), ToolsCapability) && ToolsCapability && ToolsCapability->IsValid() &&
					(*ToolsCapability)->GetBoolField(TEXT("listChanged")));
		}

		const TSharedRef<FJsonObject> Notification = Protocol::MakeJsonRpcNotification(TEXT("notifications/tools/list_changed"));
		TestEqual(TEXT("目录变化通知使用标准 JSON-RPC 方法"), Notification->GetStringField(TEXT("method")), FString(TEXT("notifications/tools/list_changed")));
		TestFalse(TEXT("JSON-RPC 通知不携带 id"), Notification->HasField(TEXT("id")));
		TestTrue(TEXT("JSON-RPC 通知携带空 params 对象"), Notification->HasField(TEXT("params")));

		const Extensions::FMcpToolCatalogState CatalogBefore = Extensions::GetToolCatalogState();
		TArray<FString> RegistrationErrors;
		TestTrue(TEXT("测试 Provider 能够原子注册到实时目录"), Extensions::RegisterToolProvider(MakeShared<FListChangedTestProvider>(), RegistrationErrors));
		for (const FString& Error : RegistrationErrors)
		{
			AddError(Error);
		}
		const Extensions::FMcpToolCatalogState CatalogRegistered = Extensions::GetToolCatalogState();
		TestTrue(TEXT("Provider 注册发布新的目录代次"), CatalogRegistered.Generation > CatalogBefore.Generation);
		TestEqual(TEXT("测试 Provider 注销一个工具"), Extensions::UnregisterToolProvider(TEXT("Tests.ListChanged")), 1);
		const TSharedPtr<FJsonObject> ScheduledStatus = ParseJsonObject(FUnrealAgentMCPServer::GetStatusJson());
		TestTrue(TEXT("真实注册表变化只安排广播而不在当前调用栈写 SSE"), ScheduledStatus.IsValid() && ScheduledStatus->GetBoolField(TEXT("toolsListChangedBroadcastScheduled")));
		ADD_LATENT_AUTOMATION_COMMAND(FVerifyListChangedBroadcastDrainedCommand(this));

		// 恢复默认协商状态，避免测试影响同进程内后续连接信息查询。
		InitializeParams->SetStringField(TEXT("protocolVersion"), Protocol::GetLatestProtocolVersion());
		FUnrealAgentMCPServer::DispatchJsonRpcRequest(InitializeRequest);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPProtocolEnvelopeValidationTest, "WorldData.UnrealAgent.Protocol.RequestEnvelopeValidation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPProtocolEnvelopeValidationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		auto GetErrorCode = [this](const TSharedPtr<FJsonObject>& Response, const TCHAR* Context)
		{
			const FJsonObject* Error = GetRequiredObjectField(*this, Response, TEXT("error"));
			if (!Error)
			{
				AddError(Context);
				return 0;
			}
			return static_cast<int32>(Error->GetNumberField(TEXT("code")));
		};

		TSharedRef<FJsonObject> InvalidVersion = MakeShared<FJsonObject>();
		InvalidVersion->SetStringField(TEXT("jsonrpc"), TEXT("1.0"));
		InvalidVersion->SetStringField(TEXT("id"), TEXT("version"));
		InvalidVersion->SetStringField(TEXT("method"), TEXT("ping"));
		const TSharedPtr<FJsonObject> InvalidVersionResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(InvalidVersion);
		TestEqual(TEXT("错误 JSON-RPC 版本返回 Invalid Request"), GetErrorCode(InvalidVersionResponse, TEXT("缺少版本错误对象")), -32600);
		TestEqual(TEXT("有效字符串 id 在版本错误中保持不变"), InvalidVersionResponse->GetStringField(TEXT("id")), FString(TEXT("version")));

		TSharedRef<FJsonObject> InvalidId = MakeShared<FJsonObject>();
		InvalidId->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		InvalidId->SetObjectField(TEXT("id"), MakeShared<FJsonObject>());
		InvalidId->SetStringField(TEXT("method"), TEXT("ping"));
		const TSharedPtr<FJsonObject> InvalidIdResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(InvalidId);
		TestEqual(TEXT("对象型 id 返回 Invalid Request"), GetErrorCode(InvalidIdResponse, TEXT("缺少 id 错误对象")), -32600);
		TestTrue(TEXT("非法 id 的错误响应必须使用 null id"), InvalidIdResponse->TryGetField(TEXT("id"))->IsNull());

		TSharedRef<FJsonObject> InvalidParams = MakeShared<FJsonObject>();
		InvalidParams->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		InvalidParams->SetNumberField(TEXT("id"), 3.0);
		InvalidParams->SetStringField(TEXT("method"), TEXT("ping"));
		InvalidParams->SetArrayField(TEXT("params"), TArray<TSharedPtr<FJsonValue>>());
		const TSharedPtr<FJsonObject> InvalidParamsResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(InvalidParams);
		TestEqual(TEXT("数组型 MCP params 返回 Invalid Params"), GetErrorCode(InvalidParamsResponse, TEXT("缺少 params 错误对象")), -32602);

		const TSharedPtr<FJsonValue> PreciseIdA = MakeShared<FJsonValueNumberString>(TEXT("9007199254740992"));
		const TSharedPtr<FJsonValue> PreciseIdB = MakeShared<FJsonValueNumberString>(TEXT("9007199254740993"));
		TestEqual(TEXT("高精度数字 id 保留原始十进制文本"), Protocol::MakeJsonRpcRequestKey(PreciseIdB), FString(TEXT("n:9007199254740993")));
		TestNotEqual(TEXT("相邻高精度数字 id 不会因 double 舍入碰撞"), Protocol::MakeJsonRpcRequestKey(PreciseIdA), Protocol::MakeJsonRpcRequestKey(PreciseIdB));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPUnknownRequestTest, "WorldData.UnrealAgent.Protocol.UnknownMethodAndTool",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPUnknownRequestTest::RunTest(const FString& Parameters)
	{
		TSharedRef<FJsonObject> UnknownMethodRequest = MakeShared<FJsonObject>();
		UnknownMethodRequest->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		UnknownMethodRequest->SetNumberField(TEXT("id"), 17);
		UnknownMethodRequest->SetStringField(TEXT("method"), TEXT("worlddata/unknown-method"));

		const TSharedPtr<FJsonObject> MethodResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(UnknownMethodRequest);
		TestEqual(TEXT("未知方法响应保持 JSON-RPC 2.0"), MethodResponse->GetStringField(TEXT("jsonrpc")), FString(Protocol::GetJsonRpcVersion()));
		TestEqual(TEXT("未知方法保留请求 id"), MethodResponse->GetNumberField(TEXT("id")), 17.0);

		const FJsonObject* Error = GetRequiredObjectField(*this, MethodResponse, TEXT("error"));
		if (Error != nullptr)
		{
			TestEqual(TEXT("未知方法错误码"), Error->GetNumberField(TEXT("code")), -32601.0);
			TestTrue(TEXT("未知方法错误包含方法名"), Error->GetStringField(TEXT("message")).Contains(TEXT("worlddata/unknown-method")));
			const TSharedPtr<FJsonObject>* ErrorData = nullptr;
			TestTrue(TEXT("JSON-RPC 错误包含稳定机器码与恢复提示"),
				Error->TryGetObjectField(TEXT("data"), ErrorData) && ErrorData != nullptr && (*ErrorData)->GetStringField(TEXT("errorCode")) == TEXT("method_not_found") &&
					!(*ErrorData)->GetBoolField(TEXT("retryable")) && !(*ErrorData)->GetStringField(TEXT("recovery")).IsEmpty());
		}

		TestTrue(TEXT("工具调用契约测试必须运行在游戏线程"), IsInGameThread());
		TSharedRef<FJsonObject> ToolArguments = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ToolParams = MakeShared<FJsonObject>();
		ToolParams->SetStringField(TEXT("name"), TEXT("__unknown_uebridge_tool__"));
		ToolParams->SetObjectField(TEXT("arguments"), ToolArguments);

		TSharedRef<FJsonObject> UnknownToolRequest = MakeShared<FJsonObject>();
		UnknownToolRequest->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		UnknownToolRequest->SetStringField(TEXT("id"), TEXT("unknown-tool"));
		UnknownToolRequest->SetStringField(TEXT("method"), TEXT("tools/call"));
		UnknownToolRequest->SetObjectField(TEXT("params"), ToolParams);

		const TSharedPtr<FJsonObject> ToolResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(UnknownToolRequest);
		const FJsonObject* ToolResult = GetRequiredObjectField(*this, ToolResponse, TEXT("result"));
		if (ToolResult != nullptr)
		{
			TestTrue(TEXT("未知工具通过 MCP isError 标记失败"), ToolResult->GetBoolField(TEXT("isError")));
			const TArray<TSharedPtr<FJsonValue>>& Content = ToolResult->GetArrayField(TEXT("content"));
			TestEqual(TEXT("未知工具仍返回一个文本内容项"), Content.Num(), 1);
			if (Content.Num() == 1 && Content[0].IsValid())
			{
				const TSharedPtr<FJsonObject> TextContent = Content[0]->AsObject();
				TestTrue(TEXT("未知工具文本内容有效"), TextContent.IsValid());
				if (TextContent.IsValid())
				{
					TestTrue(TEXT("未知工具错误文本包含外部工具名"), TextContent->GetStringField(TEXT("text")).Contains(TEXT("__unknown_uebridge_tool__")));
				}
			}
			const TSharedPtr<FJsonObject>* Structured = nullptr;
			TestTrue(TEXT("未知工具必须返回 structuredContent"),
				ToolResult->TryGetObjectField(TEXT("structuredContent"), Structured) && Structured != nullptr && Structured->IsValid());
			if (Structured != nullptr && Structured->IsValid())
			{
				TestTrue(TEXT("未知工具 structuredContent 含诊断字段"),
					(*Structured)->HasField(TEXT("success")) || (*Structured)->HasField(TEXT("error")) || (*Structured)->HasField(TEXT("text")));
			}
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolResultBudgetTest, "WorldData.UnrealAgent.Protocol.ToolResultBudget",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolResultBudgetTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FString OversizedResult = FString::Printf(TEXT("{\"success\":true,\"payload\":\"%s\"}"), *FString::ChrN(Protocol::GetMaximumToolResultBytes(), TEXT('x')));
		const TSharedPtr<FJsonObject> Result = Protocol::MakeCallToolResult(OversizedResult);
		TestTrue(TEXT("超预算工具结果失败关闭"), Result.IsValid() && Result->GetBoolField(TEXT("isError")));
		const TSharedPtr<FJsonObject>* Structured = nullptr;
		TestTrue(TEXT("超预算结果返回结构化恢复合同"),
			Result.IsValid() && Result->TryGetObjectField(TEXT("structuredContent"), Structured) && Structured != nullptr &&
				(*Structured)->GetStringField(TEXT("code")) == TEXT("response_budget_exceeded") &&
				(*Structured)->GetStringField(TEXT("operationOutcome")) == TEXT("completed_result_omitted") && !(*Structured)->GetBoolField(TEXT("replaySafe")) &&
				(*Structured)->GetNumberField(TEXT("resultBytes")) > Protocol::GetMaximumToolResultBytes());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolCatalogContractTest, "WorldData.UnrealAgent.Protocol.ToolCatalogMatchesHandlers",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolCatalogContractTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> RegisteredHandlerNames = FUnrealAgentMCPServer::GetRegisteredToolHandlerNames();
		const FMcpToolCatalogValidationResult Validation = ToolRegistry::ValidateToolCatalog(FUnrealAgentMCPServer::GetToolDefinitionsJson(), RegisteredHandlerNames);

		for (const FString& Error : Validation.Errors)
		{
			AddError(Error);
		}
		TestTrue(TEXT("实际工具目录名称唯一且 Schema/Handler 双向配对"), Validation.IsValid());
		TestTrue(TEXT("核心 74 个工具全部存在，运行时 Provider 可追加扩展工具"), RegisteredHandlerNames.Num() >= 74);
		const TArray<FString> ToolsetGatewayNames = { TEXT("search_tools"), TEXT("get_toolset_status"), TEXT("list_toolsets"), TEXT("describe_toolset"), TEXT("call_tool"),
			TEXT("get_toolset_call_result") };
		for (const FString& ToolName : ToolsetGatewayNames)
		{
			TestTrue(*FString::Printf(TEXT("Toolset gateway handler registered: %s"), *ToolName), RegisteredHandlerNames.Contains(ToolName));
		}

		TSharedRef<FJsonObject> SearchArguments = MakeShared<FJsonObject>();
		SearchArguments->SetStringField(TEXT("query"), TEXT("landscape"));
		SearchArguments->SetNumberField(TEXT("maxResults"), 3);
		TSharedRef<FJsonObject> SearchParams = MakeShared<FJsonObject>();
		SearchParams->SetStringField(TEXT("name"), TEXT("search_tools"));
		SearchParams->SetObjectField(TEXT("arguments"), SearchArguments);
		TSharedRef<FJsonObject> SearchRequest = MakeShared<FJsonObject>();
		SearchRequest->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		SearchRequest->SetStringField(TEXT("id"), TEXT("search-tools"));
		SearchRequest->SetStringField(TEXT("method"), TEXT("tools/call"));
		SearchRequest->SetObjectField(TEXT("params"), SearchParams);
		const TSharedPtr<FJsonObject> SearchResponse = FUnrealAgentMCPServer::DispatchJsonRpcRequest(SearchRequest);
		const FJsonObject* SearchResult = GetRequiredObjectField(*this, SearchResponse, TEXT("result"));
		if (SearchResult != nullptr)
		{
			const TSharedPtr<FJsonObject>* Structured = nullptr;
			TestTrue(TEXT("渐进工具搜索返回紧凑、受限的实时目录匹配"),
				SearchResult->TryGetObjectField(TEXT("structuredContent"), Structured) && Structured != nullptr && (*Structured)->GetBoolField(TEXT("success")) &&
					(*Structured)->GetNumberField(TEXT("returnedCount")) >= 1.0 && (*Structured)->GetNumberField(TEXT("returnedCount")) <= 3.0 &&
					!(*Structured)->GetArrayField(TEXT("matches")).IsEmpty());
		}
		const TArray<FString> MigratedDomainNames = { TEXT("project"), TEXT("level"), TEXT("asset"), TEXT("editor"), TEXT("reflection"), TEXT("foliage"), TEXT("networking"),
			TEXT("plugins"), TEXT("feedback"), TEXT("epic"), TEXT("chooser"), TEXT("fab"), TEXT("landscape"), TEXT("whitebox"), TEXT("pcg"), TEXT("gas"), TEXT("material"),
			TEXT("widget"), TEXT("niagara"), TEXT("statetree"), TEXT("audio"), TEXT("blueprint"), TEXT("gameplay"), TEXT("animation"), TEXT("demo") };
		for (const FString& ToolName : MigratedDomainNames)
		{
			TestTrue(*FString::Printf(TEXT("第一批分类工具已注册：%s"), *ToolName), RegisteredHandlerNames.Contains(ToolName));
		}

		TSharedPtr<FJsonObject> RuntimeCatalog;
		const TSharedRef<TJsonReader<>> RuntimeCatalogReader = TJsonReaderFactory<>::Create(FUnrealAgentMCPServer::ReadResource(TEXT("worlddata://tools/catalog")));
		TestTrue(TEXT("运行时工具目录资源可解析"), FJsonSerializer::Deserialize(RuntimeCatalogReader, RuntimeCatalog) && RuntimeCatalog.IsValid());
		if (RuntimeCatalog.IsValid())
		{
			TestEqual(TEXT("运行时工具目录与 tools/list 数量一致"), static_cast<int32>(RuntimeCatalog->GetNumberField(TEXT("toolCount"))), RegisteredHandlerNames.Num());
			TestTrue(TEXT("运行时工具目录明确由 tools/list 发现"), RuntimeCatalog->GetStringField(TEXT("discoveryMethod")) == TEXT("tools/list"));
			TestTrue(TEXT("运行时工具目录明确由 tools/call 调用"), RuntimeCatalog->GetStringField(TEXT("invocationMethod")) == TEXT("tools/call"));
			const TArray<TSharedPtr<FJsonValue>>& RuntimeTools = RuntimeCatalog->GetArrayField(TEXT("tools"));
			const TSharedPtr<FJsonValue>* WhiteboxTool = RuntimeTools.FindByPredicate(
				[](const TSharedPtr<FJsonValue>& Value)
				{
					return Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->GetStringField(TEXT("name")) == TEXT("whitebox");
				});
			TestTrue(TEXT("运行时目录将 Whitebox 标记为可恢复的 action 路由工具"),
				WhiteboxTool != nullptr && (*WhiteboxTool)->AsObject()->GetBoolField(TEXT("actionRouted")) && (*WhiteboxTool)->AsObject()->GetBoolField(TEXT("resumable")));
		}

		TArray<TSharedPtr<FJsonValue>> Definitions;
		const TSharedRef<TJsonReader<>> DefinitionsReader = TJsonReaderFactory<>::Create(FUnrealAgentMCPServer::GetToolDefinitionsJson());
		TestTrue(TEXT("统一描述符目录可解析"), FJsonSerializer::Deserialize(DefinitionsReader, Definitions));
		auto FindDefinition = [&Definitions](const FString& Name) -> TSharedPtr<FJsonObject>
		{
			for (const TSharedPtr<FJsonValue>& Value : Definitions)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				FString CandidateName;
				if (Object.IsValid() && Object->TryGetStringField(TEXT("name"), CandidateName) && CandidateName == Name)
				{
					return Object;
				}
			}
			return nullptr;
		};

		const TSharedPtr<FJsonObject> LevelDefinition = FindDefinition(TEXT("level"));
		const TSharedPtr<FJsonObject> ReflectionDefinition = FindDefinition(TEXT("reflection"));
		const TSharedPtr<FJsonObject> PCGDefinition = FindDefinition(TEXT("pcg"));
		const TSharedPtr<FJsonObject> WhiteboxDefinition = FindDefinition(TEXT("whitebox"));
		TestTrue(TEXT("Level 已升级为显式统一描述符"), LevelDefinition.IsValid());
		TestTrue(TEXT("Reflection 已升级为显式统一描述符"), ReflectionDefinition.IsValid());
		TestTrue(TEXT("PCG 已升级为显式统一描述符"), PCGDefinition.IsValid());
		TestTrue(TEXT("Whitebox 已升级为显式统一描述符"), WhiteboxDefinition.IsValid());
		if (LevelDefinition.IsValid())
		{
			const TSharedPtr<FJsonObject>* Metadata = nullptr;
			TestTrue(TEXT("Level 描述符包含事务策略"),
				LevelDefinition->TryGetObjectField(TEXT("_meta"), Metadata) && Metadata && (*Metadata)->GetStringField(TEXT("transactionPolicy")) == TEXT("ScopedTransaction"));
		}
		if (PCGDefinition.IsValid())
		{
			const TSharedPtr<FJsonObject>* Metadata = nullptr;
			TestTrue(TEXT("PCG 描述符声明可取消与资产沙箱参数"),
				PCGDefinition->TryGetObjectField(TEXT("_meta"), Metadata) && Metadata && (*Metadata)->GetBoolField(TEXT("cancelable")) &&
					(*Metadata)->GetArrayField(TEXT("assetPathArguments")).Num() == 3);
		}
		if (WhiteboxDefinition.IsValid())
		{
			const TSharedPtr<FJsonObject>* Metadata = nullptr;
			TestTrue(TEXT("Whitebox 描述符声明可恢复任务与资产路径参数"),
				WhiteboxDefinition->TryGetObjectField(TEXT("_meta"), Metadata) && Metadata && (*Metadata)->GetBoolField(TEXT("resumable")) &&
					(*Metadata)->GetArrayField(TEXT("assetPathArguments")).Num() == 2);
		}

		FUnrealAgentMCPToolApprovalRequest ReadApproval;
		TestTrue(TEXT("只读工具审批元数据来自运行时唯一描述符"),
			FUnrealAgentMCPServer::DescribeToolApprovalRequest(TEXT("read_file"), MakeShared<FJsonObject>(), ReadApproval) && ReadApproval.bReadOnly && !ReadApproval.bHighRisk);
		FUnrealAgentMCPToolApprovalRequest DeleteApproval;
		TestTrue(TEXT("破坏性工具能够触发高风险审批"),
			FUnrealAgentMCPServer::DescribeToolApprovalRequest(TEXT("delete_actor"), MakeShared<FJsonObject>(), DeleteApproval) && !DeleteApproval.bReadOnly &&
				DeleteApproval.bHighRisk && DeleteApproval.bRequiresConfirmation);

		const FString InvalidCatalog = TEXT(R"JSON([
{"name":"duplicate","inputSchema":{"type":"object"}},
{"name":"duplicate","inputSchema":{"type":"object"}},
{"name":"schema_only","inputSchema":{"type":"object"}}
])JSON");
		const TArray<FString> InvalidHandlerNames = { TEXT("duplicate"), TEXT("handler_only") };
		const FMcpToolCatalogValidationResult InvalidValidation = ToolRegistry::ValidateToolCatalog(InvalidCatalog, InvalidHandlerNames);
		TestFalse(TEXT("校验器能拒绝重复名称与未配对条目"), InvalidValidation.IsValid());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelActionContractTest, "WorldData.UnrealAgent.Protocol.LevelActionContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelActionContractTest::RunTest(const FString& Parameters)
	{
		TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("action"), TEXT("__not_migrated_level_action__"));
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), TEXT("level"));
		Params->SetObjectField(TEXT("arguments"), Arguments);
		TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
		Request->SetStringField(TEXT("id"), TEXT("level-contract"));
		Request->SetStringField(TEXT("method"), TEXT("tools/call"));
		Request->SetObjectField(TEXT("params"), Params);

		const TSharedPtr<FJsonObject> Response = FUnrealAgentMCPServer::DispatchJsonRpcRequest(Request);
		const FJsonObject* Result = GetRequiredObjectField(*this, Response, TEXT("result"));
		if (Result == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("未迁移 action 必须显式失败"), Result->GetBoolField(TEXT("isError")));
		const TSharedPtr<FJsonObject>* Structured = nullptr;
		TestTrue(TEXT("未迁移 action 仍返回 structuredContent"),
			Result->TryGetObjectField(TEXT("structuredContent"), Structured) && Structured != nullptr && Structured->IsValid());
		const TArray<TSharedPtr<FJsonValue>>& Content = Result->GetArrayField(TEXT("content"));
		TestEqual(TEXT("Level action 返回单个诊断内容"), Content.Num(), 1);
		if (Content.Num() == 1 && Content[0].IsValid())
		{
			const TSharedPtr<FJsonObject> Text = Content[0]->AsObject();
			TestTrue(TEXT("Level action 诊断内容有效"), Text.IsValid());
			if (Text.IsValid())
			{
				const FString Diagnostic = Text->GetStringField(TEXT("text"));
				TestTrue(TEXT("诊断列出新迁移的 Actor 文件夹能力"), Diagnostic.Contains(TEXT("set_actor_folder_path")));
				TestTrue(TEXT("诊断列出新迁移的批量删除能力"), Diagnostic.Contains(TEXT("delete_actors")));
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPRepresentativeDomainKernelTest, "WorldData.UnrealAgent.Protocol.RepresentativeDomainKernel",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPRepresentativeDomainKernelTest::RunTest(const FString& Parameters)
	{
		auto CallTool = [this](const FString& ToolName, const TSharedRef<FJsonObject>& Arguments, bool& OutIsError)
		{
			TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("name"), ToolName);
			Params->SetObjectField(TEXT("arguments"), Arguments);
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
			Request->SetStringField(TEXT("id"), FGuid::NewGuid().ToString());
			Request->SetStringField(TEXT("method"), TEXT("tools/call"));
			Request->SetObjectField(TEXT("params"), Params);
			const TSharedPtr<FJsonObject> Response = FUnrealAgentMCPServer::DispatchJsonRpcRequest(Request);
			const FJsonObject* Result = GetRequiredObjectField(*this, Response, TEXT("result"));
			if (!Result)
			{
				OutIsError = true;
				return FString();
			}
			OutIsError = Result->GetBoolField(TEXT("isError"));
			const TArray<TSharedPtr<FJsonValue>>& Content = Result->GetArrayField(TEXT("content"));
			return Content.IsEmpty() || !Content[0].IsValid() ? FString() : Content[0]->AsObject()->GetStringField(TEXT("text"));
		};

		bool bIsError = false;
		TSharedRef<FJsonObject> ReflectionArguments = MakeShared<FJsonObject>();
		ReflectionArguments->SetStringField(TEXT("action"), TEXT("list_tags"));
		const FString ReflectionResult = CallTool(TEXT("reflection"), ReflectionArguments, bIsError);
		TestFalse(TEXT("Reflection 读操作通过统一策略"), bIsError);
		TestTrue(TEXT("Reflection 返回结构化成功结果"), ReflectionResult.Contains(TEXT("\"success\":true")));

		TSharedRef<FJsonObject> LevelArguments = MakeShared<FJsonObject>();
		LevelArguments->SetStringField(TEXT("action"), TEXT("get_outliner"));
		const FString LevelResult = CallTool(TEXT("level"), LevelArguments, bIsError);
		TestFalse(TEXT("Level 读操作通过统一策略"), bIsError);
		TestTrue(TEXT("Level 读取 Actor 文件夹成功"), LevelResult.Contains(TEXT("\"success\":true")));

		TSharedRef<FJsonObject> UnsafePCGArguments = MakeShared<FJsonObject>();
		UnsafePCGArguments->SetStringField(TEXT("action"), TEXT("create_graph"));
		UnsafePCGArguments->SetStringField(TEXT("name"), TEXT("RejectedGraph"));
		UnsafePCGArguments->SetStringField(TEXT("packagePath"), TEXT("/Engine/Rejected"));
		const FString UnsafePCGResult = CallTool(TEXT("pcg"), UnsafePCGArguments, bIsError);
		TestTrue(TEXT("PCG 越界资产写入被策略拒绝"), bIsError);
		TestTrue(TEXT("PCG 拒绝结果包含稳定策略码"), UnsafePCGResult.Contains(TEXT("asset_path_outside_sandbox")));

		TSharedRef<FJsonObject> TaskPCGArguments = MakeShared<FJsonObject>();
		TaskPCGArguments->SetStringField(TEXT("action"), TEXT("execute"));
		TaskPCGArguments->SetStringField(TEXT("actorLabel"), TEXT("__UnrealAgentMCP_Missing_PCG_Actor__"));
		const FString TaskPCGResult = CallTool(TEXT("pcg"), TaskPCGArguments, bIsError);
		TestFalse(TEXT("PCG 生成请求返回 Task 回执"), bIsError);
		TestTrue(TEXT("PCG 生成动作进入统一 Task Kernel"), TaskPCGResult.Contains(TEXT("\"async\"")) && TaskPCGResult.Contains(TEXT("\"taskId\"")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPSessionRegistryDurabilityTest, "WorldData.UnrealAgent.Protocol.SessionRegistryDurability",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPSessionRegistryDurabilityTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		Protocol::FMcpSessionRegistry EvictionRegistry(3);
		const FString Active = EvictionRegistry.Open(TEXT("2025-06-18"));
		EvictionRegistry.MarkInitialized(Active);
		const FString Abandoned = EvictionRegistry.Open(TEXT("2025-03-26"));
		EvictionRegistry.Open(TEXT("2024-11-05"));
		EvictionRegistry.Validate(Active, TEXT("2025-06-18"), true);
		EvictionRegistry.Open(TEXT("2025-06-18"));
		TestEqual(TEXT("会话上限保持为三"), EvictionRegistry.Num(), 3);
		TestEqual(TEXT("超过上限时优先淘汰未完成初始化的闲置会话"), EvictionRegistry.Validate(Abandoned, TEXT("2025-03-26"), false),
			Protocol::EMcpSessionValidation::UnknownSession);
		TestEqual(TEXT("近期活跃的已初始化会话不会被闲置 initialize 挤掉"), EvictionRegistry.Validate(Active, TEXT("2025-06-18"), true), Protocol::EMcpSessionValidation::Ready);

		Protocol::FMcpSessionRegistry BindingRegistry(1);
		const FString BindingSession = BindingRegistry.Open(TEXT("2025-06-18"));
		for (int32 Index = 0; Index < 300; ++Index)
		{
			BindingRegistry.BindRequestToTask(BindingSession, FString::Printf(TEXT("n:%d"), Index), FString::Printf(TEXT("task:%d"), Index));
		}
		Protocol::FMcpSessionSnapshot BindingSnapshot;
		TestTrue(TEXT("可读取有界请求绑定会话"), BindingRegistry.TryGet(BindingSession, BindingSnapshot));
		TestEqual(TEXT("每个会话只保留最近 256 条请求到任务绑定"), BindingSnapshot.RequestBindingCount, 256);
		FString EvictedTaskId;
		TestFalse(TEXT("最旧请求绑定已淘汰"), BindingRegistry.ResolveTaskForRequest(BindingSession, TEXT("n:0"), EvictedTaskId));

		Protocol::FMcpSessionRegistry Registry(64);
		std::atomic<int32> Failures(0);
		TArray<TFuture<void>> Workers;
		const double StartedAt = FPlatformTime::Seconds();
		for (int32 WorkerIndex = 0; WorkerIndex < 4; ++WorkerIndex)
		{
			Workers.Add(Async(EAsyncExecution::ThreadPool,
				[&Registry, &Failures, WorkerIndex]()
				{
					for (int32 Index = 0; Index < 250; ++Index)
					{
						const FString Version = Protocol::GetSupportedProtocolVersions()[(WorkerIndex + Index) % Protocol::GetSupportedProtocolVersions().Num()];
						const FString SessionId = Registry.Open(Version);
						if (!Registry.MarkInitialized(SessionId) || Registry.Validate(SessionId, Version, true) != Protocol::EMcpSessionValidation::Ready)
						{
							++Failures;
						}
						const FString RequestKey = FString::Printf(TEXT("n:%d"), WorkerIndex * 250 + Index);
						const FString TaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
						FString ResolvedTaskId;
						if (!Registry.BindRequestToTask(SessionId, RequestKey, TaskId) || !Registry.ResolveTaskForRequest(SessionId, RequestKey, ResolvedTaskId) ||
							ResolvedTaskId != TaskId || !Registry.Close(SessionId))
						{
							++Failures;
						}
					}
				}));
		}
		for (TFuture<void>& Worker : Workers)
		{
			Worker.Wait();
		}
		const double DurationMilliseconds = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
		TestEqual(TEXT("1000 次并发初始化/绑定/删除无状态错误"), Failures.load(), 0);
		TestEqual(TEXT("耐久循环后不残留会话"), Registry.Num(), 0);
		TestTrue(TEXT("1000 次内核会话生命周期不超过 5000ms"), DurationMilliseconds <= 5000.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPaginationCursorTest, "WorldData.UnrealAgent.Protocol.PaginationCursorContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPPaginationCursorTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TArray<TSharedPtr<FJsonValue>> Source;
		for (int32 Index = 0; Index < 23; ++Index)
		{
			Source.Add(MakeShared<FJsonValueNumber>(Index));
		}
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetNumberField(TEXT("pageSize"), 7);
		FString Cursor;
		FString FirstCursor;
		int32 Received = 0;
		int32 PageCount = 0;
		do
		{
			if (Cursor.IsEmpty())
			{
				Params->RemoveField(TEXT("cursor"));
			}
			else
			{
				Params->SetStringField(TEXT("cursor"), Cursor);
			}
			Protocol::FMcpPaginationResult Page;
			FString Error;
			if (!TestTrue(TEXT("有效 Cursor 页面可解析"), Protocol::PaginateJsonValues(Params, Source, TEXT("tests"), TEXT("revision-a"), Page, Error)))
			{
				return false;
			}
			Received += Page.Items.Num();
			++PageCount;
			Cursor = Page.NextCursor;
			if (PageCount == 1)
			{
				FirstCursor = Cursor;
			}
		} while (!Cursor.IsEmpty());
		TestEqual(TEXT("分页没有遗漏或重复条目"), Received, Source.Num());
		TestEqual(TEXT("23 条按 7 条一页形成四页"), PageCount, 4);

		Params->SetStringField(TEXT("cursor"), FirstCursor);
		Protocol::FMcpPaginationResult RejectedPage;
		FString Error;
		TestFalse(TEXT("目录修订变化后旧 Cursor 被拒绝"), Protocol::PaginateJsonValues(Params, Source, TEXT("tests"), TEXT("revision-b"), RejectedPage, Error));
		TestTrue(TEXT("过期 Cursor 返回稳定诊断"), Error.Contains(TEXT("expired")));
		Params->RemoveField(TEXT("cursor"));
		Params->SetNumberField(TEXT("pageSize"), 251);
		TestFalse(TEXT("超过上限的页大小被拒绝"), Protocol::PaginateJsonValues(Params, Source, TEXT("tests"), TEXT("revision-a"), RejectedPage, Error));
		Params->SetField(TEXT("pageSize"), MakeShared<FJsonValueNumberString>(TEXT("999999999999999999999")));
		TestFalse(TEXT("超大 pageSize 在转换前被安全拒绝"), Protocol::PaginateJsonValues(Params, Source, TEXT("tests"), TEXT("revision-a"), RejectedPage, Error));
		return true;
	}
}

#endif
