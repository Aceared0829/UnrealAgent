// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealFoliageAdapter.Authoring.cpp
 * @brief 植被类型资产创建、反射式设置写入和持久化实现。
 */

#include "Adapters/Unreal/Foliage/UnrealAgentMCPUnrealFoliageAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizeFoliagePackagePath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.EndsWith(TEXT("/")))
				Path.LeftChopInline(1);
			return Path.IsEmpty() ? TEXT("/Game/Foliage") : Path;
		}

		bool SaveTypeAsset(UFoliageType* Type, FString& OutError)
		{
			if (!Type || !Type->IsAsset())
				return true;
			UPackage* Package = Type->GetOutermost();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, Type, *Filename, SaveArgs))
			{
				OutError = TEXT("植被类型资产保存失败。");
				return false;
			}
			return true;
		}

		bool IsProtectedSetting(const FString& Name)
		{
			return Name.Equals(TEXT("mesh"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("overrideMaterials"), ESearchCase::IgnoreCase) ||
				Name.Equals(TEXT("naniteOverrideMaterials"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("componentClass"), ESearchCase::IgnoreCase);
		}

		TSharedRef<FJsonObject> MakeWritableSettings(const TSharedPtr<FJsonObject>& Input, TArray<FString>& OutFields)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Input->Values)
			{
				if (!IsProtectedSetting(Pair.Key))
				{
					Result->SetField(Pair.Key, Pair.Value);
					OutFields.Add(Pair.Key);
				}
			}
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealFoliageAdapter::CreateType(const TSharedPtr<FJsonObject>& Args)
	{
		FString MeshPath;
		if (!Args->TryGetStringField(TEXT("meshPath"), MeshPath) || MeshPath.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 meshPath。"));
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *JsonConversion::NormalizeAssetObjectPath(MeshPath));
		if (!Mesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh：%s"), *MeshPath));
		}

		FString Name = TEXT("FT_") + Mesh->GetName();
		Args->TryGetStringField(TEXT("name"), Name);
		if (!FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
		{
			return ErrorJson(TEXT("植被类型名称包含非法字符。"));
		}
		FString PackagePath = TEXT("/Game/Foliage");
		Args->TryGetStringField(TEXT("packagePath"), PackagePath);
		PackagePath = NormalizeFoliagePackagePath(PackagePath);
		if (!PackagePath.StartsWith(TEXT("/Game")))
		{
			return ErrorJson(TEXT("packagePath 必须位于 /Game。"));
		}

		const FString PackageName = PackagePath + TEXT("/") + Name;
		const FString AssetPath = PackageName + TEXT(".") + Name;
		UFoliageType_InstancedStaticMesh* Type = LoadObject<UFoliageType_InstancedStaticMesh>(nullptr, *AssetPath);
		const bool bExisted = Type != nullptr;
		if (!Type)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Type = NewObject<UFoliageType_InstancedStaticMesh>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			if (!Type)
				return ErrorJson(TEXT("植被类型创建失败。"));
			Type->SetStaticMesh(Mesh);
			Type->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(Type);
		}
		else if (Type->GetStaticMesh() != Mesh)
		{
			Type->Modify();
			Type->SetStaticMesh(Mesh);
			Type->PostEditChange();
			Type->MarkPackageDirty();
		}

		FString Error;
		if (!SaveTypeAsset(Type, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeTypeJson(Type, 0);
		Result->SetBoolField(TEXT("existed"), bExisted);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealFoliageAdapter::SetSettings(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("foliageTypeName"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 foliageTypeName。"));
		}
		const TSharedPtr<FJsonObject>* SettingsInput = nullptr;
		if (!Args->TryGetObjectField(TEXT("settings"), SettingsInput) || SettingsInput == nullptr || !SettingsInput->IsValid())
		{
			return ErrorJson(TEXT("缺少有效 settings 对象。"));
		}
		UFoliageType* Type = ResolveFoliageType(Name);
		if (!Type)
		{
			return ErrorJson(FString::Printf(TEXT("未找到植被类型：%s"), *Name));
		}

		TArray<FString> ChangedFields;
		const TSharedRef<FJsonObject> WritableSettings = MakeWritableSettings(*SettingsInput, ChangedFields);
		if (ChangedFields.IsEmpty())
		{
			return ErrorJson(TEXT("settings 未包含可写字段。"));
		}

		Type->Modify();
		Type->PreEditChange(nullptr);
		FText FailureReason;
		if (!FJsonObjectConverter::JsonObjectToUStruct(WritableSettings, Type->GetClass(), Type, CPF_Edit, CPF_Transient | CPF_Deprecated, false, &FailureReason))
		{
			return ErrorJson(FString::Printf(TEXT("植被设置写入失败：%s"), *FailureReason.ToString()));
		}
		Type->UpdateGuid = FGuid::NewGuid();
		Type->PostEditChange();
		Type->MarkPackageDirty();

		if (UWorld* World = ActorSupport::GetEditorWorld())
		{
			for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
			{
				It->DetectFoliageTypeChangeAndUpdate();
			}
		}

		FString Error;
		if (!SaveTypeAsset(Type, Error))
			return ErrorJson(Error);

		TArray<TSharedPtr<FJsonValue>> Fields;
		for (const FString& Field : ChangedFields)
			Fields.Add(MakeShared<FJsonValueString>(Field));
		TSharedRef<FJsonObject> Result = MakeTypeJson(Type, -1);
		Result->SetArrayField(TEXT("changedFields"), Fields);
		Result->SetObjectField(TEXT("settings"), MakeSettingsJson(Type));
		Result->SetBoolField(TEXT("saved"), Type->IsAsset());
		return SuccessJson(Result);
	}
}
