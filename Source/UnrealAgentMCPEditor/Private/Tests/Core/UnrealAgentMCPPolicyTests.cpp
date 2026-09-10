// Copyright ZhaoZining. All Rights Reserved.

#include "Core/Policy/UnrealAgentMCPPolicy.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPolicySandboxTest, "WorldData.UnrealAgent.Core.Policy.PathSandbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPolicySandboxTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::Policy;

	FMcpProjectPathSandbox FileSandbox(FPaths::ProjectDir());
	FString Resolved;
	FString Error;
	TestTrue(TEXT("项目内现有文件可读"), FileSandbox.ResolveForRead(TEXT("CollectWorldData.uproject"), Resolved, Error));
	TestTrue(TEXT("项目内待写路径可解析"), FileSandbox.ResolveForWrite(TEXT("Saved/Policy/record.json"), Resolved, Error));
	TestFalse(TEXT("相对路径不能逃逸项目根"), FileSandbox.ResolveForWrite(TEXT("../outside.txt"), Resolved, Error));
	TestFalse(TEXT("绝对路径不能逃逸项目根"),
		FileSandbox.ResolveForWrite(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../outside.txt"))), Resolved, Error));

	FMcpAssetPathSandbox AssetSandbox({ TEXT("/Game") });
	TestTrue(TEXT("Game 资产对象路径可规范化"), AssetSandbox.Validate(TEXT("/Game/Buildings/SM_Wall.SM_Wall"), Resolved, Error));
	TestEqual(TEXT("对象后缀被移除"), Resolved, FString(TEXT("/Game/Buildings/SM_Wall")));
	TestFalse(TEXT("Engine 内容根默认不可访问"), AssetSandbox.Validate(TEXT("/Engine/BasicShapes/Cube"), Resolved, Error));
	TestFalse(TEXT("资产路径不能上级跳转"), AssetSandbox.Validate(TEXT("/Game/../Engine/BasicShapes/Cube"), Resolved, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPolicyDecisionTest, "WorldData.UnrealAgent.Core.Policy.DecisionAndAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPolicyDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Policy;

	FMcpServerPolicy ServerPolicy = FMcpServerPolicy::LocalProject(FPaths::ProjectDir());
	TestTrue(TEXT("本地项目策略默认确认文件写入"), ServerPolicy.ConfirmationRisks.Contains(EMcpToolRisk::FileMutation));
	TestTrue(TEXT("本地项目策略默认确认破坏性操作"), ServerPolicy.ConfirmationRisks.Contains(EMcpToolRisk::Destructive));
	TestTrue(TEXT("本地项目策略默认确认代码执行"), ServerPolicy.ConfirmationRisks.Contains(EMcpToolRisk::CodeExecution));
	TestTrue(TEXT("本地项目策略默认确认外部进程"), ServerPolicy.ConfirmationRisks.Contains(EMcpToolRisk::ExternalProcess));
	ServerPolicy.MaximumRisk = EMcpToolRisk::ContentMutation;
	ServerPolicy.ConfirmationRisks.Add(EMcpToolRisk::ContentMutation);
	FMcpPolicyEngine Engine(ServerPolicy);

	FMcpToolDescriptor Descriptor;
	Descriptor.Provider = TEXT("Automation");
	Descriptor.Name = TEXT("write_project_file");
	Descriptor.QualifiedName = TEXT("worlddata.project.write_source_file");
	Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract&)
	{
		return true;
	};
	Descriptor.Risk = EMcpToolRisk::ContentMutation;
	Descriptor.FilePathArguments = { TEXT("file_path") };

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("file_path"), TEXT("Saved/Policy/test.json"));
	FMcpPolicyRequestContext Context;
	FMcpPolicyDecision Decision = Engine.Evaluate(Descriptor, Arguments, Context);
	TestEqual(TEXT("需要确认的风险不能直接执行"), Decision.Outcome, EMcpPolicyOutcome::ConfirmationRequired);

	Context.bConfirmed = true;
	Decision = Engine.Evaluate(Descriptor, Arguments, Context);
	TestEqual(TEXT("确认后允许项目内写入"), Decision.Outcome, EMcpPolicyOutcome::Allowed);

	Arguments->SetStringField(TEXT("file_path"), TEXT("../outside.json"));
	Decision = Engine.Evaluate(Descriptor, Arguments, Context);
	TestEqual(TEXT("确认不能绕过文件沙箱"), Decision.Outcome, EMcpPolicyOutcome::Denied);
	TestEqual(TEXT("文件沙箱返回稳定错误码"), Decision.Code, FString(TEXT("file_path_outside_sandbox")));

	FMcpServerPolicy HighRiskPolicy = FMcpServerPolicy::LocalProject(FPaths::ProjectDir());
	FMcpPolicyEngine HighRiskEngine(MoveTemp(HighRiskPolicy));
	Descriptor.Risk = EMcpToolRisk::FileMutation;
	Descriptor.FilePathArguments.Reset();
	Context.bConfirmed = false;
	Decision = HighRiskEngine.Evaluate(Descriptor, MakeShared<FJsonObject>(), Context);
	TestEqual(TEXT("未单独标注的高风险工具仍由本地项目策略要求确认"), Decision.Outcome, EMcpPolicyOutcome::ConfirmationRequired);
	Context.bConfirmed = true;
	Decision = HighRiskEngine.Evaluate(Descriptor, MakeShared<FJsonObject>(), Context);
	TestEqual(TEXT("服务端确认后允许高风险工具"), Decision.Outcome, EMcpPolicyOutcome::Allowed);
	Descriptor.Risk = EMcpToolRisk::ContentMutation;
	Descriptor.FilePathArguments = { TEXT("file_path") };

	FMcpServerPolicy CanonicalDenyPolicy = FMcpServerPolicy::LocalProject(FPaths::ProjectDir());
	CanonicalDenyPolicy.DeniedTools.Add(TEXT("worlddata.project.write_source_file"));
	FMcpPolicyEngine CanonicalDenyEngine(MoveTemp(CanonicalDenyPolicy));
	Arguments->SetStringField(TEXT("file_path"), TEXT("Saved/Policy/test.json"));
	Decision = CanonicalDenyEngine.Evaluate(Descriptor, Arguments, Context);
	TestEqual(TEXT("策略可按 Canonical Tool ID 拒绝单个 Action"), Decision.Code, FString(TEXT("tool_denied")));

	FMcpServerPolicy TransportAllowPolicy = FMcpServerPolicy::LocalProject(FPaths::ProjectDir());
	TransportAllowPolicy.AllowedTools.Add(TEXT("write_project_file"));
	FMcpPolicyEngine TransportAllowEngine(MoveTemp(TransportAllowPolicy));
	Decision = TransportAllowEngine.Evaluate(Descriptor, Arguments, Context);
	TestEqual(TEXT("分类入口白名单保持向后兼容"), Decision.Outcome, EMcpPolicyOutcome::Allowed);

	Descriptor.Risk = EMcpToolRisk::CodeExecution;
	Descriptor.FilePathArguments.Reset();
	Decision = Engine.Evaluate(Descriptor, MakeShared<FJsonObject>(), Context);
	TestEqual(TEXT("超出风险上限的代码执行被拒绝"), Decision.Outcome, EMcpPolicyOutcome::Denied);

	Descriptor.Risk = EMcpToolRisk::ReadOnly;
	Context.bAuthenticated = false;
	Decision = Engine.Evaluate(Descriptor, MakeShared<FJsonObject>(), Context);
	TestEqual(TEXT("未认证调用方被服务端拒绝"), Decision.Code, FString(TEXT("authentication_required")));

	FMcpAuditLog AuditLog(2);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FMcpAuditRecord Record;
		Record.RequestId = FGuid::NewGuid();
		Record.Timestamp = FDateTime::UtcNow();
		Record.ToolName = FString::FromInt(Index);
		AuditLog.Append(MoveTemp(Record));
	}
	TestEqual(TEXT("审计日志遵守容量上限"), AuditLog.Num(), 2);
	TestEqual(TEXT("审计日志按时间保留最近记录"), AuditLog.List(10)[0].ToolName, FString(TEXT("1")));
	return true;
}
