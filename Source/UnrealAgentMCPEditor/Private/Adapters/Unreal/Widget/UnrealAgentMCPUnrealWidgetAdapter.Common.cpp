// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Common.cpp
 * @brief Widget 资产加载、类解析、属性转换和运行态定位的共享实现。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.Internal.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealAgentMCP::WidgetInternal
{
	bool RequireString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutValue, FString& OutError)
	{
		if (!Args.IsValid() || !Args->TryGetStringField(Field, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("缺少必填 %s。"), Field);
			return false;
		}
		OutValue.TrimStartAndEndInline();
		return true;
	}

	UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath, FString& OutError)
	{
		UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		UWidgetBlueprint* Blueprint = Assets ? Cast<UWidgetBlueprint>(Assets->LoadAsset(AssetPath)) : nullptr;
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("未找到 Widget Blueprint：%s"), *AssetPath);
		}
		return Blueprint;
	}

	bool SaveBlueprint(UWidgetBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint || !GEditor)
		{
			OutError = TEXT("Widget Blueprint 或编辑器上下文无效。");
			return false;
		}
		UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (!Assets || !Assets->SaveLoadedAsset(Blueprint, false))
		{
			OutError = FString::Printf(TEXT("保存 Widget Blueprint 失败：%s"), *Blueprint->GetPathName());
			return false;
		}
		return true;
	}

	UClass* ResolveWidgetClass(const FString& ClassName, FString& OutError)
	{
		if (ClassName.IsEmpty())
		{
			OutError = TEXT("缺少 Widget className。");
			return nullptr;
		}
		if (UClass* Loaded = StaticLoadClass(UWidget::StaticClass(), nullptr, *ClassName))
		{
			return Loaded;
		}
		FString Search = ClassName;
		Search.RemoveFromStart(TEXT("U"));
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate->IsChildOf(UWidget::StaticClass()) || Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			FString CandidateName = Candidate->GetName();
			CandidateName.RemoveFromStart(TEXT("U"));
			if (CandidateName.Equals(Search, ESearchCase::IgnoreCase) || Candidate->GetPathName().Equals(ClassName, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		OutError = FString::Printf(TEXT("未找到可实例化 Widget 类：%s"), *ClassName);
		return nullptr;
	}

	UWidget* ResolveWidget(UWidgetBlueprint* Blueprint, const FString& WidgetName, FString& OutError, const bool bAllowRootDefault)
	{
		if (!Blueprint || !Blueprint->WidgetTree)
		{
			OutError = TEXT("Widget Blueprint 没有有效 WidgetTree。");
			return nullptr;
		}
		UWidget* Widget = WidgetName.IsEmpty() && bAllowRootDefault ? Blueprint->WidgetTree->RootWidget.Get() : Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Widget)
		{
			OutError = WidgetName.IsEmpty() ? TEXT("WidgetTree 尚未设置根控件。") : FString::Printf(TEXT("未找到控件：%s"), *WidgetName);
		}
		return Widget;
	}

	UPanelWidget* ResolvePanel(UWidgetBlueprint* Blueprint, const FString& WidgetName, FString& OutError)
	{
		UWidget* Widget = ResolveWidget(Blueprint, WidgetName, OutError, true);
		UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
		if (Widget && !Panel)
		{
			OutError = FString::Printf(TEXT("控件 %s 不是 PanelWidget。"), *Widget->GetName());
		}
		return Panel;
	}

	void MarkBlueprintChanged(UWidgetBlueprint* Blueprint, const bool bStructural)
	{
		if (!Blueprint)
		{
			return;
		}
		Blueprint->Modify();
		if (Blueprint->WidgetTree)
		{
			Blueprint->WidgetTree->Modify();
		}
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
	}

	TSharedPtr<FJsonValue> ExportPropertyValue(UObject* Target, FProperty* Property)
	{
		if (!Target || !Property)
		{
			return MakeShared<FJsonValueNull>();
		}
		const void* Address = Property->ContainerPtrToValuePtr<void>(Target);
		if (const FBoolProperty* Typed = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(Typed->GetPropertyValue(Address));
		}
		if (const FNumericProperty* Typed = CastField<FNumericProperty>(Property))
		{
			return MakeShared<FJsonValueNumber>(
				Typed->IsInteger() ? static_cast<double>(Typed->GetSignedIntPropertyValue(Address)) : Typed->GetFloatingPointPropertyValue(Address));
		}
		if (const FStrProperty* Typed = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(Typed->GetPropertyValue(Address));
		}
		if (const FNameProperty* Typed = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(Typed->GetPropertyValue(Address).ToString());
		}
		if (const FTextProperty* Typed = CastField<FTextProperty>(Property))
		{
			return MakeShared<FJsonValueString>(Typed->GetPropertyValue(Address).ToString());
		}
		FString Text;
		Property->ExportTextItem_Direct(Text, Address, nullptr, Target, PPF_None);
		return MakeShared<FJsonValueString>(Text);
	}

	TSharedRef<FJsonObject> DescribeWidget(UWidget* Widget, const bool bIncludeProperties)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Widget)
		{
			return Result;
		}
		Result->SetStringField(TEXT("name"), Widget->GetName());
		Result->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
		Result->SetBoolField(TEXT("isVariable"), Widget->bIsVariable);
		if (Widget->Slot)
		{
			Result->SetStringField(TEXT("slotClass"), Widget->Slot->GetClass()->GetPathName());
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			Result->SetNumberField(TEXT("childCount"), Panel->GetChildrenCount());
		}
		if (bIncludeProperties)
		{
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
			{
				FProperty* Property = *It;
				if (Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_Transient))
				{
					Properties->SetField(Property->GetName(), ExportPropertyValue(Widget, Property));
				}
			}
			Result->SetObjectField(TEXT("properties"), Properties);
		}
		return Result;
	}

	TSharedRef<FJsonObject> DescribeRuntimeWidget(UUserWidget* Widget)
	{
		TSharedRef<FJsonObject> Result = DescribeWidget(Widget, false);
		if (!Widget)
		{
			return Result;
		}
		Result->SetStringField(TEXT("path"), Widget->GetPathName());
		Result->SetBoolField(TEXT("inViewport"), Widget->IsInViewport());
		if (Widget->GetWorld())
		{
			Result->SetStringField(TEXT("world"), Widget->GetWorld()->GetPathName());
		}
		return Result;
	}

	bool SetPropertyValue(UObject* Target, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Target || PropertyPath.IsEmpty())
		{
			OutError = TEXT("目标对象或 propertyName 无效。");
			return false;
		}
		UObject* Owner = Target;
		FString PropertyName = PropertyPath;
		FString Prefix;
		if (PropertyPath.Split(TEXT("."), &Prefix, &PropertyName) && Prefix.Equals(TEXT("Slot"), ESearchCase::IgnoreCase))
		{
			UWidget* Widget = Cast<UWidget>(Target);
			Owner = Widget ? Widget->Slot : nullptr;
		}
		if (!Owner)
		{
			OutError = TEXT("目标控件没有可写 Slot。");
			return false;
		}
		FProperty* Property = FindFProperty<FProperty>(Owner->GetClass(), FName(*PropertyName));
		if (!Property)
		{
			OutError = FString::Printf(TEXT("未找到属性：%s"), *PropertyPath);
			return false;
		}
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Object) && Object && Object->IsValid())
			{
				void* Address = Property->ContainerPtrToValuePtr<void>(Owner);
				if (FJsonObjectConverter::JsonObjectToUStruct(Object->ToSharedRef(), StructProperty->Struct, Address, 0, 0))
				{
					Owner->PostEditChange();
					return true;
				}
				OutError = FString::Printf(TEXT("结构体属性转换失败：%s"), *PropertyPath);
				return false;
			}
		}
		Owner->Modify();
		if (!PropertyWriter::SetPropertyFromJson(Owner, Property, Value, OutError))
		{
			return false;
		}
		Owner->PostEditChange();
		return true;
	}

	UUserWidget* FindRuntimeWidget(const FString& Identity, const bool bViewportOnly, FString& OutError)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Widget = *It;
			if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || (bViewportOnly && !Widget->IsInViewport()))
			{
				continue;
			}
			if (Identity.IsEmpty() || Widget->GetName().Equals(Identity, ESearchCase::IgnoreCase) || Widget->GetPathName().Equals(Identity, ESearchCase::IgnoreCase))
			{
				return Widget;
			}
		}
		OutError = FString::Printf(TEXT("未找到运行态 Widget：%s"), *Identity);
		return nullptr;
	}
}
