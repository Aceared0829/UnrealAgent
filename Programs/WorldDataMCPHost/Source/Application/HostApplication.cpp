// Copyright ZhaoZining. All Rights Reserved.

#include "Application/HostApplication.h"
#include "Application/Ports/EditorGateway.h"
#include "Application/Ports/EditorProcessService.h"
#include "Application/Ports/ProjectFileService.h"

#include <algorithm>
#include <set>

namespace worlddata::host
{
	namespace
	{
		json::Value EmptyObjectSchema()
		{
			return json::Value::Object({ { "properties", json::Value::Object() }, { "type", json::Value::String("object") } });
		}

		json::Value StringProperty(const std::string& description)
		{
			return json::Value::Object({ { "description", json::Value::String(description) }, { "type", json::Value::String("string") } });
		}

		json::Value BooleanProperty(const std::string& description, const bool default_value)
		{
			return json::Value::Object(
				{ { "default", json::Value::Boolean(default_value) }, { "description", json::Value::String(description) }, { "type", json::Value::String("boolean") } });
		}

		json::Value IntegerProperty(const std::string& description, const int default_value, const int minimum, const int maximum)
		{
			return json::Value::Object({ { "default", json::Value::Number(default_value) }, { "description", json::Value::String(description) },
				{ "maximum", json::Value::Number(maximum) }, { "minimum", json::Value::Number(minimum) }, { "type", json::Value::String("integer") } });
		}

		json::Value ToolDefinition(const std::string& name, const std::string& description, json::Value schema, const bool read_only = true, const bool destructive = false)
		{
			return json::Value::Object({ { "annotations",
											 json::Value::Object({ { "destructiveHint", json::Value::Boolean(destructive) }, { "openWorldHint", json::Value::Boolean(false) },
												 { "readOnlyHint", json::Value::Boolean(read_only) }, { "title", json::Value::String(name) } }) },
				{ "description", json::Value::String(description) }, { "inputSchema", std::move(schema) }, { "name", json::Value::String(name) } });
		}

		const json::Value* Params(const json::Value& request)
		{
			const json::Value* params = request.Find("params");
			return params && params->type == json::Type::Object ? params : nullptr;
		}

		EditorProcessRequest ProcessRequest(const json::Value& arguments, const bool default_wait)
		{
			EditorProcessRequest request;
			request.wait = default_wait;
			if (const json::Value* value = arguments.Find("dryRun"))
				request.dry_run = value->BooleanOr();
			if (const json::Value* value = arguments.Find("force"))
				request.force = value->BooleanOr();
			if (const json::Value* value = arguments.Find("clean"))
				request.clean = value->BooleanOr();
			if (const json::Value* value = arguments.Find("wait"))
				request.wait = value->BooleanOr(default_wait);
			if (const json::Value* value = arguments.Find("timeoutSeconds"))
			{
				request.timeout_seconds = std::clamp(static_cast<int>(value->NumberOr(15)), 1, 120);
			}
			if (const json::Value* value = arguments.Find("configuration"))
				request.configuration = value->StringOr("Development");
			if (const json::Value* value = arguments.Find("platform"))
				request.platform = value->StringOr("Win64");
			if (const json::Value* value = arguments.Find("extraArguments"))
				request.extra_arguments = value->StringOr();
			return request;
		}

		json::Value ProcessPayload(const EditorProcessResult& result)
		{
			json::Value payload = json::Value::Object({ { "arguments", json::Value::String(result.arguments) }, { "completed", json::Value::Boolean(result.completed) },
				{ "dryRun", json::Value::Boolean(result.dry_run) }, { "executable", json::Value::String(result.executable) }, { "exitCode", json::Value::Number(result.exit_code) },
				{ "launched", json::Value::Boolean(result.launched) }, { "logPath", json::Value::String(result.log_path) }, { "processId", json::Value::Number(result.process_id) },
				{ "running", json::Value::Boolean(result.running) }, { "stopped", json::Value::Boolean(result.stopped) }, { "success", json::Value::Boolean(result.success) } });
			if (!result.error.empty())
			{
				payload.object["error"] = json::Value::String(result.error);
			}
			return payload;
		}
	}

