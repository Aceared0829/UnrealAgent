// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentCodexACPClientRulesTests.cpp
 * @brief Codex ACP 启动、JSON-RPC 分帧与权限规则自动化测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#include "Application/ACP/UnrealAgentCodexACPClientRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpLaunchSpecRulesTest, "WorldData.UnrealAgent.ACP.LaunchSpecRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpLaunchSpecRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("mode 配置 ID 被识别为会话模式"), WorldDataCodexAcpRules::IsSessionModeConfigId(TEXT("mode")));
	TestTrue(TEXT("带命名空间的 mode 配置 ID 被识别"), WorldDataCodexAcpRules::IsSessionModeConfigId(TEXT("session_mode")));
	TestFalse(TEXT("model 配置 ID 不得被误识别为 mode"), WorldDataCodexAcpRules::IsSessionModeConfigId(TEXT("model")));

	FWorldDataCodexAcpLaunchSpec LaunchSpec;
	const FString ExecutableAdapter = TEXT("C:/Tools/codex-acp.exe");
	TestTrue(TEXT("可执行适配器可直接形成启动规格"), WorldDataCodexAcpRules::BuildLaunchSpecForResolvedAdapterPath(ExecutableAdapter, TEXT(""), TEXT(""), LaunchSpec));
	TestEqual(TEXT("直接启动规格使用适配器自身"), LaunchSpec.Executable, WorldDataCodexAcpRules::NormalizeLaunchPath(ExecutableAdapter));
	TestTrue(TEXT("直接启动不追加参数"), LaunchSpec.Arguments.IsEmpty());

