// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Tree.cpp
 * @brief WidgetTree 结构编辑、绑定清理与批量属性写入实现。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.Internal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "WidgetBlueprint.h"

namespace UnrealAgentMCP
{
	using namespace WidgetInternal;

	namespace
	{
		FString FinishTreeChange(UWidgetBlueprint* Blueprint, const TSharedRef<FJsonObject>& Result, FString& Error, const bool bStructural = true)
		{
			MarkBlueprintChanged(Blueprint, bStructural);
			if (!SaveBlueprint(Blueprint, Error))
			{
				return ErrorJson(Error);
			}
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			return SuccessJson(Result);
		}

		bool DetachWidget(UWidgetBlueprint* Blueprint, UWidget* Widget)
		{
			if (!Blueprint || !Blueprint->WidgetTree || !Widget)
			{
				return false;
			}
			int32 ChildIndex = INDEX_NONE;
			if (UPanelWidget* Parent = UWidgetTree::FindWidgetParent(Widget, ChildIndex))
			{
				return Parent->RemoveChild(Widget);
			}
			if (Blueprint->WidgetTree->RootWidget == Widget)
			{
				Blueprint->WidgetTree->RootWidget = nullptr;
				return true;
			}
			return false;
		}

		bool RemoveWidgetSubtree(UWidgetBlueprint* Blueprint, UWidget* Widget)
		{
			if (!Blueprint || !Blueprint->WidgetTree || !Widget)
			{
				return false;
			}
			TArray<UWidget*> RemovedWidgets;
			RemovedWidgets.Add(Widget);
			UWidgetTree::GetChildWidgets(Widget, RemovedWidgets);
			const bool bRemoved = Blueprint->WidgetTree->RemoveWidget(Widget);
			if (!bRemoved)
			{
				return false;
			}
			for (UWidget* RemovedWidget : RemovedWidgets)
			{
				if (!RemovedWidget)
				{
					continue;
				}
				const FName RemovedName = RemovedWidget->GetFName();
				RemovedWidget->Modify();
				RemovedWidget->Rename(nullptr, GetTransientPackage());
				Blueprint->OnVariableRemoved(RemovedName);
			}
			return true;
		}

		bool ApplyPropertyObject(UWidget* Widget, const TSharedPtr<FJsonObject>& Properties, int32& OutChanged, FString& OutError)
		{
			if (!Widget || !Properties.IsValid())
			{
				OutError = TEXT("控件或 properties 对象无效。");
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
			{
				if (!SetPropertyValue(Widget, Pair.Key, Pair.Value, OutError))
				{
					return false;
				}
				++OutChanged;
			}
			return true;
		}
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::ExecuteTreeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		FString Error;
		if (!RequireString(Args, TEXT("assetPath"), AssetPath, Error))
		{
			Args->TryGetStringField(TEXT("path"), AssetPath);
			if (AssetPath.IsEmpty())
			{
				return ErrorJson(Error);
			}
		}
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(AssetPath, Error);
		if (!Blueprint || !Blueprint->WidgetTree)
		{
			return ErrorJson(Error.IsEmpty() ? TEXT("WidgetTree 无效。") : Error);
		}
		const Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(FText::FromString(TEXT("Unreal Agent 编辑 WidgetTree")));
		Blueprint->Modify();
		Blueprint->WidgetTree->Modify();

		if (Action == TEXT("clear_binding"))
		{
			FString WidgetName;
			FString PropertyName;
			Args->TryGetStringField(TEXT("widgetName"), WidgetName);
			Args->TryGetStringField(TEXT("propertyName"), PropertyName);
			const int32 Before = Blueprint->Bindings.Num();
			Blueprint->Bindings.RemoveAll(
				[&WidgetName, &PropertyName](const FDelegateEditorBinding& Binding)
				{
					return (WidgetName.IsEmpty() || Binding.ObjectName.Equals(WidgetName, ESearchCase::IgnoreCase)) &&
						(PropertyName.IsEmpty() || Binding.PropertyName.ToString().Equals(PropertyName, ESearchCase::IgnoreCase));
				});
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("removed"), Before - Blueprint->Bindings.Num());
			return FinishTreeChange(Blueprint, Result, Error, false);
		}

