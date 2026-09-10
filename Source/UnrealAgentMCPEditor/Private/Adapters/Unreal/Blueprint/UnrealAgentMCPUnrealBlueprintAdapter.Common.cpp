// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Common.cpp
 * @brief Blueprint 资产、图、节点、组件与属性公共实现。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.Internal.h"

#include "Components/ActorComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::BlueprintPrivate
{
	TSharedRef<FJsonObject> SuccessObject()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("blueprint"));
		return Result;
	}

	FString Serialize(const TSharedRef<FJsonObject>& Object)
	{
		return JsonObjectToString(Object);
	}

	FString Failure(const FString& Error)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("blueprint"));
		Result->SetStringField(TEXT("error"), Error);
		return Serialize(Result);
	}

	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& DefaultValue)
	{
		FString Value;
		if (Args.IsValid())
		{
			for (const TCHAR* Name : Names)
			{
				if (Args->TryGetStringField(Name, Value) && !Value.IsEmpty())
					return Value;
			}
		}
		return DefaultValue;
	}

	bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		return Args.IsValid() && Args->TryGetBoolField(Name, Value) ? Value : DefaultValue;
	}

	double NumberArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const double DefaultValue)
	{
		double Value = DefaultValue;
		return Args.IsValid() && Args->TryGetNumberField(Name, Value) ? Value : DefaultValue;
	}

	UBlueprint* LoadBlueprint(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		const FString Path = StringArg(Args, { TEXT("assetPath"), TEXT("blueprintPath"), TEXT("path") });
		if (Path.IsEmpty())
		{
			OutError = TEXT("缺少 Blueprint 资产路径。");
			return nullptr;
		}
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
		if (!Blueprint && !Path.Contains(TEXT(".")))
		{
			Blueprint = LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path)));
		}
		if (!Blueprint)
			OutError = FString::Printf(TEXT("找不到 Blueprint：%s"), *Path);
		return Blueprint;
	}

	UClass* ResolveClass(const FString& Name, UClass* RequiredBase)
	{
		if (Name.IsEmpty())
			return nullptr;
		UClass* Class = LoadObject<UClass>(nullptr, *Name);
		if (!Class)
		{
			Class = FindObject<UClass>(nullptr, *Name);
		}
		if (!Class)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) || It->GetName().Equals(Name + TEXT("_C"), ESearchCase::IgnoreCase))
				{
					Class = *It;
					break;
				}
			}
		}
		return Class && (!RequiredBase || Class->IsChildOf(RequiredBase)) ? Class : nullptr;
	}

	UEdGraph* ResolveGraph(UBlueprint* Blueprint, const FString& Name)
	{
		if (!Blueprint)
			return nullptr;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		const FString Wanted = Name.IsEmpty() ? TEXT("EventGraph") : Name;
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && (Graph->GetName().Equals(Wanted, ESearchCase::IgnoreCase) || Graph->GetFName().ToString().Equals(Wanted, ESearchCase::IgnoreCase)))
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UEdGraphNode* ResolveNode(UEdGraph* Graph, const FString& IdOrName)
	{
		if (!Graph || IdOrName.IsEmpty())
			return nullptr;
		FGuid Guid;
		const bool bGuid = FGuid::Parse(IdOrName, Guid);
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node &&
				((bGuid && Node->NodeGuid == Guid) || Node->GetName().Equals(IdOrName, ESearchCase::IgnoreCase) ||
					Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(IdOrName, ESearchCase::IgnoreCase)))
			{
				return Node;
			}
		}
		return nullptr;
	}

	UActorComponent* ResolveComponent(UBlueprint* Blueprint, const FString& Name)
	{
		if (!Blueprint || Name.IsEmpty())
			return nullptr;
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate &&
					(Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase) || Node->ComponentTemplate->GetName().Equals(Name, ESearchCase::IgnoreCase)))
				{
					return Node->ComponentTemplate;
				}
			}
		}
		UObject* Cdo = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
		if (AActor* Actor = Cast<AActor>(Cdo))
		{
			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
		}
		return nullptr;
	}

	bool SaveBlueprint(UBlueprint* Blueprint, const bool bStructural)
	{
		if (!Blueprint)
			return false;
		if (bStructural)
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		else
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false);
	}

	bool SetProperty(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Object || !Value.IsValid())
		{
			OutError = TEXT("属性写入对象或值无效。");
			return false;
		}
		FProperty* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("找不到属性：%s"), *PropertyName);
			return false;
		}
		Object->Modify();
		void* Address = Property->ContainerPtrToValuePtr<void>(Object);
		if (!FJsonObjectConverter::JsonValueToUProperty(Value, Property, Address, 0, 0))
		{
			FString Text;
			if (!Value->TryGetString(Text) || !Property->ImportText_Direct(*Text, Address, Object, PPF_None))
			{
				OutError = FString::Printf(TEXT("无法写入属性：%s"), *PropertyName);
				return false;
			}
		}
		Object->PostEditChange();
		return true;
	}

	TSharedPtr<FJsonValue> ReadProperty(UObject* Object, const FString& PropertyName)
	{
		if (!Object)
			return nullptr;
		FProperty* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
		return Property ? FJsonObjectConverter::UPropertyToJsonValue(Property, Property->ContainerPtrToValuePtr<void>(Object), 0, 0) : nullptr;
	}

	TSharedRef<FJsonObject> NodeJson(const UEdGraphNode* Node, const bool bIncludePins, const bool bIncludeDefaults, const bool bIncludeComments)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Node)
			return Result;
		Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		Result->SetStringField(TEXT("name"), Node->GetName());
		Result->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Result->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Result->SetNumberField(TEXT("posX"), Node->NodePosX);
		Result->SetNumberField(TEXT("posY"), Node->NodePosY);
		if (bIncludeComments)
			Result->SetStringField(TEXT("comment"), Node->NodeComment);
		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
					continue;
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Pin->PinName.ToString());
				Item->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
				Item->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				if (bIncludeDefaults)
					Item->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
				TArray<TSharedPtr<FJsonValue>> Links;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode())
					{
						Links.Add(MakeShared<FJsonValueString>(Linked->GetOwningNode()->NodeGuid.ToString() + TEXT(":") + Linked->PinName.ToString()));
					}
				}
				Item->SetArrayField(TEXT("links"), Links);
				Pins.Add(MakeShared<FJsonValueObject>(Item));
			}
			Result->SetArrayField(TEXT("pins"), Pins);
		}
		return Result;
	}
}
