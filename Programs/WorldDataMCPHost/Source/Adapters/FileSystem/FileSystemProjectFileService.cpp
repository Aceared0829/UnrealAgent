// Copyright ZhaoZining. All Rights Reserved.

#include "Adapters/FileSystem/FileSystemProjectFileService.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <chrono>

namespace worlddata::host
{
	namespace
	{
		std::wstring LowerPath(std::filesystem::path path)
		{
			std::wstring value = path.lexically_normal().wstring();
			std::transform(value.begin(), value.end(), value.begin(),
				[](const wchar_t character)
				{
					return static_cast<wchar_t>(std::towlower(character));
				});
			while (!value.empty() && (value.back() == L'/' || value.back() == L'\\'))
			{
				value.pop_back();
			}
			return value;
		}

		bool IsInside(const std::filesystem::path& root, const std::filesystem::path& candidate)
		{
			const std::wstring root_text = LowerPath(root);
			const std::wstring candidate_text = LowerPath(candidate);
			if (candidate_text == root_text)
			{
				return true;
			}
			if (candidate_text.size() <= root_text.size() || candidate_text.compare(0, root_text.size(), root_text) != 0)
			{
				return false;
			}
			const wchar_t separator = candidate_text[root_text.size()];
			return separator == L'/' || separator == L'\\';
		}

		std::string GenericUtf8(const std::filesystem::path& path)
		{
			return path.generic_u8string();
		}

		char LowerAscii(const char character)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
	}

	FileSystemProjectFileService::FileSystemProjectFileService(std::filesystem::path project_file) : project_file_(std::move(project_file))
	{
		std::error_code error;
		if (!project_file_.empty())
		{
			project_file_ = std::filesystem::weakly_canonical(project_file_, error);
			if (error)
			{
				project_file_.clear();
			}
		}
		project_root_ = project_file_.empty() ? std::filesystem::current_path() : project_file_.parent_path();
		project_root_ = std::filesystem::weakly_canonical(project_root_, error);
	}

	bool FileSystemProjectFileService::HasProject() const
	{
		return !project_file_.empty();
	}

	std::string FileSystemProjectFileService::ProjectName() const
	{
		return HasProject() ? project_file_.stem().u8string() : std::string();
	}

	std::string FileSystemProjectFileService::ProjectFile() const
	{
		return HasProject() ? GenericUtf8(project_file_) : std::string();
	}

	std::string FileSystemProjectFileService::ProjectRoot() const
	{
		return GenericUtf8(project_root_);
	}

	std::optional<std::filesystem::path> FileSystemProjectFileService::Resolve(const std::string& requested_path) const
	{
		if (requested_path.empty())
		{
			return std::nullopt;
		}
		std::error_code error;
		std::filesystem::path candidate = std::filesystem::u8path(requested_path);
		if (candidate.is_relative())
		{
			candidate = project_root_ / candidate;
		}
		candidate = std::filesystem::weakly_canonical(candidate, error);
		if (error || !IsInside(project_root_, candidate))
		{
			return std::nullopt;
		}
		return candidate;
	}

	ReadFileResult FileSystemProjectFileService::ReadTextFile(const std::string& requested_path, const std::uintmax_t maximum_bytes) const
	{
		ReadFileResult result;
		const auto resolved = Resolve(requested_path);
		if (!resolved)
		{
			result.error = "Missing path or path escapes the project root.";
			return result;
		}

		std::error_code error;
		result.size = std::filesystem::file_size(*resolved, error);
		if (error)
		{
			result.error = "File does not exist.";
			return result;
		}
		if (result.size > maximum_bytes)
		{
			result.error = "File exceeds the configured text-read limit.";
			return result;
		}

		std::ifstream stream(*resolved, std::ios::binary);
		result.content = std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		if (!stream.good() && !stream.eof())
		{
			result.content.clear();
			result.error = "Unable to read file.";
			return result;
		}

		result.relative_path = GenericUtf8(std::filesystem::relative(*resolved, project_root_));
		result.success = true;
		return result;
	}

