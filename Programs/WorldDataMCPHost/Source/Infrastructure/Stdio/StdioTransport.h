#pragma once

namespace worlddata::host
{
	class HostApplication;

	/** 运行按行分帧的 stdio MCP 传输循环，直至输入流结束。 */
	int RunStdio(HostApplication& application);
}
