// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Common.cpp
 * @brief StateTree JSON、资产、节点反射和 Property Bag 公共实现。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "StructUtils/PropertyBag.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeState.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::StateTreePrivate
{
	TSharedRef<FJsonObject> SuccessObject()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("statetree"));
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
		Result->SetStringField(TEXT("domain"), TEXT("statetree"));
		Result->SetStringField(TEXT("error"), Error);
		return Serialize(Result);
	}

	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue)
	{
		if (!Args.IsValid() || !Args->TryGetStringField(Name, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			return FString::Printf(TEXT("缺少必填参数 '%s'。"), *Name);
		}
		return FString();
	}

	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue)
	{
		FString Value;
		return Args.IsValid() && Args->TryGetStringField(Name, Value) ? Value : DefaultValue;
	}

	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, const int32 DefaultValue)
	{
		double Value = DefaultValue;
		return Args.IsValid() && Args->TryGetNumberField(Name, Value) ? static_cast<int32>(Value) : DefaultValue;
	}

	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		return Args.IsValid() && Args->TryGetBoolField(Name, Value) ? Value : DefaultValue;
	}

	UStateTree* LoadStateTree(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		return Cast<UStateTree>(StaticLoadObject(UStateTree::StaticClass(), nullptr, *AssetPath));
	}

	UStateTreeEditorData* GetEditorData(UStateTree* StateTree)
	{
		return StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
	}

	FGuid ParseGuid(const FString& Text)
	{
		FGuid Guid;
		FGuid::Parse(Text, Guid);
		return Guid;
	}

	FString GuidString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	UStateTreeState* ResolveState(UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Args)
	{
		if (!EditorData || !Args.IsValid())
		{
			return nullptr;
		}
		FString StateId;
		if (Args->TryGetStringField(TEXT("stateId"), StateId))
		{
			if (UStateTreeState* State = EditorData->GetMutableStateByID(ParseGuid(StateId)))
			{
				return State;
			}
		}
		FString StatePath;
		Args->TryGetStringField(TEXT("statePath"), StatePath);
		FString StateName;
		Args->TryGetStringField(TEXT("stateName"), StateName);
		UStateTreeState* Match = nullptr;
		EditorData->VisitHierarchy(
			[&](UStateTreeState& State, UStateTreeState*)
			{
				if ((!StatePath.IsEmpty() && State.GetPath().Equals(StatePath, ESearchCase::IgnoreCase)) ||
					(!StateName.IsEmpty() && State.Name.ToString().Equals(StateName, ESearchCase::IgnoreCase)))
				{
					Match = &State;
					return EStateTreeVisitor::Break;
				}
				return EStateTreeVisitor::Continue;
			});
		return Match;
	}

	bool SaveStateTree(UStateTree* StateTree)
	{
		if (!StateTree)
		{
			return false;
		}
		StateTree->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(StateTree, false);
	}

	void MarkModified(UStateTree* StateTree, UObject* Object)
	{
		if (Object)
		{
			Object->Modify();
		}
		if (StateTree)
		{
			UStateTreeEditingSubsystem::MarkAsModified(StateTree);
			StateTree->MarkPackageDirty();
		}
	}

	UScriptStruct* ResolveNodeStruct(const FString& StructType, const UScriptStruct* BaseStruct)
	{
		if (StructType.IsEmpty() || !BaseStruct)
		{
			return nullptr;
		}
		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructType);
		if (!Struct)
		{
			const FString ShortName = StructType.StartsWith(TEXT("F")) ? StructType.Mid(1) : StructType;
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName().Equals(StructType, ESearchCase::IgnoreCase) || It->GetName().Equals(ShortName, ESearchCase::IgnoreCase))
				{
					Struct = *It;
					break;
				}
			}
		}
		return Struct && Struct->IsChildOf(BaseStruct) ? Struct : nullptr;
	}

	bool AddNode(TArray<FStateTreeEditorNode>& Nodes, UObject* Outer, const FString& StructType, const UScriptStruct* BaseStruct, FStateTreeEditorNode*& OutNode, FString& OutError)
	{
		UScriptStruct* Struct = ResolveNodeStruct(StructType, BaseStruct);
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("找不到匹配基础类型的节点结构：%s"), *StructType);
			return false;
		}
		FStateTreeEditorNode& Node = Nodes.AddDefaulted_GetRef();
		Node.InitializeAs(Outer, Struct);
		OutNode = &Node;
		return true;
	}

	bool SetNodeProperty(FStateTreeEditorNode& Node, const bool bInstance, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		const UStruct* Struct = nullptr;
		void* Memory = nullptr;
		if (bInstance)
		{
			FStateTreeDataView View = Node.GetInstance();
			Struct = View.GetStruct();
			Memory = View.GetMutableMemory();
		}
		else
		{
			Struct = Node.Node.GetScriptStruct();
			Memory = Node.Node.GetMutableMemory();
		}
		if (!Struct || !Memory)
		{
			OutError = bInstance ? TEXT("节点没有实例数据。") : TEXT("节点模板数据无效。");
			return false;
		}
		FProperty* Property = Struct->FindPropertyByName(*PropertyName);
		if (!Property || !Value.IsValid())
		{
			OutError = FString::Printf(TEXT("找不到节点属性：%s"), *PropertyName);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Memory);
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			bool Parsed = false;
			if (Value->TryGetBool(Parsed))
			{
				Bool->SetPropertyValue(Address, Parsed);
				return true;
			}
		}
		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double Parsed = 0.0;
			if (Value->TryGetNumber(Parsed))
			{
				if (Numeric->IsInteger())
				{
					Numeric->SetIntPropertyValue(Address, static_cast<int64>(Parsed));
				}
				else
				{
					Numeric->SetFloatingPointPropertyValue(Address, Parsed);
				}
				return true;
			}
		}
		FString Text;
		if (!Value->TryGetString(Text) || !Property->ImportText_Direct(*Text, Address, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("无法写入节点属性：%s"), *PropertyName);
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> SerializeNode(const FStateTreeEditorNode& Node)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("id"), GuidString(Node.ID));
		Result->SetStringField(TEXT("name"), Node.GetName().ToString());
		Result->SetStringField(TEXT("structType"), Node.Node.GetScriptStruct() ? Node.Node.GetScriptStruct()->GetPathName() : FString());
		return Result;
	}

	TSharedRef<FJsonObject> SerializeState(const UStateTreeState* State)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!State)
		{
			return Result;
		}
		Result->SetStringField(TEXT("id"), GuidString(State->ID));
		Result->SetStringField(TEXT("name"), State->Name.ToString());
		Result->SetStringField(TEXT("path"), State->GetPath());
		Result->SetNumberField(TEXT("type"), static_cast<uint8>(State->Type));
		Result->SetBoolField(TEXT("enabled"), State->bEnabled);
		Result->SetStringField(TEXT("description"), State->Description);
		TArray<TSharedPtr<FJsonValue>> Tasks;
		for (const FStateTreeEditorNode& Node : State->Tasks)
		{
			Tasks.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));
		}
		Result->SetArrayField(TEXT("tasks"), Tasks);
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const TObjectPtr<UStateTreeState>& Child : State->Children)
		{
			Children.Add(MakeShared<FJsonValueObject>(SerializeState(Child)));
		}
		Result->SetArrayField(TEXT("children"), Children);
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> SerializePropertyBag(const FInstancedPropertyBag& Bag)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
		const uint8* Memory = Bag.GetValue().GetMemory();
		if (!BagStruct)
		{
			return Result;
		}
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Desc.Name.ToString());
			Entry->SetStringField(TEXT("id"), GuidString(Desc.ID));
			Entry->SetNumberField(TEXT("type"), static_cast<uint8>(Desc.ValueType));
			if (Memory)
			{
				if (FProperty* Property = BagStruct->FindPropertyByName(Desc.Name))
				{
					FString Text;
					Property->ExportTextItem_Direct(Text, Property->ContainerPtrToValuePtr<void>(Memory), nullptr, nullptr, PPF_None);
					Entry->SetStringField(TEXT("value"), Text);
				}
			}
			Result.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Result;
	}

	static bool ParseBagType(const FString& Text, EPropertyBagPropertyType& OutType)
	{
		const FString Type = Text.ToLower();
		if (Type == TEXT("bool"))
			OutType = EPropertyBagPropertyType::Bool;
		else if (Type == TEXT("byte"))
			OutType = EPropertyBagPropertyType::Byte;
		else if (Type == TEXT("int") || Type == TEXT("int32"))
			OutType = EPropertyBagPropertyType::Int32;
		else if (Type == TEXT("int64"))
			OutType = EPropertyBagPropertyType::Int64;
		else if (Type == TEXT("float"))
			OutType = EPropertyBagPropertyType::Float;
		else if (Type == TEXT("double"))
			OutType = EPropertyBagPropertyType::Double;
		else if (Type == TEXT("name"))
			OutType = EPropertyBagPropertyType::Name;
		else if (Type == TEXT("string"))
			OutType = EPropertyBagPropertyType::String;
		else if (Type == TEXT("text"))
			OutType = EPropertyBagPropertyType::Text;
		else
			return false;
		return true;
	}

	bool AddBagProperty(FInstancedPropertyBag& Bag, const FString& Name, const FString& Type, FString& OutError)
	{
		EPropertyBagPropertyType BagType;
		if (Name.IsEmpty() || !ParseBagType(Type, BagType))
		{
			OutError = TEXT("参数名称为空或参数类型不受支持。");
			return false;
		}
		if (Bag.FindPropertyDescByName(*Name))
		{
			OutError = FString::Printf(TEXT("参数已存在：%s"), *Name);
			return false;
		}
		Bag.AddProperty(*Name, BagType);
		return true;
	}

	bool SetBagValue(FInstancedPropertyBag& Bag, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
		FProperty* Property = BagStruct ? BagStruct->FindPropertyByName(*Name) : nullptr;
		uint8* Memory = Bag.GetMutableValue().GetMemory();
		if (!Property || !Memory || !Value.IsValid())
		{
			OutError = FString::Printf(TEXT("找不到参数：%s"), *Name);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Memory);
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			bool Parsed = false;
			if (Value->TryGetBool(Parsed))
			{
				Bool->SetPropertyValue(Address, Parsed);
				return true;
			}
		}
		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double Parsed = 0.0;
			if (Value->TryGetNumber(Parsed))
			{
				if (Numeric->IsInteger())
					Numeric->SetIntPropertyValue(Address, static_cast<int64>(Parsed));
				else
					Numeric->SetFloatingPointPropertyValue(Address, Parsed);
				return true;
			}
		}
		FString Text;
		if (!Value->TryGetString(Text) || !Property->ImportText_Direct(*Text, Address, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("无法写入参数：%s"), *Name);
			return false;
		}
		return true;
	}
}
