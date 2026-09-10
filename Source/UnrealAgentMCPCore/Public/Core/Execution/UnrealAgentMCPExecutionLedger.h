#pragma once

#include "CoreMinimal.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Policy/UnrealAgentMCPPolicy.h"

namespace UnrealAgentMCP::Execution
{
	class UNREALAGENTMCPCORE_API IMcpExecutionLedger
	{
	public:
		virtual ~IMcpExecutionLedger() = default;

		virtual bool AppendAuditRecord(const Policy::FMcpAuditRecord& Record, FString& OutError) = 0;
		virtual bool AppendTaskSnapshot(const FMcpTaskSnapshot& Snapshot, FString& OutError) = 0;
		virtual bool IsHealthy(FString& OutError) const = 0;
		virtual FString GetLocation() const = 0;
	};
}
