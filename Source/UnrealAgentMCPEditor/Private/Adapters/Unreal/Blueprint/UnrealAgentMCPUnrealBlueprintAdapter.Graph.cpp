// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Graph.cpp
 * @brief Blueprint 图读取、节点编辑、连线、导入导出与自动布局实现。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	using namespace BlueprintPrivate;

	namespace
	{
		UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& Name, const EEdGraphPinDirection Direction)
		{
			if (!Node)
				return nullptr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == Direction && Pin->PinName.ToString().Equals(Name, ESearchCase::IgnoreCase))
					return Pin;
			}
			return nullptr;
		}

		TSharedRef<FJsonObject> GraphResult(UEdGraph* Graph, const bool bPins)
		{
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("graphName"), Graph ? Graph->GetName() : FString());
			TArray<TSharedPtr<FJsonValue>> Nodes;
			if (Graph)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node)
						Nodes.Add(MakeShared<FJsonValueObject>(NodeJson(Node, bPins, bPins, true)));
				}
			}
			Result->SetArrayField(TEXT("nodes"), Nodes);
			Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealBlueprintAdapter::ExecuteGraph(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_node_types") || Action == TEXT("search_node_types"))
		{
			const FString Search = StringArg(Args, { TEXT("search"), TEXT("query") }).ToLower();
			TArray<TSharedPtr<FJsonValue>> Types;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class->IsChildOf(UEdGraphNode::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
					continue;
				const FString Path = Class->GetPathName();
				if (!Search.IsEmpty() && !Path.ToLower().Contains(Search))
					continue;
				Types.Add(MakeShared<FJsonValueString>(Path));
				if (Types.Num() >= 500)
					break;
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("nodeTypes"), Types);
			return Serialize(Result);
		}

		FString Error;
		UBlueprint* Blueprint = LoadBlueprint(Args, Error);
		if (!Blueprint)
			return Failure(Error);
		UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("graphName"), TEXT("functionName") }));
		if (!Graph)
			return Failure(TEXT("找不到目标 Blueprint 图。"));

		if (Action == TEXT("read_graph"))
			return Serialize(GraphResult(Graph, true));
		if (Action == TEXT("read_graph_summary"))
			return Serialize(GraphResult(Graph, false));
		if (Action == TEXT("get_execution_flow"))
		{
			TArray<TSharedPtr<FJsonValue>> Edges;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
					continue;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != TEXT("exec"))
						continue;
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (!Linked || !Linked->GetOwningNode())
							continue;
						TSharedRef<FJsonObject> Edge = MakeShared<FJsonObject>();
						Edge->SetStringField(TEXT("from"), Node->NodeGuid.ToString());
						Edge->SetStringField(TEXT("to"), Linked->GetOwningNode()->NodeGuid.ToString());
						Edges.Add(MakeShared<FJsonValueObject>(Edge));
					}
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("edges"), Edges);
			return Serialize(Result);
		}

		if (Action == TEXT("add_node"))
		{
			UClass* Class = ResolveClass(StringArg(Args, { TEXT("nodeClass"), TEXT("class") }), UEdGraphNode::StaticClass());
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
				return Failure(TEXT("节点类无效。"));
			Graph->Modify();
			UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, Class, NAME_None, RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
			Node->NodePosX = static_cast<int32>(NumberArg(Args, TEXT("x"), 0));
			Node->NodePosY = static_cast<int32>(NumberArg(Args, TEXT("y"), 0));
			Node->AllocateDefaultPins();
			SaveBlueprint(Blueprint, true);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetObjectField(TEXT("node"), NodeJson(Node, true, true, true));
			return Serialize(Result);
		}

		UEdGraphNode* Node = ResolveNode(Graph, StringArg(Args, { TEXT("nodeId"), TEXT("nodeName") }));
		if (Action == TEXT("delete_node"))
		{
			if (!Node)
				return Failure(TEXT("找不到节点。"));
			Graph->RemoveNode(Node);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_node_property") || Action == TEXT("read_node_property"))
		{
			if (!Node)
				return Failure(TEXT("找不到节点。"));
			const FString PropertyName = StringArg(Args, { TEXT("propertyName"), TEXT("pinName") });
			if (Action == TEXT("read_node_property"))
			{
				TSharedPtr<FJsonValue> Value = ReadProperty(Node, PropertyName);
				if (!Value.IsValid())
				{
					if (UEdGraphPin* Pin = FindPin(Node, PropertyName, EGPD_Input))
						Value = MakeShared<FJsonValueString>(Pin->DefaultValue);
				}
				if (!Value.IsValid())
					return Failure(TEXT("找不到节点属性或输入 Pin。"));
				TSharedRef<FJsonObject> Result = SuccessObject();
				Result->SetField(TEXT("value"), Value);
				return Serialize(Result);
			}
			if (UEdGraphPin* Pin = FindPin(Node, PropertyName, EGPD_Input))
			{
				Pin->Modify();
				FString Text;
				if (!Args->TryGetStringField(TEXT("value"), Text))
					Text = Args->TryGetField(TEXT("value"))->AsString();
				Pin->DefaultValue = Text;
			}
			else if (!SetProperty(Node, PropertyName, Args->TryGetField(TEXT("value")), Error))
				return Failure(Error);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("connect_pins"))
		{
			UEdGraphNode* From = ResolveNode(Graph, StringArg(Args, { TEXT("fromNodeId") }));
			UEdGraphNode* To = ResolveNode(Graph, StringArg(Args, { TEXT("toNodeId") }));
			UEdGraphPin* Out = FindPin(From, StringArg(Args, { TEXT("outputPin"), TEXT("fromPin") }), EGPD_Output);
			UEdGraphPin* In = FindPin(To, StringArg(Args, { TEXT("inputPin"), TEXT("toPin") }), EGPD_Input);
			if (!Out || !In || !Graph->GetSchema()->TryCreateConnection(Out, In))
				return Failure(TEXT("Pin 不存在或类型不兼容。"));
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("export_nodes_t3d"))
		{
			TSet<UObject*> Nodes;
			if (Node)
				Nodes.Add(Node);
			else
				for (UEdGraphNode* Item : Graph->Nodes)
					if (Item)
						Nodes.Add(Item);
			FString Text;
			FEdGraphUtilities::ExportNodesToText(Nodes, Text);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("text"), Text);
			return Serialize(Result);
		}
		else if (Action == TEXT("import_nodes_t3d"))
		{
			Transactions::FUnrealAgentMCPCompensatingEditorTransaction Compensation(FText::FromString(TEXT("Unreal Agent 导入 Blueprint 节点")),
				[Blueprint](FString& RestoreError)
				{
					if (SaveBlueprint(Blueprint, true))
					{
						return true;
					}
					RestoreError = TEXT("恢复后的 Blueprint 保存失败。");
					return false;
				});
			if (!Compensation.IsReady(Error))
			{
				return Failure(Error);
			}
			Graph->Modify();
			TSet<UEdGraphNode*> Imported;
			FEdGraphUtilities::ImportNodesFromText(Graph, StringArg(Args, { TEXT("text"), TEXT("t3d") }), Imported);
			if (!SaveBlueprint(Blueprint, true))
			{
				return Failure(TEXT("Blueprint 节点导入后保存失败。"));
			}
			if (!Compensation.Register(Error))
			{
				return Failure(Error);
			}
		}
		else if (Action == TEXT("cleanup_graph"))
		{
			for (int32 Index = Graph->Nodes.Num() - 1; Index >= 0; --Index)
			{
				UEdGraphNode* Item = Graph->Nodes[Index];
				bool bConnected = false;
				if (Item)
				{
					for (const UEdGraphPin* Pin : Item->Pins)
					{
						if (Pin && !Pin->LinkedTo.IsEmpty())
						{
							bConnected = true;
							break;
						}
					}
				}
				if (Item && Item->CanUserDeleteNode() && !bConnected)
					Graph->RemoveNode(Item);
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("connect_pins_batch"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (!Args->TryGetArrayField(TEXT("connections"), Connections))
				return Failure(TEXT("缺少 connections。"));
			int32 Connected = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Connections)
			{
				const TSharedPtr<FJsonObject> C = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!C)
					continue;
				UEdGraphNode* From = ResolveNode(Graph, StringArg(C, { TEXT("fromNodeId") }));
				UEdGraphNode* To = ResolveNode(Graph, StringArg(C, { TEXT("toNodeId") }));
				UEdGraphPin* Out = FindPin(From, StringArg(C, { TEXT("outputPin"), TEXT("fromPin") }), EGPD_Output);
				UEdGraphPin* In = FindPin(To, StringArg(C, { TEXT("inputPin"), TEXT("toPin") }), EGPD_Input);
				if (Out && In && Graph->GetSchema()->TryCreateConnection(Out, In))
					++Connected;
			}
			SaveBlueprint(Blueprint, true);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("connected"), Connected);
			return Serialize(Result);
		}
		else if (Action == TEXT("set_node_position"))
		{
			if (!Node)
				return Failure(TEXT("找不到节点。"));
			Node->Modify();
			Node->NodePosX = static_cast<int32>(NumberArg(Args, TEXT("x"), Node->NodePosX));
			Node->NodePosY = static_cast<int32>(NumberArg(Args, TEXT("y"), Node->NodePosY));
			SaveBlueprint(Blueprint, false);
		}
		else if (Action == TEXT("auto_layout"))
		{
			int32 Index = 0;
			for (UEdGraphNode* Item : Graph->Nodes)
			{
				if (!Item)
					continue;
				Item->Modify();
				Item->NodePosX = (Index % 5) * 420;
				Item->NodePosY = (Index / 5) * 240;
				++Index;
			}
			SaveBlueprint(Blueprint, false);
		}
		else
			return Failure(FString::Printf(TEXT("未知图操作：%s"), *Action));

		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		return Serialize(Result);
	}
}
