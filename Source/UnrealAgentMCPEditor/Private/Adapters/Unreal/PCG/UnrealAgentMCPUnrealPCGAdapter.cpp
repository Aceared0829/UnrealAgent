// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.cpp
 * @brief PCG 图谱、节点类型、持久化和 JSON 表达的公共实现。
 */

#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "PCGComponent.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString StableNodeName(UPCGGraph* Graph, UPCGNode* Node)
		{
			if (!Graph || !Node)
				return FString();
			if (Node == Graph->GetInputNode())
				return TEXT("input");
			if (Node == Graph->GetOutputNode())
				return TEXT("output");
			if (Node->HasAuthoredTitle())
				return Node->GetAuthoredTitleName().ToString();
			if (UPCGSettings* Settings = Node->GetSettings())
				return Settings->GetClass()->GetName();
			return Node->GetName();
		}

		FString NormalizeSettingsType(FString Value)
		{
			Value.TrimStartAndEndInline();
			if (Value.Contains(TEXT(".")))
				Value = Value.RightChop(Value.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
			Value.ReplaceInline(TEXT("_"), TEXT(""));
			Value.ReplaceInline(TEXT(" "), TEXT(""));
			Value.ToLowerInline();
			if (Value.StartsWith(TEXT("u")))
				Value.RightChopInline(1);
			if (Value.StartsWith(TEXT("pcg")))
				Value.RightChopInline(3);
			if (Value.EndsWith(TEXT("settings")))
				Value.LeftChopInline(8);
			return Value;
		}

		TArray<TSharedPtr<FJsonValue>> PinNames(const TArray<FPCGPinProperties>& Pins)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FPCGPinProperties& Pin : Pins)
				Values.Add(MakeShared<FJsonValueString>(Pin.Label.ToString()));
			return Values;
		}
	}

	UPCGGraph* FUnrealAgentMCPUnrealPCGAdapter::ResolveGraph(const TSharedPtr<FJsonObject>& Args, FString* OutError)
	{
		FString AssetPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			if (OutError)
				*OutError = TEXT("缺少必填 assetPath。");
			return nullptr;
		}
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *JsonConversion::NormalizeAssetObjectPath(AssetPath));
		if (!Graph && OutError)
		{
			*OutError = FString::Printf(TEXT("未找到 PCGGraph：%s"), *AssetPath);
		}
		return Graph;
	}

	UPCGNode* FUnrealAgentMCPUnrealPCGAdapter::ResolveNode(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty())
			return nullptr;
		if (Name.Equals(TEXT("input"), ESearchCase::IgnoreCase))
			return Graph->GetInputNode();
		if (Name.Equals(TEXT("output"), ESearchCase::IgnoreCase))
			return Graph->GetOutputNode();
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!Node)
				continue;
			const FString Stable = StableNodeName(Graph, Node);
			if (Stable.Equals(Name, ESearchCase::IgnoreCase) || Node->GetName().Equals(Name, ESearchCase::IgnoreCase) ||
				Node->GetDefaultTitle().ToString().Equals(Name, ESearchCase::IgnoreCase) ||
				(Node->GetSettings() && Node->GetSettings()->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Node;
			}
		}
		return nullptr;
	}

	UClass* FUnrealAgentMCPUnrealPCGAdapter::ResolveSettingsClass(const FString& NodeType)
	{
		const FString Normalized = NormalizeSettingsType(NodeType);
		if (Normalized.IsEmpty())
			return nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UPCGSettings::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}
			if (It->GetName().Equals(NodeType, ESearchCase::IgnoreCase) || NormalizeSettingsType(It->GetName()) == Normalized)
			{
				return *It;
			}
		}
		return nullptr;
	}

	UPCGComponent* FUnrealAgentMCPUnrealPCGAdapter::ResolveComponent(const FString& ActorLabel, AActor** OutActor)
	{
		if (OutActor)
			*OutActor = nullptr;
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World || ActorLabel.IsEmpty())
			return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) && !It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (UPCGComponent* Component = It->FindComponentByClass<UPCGComponent>())
			{
				if (OutActor)
					*OutActor = *It;
				return Component;
			}
		}
		return nullptr;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealPCGAdapter::MakeNodeJson(UPCGGraph* Graph, UPCGNode* Node, const bool bIncludeSettings)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), StableNodeName(Graph, Node));
		Json->SetStringField(TEXT("objectName"), Node->GetName());
		Json->SetBoolField(TEXT("isInput"), Node == Graph->GetInputNode());
		Json->SetBoolField(TEXT("isOutput"), Node == Graph->GetOutputNode());
		UPCGSettings* Settings = Node->GetSettings();
		Json->SetStringField(TEXT("settingsClass"), Settings ? Settings->GetClass()->GetPathName() : FString());
#if WITH_EDITOR
		int32 PositionX = 0;
		int32 PositionY = 0;
		Node->GetNodePosition(PositionX, PositionY);
		Json->SetNumberField(TEXT("posX"), PositionX);
		Json->SetNumberField(TEXT("posY"), PositionY);
#endif
		Json->SetArrayField(TEXT("inputPins"), PinNames(Node->InputPinProperties()));
		Json->SetArrayField(TEXT("outputPins"), PinNames(Node->OutputPinProperties()));
		if (bIncludeSettings && Settings)
		{
			TSharedRef<FJsonObject> SettingsJson = MakeShared<FJsonObject>();
			FJsonObjectConverter::UStructToJsonObject(Settings->GetClass(), Settings, SettingsJson, CPF_Edit, CPF_Transient | CPF_Deprecated);
			Json->SetObjectField(TEXT("settings"), SettingsJson);
		}
		return Json;
	}

	bool FUnrealAgentMCPUnrealPCGAdapter::PersistGraph(UPCGGraph* Graph, FString& OutError)
	{
		if (!Graph || !Graph->IsAsset())
			return true;
		UPackage* Package = Graph->GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Graph, *Filename, SaveArgs))
		{
			OutError = TEXT("PCGGraph 资产保存失败。");
			return false;
		}
		return true;
	}

	void FUnrealAgentMCPUnrealPCGAdapter::NotifyGraphChanged(UPCGGraph* Graph)
	{
		if (!Graph)
			return;
		Graph->ForceNotificationForEditor(EPCGChangeType::Structural | EPCGChangeType::Edge | EPCGChangeType::Node | EPCGChangeType::Settings);
		Graph->MarkPackageDirty();
	}
}
