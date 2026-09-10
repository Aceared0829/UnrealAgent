// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.h
 * @brief 通过 Unreal PCG 公共 API 实现图谱和关卡组件操作。
 */

#include "Application/Ports/UnrealAgentMCPPCGPort.h"

class AActor;
class UPCGComponent;
class UPCGGraph;
class UPCGNode;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealPCGAdapter final : public IUnrealAgentMCPPCGPort
	{
	public:
		virtual FString ListGraphs(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadGraph(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadNodeSettings(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetComponents(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetComponentDetails(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateGraph(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddNode(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ConnectNodes(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString DisconnectNodes(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetNodeSettings(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString RemoveNode(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Execute(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ForceRegenerate(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Cleanup(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ToggleGraph(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddVolume(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ImportGraph(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ExportGraph(const TSharedPtr<FJsonObject>& Args) override;

	public:
		/** 以下辅助入口供同一 Adapter 的分拆实现文件复用。 */
		static UPCGGraph* ResolveGraph(const TSharedPtr<FJsonObject>& Args, FString* OutError = nullptr);
		static UPCGNode* ResolveNode(UPCGGraph* Graph, const FString& Name);
		static UClass* ResolveSettingsClass(const FString& NodeType);
		static UPCGComponent* ResolveComponent(const FString& ActorLabel, AActor** OutActor = nullptr);
		static TSharedRef<FJsonObject> MakeNodeJson(UPCGGraph* Graph, UPCGNode* Node, bool bIncludeSettings);
		static bool PersistGraph(UPCGGraph* Graph, FString& OutError);
		static void NotifyGraphChanged(UPCGGraph* Graph);
	};
}
