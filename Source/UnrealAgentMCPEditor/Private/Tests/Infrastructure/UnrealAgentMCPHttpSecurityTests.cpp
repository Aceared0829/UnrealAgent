// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPHttpSecurityTests.cpp
 * @brief MCP 本地 HTTP Host、Origin 与令牌规则测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Infrastructure/Http/UnrealAgentMCPHttpSecurity.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPHttpAuthorityRulesTest, "WorldData.UnrealAgent.HttpSecurity.AuthorityRules",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPHttpAuthorityRulesTest::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("提取带端口 IPv4 Host"), HttpSecurity::ExtractAuthorityHost(TEXT("127.0.0.1:5753")), FString(TEXT("127.0.0.1")));
		TestEqual(TEXT("提取带协议、端口和路径的 Origin"), HttpSecurity::ExtractAuthorityHost(TEXT("http://LOCALHOST:5753/mcp")), FString(TEXT("localhost")));
		TestEqual(TEXT("提取方括号 IPv6 回环地址"), HttpSecurity::ExtractAuthorityHost(TEXT("http://[::1]:5753/mcp")), FString(TEXT("::1")));

		TestTrue(TEXT("允许省略 Host"), HttpSecurity::IsHostHeaderAllowed(FString()));
		TestTrue(TEXT("允许 localhost Host"), HttpSecurity::IsHostHeaderAllowed(TEXT("localhost:5753")));
		TestTrue(TEXT("允许 IPv6 回环 Host"), HttpSecurity::IsHostHeaderAllowed(TEXT("[::1]:5753")));
		TestFalse(TEXT("拒绝公网 Host"), HttpSecurity::IsHostHeaderAllowed(TEXT("example.com:5753")));
		TestFalse(TEXT("拒绝 DNS 重绑定 Host"), HttpSecurity::IsHostHeaderAllowed(TEXT("attacker.test")));

		TestTrue(TEXT("允许原生客户端省略 Origin"), HttpSecurity::IsOriginHeaderAllowed(FString()));
		TestTrue(TEXT("允许浏览器 null Origin"), HttpSecurity::IsOriginHeaderAllowed(TEXT("null")));
		TestTrue(TEXT("允许回环 Origin"), HttpSecurity::IsOriginHeaderAllowed(TEXT("http://127.0.0.1:5753")));
		TestFalse(TEXT("拒绝非回环 Origin"), HttpSecurity::IsOriginHeaderAllowed(TEXT("https://example.com")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAccessTokenComparisonTest, "WorldData.UnrealAgent.HttpSecurity.AccessTokenComparison",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAccessTokenComparisonTest::RunTest(const FString& Parameters)
	{
		const FString Token = TEXT("0123456789abcdef0123456789abcdef");
		TestTrue(TEXT("相同令牌匹配"), HttpSecurity::ConstantTimeEquals(Token, Token));
		TestFalse(TEXT("同长度不同令牌不匹配"), HttpSecurity::ConstantTimeEquals(Token, TEXT("0123456789abcdef0123456789abcdee")));
		TestFalse(TEXT("短令牌不匹配"), HttpSecurity::ConstantTimeEquals(Token, TEXT("short")));
		TestFalse(TEXT("空令牌不匹配"), HttpSecurity::ConstantTimeEquals(Token, FString()));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAcpMutationRateLimitTest, "WorldData.UnrealAgent.HttpSecurity.AcpMutationRateLimit",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAcpMutationRateLimitTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FString ClientId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		TestTrue(TEXT("First mutation receives a permit"), FUnrealAgentMCPServer::TryAcquireAcpMutationPermit(ClientId));
		TestFalse(TEXT("Immediate repeated mutation is throttled"), FUnrealAgentMCPServer::TryAcquireAcpMutationPermit(ClientId));
		FUnrealAgentMCPServer::UnregisterToolApprovalHandler(ClientId);
		TestTrue(TEXT("Revoking the client clears its limiter state"), FUnrealAgentMCPServer::TryAcquireAcpMutationPermit(ClientId));
		FUnrealAgentMCPServer::UnregisterToolApprovalHandler(ClientId);
		return true;
	}
}

#endif
