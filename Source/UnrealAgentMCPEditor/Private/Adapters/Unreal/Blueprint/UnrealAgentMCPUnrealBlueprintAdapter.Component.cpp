// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Component.cpp
 * @brief Blueprint SCS 组件树、模板属性、材质、碰撞和构造脚本实现。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.Internal.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/MaterialInterface.h"

namespace UnrealAgentMCP
{
	using namespace BlueprintPrivate;

	namespace
	{
		USCS_Node* FindSCSNode(UBlueprint* Blueprint, const FString& Name)
		{
			if (!Blueprint || !Blueprint->SimpleConstructionScript)
				return nullptr;
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node &&
					(Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase) ||
						(Node->ComponentTemplate && Node->ComponentTemplate->GetName().Equals(Name, ESearchCase::IgnoreCase))))
					return Node;
			}
			return nullptr;
		}
	}

	FString FUnrealAgentMCPUnrealBlueprintAdapter::ExecuteComponent(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = LoadBlueprint(Args, Error);
		if (!Blueprint)
			return Failure(Error);
		if (!Blueprint->SimpleConstructionScript && Action != TEXT("set_actor_tick_settings") && Action != TEXT("run_construction_script") && Action != TEXT("add_timeline_track"))
		{
			return Failure(TEXT("该 Blueprint 不支持 SCS 组件树。"));
		}

		const FString ComponentName = StringArg(Args, { TEXT("componentName"), TEXT("name") });
		if (Action == TEXT("add_component"))
		{
			UClass* Class = ResolveClass(StringArg(Args, { TEXT("componentClass"), TEXT("class") }), UActorComponent::StaticClass());
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
				return Failure(TEXT("组件类无效。"));
			const FName VariableName = ComponentName.IsEmpty() ? NAME_None : FName(*ComponentName);
			USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, VariableName);
			if (!Node)
				return Failure(TEXT("创建组件节点失败。"));
			const FString ParentName = StringArg(Args, { TEXT("parentName"), TEXT("parentComponent") });
			if (USCS_Node* Parent = FindSCSNode(Blueprint, ParentName))
				Parent->AddChildNode(Node);
			else
				Blueprint->SimpleConstructionScript->AddNode(Node);
			SaveBlueprint(Blueprint, true);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("componentName"), Node->GetVariableName().ToString());
			return Serialize(Result);
		}

		if (Action == TEXT("remove_component"))
		{
			USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
			if (!Node)
				return Failure(TEXT("找不到组件节点。"));
			Blueprint->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Node);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("reparent_component"))
		{
			USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
			USCS_Node* Parent = FindSCSNode(Blueprint, StringArg(Args, { TEXT("parentName"), TEXT("parentComponent") }));
			if (!Node || !Parent || Node == Parent)
				return Failure(TEXT("组件或父组件无效。"));
			Blueprint->SimpleConstructionScript->RemoveNode(Node, false);
			Parent->AddChildNode(Node);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_component_property") || Action == TEXT("get_component_property"))
		{
			UActorComponent* Component = ResolveComponent(Blueprint, ComponentName);
			if (!Component)
				return Failure(TEXT("找不到组件模板。"));
			const FString PropertyName = StringArg(Args, { TEXT("propertyName") });
			if (Action == TEXT("get_component_property"))
			{
				TSharedPtr<FJsonValue> Value = ReadProperty(Component, PropertyName);
				if (!Value.IsValid())
					return Failure(TEXT("找不到组件属性。"));
				TSharedRef<FJsonObject> Result = SuccessObject();
				Result->SetField(TEXT("value"), Value);
				return Serialize(Result);
			}
			if (!SetProperty(Component, PropertyName, Args->TryGetField(TEXT("value")), Error))
				return Failure(Error);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("read_component_properties"))
		{
			UActorComponent* Component = ResolveComponent(Blueprint, ComponentName);
			if (!Component)
				return Failure(TEXT("找不到组件模板。"));
			TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Component->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
					continue;
				if (TSharedPtr<FJsonValue> Value = ReadProperty(Component, It->GetName()))
					Values->SetField(It->GetName(), Value);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetObjectField(TEXT("properties"), Values);
			return Serialize(Result);
		}
		else if (Action == TEXT("set_component_override_materials"))
		{
			UMeshComponent* Mesh = Cast<UMeshComponent>(ResolveComponent(Blueprint, ComponentName));
			const TArray<TSharedPtr<FJsonValue>>* Materials = nullptr;
			if (!Mesh || !Args->TryGetArrayField(TEXT("materials"), Materials))
				return Failure(TEXT("网格组件或 materials 无效。"));
			Mesh->Modify();
			for (int32 Index = 0; Index < Materials->Num(); ++Index)
			{
				FString Path;
				if ((*Materials)[Index].IsValid() && (*Materials)[Index]->TryGetString(Path))
				{
					Mesh->SetMaterial(Index, LoadObject<UMaterialInterface>(nullptr, *Path));
				}
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_capsule_size"))
		{
			UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(ResolveComponent(Blueprint, ComponentName));
			if (!Capsule)
				return Failure(TEXT("目标不是 CapsuleComponent。"));
			Capsule->Modify();
			Capsule->SetCapsuleSize(static_cast<float>(NumberArg(Args, TEXT("radius"), Capsule->GetUnscaledCapsuleRadius())),
				static_cast<float>(NumberArg(Args, TEXT("halfHeight"), Capsule->GetUnscaledCapsuleHalfHeight())), false);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("add_timeline_track"))
		{
			const FName TimelineName(*StringArg(Args, { TEXT("timelineName") }, TEXT("MCP_Timeline")));
			UTimelineTemplate* Timeline = Blueprint->FindTimelineTemplateByVariableName(TimelineName);
			if (!Timeline)
				Timeline = FBlueprintEditorUtils::AddNewTimeline(Blueprint, TimelineName);
			if (!Timeline)
				return Failure(TEXT("创建 Timeline 失败。"));
			const FName TrackName(*StringArg(Args, { TEXT("trackName") }, TEXT("MCP_Track")));
			if (!Timeline->IsNewTrackNameValid(TrackName))
				return Failure(TEXT("Timeline Track 名称重复。"));
			FTTFloatTrack& Track = Timeline->FloatTracks.AddDefaulted_GetRef();
			Track.SetTrackName(TrackName, Timeline);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_actor_tick_settings"))
		{
			AActor* Actor = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (!Actor)
				return Failure(TEXT("Blueprint 默认对象不是 Actor。"));
			Actor->Modify();
			Actor->PrimaryActorTick.bCanEverTick = BoolArg(Args, TEXT("canEverTick"), true);
			Actor->PrimaryActorTick.bStartWithTickEnabled = BoolArg(Args, TEXT("startEnabled"), true);
			Actor->PrimaryActorTick.TickInterval = static_cast<float>(NumberArg(Args, TEXT("tickInterval"), 0.0));
			SaveBlueprint(Blueprint, false);
		}
		else if (Action == TEXT("run_construction_script"))
		{
			int32 Count = 0;
			if (GEditor && GEditor->GetEditorWorldContext().World() && Blueprint->GeneratedClass)
			{
				for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
				{
					if (!It->IsA(Blueprint->GeneratedClass))
						continue;
					It->RerunConstructionScripts();
					++Count;
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("actorCount"), Count);
			return Serialize(Result);
		}
		else
			return Failure(FString::Printf(TEXT("未知组件操作：%s"), *Action));

		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		return Serialize(Result);
	}
}