	HostApplication::HostApplication(std::shared_ptr<IProjectFileService> project_file_service, std::shared_ptr<IEditorGateway> editor_gateway,
		std::shared_ptr<IEditorProcessService> editor_process_service)
		: project_file_service_(std::move(project_file_service)), editor_gateway_(std::move(editor_gateway)), editor_process_service_(std::move(editor_process_service))
	{
		tool_definitions_.push_back(ToolDefinition("get_host_status", "Report the independent WorldDataMCPHost process and transport state.", EmptyObjectSchema()));
		tool_definitions_.push_back(ToolDefinition("get_project_info", "Read project identity without starting Unreal Editor.", EmptyObjectSchema()));

		json::Value read_schema = EmptyObjectSchema();
		read_schema.object["properties"].object["path"] = StringProperty("Project-relative file path.");
		read_schema.object["required"] = json::Value::Array({ json::Value::String("path") });
		tool_definitions_.push_back(ToolDefinition("read_project_file", "Read a UTF-8 text file constrained to the project root.", std::move(read_schema)));

		json::Value list_schema = EmptyObjectSchema();
		auto& list_properties = list_schema.object["properties"].object;
		list_properties["directory"] = StringProperty("Project-relative directory. Default is the project root.");
		list_properties["extension"] = StringProperty("Optional extension without a leading dot.");
		list_properties["recursive"] = json::Value::Object({ { "type", json::Value::String("boolean") } });
		list_properties["limit"] =
			json::Value::Object({ { "maximum", json::Value::Number(2000) }, { "minimum", json::Value::Number(1) }, { "type", json::Value::String("integer") } });
		tool_definitions_.push_back(ToolDefinition("list_project_files", "List files constrained to the project root.", std::move(list_schema)));

		json::Value replace_schema = EmptyObjectSchema();
		auto& replace_properties = replace_schema.object["properties"].object;
		replace_properties["path"] = StringProperty("Project or plugin source file path.");
		replace_properties["expectedText"] = StringProperty("Exact text that must occur once.");
		replace_properties["replacementText"] = StringProperty("Replacement text.");
		replace_schema.object["required"] = json::Value::Array({ json::Value::String("path"), json::Value::String("expectedText"), json::Value::String("replacementText") });
		tool_definitions_.push_back(
			ToolDefinition("replace_project_text", "Apply one deterministic source edit offline and create a recovery backup.", std::move(replace_schema), false, true));

		json::Value start_schema = EmptyObjectSchema();
		auto& start_properties = start_schema.object["properties"].object;
		start_properties["dryRun"] = BooleanProperty("仅返回启动计划，不创建进程。", false);
		start_properties["extraArguments"] = StringProperty("附加到 Unreal Editor 的命令行参数。");
		tool_definitions_.push_back(ToolDefinition("start_editor", "启动当前项目对应的 Unreal Editor；已运行时返回现有进程。", std::move(start_schema), false));

		json::Value stop_schema = EmptyObjectSchema();
		auto& stop_properties = stop_schema.object["properties"].object;
		stop_properties["dryRun"] = BooleanProperty("仅验证目标进程，不关闭编辑器。", false);
		stop_properties["force"] = BooleanProperty("正常关闭超时后强制结束经过校验的编辑器进程。", false);
		stop_properties["timeoutSeconds"] = IntegerProperty("等待编辑器正常退出的秒数。", 15, 1, 120);
		tool_definitions_.push_back(ToolDefinition("stop_editor", "关闭连接清单中属于当前项目的 Unreal Editor。", std::move(stop_schema), false, true));

		json::Value restart_schema = EmptyObjectSchema();
		auto& restart_properties = restart_schema.object["properties"].object;
		restart_properties["dryRun"] = BooleanProperty("仅验证关闭目标并返回重新启动计划。", false);
		restart_properties["force"] = BooleanProperty("正常关闭超时后强制结束经过校验的编辑器进程。", false);
		restart_properties["timeoutSeconds"] = IntegerProperty("等待编辑器正常退出的秒数。", 15, 1, 120);
		restart_properties["extraArguments"] = StringProperty("重新启动时附加到 Unreal Editor 的命令行参数。");
		tool_definitions_.push_back(ToolDefinition("restart_editor", "安全关闭并重新启动当前项目的 Unreal Editor。", std::move(restart_schema), false, true));

		json::Value build_schema = EmptyObjectSchema();
		auto& build_properties = build_schema.object["properties"].object;
		build_properties["clean"] = BooleanProperty("构建前执行目标清理。", false);
		build_properties["configuration"] = StringProperty("构建配置：Debug、DebugGame、Development、Shipping 或 Test。");
		build_properties["dryRun"] = BooleanProperty("仅返回 UnrealBuildTool 调用计划。", false);
		build_properties["platform"] = StringProperty("目标平台：Win64、Linux 或 Mac。");
		build_properties["wait"] = BooleanProperty("等待构建结束并返回退出码。", true);
		tool_definitions_.push_back(ToolDefinition("build_project", "使用项目绑定的 UnrealBuildTool 构建 Editor 目标。", std::move(build_schema), false));
	}

