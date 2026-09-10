// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.AI.cpp
 * @brief BehaviorTree、Blackboard、EQS、感知、StateTree 与 SmartObject 实现。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyAllTypes.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "SmartObjectDefinition.h"
#include "StateTree.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	using namespace GameplayPrivate;

	namespace
	{
		TArray<FAssetData> FindAssets(UClass* Class, const TSharedPtr<FJsonObject>& Args)
		{
			FARFilter Filter;
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = true;
			Filter.ClassPaths.Add(Class->GetClassPathName());
			Filter.PackagePaths.Add(*StringArg(Args, { TEXT("directory") }, TEXT("/Game")));
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
			return Assets;
		}

		FString AssetList(UClass* Class, const TSharedPtr<FJsonObject>& Args, const FString& Field)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FAssetData& Asset : FindAssets(Class, Args))
				Values.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(Field, Values);
			return Success(Result);
		}

		UBlackboardKeyType* NewKeyType(UBlackboardData* Blackboard, const FString& Type)
		{
			UClass* Class = Type.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) ? UBlackboardKeyType_Bool::StaticClass()
				: Type.Equals(TEXT("Int"), ESearchCase::IgnoreCase)            ? UBlackboardKeyType_Int::StaticClass()
				: Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase)          ? UBlackboardKeyType_Float::StaticClass()
				: Type.Equals(TEXT("String"), ESearchCase::IgnoreCase)         ? UBlackboardKeyType_String::StaticClass()
				: Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase)           ? UBlackboardKeyType_Name::StaticClass()
				: Type.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase)        ? UBlackboardKeyType_Rotator::StaticClass()
				: Type.Equals(TEXT("Object"), ESearchCase::IgnoreCase)         ? UBlackboardKeyType_Object::StaticClass()
				: Type.Equals(TEXT("Class"), ESearchCase::IgnoreCase)          ? UBlackboardKeyType_Class::StaticClass()
				: Type.Equals(TEXT("Enum"), ESearchCase::IgnoreCase)           ? UBlackboardKeyType_Enum::StaticClass()
																			   : UBlackboardKeyType_Vector::StaticClass();
			return NewObject<UBlackboardKeyType>(Blackboard, Class, NAME_None, RF_Transactional);
		}

		TSharedRef<FJsonObject> BlackboardJson(const UBlackboardData* Blackboard)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("parent"), Blackboard && Blackboard->Parent ? Blackboard->Parent->GetPathName() : TEXT("None"));
			TArray<TSharedPtr<FJsonValue>> Keys;
			if (Blackboard)
				for (const FBlackboardEntry& Entry : Blackboard->Keys)
				{
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("name"), Entry.EntryName.ToString());
					Item->SetStringField(TEXT("type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetPathName() : TEXT("None"));
					Keys.Add(MakeShared<FJsonValueObject>(Item));
				}
			Result->SetArrayField(TEXT("ownKeys"), Keys);
			return Result;
		}

		FString AddBlueprintComponent(const TSharedPtr<FJsonObject>& Args, const FString& ClassPath, const FString& Name)
		{
			TSharedRef<FJsonObject> Forward = MakeShared<FJsonObject>(*Args);
			Forward->SetStringField(TEXT("assetPath"), StringArg(Args, { TEXT("blueprintPath"), TEXT("assetPath") }));
			Forward->SetStringField(TEXT("componentClass"), ClassPath);
			Forward->SetStringField(TEXT("componentName"), Name);
			FUnrealAgentMCPUnrealBlueprintAdapter Adapter;
			return Adapter.ExecuteAction(TEXT("add_component"), Forward);
		}
	}

	FString FUnrealAgentMCPUnrealGameplayAdapter::AIAndAssets(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_behavior_trees"))
			return AssetList(UBehaviorTree::StaticClass(), Args, TEXT("assets"));
		if (Action == TEXT("list_eqs_queries"))
			return AssetList(UEnvQuery::StaticClass(), Args, TEXT("assets"));
		if (Action == TEXT("list_state_trees"))
			return AssetList(UStateTree::StaticClass(), Args, TEXT("assets"));
		if (Action == TEXT("create_blackboard") || Action == TEXT("create_behavior_tree") || Action == TEXT("create_eqs_query") || Action == TEXT("create_state_tree") ||
			Action == TEXT("create_smart_object_def"))
		{
			UClass* Class = Action == TEXT("create_blackboard") ? UBlackboardData::StaticClass()
				: Action == TEXT("create_behavior_tree")        ? UBehaviorTree::StaticClass()
				: Action == TEXT("create_eqs_query")            ? UEnvQuery::StaticClass()
				: Action == TEXT("create_state_tree")           ? UStateTree::StaticClass()
																: USmartObjectDefinition::StaticClass();
			FString Path;
			UObject* Asset = CreateAsset(Class, Args, TEXT("GameplayAsset"), Path);
			if (!Asset)
				return Error(TEXT("创建 Gameplay AI 资产失败。"));
			if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
				Tree->BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *StringArg(Args, { TEXT("blackboardPath") }));
			Save(Asset);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Path);
			return Success(Result);
		}
		if (Action == TEXT("add_perception"))
			return AddBlueprintComponent(Args, TEXT("/Script/AIModule.AIPerceptionComponent"), TEXT("AIPerception"));
		if (Action == TEXT("add_state_tree_component"))
			return AddBlueprintComponent(Args, TEXT("/Script/GameplayStateTreeModule.StateTreeComponent"), TEXT("StateTree"));
		if (Action == TEXT("add_smart_object_component"))
			return AddBlueprintComponent(Args, TEXT("/Script/SmartObjectsModule.SmartObjectComponent"), TEXT("SmartObject"));
		if (Action == TEXT("configure_sense"))
			return Error(TEXT("需要 Blueprint 中已有的 AIPerceptionComponent 和有效 senseType。"));
		if (Action == TEXT("get_state_tree_runtime"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("status"), TEXT("EditorNotRunning"));
			return Success(Result);
		}
		if (Action == TEXT("list_bt_node_classes"))
		{
			const FString Kind = StringArg(Args, { TEXT("kind") }).ToLower();
			TArray<TSharedPtr<FJsonValue>> Values;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				const bool bMatch = (Kind.IsEmpty() && It->IsChildOf(UBTNode::StaticClass())) || (Kind == TEXT("composite") && It->IsChildOf(UBTCompositeNode::StaticClass())) ||
					(Kind == TEXT("task") && It->IsChildOf(UBTTaskNode::StaticClass())) || (Kind == TEXT("decorator") && It->IsChildOf(UBTDecorator::StaticClass())) ||
					(Kind == TEXT("service") && It->IsChildOf(UBTService::StaticClass()));
				if (bMatch && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
					Values.Add(MakeShared<FJsonValueString>(It->GetPathName()));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("classes"), Values);
			return Success(Result);
		}

		if (Action.Contains(TEXT("blackboard")) && Action != TEXT("set_behavior_tree_blackboard"))
		{
			UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *StringArg(Args, { TEXT("blackboardPath"), TEXT("assetPath") }));
			if (!Blackboard)
				return Error(TEXT("找不到 BlackboardData。"));
			if (Action == TEXT("read_blackboard"))
				return Success(BlackboardJson(Blackboard));
			Blackboard->Modify();
			if (Action == TEXT("add_blackboard_key"))
			{
				FBlackboardEntry Entry;
				Entry.EntryName = *StringArg(Args, { TEXT("keyName") });
				Entry.KeyType = NewKeyType(Blackboard, StringArg(Args, { TEXT("keyType") }, TEXT("Vector")));
				if (Entry.EntryName.IsNone())
					return Error(TEXT("缺少 keyName。"));
				Blackboard->Keys.Add(MoveTemp(Entry));
			}
			else if (Action == TEXT("remove_blackboard_key"))
				Blackboard->Keys.RemoveAll(
					[&](const FBlackboardEntry& Entry)
					{
						return Entry.EntryName.ToString().Equals(StringArg(Args, { TEXT("keyName") }), ESearchCase::IgnoreCase);
					});
			else if (Action == TEXT("set_blackboard_parent"))
			{
				const FString ParentPath = StringArg(Args, { TEXT("parentPath") });
				Blackboard->Parent = ParentPath.IsEmpty() || ParentPath == TEXT("None") ? nullptr : LoadObject<UBlackboardData>(nullptr, *ParentPath);
				Blackboard->UpdateParentKeys();
			}
			Save(Blackboard);
			return Success(BlackboardJson(Blackboard));
		}

		if (Action == TEXT("get_behavior_tree_info") || Action == TEXT("read_behavior_tree_graph") || Action == TEXT("set_behavior_tree_blackboard"))
		{
			UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *StringArg(Args, { TEXT("behaviorTreePath"), TEXT("assetPath") }));
			if (!Tree)
				return Error(TEXT("找不到 BehaviorTree。"));
			if (Action == TEXT("set_behavior_tree_blackboard"))
			{
				Tree->Modify();
				Tree->BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *StringArg(Args, { TEXT("blackboardPath") }));
				Save(Tree);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Tree->GetPathName());
			Result->SetStringField(TEXT("blackboard"), Tree->BlackboardAsset ? Tree->BlackboardAsset->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("rootNode"), Tree->RootNode ? Tree->RootNode->GetClass()->GetPathName() : TEXT("None"));
			return Success(Result);
		}

		USmartObjectDefinition* Definition = LoadObject<USmartObjectDefinition>(nullptr, *StringArg(Args, { TEXT("assetPath") }));
		if (!Definition)
			return Error(TEXT("找不到 SmartObjectDefinition。"));
		if (Action == TEXT("list_smart_object_slots"))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			int32 Index = 0;
			for (const FSmartObjectSlotDefinition& Slot : Definition->GetSlots())
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("index"), Index++);
				Item->SetNumberField(TEXT("x"), Slot.Offset.X);
				Item->SetNumberField(TEXT("y"), Slot.Offset.Y);
				Item->SetNumberField(TEXT("z"), Slot.Offset.Z);
				Values.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("slots"), Values);
			return Success(Result);
		}
		Definition->Modify();
		const int32 SlotIndex = static_cast<int32>(NumberArg(Args, TEXT("slotIndex"), 0));
		if (Action == TEXT("add_smart_object_slot"))
		{
			FSmartObjectSlotDefinition& Slot = Definition->DebugAddSlot();
			const FVector Offset = VectorArg(Args, TEXT("offset"));
			Slot.Offset = FVector3f(Offset);
#if WITH_EDITORONLY_DATA
			Slot.Name = *StringArg(Args, { TEXT("name") }, TEXT("MCP_Slot"));
			Slot.ID = FGuid::NewGuid();
#endif
		}
		else if (Action == TEXT("set_smart_object_slot"))
		{
			if (!Definition->IsValidSlotIndex(SlotIndex))
				return Error(TEXT("slotIndex 越界。"));
			FSmartObjectSlotDefinition& Slot = Definition->GetMutableSlot(SlotIndex);
			const FVector Offset = VectorArg(Args, TEXT("offset"), FVector(Slot.Offset));
			Slot.Offset = FVector3f(Offset);
		}
		else if (Action == TEXT("remove_smart_object_slot"))
		{
			FArrayProperty* SlotsProperty = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("Slots"));
			if (!SlotsProperty)
				return Error(TEXT("无法访问 Slots 属性。"));
			FScriptArrayHelper Helper(SlotsProperty, SlotsProperty->ContainerPtrToValuePtr<void>(Definition));
			if (Helper.IsValidIndex(SlotIndex))
				Helper.RemoveValues(SlotIndex, 1);
		}
		else if (Action == TEXT("add_smart_object_slot_behavior"))
		{
			if (!Definition->IsValidSlotIndex(SlotIndex))
				return Error(TEXT("slotIndex 越界。"));
			UClass* Class = LoadObject<UClass>(nullptr, *StringArg(Args, { TEXT("behaviorClass") }));
			if (!Class || !Class->IsChildOf(USmartObjectBehaviorDefinition::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
				return Error(TEXT("behaviorClass 无效。"));
			Definition->GetMutableSlot(SlotIndex).BehaviorDefinitions.Add(NewObject<USmartObjectBehaviorDefinition>(Definition, Class, NAME_None, RF_Transactional));
		}
		Save(Definition);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Definition->GetPathName());
		Result->SetNumberField(TEXT("slotCount"), Definition->GetSlots().Num());
		return Success(Result);
	}
}
