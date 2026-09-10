// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Query.cpp
 * @brief Widget 蓝图树、属性、绑定、动画、资产与类目录查询。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.Internal.h"
#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MovieScene.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealAgentMCP
{
	using namespace WidgetInternal;

	namespace
	{
		TSharedRef<FJsonObject> DescribeTreeNode(UWidget* Widget, const int32 Depth, const int32 MaxDepth, const bool bIncludeProperties)
		{
			TSharedRef<FJsonObject> Node = DescribeWidget(Widget, bIncludeProperties);
			TArray<TSharedPtr<FJsonValue>> Children;
			if (Depth < MaxDepth)
			{
				if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
				{
					for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
					{
						Children.Add(MakeShared<FJsonValueObject>(DescribeTreeNode(Panel->GetChildAt(Index), Depth + 1, MaxDepth, bIncludeProperties)));
					}
				}
			}
			Node->SetArrayField(TEXT("children"), Children);
			return Node;
		}

		bool MatchesText(const FString& Value, const FString& Filter)
		{
			return Filter.IsEmpty() || Value.Contains(Filter, ESearchCase::IgnoreCase);
		}
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::ExecuteQueryAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_classes"))
		{
			FString Filter;
			Args->TryGetStringField(TEXT("classFilter"), Filter);
			TArray<TSharedPtr<FJsonValue>> Classes;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
					!MatchesText(Class->GetName(), Filter))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Class->GetName());
				Item->SetStringField(TEXT("path"), Class->GetPathName());
				Item->SetBoolField(TEXT("isPanel"), Class->IsChildOf(UPanelWidget::StaticClass()));
				Classes.Add(MakeShared<FJsonValueObject>(Item));
			}
			Classes.Sort(
				[](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
				{
					return Left->AsObject()->GetStringField(TEXT("name")) < Right->AsObject()->GetStringField(TEXT("name"));
				});
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Classes.Num());
			Result->SetArrayField(TEXT("classes"), Classes);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list"))
		{
			FString Directory = TEXT("/Game");
			FString NamePrefix;
			bool bRecursive = true;
			Args->TryGetStringField(TEXT("directory"), Directory);
			Args->TryGetStringField(TEXT("namePrefix"), NamePrefix);
			Args->TryGetBoolField(TEXT("recursive"), bRecursive);
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*Directory));
			Filter.ClassPaths.Add(UWidgetBlueprint::StaticClass()->GetClassPathName());
			Filter.bRecursivePaths = bRecursive;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FAssetData& Asset : Assets)
			{
				if (!MatchesText(Asset.AssetName.ToString(), NamePrefix))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Item->SetStringField(TEXT("assetPath"), Asset.GetObjectPathString());
				Item->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
				Items.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Items.Num());
			Result->SetArrayField(TEXT("widgets"), Items);
			return SuccessJson(Result);
		}

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
		if (!Blueprint)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("read_animations"))
		{
			TArray<TSharedPtr<FJsonValue>> Animations;
			for (UWidgetAnimation* Animation : Blueprint->Animations)
			{
				if (!Animation)
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Animation->GetName());
				Item->SetStringField(TEXT("path"), Animation->GetPathName());
				if (UMovieScene* Scene = Animation->GetMovieScene())
				{
					const UMovieScene* ReadOnlyScene = Scene;
					const TRange<FFrameNumber> Range = Scene->GetPlaybackRange();
					Item->SetNumberField(TEXT("startFrame"), Range.GetLowerBoundValue().Value);
					Item->SetNumberField(TEXT("endFrame"), Range.GetUpperBoundValue().Value);
					Item->SetNumberField(TEXT("bindingCount"), ReadOnlyScene->GetBindings().Num());
				}
				Animations.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			Result->SetNumberField(TEXT("count"), Animations.Num());
			Result->SetArrayField(TEXT("animations"), Animations);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_bindings"))
		{
			TArray<TSharedPtr<FJsonValue>> Bindings;
			for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("widgetName"), Binding.ObjectName);
				Item->SetStringField(TEXT("propertyName"), Binding.PropertyName.ToString());
				Item->SetStringField(TEXT("functionName"), Binding.FunctionName.ToString());
				Item->SetStringField(TEXT("sourceProperty"), Binding.SourceProperty.ToString());
				Bindings.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Bindings.Num());
			Result->SetArrayField(TEXT("bindings"), Bindings);
			return SuccessJson(Result);
		}

		FString WidgetName;
		Args->TryGetStringField(TEXT("widgetName"), WidgetName);
		UWidget* Widget = ResolveWidget(Blueprint, WidgetName, Error, true);
		if (!Widget)
		{
			return ErrorJson(Error);
		}
		if (Action == TEXT("read_tree"))
		{
			double RequestedDepth = 32.0;
			bool bIncludeProperties = false;
			Args->TryGetNumberField(TEXT("maxDepth"), RequestedDepth);
			Args->TryGetBoolField(TEXT("includeProperties"), bIncludeProperties);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			Result->SetObjectField(TEXT("root"), DescribeTreeNode(Widget, 0, FMath::Clamp(static_cast<int32>(RequestedDepth), 0, 128), bIncludeProperties));
			return SuccessJson(Result);
		}
		if (Action == TEXT("get_details"))
		{
			return SuccessJson(DescribeWidget(Widget, true));
		}

		FString PropertyFilter;
		Args->TryGetStringField(TEXT("filterProperty"), PropertyFilter);
		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!MatchesText(Property->GetName(), PropertyFilter) || Property->HasAnyPropertyFlags(CPF_Transient))
			{
				continue;
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Property->GetName());
			Item->SetStringField(TEXT("type"), Property->GetCPPType());
			Item->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_EditConst));
			Item->SetField(TEXT("value"), ExportPropertyValue(Widget, Property));
			Properties.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("widgetName"), Widget->GetName());
		Result->SetNumberField(TEXT("count"), Properties.Num());
		Result->SetArrayField(TEXT("properties"), Properties);
		return SuccessJson(Result);
	}
}
