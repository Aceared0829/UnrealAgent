// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Advanced.cpp
 * @brief IK、Control Rig、重定向、PoseSearch 与动画修改器能力。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.Internal.h"

#include "Animation/MirrorDataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	using namespace AnimationPrivate;

	namespace
	{
		void SetStringArray(const TSharedRef<FJsonObject>& Object, const FString& Name, const TArray<FString>& Strings)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& String : Strings)
			{
				Values.Add(MakeShared<FJsonValueString>(String));
			}
			Object->SetArrayField(Name, Values);
		}

		UClass* ResolveAdvancedClass(const FString& Action)
		{
			if (Action == TEXT("create_ik_rig"))
				return FindClass(TEXT("/Script/IKRig.IKRigDefinition"));
			if (Action == TEXT("create_ik_retargeter"))
				return FindClass(TEXT("/Script/IKRig.IKRetargeter"));
			if (Action == TEXT("create_pose_search_database"))
				return FindClass(TEXT("/Script/PoseSearch.PoseSearchDatabase"));
			if (Action == TEXT("create_pose_search_schema"))
				return FindClass(TEXT("/Script/PoseSearch.PoseSearchSchema"));
			if (Action == TEXT("create_pose_search_normalization_set"))
				return FindClass(TEXT("/Script/PoseSearch.PoseSearchNormalizationSet"));
			return nullptr;
		}

		FString PrefixFor(const FString& Action)
		{
			if (Action.Contains(TEXT("retargeter")))
				return TEXT("IKRetargeter");
			if (Action.Contains(TEXT("ik_rig")))
				return TEXT("IKRig");
			if (Action.Contains(TEXT("schema")))
				return TEXT("PoseSearchSchema");
			if (Action.Contains(TEXT("database")))
				return TEXT("PoseSearchDatabase");
			if (Action.Contains(TEXT("mirror")))
				return TEXT("MirrorDataTable");
			return TEXT("PoseSearchNormalizationSet");
		}

		void ApplyKnownReferences(UObject* Asset, const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutChanged)
		{
			struct FMapping
			{
				const TCHAR* Argument;
				const TCHAR* Property;
			};
			const FMapping Mappings[] = { { TEXT("skeletonPath"), TEXT("Skeleton") }, { TEXT("schemaPath"), TEXT("Schema") },
				{ TEXT("skeletalMeshPath"), TEXT("PreviewSkeletalMesh") }, { TEXT("sourceRig"), TEXT("SourceIKRigAsset") }, { TEXT("targetRig"), TEXT("TargetIKRigAsset") },
				{ TEXT("sourceRigPath"), TEXT("SourceIKRigAsset") }, { TEXT("targetRigPath"), TEXT("TargetIKRigAsset") }, { TEXT("chooserPath"), TEXT("DatabaseChooser") } };
			for (const FMapping& Mapping : Mappings)
			{
				if (!Args || !Args->HasField(Mapping.Argument))
					continue;
				FString ErrorText;
				if (SetProperty(Asset, Mapping.Property, Args->TryGetField(Mapping.Argument), ErrorText))
				{
					OutChanged.Add(Mapping.Property);
				}
			}
		}
	}

	FString FUnrealAgentMCPUnrealAnimationAdapter::Advanced(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_mirror_data_table"))
		{
			const FString Name = StringArg(Args, { TEXT("name") }, TEXT("MirrorDataTable"));
			const FString Directory = StringArg(Args, { TEXT("packagePath"), TEXT("directory") }, TEXT("/Game/Animation"));
			const FString ObjectPath = Directory / Name + TEXT(".") + Name;
			UMirrorDataTable* Asset = Cast<UMirrorDataTable>(LoadAsset(ObjectPath, UMirrorDataTable::StaticClass()));
			if (!Asset)
			{
				UPackage* Package = CreatePackage(*FPackageName::ObjectPathToPackageName(ObjectPath));
				Asset = NewObject<UMirrorDataTable>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
				if (Asset)
				{
					FObjectPropertyBase* RowStructProperty = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("RowStruct"));
					if (RowStructProperty)
					{
						RowStructProperty->SetObjectPropertyValue_InContainer(Asset, FMirrorTableRow::StaticStruct());
					}
					FAssetRegistryModule::AssetCreated(Asset);
				}
			}
			if (!Asset)
				return Error(TEXT("创建 MirrorDataTable 失败。"));
			TArray<FString> Changed;
			ApplyKnownReferences(Asset, Args, Changed);
			Save(Asset);
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			Result->SetBoolField(TEXT("created"), true);
			SetStringArray(Result, TEXT("changedProperties"), Changed);
			return Success(Result);
		}

		if (UClass* Class = ResolveAdvancedClass(Action))
		{
			FString Path;
			UObject* Asset = CreateAsset(Class, Args, PrefixFor(Action), Path);
			if (!Asset)
			{
				return Error(FString::Printf(TEXT("创建 %s 失败，请确认对应 UE 插件已启用。"), *PrefixFor(Action)));
			}
			TArray<FString> Changed;
			ApplyKnownReferences(Asset, Args, Changed);
			Save(Asset);
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			Result->SetBoolField(TEXT("created"), true);
			SetStringArray(Result, TEXT("changedProperties"), Changed);
			return Success(Result);
		}

		const FString Path = StringArg(Args, { TEXT("assetPath"), TEXT("path") });
		if (Path.IsEmpty())
			return Error(TEXT("缺少必填 assetPath。"));
		UObject* Asset = LoadAsset(Path);
		if (!Asset)
			return Error(FString::Printf(TEXT("找不到进阶动画资产：%s"), *Path));

		if (Action.StartsWith(TEXT("read_")) || Action.StartsWith(TEXT("list_")))
		{
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			if (Action == TEXT("list_modifiers"))
			{
				Result->SetArrayField(TEXT("modifiers"), {});
				Result->SetNumberField(TEXT("count"), 0);
			}
			return Success(Result);
		}

		TArray<FString> Changed;
		ApplyKnownReferences(Asset, Args, Changed);
		if (Args)
		{
			const TSharedPtr<FJsonObject>* Settings = nullptr;
			if (Args->TryGetObjectField(TEXT("settings"), Settings) && Settings && *Settings)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Settings)->Values)
				{
					FString ErrorText;
					if (SetProperty(Asset, Pair.Key, Pair.Value, ErrorText))
						Changed.Add(Pair.Key);
				}
			}
		}
		Save(Asset);
		TSharedRef<FJsonObject> Result = DescribeObject(Asset);
		Result->SetStringField(TEXT("operation"), Action);
		SetStringArray(Result, TEXT("changedProperties"), Changed);
		Result->SetStringField(TEXT("status"), Changed.IsEmpty() ? TEXT("目标已验证；该操作需要相应的链、通道或动画条目参数。") : TEXT("属性已更新并保存。"));
		return Success(Result);
	}
}