	std::optional<json::Value> HostApplication::Handle(const json::Value& request) const
	{
		if (request.type != json::Type::Object)
		{
			return Error(request, -32600, "Invalid JSON-RPC request.");
		}
		const json::Value* method_value = request.Find("method");
		if (!method_value || method_value->type != json::Type::String)
		{
			return Error(request, -32600, "Missing JSON-RPC method.");
		}
		if (!request.Find("id") && method_value->string.rfind("notifications/", 0) == 0)
		{
			return std::nullopt;
		}

		if (method_value->string == "initialize")
			return Initialize(request);
		if (method_value->string == "ping")
		{
			return Response(request, json::Value::Object());
		}
		if (method_value->string == "tools/list")
			return ListTools(request);
		if (method_value->string == "tools/call")
			return CallTool(request);
		if (method_value->string == "resources/list")
			return ListResources(request);
		if (method_value->string == "resources/read")
			return ReadResource(request);
		return Error(request, -32601, "Unknown method: " + method_value->string);
	}

	json::Value HostApplication::Initialize(const json::Value& request) const
	{
		std::string protocol_version = "2025-06-18";
		if (const json::Value* params = Params(request))
		{
			if (const json::Value* requested = params->Find("protocolVersion"); requested && requested->type == json::Type::String)
			{
				if (requested->string == "2025-06-18" || requested->string == "2025-03-26" || requested->string == "2024-11-05")
				{
					protocol_version = requested->string;
				}
			}
		}
		return Response(request,
			json::Value::Object({ { "capabilities", json::Value::Object({ { "resources", json::Value::Object() }, { "tools", json::Value::Object() } }) },
				{ "protocolVersion", json::Value::String(protocol_version) },
				{ "serverInfo",
					json::Value::Object({ { "name", json::Value::String("WorldDataMCPHost") }, { "title", json::Value::String("Unreal Agent Host") },
						{ "version", json::Value::String("0.3.0") } }) } }));
	}

