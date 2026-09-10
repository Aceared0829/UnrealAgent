// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Streaming.cpp
 * @brief World Partition 描述符、当前编辑关卡与流送子关卡的独立实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "UObject/Package.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace UnrealAgentMCP
{
	namespace
	{
		struct FActorDescFilter
		{
			FString Text;
			FString ClassName;
			TSet<FString> Guids;
			bool bLoadedOnly = false;
			bool bUnloadedOnly = false;
			bool bHasBounds = false;
			FBox Bounds = FBox(EForceInit::ForceInit);
			int32 Limit = 500;
		};

		FString NormalizeLevelPackageName(FString Value)
		{
			Value.TrimStartAndEndInline();
			Value.RemoveFromEnd(TEXT(".umap"));
			if (Value.StartsWith(TEXT("/")) && Value.Contains(TEXT(".")))
			{
				Value = FPackageName::ObjectPathToPackageName(Value);
			}
			return Value;
		}

		FString GetLevelPackageName(const ULevel* Level)
		{
			return Level && Level->GetOutermost() ? Level->GetOutermost()->GetName() : FString();
		}

		FString GetLevelShortName(const FString& PackageName)
		{
			return FPackageName::GetShortName(PackageName);
		}

		ULevelStreaming* FindStreamingLevel(UWorld* World, const FString& NameOrPath)
		{
			if (!World)
			{
				return nullptr;
			}
			const FString Normalized = NormalizeLevelPackageName(NameOrPath);
			for (ULevelStreaming* Streaming : World->GetStreamingLevels())
			{
				if (!Streaming)
				{
					continue;
				}
				const FString PackageName = Streaming->GetWorldAssetPackageName();
				if (PackageName.Equals(Normalized, ESearchCase::IgnoreCase) || GetLevelShortName(PackageName).Equals(NameOrPath, ESearchCase::IgnoreCase) ||
					Streaming->GetName().Equals(NameOrPath, ESearchCase::IgnoreCase))
				{
					return Streaming;
				}
			}
			return nullptr;
		}

		bool ReadActorDescFilter(const TSharedPtr<FJsonObject>& Args, const int32 DefaultLimit, FActorDescFilter& OutFilter, FString& OutError)
		{
			OutFilter.Limit = DefaultLimit;
			if (!Args.IsValid())
			{
				return true;
			}
			Args->TryGetStringField(TEXT("filter"), OutFilter.Text);
			Args->TryGetStringField(TEXT("className"), OutFilter.ClassName);
			Args->TryGetBoolField(TEXT("loadedOnly"), OutFilter.bLoadedOnly);
			Args->TryGetBoolField(TEXT("unloadedOnly"), OutFilter.bUnloadedOnly);
			if (OutFilter.bLoadedOnly && OutFilter.bUnloadedOnly)
			{
				OutError = TEXT("loadedOnly 与 unloadedOnly 不能同时为 true。");
				return false;
			}

			double Limit = DefaultLimit;
			if (Args->TryGetNumberField(TEXT("limit"), Limit) || Args->TryGetNumberField(TEXT("maxActors"), Limit))
			{
				OutFilter.Limit = FMath::Clamp(static_cast<int32>(Limit), 1, 5000);
			}

			const TArray<TSharedPtr<FJsonValue>>* GuidValues = nullptr;
			if (Args->TryGetArrayField(TEXT("guids"), GuidValues) && GuidValues)
			{
				for (const TSharedPtr<FJsonValue>& Value : *GuidValues)
				{
					FString Guid;
					if (Value.IsValid() && Value->TryGetString(Guid))
					{
						Guid.ToLowerInline();
						OutFilter.Guids.Add(Guid);
					}
				}
			}

			const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
			if (Args->TryGetObjectField(TEXT("bounds"), BoundsObject) && BoundsObject && BoundsObject->IsValid())
			{
				FVector Minimum;
				FVector Maximum;
				if (!JsonConversion::TryGetVectorField(*BoundsObject, TEXT("min"), Minimum) || !JsonConversion::TryGetVectorField(*BoundsObject, TEXT("max"), Maximum))
				{
					OutError = TEXT("bounds 必须同时包含 min 与 max 向量。");
					return false;
				}
				OutFilter.Bounds = FBox(Minimum.ComponentMin(Maximum), Minimum.ComponentMax(Maximum));
				OutFilter.bHasBounds = true;
			}
			return true;
		}

		bool MatchesActorDesc(const FWorldPartitionActorDescInstance* Desc, const FActorDescFilter& Filter)
		{
			if (!Desc)
			{
				return false;
			}
			const bool bLoaded = Desc->IsLoaded();
			if ((Filter.bLoadedOnly && !bLoaded) || (Filter.bUnloadedOnly && bLoaded))
			{
				return false;
			}

			const FString Guid = Desc->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
			if (!Filter.Guids.IsEmpty() && !Filter.Guids.Contains(Guid))
			{
				return false;
			}

			if (!Filter.ClassName.IsEmpty() && !Desc->GetDisplayClassNameString().Contains(Filter.ClassName, ESearchCase::IgnoreCase) &&
				!Desc->GetNativeClass().ToString().Contains(Filter.ClassName, ESearchCase::IgnoreCase))
			{
				return false;
			}

			if (!Filter.Text.IsEmpty() && !Desc->GetActorLabelString().Contains(Filter.Text, ESearchCase::IgnoreCase) &&
				!Desc->GetActorNameString().Contains(Filter.Text, ESearchCase::IgnoreCase) && !Desc->GetActorSoftPath().ToString().Contains(Filter.Text, ESearchCase::IgnoreCase))
			{
				return false;
			}

			if (Filter.bHasBounds && !Desc->GetEditorBounds().Intersect(Filter.Bounds))
			{
				return false;
			}
			return true;
		}

		TSharedRef<FJsonObject> MakeActorDescObject(const FWorldPartitionActorDescInstance* Desc)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("guid"), Desc->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("actorLabel"), Desc->GetActorLabelString());
			Object->SetStringField(TEXT("actorName"), Desc->GetActorNameString());
			Object->SetStringField(TEXT("actorPath"), Desc->GetActorSoftPath().ToString());
			Object->SetStringField(TEXT("className"), Desc->GetDisplayClassNameString());
			Object->SetStringField(TEXT("nativeClass"), Desc->GetNativeClass().ToString());
			Object->SetStringField(TEXT("runtimeGrid"), Desc->GetRuntimeGrid().ToString());
			Object->SetBoolField(TEXT("loaded"), Desc->IsLoaded());
			Object->SetBoolField(TEXT("spatiallyLoaded"), Desc->GetIsSpatiallyLoaded());

			const FBox Bounds = Desc->GetEditorBounds();
			TSharedRef<FJsonObject> BoundsObject = MakeShared<FJsonObject>();
			BoundsObject->SetBoolField(TEXT("valid"), Bounds.IsValid != 0);
			BoundsObject->SetObjectField(TEXT("min"), JsonConversion::MakeVectorObject(Bounds.Min));
			BoundsObject->SetObjectField(TEXT("max"), JsonConversion::MakeVectorObject(Bounds.Max));
			Object->SetObjectField(TEXT("bounds"), BoundsObject);

			TArray<TSharedPtr<FJsonValue>> DataLayers;
			for (const FName Name : Desc->GetDataLayerInstanceNames().ToArray())
			{
				DataLayers.Add(MakeShared<FJsonValueString>(Name.ToString()));
			}
			Object->SetArrayField(TEXT("dataLayers"), DataLayers);
			return Object;
		}

		TSharedRef<FJsonObject> MakeStreamingLevelObject(ULevelStreaming* Streaming)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			const FString PackageName = Streaming->GetWorldAssetPackageName();
			Object->SetStringField(TEXT("levelName"), GetLevelShortName(PackageName));
			Object->SetStringField(TEXT("packageName"), PackageName);
			Object->SetStringField(TEXT("streamingClass"), Streaming->GetClass()->GetName());
			Object->SetBoolField(TEXT("initiallyLoaded"), Streaming->ShouldBeLoaded());
			Object->SetBoolField(TEXT("initiallyVisible"), Streaming->GetShouldBeVisibleFlag());
			Object->SetBoolField(TEXT("editorVisible"), Streaming->GetShouldBeVisibleInEditor());
			Object->SetBoolField(TEXT("loaded"), Streaming->IsLevelLoaded());
			Object->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Streaming->LevelTransform.GetLocation()));
			return Object;
		}
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ListActorDescs(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		UWorldPartition* Partition = World ? World->GetWorldPartition() : nullptr;
		if (!Partition)
		{
			return ErrorJson(TEXT("当前编辑器世界未启用 World Partition。"));
		}

		FActorDescFilter Filter;
		FString Error;
		if (!ReadActorDescFilter(Args, 500, Filter, Error))
		{
			return ErrorJson(Error);
		}

		TArray<TSharedPtr<FJsonValue>> Actors;
		FWorldPartitionHelpers::ForEachActorDescInstance(Partition,
			[&Actors, &Filter](const FWorldPartitionActorDescInstance* Desc)
			{
				if (MatchesActorDesc(Desc, Filter))
				{
					Actors.Add(MakeShared<FJsonValueObject>(MakeActorDescObject(Desc)));
				}
				return Actors.Num() < Filter.Limit;
			});

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Actors.Num());
		Result->SetNumberField(TEXT("limit"), Filter.Limit);
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::LoadActorDescs(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		UWorldPartition* Partition = World ? World->GetWorldPartition() : nullptr;
		if (!Partition)
		{
			return ErrorJson(TEXT("当前编辑器世界未启用 World Partition。"));
		}

		FString Mode = TEXT("pin");
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("mode"), Mode);
		}
		Mode.ToLowerInline();
		if (Mode != TEXT("pin") && Mode != TEXT("unpin"))
		{
			return ErrorJson(TEXT("mode 必须为 pin 或 unpin。"));
		}

		FActorDescFilter Filter;
		FString Error;
		if (!ReadActorDescFilter(Args, 256, Filter, Error))
		{
			return ErrorJson(Error);
		}
		bool bDryRun = false;
		if (Args.IsValid())
		{
			Args->TryGetBoolField(TEXT("dryRun"), bDryRun);
		}

		if (PinnedWorldPartition.Get() != Partition)
		{
			PinnedActorDescs.Reset();
			PinnedWorldPartition = Partition;
		}

		int32 Matched = 0;
		int32 Changed = 0;
		int32 LoadedAfter = 0;
		TArray<TSharedPtr<FJsonValue>> Actors;
		FWorldPartitionHelpers::ForEachActorDescInstance(Partition,
			[this, &Actors, &Filter, &Mode, bDryRun, &Matched, &Changed, &LoadedAfter](const FWorldPartitionActorDescInstance* Desc)
			{
				if (!MatchesActorDesc(Desc, Filter))
				{
					return true;
				}
				++Matched;
				const FGuid Guid = Desc->GetGuid();
				const bool bWasPinned = PinnedActorDescs.Contains(Guid);
				const bool bLoadedBefore = Desc->IsLoaded();

				if (!bDryRun)
				{
					if (Mode == TEXT("pin") && !bWasPinned)
					{
						PinnedActorDescs.Add(Guid, MakeUnique<FWorldPartitionReference>(const_cast<FWorldPartitionActorDescInstance*>(Desc)));
						++Changed;
					}
					else if (Mode == TEXT("unpin") && bWasPinned)
					{
						PinnedActorDescs.Remove(Guid);
						++Changed;
					}
				}

				const bool bLoadedNow = Desc->IsLoaded();
				if (bLoadedNow)
				{
					++LoadedAfter;
				}
				TSharedRef<FJsonObject> Entry = MakeActorDescObject(Desc);
				Entry->SetBoolField(TEXT("loadedBefore"), bLoadedBefore);
				Entry->SetBoolField(TEXT("loadedAfter"), bLoadedNow);
				Entry->SetBoolField(TEXT("pinned"), Mode == TEXT("pin") ? (bDryRun ? true : PinnedActorDescs.Contains(Guid)) : (bDryRun ? false : PinnedActorDescs.Contains(Guid)));
				Actors.Add(MakeShared<FJsonValueObject>(Entry));
				return Matched < Filter.Limit;
			});

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("mode"), Mode);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetNumberField(TEXT("matched"), Matched);
		Result->SetNumberField(TEXT("changed"), Changed);
		Result->SetNumberField(TEXT("loadedAfter"), LoadedAfter);
		Result->SetNumberField(TEXT("pinnedCount"), PinnedActorDescs.Num());
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level)
		{
			return ErrorJson(TEXT("当前编辑关卡不可用。"));
		}
		const FString PackageName = GetLevelPackageName(Level);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelName"), GetLevelShortName(PackageName));
		Result->SetStringField(TEXT("levelPath"), PackageName);
		Result->SetBoolField(TEXT("isPersistent"), Level == World->PersistentLevel);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString Name;
		if (!World || !Args.IsValid() || (!Args->TryGetStringField(TEXT("levelName"), Name) && !Args->TryGetStringField(TEXT("levelPath"), Name)) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 levelName/levelPath。"));
		}

		ULevel* Target = nullptr;
		const FString PersistentPackage = GetLevelPackageName(World->PersistentLevel);
		if (PersistentPackage.Equals(NormalizeLevelPackageName(Name), ESearchCase::IgnoreCase) || GetLevelShortName(PersistentPackage).Equals(Name, ESearchCase::IgnoreCase))
		{
			Target = World->PersistentLevel;
		}
		else if (ULevelStreaming* Streaming = FindStreamingLevel(World, Name))
		{
			Target = Streaming->GetLoadedLevel();
			if (!Target)
			{
				return ErrorJson(FString::Printf(TEXT("子关卡尚未加载，无法设为编辑目标：%s"), *Name));
			}
		}
		if (!Target)
		{
			return ErrorJson(FString::Printf(TEXT("未找到已加载关卡：%s"), *Name));
		}

		UEditorLevelUtils::MakeLevelCurrent(Target, true);
		if (World->GetCurrentLevel() != Target)
		{
			return ErrorJson(TEXT("设置当前编辑关卡失败。"));
		}
		const FString PackageName = GetLevelPackageName(Target);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelName"), GetLevelShortName(PackageName));
		Result->SetStringField(TEXT("levelPath"), PackageName);
		Result->SetBoolField(TEXT("isPersistent"), Target == World->PersistentLevel);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ListStreamingSublevels(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		TArray<TSharedPtr<FJsonValue>> Sublevels;
		for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming)
			{
				Sublevels.Add(MakeShared<FJsonValueObject>(MakeStreamingLevelObject(Streaming)));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Sublevels.Num());
		Result->SetArrayField(TEXT("sublevels"), Sublevels);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AddStreamingSublevel(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString LevelPath;
		if (!World || !Args.IsValid() || !Args->TryGetStringField(TEXT("levelPath"), LevelPath) || LevelPath.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 levelPath。"));
		}
		LevelPath = NormalizeLevelPackageName(LevelPath);
		if (FindStreamingLevel(World, LevelPath))
		{
			return ErrorJson(FString::Printf(TEXT("流送子关卡已存在：%s"), *LevelPath));
		}

		FString StreamingClassName = TEXT("LevelStreamingDynamic");
		Args->TryGetStringField(TEXT("streamingClass"), StreamingClassName);
		TSubclassOf<ULevelStreaming> StreamingClass = ULevelStreamingDynamic::StaticClass();
		if (StreamingClassName.Equals(TEXT("LevelStreamingAlwaysLoaded"), ESearchCase::IgnoreCase))
		{
			StreamingClass = ULevelStreamingAlwaysLoaded::StaticClass();
		}
		else if (!StreamingClassName.Equals(TEXT("LevelStreamingDynamic"), ESearchCase::IgnoreCase))
		{
			return ErrorJson(TEXT("streamingClass 仅支持 LevelStreamingDynamic 或 LevelStreamingAlwaysLoaded。"));
		}

		FVector Location = FVector::ZeroVector;
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		FTransform Transform;
		Transform.SetLocation(Location);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AddStreamingSublevel", "MCP 添加流送子关卡"));
		World->Modify();
		ULevelStreaming* Streaming = UEditorLevelUtils::AddLevelToWorld(World, *LevelPath, StreamingClass, Transform);
		if (!Streaming)
		{
			return ErrorJson(FString::Printf(TEXT("添加流送子关卡失败：%s"), *LevelPath));
		}
		bool bInitiallyLoaded = true;
		bool bInitiallyVisible = true;
		Args->TryGetBoolField(TEXT("initiallyLoaded"), bInitiallyLoaded);
		Args->TryGetBoolField(TEXT("initiallyVisible"), bInitiallyVisible);
		Streaming->SetShouldBeLoaded(bInitiallyLoaded);
		Streaming->SetShouldBeVisible(bInitiallyVisible);
		Streaming->SetShouldBeVisibleInEditor(bInitiallyVisible);
		Streaming->MarkPackageDirty();
		World->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeStreamingLevelObject(Streaming);
		Result->SetBoolField(TEXT("created"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString Name;
		if (!World || !Args.IsValid() || (!Args->TryGetStringField(TEXT("levelName"), Name) && !Args->TryGetStringField(TEXT("levelPath"), Name)) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 levelName/levelPath。"));
		}
		ULevelStreaming* Streaming = FindStreamingLevel(World, Name);
		if (!Streaming)
		{
			return ErrorJson(FString::Printf(TEXT("未找到流送子关卡：%s"), *Name));
		}
		const FString PackageName = Streaming->GetWorldAssetPackageName();

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "RemoveStreamingSublevel", "MCP 移除流送子关卡"));
		bool bRemoved = false;
		if (ULevel* LoadedLevel = Streaming->GetLoadedLevel())
		{
			bRemoved = UEditorLevelUtils::RemoveLevelFromWorld(LoadedLevel, true, false);
		}
		else
		{
			World->Modify();
			Streaming->Modify();
			bRemoved = World->RemoveStreamingLevel(Streaming);
			if (bRemoved)
			{
				Streaming->MarkAsGarbage();
				World->RefreshStreamingLevels({});
				World->BroadcastLevelsChanged();
			}
		}
		if (!bRemoved)
		{
			return ErrorJson(FString::Printf(TEXT("移除流送子关卡失败：%s"), *PackageName));
		}
		World->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelName"), GetLevelShortName(PackageName));
		Result->SetStringField(TEXT("levelPath"), PackageName);
		Result->SetBoolField(TEXT("removed"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString Name;
		if (!World || !Args.IsValid() || (!Args->TryGetStringField(TEXT("levelName"), Name) && !Args->TryGetStringField(TEXT("levelPath"), Name)) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 levelName/levelPath。"));
		}
		ULevelStreaming* Streaming = FindStreamingLevel(World, Name);
		if (!Streaming)
		{
			return ErrorJson(FString::Printf(TEXT("未找到流送子关卡：%s"), *Name));
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetStreamingSublevelProperties", "MCP 修改流送子关卡"));
		Streaming->Modify();
		bool bChanged = false;
		bool Value = false;
		if (Args->TryGetBoolField(TEXT("initiallyLoaded"), Value))
		{
			Streaming->SetShouldBeLoaded(Value);
			bChanged = true;
		}
		if (Args->TryGetBoolField(TEXT("initiallyVisible"), Value))
		{
			Streaming->SetShouldBeVisible(Value);
			bChanged = true;
		}
		if (Args->TryGetBoolField(TEXT("editorVisible"), Value))
		{
			Streaming->SetShouldBeVisibleInEditor(Value);
			if (ULevel* LoadedLevel = Streaming->GetLoadedLevel())
			{
				UEditorLevelUtils::SetLevelVisibility(LoadedLevel, Value, false);
			}
			bChanged = true;
		}
		FVector Location;
		if (JsonConversion::TryGetVectorField(Args, TEXT("location"), Location))
		{
			FTransform Transform = Streaming->LevelTransform;
			Transform.SetLocation(Location);
			Streaming->LevelTransform = Transform;
			bChanged = true;
		}
		if (bChanged)
		{
			Streaming->PostEditChange();
			Streaming->MarkPackageDirty();
			World->MarkPackageDirty();
			World->UpdateLevelStreaming();
		}

		TSharedRef<FJsonObject> Result = MakeStreamingLevelObject(Streaming);
		Result->SetBoolField(TEXT("updated"), bChanged);
		return SuccessJson(Result);
	}
}
