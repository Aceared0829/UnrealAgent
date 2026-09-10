// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

namespace worlddata::host
{
	/** 编辑器进程用例的统一请求。 */
	struct EditorProcessRequest
	{
		bool dry_run = false;
		bool force = false;
		bool clean = false;
		bool wait = true;
		int timeout_seconds = 15;
		std::string configuration = "Development";
		std::string platform = "Win64";
		std::string extra_arguments;
	};

	/** 编辑器进程用例的统一结果。 */
	struct EditorProcessResult
	{
		bool success = false;
		bool dry_run = false;
		bool running = false;
		bool launched = false;
		bool stopped = false;
		bool completed = false;
		std::uint32_t process_id = 0;
		std::uint32_t exit_code = 0;
		std::string executable;
		std::string arguments;
		std::string log_path;
		std::string error;
	};

	/**
	 * 编辑器与构建进程边界。
	 *
	 * Application 层只编排进程用例，Windows API、引擎定位和进程校验
	 * 全部由 Adapter 层实现。
	 */
	class IEditorProcessService
	{
	public:
		virtual ~IEditorProcessService() = default;

		virtual EditorProcessResult Start(const EditorProcessRequest& request) const = 0;
		virtual EditorProcessResult Stop(const EditorProcessRequest& request) const = 0;
		virtual EditorProcessResult Restart(const EditorProcessRequest& request) const = 0;
		virtual EditorProcessResult Build(const EditorProcessRequest& request) const = 0;
	};
}
