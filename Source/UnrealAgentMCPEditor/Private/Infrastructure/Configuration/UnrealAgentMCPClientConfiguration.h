// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPClientConfiguration.h
 * @brief Cursor、Codex 与 Claude 客户端配置的纯合并规则。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::ClientConfiguration
{
	/** 写入各客户端配置所需的不可变连接快照。 */
	struct FMcpClientConnectionDescriptor
	{
		FString ProjectName;
		FString ProjectId;
		FString ServerName;
		FString Url;
		FString ProtocolVersion;
		FString AccessTokenHeaderName;
		FString AccessToken;
		FString Command;
		TArray<FString> Arguments;

		bool UsesStdioHost() const
		{
			return !Command.IsEmpty();
		}
	};

	/**
	 * 合并 .mcp.json / .cursor/mcp.json。
	 * 保留用户条目，移除本插件旧条目，再写入当前工程条目。
	 */
	TSharedRef<FJsonObject> MergeJsonClientConfiguration(const TSharedPtr<FJsonObject>& ExistingRoot, const FMcpClientConnectionDescriptor& Connection);

	/**
	 * 合并 ~/.codex/config.toml。
	 * 保留用户内容，只替换当前或遗留的 world_data_* 受管区段。
	 */
	FString MergeCodexTomlConfiguration(const FString& ExistingConfiguration, const FMcpClientConnectionDescriptor& Connection);

	/** 保留 Claude 其它设置并启用工程 MCP 服务。 */
	TSharedRef<FJsonObject> MergeClaudeProjectSettings(const TSharedPtr<FJsonObject>& ExistingRoot);
}
