// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.Authoring.cpp
 * @brief PCG 图谱、节点、连线、设置和静态网格列表写入实现。
 */

#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Engine/StaticMesh.h"
#include "JsonObjectConverter.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Misc/PackageName.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizePCGPackagePath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.EndsWith(TEXT("/")))
				Path.LeftChopInline(1);
			return Path.IsEmpty() ? TEXT("/Game/PCG") : Path;
		}

		FName ResolvePin(UPCGNode* Node, const FString& Requested, const bool bOutput, FString& OutError)
		{
			if (!Node)
				return NAME_None;
			const TArray<FPCGPinProperties> Pins = bOutput ? Node->OutputPinProperties() : Node->InputPinProperties();
			if (!Requested.IsEmpty())
			{
				const FName RequestedName(*Requested);
				for (const FPCGPinProperties& Pin : Pins)
				{
					if (Pin.Label == RequestedName || Pin.Label.ToString().Equals(Requested, ESearchCase::IgnoreCase))
					{
						return Pin.Label;
					}
				}
				OutError = FString::Printf(TEXT("节点 %s 不存在 %s Pin：%s"), *Node->GetName(), bOutput ? TEXT("输出") : TEXT("输入"), *Requested);
				return NAME_None;
			}
			if (!Pins.IsEmpty())
				return Pins[0].Label;
			OutError = FString::Printf(TEXT("节点 %s 没有可用的%s Pin。"), *Node->GetName(), bOutput ? TEXT("输出") : TEXT("输入"));
			return NAME_None;
		}

		bool EdgeMatches(const UPCGEdge* Edge, const UPCGNode* Source, const UPCGNode* Target, const FName SourcePin, const FName TargetPin)
		{
			return Edge && Edge->IsValid() && Edge->GetInputNode() == Source && Edge->GetOutputNode() == Target && (SourcePin.IsNone() || Edge->GetInputPinLabel() == SourcePin) &&
				(TargetPin.IsNone() || Edge->GetOutputPinLabel() == TargetPin);
		}
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::CreateGraph(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 name。"));
		}
		if (!FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
		{
			return ErrorJson(TEXT("PCGGraph 名称包含非法字符。"));
		}
		FString PackagePath = TEXT("/Game/PCG");
		Args->TryGetStringField(TEXT("packagePath"), PackagePath);
		PackagePath = NormalizePCGPackagePath(PackagePath);
		if (!PackagePath.StartsWith(TEXT("/Game")))
			return ErrorJson(TEXT("packagePath 必须位于 /Game。"));

		const FString PackageName = PackagePath + TEXT("/") + Name;
		const FString AssetPath = PackageName + TEXT(".") + Name;
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
		const bool bExisted = Graph != nullptr;
		if (!Graph)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			if (!Graph)
				return ErrorJson(TEXT("PCGGraph 创建失败。"));
#if WITH_EDITOR
			Graph->SetupEditorData();
#endif
			FAssetRegistryModule::AssetCreated(Graph);
			NotifyGraphChanged(Graph);
		}
		FString Error;
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Graph->GetName());
		Result->SetStringField(TEXT("assetPath"), Graph->GetPathName());
		Result->SetBoolField(TEXT("existed"), bExisted);
		Result->SetBoolField(TEXT("saved"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::AddNode(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString NodeType;
		if (!Args->TryGetStringField(TEXT("nodeType"), NodeType) || NodeType.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 nodeType。"));
		}
		UClass* SettingsClass = ResolveSettingsClass(NodeType);
		if (!SettingsClass)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 UPCGSettings 类型：%s"), *NodeType));
		}
		UPCGSettings* Settings = nullptr;
		Graph->Modify();
		UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, Settings);
		if (!Node || !Settings)
			return ErrorJson(TEXT("PCG 节点创建失败。"));

		FString NodeName;
		Args->TryGetStringField(TEXT("nodeName"), NodeName);
		if (NodeName.IsEmpty())
		{
			NodeName = FString::Printf(TEXT("%s_%d"), *SettingsClass->GetName(), Graph->GetNodes().Num());
		}
#if WITH_EDITOR
		Node->SetNodeTitle(FName(*NodeName));
		Node->SetNodePosition(260 * Graph->GetNodes().Num(), 0);
#endif
		Node->UpdateAfterSettingsChangeDuringCreation();
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeNodeJson(Graph, Node, false);
		Result->SetBoolField(TEXT("saved"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ConnectNodes(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString SourceName;
		FString SourcePinText;
		FString TargetName;
		FString TargetPinText;
		if (!Args->TryGetStringField(TEXT("sourceNode"), SourceName) || !Args->TryGetStringField(TEXT("sourcePin"), SourcePinText) ||
			!Args->TryGetStringField(TEXT("targetNode"), TargetName) || !Args->TryGetStringField(TEXT("targetPin"), TargetPinText))
		{
			return ErrorJson(TEXT("缺少 sourceNode/sourcePin/targetNode/targetPin。"));
		}
		UPCGNode* Source = ResolveNode(Graph, SourceName);
		UPCGNode* Target = ResolveNode(Graph, TargetName);
		if (!Source || !Target)
			return ErrorJson(TEXT("源节点或目标节点不存在。"));
		const FName SourcePin = ResolvePin(Source, SourcePinText, true, Error);
		const FName TargetPin = ResolvePin(Target, TargetPinText, false, Error);
		if (SourcePin.IsNone() || TargetPin.IsNone())
			return ErrorJson(Error);

		const bool bAlreadyConnected = Graph->GetAllEdges().ContainsByPredicate(
			[Source, Target, SourcePin, TargetPin](const UPCGEdge* Edge)
			{
				return EdgeMatches(Edge, Source, Target, SourcePin, TargetPin);
			});
		if (!bAlreadyConnected)
		{
			Graph->Modify();
			Graph->AddEdge(Source, SourcePin, Target, TargetPin);
		}
		const bool bEdgeVerified = Graph->GetAllEdges().ContainsByPredicate(
			[Source, Target, SourcePin, TargetPin](const UPCGEdge* Edge)
			{
				return EdgeMatches(Edge, Source, Target, SourcePin, TargetPin);
			});
		if (!bEdgeVerified)
			return ErrorJson(TEXT("PCG AddEdge 后未发现持久化连线。"));
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sourceNode"), SourceName);
		Result->SetStringField(TEXT("sourcePin"), SourcePin.ToString());
		Result->SetStringField(TEXT("targetNode"), TargetName);
		Result->SetStringField(TEXT("targetPin"), TargetPin.ToString());
		Result->SetBoolField(TEXT("edgeVerified"), true);
		Result->SetBoolField(TEXT("alreadyConnected"), bAlreadyConnected);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::DisconnectNodes(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString SourceName;
		FString TargetName;
		if (!Args->TryGetStringField(TEXT("sourceNode"), SourceName) || !Args->TryGetStringField(TEXT("targetNode"), TargetName))
		{
			return ErrorJson(TEXT("缺少 sourceNode 或 targetNode。"));
		}
		FString SourcePinText;
		FString TargetPinText;
		Args->TryGetStringField(TEXT("sourcePin"), SourcePinText);
		Args->TryGetStringField(TEXT("targetPin"), TargetPinText);
		UPCGNode* Source = ResolveNode(Graph, SourceName);
		UPCGNode* Target = ResolveNode(Graph, TargetName);
		if (!Source || !Target)
			return ErrorJson(TEXT("源节点或目标节点不存在。"));
		const FName SourcePin = SourcePinText.IsEmpty() ? NAME_None : FName(*SourcePinText);
		const FName TargetPin = TargetPinText.IsEmpty() ? NAME_None : FName(*TargetPinText);

		TArray<TPair<FName, FName>> EdgesToRemove;
		for (UPCGEdge* Edge : Graph->GetAllEdges())
		{
			if (EdgeMatches(Edge, Source, Target, SourcePin, TargetPin))
			{
				EdgesToRemove.Emplace(Edge->GetInputPinLabel(), Edge->GetOutputPinLabel());
			}
		}
		Graph->Modify();
		int32 RemovedEdges = 0;
		for (const TPair<FName, FName>& Pair : EdgesToRemove)
		{
			if (Graph->RemoveEdge(Source, Pair.Key, Target, Pair.Value))
			{
				++RemovedEdges;
			}
		}
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("removedEdges"), RemovedEdges);
		Result->SetStringField(TEXT("sourceNode"), SourceName);
		Result->SetStringField(TEXT("targetNode"), TargetName);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::SetNodeSettings(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString NodeName;
		if (!Args->TryGetStringField(TEXT("nodeName"), NodeName) || NodeName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 nodeName。"));
		}
		const TSharedPtr<FJsonObject>* SettingsInput = nullptr;
		if (!Args->TryGetObjectField(TEXT("settings"), SettingsInput) || !SettingsInput || !SettingsInput->IsValid())
		{
			return ErrorJson(TEXT("缺少有效 settings 对象。"));
		}
		UPCGNode* Node = ResolveNode(Graph, NodeName);
		UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
		if (!Node || !Settings)
			return ErrorJson(TEXT("未找到可配置的 PCG 节点。"));

		Settings->Modify();
		FText FailureReason;
		if (!FJsonObjectConverter::JsonObjectToUStruct((*SettingsInput).ToSharedRef(), Settings->GetClass(), Settings, CPF_Edit, CPF_Transient | CPF_Deprecated, false,
				&FailureReason))
		{
			return ErrorJson(FString::Printf(TEXT("PCG 设置写入失败：%s"), *FailureReason.ToString()));
		}
		Settings->PostEditChange();
		Node->UpdateAfterSettingsChangeDuringCreation();
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeNodeJson(Graph, Node, true);
		Result->SetBoolField(TEXT("saved"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString NodeName;
		if (!Args->TryGetStringField(TEXT("nodeName"), NodeName))
			return ErrorJson(TEXT("缺少必填 nodeName。"));
		UPCGNode* Node = ResolveNode(Graph, NodeName);
		UPCGStaticMeshSpawnerSettings* Settings = Node ? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings()) : nullptr;
		if (!Settings)
		{
			return ErrorJson(TEXT("目标节点不是 PCGStaticMeshSpawnerSettings。"));
		}
		const TArray<TSharedPtr<FJsonValue>>* EntriesInput = nullptr;
		if (!Args->TryGetArrayField(TEXT("entries"), EntriesInput) || !EntriesInput)
		{
			return ErrorJson(TEXT("缺少 entries 数组。"));
		}
		bool bReplace = true;
		Args->TryGetBoolField(TEXT("replace"), bReplace);
		Settings->Modify();
		Settings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
		UPCGMeshSelectorWeighted* Selector = Cast<UPCGMeshSelectorWeighted>(Settings->MeshSelectorParameters);
		if (!Selector)
			return ErrorJson(TEXT("Weighted Mesh Selector 初始化失败。"));
		Selector->Modify();
		if (bReplace)
			Selector->MeshEntries.Reset();

		int32 AddedCount = 0;
		for (const TSharedPtr<FJsonValue>& Value : *EntriesInput)
		{
			const TSharedPtr<FJsonObject>* EntryJson = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(EntryJson) || !EntryJson || !EntryJson->IsValid())
			{
				return ErrorJson(TEXT("entries 包含无效对象。"));
			}
			FString MeshPath;
			if (!(*EntryJson)->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty())
			{
				return ErrorJson(TEXT("Mesh 条目缺少 mesh。"));
			}
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *JsonConversion::NormalizeAssetObjectPath(MeshPath));
			if (!Mesh)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh：%s"), *MeshPath));
			}
			double WeightNumber = 1.0;
			(*EntryJson)->TryGetNumberField(TEXT("weight"), WeightNumber);
			FPCGMeshSelectorWeightedEntry Entry;
			Entry.Descriptor.StaticMesh = TSoftObjectPtr<UStaticMesh>(Mesh);
			Entry.Weight = FMath::Max(0, static_cast<int32>(WeightNumber));
			Selector->MeshEntries.Add(MoveTemp(Entry));
			++AddedCount;
		}
#if WITH_EDITOR
		Selector->RefreshDisplayNames();
#endif
		Settings->PostEditChange();
		Node->UpdateAfterSettingsChangeDuringCreation();
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("nodeName"), NodeName);
		Result->SetNumberField(TEXT("addedCount"), AddedCount);
		Result->SetNumberField(TEXT("entryCount"), Selector->MeshEntries.Num());
		Result->SetBoolField(TEXT("replaced"), bReplace);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::RemoveNode(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UPCGGraph* Graph = ResolveGraph(Args, &Error);
		if (!Graph)
			return ErrorJson(Error);
		FString NodeName;
		if (!Args->TryGetStringField(TEXT("nodeName"), NodeName))
			return ErrorJson(TEXT("缺少必填 nodeName。"));
		UPCGNode* Node = ResolveNode(Graph, NodeName);
		if (!Node)
			return ErrorJson(TEXT("未找到目标 PCG 节点。"));
		if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
		{
			return ErrorJson(TEXT("不能删除 PCG 输入或输出节点。"));
		}
		Graph->Modify();
		Graph->RemoveNode(Node);
		NotifyGraphChanged(Graph);
		if (!PersistGraph(Graph, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("removedNode"), NodeName);
		Result->SetNumberField(TEXT("remainingNodeCount"), Graph->GetNodes().Num());
		return SuccessJson(Result);
	}
}
