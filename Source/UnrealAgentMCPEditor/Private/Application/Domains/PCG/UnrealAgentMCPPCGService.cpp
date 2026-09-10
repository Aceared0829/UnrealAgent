// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPCGService.cpp
 * @brief PCG 应用服务实现，只依赖 PCG Port 和 JSON 契约。
 */

#include "Application/Domains/PCG/UnrealAgentMCPPCGService.h"

#include "Application/Ports/UnrealAgentMCPPCGPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPPCGService::FUnrealAgentMCPPCGService(TSharedRef<IUnrealAgentMCPPCGPort> InPCGPort) : PCGPort(MoveTemp(InPCGPort))
	{
	}

	TArray<FString> FUnrealAgentMCPPCGService::GetImplementedActions()
	{
		return { TEXT("list_graphs"), TEXT("read_graph"), TEXT("read_node_settings"), TEXT("get_components"), TEXT("get_component_details"), TEXT("create_graph"), TEXT("add_node"),
			TEXT("connect_nodes"), TEXT("disconnect_nodes"), TEXT("set_node_settings"), TEXT("set_static_mesh_spawner_meshes"), TEXT("remove_node"), TEXT("execute"),
			TEXT("force_regenerate"), TEXT("cleanup"), TEXT("toggle_graph"), TEXT("add_volume"), TEXT("import_graph"), TEXT("export_graph") };
	}

	FString FUnrealAgentMCPPCGService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();

		if (Action == TEXT("list_graphs"))
			return PCGPort->ListGraphs(SafeArgs);
		if (Action == TEXT("read_graph"))
			return PCGPort->ReadGraph(SafeArgs);
		if (Action == TEXT("read_node_settings"))
			return PCGPort->ReadNodeSettings(SafeArgs);
		if (Action == TEXT("get_components"))
			return PCGPort->GetComponents(SafeArgs);
		if (Action == TEXT("get_component_details"))
			return PCGPort->GetComponentDetails(SafeArgs);
		if (Action == TEXT("create_graph"))
			return PCGPort->CreateGraph(SafeArgs);
		if (Action == TEXT("add_node"))
			return PCGPort->AddNode(SafeArgs);
		if (Action == TEXT("connect_nodes"))
			return PCGPort->ConnectNodes(SafeArgs);
		if (Action == TEXT("disconnect_nodes"))
			return PCGPort->DisconnectNodes(SafeArgs);
		if (Action == TEXT("set_node_settings"))
			return PCGPort->SetNodeSettings(SafeArgs);
		if (Action == TEXT("set_static_mesh_spawner_meshes"))
			return PCGPort->SetStaticMeshSpawnerMeshes(SafeArgs);
		if (Action == TEXT("remove_node"))
			return PCGPort->RemoveNode(SafeArgs);
		if (Action == TEXT("execute"))
			return PCGPort->Execute(SafeArgs);
		if (Action == TEXT("force_regenerate"))
			return PCGPort->ForceRegenerate(SafeArgs);
		if (Action == TEXT("cleanup"))
			return PCGPort->Cleanup(SafeArgs);
		if (Action == TEXT("toggle_graph"))
			return PCGPort->ToggleGraph(SafeArgs);
		if (Action == TEXT("add_volume"))
			return PCGPort->AddVolume(SafeArgs);
		if (Action == TEXT("import_graph"))
			return PCGPort->ImportGraph(SafeArgs);
		if (Action == TEXT("export_graph"))
			return PCGPort->ExportGraph(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("pcg"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("PCG action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
