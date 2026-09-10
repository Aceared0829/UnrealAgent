// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Internal.h
 * @brief Material Adapter 各实现文件共享的原生类型解析与 JSON 辅助函数。
 */

#include "CoreMinimal.h"
#include "SceneTypes.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
class UMaterialInterface;
class UMaterialInstanceConstant;

namespace UnrealAgentMCP::MaterialInternal
{
	bool RequireString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutValue, FString& OutError);
	bool TryReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor);
	bool TryReadVector2(const TSharedPtr<FJsonValue>& Value, FVector2D& OutValue);

	UMaterialInterface* LoadMaterialInterface(const FString& AssetPath, FString& OutError);
	UMaterial* LoadMaterial(const FString& AssetPath, FString& OutError);
	UMaterialInstanceConstant* LoadMaterialInstance(const FString& AssetPath, FString& OutError);
	UMaterialFunction* LoadMaterialFunction(const FString& AssetPath, FString& OutError);

	bool SplitAssetPath(const FString& AssetPath, FString& OutPackagePath, FString& OutName, FString& OutError);
	bool SaveAsset(UObject* Asset, FString& OutError);
	UMaterial* CreateMaterial(const FString& Name, const FString& PackagePath, FString& OutError);
	UMaterialInstanceConstant* CreateMaterialInstance(const FString& Name, const FString& PackagePath, UMaterialInterface* Parent, FString& OutError);
	UMaterialFunction* CreateMaterialFunction(const FString& Name, const FString& PackagePath, FString& OutError);

	UClass* ResolveExpressionClass(const FString& ExpressionType, FString& OutError);
	UMaterialExpression* ResolveExpression(UMaterial* Material, const TSharedPtr<FJsonValue>& Identity);
	UMaterialExpression* ResolveFunctionExpression(UMaterialFunction* Function, const TSharedPtr<FJsonValue>& Identity);
	TSharedRef<FJsonObject> DescribeExpression(UMaterialExpression* Expression, int32 Index);

	bool ResolveMaterialProperty(const FString& Name, EMaterialProperty& OutProperty);
	FString MaterialPropertyName(EMaterialProperty Property);
	void MarkMaterialChanged(UMaterial* Material);
	void MarkFunctionChanged(UMaterialFunction* Function);
}
