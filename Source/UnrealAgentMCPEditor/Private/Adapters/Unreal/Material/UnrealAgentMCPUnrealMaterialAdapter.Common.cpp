// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Common.cpp
 * @brief 材质路径、资产创建、表达式定位和属性映射的共享实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"

#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::MaterialInternal
{
	namespace
	{
		UObject* LoadAsset(const FString& AssetPath)
		{
			if (AssetPath.IsEmpty() || !GEditor)
			{
				return nullptr;
			}
			UEditorAssetSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			return Subsystem ? Subsystem->LoadAsset(AssetPath) : nullptr;
		}

		FString ExpressionLabel(UMaterialExpression* Expression)
		{
			if (!Expression)
			{
				return {};
			}
			if (!Expression->Desc.IsEmpty())
			{
				return Expression->Desc;
			}
			if (const FNameProperty* NameProperty = FindFProperty<FNameProperty>(Expression->GetClass(), TEXT("ParameterName")))
			{
				const void* Address = NameProperty->ContainerPtrToValuePtr<void>(Expression);
				const FName Name = NameProperty->GetPropertyValue(Address);
				if (!Name.IsNone())
				{
					return Name.ToString();
				}
			}
			return Expression->GetName();
		}

		UMaterialExpression* ResolveFromArray(const TArray<UMaterialExpression*>& Expressions, const TSharedPtr<FJsonValue>& Identity)
		{
			if (!Identity.IsValid())
			{
				return nullptr;
			}
			double Number = 0.0;
			if (Identity->TryGetNumber(Number))
			{
				const int32 Index = static_cast<int32>(Number);
				return Expressions.IsValidIndex(Index) ? Expressions[Index] : nullptr;
			}
			FString Text;
			if (!Identity->TryGetString(Text))
			{
				return nullptr;
			}
			if (Text.IsNumeric())
			{
				const int32 Index = FCString::Atoi(*Text);
				if (Expressions.IsValidIndex(Index))
				{
					return Expressions[Index];
				}
			}
			for (UMaterialExpression* Expression : Expressions)
			{
				if (Expression && (Expression->GetName().Equals(Text, ESearchCase::IgnoreCase) || ExpressionLabel(Expression).Equals(Text, ESearchCase::IgnoreCase)))
				{
					return Expression;
				}
			}
			return nullptr;
		}
	}

	bool RequireString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutValue, FString& OutError)
	{
		if (!Args.IsValid() || !Args->TryGetStringField(Field, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("缺少必填 %s。"), Field);
			return false;
		}
		OutValue = OutValue.TrimStartAndEnd();
		return true;
	}

	bool TryReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Value->TryGetArray(Values) && Values && Values->Num() >= 3)
		{
			OutColor.R = static_cast<float>((*Values)[0]->AsNumber());
			OutColor.G = static_cast<float>((*Values)[1]->AsNumber());
			OutColor.B = static_cast<float>((*Values)[2]->AsNumber());
			OutColor.A = Values->Num() > 3 ? static_cast<float>((*Values)[3]->AsNumber()) : 1.0f;
			return true;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value->TryGetObject(Object) && Object && Object->IsValid())
		{
			double Red = 0.0;
			double Green = 0.0;
			double Blue = 0.0;
			double Alpha = 1.0;
			const bool bHasColor = ((*Object)->TryGetNumberField(TEXT("r"), Red) || (*Object)->TryGetNumberField(TEXT("x"), Red)) &&
				((*Object)->TryGetNumberField(TEXT("g"), Green) || (*Object)->TryGetNumberField(TEXT("y"), Green)) &&
				((*Object)->TryGetNumberField(TEXT("b"), Blue) || (*Object)->TryGetNumberField(TEXT("z"), Blue));
			(*Object)->TryGetNumberField(TEXT("a"), Alpha);
			if (bHasColor)
			{
				OutColor = FLinearColor(static_cast<float>(Red), static_cast<float>(Green), static_cast<float>(Blue), static_cast<float>(Alpha));
				return true;
			}
		}
		return false;
	}

	bool TryReadVector2(const TSharedPtr<FJsonValue>& Value, FVector2D& OutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Value->TryGetArray(Values) && Values && Values->Num() >= 2)
		{
			OutValue.X = (*Values)[0]->AsNumber();
			OutValue.Y = (*Values)[1]->AsNumber();
			return true;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value->TryGetObject(Object) && Object && Object->IsValid())
		{
			return (*Object)->TryGetNumberField(TEXT("x"), OutValue.X) && (*Object)->TryGetNumberField(TEXT("y"), OutValue.Y);
		}
		return false;
	}

	UMaterialInterface* LoadMaterialInterface(const FString& AssetPath, FString& OutError)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(LoadAsset(AssetPath));
		if (!Material)
		{
			OutError = FString::Printf(TEXT("未找到材质资产：%s"), *AssetPath);
		}
		return Material;
	}

	UMaterial* LoadMaterial(const FString& AssetPath, FString& OutError)
	{
		UMaterial* Material = Cast<UMaterial>(LoadAsset(AssetPath));
		if (!Material)
		{
			OutError = FString::Printf(TEXT("未找到基础材质：%s"), *AssetPath);
		}
		return Material;
	}

	UMaterialInstanceConstant* LoadMaterialInstance(const FString& AssetPath, FString& OutError)
	{
		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAsset(AssetPath));
		if (!Instance)
		{
			OutError = FString::Printf(TEXT("未找到 MaterialInstanceConstant：%s"), *AssetPath);
		}
		return Instance;
	}

	UMaterialFunction* LoadMaterialFunction(const FString& AssetPath, FString& OutError)
	{
		UMaterialFunction* Function = Cast<UMaterialFunction>(LoadAsset(AssetPath));
		if (!Function)
		{
			OutError = FString::Printf(TEXT("未找到 MaterialFunction：%s"), *AssetPath);
		}
		return Function;
	}

	bool SplitAssetPath(const FString& AssetPath, FString& OutPackagePath, FString& OutName, FString& OutError)
	{
		FString Path = AssetPath;
		Path.TrimStartAndEndInline();
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			OutError = TEXT("资产路径必须位于 /Game。");
			return false;
		}
		const int32 DotIndex = Path.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (DotIndex != INDEX_NONE)
		{
			Path.LeftInline(DotIndex);
		}
		int32 SlashIndex = INDEX_NONE;
		if (!Path.FindLastChar(TEXT('/'), SlashIndex) || SlashIndex <= 5 || SlashIndex + 1 >= Path.Len())
		{
			OutError = TEXT("资产路径缺少有效的目录或名称。");
			return false;
		}
		OutPackagePath = Path.Left(SlashIndex);
		OutName = Path.Mid(SlashIndex + 1);
		return true;
	}

	bool SaveAsset(UObject* Asset, FString& OutError)
	{
		if (!Asset || !GEditor)
		{
			OutError = TEXT("资产或编辑器上下文不可用。");
			return false;
		}
		UEditorAssetSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (!Subsystem || !Subsystem->SaveAsset(Asset->GetPathName(), false))
		{
			OutError = FString::Printf(TEXT("保存资产失败：%s"), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	UMaterial* CreateMaterial(const FString& Name, const FString& PackagePath, FString& OutError)
	{
		if (!PackagePath.StartsWith(TEXT("/Game")))
		{
			OutError = TEXT("packagePath 必须位于 /Game。");
			return nullptr;
		}
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(FAssetToolsModule::GetModule().Get().CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory));
		if (!Material)
		{
			OutError = TEXT("基础材质创建失败。");
		}
		return Material;
	}

	UMaterialInstanceConstant* CreateMaterialInstance(const FString& Name, const FString& PackagePath, UMaterialInterface* Parent, FString& OutError)
	{
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Parent;
		UMaterialInstanceConstant* Instance =
			Cast<UMaterialInstanceConstant>(FAssetToolsModule::GetModule().Get().CreateAsset(Name, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory));
		if (!Instance)
		{
			OutError = TEXT("材质实例创建失败。");
		}
		return Instance;
	}

	UMaterialFunction* CreateMaterialFunction(const FString& Name, const FString& PackagePath, FString& OutError)
	{
		UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
		UMaterialFunction* Function = Cast<UMaterialFunction>(FAssetToolsModule::GetModule().Get().CreateAsset(Name, PackagePath, UMaterialFunction::StaticClass(), Factory));
		if (!Function)
		{
			OutError = TEXT("材质函数创建失败。");
		}
		return Function;
	}

	UClass* ResolveExpressionClass(const FString& ExpressionType, FString& OutError)
	{
		FString Type = ExpressionType;
		Type.TrimStartAndEndInline();
		if (Type.IsEmpty())
		{
			OutError = TEXT("缺少必填 expressionType。");
			return nullptr;
		}
		if (!Type.StartsWith(TEXT("MaterialExpression")))
		{
			Type = TEXT("MaterialExpression") + Type;
		}
		UClass* Class = FindObject<UClass>(nullptr, *Type);
		if (!Class)
		{
			Class = LoadObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + Type));
		}
		if (!Class || !Class->IsChildOf(UMaterialExpression::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = FString::Printf(TEXT("表达式类型无效或不可实例化：%s"), *ExpressionType);
			return nullptr;
		}
		return Class;
	}

	UMaterialExpression* ResolveExpression(UMaterial* Material, const TSharedPtr<FJsonValue>& Identity)
	{
		return Material ? ResolveFromArray(UMaterialEditingLibrary::GetMaterialExpressions(Material), Identity) : nullptr;
	}

	UMaterialExpression* ResolveFunctionExpression(UMaterialFunction* Function, const TSharedPtr<FJsonValue>& Identity)
	{
		return Function ? ResolveFromArray(UMaterialEditingLibrary::GetMaterialFunctionExpressions(Function), Identity) : nullptr;
	}

	TSharedRef<FJsonObject> DescribeExpression(UMaterialExpression* Expression, const int32 Index)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("name"), Expression ? Expression->GetName() : TEXT(""));
		if (!Expression)
		{
			Result->SetBoolField(TEXT("valid"), false);
			return Result;
		}
		FString Type = Expression->GetClass()->GetName();
		Type.RemoveFromStart(TEXT("MaterialExpression"));
		Result->SetBoolField(TEXT("valid"), true);
		Result->SetStringField(TEXT("class"), Expression->GetClass()->GetPathName());
		Result->SetStringField(TEXT("type"), Type);
		Result->SetStringField(TEXT("label"), ExpressionLabel(Expression));
		Result->SetStringField(TEXT("description"), Expression->Desc);
		Result->SetNumberField(TEXT("positionX"), Expression->MaterialExpressionEditorX);
		Result->SetNumberField(TEXT("positionY"), Expression->MaterialExpressionEditorY);
		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (const FString& Input : UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression))
		{
			Inputs.Add(MakeShared<FJsonValueString>(Input));
		}
		TArray<TSharedPtr<FJsonValue>> Outputs;
		for (const FString& Output : UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression))
		{
			Outputs.Add(MakeShared<FJsonValueString>(Output));
		}
		Result->SetArrayField(TEXT("inputs"), Inputs);
		Result->SetArrayField(TEXT("outputs"), Outputs);
		return Result;
	}

	bool ResolveMaterialProperty(const FString& Name, EMaterialProperty& OutProperty)
	{
		FString Key = Name;
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key = Key.ToLower();
		const TMap<FString, EMaterialProperty> Properties{ { TEXT("basecolor"), MP_BaseColor }, { TEXT("metallic"), MP_Metallic }, { TEXT("specular"), MP_Specular },
			{ TEXT("roughness"), MP_Roughness }, { TEXT("anisotropy"), MP_Anisotropy }, { TEXT("emissive"), MP_EmissiveColor }, { TEXT("emissivecolor"), MP_EmissiveColor },
			{ TEXT("opacity"), MP_Opacity }, { TEXT("opacitymask"), MP_OpacityMask }, { TEXT("normal"), MP_Normal }, { TEXT("tangent"), MP_Tangent },
			{ TEXT("worldpositionoffset"), MP_WorldPositionOffset }, { TEXT("subsurfacecolor"), MP_SubsurfaceColor }, { TEXT("ambientocclusion"), MP_AmbientOcclusion },
			{ TEXT("refraction"), MP_Refraction }, { TEXT("pixeldepthoffset"), MP_PixelDepthOffset }, { TEXT("materialattributes"), MP_MaterialAttributes },
			{ TEXT("customdata0"), MP_CustomData0 }, { TEXT("customdata1"), MP_CustomData1 } };
		if (const EMaterialProperty* Property = Properties.Find(Key))
		{
			OutProperty = *Property;
			return true;
		}
		return false;
	}

	FString MaterialPropertyName(const EMaterialProperty Property)
	{
		switch (Property)
		{
		case MP_BaseColor:
			return TEXT("BaseColor");
		case MP_Metallic:
			return TEXT("Metallic");
		case MP_Specular:
			return TEXT("Specular");
		case MP_Roughness:
			return TEXT("Roughness");
		case MP_Anisotropy:
			return TEXT("Anisotropy");
		case MP_EmissiveColor:
			return TEXT("EmissiveColor");
		case MP_Opacity:
			return TEXT("Opacity");
		case MP_OpacityMask:
			return TEXT("OpacityMask");
		case MP_Normal:
			return TEXT("Normal");
		case MP_Tangent:
			return TEXT("Tangent");
		case MP_WorldPositionOffset:
			return TEXT("WorldPositionOffset");
		case MP_SubsurfaceColor:
			return TEXT("SubsurfaceColor");
		case MP_AmbientOcclusion:
			return TEXT("AmbientOcclusion");
		case MP_Refraction:
			return TEXT("Refraction");
		case MP_PixelDepthOffset:
			return TEXT("PixelDepthOffset");
		case MP_MaterialAttributes:
			return TEXT("MaterialAttributes");
		case MP_CustomData0:
			return TEXT("CustomData0");
		case MP_CustomData1:
			return TEXT("CustomData1");
		default:
			return FString::Printf(TEXT("Property%d"), Property);
		}
	}

	void MarkMaterialChanged(UMaterial* Material)
	{
		if (!Material)
		{
			return;
		}
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}

	void MarkFunctionChanged(UMaterialFunction* Function)
	{
		if (!Function)
		{
			return;
		}
		Function->PreEditChange(nullptr);
		Function->PostEditChange();
		Function->MarkPackageDirty();
		UMaterialEditingLibrary::UpdateMaterialFunction(Function);
	}
}
