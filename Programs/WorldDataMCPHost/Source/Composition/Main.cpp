// Copyright ZhaoZining. All Rights Reserved.

#include "Adapters/Editor/WinHttpEditorGateway.h"
#include "Adapters/FileSystem/FileSystemProjectFileService.h"
#include "Adapters/Process/WindowsEditorProcessService.h"
#include "Application/HostApplication.h"
#include "Infrastructure/Stdio/StdioTransport.h"

#include <filesystem>
#include <memory>
#include <string>

namespace
{
	std::filesystem::path FindProjectFile(const int argument_count, wchar_t* arguments[])
	{
		for (int index = 1; index < argument_count; ++index)
		{
			const std::wstring argument(arguments[index]);
			const std::wstring project_prefix = L"--project=";
			const std::wstring unreal_prefix = L"-Project=";
			if (argument.rfind(project_prefix, 0) == 0)
			{
				return argument.substr(project_prefix.size());
			}
			if (argument.rfind(unreal_prefix, 0) == 0)
			{
				return argument.substr(unreal_prefix.size());
			}
			if (std::filesystem::path(argument).extension() == L".uproject")
			{
				return argument;
			}
		}

		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path(), std::filesystem::directory_options::skip_permission_denied, error))
		{
			if (entry.is_regular_file(error) && entry.path().extension() == L".uproject")
			{
				return entry.path();
			}
		}
		return {};
	}

	std::filesystem::path FindConnectionManifestOverride(const int argument_count, wchar_t* arguments[])
	{
		for (int index = 1; index < argument_count; ++index)
		{
			const std::wstring argument(arguments[index]);
			const std::wstring prefix = L"--connection-manifest=";
			if (argument.rfind(prefix, 0) == 0)
			{
				return argument.substr(prefix.size());
			}
		}
		return {};
	}
}

int wmain(const int argument_count, wchar_t* arguments[])
{
	const std::filesystem::path project_file = FindProjectFile(argument_count, arguments);
	auto project_file_service = std::make_shared<worlddata::host::FileSystemProjectFileService>(project_file);
	std::filesystem::path connection_manifest = FindConnectionManifestOverride(argument_count, arguments);
	if (connection_manifest.empty())
	{
		const std::filesystem::path project_directory = project_file.empty() ? std::filesystem::current_path() : project_file.parent_path();
		const std::filesystem::path manifest_directory = project_directory / "Saved" / "UnrealAgent";
		const std::filesystem::path recovery_manifest = manifest_directory / "mcp.recovered.json";
		connection_manifest = std::filesystem::exists(recovery_manifest) ? recovery_manifest : manifest_directory / "mcp.json";
	}
	auto editor_gateway = std::make_shared<worlddata::host::WinHttpEditorGateway>(connection_manifest);
	auto editor_process_service = std::make_shared<worlddata::host::WindowsEditorProcessService>(project_file, connection_manifest);
	worlddata::host::HostApplication application(project_file_service, editor_gateway, editor_process_service);
	return worlddata::host::RunStdio(application);
}
