// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Execution/UnrealAgentMCPExecutionLedger.h"

namespace UnrealAgentMCP::Audit
{
	TSharedRef<Execution::IMcpExecutionLedger, ESPMode::ThreadSafe> CreateJsonlExecutionLedger(FString Location = FString());
}