	json::Value HostApplication::ListTools(const json::Value& request) const
	{
		std::vector<json::Value> tools = tool_definitions_;
		std::set<std::string> names;
		for (const json::Value& tool : tools)
		{
			if (const json::Value* name = tool.Find("name"))
			{
				names.insert(name->StringOr());
			}
		}
		if (editor_gateway_)
		{
			const EditorGatewayResult forwarded = editor_gateway_->Forward(request);
			const json::Value* result = forwarded.success ? forwarded.response.Find("result") : nullptr;
			const json::Value* remote_tools = result ? result->Find("tools") : nullptr;
			if (remote_tools && remote_tools->type == json::Type::Array)
			{
				for (const json::Value& tool : remote_tools->array)
				{
					const json::Value* name = tool.Find("name");
					if (name && names.insert(name->StringOr()).second)
					{
						tools.push_back(tool);
					}
				}
			}
		}
		return Response(request, json::Value::Object({ { "tools", json::Value::Array(std::move(tools)) } }));
	}

	json::Value HostApplication::CallTool(const json::Value& request) const
	{
		const json::Value* params = Params(request);
		const json::Value* name = params ? params->Find("name") : nullptr;
		if (!name || name->type != json::Type::String)
		{
			return Error(request, -32602, "Missing tools/call name.");
		}
		const json::Value* arguments = params->Find("arguments");
		const json::Value empty_arguments = json::Value::Object();
		if (!arguments || arguments->type != json::Type::Object)
		{
			arguments = &empty_arguments;
		}

		json::Value payload;
		bool is_error = false;
		if (name->string == "get_host_status")
			payload = GetHostStatus();
		else if (name->string == "get_project_info")
			payload = GetProjectInfo();
		else if (name->string == "read_project_file")
			payload = ReadProjectFile(*arguments);
		else if (name->string == "list_project_files")
			payload = ListProjectFiles(*arguments);
		else if (name->string == "replace_project_text")
			payload = ReplaceProjectText(*arguments);
		else if (name->string == "start_editor")
			payload = StartEditor(*arguments);
		else if (name->string == "stop_editor")
			payload = StopEditor(*arguments);
		else if (name->string == "restart_editor")
			payload = RestartEditor(*arguments);
		else if (name->string == "build_project")
			payload = BuildProject(*arguments);
		else
		{
			if (editor_gateway_)
			{
				EditorGatewayResult forwarded = editor_gateway_->Forward(request);
				if (forwarded.success)
				{
					return forwarded.response;
				}
				payload = json::Value::Object(
					{ { "editorConnected", json::Value::Boolean(false) }, { "error", json::Value::String(forwarded.error) }, { "success", json::Value::Boolean(false) } });
				return Response(request, ToolResult(std::move(payload), true));
			}
			payload = json::Value::Object({ { "error", json::Value::String("Unknown host tool: " + name->string) }, { "success", json::Value::Boolean(false) } });
			is_error = true;
		}
		if (const json::Value* success = payload.Find("success"))
		{
			is_error = !success->BooleanOr(true);
		}
		return Response(request, ToolResult(std::move(payload), is_error));
	}

	json::Value HostApplication::GetHostStatus() const
	{
		const bool editor_connected = editor_gateway_ && editor_gateway_->IsAvailable();
		return json::Value::Object({ { "editorConnected", json::Value::Boolean(editor_connected) }, { "editorRequired", json::Value::Boolean(false) },
			{ "implementation", json::Value::String("WorldDataMCPHost.Native") }, { "independent", json::Value::Boolean(true) },
			{ "localToolCount", json::Value::Number(static_cast<double>(tool_definitions_.size())) }, { "success", json::Value::Boolean(true) },
			{ "transport", json::Value::String("stdio") } });
	}

	json::Value HostApplication::GetProjectInfo() const
	{
		const bool valid = project_file_service_->HasProject();
		const bool editor_connected = editor_gateway_ && editor_gateway_->IsAvailable();
		json::Value result = json::Value::Object({ { "editorConnected", json::Value::Boolean(editor_connected) },
			{ "projectFile", json::Value::String(project_file_service_->ProjectFile()) }, { "projectName", json::Value::String(project_file_service_->ProjectName()) },
			{ "projectRoot", json::Value::String(project_file_service_->ProjectRoot()) }, { "success", json::Value::Boolean(valid) } });
		if (!valid)
		{
			result.object["error"] = json::Value::String("No .uproject was supplied or found.");
		}
		return result;
	}

