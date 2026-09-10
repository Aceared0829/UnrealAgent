// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "Application/Ports/ProjectFileService.h"

#include <filesystem>
#include <optional>

namespace worlddata::host
{
	/** 基于本机文件系统实现项目文件访问端口，并强制限定访问范围。 */
	class FileSystemProjectFileService final : public IProjectFileService
	{
	public:
		explicit FileSystemProjectFileService(std::filesystem::path project_file);

		bool HasProject() const override;
		std::string ProjectName() const override;
		std::string ProjectFile() const override;
		std::string ProjectRoot() const override;
		ReadFileResult ReadTextFile(const std::string& requested_path, std::uintmax_t maximum_bytes) const override;
		ListFilesResult ListFiles(const std::string& directory, const std::string& extension, bool recursive, int limit) const override;
		ReplaceTextResult ReplaceSourceText(const std::string& requested_path, const std::string& expected_text, const std::string& replacement_text) const override;

	private:
		/** 解析并校验项目相对路径，拒绝逃逸项目根目录的请求。 */
		std::optional<std::filesystem::path> Resolve(const std::string& requested_path) const;

		std::filesystem::path project_file_;
		std::filesystem::path project_root_;
	};
}
