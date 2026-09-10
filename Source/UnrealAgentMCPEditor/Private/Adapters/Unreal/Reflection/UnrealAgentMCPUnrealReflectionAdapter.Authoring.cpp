// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealReflectionAdapter.Authoring.cpp
 * @brief Reflection 端口的 GameplayTag 与 UserDefinedEnum 写入实现。
 */

#include "Adapters/Unreal/Reflection/UnrealAgentMCPUnrealReflectionAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/UserDefinedEnum.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UnrealAgentMCP
{
	namespace
	{
		struct FEnumEntryInput
		{
			FString Name;
			FString DisplayName;
		};

		bool ParseEnumEntries(const TSharedPtr<FJsonObject>& Args, TArray<FEnumEntryInput>& OutEntries, FString& OutError, const bool bRequired)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Args->TryGetArrayField(TEXT("entries"), Values) || Values == nullptr)
			{
				if (bRequired)
				{
					OutError = TEXT("缺少必填 entries。");
					return false;
				}
				return true;
			}
			TSet<FString> Names;
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FEnumEntryInput Entry;
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Entry.Name = Value->AsString();
				}
				else if (Value.IsValid() && Value->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> Object = Value->AsObject();
					if (Object.IsValid())
					{
						Object->TryGetStringField(TEXT("name"), Entry.Name);
						Object->TryGetStringField(TEXT("displayName"), Entry.DisplayName);
					}
				}
				if (Entry.Name.IsEmpty() || !FName::IsValidXName(Entry.Name, INVALID_OBJECTNAME_CHARACTERS))
				{
					OutError = TEXT("枚举条目名称为空或包含非法字符。");
					return false;
				}
				if (Names.Contains(Entry.Name))
				{
					OutError = FString::Printf(TEXT("枚举条目重复：%s"), *Entry.Name);
					return false;
				}
				Names.Add(Entry.Name);
				if (Entry.DisplayName.IsEmpty())
					Entry.DisplayName = Entry.Name;
				OutEntries.Add(MoveTemp(Entry));
			}
			return true;
		}

		bool ApplyEnumEntries(UUserDefinedEnum* Enum, const TArray<FEnumEntryInput>& Entries, FString& OutError)
		{
			if (!Enum)
			{
				OutError = TEXT("目标 UserDefinedEnum 无效。");
				return false;
			}
			TArray<TPair<FName, int64>> Names;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				Names.Emplace(FName(*Enum->GenerateFullEnumName(*Entries[Index].Name)), Index);
			}
			if (!Enum->SetEnums(Names, UEnum::ECppForm::Namespaced, UEnum::EUnderlyingType::uint8, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes))
			{
				OutError = TEXT("枚举条目写入失败。");
				return false;
			}
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(Entries[Index].DisplayName));
			}
			Enum->PostEditChange();
			Enum->MarkPackageDirty();
			return true;
		}

		bool SaveEnumPackage(UUserDefinedEnum* Enum, FString& OutError)
		{
			UPackage* Package = Enum ? Enum->GetOutermost() : nullptr;
			if (!Package)
			{
				OutError = TEXT("枚举 Package 无效。");
				return false;
			}
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, Enum, *Filename, SaveArgs))
			{
				OutError = TEXT("枚举资产保存失败。");
				return false;
			}
			return true;
		}

		FString NormalizePackagePath(FString PackagePath)
		{
			if (PackagePath.IsEmpty())
				PackagePath = TEXT("/Game");
			PackagePath.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (PackagePath.EndsWith(TEXT("/")))
				PackagePath.LeftChopInline(1);
			return PackagePath;
		}
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::CreateTag(const TSharedPtr<FJsonObject>& Args)
	{
		FString TagName;
		FString Comment;
		if (!Args->TryGetStringField(TEXT("tag"), TagName) || TagName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 tag。"));
		}
		Args->TryGetStringField(TEXT("comment"), Comment);
		if (!FGameplayTag::IsValidGameplayTagString(TagName, nullptr, nullptr))
		{
			return ErrorJson(TEXT("GameplayTag 名称格式无效。"));
		}
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
		const FGameplayTag Existing = Manager.RequestGameplayTag(FName(*TagName), false);
		const bool bExisted = Existing.IsValid();
		const FGameplayTag Created = bExisted ? Existing : Manager.AddNativeGameplayTag(FName(*TagName), Comment);

		FString ConfigPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGameplayTags.ini")));
		FPaths::NormalizeFilename(ConfigPath);
		FConfigFile ConfigFile;
		if (IFileManager::Get().FileExists(*ConfigPath))
			ConfigFile.Read(ConfigPath);
		const FString Entry = FString::Printf(TEXT("(Tag=\"%s\",DevComment=\"%s\")"), *TagName.ReplaceCharWithEscapedChar(), *Comment.ReplaceCharWithEscapedChar());
		ConfigFile.AddUniqueToSection(TEXT("/Script/GameplayTags.GameplayTagsList"), FName(TEXT("GameplayTagList")), Entry);
		if (!ConfigFile.Write(ConfigPath, false))
			return ErrorJson(TEXT("GameplayTag 配置写入失败。"));
		GConfig->UnloadFile(ConfigPath);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tag"), Created.ToString());
		Result->SetStringField(TEXT("comment"), Comment);
		Result->SetBoolField(TEXT("existed"), bExisted);
		Result->SetStringField(TEXT("configPath"), ConfigPath);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::CreateEnum(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
		{
			return ErrorJson(TEXT("枚举 name 为空或包含非法字符。"));
		}
		FString PackagePath = TEXT("/Game");
		Args->TryGetStringField(TEXT("packagePath"), PackagePath);
		PackagePath = NormalizePackagePath(PackagePath);
		if (!PackagePath.StartsWith(TEXT("/Game")))
			return ErrorJson(TEXT("packagePath 必须位于 /Game。"));
		const FString PackageName = PackagePath + TEXT("/") + Name;
		const FString AssetPath = PackageName + TEXT(".") + Name;
		FString Conflict = TEXT("skip");
		Args->TryGetStringField(TEXT("onConflict"), Conflict);
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
		const bool bExisted = Enum != nullptr;
		if (bExisted && Conflict.Equals(TEXT("error"), ESearchCase::IgnoreCase))
		{
			return ErrorJson(TEXT("目标枚举资产已存在。"));
		}
		if (bExisted && Conflict.Equals(TEXT("skip"), ESearchCase::IgnoreCase))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), AssetPath);
			Result->SetBoolField(TEXT("existed"), true);
			Result->SetBoolField(TEXT("skipped"), true);
			return SuccessJson(Result);
		}
		if (!Enum)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Enum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional));
			if (!Enum)
				return ErrorJson(TEXT("UserDefinedEnum 创建失败。"));
			FAssetRegistryModule::AssetCreated(Enum);
		}

		TArray<FEnumEntryInput> Entries;
		FString Error;
		if (!ParseEnumEntries(Args, Entries, Error, false) || !ApplyEnumEntries(Enum, Entries, Error) || !SaveEnumPackage(Enum, Error))
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Enum->GetPathName());
		Result->SetNumberField(TEXT("entryCount"), Entries.Num());
		Result->SetBoolField(TEXT("existed"), bExisted);
		Result->SetBoolField(TEXT("skipped"), false);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::SetEnumEntries(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath))
			return ErrorJson(TEXT("缺少必填 assetPath。"));
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
		if (!Enum)
			return ErrorJson(TEXT("未找到 UserDefinedEnum 资产。"));
		TArray<FEnumEntryInput> Entries;
		FString Error;
		if (!ParseEnumEntries(Args, Entries, Error, true) || !ApplyEnumEntries(Enum, Entries, Error) || !SaveEnumPackage(Enum, Error))
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Enum->GetPathName());
		Result->SetNumberField(TEXT("entryCount"), Entries.Num());
		return SuccessJson(Result);
	}
}
