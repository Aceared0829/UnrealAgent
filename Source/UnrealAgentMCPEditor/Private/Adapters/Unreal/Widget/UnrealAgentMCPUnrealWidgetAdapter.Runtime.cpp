// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Runtime.cpp
 * @brief PIE 与运行态 Widget 实例、委托、视口创建及函数调用实现。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.Internal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	using namespace WidgetInternal;

	namespace
	{
		bool MatchesRuntimeFilters(UUserWidget* Widget, const FString& NameFilter, const FString& ClassFilter, const bool bViewportOnly)
		{
			return Widget && IsValid(Widget) && !Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && (!bViewportOnly || Widget->IsInViewport()) &&
				(NameFilter.IsEmpty() || Widget->GetName().Contains(NameFilter, ESearchCase::IgnoreCase)) &&
				(ClassFilter.IsEmpty() || Widget->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase));
		}

		UObject* ResolveRuntimeTarget(UUserWidget* Root, const FString& ChildName, FString& OutError)
		{
			if (!Root)
			{
				OutError = TEXT("运行态根 Widget 无效。");
				return nullptr;
			}
			if (ChildName.IsEmpty())
			{
				return Root;
			}
			UWidget* Child = Root->WidgetTree ? Root->WidgetTree->FindWidget(FName(*ChildName)) : nullptr;
			if (!Child)
			{
				OutError = FString::Printf(TEXT("未找到运行态子控件：%s"), *ChildName);
			}
			return Child;
		}

		TSharedRef<FJsonObject> DescribeRuntimeTree(UUserWidget* Widget)
		{
			TSharedRef<FJsonObject> Result = DescribeRuntimeWidget(Widget);
			TArray<TSharedPtr<FJsonValue>> Children;
			if (Widget && Widget->WidgetTree)
			{
				TArray<UWidget*> Widgets;
				Widget->WidgetTree->GetAllWidgets(Widgets);
				for (UWidget* Child : Widgets)
				{
					Children.Add(MakeShared<FJsonValueObject>(DescribeWidget(Child, false)));
				}
			}
			Result->SetArrayField(TEXT("subtree"), Children);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::ExecuteRuntimeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_runtime"))
		{
			FString NameFilter;
			FString ClassFilter;
			bool bViewportOnly = false;
			Args->TryGetStringField(TEXT("filterWidgetName"), NameFilter);
			Args->TryGetStringField(TEXT("classFilter"), ClassFilter);
			Args->TryGetBoolField(TEXT("viewportOnly"), bViewportOnly);
			TArray<TSharedPtr<FJsonValue>> Widgets;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				if (MatchesRuntimeFilters(*It, NameFilter, ClassFilter, bViewportOnly))
				{
					Widgets.Add(MakeShared<FJsonValueObject>(DescribeRuntimeWidget(*It)));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Widgets.Num());
			Result->SetArrayField(TEXT("widgets"), Widgets);
			return SuccessJson(Result);
		}

		if (Action == TEXT("add_to_viewport"))
		{
			FString WidgetClass;
			FString Error;
			if (!RequireString(Args, TEXT("widgetClass"), WidgetClass, Error))
			{
				Args->TryGetStringField(TEXT("className"), WidgetClass);
				if (WidgetClass.IsEmpty())
				{
					return ErrorJson(Error);
				}
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World)
			{
				return ErrorJson(TEXT("add_to_viewport 需要正在运行的 PIE 会话。"));
			}
			UClass* Class = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *WidgetClass);
			if (!Class)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 UserWidget 类：%s"), *WidgetClass));
			}
			UUserWidget* Widget = CreateWidget<UUserWidget>(World, Class);
			if (!Widget)
			{
				return ErrorJson(TEXT("运行态 Widget 创建失败。"));
			}
			double RequestedZOrder = 0.0;
			Args->TryGetNumberField(TEXT("zOrder"), RequestedZOrder);
			Widget->AddToViewport(static_cast<int32>(RequestedZOrder));
			TSharedRef<FJsonObject> Result = DescribeRuntimeWidget(Widget);
			Result->SetNumberField(TEXT("zOrder"), RequestedZOrder);
			return SuccessJson(Result);
		}

		FString Identity;
		bool bViewportOnly = false;
		Args->TryGetStringField(TEXT("widgetName"), Identity);
		if (Identity.IsEmpty())
		{
			Args->TryGetStringField(TEXT("path"), Identity);
		}
		Args->TryGetBoolField(TEXT("viewportOnly"), bViewportOnly);
		FString Error;
		UUserWidget* Root = FindRuntimeWidget(Identity, bViewportOnly, Error);
		if (!Root)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("get_runtime"))
		{
			bool bIncludeSubtree = true;
			Args->TryGetBoolField(TEXT("includeSubtree"), bIncludeSubtree);
			return SuccessJson(bIncludeSubtree ? DescribeRuntimeTree(Root) : DescribeRuntimeWidget(Root));
		}

		FString ChildName;
		Args->TryGetStringField(TEXT("childName"), ChildName);
		UObject* Target = ResolveRuntimeTarget(Root, ChildName, Error);
		if (!Target)
		{
			return ErrorJson(Error);
		}
		if (Action == TEXT("get_runtime_delegates"))
		{
			TArray<TSharedPtr<FJsonValue>> Delegates;
			for (TFieldIterator<FMulticastDelegateProperty> It(Target->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FMulticastDelegateProperty* Property = *It;
				FMulticastScriptDelegate* Delegate = Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Target);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Property->GetName());
				Item->SetBoolField(TEXT("bound"), Delegate && Delegate->IsBound());
				Delegates.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("target"), Target->GetPathName());
			Result->SetNumberField(TEXT("count"), Delegates.Num());
			Result->SetArrayField(TEXT("delegates"), Delegates);
			return SuccessJson(Result);
		}

		FString FunctionName;
		if (!RequireString(Args, TEXT("functionName"), FunctionName, Error))
		{
			return ErrorJson(Error);
		}
		if (UButton* Button = Cast<UButton>(Target); Button && FunctionName.Equals(TEXT("OnClicked"), ESearchCase::IgnoreCase))
		{
			Button->OnClicked.Broadcast();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("target"), Target->GetPathName());
			Result->SetStringField(TEXT("functionName"), FunctionName);
			Result->SetBoolField(TEXT("invoked"), true);
			return SuccessJson(Result);
		}
		UFunction* Function = Target->FindFunction(FName(*FunctionName));
		if (!Function)
		{
			return ErrorJson(FString::Printf(TEXT("未找到运行态函数：%s"), *FunctionName));
		}
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			if ((*It)->HasAnyPropertyFlags(CPF_Parm) && !(*It)->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				return ErrorJson(TEXT("当前调用仅接受无输入参数函数。"));
			}
		}
		TArray<uint8> Parameters;
		Parameters.SetNumZeroed(Function->ParmsSize);
		Target->ProcessEvent(Function, Parameters.GetData());
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("target"), Target->GetPathName());
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetBoolField(TEXT("invoked"), true);
		return SuccessJson(Result);
	}
}