	json::Value HostApplication::ReadProjectFile(const json::Value& arguments) const
	{
		const json::Value* path = arguments.Find("path");
		const ReadFileResult result = project_file_service_->ReadTextFile(path ? path->StringOr() : std::string(), 2 * 1024 * 1024);
		json::Value payload = json::Value::Object({ { "success", json::Value::Boolean(result.success) } });
		if (!result.success)
		{
			payload.object["error"] = json::Value::String(result.error);
			return payload;
		}
		payload.object["content"] = json::Value::String(result.content);
		payload.object["path"] = json::Value::String(result.relative_path);
		payload.object["size"] = json::Value::Number(static_cast<double>(result.size));
		return payload;
	}

	json::Value HostApplication::ListProjectFiles(const json::Value& arguments) const
	{
		std::string directory = ".";
		std::string extension;
		bool recursive = true;
		int limit = 500;
		if (const json::Value* value = arguments.Find("directory"))
			directory = value->StringOr(".");
		if (const json::Value* value = arguments.Find("extension"))
			extension = value->StringOr();
		if (const json::Value* value = arguments.Find("recursive"))
			recursive = value->BooleanOr(true);
		if (const json::Value* value = arguments.Find("limit"))
			limit = std::clamp(static_cast<int>(value->NumberOr(500)), 1, 2000);
		const ListFilesResult result = project_file_service_->ListFiles(directory, extension, recursive, limit);
		if (!result.success)
		{
			return json::Value::Object({ { "error", json::Value::String(result.error) }, { "success", json::Value::Boolean(false) } });
		}
		std::vector<json::Value> files;
		for (const std::string& file : result.files)
		{
			files.push_back(json::Value::String(file));
		}
		return json::Value::Object({ { "directory", json::Value::String(result.directory) }, { "files", json::Value::Array(std::move(files)) },
			{ "matched", json::Value::Number(static_cast<double>(result.matched)) }, { "success", json::Value::Boolean(true) },
			{ "truncated", json::Value::Boolean(result.truncated) } });
	}

	json::Value HostApplication::ReplaceProjectText(const json::Value& arguments) const
	{
		const json::Value* path = arguments.Find("path");
		const json::Value* expected = arguments.Find("expectedText");
		const json::Value* replacement = arguments.Find("replacementText");
		const ReplaceTextResult result = project_file_service_->ReplaceSourceText(path ? path->StringOr() : std::string(), expected ? expected->StringOr() : std::string(),
			replacement ? replacement->StringOr() : std::string());
		json::Value payload = json::Value::Object({ { "success", json::Value::Boolean(result.success) } });
		if (!result.success)
		{
			payload.object["error"] = json::Value::String(result.error);
			return payload;
		}
		payload.object["path"] = json::Value::String(result.relative_path);
		payload.object["backupPath"] = json::Value::String(result.backup_relative_path);
		payload.object["replacements"] = json::Value::Number(static_cast<double>(result.replacements));
		return payload;
	}

	json::Value HostApplication::StartEditor(const json::Value& arguments) const
	{
		if (!editor_process_service_)
		{
			return json::Value::Object({ { "error", json::Value::String("编辑器进程服务不可用。") }, { "success", json::Value::Boolean(false) } });
		}
		return ProcessPayload(editor_process_service_->Start(ProcessRequest(arguments, false)));
	}

	json::Value HostApplication::StopEditor(const json::Value& arguments) const
	{
		if (!editor_process_service_)
		{
			return json::Value::Object({ { "error", json::Value::String("编辑器进程服务不可用。") }, { "success", json::Value::Boolean(false) } });
		}
		return ProcessPayload(editor_process_service_->Stop(ProcessRequest(arguments, true)));
	}

