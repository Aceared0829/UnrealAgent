// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.ImportExport.cpp
 * @brief PCG 图谱批量导入与可往返 JSON 导出实现。
 */

#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FName FirstOrNamedPin(UPCGNode* Node, const FString& Requested, const bool bOutput)
		{
			if (!Node)
				return NAME_None;
			const TArray<FPCGPinProperties> Pins = bOutput ? Node->OutputPinProperties() : Node->InputPinProperties();
			if (Requested.IsEmpty())
				return Pins.IsEmpty() ? NAME_None : Pins[0].Label;
			for (const FPCGPinProperties& Pin : Pins)
			{
				if (Pin.Label.ToString().Equals(Requested, ESearchCase::IgnoreCase))
				{
					return Pin.Label;
				}
			}
			return NAME_None;
		}

		FString ExportNodeName(UPCGGraph* Graph, UPCGNode* Node)
		{
			if (Node == Graph->GetInputNode())
				return TEXT("input");
			if (Node == Graph->GetOutputNode())
				return TEXT("output");
			if (Node && Node->HasAuthoredTitle())
				return Node->GetAuthoredTitleName().ToString();
			return Node ? Node->GetName() : FString();
		}
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ImportGraph(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		const TArray<TSharedPtr<FJsonValue>>* NodesInput = nullptr;
		if (!Args->TryGetArrayField(TEXT("nodes"), NodesInput) || !NodesInput)
		{
			return ErrorJson(TEXT("缺少 nodes 数组。"));
		}
		const TArray<TSharedPtr<FJsonValue>>* ConnectionsInput = nullptr;
		Args->TryGetArrayField(TEXT("connections"), ConnectionsInput);
		bool bReplace = false;
		Args->TryGetBoolField(TEXT("replace"), bReplace);
		Transactions::FUnrealAgentMCPCompensatingEditorTransaction Compensation(FText::FromString(TEXT("Unreal Agent 导入 PCGGraph")),
			[Graph](FString& RestoreError)
			{
				NotifyGraphChanged(Graph);
				return PersistGraph(Graph, RestoreError);
			});
		if (!Compensation.IsReady(Error))
		{
			return ErrorJson(Error);
		}

		Graph->Modify();
		int32 RemovedNodeCount = 0;
		if (bReplace)
		{
			TArray<UPCGNode*> ToRemove;
			for (UPCGNode* Node : Graph->GetNodes())
			{
				if (Node && Node != Graph->GetInputNode() && Node != Graph->GetOutputNode())
				{
					ToRemove.Add(Node);
				}
			}
			RemovedNodeCount = ToRemove.Num();
			Graph->RemoveNodes(ToRemove);
		}

		TMap<FString, UPCGNode*> NodesByName;
		NodesByName.Add(TEXT("input"), Graph->GetInputNode());
		NodesByName.Add(TEXT("output"), Graph->GetOutputNode());
		for (UPCGNode* Existing : Graph->GetNodes())
		{
			if (!Existing)
				continue;
			NodesByName.Add(ExportNodeName(Graph, Existing), Existing);
		}

		int32 CreatedNodeCount = 0;
		for (const TSharedPtr<FJsonValue>& Value : *NodesInput)
		{
			const TSharedPtr<FJsonObject>* NodeJson = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(NodeJson) || !NodeJson || !NodeJson->IsValid())
			{
				return ErrorJson(TEXT("nodes 包含无效对象。"));
			}
			FString Name;
			(*NodeJson)->TryGetStringField(TEXT("name"), Name);
			if (Name.Equals(TEXT("input"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("output"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			FString ClassName;
			if (Name.IsEmpty() || !(*NodeJson)->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
			{
				return ErrorJson(TEXT("每个普通节点必须包含 name 和 class。"));
			}
			if (NodesByName.Contains(Name))
			{
				return ErrorJson(FString::Printf(TEXT("节点名称重复：%s"), *Name));
			}
			UClass* SettingsClass = ResolveSettingsClass(ClassName);
			if (!SettingsClass)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 UPCGSettings 类型：%s"), *ClassName));
			}
			UPCGSettings* Settings = nullptr;
			UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, Settings);
			if (!Node || !Settings)
				return ErrorJson(TEXT("批量创建 PCG 节点失败。"));
#if WITH_EDITOR
			Node->SetNodeTitle(FName(*Name));
			double PosX = 260.0 * (CreatedNodeCount + 1);
			double PosY = 0.0;
			(*NodeJson)->TryGetNumberField(TEXT("posX"), PosX);
			(*NodeJson)->TryGetNumberField(TEXT("posY"), PosY);
			Node->SetNodePosition(static_cast<int32>(PosX), static_cast<int32>(PosY));
#endif
			const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
			if ((*NodeJson)->TryGetObjectField(TEXT("settings"), SettingsJson) && SettingsJson && SettingsJson->IsValid())
			{
				FText FailureReason;
				if (!FJsonObjectConverter::JsonObjectToUStruct((*SettingsJson).ToSharedRef(), Settings->GetClass(), Settings, CPF_Edit, CPF_Transient | CPF_Deprecated, false,
						&FailureReason))
				{
					return ErrorJson(FString::Printf(TEXT("节点 %s 设置导入失败：%s"), *Name, *FailureReason.ToString()));
				}
			}
			Node->UpdateAfterSettingsChangeDuringCreation();
			NodesByName.Add(Name, Node);
			++CreatedNodeCount;
		}

		int32 CreatedEdgeCount = 0;
		if (ConnectionsInput)
		{
			for (const TSharedPtr<FJsonValue>& Value : *ConnectionsInput)
			{
				const TSharedPtr<FJsonObject>* EdgeJson = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(EdgeJson) || !EdgeJson || !EdgeJson->IsValid())
				{
					return ErrorJson(TEXT("connections 包含无效对象。"));
				}
				FString From;
				FString To;
				FString FromPinText;
				FString ToPinText;
				(*EdgeJson)->TryGetStringField(TEXT("from"), From);
				(*EdgeJson)->TryGetStringField(TEXT("to"), To);
				(*EdgeJson)->TryGetStringField(TEXT("fromPin"), FromPinText);
				(*EdgeJson)->TryGetStringField(TEXT("toPin"), ToPinText);
				UPCGNode* const* Source = NodesByName.Find(From);
				UPCGNode* const* Target = NodesByName.Find(To);
				if (!Source || !Target || !*Source || !*Target)
				{
					return ErrorJson(FString::Printf(TEXT("连线引用未知节点：%s -> %s"), *From, *To));
				}
				const FName FromPin = FirstOrNamedPin(*Source, FromPinText, true);
				const FName ToPin = FirstOrNamedPin(*Target, ToPinText, false);
				if (FromPin.IsNone() || ToPin.IsNone())
				{
					return ErrorJson(FString::Printf(TEXT("连线 Pin 不存在：%s -> %s"), *From, *To));
				}
				Graph->AddEdge(*Source, FromPin, *Target, ToPin);
				++CreatedEdgeCount;
			}
		}
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		if (!Compensation.Register(Error))
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Graph->GetPathName());
		Result->SetNumberField(TEXT("removedNodeCount"), RemovedNodeCount);
		Result->SetNumberField(TEXT("createdNodeCount"), CreatedNodeCount);
		Result->SetNumberField(TEXT("createdEdgeCount"), CreatedEdgeCount);
		Result->SetBoolField(TEXT("replaced"), bReplace);
		Result->SetBoolField(TEXT("saved"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ExportGraph(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		bool bIncludeSettings = true;
		Args->TryGetBoolField(TEXT("includeSettings"), bIncludeSettings);

		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!Node || Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
			{
				continue;
			}
			TSharedRef<FJsonObject> Json = MakeNodeJson(Graph, Node, bIncludeSettings);
			if (UPCGSettings* Settings = Node->GetSettings())
			{
				Json->SetStringField(TEXT("class"), Settings->GetClass()->GetPathName());
			}
			Nodes.Add(MakeShared<FJsonValueObject>(Json));
		}
		TArray<TSharedPtr<FJsonValue>> Connections;
		for (UPCGEdge* Edge : Graph->GetAllEdges())
		{
			if (!Edge || !Edge->IsValid())
				continue;
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("from"), ExportNodeName(Graph, const_cast<UPCGNode*>(Edge->GetInputNode())));
			Json->SetStringField(TEXT("fromPin"), Edge->GetInputPinLabel().ToString());
			Json->SetStringField(TEXT("to"), ExportNodeName(Graph, const_cast<UPCGNode*>(Edge->GetOutputNode())));
			Json->SetStringField(TEXT("toPin"), Edge->GetOutputPinLabel().ToString());
			Connections.Add(MakeShared<FJsonValueObject>(Json));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Graph->GetPathName());
		Result->SetBoolField(TEXT("includeSettings"), bIncludeSettings);
		Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
		Result->SetArrayField(TEXT("nodes"), Nodes);
		Result->SetNumberField(TEXT("connectionCount"), Connections.Num());
		Result->SetArrayField(TEXT("connections"), Connections);
		return SuccessJson(Result);
	}
}