		if (Action == TEXT("bulk_set_properties"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!Args->TryGetArrayField(TEXT("properties"), Entries) || !Entries)
			{
				return ErrorJson(TEXT("properties 必须是属性操作数组。"));
			}
			int32 Changed = 0;
			for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (!EntryValue.IsValid() || !EntryValue->TryGetObject(Entry) || !Entry || !Entry->IsValid())
				{
					return ErrorJson(TEXT("properties 中存在无效条目。"));
				}
				FString WidgetName;
				FString PropertyName;
				(*Entry)->TryGetStringField(TEXT("widgetName"), WidgetName);
				if (!(*Entry)->TryGetStringField(TEXT("propertyName"), PropertyName))
				{
					return ErrorJson(TEXT("属性条目缺少 propertyName。"));
				}
				UWidget* Widget = ResolveWidget(Blueprint, WidgetName, Error, true);
				if (!Widget)
				{
					return ErrorJson(Error);
				}
				const TSharedPtr<FJsonValue>* Value = (*Entry)->Values.Find(TEXT("value"));
				if (!Value || !SetPropertyValue(Widget, PropertyName, *Value, Error))
				{
					return ErrorJson(Error.IsEmpty() ? TEXT("属性条目缺少 value。") : Error);
				}
				++Changed;
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("changed"), Changed);
			return FinishTreeChange(Blueprint, Result, Error, false);
		}

		FString WidgetName;
		Args->TryGetStringField(TEXT("widgetName"), WidgetName);
		if (Action == TEXT("set_property") || Action == TEXT("set_style"))
		{
			UWidget* Widget = ResolveWidget(Blueprint, WidgetName, Error, true);
			if (!Widget)
			{
				return ErrorJson(Error);
			}
			int32 Changed = 0;
			if (Action == TEXT("set_property"))
			{
				FString PropertyName;
				if (!RequireString(Args, TEXT("propertyName"), PropertyName, Error))
				{
					return ErrorJson(Error);
				}
				const TSharedPtr<FJsonValue>* Value = Args->Values.Find(TEXT("value"));
				if (!Value || !SetPropertyValue(Widget, PropertyName, *Value, Error))
				{
					return ErrorJson(Error.IsEmpty() ? TEXT("缺少必填 value。") : Error);
				}
				Changed = 1;
			}
			else
			{
				const TSharedPtr<FJsonObject>* Properties = nullptr;
				if (!Args->TryGetObjectField(TEXT("properties"), Properties) || !Properties || !ApplyPropertyObject(Widget, *Properties, Changed, Error))
				{
					return ErrorJson(Error.IsEmpty() ? TEXT("set_style 需要 properties 对象。") : Error);
				}
			}
			TSharedRef<FJsonObject> Result = DescribeWidget(Widget, false);
			Result->SetNumberField(TEXT("changed"), Changed);
			return FinishTreeChange(Blueprint, Result, Error, false);
		}

