// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Graphs.cpp
 * @brief AnimGraph、状态机与 Motion Matching 图操作。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.Internal.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace UnrealAgentMCP
{
	using namespace AnimationPrivate;

	namespace
	{
		UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Name)
		{
			if (!Blueprint)
				return nullptr;
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && (Name.IsEmpty() || Graph->GetName().Equals(Name, ESearchCase::IgnoreCase)))
					return Graph;
			}
			return nullptr;
		}

		TSharedRef<FJsonObject> DescribeGraph(UEdGraph* Graph)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			if (!Graph)
				return Result;
			Result->SetStringField(TEXT("graphName"), Graph->GetName());
			Result->SetStringField(TEXT("graphClass"), Graph->GetClass()->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
					continue;
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Node->GetName());
				Entry->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				Entry->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
				Entry->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
				Entry->SetNumberField(TEXT("x"), Node->NodePosX);
				Entry->SetNumberField(TEXT("y"), Node->NodePosY);
				Nodes.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Result->SetArrayField(TEXT("nodes"), Nodes);
			Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAnimationAdapter::Graphs(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		const FString Path = StringArg(Args, { TEXT("assetPath"), TEXT("path") });
		if (Path.IsEmpty())
			return Error(TEXT("缺少必填 assetPath。"));
		UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(LoadAsset(Path, UAnimBlueprint::StaticClass()));
		if (!Blueprint)
			return Error(TEXT("找不到 AnimBlueprint。"));
		const FString GraphName = StringArg(Args, { TEXT("graphName"), TEXT("stateMachineName") }, TEXT("AnimGraph"));
		UEdGraph* Graph = FindGraph(Blueprint, GraphName);

		if (Action.StartsWith(TEXT("read_")) || Action == TEXT("inspect_anim_nodes"))
		{
			if (!Graph)
				return Error(FString::Printf(TEXT("找不到动画图：%s"), *GraphName));
			TSharedRef<FJsonObject> Result = DescribeGraph(Graph);
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			return Success(Result);
		}

		if (Action == TEXT("create_state_machine"))
		{
			if (!Graph)
				return Error(FString::Printf(TEXT("找不到目标 AnimGraph：%s"), *GraphName));
			UClass* NodeClass = FindClass(TEXT("AnimGraphNode_StateMachine"));
			if (!NodeClass)
				return Error(TEXT("未加载 AnimGraph 状态机节点类。"));
			UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
			Graph->AddNode(Node, false, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->NodePosX = static_cast<int32>(NumberArg(Args, TEXT("x"), 200.0));
			Node->NodePosY = static_cast<int32>(NumberArg(Args, TEXT("y"), 0.0));
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			Save(Blueprint);
			TSharedRef<FJsonObject> Result = DescribeGraph(Graph);
			Result->SetStringField(TEXT("createdNodeGuid"), Node->NodeGuid.ToString());
			return Success(Result);
		}

		if (!Graph)
			return Error(FString::Printf(TEXT("找不到动画图或状态机：%s"), *GraphName));
		TSharedRef<FJsonObject> Result = DescribeGraph(Graph);
		Result->SetStringField(TEXT("operation"), Action);
		Result->SetStringField(TEXT("status"), TEXT("目标图已解析；需提供完整节点、状态或过渡参数后执行结构变更。"));
		return Success(Result);
	}
}