	json::Value HostApplication::RestartEditor(const json::Value& arguments) const
	{
		if (!editor_process_service_)
		{
			return json::Value::Object({ { "error", json::Value::String("编辑器进程服务不可用。") }, { "success", json::Value::Boolean(false) } });
		}
		return ProcessPayload(editor_process_service_->Restart(ProcessRequest(arguments, false)));
	}

	json::Value HostApplication::BuildProject(const json::Value& arguments) const
	{
		if (!editor_process_service_)
		{
			return json::Value::Object({ { "error", json::Value::String("编辑器进程服务不可用。") }, { "success", json::Value::Boolean(false) } });
		}
		return ProcessPayload(editor_process_service_->Build(ProcessRequest(arguments, true)));
	}

	json::Value HostApplication::ListResources(const json::Value& request) const
	{
		std::vector<json::Value> resources{ json::Value::Object(
			{ { "description", json::Value::String("Editor-independent project identity and host state.") }, { "mimeType", json::Value::String("application/json") },
				{ "name", json::Value::String("Standalone Project Info") }, { "uri", json::Value::String("worlddata-host://project/info") } }) };
		std::set<std::string> uris{ "worlddata-host://project/info" };
		if (editor_gateway_)
		{
			const EditorGatewayResult forwarded = editor_gateway_->Forward(request);
			const json::Value* result = forwarded.success ? forwarded.response.Find("result") : nullptr;
			const json::Value* remote_resources = result ? result->Find("resources") : nullptr;
			if (remote_resources && remote_resources->type == json::Type::Array)
			{
				for (const json::Value& resource : remote_resources->array)
				{
					const json::Value* uri = resource.Find("uri");
					if (uri && uris.insert(uri->StringOr()).second)
					{
						resources.push_back(resource);
					}
				}
			}
		}
		return Response(request, json::Value::Object({ { "resources", json::Value::Array(std::move(resources)) } }));
	}

	json::Value HostApplication::ReadResource(const json::Value& request) const
	{
		const json::Value* params = Params(request);
		const json::Value* uri = params ? params->Find("uri") : nullptr;
		if (!uri)
		{
			return Error(request, -32602, "Missing resources/read URI.");
		}
		if (uri->StringOr() != "worlddata-host://project/info")
		{
			if (editor_gateway_)
			{
				EditorGatewayResult forwarded = editor_gateway_->Forward(request);
				if (forwarded.success)
				{
					return forwarded.response;
				}
				return Error(request, -32001, forwarded.error);
			}
			return Error(request, -32602, "Unknown host resource.");
		}
		return Response(request,
			json::Value::Object({ { "contents",
				json::Value::Array({ json::Value::Object({ { "mimeType", json::Value::String("application/json") },
					{ "text", json::Value::String(json::Serialize(GetProjectInfo())) }, { "uri", json::Value::String(uri->string) } }) }) } }));
	}

	json::Value HostApplication::Response(const json::Value& request, json::Value result) const
	{
		json::Value response = json::Value::Object({ { "jsonrpc", json::Value::String("2.0") }, { "result", std::move(result) } });
		if (const json::Value* id = request.Find("id"))
		{
			response.object["id"] = *id;
		}
		return response;
	}

	json::Value HostApplication::Error(const json::Value& request, const int code, std::string message) const
	{
		json::Value response =
			json::Value::Object({ { "error", json::Value::Object({ { "code", json::Value::Number(code) }, { "message", json::Value::String(std::move(message)) } }) },
				{ "jsonrpc", json::Value::String("2.0") } });
		if (const json::Value* id = request.Find("id"))
		{
			response.object["id"] = *id;
		}
		return response;
	}

	json::Value HostApplication::ToolResult(json::Value payload, const bool is_error) const
	{
		return json::Value::Object(
			{ { "content", json::Value::Array({ json::Value::Object({ { "text", json::Value::String(json::Serialize(payload)) }, { "type", json::Value::String("text") } }) }) },
				{ "isError", json::Value::Boolean(is_error) } });
	}
}
