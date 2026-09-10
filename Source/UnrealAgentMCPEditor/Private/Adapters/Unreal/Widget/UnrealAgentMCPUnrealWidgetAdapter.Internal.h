// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Internal.h
 * @brief Widget Adapter 各实现文件共享的路径、反射和树操作辅助函数。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class FProperty;
class UPanelWidget;
class UUserWidget;
class UWidget;
class UWidgetBlueprint;

namespace UnrealAgentMCP::WidgetInternal
{
	bool RequireString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutValue, FString& OutError);
	UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath, FString& OutError);
	bool SaveBlueprint(UWidgetBlueprint* Blueprint, FString& OutError);
	UClass* ResolveWidgetClass(const FString& ClassName, FString& OutError);
	UWidget* ResolveWidget(UWidgetBlueprint* Blueprint, const FString& WidgetName, FString& OutError, bool bAllowRootDefault = false);
	UPanelWidget* ResolvePanel(UWidgetBlueprint* Blueprint, const FString& WidgetName, FString& OutError);
	void MarkBlueprintChanged(UWidgetBlueprint* Blueprint, bool bStructural = true);
	TSharedRef<FJsonObject> DescribeWidget(UWidget* Widget, bool bIncludeProperties = false);
	TSharedRef<FJsonObject> DescribeRuntimeWidget(UUserWidget* Widget);
	TSharedPtr<FJsonValue> ExportPropertyValue(UObject* Target, FProperty* Property);
	bool SetPropertyValue(UObject* Target, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	UUserWidget* FindRuntimeWidget(const FString& Identity, bool bViewportOnly, FString& OutError);
}
