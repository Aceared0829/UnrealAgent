// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "Core/Json/JsonValue.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace worlddata::host
{
	class IEditorGateway;
	class IEditorProcessService;
	class IProjectFileService;

	/**
	 * 独立 MCP Host 的应用服务。
	 *
	 * 负责 JSON-RPC/MCP 用例编排，通过 Ports 使用外部能力。
	 */
	class HostApplication
	{
	public:
		explicit HostApplication(std::shared_ptr<IProjectFileService> project_file_service, std::shared_ptr<IEditorGateway> editor_gateway,
			std::shared_ptr<IEditorProcessService> editor_process_service);

		/** 处理单条 JSON-RPC 请求；通知消息不产生响应。 */
		std::optional<json::Value> Handle(const json::Value& request) const;

	private:
		json::Value Initialize(const json::Value& request) const;
		json::Value ListTools(const json::Value& request) const;
		json::Value CallTool(const json::Value& request) const;
		json::Value ListResources(const json::Value& request) const;
		json::Value ReadResource(const json::Value& request) const;

		json::Value GetHostStatus() const;
		json::Value GetProjectInfo() const;
		json::Value ReadProjectFile(const json::Value& arguments) const;
		json::Value ListProjectFiles(const json::Value& arguments) const;
		json::Value ReplaceProjectText(const json::Value& arguments) const;
		json::Value StartEditor(const json::Value& arguments) const;
		json::Value StopEditor(const json::Value& arguments) const;
		json::Value RestartEditor(const json::Value& arguments) const;
		json::Value BuildProject(const json::Value& arguments) const;

		json::Value Response(const json::Value& request, json::Value result) const;
		json::Value Error(const json::Value& request, int code, std::string message) const;
		json::Value ToolResult(json::Value payload, bool is_error) const;
		std::shared_ptr<IProjectFileService> project_file_service_;
		std::shared_ptr<IEditorGateway> editor_gateway_;
		std::shared_ptr<IEditorProcessService> editor_process_service_;
		std::vector<json::Value> tool_definitions_;
	};
}