	ListFilesResult FileSystemProjectFileService::ListFiles(const std::string& directory, const std::string& extension, const bool recursive, const int limit) const
	{
		ListFilesResult result;
		result.directory = directory.empty() ? "." : directory;
		const auto resolved = Resolve(result.directory);
		if (!resolved || !std::filesystem::is_directory(*resolved))
		{
			result.error = "Directory does not exist or escapes the project root.";
			return result;
		}

		std::string expected_extension = extension;
		if (!expected_extension.empty() && expected_extension.front() == '.')
		{
			expected_extension.erase(0, 1);
		}
		std::transform(expected_extension.begin(), expected_extension.end(), expected_extension.begin(), LowerAscii);

		std::vector<std::filesystem::path> matches;
		std::error_code error;
		auto consider = [&](const std::filesystem::directory_entry& entry)
		{
			if (!entry.is_regular_file(error))
			{
				return;
			}
			if (!expected_extension.empty())
			{
				std::string actual = entry.path().extension().u8string();
				if (!actual.empty() && actual.front() == '.')
					actual.erase(0, 1);
				std::transform(actual.begin(), actual.end(), actual.begin(), LowerAscii);
				if (actual != expected_extension)
				{
					return;
				}
			}
			matches.push_back(entry.path());
		};

		if (recursive)
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(*resolved, std::filesystem::directory_options::skip_permission_denied, error))
			{
				consider(entry);
			}
		}
		else
		{
			for (const auto& entry : std::filesystem::directory_iterator(*resolved, std::filesystem::directory_options::skip_permission_denied, error))
			{
				consider(entry);
			}
		}
		std::sort(matches.begin(), matches.end());
		result.matched = matches.size();
		result.truncated = matches.size() > static_cast<std::size_t>(limit);
		for (int index = 0; index < static_cast<int>(matches.size()) && index < limit; ++index)
		{
			result.files.push_back(GenericUtf8(std::filesystem::relative(matches[index], project_root_)));
		}
		result.success = true;
		return result;
	}

	ReplaceTextResult FileSystemProjectFileService::ReplaceSourceText(const std::string& requested_path, const std::string& expected_text,
		const std::string& replacement_text) const
	{
		ReplaceTextResult result;
		const auto resolved = Resolve(requested_path);
		if (!resolved || expected_text.empty())
		{
			result.error = "Missing source path/expected text or path escapes the project root.";
			return result;
		}
		const std::string relative = GenericUtf8(std::filesystem::relative(*resolved, project_root_));
		std::string normalized = relative;
		std::replace(normalized.begin(), normalized.end(), '\\', '/');
		std::string lowered = normalized;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), LowerAscii);
		const bool project_source = lowered.rfind("source/", 0) == 0;
		const bool plugin_source = lowered.rfind("plugins/", 0) == 0 && lowered.find("/source/") != std::string::npos;
		std::string extension = resolved->extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(), LowerAscii);
		if ((!project_source && !plugin_source) || (extension != ".h" && extension != ".hpp" && extension != ".cpp" && extension != ".inl" && extension != ".cs"))
		{
			result.error = "Recovery writes are limited to project or plugin source files.";
			return result;
		}
		const ReadFileResult current = ReadTextFile(requested_path, 2 * 1024 * 1024);
		if (!current.success)
		{
			result.error = current.error;
			return result;
		}
		const std::size_t first = current.content.find(expected_text);
		if (first == std::string::npos || current.content.find(expected_text, first + expected_text.size()) != std::string::npos)
		{
			result.error = "Expected text must match exactly once; the file changed or the patch is ambiguous.";
			return result;
		}
		std::string updated = current.content;
		updated.replace(first, expected_text.size(), replacement_text);

		const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		const std::filesystem::path backup = project_root_ / "Saved" / "UnrealAgent" / "Recovery" / std::to_string(stamp) / std::filesystem::u8path(normalized);
		std::error_code error;
		std::filesystem::create_directories(backup.parent_path(), error);
		if (error || !std::filesystem::copy_file(*resolved, backup, std::filesystem::copy_options::overwrite_existing, error))
		{
			result.error = "Unable to create the recovery backup.";
			return result;
		}
		const std::filesystem::path staged = resolved->string() + ".uebridge-repair-stage";
		{
			std::ofstream stream(staged, std::ios::binary | std::ios::trunc);
			stream.write(updated.data(), static_cast<std::streamsize>(updated.size()));
			if (!stream.good())
			{
				std::filesystem::remove(staged, error);
				result.error = "Unable to stage the repaired source file.";
				return result;
			}
		}
		const std::filesystem::path rollback = resolved->string() + ".uebridge-repair-original";
		std::filesystem::remove(rollback, error);
		error.clear();
		std::filesystem::rename(*resolved, rollback, error);
		if (error)
		{
			std::filesystem::remove(staged, error);
			result.error = "Unable to prepare the source file for atomic replacement.";
			return result;
		}
		std::filesystem::rename(staged, *resolved, error);
		if (error)
		{
			std::error_code restore_error;
			std::filesystem::rename(rollback, *resolved, restore_error);
			result.error = "Unable to install the repaired source file; the original was restored.";
			return result;
		}
		std::filesystem::remove(rollback, error);
		result.success = true;
		result.relative_path = normalized;
		result.backup_relative_path = GenericUtf8(std::filesystem::relative(backup, project_root_));
		result.replacements = 1;
		return result;
	}
}
