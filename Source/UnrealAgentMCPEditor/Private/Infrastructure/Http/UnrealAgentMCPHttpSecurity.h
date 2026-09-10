#pragma once

/**
 * @file UnrealAgentMCPHttpSecurity.h
 * @brief 本地 MCP HTTP 服务的 Host、Origin 与令牌校验规则。
 */

#include "CoreMinimal.h"

namespace UnrealAgentMCP::HttpSecurity
{
	/** 从 Host 或 Origin 中提取规范化的小写主机名。 */
	FString ExtractAuthorityHost(FString Authority);

	bool IsLoopbackAuthority(const FString& Authority);

	/** 原生客户端可省略 Host；存在时必须指向回环地址。 */
	bool IsHostHeaderAllowed(const FString& HostHeaderValue);

	/** 原生客户端可省略 Origin；浏览器 Origin 必须指向回环地址。 */
	bool IsOriginHeaderAllowed(const FString& OriginHeaderValue);

	/** 以固定迭代次数比较 UTF-8 令牌，降低时序旁路风险。 */
	bool ConstantTimeEquals(const FString& Expected, const FString& Actual);
}
