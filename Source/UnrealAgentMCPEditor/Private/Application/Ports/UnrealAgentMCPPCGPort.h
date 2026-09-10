// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPCGPort.h
 * @brief PCG 图谱、节点、连线和关卡组件的稳定应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPPCGPort
	{
	public:
		virtual ~IUnrealAgentMCPPCGPort() = default;

		virtual FString ListGraphs(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadGraph(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadNodeSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetComponents(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetComponentDetails(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateGraph(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddNode(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ConnectNodes(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DisconnectNodes(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetNodeSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemoveNode(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Execute(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ForceRegenerate(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Cleanup(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ToggleGraph(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddVolume(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ImportGraph(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ExportGraph(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