#if PLATFORM_WINDOWS
	const FString BatchAdapter = TEXT("C:/Tools Folder/codex-acp.cmd");
	const FString CommandInterpreter = TEXT("C:\\Windows\\System32\\cmd.exe");
	TestTrue(TEXT("批处理适配器通过命令解释器启动"), WorldDataCodexAcpRules::BuildLaunchSpecForResolvedAdapterPath(BatchAdapter, CommandInterpreter, TEXT(""), LaunchSpec));
	TestEqual(TEXT("批处理启动器保持指定命令解释器"), LaunchSpec.Executable, CommandInterpreter);
	TestEqual(TEXT("批处理命令行保持原有双引号契约"), LaunchSpec.Arguments,
		FString::Printf(TEXT("/d /s /c \"\"%s\"\""), *WorldDataCodexAcpRules::NormalizeLaunchPath(BatchAdapter)));

	TestFalse(TEXT("没有命令解释器时不能启动批处理适配器"), WorldDataCodexAcpRules::BuildLaunchSpecForResolvedAdapterPath(BatchAdapter, TEXT(""), TEXT(""), LaunchSpec));

	const FString PowerShellAdapter = TEXT("C:/Tools/codex-acp.ps1");
	const FString PowerShell = TEXT("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
	TestTrue(TEXT("PowerShell 适配器通过 PowerShell 启动"), WorldDataCodexAcpRules::BuildLaunchSpecForResolvedAdapterPath(PowerShellAdapter, TEXT(""), PowerShell, LaunchSpec));
	TestEqual(TEXT("PowerShell 启动器保持指定路径"), LaunchSpec.Executable, PowerShell);
	TestTrue(TEXT("PowerShell 参数包含无配置和文件参数"), LaunchSpec.Arguments.Contains(TEXT("-NoProfile -ExecutionPolicy Bypass -File")));
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpJsonFrameRulesTest, "WorldData.UnrealAgent.ACP.JsonFrameRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpJsonFrameRulesTest::RunTest(const FString& Parameters)
{
	FString Buffer = TEXT("{\"jsonrpc\":\"2.0\",\"id\":1}\n{\"jsonrpc\":\"2.0\"");
	TArray<FString> Frames;
	WorldDataCodexAcpRules::ExtractCompleteJsonRpcFrames(Buffer, Frames);
	TestEqual(TEXT("换行分帧先返回完整消息"), Frames.Num(), 1);
	TestTrue(TEXT("不完整尾帧继续保留"), Buffer.StartsWith(TEXT("{\"jsonrpc\"")));

	Buffer += TEXT(",\"method\":\"session/update\"}\n");
	WorldDataCodexAcpRules::ExtractCompleteJsonRpcFrames(Buffer, Frames);
	TestEqual(TEXT("补齐后返回第二条消息"), Frames.Num(), 2);
	TestTrue(TEXT("完整换行帧消费后缓冲为空"), Buffer.IsEmpty());

	Buffer = TEXT("{\"id\":2,\"text\":\"brace } and quote \\\" ok\"}{\"id\":3}");
	TArray<FString> ConcatenatedFrames;
	WorldDataCodexAcpRules::ExtractCompleteJsonRpcFrames(Buffer, ConcatenatedFrames);
	TestEqual(TEXT("无换行拼接对象可拆成两帧"), ConcatenatedFrames.Num(), 2);
	TestTrue(TEXT("拼接对象消费后缓冲为空"), Buffer.IsEmpty());

	TSharedPtr<FJsonObject> ParsedFirst;
	TSharedPtr<FJsonObject> ParsedSecond;
	TestTrue(TEXT("包含字符串花括号和转义引号的第一帧仍可解析"), WorldDataCodexAcpRules::ParseJsonObject(ConcatenatedFrames[0], ParsedFirst));
	TestTrue(TEXT("第二帧可解析"), WorldDataCodexAcpRules::ParseJsonObject(ConcatenatedFrames[1], ParsedSecond));
	TestEqual(TEXT("第二帧 ID 保持"), static_cast<int32>(ParsedSecond->GetNumberField(TEXT("id"))), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpJsonMessageRulesTest, "WorldData.UnrealAgent.ACP.JsonMessageRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpJsonMessageRulesTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Request;
	TestTrue(TEXT("JSON-RPC 请求对象可解析"), WorldDataCodexAcpRules::ParseJsonObject(TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"line\":\"12\"}"), Request));
	TestEqual(TEXT("数字 RPC ID 可提取"), static_cast<int32>(WorldDataCodexAcpRules::ExtractRpcId(Request)->AsNumber()), 7);
	TestEqual(TEXT("字符串整数兼容旧协议输入"), WorldDataCodexAcpRules::GetOptionalInt(Request, TEXT("line")), 12);

	const TSharedPtr<FJsonObject> ErrorResponse = WorldDataCodexAcpRules::BuildJsonRpcErrorResponse(Request, -32002, TEXT("blocked"));
	TestEqual(TEXT("错误响应继承请求 ID"), static_cast<int32>(ErrorResponse->GetNumberField(TEXT("id"))), 7);
	TestEqual(TEXT("错误码保持"), static_cast<int32>(ErrorResponse->GetObjectField(TEXT("error"))->GetNumberField(TEXT("code"))), -32002);

	TSharedPtr<FJsonObject> RoundTrip;
	TestTrue(TEXT("压缩序列化结果仍可解析"), WorldDataCodexAcpRules::ParseJsonObject(WorldDataCodexAcpRules::SerializeJsonObject(ErrorResponse), RoundTrip));
	TestEqual(TEXT("往返后错误信息保持"), RoundTrip->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")), FString(TEXT("blocked")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpPromptCompletionRulesTest, "WorldData.UnrealAgent.ACP.PromptCompletionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpPromptCompletionRulesTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("stopReason"), TEXT("max_tokens"));
	TSharedRef<FJsonObject> Usage = MakeShared<FJsonObject>();
	Usage->SetNumberField(TEXT("inputTokens"), 120);
	Usage->SetNumberField(TEXT("outputTokens"), 34);
	Result->SetObjectField(TEXT("usage"), Usage);

	const FWorldDataCodexTurnStatus Status = WorldDataCodexAcpRules::BuildPromptCompletionStatus(Result, TEXT("已处理"));
	TestTrue(TEXT("Prompt 完成状态保持 ACP stopReason"), Status.State == EWorldDataCodexTurnState::Completed && Status.StopReason == TEXT("max_tokens"));
	TestTrue(TEXT("Prompt 完成状态标记 usage 存在"), Status.bHasUsage);
	TestEqual(TEXT("输入 token 保持"), Status.InputTokens, 120);
	TestEqual(TEXT("输出 token 保持"), Status.OutputTokens, 34);

	const FWorldDataCodexTurnStatus MissingUsage = WorldDataCodexAcpRules::BuildPromptCompletionStatus(MakeShared<FJsonObject>(), TEXT("已处理"));
	TestTrue(TEXT("旧适配器缺少 usage 时保持兼容"), !MissingUsage.bHasUsage && MissingUsage.StopReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpPermissionRulesTest, "WorldData.UnrealAgent.ACP.PermissionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpPermissionRulesTest::RunTest(const FString& Parameters)
{
	auto MakeOption = [](const FString& OptionId, const FString& Name, const FString& Kind)
	{
		TSharedRef<FJsonObject> Option = MakeShared<FJsonObject>();
		Option->SetStringField(TEXT("optionId"), OptionId);
		Option->SetStringField(TEXT("name"), Name);
		Option->SetStringField(TEXT("kind"), Kind);
		return MakeShared<FJsonValueObject>(Option);
	};

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetArrayField(TEXT("options"), { MakeOption(TEXT("allow_once"), TEXT("Allow once"), TEXT("allow")), MakeOption(TEXT("reject_once"), TEXT("Reject"), TEXT("reject")) });

	const TArray<FUnrealAgentAcpPermissionOption> Options = WorldDataCodexAcpRules::ExtractPermissionOptions(Params);
	TestEqual(TEXT("权限选项完整提取"), Options.Num(), 2);
	TestEqual(TEXT("允许选项按语义选择"), WorldDataCodexAcpRules::SelectAllowPermissionOptionId(Options), FString(TEXT("allow_once")));
	TestEqual(TEXT("拒绝选项按语义选择"), WorldDataCodexAcpRules::SelectDenyPermissionOptionId(Options), FString(TEXT("reject_once")));

	TSharedPtr<FJsonObject> ToolCall = MakeShared<FJsonObject>();
	ToolCall->SetStringField(TEXT("title"), TEXT("Run terminal command"));
	TestTrue(TEXT("终端类工具请求可识别"), WorldDataCodexAcpRules::IsShellPermissionRequest(ToolCall));
	TestEqual(TEXT("权限标题优先使用 title"), WorldDataCodexAcpRules::GetPermissionRequestTitle(ToolCall), FString(TEXT("Run terminal command")));
	TestTrue(TEXT("allow/approve 选项视为允许"), WorldDataCodexAcpRules::IsAllowPermissionOption(TEXT("approve_always")));
	TestTrue(TEXT("计划模式提示仅允许只读工具"),
		WorldDataCodexAcpRules::GetExecutionPolicyInstruction(EWorldDataAgentMode::Plan, EWorldDataApprovalPolicy::AlwaysAsk, EWorldDataSelfRepairPolicy::Off)
			.Contains(TEXT("只读 MCP")));
	TestTrue(TEXT("项目完全访问仍明确禁用终端"),
		WorldDataCodexAcpRules::GetExecutionPolicyInstruction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::FullProject, EWorldDataSelfRepairPolicy::Automatic)
			.Contains(TEXT("Shell")));
	TestTrue(TEXT("请求批准明确使用面板按钮而非文字回复"),
		WorldDataCodexAcpRules::GetExecutionPolicyInstruction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::AlwaysAsk, EWorldDataSelfRepairPolicy::Off)
			.Contains(TEXT("允许/拒绝按钮")));
	TestTrue(TEXT("请求批准明确禁止要求用户打字批准"),
		WorldDataCodexAcpRules::GetExecutionPolicyInstruction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::AlwaysAsk, EWorldDataSelfRepairPolicy::Off)
			.Contains(TEXT("不要在对话中要求用户打字回复批准")));

	FUnrealAgentMCPToolApprovalRequest ReadOnlyTool;
	ReadOnlyTool.bReadOnly = true;
	FUnrealAgentMCPToolApprovalRequest MutationTool;
	FUnrealAgentMCPToolApprovalRequest HighRiskTool;
	HighRiskTool.bHighRisk = true;
	FUnrealAgentMCPToolApprovalRequest ConfirmationTool;
	ConfirmationTool.bRequiresConfirmation = true;
	TestTrue(TEXT("对话模式拒绝真实 MCP 调用"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Chat, EWorldDataApprovalPolicy::FullProject, ReadOnlyTool) ==
			EUnrealAgentMCPToolApprovalAction::Deny);
	TestTrue(TEXT("计划模式只允许只读工具"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Plan, EWorldDataApprovalPolicy::FullProject, ReadOnlyTool) ==
				EUnrealAgentMCPToolApprovalAction::Allow &&
			WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Plan, EWorldDataApprovalPolicy::FullProject, MutationTool) ==
				EUnrealAgentMCPToolApprovalAction::Deny);
	TestTrue(TEXT("请求批准会拦截每个修改工具"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::AlwaysAsk, MutationTool) ==
			EUnrealAgentMCPToolApprovalAction::Ask);
	TestTrue(TEXT("风险审批仅拦截高风险和强制确认工具"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::RiskBased, MutationTool) ==
				EUnrealAgentMCPToolApprovalAction::Allow &&
			WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::RiskBased, HighRiskTool) ==
				EUnrealAgentMCPToolApprovalAction::Ask &&
			WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::RiskBased, ConfirmationTool) ==
				EUnrealAgentMCPToolApprovalAction::Ask);
	TestTrue(TEXT("完全访问自动允许项目工具"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::FullProject, HighRiskTool) ==
			EUnrealAgentMCPToolApprovalAction::Allow);
	TestTrue(TEXT("Codex 与 Cursor 的完全访问仍遵守工具强制确认"),
		WorldDataCodexAcpRules::ResolveMcpToolApprovalAction(EWorldDataAgentMode::Agent, EWorldDataApprovalPolicy::FullProject, ConfirmationTool) ==
			EUnrealAgentMCPToolApprovalAction::Ask);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCursorModelModeRulesTest, "WorldData.UnrealAgent.ACP.CursorModelModeRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCursorModelModeRulesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> FastHigh = WorldDataCodexAcpRules::GetCursorModelModeLabels(TEXT("grok-4.6[effort=high,fast=true]"));
	TestEqual(TEXT("Fast 模型保留速度与推理标签"), FastHigh.Num(), 2);
	TestEqual(TEXT("Fast 标签优先显示"), FastHigh[0], FString(TEXT("Fast")));
	TestEqual(TEXT("High 推理标签被解析"), FastHigh[1], FString(TEXT("High")));

	const TArray<FString> Max = WorldDataCodexAcpRules::GetCursorModelModeLabels(TEXT("kimi-k3[reasoning=max]"));
	TestEqual(TEXT("Max 推理模型只显示一个标签"), Max.Num(), 1);
	TestEqual(TEXT("Max 标签被解析"), Max[0], FString(TEXT("Max")));

	const TArray<FString> Thinking = WorldDataCodexAcpRules::GetCursorModelModeLabels(TEXT("claude-opus-5[thinking=true,effort=xhigh,fast=false]"));
	TestEqual(TEXT("Thinking 与 XHigh 均被保留"), Thinking.Num(), 2);
	TestEqual(TEXT("Thinking 标签在推理强度前显示"), Thinking[0], FString(TEXT("Thinking")));
	TestEqual(TEXT("XHigh 标签规范化"), Thinking[1], FString(TEXT("XHigh")));
	TestTrue(TEXT("没有 Cursor 模式参数时返回空标签"), WorldDataCodexAcpRules::GetCursorModelModeLabels(TEXT("default[]")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexModelCatalogRulesTest, "WorldData.UnrealAgent.ACP.ModelCatalogRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexModelCatalogRulesTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON(
	{
		"models": [
			{
				"slug": "gpt-5.6-sol",
				"display_name": "GPT-5.6-Sol",
				"description": "Frontier",
				"default_reasoning_level": "low",
				"supported_reasoning_levels": [
					{"effort": "low", "description": "Fast"},
					{"effort": "ultra", "description": "Deep"}
				],
				"visibility": "list",
				"additional_speed_tiers": ["fast"],
				"service_tiers": [
					{
						"id": "priority",
						"name": "Fast",
						"description": "1.5x"
					}
				]
			},
			{
				"slug": "hidden-model",
				"display_name": "Hidden",
				"visibility": "hide"
			}
		]
	}
	)JSON");

	TArray<FWorldDataCodexModelCatalogEntry> Catalog;
	FString Error;
	TestTrue(TEXT("当前 CLI 原始模型目录可解析"), WorldDataCodexAcpRules::ParseCodexModelCatalog(Json, Catalog, Error));
	TestEqual(TEXT("只保留 picker 可见模型"), Catalog.Num(), 1);
	TestEqual(TEXT("没有描述元数据也不参与模型有效性判断"), Catalog[0].Id, FString(TEXT("gpt-5.6-sol")));
	TestEqual(TEXT("模型显示名按 Codex 选择器格式压缩"), Catalog[0].DisplayName, FString(TEXT("5.6 Sol")));
	TestEqual(TEXT("推理能力完整保留"), Catalog[0].ReasoningEfforts.Num(), 2);
	TestEqual(TEXT("priority 服务档映射为 UI 的 fast"), Catalog[0].SpeedOptions[0].Value, FString(TEXT("fast")));
	TestTrue(TEXT("旧 ACP 可安全应用极高推理档"), WorldDataCodexAcpRules::IsBaselineReasoningEffort(TEXT("xhigh")));
	TestFalse(TEXT("最高档必须等待 ACP 明确公开能力"), WorldDataCodexAcpRules::IsBaselineReasoningEffort(TEXT("max")));
	TestFalse(TEXT("超高档必须等待 ACP 明确公开能力"), WorldDataCodexAcpRules::IsBaselineReasoningEffort(TEXT("ultra")));
	TestFalse(TEXT("codex-acp 0.16.0 不暴露 GPT-5.6 直连模型元数据"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("gpt-5.6-sol"), TEXT("0.16.0")));
	TestFalse(TEXT("codex-acp 0.16.0 不暴露 5.3 Spark 元数据"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("gpt-5.3-codex-spark"), TEXT("0.16.0")));
	TestTrue(TEXT("codex-acp 0.16.0 可执行 GPT-5.5"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("gpt-5.5"), TEXT("0.16.0")));
	TestTrue(TEXT("Codex 兼容别名仍由旧 ACP 处理"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("codex-auto-review"), TEXT("0.16.0")));
	TestTrue(TEXT("未来 ACP 版本不被旧版本规则永久屏蔽"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("gpt-5.6-sol"), TEXT("0.17.0")));
	TestTrue(TEXT("App Server ACP 1.1.7 可执行 GPT-5.6"), WorldDataCodexAcpRules::IsModelCompatibleWithAdapter(TEXT("gpt-5.6-sol"), TEXT("1.1.7")));
	TestEqual(TEXT("旧 ACP 使用已知具有元数据的保守回退模型"), WorldDataCodexAcpRules::GetSafeFallbackModelIdForAdapter(TEXT("0.16.0")), FString(TEXT("gpt-5.5")));
	TestTrue(TEXT("App Server ACP 不需要旧模型回退"), WorldDataCodexAcpRules::GetSafeFallbackModelIdForAdapter(TEXT("1.1.7")).IsEmpty());
	const FString ModelInstruction = WorldDataCodexAcpRules::BuildConfirmedModelInstruction(TEXT("codex-auto-review"), TEXT("5.6 Terra"));
	TestTrue(TEXT("模型身份提示包含 ACP 已确认的精确模型 ID"), ModelInstruction.Contains(TEXT("codex-auto-review")));
	TestTrue(TEXT("模型身份提示包含面向用户的显示名"), ModelInstruction.Contains(TEXT("5.6 Terra")));
	TestTrue(TEXT("模型身份提示明确要求直接回答"), ModelInstruction.Contains(TEXT("直接准确回答")));
	TestTrue(TEXT("没有确认模型时禁止猜测"), WorldDataCodexAcpRules::BuildConfirmedModelInstruction(FString(), FString()).Contains(TEXT("不要猜测")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpMcpInjectionRulesTest, "WorldData.UnrealAgent.ACP.McpInjectionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpMcpInjectionRulesTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FJsonObject> Server = WorldDataCodexAcpRules::BuildHttpMcpServer(TEXT("world_data_project_abcd"), TEXT("http://127.0.0.1:25072/mcp"),
		TEXT("X-WorldData-Token"), TEXT("secret"), TEXT("approval-client"), TEXT("codex"));
	TestEqual(TEXT("HTTP MCP type"), WorldDataCodexAcpRules::GetOptionalString(Server, TEXT("type")), FString(TEXT("http")));
	TestEqual(TEXT("UnrealAgent server name"), WorldDataCodexAcpRules::GetOptionalString(Server, TEXT("name")), FString(TEXT("world_data_project_abcd")));
	const TArray<TSharedPtr<FJsonValue>>* Headers = nullptr;
	TestTrue(TEXT("ACP headers use the SDK array contract"), Server->TryGetArrayField(TEXT("headers"), Headers) && Headers && Headers->Num() == 3);

	TSharedRef<FJsonObject> Update = MakeShared<FJsonObject>();
	Update->SetStringField(TEXT("sessionUpdate"), TEXT("tool_call"));
	Update->SetStringField(TEXT("toolCallId"), TEXT("mcp_startup.world_data_project_abcd"));
	Update->SetStringField(TEXT("title"), TEXT("mcp__world_data_project_abcd__startup"));
	Update->SetStringField(TEXT("status"), TEXT("failed"));
	FString Error;
	TestTrue(TEXT("Exact injected MCP startup failure is fail-closed"), WorldDataCodexAcpRules::TryGetMcpStartupFailure(Update, TEXT("world_data_project_abcd"), Error));
	TestFalse(TEXT("A different MCP server cannot invalidate this session"), WorldDataCodexAcpRules::TryGetMcpStartupFailure(Update, TEXT("ue_mcp"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpWarmSessionLifecycleRulesTest, "WorldData.UnrealAgent.ACP.WarmSessionLifecycleRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpWarmSessionLifecycleRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("仅后台创建 session 时输入区保持可发送"), WorldDataCodexAcpRules::HasActiveTurnState(false, false, 0));
	TestTrue(TEXT("排队消息会阻止重复发送"), WorldDataCodexAcpRules::HasActiveTurnState(true, false, 0));
	TestTrue(TEXT("流式回复会阻止并发发送"), WorldDataCodexAcpRules::HasActiveTurnState(false, true, 0));
	TestTrue(TEXT("权限等待属于活动回合"), WorldDataCodexAcpRules::HasActiveTurnState(false, false, 1));
	TestTrue(TEXT("在途 Prompt 失败必须结束用户回合"), WorldDataCodexAcpRules::ShouldFailCloseUserTurn(true, false, false, false));
	TestTrue(TEXT("待发送 Prompt 失败必须结束用户回合"), WorldDataCodexAcpRules::ShouldFailCloseUserTurn(false, true, false, false));
	TestTrue(TEXT("待发送图片失败必须结束用户回合"), WorldDataCodexAcpRules::ShouldFailCloseUserTurn(false, false, true, false));
	TestTrue(TEXT("传输字段清理前捕获的用户回合仍必须结束"), WorldDataCodexAcpRules::ShouldFailCloseUserTurn(false, false, false, true));
	TestFalse(TEXT("空闲连接失败不得假装结束用户回合"), WorldDataCodexAcpRules::ShouldFailCloseUserTurn(false, false, false, false));
	TestFalse(TEXT("发送准备状态不得提前消费上下文回放"), WorldDataCodexAcpRules::ShouldConsumeContextReplay(EWorldDataCodexTurnState::Sending));
	TestTrue(TEXT("ACP 接收 Prompt 后消费上下文回放"), WorldDataCodexAcpRules::ShouldConsumeContextReplay(EWorldDataCodexTurnState::Received));
	TestFalse(TEXT("发送前失败必须保留上下文回放"), WorldDataCodexAcpRules::ShouldConsumeContextReplay(EWorldDataCodexTurnState::Failed));
	TestTrue(TEXT("session 后台创建不锁定模型选择"), WorldDataCodexAcpRules::CanChangeConfigState(false, 0));
	TestFalse(TEXT("真实回复生成期间锁定模型选择"), WorldDataCodexAcpRules::CanChangeConfigState(true, 0));
	TestTrue(TEXT("配置 RPC 在途时保留最新选择而不是禁用菜单"), WorldDataCodexAcpRules::ShouldQueueLatestConfigSelection(1));

	TestFalse(TEXT("当前 session 的更新继续处理"), WorldDataCodexAcpRules::ShouldIgnoreSessionScopedMessage(TEXT("new-session"), TEXT("new-session"), false));
	TestTrue(TEXT("进程复用后丢弃旧 session 的迟到更新"), WorldDataCodexAcpRules::ShouldIgnoreSessionScopedMessage(TEXT("new-session"), TEXT("old-session"), false));
	TestTrue(TEXT("新 session 创建窗口内丢弃旧 session 更新"), WorldDataCodexAcpRules::ShouldIgnoreSessionScopedMessage(FString(), TEXT("old-session"), true));
	TestFalse(TEXT("兼容未携带 sessionId 的旧通知"), WorldDataCodexAcpRules::ShouldIgnoreSessionScopedMessage(TEXT("new-session"), FString(), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldDataCodexAcpImagePromptRulesTest, "WorldData.UnrealAgent.ACP.ImagePromptRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDataCodexAcpImagePromptRulesTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> InitializeResult = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> AgentCapabilities = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> PromptCapabilities = MakeShared<FJsonObject>();
	PromptCapabilities->SetBoolField(TEXT("image"), true);
	AgentCapabilities->SetObjectField(TEXT("promptCapabilities"), PromptCapabilities);
	InitializeResult->SetObjectField(TEXT("agentCapabilities"), AgentCapabilities);

	TestTrue(TEXT("initialize 明确广告 image 时启用图片 Prompt"), WorldDataCodexAcpRules::DoesInitializeResultSupportImagePrompts(InitializeResult));
	TestFalse(TEXT("缺少能力字段时按 fail-closed 处理"), WorldDataCodexAcpRules::DoesInitializeResultSupportImagePrompts(MakeShared<FJsonObject>()));
	PromptCapabilities->SetBoolField(TEXT("image"), false);
	TestFalse(TEXT("Agent 明确拒绝 image 时不得发送图片"), WorldDataCodexAcpRules::DoesInitializeResultSupportImagePrompts(InitializeResult));

	FUnrealAgentAcpPromptImage Image;
	Image.MimeType = TEXT("image/jpeg");
	Image.Base64Data = TEXT("base64-payload");
	const TArray<TSharedPtr<FJsonValue>> Blocks = WorldDataCodexAcpRules::BuildPromptContentBlocks(TEXT("inspect this image"), { Image });
	TestEqual(TEXT("ACP Prompt 包含 text 与 image 两个块"), Blocks.Num(), 2);
	if (Blocks.Num() == 2)
	{
		const TSharedPtr<FJsonObject> TextBlock = Blocks[0]->AsObject();
		const TSharedPtr<FJsonObject> ImageBlock = Blocks[1]->AsObject();
		TestTrue(TEXT("首块保持 ACP text 契约"),
			TextBlock.IsValid() && TextBlock->GetStringField(TEXT("type")) == TEXT("text") && TextBlock->GetStringField(TEXT("text")) == TEXT("inspect this image"));
		TestTrue(TEXT("图片块保持 ACP image/mimeType/data 契约"),
			ImageBlock.IsValid() && ImageBlock->GetStringField(TEXT("type")) == TEXT("image") && ImageBlock->GetStringField(TEXT("mimeType")) == TEXT("image/jpeg") &&
				ImageBlock->GetStringField(TEXT("data")) == TEXT("base64-payload"));
	}

	TSharedRef<FJsonObject> Rpc = MakeShared<FJsonObject>();
	Rpc->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Rpc->SetStringField(TEXT("method"), TEXT("session/prompt"));
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetArrayField(TEXT("prompt"), Blocks);
	Rpc->SetObjectField(TEXT("params"), Params);
	const FString LogSafeRpc = WorldDataCodexAcpRules::BuildLogSafeJsonRpc(WorldDataCodexAcpRules::SerializeJsonObject(Rpc));
	TestFalse(TEXT("ACP 日志不得包含 Base64 图片正文"), LogSafeRpc.Contains(TEXT("base64-payload")));
	TestTrue(TEXT("ACP 日志保留图片块结构并标记脱敏"), LogSafeRpc.Contains(TEXT("session/prompt")) && LogSafeRpc.Contains(TEXT("<redacted>")));

	FUnrealAgentAcpPromptImage CountedImage;
	CountedImage.MimeType = TEXT("image/png");
	CountedImage.Base64Data = FString::ChrN(1000, TEXT('A'));
	TestEqual(TEXT("JSON-RPC 估算计入正文、MIME 与 Base64"), WorldDataCodexAcpRules::EstimateSessionPromptJsonRpcChars(TEXT("ab"), { CountedImage }), 256 + 2 + 96 + 9 + 1000);
	TestFalse(TEXT("超大估算不得进入 session/prompt JSON-RPC"), WorldDataCodexAcpRules::IsSessionPromptJsonRpcWithinLimit(TEXT("ab"), { CountedImage }, 1000));
	TestTrue(TEXT("常规图片 Prompt 低于 JSON-RPC 上限"), WorldDataCodexAcpRules::IsSessionPromptJsonRpcWithinLimit(TEXT("inspect this image"), { Image }));
	return true;
}

#endif
