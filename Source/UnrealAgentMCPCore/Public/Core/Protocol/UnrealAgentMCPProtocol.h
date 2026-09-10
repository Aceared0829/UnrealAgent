// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPProtocol.h
 * @brief 与编辑器状态无关的 MCP/JSON-RPC 协议定义与响应构造。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UnrealAgentMCP::Protocol
{
	struct FMcpPaginationResult
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		FString NextCursor;
	};

	/** 当前实现使用的 JSON-RPC 版本。 */
	UNREALAGENTMCPCORE_API const TCHAR* GetJsonRpcVersion();

	/** 按新到旧排列的 MCP 协议版本。 */
	UNREALAGENTMCPCORE_API const TArray<FString>& GetSupportedProtocolVersions();

	/** 最新的 MCP 协议版本。 */
	UNREALAGENTMCPCORE_API const FString& GetLatestProtocolVersion();

	/**
	 * 协商 MCP 协议版本。
	 * 请求版本受支持时原样返回，否则回退到最新版本以保持旧客户端兼容。
	 */
	UNREALAGENTMCPCORE_API FString NegotiateProtocolVersion(const FString& RequestedVersion);

	/** 为目录内容计算稳定修订标识，Cursor 会绑定该修订以拒绝过期翻页。 */
	UNREALAGENTMCPCORE_API FString CalculateStableRevision(const FString& Content);

	/** 将 JSON-RPC string/number/null id 规范化为 Session 内请求键。 */
	UNREALAGENTMCPCORE_API FString MakeJsonRpcRequestKey(const TSharedPtr<FJsonValue>& RequestId);

	/**
	 * 使用 URL-safe Base64 不透明 Cursor 对稳定数组分页。
	 * Cursor 同时绑定 Scope 与 Revision，目录热重载后旧 Cursor 会明确失败。
	 */
	UNREALAGENTMCPCORE_API bool PaginateJsonValues(const TSharedPtr<FJsonObject>& Params, const TArray<TSharedPtr<FJsonValue>>& Source, const FString& Scope,
		const FString& Revision, FMcpPaginationResult& OutPage, FString& OutError, int32 DefaultPageSize = 100, int32 MaximumPageSize = 250);

	/** 构造标准 JSON-RPC 错误响应。 */
	UNREALAGENTMCPCORE_API TSharedPtr<FJsonObject> MakeJsonRpcErrorResponse(const TSharedPtr<FJsonValue>& RequestId, int32 ErrorCode, const FString& ErrorMessage);

	/** 单次 tools/call 文本与 structuredContent 的最大 UTF-8 字节数。 */
	UNREALAGENTMCPCORE_API int32 GetMaximumToolResultBytes();

	/** 构造无 id 的标准 JSON-RPC 通知；省略参数时仍输出空 params 对象。 */
	UNREALAGENTMCPCORE_API TSharedRef<FJsonObject> MakeJsonRpcNotification(const FString& Method, TSharedPtr<FJsonObject> Params = nullptr);

	/**
	 * 构造 MCP tools/call 结果：content + structuredContent + isError。
	 * 工具声明 outputSchema 时，客户端要求必须返回 structuredContent。
	 */
	UNREALAGENTMCPCORE_API TSharedPtr<FJsonObject> MakeCallToolResult(const FString& ToolResultText);
}
