// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Texture.cpp
 * @brief 纹理发现、信息读取与批量设置实现。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> MakeTextureJson(UTexture* Texture)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Texture->GetPathName());
			Result->SetStringField(TEXT("class"), Texture->GetClass()->GetPathName());
			Result->SetBoolField(TEXT("sRGB"), Texture->SRGB);
			Result->SetStringField(TEXT("compressionSettings"), StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(static_cast<int64>(Texture->CompressionSettings)));
			Result->SetStringField(TEXT("lodGroup"), StaticEnum<TextureGroup>()->GetNameStringByValue(static_cast<int64>(Texture->LODGroup)));
			if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
			{
				Result->SetNumberField(TEXT("width"), Texture2D->GetSizeX());
				Result->SetNumberField(TEXT("height"), Texture2D->GetSizeY());
				Result->SetNumberField(TEXT("mipCount"), Texture2D->GetNumMips());
			}
			return Result;
		}

		bool ApplyTextureSettings(UTexture* Texture, const TSharedPtr<FJsonObject>& Settings, TArray<FString>& OutErrors)
		{
			if (!Texture || !Settings.IsValid())
			{
				OutErrors.Add(TEXT("纹理或 settings 无效。"));
				return false;
			}
			Texture->Modify();
			int32 Written = 0;
			for (const auto& Pair : Settings->Values)
			{
				FProperty* Property = FindFProperty<FProperty>(Texture->GetClass(), *Pair.Key);
				FString Error;
				if (!Property || !PropertyWriter::SetPropertyFromJson(Texture, Property, Pair.Value, Error))
				{
					OutErrors.Add(FString::Printf(TEXT("%s：%s"), *Pair.Key, Error.IsEmpty() ? TEXT("属性不存在") : *Error));
					continue;
				}
				++Written;
			}
			if (Written > 0)
			{
				Texture->PostEditChange();
				Texture->MarkPackageDirty();
			}
			return Written > 0;
		}

		TSharedPtr<FJsonObject> GetTextureSettingsArgument(const TSharedPtr<FJsonObject>& Args)
		{
			if (!Args.IsValid())
			{
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* Settings = nullptr;
			if (Args->TryGetObjectField(TEXT("settings"), Settings) && Settings)
			{
				return *Settings;
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			for (const TCHAR* Name : { TEXT("SRGB"), TEXT("CompressionSettings"), TEXT("LODGroup"), TEXT("NeverStream") })
			{
				const TSharedPtr<FJsonValue> Value = Args->TryGetField(Name);
				if (Value.IsValid())
				{
					Result->SetField(Name, Value);
				}
			}
			return Result->Values.IsEmpty() ? TSharedPtr<FJsonObject>() : TSharedPtr<FJsonObject>(Result);
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Textures(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		if (!Subsystem)
		{
			return ErrorJson(TEXT("EditorAssetSubsystem 当前不可用。"));
		}

		if (Action == TEXT("list_textures") || Action == TEXT("set_texture_settings_by_type"))
		{
			FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FARFilter Filter;
			Filter.ClassPaths.Add(UTexture::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = true;
			const FString Directory = GetStringArgument(Args, { TEXT("directory"), TEXT("packagePath") });
			if (!Directory.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Directory));
			}
			TArray<FAssetData> Assets;
			Module.Get().GetAssets(Filter, Assets);
			Assets.Sort(
				[](const FAssetData& Left, const FAssetData& Right)
				{
					return Left.PackageName.LexicalLess(Right.PackageName);
				});

			if (Action == TEXT("list_textures"))
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				for (const FAssetData& Asset : Assets)
				{
					Items.Add(MakeShared<FJsonValueObject>(MakeAssetJson(Asset)));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("count"), Items.Num());
				Result->SetArrayField(TEXT("textures"), Items);
				return SuccessJson(Result);
			}

			const TSharedPtr<FJsonObject> Settings = GetTextureSettingsArgument(Args);
			if (!Settings.IsValid())
			{
				return ErrorJson(TEXT("缺少 settings 对象。"));
			}
			int32 Updated = 0;
			TArray<TSharedPtr<FJsonValue>> Failures;
			for (const FAssetData& Asset : Assets)
			{
				UTexture* Texture = Cast<UTexture>(Subsystem->LoadAsset(Asset.GetSoftObjectPath().ToString()));
				TArray<FString> Errors;
				if (ApplyTextureSettings(Texture, Settings, Errors))
				{
					++Updated;
				}
				else
				{
					TSharedRef<FJsonObject> Failure = MakeShared<FJsonObject>();
					Failure->SetStringField(TEXT("assetPath"), Asset.GetSoftObjectPath().ToString());
					TArray<TSharedPtr<FJsonValue>> ErrorValues;
					for (const FString& Error : Errors)
					{
						ErrorValues.Add(MakeShared<FJsonValueString>(Error));
					}
					Failure->SetArrayField(TEXT("errors"), ErrorValues);
					Failures.Add(MakeShared<FJsonValueObject>(Failure));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("matched"), Assets.Num());
			Result->SetNumberField(TEXT("updated"), Updated);
			Result->SetArrayField(TEXT("failures"), Failures);
			return SuccessJson(Result);
		}

		const FString AssetPath = GetStringArgument(Args, { TEXT("assetPath"), TEXT("path"), TEXT("texturePath") });
		UTexture* Texture = Cast<UTexture>(Subsystem->LoadAsset(AssetPath));
		if (!Texture)
		{
			return ErrorJson(FString::Printf(TEXT("未找到纹理：%s"), *AssetPath));
		}
		if (Action == TEXT("get_texture_info"))
		{
			return SuccessJson(MakeTextureJson(Texture));
		}
		if (Action == TEXT("set_texture_settings"))
		{
			const TSharedPtr<FJsonObject> Settings = GetTextureSettingsArgument(Args);
			TArray<FString> Errors;
			if (!ApplyTextureSettings(Texture, Settings, Errors))
			{
				return ErrorJson(Errors.IsEmpty() ? TEXT("没有可写的纹理设置。") : FString::Join(Errors, TEXT("；")));
			}
			TSharedRef<FJsonObject> Result = MakeTextureJson(Texture);
			TArray<TSharedPtr<FJsonValue>> Warnings;
			for (const FString& Error : Errors)
			{
				Warnings.Add(MakeShared<FJsonValueString>(Error));
			}
			Result->SetArrayField(TEXT("warnings"), Warnings);
			return SuccessJson(Result);
		}
		return ErrorJson(FString::Printf(TEXT("纹理动作未进入有效分支：%s"), *Action));
	}
}
