// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.State.cpp
 * @brief StateTree 状态层级、颜色与生命周期操作。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeState.h"

namespace UnrealAgentMCP
{
	using namespace StateTreePrivate;

	static EStateTreeStateType ParseStateType(const FString& Value)
	{
		if (Value.Equals(TEXT("Group"), ESearchCase::IgnoreCase))
			return EStateTreeStateType::Group;
		if (Value.Equals(TEXT("Linked"), ESearchCase::IgnoreCase))
			return EStateTreeStateType::Linked;
		if (Value.Equals(TEXT("LinkedAsset"), ESearchCase::IgnoreCase))
			return EStateTreeStateType::LinkedAsset;
		if (Value.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase))
			return EStateTreeStateType::Subtree;
		return EStateTreeStateType::State;
	}

	static EStateTreeStateSelectionBehavior ParseSelection(const FString& Value)
	{
		if (Value.Equals(TEXT("TryEnterState"), ESearchCase::IgnoreCase))
			return EStateTreeStateSelectionBehavior::TryEnterState;
		if (Value.Equals(TEXT("TrySelectChildrenAtRandom"), ESearchCase::IgnoreCase))
			return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
		if (Value.Equals(TEXT("TrySelectChildrenWithHighestUtility"), ESearchCase::IgnoreCase))
			return EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
		if (Value.Equals(TEXT("TrySelectChildrenWithWeightedRandom"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("TrySelectChildrenAtRandomWeightedByUtility"), ESearchCase::IgnoreCase))
		{
			return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
		}
		return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteStateAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (const FString Error = RequireString(Args, TEXT("assetPath"), AssetPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UStateTree* StateTree = LoadStateTree(AssetPath);
		UStateTreeEditorData* EditorData = GetEditorData(StateTree);
		if (!StateTree || !EditorData)
		{
			return Failure(FString::Printf(TEXT("找不到 StateTree 或其编辑数据：%s"), *AssetPath));
		}

		if (Action == TEXT("read") || Action == TEXT("list_states"))
		{
			TSharedRef<FJsonObject> Result = SuccessObject();
			TArray<TSharedPtr<FJsonValue>> States;
			if (Action == TEXT("read"))
			{
				for (const TObjectPtr<UStateTreeState>& Root : EditorData->SubTrees)
				{
					States.Add(MakeShared<FJsonValueObject>(SerializeState(Root)));
				}
				Result->SetArrayField(TEXT("subTrees"), States);
				TArray<TSharedPtr<FJsonValue>> Evaluators;
				for (const FStateTreeEditorNode& Node : EditorData->Evaluators)
				{
					Evaluators.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));
				}
				Result->SetArrayField(TEXT("evaluators"), Evaluators);
				TArray<TSharedPtr<FJsonValue>> GlobalTasks;
				for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
				{
					GlobalTasks.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));
				}
				Result->SetArrayField(TEXT("globalTasks"), GlobalTasks);
				Result->SetArrayField(TEXT("rootParameters"), SerializePropertyBag(EditorData->GetRootParametersPropertyBag()));
			}
			else
			{
				EditorData->VisitHierarchy(
					[&](UStateTreeState& State, UStateTreeState*)
					{
						TSharedRef<FJsonObject> Entry = SerializeState(&State);
						Entry->RemoveField(TEXT("tasks"));
						Entry->RemoveField(TEXT("children"));
						States.Add(MakeShared<FJsonValueObject>(Entry));
						return EStateTreeVisitor::Continue;
					});
				Result->SetArrayField(TEXT("states"), States);
			}
			Result->SetStringField(TEXT("assetPath"), StateTree->GetPathName());
			return Serialize(Result);
		}

		if (Action == TEXT("add_state"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			UStateTreeState* Parent = ResolveState(EditorData, Args);
			MarkModified(StateTree, Parent ? static_cast<UObject*>(Parent) : EditorData);
			UStateTreeState& NewState = Parent ? Parent->AddChildState(*Name, ParseStateType(OptionalString(Args, TEXT("stateType"))))
											   : EditorData->AddSubTree(*Name, ParseStateType(OptionalString(Args, TEXT("stateType"))));
			NewState.SelectionBehavior = ParseSelection(OptionalString(Args, TEXT("selectionBehavior")));
			const int32 InsertIndex = OptionalInt(Args, TEXT("insertIndex"));
			TArray<TObjectPtr<UStateTreeState>>& Siblings = Parent ? Parent->Children : EditorData->SubTrees;
			if (InsertIndex >= 0 && InsertIndex < Siblings.Num() - 1)
			{
				TObjectPtr<UStateTreeState> Moved = Siblings.Pop();
				Siblings.Insert(Moved, InsertIndex);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("stateId"), GuidString(NewState.ID));
			Result->SetStringField(TEXT("statePath"), NewState.GetPath());
			return Serialize(Result);
		}

		if (Action == TEXT("list_colors"))
		{
			TArray<TSharedPtr<FJsonValue>> Colors;
			for (const FStateTreeEditorColor& Color : EditorData->Colors)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("id"), GuidString(Color.ColorRef.ID));
				Entry->SetStringField(TEXT("displayName"), Color.DisplayName);
				Entry->SetStringField(TEXT("color"), Color.Color.ToString());
				Colors.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("colors"), Colors);
			return Serialize(Result);
		}

		if (Action == TEXT("add_color"))
		{
			FString DisplayName;
			if (const FString Error = RequireString(Args, TEXT("displayName"), DisplayName); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			FStateTreeEditorColor Color;
			Color.DisplayName = DisplayName;
			Color.Color.InitFromString(OptionalString(Args, TEXT("color"), TEXT("(R=0.2,G=0.6,B=1.0,A=1.0)")));
			MarkModified(StateTree, EditorData);
			EditorData->Colors.Add(Color);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("colorId"), GuidString(Color.ColorRef.ID));
			return Serialize(Result);
		}

		if (Action == TEXT("compile"))
		{
			FStateTreeCompilerLog Log;
			const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
			if (bCompiled)
			{
				SaveStateTree(StateTree);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("compiled"), bCompiled);
			return Serialize(Result);
		}
		if (Action == TEXT("validate"))
		{
			UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("validated"), true);
			return Serialize(Result);
		}

		UStateTreeState* State = ResolveState(EditorData, Args);
		if (!State)
		{
			return Failure(TEXT("找不到目标状态。"));
		}
		if (Action == TEXT("remove_state"))
		{
			UStateTreeState* Parent = State->Parent;
			MarkModified(StateTree, Parent ? static_cast<UObject*>(Parent) : EditorData);
			if (Parent)
				Parent->Children.Remove(State);
			else
				EditorData->SubTrees.Remove(State);
			UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("removed"), true);
			return Serialize(Result);
		}
		if (Action == TEXT("clear_state_nodes"))
		{
			MarkModified(StateTree, State);
			State->Tasks.Reset();
			State->EnterConditions.Reset();
			State->Transitions.Reset();
			State->SingleTask.Reset();
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("cleared"), true);
			return Serialize(Result);
		}

		FString PropertyName;
		if (const FString Error = RequireString(Args, TEXT("propertyName"), PropertyName); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
		if (!Value.IsValid())
		{
			return Failure(TEXT("缺少必填参数 'value'。"));
		}
		MarkModified(StateTree, State);
		if (PropertyName.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			State->Name = *Value->AsString();
		}
		else if (PropertyName.Equals(TEXT("description"), ESearchCase::IgnoreCase))
		{
			State->Description = Value->AsString();
		}
		else if (PropertyName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase) || PropertyName.Equals(TEXT("bEnabled"), ESearchCase::IgnoreCase))
		{
			State->bEnabled = Value->AsBool();
		}
		else if (PropertyName.Equals(TEXT("weight"), ESearchCase::IgnoreCase))
		{
			State->Weight = Value->AsNumber();
		}
		else if (PropertyName.Equals(TEXT("type"), ESearchCase::IgnoreCase))
		{
			State->Type = ParseStateType(Value->AsString());
		}
		else if (PropertyName.Equals(TEXT("selectionBehavior"), ESearchCase::IgnoreCase))
		{
			State->SelectionBehavior = ParseSelection(Value->AsString());
		}
		else if (PropertyName.Equals(TEXT("customTickRate"), ESearchCase::IgnoreCase))
		{
			State->CustomTickRate = Value->AsNumber();
			State->bHasCustomTickRate = true;
		}
		else if (PropertyName.Equals(TEXT("tag"), ESearchCase::IgnoreCase))
		{
			State->Tag = FGameplayTag::RequestGameplayTag(*Value->AsString(), false);
		}
		else if (PropertyName.Equals(TEXT("color"), ESearchCase::IgnoreCase))
		{
			const FString ColorValue = Value->AsString();
			for (const FStateTreeEditorColor& Color : EditorData->Colors)
			{
				if (Color.DisplayName.Equals(ColorValue, ESearchCase::IgnoreCase) || GuidString(Color.ColorRef.ID).Equals(ColorValue, ESearchCase::IgnoreCase))
				{
					State->ColorRef = Color.ColorRef;
					break;
				}
			}
		}
		else
		{
			return Failure(FString::Printf(TEXT("不支持的状态属性：%s"), *PropertyName));
		}
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetBoolField(TEXT("updated"), true);
		return Serialize(Result);
	}
}
