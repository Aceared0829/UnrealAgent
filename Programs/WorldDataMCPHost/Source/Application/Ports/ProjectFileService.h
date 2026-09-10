// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace worlddata::host
{
	/** 读取项目文本文件后的稳定应用层结果。 */
	struct ReadFileResult
	{
		bool success = false;
		std::string error;
		std::string relative_path;
		std::string content;
		std::uintmax_t size = 0;
	};

	/** 枚举项目文件后的稳定应用层结果。 */
	struct ListFilesResult
	{
		bool success = false;
		std::string error;
		std::string directory;
		std::vector<std::string> files;
		std::size_t matched = 0;
		bool truncated = false;
	};

	struct ReplaceTextResult
	{
		bool success = false;
		std::string error;
		std::string relative_path;
		std::string backup_relative_path;
		std::size_t replacements = 0;
	};

	/**
	 * 项目文件访问端口。
	 *
	 * Application 只依赖该抽象，不感知具体文件系统与路径实现。
	 */
	class IProjectFileService
	{
	public:
		virtual ~IProjectFileService() = default;

		virtual bool HasProject() const = 0;
		virtual std::string ProjectName() const = 0;
		virtual std::string ProjectFile() const = 0;
		virtual std::string ProjectRoot() const = 0;
		virtual ReadFileResult ReadTextFile(const std::string& requested_path, std::uintmax_t maximum_bytes) const = 0;
		virtual ListFilesResult ListFiles(const std::string& directory, const std::string& extension, bool recursive, int limit) const = 0;
		virtual ReplaceTextResult ReplaceSourceText(const std::string& requested_path, const std::string& expected_text, const std::string& replacement_text) const = 0;
	};
}
