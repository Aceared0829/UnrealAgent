// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "Application/Ports/EditorGateway.h"

#include <filesystem>

namespace worlddata::host
{
	/**
	 * 通过项目 Saved 连接清单和 WinHTTP 访问 Editor 的适配器。
	 *
	 * 每次调用都重新读取连接清单，使 Editor 重启、端口和令牌轮换立即生效。
	 */
	class WinHttpEditorGateway final : public IEditorGateway
	{
	public:
		explicit WinHttpEditorGateway(std::filesystem::path connection_file);

		bool IsAvailable() const override;
		EditorGatewayResult Forward(const json::Value& request) const override;

	private:
		std::filesystem::path connection_file_;
	};
}
