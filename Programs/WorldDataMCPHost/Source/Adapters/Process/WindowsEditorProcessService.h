// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "Application/Ports/EditorProcessService.h"

#include <filesystem>

namespace worlddata::host
{
	/**
	 * Windows 下的 Unreal Editor 进程适配器。
	 *
	 * 该实现只依赖操作系统和项目描述文件，不读取引擎 Toolset 源码，
	 * 也不依赖任何第三方 MCP 文件。
	 */
	class WindowsEditorProcessService final : public IEditorProcessService
	{
	public:
		WindowsEditorProcessService(std::filesystem::path project_file, std::filesystem::path connection_manifest);

		EditorProcessResult Start(const EditorProcessRequest& request) const override;
		EditorProcessResult Stop(const EditorProcessRequest& request) const override;
		EditorProcessResult Restart(const EditorProcessRequest& request) const override;
		EditorProcessResult Build(const EditorProcessRequest& request) const override;

	private:
		std::filesystem::path FindEngineRoot(std::string& error) const;
		std::filesystem::path project_file_;
		std::filesystem::path connection_manifest_;
	};
}