		if (Action == TEXT("add_widget"))
		{
			FString ClassName;
			if (!RequireString(Args, TEXT("className"), ClassName, Error))
			{
				Args->TryGetStringField(TEXT("widgetClass"), ClassName);
				if (ClassName.IsEmpty())
				{
					return ErrorJson(Error);
				}
			}
			FString NewName;
			if (!Args->TryGetStringField(TEXT("name"), NewName))
			{
				Args->TryGetStringField(TEXT("childName"), NewName);
			}
			if (NewName.IsEmpty())
			{
				NewName = TEXT("Widget");
			}
			UClass* Class = ResolveWidgetClass(ClassName, Error);
			if (!Class)
			{
				return ErrorJson(Error);
			}
			if (Blueprint->WidgetTree->FindWidget(FName(*NewName)))
			{
				return ErrorJson(FString::Printf(TEXT("控件名称已存在：%s"), *NewName));
			}
			UWidget* NewWidget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, FName(*NewName));
			if (!NewWidget)
			{
				return ErrorJson(TEXT("创建控件实例失败。"));
			}
			FString ParentName;
			Args->TryGetStringField(TEXT("parentWidgetName"), ParentName);
			if (!Blueprint->WidgetTree->RootWidget)
			{
				Blueprint->WidgetTree->RootWidget = NewWidget;
			}
			else
			{
				UPanelWidget* Parent = ResolvePanel(Blueprint, ParentName, Error);
				if (!Parent || !Parent->AddChild(NewWidget))
				{
					NewWidget->Rename(nullptr, GetTransientPackage());
					return ErrorJson(Error.IsEmpty() ? TEXT("父 Panel 无法接收新控件。") : Error);
				}
			}
			Blueprint->OnVariableAdded(NewWidget->GetFName());
			TSharedRef<FJsonObject> Result = DescribeWidget(NewWidget, false);
			return FinishTreeChange(Blueprint, Result, Error);
		}

		UWidget* Widget = ResolveWidget(Blueprint, WidgetName, Error, false);
		if (!Widget)
		{
			return ErrorJson(Error);
		}
		if (Action == TEXT("remove_widget"))
		{
			const FString RemovedName = Widget->GetName();
			const bool bRemoved = RemoveWidgetSubtree(Blueprint, Widget);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("widgetName"), RemovedName);
			Result->SetBoolField(TEXT("removed"), bRemoved);
			return bRemoved ? FinishTreeChange(Blueprint, Result, Error) : ErrorJson(TEXT("控件未能从 WidgetTree 移除。"));
		}
		if (Action == TEXT("reorder_child"))
		{
			FString ParentName;
			Args->TryGetStringField(TEXT("parentWidgetName"), ParentName);
			UPanelWidget* Parent = ParentName.IsEmpty() ? Cast<UPanelWidget>(Widget->GetParent()) : ResolvePanel(Blueprint, ParentName, Error);
			double RequestedIndex = 0.0;
			Args->TryGetNumberField(TEXT("index"), RequestedIndex);
			if (!Parent || Parent->GetChildIndex(Widget) == INDEX_NONE)
			{
				return ErrorJson(TEXT("控件不属于指定父 Panel。"));
			}
			const int32 NewIndex = FMath::Clamp(static_cast<int32>(RequestedIndex), 0, FMath::Max(0, Parent->GetChildrenCount() - 1));
			Parent->ShiftChild(NewIndex, Widget);
			TSharedRef<FJsonObject> Result = DescribeWidget(Widget, false);
			Result->SetNumberField(TEXT("index"), NewIndex);
			return FinishTreeChange(Blueprint, Result, Error);
		}
		if (Action == TEXT("move_widget"))
		{
			FString NewParentName;
			if (!RequireString(Args, TEXT("newParentWidgetName"), NewParentName, Error))
			{
				return ErrorJson(Error);
			}
			UPanelWidget* NewParent = ResolvePanel(Blueprint, NewParentName, Error);
			if (!NewParent || NewParent == Widget)
			{
				return ErrorJson(Error.IsEmpty() ? TEXT("新父控件无效。") : Error);
			}
			DetachWidget(Blueprint, Widget);
			if (!NewParent->AddChild(Widget))
			{
				return ErrorJson(TEXT("新父 Panel 无法接收控件。"));
			}
			TSharedRef<FJsonObject> Result = DescribeWidget(Widget, false);
			Result->SetStringField(TEXT("parentWidgetName"), NewParent->GetName());
			return FinishTreeChange(Blueprint, Result, Error);
		}
		if (Action == TEXT("set_root"))
		{
			UWidget* OldRoot = Blueprint->WidgetTree->RootWidget.Get();
			DetachWidget(Blueprint, Widget);
			if (OldRoot && OldRoot != Widget)
			{
				RemoveWidgetSubtree(Blueprint, OldRoot);
			}
			Blueprint->WidgetTree->RootWidget = Widget;
			TSharedRef<FJsonObject> Result = DescribeWidget(Widget, false);
			return FinishTreeChange(Blueprint, Result, Error);
		}
		if (Action == TEXT("wrap_root"))
		{
			UWidget* OldRoot = Blueprint->WidgetTree->RootWidget.Get();
			if (Widget != OldRoot)
			{
				return ErrorJson(TEXT("wrap_root 的 widgetName 必须指向当前根控件。"));
			}
			FString WrapperClass;
			FString WrapperName = TEXT("RootWrapper");
			if (!RequireString(Args, TEXT("wrapperClass"), WrapperClass, Error))
			{
				return ErrorJson(Error);
			}
			Args->TryGetStringField(TEXT("wrapperName"), WrapperName);
			UClass* Class = ResolveWidgetClass(WrapperClass, Error);
			UPanelWidget* Wrapper = Class ? Cast<UPanelWidget>(Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, FName(*WrapperName))) : nullptr;
			if (!Wrapper)
			{
				if (UWidget* InvalidWrapper = Blueprint->WidgetTree->FindWidget(FName(*WrapperName)))
				{
					InvalidWrapper->Rename(nullptr, GetTransientPackage());
				}
				return ErrorJson(Error.IsEmpty() ? TEXT("wrapperClass 必须派生自 PanelWidget。") : Error);
			}
			Blueprint->OnVariableAdded(Wrapper->GetFName());
			Blueprint->WidgetTree->RootWidget = Wrapper;
			if (!Wrapper->AddChild(OldRoot))
			{
				Blueprint->WidgetTree->RootWidget = OldRoot;
				return ErrorJson(TEXT("包装控件无法接收原根控件。"));
			}
			TSharedRef<FJsonObject> Result = DescribeWidget(Wrapper, false);
			return FinishTreeChange(Blueprint, Result, Error);
		}

		return ErrorJson(FString::Printf(TEXT("未实现树操作：%s"), *Action));
	}
}
