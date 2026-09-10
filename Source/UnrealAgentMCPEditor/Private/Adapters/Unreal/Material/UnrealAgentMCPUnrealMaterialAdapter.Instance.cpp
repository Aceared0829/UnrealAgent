// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Instance.cpp
 * @brief 材质实例的父级、参数、静态开关和批处理实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP
{
	using namespace MaterialInternal;

	namespace
	{
		EMaterialParameterAssociation ReadAssociation(const TSharedPtr<FJsonObject>& Args)
		{
			FString Association;
			Args->TryGetStringField(TEXT("association"), Association);
			if (Association.Equals(TEXT("LayerParameter"), ESearchCase::IgnoreCase) || Association.Equals(TEXT("Layer"), ESearchCase::IgnoreCase))
			{
				return EMaterialParameterAssociation::LayerParameter;
			}
			if (Association.Equals(TEXT("BlendParameter"), ESearchCase::IgnoreCase) || Association.Equals(TEXT("Blend"), ESearchCase::IgnoreCase))
			{
				return EMaterialParameterAssociation::BlendParameter;
			}
			return EMaterialParameterAssociation::GlobalParameter;
		}

		FString AssociationName(const EMaterialParameterAssociation Association)
		{
			switch (Association)
			{
			case EMaterialParameterAssociation::LayerParameter:
				return TEXT("LayerParameter");
			case EMaterialParameterAssociation::BlendParameter:
				return TEXT("BlendParameter");
			default:
				return TEXT("GlobalParameter");
			}
		}

		FMaterialParameterInfo ReadParameterInfo(const TSharedPtr<FJsonObject>& Args, const FString& Name)
		{
			double Index = INDEX_NONE;
			Args->TryGetNumberField(TEXT("parameterIndex"), Index);
			return FMaterialParameterInfo(FName(*Name), ReadAssociation(Args), static_cast<int32>(Index));
		}

		bool ApplyParameter(UMaterialInstanceConstant* Instance, const FString& Name, const FString& Type, const TSharedPtr<FJsonValue>& Value, const FMaterialParameterInfo& Info,
			FString& OutError)
		{
			if (Type.Equals(TEXT("scalar"), ESearchCase::IgnoreCase))
			{
				double Number = 0.0;
				if (!Value.IsValid() || !Value->TryGetNumber(Number))
				{
					OutError = TEXT("scalar 参数值必须是数字。");
					return false;
				}
				Instance->SetScalarParameterValueEditorOnly(Info, static_cast<float>(Number));
				return true;
			}
			if (Type.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
			{
				FLinearColor Color;
				if (!TryReadColor(Value, Color))
				{
					OutError = TEXT("vector 参数值必须是颜色数组或对象。");
					return false;
				}
				Instance->SetVectorParameterValueEditorOnly(Info, Color);
				return true;
			}
			if (Type.Equals(TEXT("texture"), ESearchCase::IgnoreCase))
			{
				FString TexturePath;
				if (!Value.IsValid() || !Value->TryGetString(TexturePath))
				{
					OutError = TEXT("texture 参数值必须是资产路径。");
					return false;
				}
				UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
				if (!Texture)
				{
					UEditorAssetSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
					Texture = Subsystem ? Cast<UTexture>(Subsystem->LoadAsset(TexturePath)) : nullptr;
				}
				if (!Texture)
				{
					OutError = FString::Printf(TEXT("未找到纹理：%s"), *TexturePath);
					return false;
				}
				Instance->SetTextureParameterValueEditorOnly(Info, Texture);
				return true;
			}
			OutError = FString::Printf(TEXT("不支持的 parameterType：%s"), *Type);
			return false;
		}

		TSharedRef<FJsonObject> InstanceSummary(UMaterialInstanceConstant* Instance)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Instance->GetPathName());
			Result->SetStringField(TEXT("parentPath"), Instance->Parent ? Instance->Parent->GetPathName() : TEXT(""));

			TArray<TSharedPtr<FJsonValue>> Scalars;
			for (const FScalarParameterValue& Parameter : Instance->ScalarParameterValues)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Parameter.ParameterInfo.Name.ToString());
				Item->SetStringField(TEXT("type"), TEXT("scalar"));
				Item->SetNumberField(TEXT("value"), Parameter.ParameterValue);
				Item->SetStringField(TEXT("association"), AssociationName(Parameter.ParameterInfo.Association));
				Item->SetNumberField(TEXT("index"), Parameter.ParameterInfo.Index);
				Scalars.Add(MakeShared<FJsonValueObject>(Item));
			}
			TArray<TSharedPtr<FJsonValue>> Vectors;
			for (const FVectorParameterValue& Parameter : Instance->VectorParameterValues)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Parameter.ParameterInfo.Name.ToString());
				Item->SetStringField(TEXT("type"), TEXT("vector"));
				Item->SetObjectField(TEXT("value"), MakeShared<FJsonObject>());
				const TSharedPtr<FJsonObject> Color = Item->GetObjectField(TEXT("value"));
				Color->SetNumberField(TEXT("r"), Parameter.ParameterValue.R);
				Color->SetNumberField(TEXT("g"), Parameter.ParameterValue.G);
				Color->SetNumberField(TEXT("b"), Parameter.ParameterValue.B);
				Color->SetNumberField(TEXT("a"), Parameter.ParameterValue.A);
				Vectors.Add(MakeShared<FJsonValueObject>(Item));
			}
			TArray<TSharedPtr<FJsonValue>> Textures;
			for (const FTextureParameterValue& Parameter : Instance->TextureParameterValues)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Parameter.ParameterInfo.Name.ToString());
				Item->SetStringField(TEXT("type"), TEXT("texture"));
				Item->SetStringField(TEXT("value"), Parameter.ParameterValue ? Parameter.ParameterValue->GetPathName() : TEXT(""));
				Textures.Add(MakeShared<FJsonValueObject>(Item));
			}
			Result->SetArrayField(TEXT("scalarOverrides"), Scalars);
			Result->SetArrayField(TEXT("vectorOverrides"), Vectors);
			Result->SetArrayField(TEXT("textureOverrides"), Textures);
			Result->SetNumberField(TEXT("overrideCount"), Scalars.Num() + Vectors.Num() + Textures.Num());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteInstanceAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_instance"))
		{
			FString ParentPath;
			FString Error;
			if (!RequireString(Args, TEXT("parentPath"), ParentPath, Error))
			{
				return ErrorJson(Error);
			}
			UMaterialInterface* Parent = LoadMaterialInterface(ParentPath, Error);
			if (!Parent)
			{
				return ErrorJson(Error);
			}
			FString Name = TEXT("MI_") + Parent->GetName();
			FString PackagePath = TEXT("/Game/Materials");
			Args->TryGetStringField(TEXT("name"), Name);
			Args->TryGetStringField(TEXT("packagePath"), PackagePath);
			UMaterialInstanceConstant* Instance = CreateMaterialInstance(Name, PackagePath, Parent, Error);
			if (!Instance || !SaveAsset(Instance, Error))
			{
				return ErrorJson(Error);
			}
			return SuccessJson(InstanceSummary(Instance));
		}

		if (Action == TEXT("batch_set_instances"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Args->TryGetArrayField(TEXT("instances"), Items) || !Items)
			{
				return ErrorJson(TEXT("缺少 instances 数组。"));
			}
			TArray<TSharedPtr<FJsonValue>> Results;
			int32 Updated = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Items)
			{
				const TSharedPtr<FJsonObject>* Item = nullptr;
				TSharedRef<FJsonObject> ItemResult = MakeShared<FJsonObject>();
				if (!Value->TryGetObject(Item) || !Item || !Item->IsValid())
				{
					ItemResult->SetBoolField(TEXT("success"), false);
					ItemResult->SetStringField(TEXT("error"), TEXT("批处理项目必须是对象。"));
					Results.Add(MakeShared<FJsonValueObject>(ItemResult));
					continue;
				}
				FString AssetPath;
				FString Error;
				if (!RequireString(*Item, TEXT("assetPath"), AssetPath, Error))
				{
					ItemResult->SetBoolField(TEXT("success"), false);
					ItemResult->SetStringField(TEXT("error"), Error);
					Results.Add(MakeShared<FJsonValueObject>(ItemResult));
					continue;
				}
				UMaterialInstanceConstant* Instance = LoadMaterialInstance(AssetPath, Error);
				if (!Instance)
				{
					ItemResult->SetBoolField(TEXT("success"), false);
					ItemResult->SetStringField(TEXT("error"), Error);
					Results.Add(MakeShared<FJsonValueObject>(ItemResult));
					continue;
				}
				FString ParentPath;
				if ((*Item)->TryGetStringField(TEXT("parentPath"), ParentPath))
				{
					UMaterialInterface* Parent = LoadMaterialInterface(ParentPath, Error);
					if (!Parent)
					{
						ItemResult->SetBoolField(TEXT("success"), false);
						ItemResult->SetStringField(TEXT("error"), Error);
						Results.Add(MakeShared<FJsonValueObject>(ItemResult));
						continue;
					}
					UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
				}
				const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
				if ((*Item)->TryGetArrayField(TEXT("parameters"), Parameters) && Parameters)
				{
					for (const TSharedPtr<FJsonValue>& ParameterValue : *Parameters)
					{
						const TSharedPtr<FJsonObject>* Parameter = nullptr;
						if (!ParameterValue->TryGetObject(Parameter) || !Parameter || !Parameter->IsValid())
						{
							Error = TEXT("parameters 项必须是对象。");
							break;
						}
						FString Name;
						FString Type;
						if (!RequireString(*Parameter, TEXT("name"), Name, Error) || !RequireString(*Parameter, TEXT("type"), Type, Error))
						{
							break;
						}
						if (!ApplyParameter(Instance, Name, Type, (*Parameter)->TryGetField(TEXT("value")), ReadParameterInfo(*Parameter, Name), Error))
						{
							break;
						}
					}
				}
				if (!Error.IsEmpty() || !SaveAsset(Instance, Error))
				{
					ItemResult->SetBoolField(TEXT("success"), false);
					ItemResult->SetStringField(TEXT("error"), Error);
				}
				else
				{
					++Updated;
					ItemResult = InstanceSummary(Instance);
					ItemResult->SetBoolField(TEXT("success"), true);
				}
				Results.Add(MakeShared<FJsonValueObject>(ItemResult));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("instances"), Results);
			Result->SetNumberField(TEXT("updated"), Updated);
			Result->SetNumberField(TEXT("requested"), Items->Num());
			return SuccessJson(Result);
		}

		FString AssetPath;
		FString Error;
		if (!RequireString(Args, TEXT("assetPath"), AssetPath, Error))
		{
			return ErrorJson(Error);
		}
		UMaterialInstanceConstant* Instance = LoadMaterialInstance(AssetPath, Error);
		if (!Instance)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("read_instance"))
		{
			return SuccessJson(InstanceSummary(Instance));
		}
		if (Action == TEXT("set_instance_parent"))
		{
			FString ParentPath;
			if (!Args->TryGetStringField(TEXT("newParentPath"), ParentPath) && !Args->TryGetStringField(TEXT("parentPath"), ParentPath))
			{
				return ErrorJson(TEXT("缺少 newParentPath 或 parentPath。"));
			}
			UMaterialInterface* Parent = LoadMaterialInterface(ParentPath, Error);
			if (!Parent)
			{
				return ErrorJson(Error);
			}
			UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
		}
		else if (Action == TEXT("clear_instance_parameters"))
		{
			UMaterialEditingLibrary::ClearAllMaterialInstanceParameters(Instance);
		}
		else if (Action == TEXT("set_parameter"))
		{
			FString Name;
			FString Type;
			if (!RequireString(Args, TEXT("parameterName"), Name, Error) || !RequireString(Args, TEXT("parameterType"), Type, Error) ||
				!ApplyParameter(Instance, Name, Type, Args->TryGetField(TEXT("value")), ReadParameterInfo(Args, Name), Error))
			{
				return ErrorJson(Error);
			}
		}
		else if (Action == TEXT("list_static_switches"))
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Instance->GetAllStaticSwitchParameterInfo(Infos, Ids);
			TArray<TSharedPtr<FJsonValue>> Switches;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				bool bValue = false;
				FGuid Guid;
				const bool bFound = Instance->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(Info), bValue, Guid);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Info.Name.ToString());
				Item->SetBoolField(TEXT("value"), bValue);
				Item->SetBoolField(TEXT("resolved"), bFound);
				Item->SetStringField(TEXT("association"), AssociationName(Info.Association));
				Item->SetNumberField(TEXT("index"), Info.Index);
				Switches.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = InstanceSummary(Instance);
			Result->SetArrayField(TEXT("switches"), Switches);
			Result->SetNumberField(TEXT("count"), Switches.Num());
			return SuccessJson(Result);
		}
		else if (Action == TEXT("set_static_switch"))
		{
			FString Name;
			bool bValue = false;
			if (!RequireString(Args, TEXT("parameterName"), Name, Error) || !Args->TryGetBoolField(TEXT("value"), bValue))
			{
				return ErrorJson(Error.IsEmpty() ? TEXT("缺少布尔 value。") : Error);
			}
			Instance->SetStaticSwitchParameterValueEditorOnly(ReadParameterInfo(Args, Name), bValue);
		}
		else
		{
			return ErrorJson(FString::Printf(TEXT("未实现的材质实例 action：%s"), *Action));
		}
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
		if (!SaveAsset(Instance, Error))
		{
			return ErrorJson(Error);
		}
		return SuccessJson(InstanceSummary(Instance));
	}
}
