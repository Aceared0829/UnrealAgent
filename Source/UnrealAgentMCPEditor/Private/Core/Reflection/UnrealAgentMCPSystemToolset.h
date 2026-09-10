// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPSystemToolset.h
 * @brief Unreal Agent 自有反射基础设施的最小生产 Toolset。
 */

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "UnrealAgentMCPSystemToolset.generated.h"

UCLASS(meta = (UnrealAgentMCPToolset = "UnrealAgentMCP.System"))
class UUnrealAgentMCPSystemToolset : public UObject
{
	GENERATED_BODY()

public:
	/** 验证自有 Toolset 发现、Schema 生成与进程内调用链。 */
	UFUNCTION(meta = (UnrealAgentMCPTool, UnrealAgentMCPToolName = "ping", UnrealAgentMCPToolDescription = "验证 Unreal Agent 自有反射工具调用链。",
				  UnrealAgentMCPRisk = "ReadOnly", UnrealAgentMCPTransaction = "ReadOnly", UnrealAgentMCPIdempotent = "true"))
	FString Ping() const;
};
