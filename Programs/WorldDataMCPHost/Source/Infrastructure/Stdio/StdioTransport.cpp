// Copyright ZhaoZining. All Rights Reserved.

#include "Infrastructure/Stdio/StdioTransport.h"

#include "Application/HostApplication.h"
#include "Core/Json/JsonValue.h"

#include <iostream>
#include <string>

namespace worlddata::host
{
	int RunStdio(HostApplication& application)
	{
		std::ios::sync_with_stdio(false);
		std::string line;
		while (std::getline(std::cin, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			json::Value request;
			std::string error;
			if (!json::Parse(line, request, error))
			{
				json::Value parse_error =
					json::Value::Object({ { "error", json::Value::Object({ { "code", json::Value::Number(-32700) }, { "message", json::Value::String(std::move(error)) } }) },
						{ "jsonrpc", json::Value::String("2.0") } });
				std::cout << json::Serialize(parse_error) << '\n' << std::flush;
				continue;
			}
			if (auto response = application.Handle(request))
			{
				std::cout << json::Serialize(*response) << '\n' << std::flush;
			}
		}
		return 0;
	}
}
