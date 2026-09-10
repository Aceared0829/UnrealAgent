// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "Core/Json/JsonValue.h"

#include <string>

namespace worlddata::host
{
	/** Editor 网关调用结果；Application 不感知具体传输协议。 */
	struct EditorGatewayResult
	{
		bool success = false;
		/** 请求可能已执行但响应未被可靠接收；此时禁止自动重放。 */
		bool request_outcome_unknown = false;
		json::Value response;
		std::string error;
		std::string cleanup_warning;
	};

	/**
	 * 独立 Host 访问 Unreal Editor 实时能力的应用层端口。
	 *
	 * 具体实现可以使用 HTTP、命名管道或其他本机传输。
	 */
	class IEditorGateway
	{
	public:
		virtual ~IEditorGateway() = default;

		/** 探测 Editor 端点是否可用。 */
		virtual bool IsAvailable() const = 0;
		/** 将完整 JSON-RPC 请求转发给 Editor。 */
		virtual EditorGatewayResult Forward(const json::Value& request) const = 0;
	};
}
