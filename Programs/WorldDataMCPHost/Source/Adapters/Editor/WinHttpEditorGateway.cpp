// Copyright ZhaoZining. All Rights Reserved.

#include "Adapters/Editor/WinHttpEditorGateway.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace worlddata::host
{
	namespace
	{
		/** 在所有返回路径上关闭 WinHTTP 句柄。 */
		class WinHttpHandle
		{
		public:
			explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle)
			{
			}

			~WinHttpHandle()
			{
				if (handle_)
				{
					WinHttpCloseHandle(handle_);
				}
			}

			WinHttpHandle(const WinHttpHandle&) = delete;
			WinHttpHandle& operator=(const WinHttpHandle&) = delete;

			HINTERNET Get() const
			{
				return handle_;
			}

			explicit operator bool() const
			{
				return handle_ != nullptr;
			}

		private:
			HINTERNET handle_;
		};

		struct ConnectionSettings
		{
			std::wstring host;
			std::wstring path;
			INTERNET_PORT port = 0;
			bool secure = false;
			std::wstring token_header;
			std::wstring token;
		};

		struct HttpRequestResult
		{
			bool success = false;
			bool request_may_have_executed = false;
			json::Value response;
			std::string error;
			std::wstring response_session_id;
		};

		EditorGatewayResult MoveToGatewayResult(HttpRequestResult result)
		{
			EditorGatewayResult gateway_result;
			gateway_result.success = result.success;
			gateway_result.response = std::move(result.response);
			gateway_result.error = std::move(result.error);
			return gateway_result;
		}

		std::wstring Utf8ToWide(const std::string& value)
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
			std::wstring converted(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), converted.data(), size);
			return converted;
		}

		std::wstring LowerAscii(std::wstring value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](const wchar_t character)
				{
					return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a') : character;
				});
			return value;
		}

		bool IsLoopbackHost(const std::wstring& host)
		{
			const std::wstring lowered = LowerAscii(host);
			return lowered == L"127.0.0.1" || lowered == L"localhost" || lowered == L"::1" || lowered == L"[::1]";
		}

		bool IsSafeHeaderName(const std::string& name)
		{
			return !name.empty() &&
				std::all_of(name.begin(), name.end(),
					[](const unsigned char character)
					{
						return std::isalnum(character) || character == '-';
					});
		}

		bool LoadConnection(const std::filesystem::path& connection_file, ConnectionSettings& out_settings, std::string& out_error)
		{
			std::ifstream stream(connection_file, std::ios::binary);
			if (!stream)
			{
				out_error = "Editor connection manifest is unavailable.";
				return false;
			}
			const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
			json::Value root;
			if (!json::Parse(text, root, out_error) || root.type != json::Type::Object)
			{
				out_error = "Editor connection manifest is invalid: " + out_error;
				return false;
			}
			const json::Value* running_value = root.Find("running");
			const json::Value* state_value = root.Find("state");
			if ((running_value && !running_value->BooleanOr(false)) || (state_value && state_value->StringOr() != "listening"))
			{
				out_error = "Editor connection manifest reports that MCP is not listening.";
				return false;
			}

			const json::Value* url_value = root.Find("url");
			const json::Value* header_value = root.Find("accessTokenHeader");
			const json::Value* token_value = root.Find("accessToken");
			const std::string url = url_value ? url_value->StringOr() : std::string();
			const std::string header = header_value ? header_value->StringOr() : std::string();
			const std::string token = token_value ? token_value->StringOr() : std::string();
			if (url.empty() || !IsSafeHeaderName(header) || token.empty())
			{
				out_error = "Editor connection manifest is incomplete.";
				return false;
			}

			const std::wstring wide_url = Utf8ToWide(url);
			URL_COMPONENTS components{};
			components.dwStructSize = sizeof(components);
			components.dwSchemeLength = static_cast<DWORD>(-1);
			components.dwHostNameLength = static_cast<DWORD>(-1);
			components.dwUrlPathLength = static_cast<DWORD>(-1);
			components.dwExtraInfoLength = static_cast<DWORD>(-1);
			if (wide_url.empty() || !WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &components))
			{
				out_error = "Editor connection URL is invalid.";
				return false;
			}

			out_settings.host.assign(components.lpszHostName, components.dwHostNameLength);
			if (!IsLoopbackHost(out_settings.host))
			{
				out_error = "Editor connection URL must use a loopback host.";
				return false;
			}
			out_settings.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
			if (components.dwExtraInfoLength > 0)
			{
				out_settings.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
			}
			if (out_settings.path.empty())
			{
				out_settings.path = L"/";
			}
			out_settings.port = components.nPort;
			out_settings.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
			out_settings.token_header = Utf8ToWide(header);
			out_settings.token = Utf8ToWide(token);
			return true;
		}

		HttpRequestResult SendRequest(const ConnectionSettings& settings, const json::Value& request_value, const std::wstring& session_id = {},
			const std::wstring& http_method = L"POST", const bool expect_json_response = true)
		{
			HttpRequestResult result;
			WinHttpHandle session(WinHttpOpen(L"WorldDataMCPHost/0.3.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
			if (!session)
			{
				result.error = "Unable to create WinHTTP session.";
				return result;
			}
			WinHttpSetTimeouts(session.Get(), 500, 500, 1000, 2000);

			WinHttpHandle connection(WinHttpConnect(session.Get(), settings.host.c_str(), settings.port, 0));
			if (!connection)
			{
				result.error = "Unable to connect to Unreal Editor.";
				return result;
			}

			const DWORD flags = settings.secure ? WINHTTP_FLAG_SECURE : 0;
			WinHttpHandle request(
				WinHttpOpenRequest(connection.Get(), http_method.c_str(), settings.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
			if (!request)
			{
				result.error = "Unable to create Editor HTTP request.";
				return result;
			}

			const std::wstring headers = L"Content-Type: application/json\r\n"
										 L"MCP-Protocol-Version: 2025-06-18\r\n" +
				settings.token_header + L": " + settings.token + L"\r\n" + (session_id.empty() ? std::wstring() : L"MCP-Session-Id: " + session_id + L"\r\n");
			std::string body = json::Serialize(request_value);
			if (!WinHttpSendRequest(request.Get(), headers.c_str(), static_cast<DWORD>(headers.size()), body.data(), static_cast<DWORD>(body.size()),
					static_cast<DWORD>(body.size()), 0))
			{
				result.error = "Unreal Editor MCP endpoint is unavailable.";
				return result;
			}
			if (!WinHttpReceiveResponse(request.Get(), nullptr))
			{
				result.request_may_have_executed = true;
				result.error = "Unreal Editor MCP response was not received.";
				return result;
			}

			DWORD status_code = 0;
			DWORD status_size = sizeof(status_code);
			if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
					WINHTTP_NO_HEADER_INDEX) ||
				(status_code != 200 && !(status_code == 202 && !expect_json_response)))
			{
				result.error = "Unreal Editor MCP endpoint rejected the request.";
				return result;
			}
			result.request_may_have_executed = true;

			DWORD session_header_size = 0;
			WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_CUSTOM, L"MCP-Session-Id", WINHTTP_NO_OUTPUT_BUFFER, &session_header_size, WINHTTP_NO_HEADER_INDEX);
			if (session_header_size > sizeof(wchar_t))
			{
				std::wstring response_session(session_header_size / sizeof(wchar_t), L'\0');
				if (WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_CUSTOM, L"MCP-Session-Id", response_session.data(), &session_header_size, WINHTTP_NO_HEADER_INDEX))
				{
					response_session.resize(session_header_size / sizeof(wchar_t));
					while (!response_session.empty() && response_session.back() == L'\0')
					{
						response_session.pop_back();
					}
					result.response_session_id = std::move(response_session);
				}
			}

			std::string response_body;
			for (;;)
			{
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(request.Get(), &available))
				{
					result.error = "Unable to read Editor MCP response.";
					return result;
				}
				if (available == 0)
				{
					break;
				}
				const std::size_t offset = response_body.size();
				response_body.resize(offset + available);
				DWORD read = 0;
				if (!WinHttpReadData(request.Get(), response_body.data() + offset, available, &read))
				{
					result.error = "Unable to read Editor MCP response.";
					return result;
				}
				response_body.resize(offset + read);
			}

			if (!expect_json_response)
			{
				result.response = json::Value::Object();
				result.success = true;
				return result;
			}
			if (!json::Parse(response_body, result.response, result.error))
			{
				result.error = "Editor MCP response is invalid JSON: " + result.error;
				return result;
			}
			result.success = true;
			return result;
		}

		EditorGatewayResult ForwardSingleSession(const ConnectionSettings& settings, const json::Value& request)
		{
			const json::Value initialize = json::Value::Object(
				{ { "id", json::Value::String("worlddata-host-initialize") }, { "jsonrpc", json::Value::String("2.0") }, { "method", json::Value::String("initialize") },
					{ "params",
						json::Value::Object({ { "capabilities", json::Value::Object() },
							{ "clientInfo",
								json::Value::Object({ { "name", json::Value::String("WorldDataMCPHost") }, { "title", json::Value::String("Unreal Agent Host") },
									{ "version", json::Value::String("0.3.0") } }) },
							{ "protocolVersion", json::Value::String("2025-06-18") } }) } });
			HttpRequestResult initialized = SendRequest(settings, initialize);
			if (!initialized.success || initialized.response_session_id.empty())
			{
				initialized.success = false;
				if (initialized.error.empty())
				{
					initialized.error = "Unreal Editor did not establish an MCP session.";
				}
				return MoveToGatewayResult(std::move(initialized));
			}

			const std::wstring session_id = initialized.response_session_id;
			const json::Value ready = json::Value::Object(
				{ { "jsonrpc", json::Value::String("2.0") }, { "method", json::Value::String("notifications/initialized") }, { "params", json::Value::Object() } });
			HttpRequestResult notification = SendRequest(settings, ready, session_id, L"POST", false);
			if (!notification.success)
			{
				return MoveToGatewayResult(std::move(notification));
			}

			HttpRequestResult forwarded = SendRequest(settings, request, session_id);
			const HttpRequestResult closed = SendRequest(settings, json::Value::Object(), session_id, L"DELETE", false);
			const bool request_outcome_unknown = !forwarded.success && forwarded.request_may_have_executed;
			EditorGatewayResult gateway_result = MoveToGatewayResult(std::move(forwarded));
			gateway_result.request_outcome_unknown = request_outcome_unknown;
			if (!closed.success)
			{
				gateway_result.cleanup_warning = "Unable to close Unreal Editor MCP session: " + closed.error;
				if (gateway_result.success)
				{
					json::Value* result = gateway_result.response.Find("result");
					if (result && result->type == json::Type::Object)
					{
						json::Value& metadata = result->object["_meta"];
						if (metadata.type != json::Type::Object)
						{
							metadata = json::Value::Object();
						}
						metadata.object["worlddataSessionCleanupWarning"] = json::Value::String(gateway_result.cleanup_warning);
					}
				}
			}
			return gateway_result;
		}
	}

	WinHttpEditorGateway::WinHttpEditorGateway(std::filesystem::path connection_file) : connection_file_(std::move(connection_file))
	{
	}

	bool WinHttpEditorGateway::IsAvailable() const
	{
		const json::Value probe = json::Value::Object({ { "id", json::Value::String("worlddata-host-probe") }, { "jsonrpc", json::Value::String("2.0") },
			{ "method", json::Value::String("ping") }, { "params", json::Value::Object() } });
		return Forward(probe).success;
	}

	EditorGatewayResult WinHttpEditorGateway::Forward(const json::Value& request) const
	{
		EditorGatewayResult latest;
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			ConnectionSettings settings;
			std::string error;
			if (!LoadConnection(connection_file_, settings, error))
			{
				latest.error = std::move(error);
				continue;
			}
			latest = ForwardSingleSession(settings, request);
			if (latest.success)
			{
				return latest;
			}
			if (latest.request_outcome_unknown)
			{
				latest.error = "Editor request outcome is unknown; the Host did not retry it to preserve at-most-once mutation semantics. " + latest.error;
				return latest;
			}
		}
		latest.error = "Editor session reconnect exhausted: " + latest.error;
		return latest;
	}
}
