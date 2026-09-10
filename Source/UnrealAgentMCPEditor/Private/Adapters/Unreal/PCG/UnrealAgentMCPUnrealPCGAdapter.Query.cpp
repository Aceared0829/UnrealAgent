// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.Query.cpp
 * @brief PCG 图谱、节点设置和关卡组件查询实现。
 */

#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGComponent.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NodeName(UPCGGraph* Graph, const UPCGNode* Node)
		{
			if (!Graph || !Node)
				return FString();
			if (Node == Graph->GetInputNode())
				return TEXT("input");
			if (Node == Graph->GetOutputNode())
				return TEXT("output");
			if (Node->HasAuthoredTitle())
				return Node->GetAuthoredTitleName().ToString();
			if (const UPCGSettings* Settings = Node->GetSettings())
				return Settings->GetClass()->GetName();
			return Node->GetName();
		}

		TSharedRef<FJsonObject> MakeComponentJson(AActor* Actor, UPCGComponent* Component)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Json->SetStringField(TEXT("actorName"), Actor->GetName());
			Json->SetStringField(TEXT("componentName"), Component->GetName());
			Json->SetStringField(TEXT("graphPath"), Component->GetGraph() ? Component->GetGraph()->GetPathName() : FString());
			Json->SetBoolField(TEXT("activated"), Component->bActivated);
			Json->SetBoolField(TEXT("generated"), Component->bGenerated);
			Json->SetBoolField(TEXT("generating"), Component->IsGenerating());
			Json->SetBoolField(TEXT("cleaningUp"), Component->IsCleaningUp());
			Json->SetNumberField(TEXT("seed"), Component->Seed);
			return Json;
		}
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ListGraphs(const TSharedPtr<FJsonObject>& Args)
	{
		FString Directory = TEXT("/Game");
		Args->TryGetStringField(TEXT("directory"), Directory);
		bool bRecursive = true;
		Args->TryGetBoolField(TEXT("recursive"), bRecursive);

		FARFilter Filter;
		Filter.ClassPaths.Add(UPCGGraph::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*Directory));
		Filter.bRecursivePaths = bRecursive;
		TArray<FAssetData> Assets;
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistry.Get().GetAssets(Filter, Assets);
		TArray<TSharedPtr<FJsonValue>> Graphs;
		for (const FAssetData& Asset : Assets)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Json->SetStringField(TEXT("assetPath"), Asset.GetObjectPathString());
			Json->SetStringField(TEXT("packagePath"), Asset.PackagePath.ToString());
			Graphs.Add(MakeShared<FJsonValueObject>(Json));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetNumberField(TEXT("count"), Graphs.Num());
		Result->SetArrayField(TEXT("graphs"), Graphs);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ReadGraph(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		TArray<TSharedPtr<FJsonValue>> Nodes;
		Nodes.Add(MakeShared<FJsonValueObject>(MakeNodeJson(Graph, Graph->GetInputNode(), false)));
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node != Graph->GetInputNode() && Node != Graph->GetOutputNode())
			{
				Nodes.Add(MakeShared<FJsonValueObject>(MakeNodeJson(Graph, Node, false)));
			}
		}
		Nodes.Add(MakeShared<FJsonValueObject>(MakeNodeJson(Graph, Graph->GetOutputNode(), false)));

		TArray<TSharedPtr<FJsonValue>> Edges;
		for (UPCGEdge* Edge : Graph->GetAllEdges())
		{
			if (!Edge || !Edge->IsValid())
				continue;
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("sourceNode"), NodeName(Graph, Edge->GetInputNode()));
			Json->SetStringField(TEXT("sourcePin"), Edge->GetInputPinLabel().ToString());
			Json->SetStringField(TEXT("targetNode"), NodeName(Graph, Edge->GetOutputNode()));
			Json->SetStringField(TEXT("targetPin"), Edge->GetOutputPinLabel().ToString());
			Edges.Add(MakeShared<FJsonValueObject>(Json));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Graph->GetPathName());
		Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
		Result->SetArrayField(TEXT("nodes"), Nodes);
		Result->SetNumberField(TEXT("edgeCount"), Edges.Num());
		Result->SetArrayField(TEXT("connections"), Edges);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ReadNodeSettings(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString NodeNameField;
		if (!Args->TryGetStringField(TEXT("nodeName"), NodeNameField) || NodeNameField.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 nodeName。"));
		}
		UPCGNode* Node = ResolveNode(Graph, NodeNameField);
		if (!Node)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 PCG 节点：%s"), *NodeNameField));
		}
		return SuccessJson(MakeNodeJson(Graph, Node, true));
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::GetComponents(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));
		TArray<TSharedPtr<FJsonValue>> Components;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UPCGComponent*> ActorComponents;
			It->GetComponents<UPCGComponent>(ActorComponents);
			for (UPCGComponent* Component : ActorComponents)
			{
				Components.Add(MakeShared<FJsonValueObject>(MakeComponentJson(*It, Component)));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Components.Num());
		Result->SetArrayField(TEXT("components"), Components);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::GetComponentDetails(const TSharedPtr<FJsonObject>& Args)
	{
		FString ActorLabel;
		if (!Args->TryGetStringField(TEXT("actorLabel"), ActorLabel) || ActorLabel.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 actorLabel。"));
		}
		AActor* Actor = nullptr;
		UPCGComponent* Component = ResolveComponent(ActorLabel, &Actor);
		if (!Component || !Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor 没有 PCGComponent：%s"), *ActorLabel));
		}
		TSharedRef<FJsonObject> Result = MakeComponentJson(Actor, Component);
		Result->SetBoolField(TEXT("partitioned"), Component->bIsComponentPartitioned);
		Result->SetNumberField(TEXT("generationTrigger"), static_cast<int32>(Component->GenerationTrigger));
		Result->SetNumberField(TEXT("generatedDataCount"), Component->GetGeneratedGraphOutput().TaggedData.Num());
		return SuccessJson(Result);
	}
}
