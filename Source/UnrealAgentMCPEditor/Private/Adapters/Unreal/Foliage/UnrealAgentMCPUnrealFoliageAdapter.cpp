// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealFoliageAdapter.cpp
 * @brief 植被类型发现、设置读取和空间实例采样实现。
 */

#include "Adapters/Unreal/Foliage/UnrealAgentMCPUnrealFoliageAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "JsonObjectConverter.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool MatchesTypeName(const UFoliageType* Type, const FString& Search)
		{
			if (!Type)
				return false;
			if (Search.IsEmpty())
				return true;
			if (Type->GetName().Equals(Search, ESearchCase::IgnoreCase) || Type->GetPathName().Equals(Search, ESearchCase::IgnoreCase) ||
				Type->GetDisplayFName().ToString().Equals(Search, ESearchCase::IgnoreCase))
			{
				return true;
			}
			const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type);
			return MeshType && MeshType->GetStaticMesh() &&
				(MeshType->GetStaticMesh()->GetName().Equals(Search, ESearchCase::IgnoreCase) || MeshType->GetStaticMesh()->GetPathName().Equals(Search, ESearchCase::IgnoreCase));
		}

		TSharedRef<FJsonObject> MakeTransformJson(const FTransform& Transform)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Transform.GetLocation()));
			Json->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Transform.Rotator()));
			Json->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Transform.GetScale3D()));
			return Json;
		}
	}

	UFoliageType* FUnrealAgentMCPUnrealFoliageAdapter::ResolveFoliageType(const FString& Name, AInstancedFoliageActor** OutActor)
	{
		if (OutActor)
			*OutActor = nullptr;
		if (Name.IsEmpty())
			return nullptr;

		if (Name.StartsWith(TEXT("/")))
		{
			const FString ObjectPath = JsonConversion::NormalizeAssetObjectPath(Name);
			if (UFoliageType* Loaded = LoadObject<UFoliageType>(nullptr, *ObjectPath))
			{
				return Loaded;
			}
		}

		UWorld* World = ActorSupport::GetEditorWorld();
		if (World)
		{
			for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
			{
				for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : It->GetFoliageInfos())
				{
					if (MatchesTypeName(Pair.Key, Name))
					{
						if (OutActor)
							*OutActor = *It;
						return Pair.Key;
					}
				}
			}
		}

		for (TObjectIterator<UFoliageType> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject) && MatchesTypeName(*It, Name))
			{
				return *It;
			}
		}
		return nullptr;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealFoliageAdapter::MakeTypeJson(UFoliageType* Type, const int32 InstanceCount)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Type->GetName());
		Json->SetStringField(TEXT("displayName"), Type->GetDisplayFName().ToString());
		Json->SetStringField(TEXT("path"), Type->GetPathName());
		Json->SetStringField(TEXT("class"), Type->GetClass()->GetName());
		Json->SetNumberField(TEXT("instanceCount"), InstanceCount);
		if (const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type))
		{
			Json->SetStringField(TEXT("meshPath"), MeshType->GetStaticMesh() ? MeshType->GetStaticMesh()->GetPathName() : FString());
		}
		return Json;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealFoliageAdapter::MakeSettingsJson(UFoliageType* Type)
	{
		TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(Type->GetClass(), Type, Settings, CPF_Edit, CPF_Transient | CPF_Deprecated);
		return Settings;
	}

	FString FUnrealAgentMCPUnrealFoliageAdapter::ListTypes(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));

		TMap<UFoliageType*, int32> Counts;
		int32 FoliageActorCount = 0;
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			++FoliageActorCount;
			for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : It->GetFoliageInfos())
			{
				if (Pair.Key)
					Counts.FindOrAdd(Pair.Key) += Pair.Value->GetPlacedInstanceCount();
			}
		}

		TArray<TSharedPtr<FJsonValue>> Types;
		for (const TPair<UFoliageType*, int32>& Pair : Counts)
		{
			Types.Add(MakeShared<FJsonValueObject>(MakeTypeJson(Pair.Key, Pair.Value)));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Types.Num());
		Result->SetNumberField(TEXT("foliageActorCount"), FoliageActorCount);
		Result->SetArrayField(TEXT("types"), Types);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealFoliageAdapter::GetSettings(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("foliageTypeName"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 foliageTypeName。"));
		}
		UFoliageType* Type = ResolveFoliageType(Name);
		if (!Type)
		{
			return ErrorJson(FString::Printf(TEXT("未找到植被类型：%s"), *Name));
		}
		TSharedRef<FJsonObject> Result = MakeTypeJson(Type, -1);
		Result->SetObjectField(TEXT("settings"), MakeSettingsJson(Type));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealFoliageAdapter::Sample(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));

		FVector Center = FVector::ZeroVector;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("center"), Center))
		{
			return ErrorJson(TEXT("缺少有效 center。"));
		}
		double RadiusNumber = 0.0;
		if (!Args->TryGetNumberField(TEXT("radius"), RadiusNumber) || RadiusNumber < 0.0)
		{
			return ErrorJson(TEXT("radius 必须是非负数。"));
		}
		double CountNumber = 500.0;
		Args->TryGetNumberField(TEXT("count"), CountNumber);
		const int32 MaxCount = FMath::Clamp(static_cast<int32>(CountNumber), 1, 5000);
		FString TypeFilter;
		Args->TryGetStringField(TEXT("foliageType"), TypeFilter);

		TArray<TSharedPtr<FJsonValue>> Instances;
		int32 MatchedCount = 0;
		const FSphere Sphere(Center, RadiusNumber);
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : It->GetFoliageInfos())
			{
				if (!MatchesTypeName(Pair.Key, TypeFilter))
					continue;
				TArray<int32> Indices;
				Pair.Value->GetInstancesInsideSphere(Sphere, Indices);
				for (const int32 Index : Indices)
				{
					++MatchedCount;
					if (Instances.Num() >= MaxCount || !Pair.Value->Instances.IsValidIndex(Index))
					{
						continue;
					}
					TSharedRef<FJsonObject> Instance = MakeTransformJson(Pair.Value->Instances[Index].GetInstanceWorldTransform());
					Instance->SetNumberField(TEXT("index"), Index);
					Instance->SetStringField(TEXT("foliageType"), Pair.Key->GetName());
					Instance->SetStringField(TEXT("foliageTypePath"), Pair.Key->GetPathName());
					Instances.Add(MakeShared<FJsonValueObject>(Instance));
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("center"), JsonConversion::MakeVectorObject(Center));
		Result->SetNumberField(TEXT("radius"), RadiusNumber);
		Result->SetNumberField(TEXT("count"), Instances.Num());
		Result->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Result->SetBoolField(TEXT("truncated"), MatchedCount > Instances.Num());
		Result->SetArrayField(TEXT("instances"), Instances);
		return SuccessJson(Result);
	}
}
