// Copyright ZhaoZining. All Rights Reserved.

#include "Adapters/Process/WindowsEditorProcessService.h"

#include "Core/Json/JsonValue.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace worlddata::host
{
	namespace
	{
		std::string ToUtf8(const std::wstring& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			std::string output(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
			return output;
		}

		std::wstring FromUtf8(const std::string& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
			if (size <= 0)
			{
				return {};
			}
			std::wstring output(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), size);
			return output;
		}

		std::string ReadUtf8File(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				return {};
			}
			return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		}

		std::filesystem::path Normalize(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
			return error ? path.lexically_normal() : normalized;
		}

		bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			std::wstring left_text = Normalize(left).wstring();
			std::wstring right_text = Normalize(right).wstring();
			std::transform(left_text.begin(), left_text.end(), left_text.begin(), ::towlower);
			std::transform(right_text.begin(), right_text.end(), right_text.begin(), ::towlower);
			return left_text == right_text;
		}

		std::wstring Quote(const std::filesystem::path& value)
		{
			std::wstring text = value.wstring();
			std::wstring escaped;
			escaped.reserve(text.size() + 2);
			escaped.push_back(L'"');
			for (const wchar_t character : text)
			{
				if (character == L'"')
				{
					escaped.push_back(L'\\');
				}
				escaped.push_back(character);
			}
			escaped.push_back(L'"');
			return escaped;
		}

		std::wstring QuoteText(const std::wstring& value)
		{
			return Quote(std::filesystem::path(value));
		}

		bool IsUnrealEditorProcess(const std::uint32_t process_id, HANDLE* output_handle = nullptr)
		{
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, process_id);
			if (!process)
			{
				return false;
			}
			wchar_t image_path[32768]{};
			DWORD image_size = static_cast<DWORD>(std::size(image_path));
			const bool valid = QueryFullProcessImageNameW(process, 0, image_path, &image_size) &&
				_wcsicmp(std::filesystem::path(image_path).filename().c_str(), L"UnrealEditor.exe") == 0 && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
			if (valid && output_handle)
			{
				*output_handle = process;
			}
			else
			{
				CloseHandle(process);
			}
			return valid;
		}

		struct ManifestProcess
		{
			std::uint32_t process_id = 0;
			std::filesystem::path project_file;
		};

		std::optional<ManifestProcess> ReadManifestProcess(const std::filesystem::path& manifest_path, const std::filesystem::path& expected_project, std::string& error)
		{
			const std::string text = ReadUtf8File(manifest_path);
			if (text.empty())
			{
				error = "未找到编辑器连接清单，无法安全确定目标进程。";
				return std::nullopt;
			}
			json::Value manifest;
			if (!json::Parse(text, manifest, error))
			{
				error = "编辑器连接清单格式无效：" + error;
				return std::nullopt;
			}
			const json::Value* pid_value = manifest.Find("pid");
			const json::Value* project_value = manifest.Find("uproject");
			if (!pid_value || !project_value)
			{
				error = "编辑器连接清单缺少 pid 或 uproject。";
				return std::nullopt;
			}
			const std::filesystem::path manifest_project = FromUtf8(project_value->StringOr());
			if (!SamePath(manifest_project, expected_project))
			{
				error = "连接清单属于其他 Unreal 项目，已拒绝操作。";
				return std::nullopt;
			}
			const auto process_id = static_cast<std::uint32_t>(pid_value->NumberOr());
			if (!process_id)
			{
				error = "编辑器连接清单中的 pid 无效。";
				return std::nullopt;
			}
			return ManifestProcess{ process_id, manifest_project };
		}

		std::wstring ReadRegistryString(HKEY root, const std::wstring& sub_key, const std::wstring& value_name)
		{
			wchar_t buffer[32768]{};
			DWORD size = sizeof(buffer);
			DWORD type = 0;
			if (RegGetValueW(root, sub_key.c_str(), value_name.empty() ? nullptr : value_name.c_str(), RRF_RT_REG_SZ, &type, buffer, &size) != ERROR_SUCCESS)
			{
				return {};
			}
			return buffer;
		}

		std::string ReadEngineAssociation(const std::filesystem::path& project_file)
		{
			json::Value project;
			std::string error;
			if (!json::Parse(ReadUtf8File(project_file), project, error))
			{
				return {};
			}
			const json::Value* association = project.Find("EngineAssociation");
			return association ? association->StringOr() : std::string();
		}

		bool HasEditorExecutable(const std::filesystem::path& root)
		{
			return std::filesystem::is_regular_file(root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe");
		}

		std::filesystem::path FindBuildExecutable(const std::filesystem::path& engine_root)
		{
			const std::vector<std::filesystem::path> candidates{ engine_root / "Engine" / "Binaries" / "DotNET" / "UnrealBuildTool" / "UnrealBuildTool.exe",
				engine_root / "Engine" / "Binaries" / "DotNET" / "UnrealBuildTool.exe" };
			for (const auto& candidate : candidates)
			{
				if (std::filesystem::is_regular_file(candidate))
				{
					return candidate;
				}
			}
			return {};
		}

		bool ValidateToken(const std::string& value, const std::vector<std::string>& allowed)
		{
			return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
		}

		std::filesystem::path MakeBuildLogPath(const std::filesystem::path& project_file)
		{
			const auto ticks = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			return project_file.parent_path() / "Saved" / "Logs" / ("UnrealAgent-Host-Build-" + std::to_string(ticks) + ".log");
		}

		std::string WindowsError(const DWORD code)
		{
			wchar_t* buffer = nullptr;
			const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
			std::wstring message = length && buffer ? std::wstring(buffer, length) : L"未知系统错误";
			if (buffer)
			{
				LocalFree(buffer);
			}
			while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
			{
				message.pop_back();
			}
			return ToUtf8(message);
		}

		BOOL CALLBACK CloseEditorWindow(const HWND window, const LPARAM parameter)
		{
			DWORD process_id = 0;
			GetWindowThreadProcessId(window, &process_id);
			if (process_id == static_cast<DWORD>(parameter) && GetWindow(window, GW_OWNER) == nullptr)
			{
				PostMessageW(window, WM_CLOSE, 0, 0);
			}
			return TRUE;
		}

		EditorProcessResult LaunchProcess(const std::filesystem::path& executable, const std::wstring& arguments, const bool hidden, const bool wait,
			const std::filesystem::path& log_path)
		{
			EditorProcessResult result;
			result.executable = ToUtf8(executable.wstring());
			result.arguments = ToUtf8(arguments);
			result.log_path = ToUtf8(log_path.wstring());

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			HANDLE log = INVALID_HANDLE_VALUE;
			if (!log_path.empty())
			{
				std::error_code error;
				std::filesystem::create_directories(log_path.parent_path(), error);
				log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (log == INVALID_HANDLE_VALUE)
				{
					result.error = "无法创建构建日志：" + WindowsError(GetLastError());
					return result;
				}
			}

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			if (hidden)
			{
				startup.dwFlags |= STARTF_USESHOWWINDOW;
				startup.wShowWindow = SW_HIDE;
			}
			if (log != INVALID_HANDLE_VALUE)
			{
				startup.dwFlags |= STARTF_USESTDHANDLES;
				startup.hStdOutput = log;
				startup.hStdError = log;
				startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			}
			PROCESS_INFORMATION process{};
			std::wstring command_line = Quote(executable) + (arguments.empty() ? L"" : L" " + arguments);
			std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
			mutable_command.push_back(L'\0');
			const DWORD flags = hidden ? CREATE_NO_WINDOW : CREATE_NEW_PROCESS_GROUP;
			const BOOL created = CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE, flags, nullptr,
				executable.parent_path().c_str(), &startup, &process);
			if (log != INVALID_HANDLE_VALUE)
			{
				CloseHandle(log);
			}
			if (!created)
			{
				result.error = "启动进程失败：" + WindowsError(GetLastError());
				return result;
			}

			CloseHandle(process.hThread);
			result.success = true;
			result.launched = true;
			result.running = true;
			result.process_id = process.dwProcessId;
			if (wait)
			{
				WaitForSingleObject(process.hProcess, INFINITE);
				DWORD exit_code = 0;
				GetExitCodeProcess(process.hProcess, &exit_code);
				result.exit_code = exit_code;
				result.completed = true;
				result.running = false;
				result.success = exit_code == 0;
				if (!result.success)
				{
					result.error = "进程返回非零退出码：" + std::to_string(exit_code);
				}
			}
			CloseHandle(process.hProcess);
			return result;
		}
	}

	WindowsEditorProcessService::WindowsEditorProcessService(std::filesystem::path project_file, std::filesystem::path connection_manifest)
		: project_file_(Normalize(std::move(project_file))), connection_manifest_(Normalize(std::move(connection_manifest)))
	{
	}

	std::filesystem::path WindowsEditorProcessService::FindEngineRoot(std::string& error) const
	{
		const std::string association = ReadEngineAssociation(project_file_);
		if (association.empty())
		{
			error = "项目文件缺少有效的 EngineAssociation。";
			return {};
		}
		std::vector<std::filesystem::path> candidates;
		wchar_t environment[32768]{};
		const DWORD environment_size = GetEnvironmentVariableW(L"UE_ENGINE_ROOT", environment, static_cast<DWORD>(std::size(environment)));
		if (environment_size > 0 && environment_size < std::size(environment))
		{
			candidates.emplace_back(environment);
		}

		const std::wstring association_wide = FromUtf8(association);
		const std::wstring custom_build = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Epic Games\\Unreal Engine\\Builds", association_wide);
		if (!custom_build.empty())
		{
			candidates.emplace_back(custom_build);
		}
		const std::wstring installed_build = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EpicGames\\Unreal Engine\\" + association_wide, L"InstalledDirectory");
		if (!installed_build.empty())
		{
			candidates.emplace_back(installed_build);
		}

		const DWORD drives = GetLogicalDrives();
		for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
		{
			if ((drives & (1u << (letter - L'A'))) == 0)
			{
				continue;
			}
			std::wstring root;
			root.push_back(letter);
			root += L":\\UE_";
			root += association_wide;
			candidates.emplace_back(root);
		}
		for (const auto& candidate : candidates)
		{
			if (HasEditorExecutable(candidate))
			{
				return Normalize(candidate);
			}
		}
		error = "未找到与 EngineAssociation " + association + " 对应的 Unreal Engine 安装目录。";
		return {};
	}

	EditorProcessResult WindowsEditorProcessService::Start(const EditorProcessRequest& request) const
	{
		EditorProcessResult result;
		result.dry_run = request.dry_run;
		if (!std::filesystem::is_regular_file(project_file_))
		{
			result.error = "未找到有效的 .uproject 文件。";
			return result;
		}
		std::string manifest_error;
		const auto manifest = ReadManifestProcess(connection_manifest_, project_file_, manifest_error);
		if (manifest && IsUnrealEditorProcess(manifest->process_id))
		{
			result.success = true;
			result.running = true;
			result.process_id = manifest->process_id;
			return result;
		}

		std::string error;
		const std::filesystem::path engine_root = FindEngineRoot(error);
		if (engine_root.empty())
		{
			result.error = std::move(error);
			return result;
		}
		const std::filesystem::path executable = engine_root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe";
		std::wstring arguments = Quote(project_file_);
		if (!request.extra_arguments.empty())
		{
			const std::wstring extra = FromUtf8(request.extra_arguments);
			if (extra.find_first_of(L"\r\n") != std::wstring::npos)
			{
				result.error = "extraArguments 不允许包含换行符。";
				return result;
			}
			arguments += L" " + extra;
		}
		result.executable = ToUtf8(executable.wstring());
		result.arguments = ToUtf8(arguments);
		if (request.dry_run)
		{
			result.success = true;
			return result;
		}
		return LaunchProcess(executable, arguments, false, false, {});
	}

	EditorProcessResult WindowsEditorProcessService::Stop(const EditorProcessRequest& request) const
	{
		EditorProcessResult result;
		result.dry_run = request.dry_run;
		std::string error;
		const auto manifest = ReadManifestProcess(connection_manifest_, project_file_, error);
		if (!manifest)
		{
			if (!std::filesystem::exists(connection_manifest_))
			{
				result.success = true;
				result.stopped = true;
				result.completed = true;
				return result;
			}
			result.error = std::move(error);
			return result;
		}
		HANDLE process = nullptr;
		if (!IsUnrealEditorProcess(manifest->process_id, &process))
		{
			result.success = true;
			result.stopped = true;
			result.process_id = manifest->process_id;
			return result;
		}
		result.process_id = manifest->process_id;
		result.running = true;
		if (request.dry_run)
		{
			result.success = true;
			CloseHandle(process);
			return result;
		}

		EnumWindows(CloseEditorWindow, static_cast<LPARAM>(manifest->process_id));
		const DWORD timeout = static_cast<DWORD>(std::clamp(request.timeout_seconds, 1, 120) * 1000);
		DWORD wait_result = WaitForSingleObject(process, timeout);
		if (wait_result == WAIT_TIMEOUT && request.force)
		{
			if (!TerminateProcess(process, 0))
			{
				result.error = "强制结束编辑器失败：" + WindowsError(GetLastError());
				CloseHandle(process);
				return result;
			}
			wait_result = WaitForSingleObject(process, 5000);
		}
		CloseHandle(process);
		if (wait_result != WAIT_OBJECT_0)
		{
			result.error = "编辑器未在超时时间内退出；可使用 force=true 强制结束。";
			return result;
		}
		result.success = true;
		result.running = false;
		result.stopped = true;
		result.completed = true;
		return result;
	}

	EditorProcessResult WindowsEditorProcessService::Restart(const EditorProcessRequest& request) const
	{
		EditorProcessResult stopped = Stop(request);
		if (!stopped.success)
		{
			return stopped;
		}
		if (request.dry_run)
		{
			std::string error;
			const std::filesystem::path engine_root = FindEngineRoot(error);
			if (engine_root.empty())
			{
				stopped.success = false;
				stopped.error = std::move(error);
				return stopped;
			}
			stopped.executable = ToUtf8((engine_root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe").wstring());
			stopped.arguments = ToUtf8(Quote(project_file_));
			return stopped;
		}
		return Start(request);
	}

	EditorProcessResult WindowsEditorProcessService::Build(const EditorProcessRequest& request) const
	{
		EditorProcessResult result;
		result.dry_run = request.dry_run;
		if (!ValidateToken(request.configuration, { "Debug", "DebugGame", "Development", "Shipping", "Test" }))
		{
			result.error = "configuration 参数不受支持。";
			return result;
		}
		if (!ValidateToken(request.platform, { "Win64", "Linux", "Mac" }))
		{
			result.error = "platform 参数不受支持。";
			return result;
		}
		std::string error;
		const std::filesystem::path engine_root = FindEngineRoot(error);
		if (engine_root.empty())
		{
			result.error = std::move(error);
			return result;
		}
		const std::filesystem::path executable = FindBuildExecutable(engine_root);
		if (executable.empty())
		{
			result.error = "引擎目录中未找到 UnrealBuildTool。";
			return result;
		}
		const std::string project_name = project_file_.stem().string();
		std::wstring arguments = FromUtf8(project_name + "Editor") + L" " + FromUtf8(request.platform) + L" " + FromUtf8(request.configuration) + L" -Project=" +
			Quote(project_file_) + L" -WaitMutex -FromMsBuild";
		if (request.clean)
		{
			arguments += L" -Clean";
		}
		const std::filesystem::path log_path = MakeBuildLogPath(project_file_);
		result.executable = ToUtf8(executable.wstring());
		result.arguments = ToUtf8(arguments);
		result.log_path = ToUtf8(log_path.wstring());
		if (request.dry_run)
		{
			result.success = true;
			return result;
		}
		return LaunchProcess(executable, arguments, true, request.wait, log_path);
	}
}
