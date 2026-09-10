// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.cpp
 * @brief Level 端口的 Unreal Editor 场景查询与事务化修改实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

namespace UnrealAgentMCP
{
	namespace
	{
		AActor* FindRequiredActor(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Name;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				OutError = TEXT("缺少必填 Actor 名称。");
				return nullptr;
			}
			AActor* Actor = ActorSupport::FindActorByNameOrLabel(ActorSupport::GetEditorWorld(), Name);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("未找到 Actor：%s"), *Name);
			}
			return Actor;
		}

		TArray<TSharedPtr<FJsonValue>> MakeTagValues(const AActor* Actor)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			if (!Actor)
			{
				return Values;
			}
			for (const FName& Tag : Actor->Tags)
			{
				Values.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			return Values;
		}

		FString MakeActorMutationResult(AActor* Actor, const TCHAR* Operation)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("operation"), Operation);
			Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
			return SuccessJson(Result);
		}

		bool MatchesClassFilter(AActor* Actor, const FString& ClassText)
		{
			if (!Actor || ClassText.IsEmpty())
			{
				return Actor != nullptr;
			}
			if (UClass* RequestedClass = ActorSupport::ResolveActorClass(ClassText))
			{
				return Actor->IsA(RequestedClass);
			}
			return Actor->GetClass()->GetName().Contains(ClassText, ESearchCase::IgnoreCase);
		}

		TArray<FString> ReadTags(const TSharedPtr<FJsonObject>& Args)
		{
			TArray<FString> Tags;
			if (Args.IsValid())
			{
				Args->TryGetStringArrayField(TEXT("tags"), Tags);
			}
			return Tags;
		}
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ListActors(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::ListLevelActors(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetSelectedActors(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::GetSelectedActors(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetActorDetails(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::GetActorDetails(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::PlaceActor(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::SpawnActor(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::DeleteActor(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::DeleteActor(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::MoveActor(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::TransformActor(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SelectActor(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::SelectActor(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AttachActor(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::AttachActor(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetActorProperty(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::SetActorProperty(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SaveLevel(const TSharedPtr<FJsonObject>& Args)
	{
		return Tools::SaveCurrentLevel(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetEditorVisibility(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		bool bVisible = true;
		Args->TryGetBoolField(TEXT("visible"), bVisible);
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetEditorVisibility", "MCP 设置编辑器可见性"));
		Actor->Modify();
		Actor->SetIsTemporarilyHiddenInEditor(!bVisible);
		Actor->MarkPackageDirty();
		return MakeActorMutationResult(Actor, TEXT("set_editor_visibility"));
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetActorsByClass(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		FString ClassText;
		Args->TryGetStringField(TEXT("class"), ClassText);
		double MaxResultsValue = 500.0;
		Args->TryGetNumberField(TEXT("maxResults"), MaxResultsValue);
		const int32 MaxResults = FMath::Clamp(static_cast<int32>(MaxResultsValue), 1, 2000);

		int32 MatchedCount = 0;
		TArray<TSharedPtr<FJsonValue>> Actors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!MatchesClassFilter(Actor, ClassText))
			{
				continue;
			}
			++MatchedCount;
			if (Actors.Num() < MaxResults)
			{
				Actors.Add(MakeShared<FJsonValueObject>(ActorSupport::MakeActorObject(Actor)));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Actors.Num());
		Result->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Result->SetBoolField(TEXT("truncated"), MatchedCount > Actors.Num());
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::CountActorsByClass(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		FString ClassText;
		Args->TryGetStringField(TEXT("class"), ClassText);
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (MatchesClassFilter(*It, ClassText))
			{
				++Count;
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("class"), ClassText);
		Result->SetNumberField(TEXT("count"), Count);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetActorBounds(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		bool bOnlyCollidingComponents = false;
		bool bIncludeChildActors = false;
		Args->TryGetBoolField(TEXT("onlyCollidingComponents"), bOnlyCollidingComponents);
		Args->TryGetBoolField(TEXT("includeChildActors"), bIncludeChildActors);
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(bOnlyCollidingComponents, Origin, Extent, bIncludeChildActors);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
		Result->SetObjectField(TEXT("origin"), JsonConversion::MakeVectorObject(Origin));
		Result->SetObjectField(TEXT("extent"), JsonConversion::MakeVectorObject(Extent));
		Result->SetObjectField(TEXT("size"), JsonConversion::MakeVectorObject(Extent * 2.0));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetComponentTree(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		TInlineComponentArray<UActorComponent*> Components(Actor);
		TArray<TSharedPtr<FJsonValue>> Values;
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component))
			{
				continue;
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Component->GetName());
			Item->SetStringField(TEXT("class"), Component->GetClass()->GetName());
			if (const USceneComponent* Scene = Cast<USceneComponent>(Component))
			{
				Item->SetStringField(TEXT("parent"), Scene->GetAttachParent() ? Scene->GetAttachParent()->GetName() : FString());
				Item->SetObjectField(TEXT("relativeLocation"), JsonConversion::MakeVectorObject(Scene->GetRelativeLocation()));
			}
			Values.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
		Result->SetNumberField(TEXT("count"), Values.Num());
		Result->SetArrayField(TEXT("components"), Values);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetRelativeTransform(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		const USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			return ErrorJson(TEXT("Actor 没有 RootComponent。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Root->GetRelativeLocation()));
		Result->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Root->GetRelativeRotation()));
		Result->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Root->GetRelativeScale3D()));
		Result->SetStringField(TEXT("parent"), Actor->GetAttachParentActor() ? Actor->GetAttachParentActor()->GetActorLabel() : FString());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ResolveActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("resolved"), true);
		Result->SetStringField(TEXT("objectPath"), Actor->GetPathName());
		Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetActorFolderPath(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		FString FolderPath;
		if (!Args->TryGetStringField(TEXT("folderPath"), FolderPath))
		{
			Args->TryGetStringField(TEXT("folder"), FolderPath);
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetActorFolderPath", "MCP 设置 Actor 文件夹"));
		Actor->Modify();
		Actor->SetFolderPath(FName(*FolderPath));
		Actor->MarkPackageDirty();
		return MakeActorMutationResult(Actor, TEXT("set_actor_folder_path"));
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AddActorTag(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		FString Tag;
		if (!Args->TryGetStringField(TEXT("tag"), Tag) || Tag.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 tag。"));
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AddActorTag", "MCP 添加 Actor 标签"));
		Actor->Modify();
		Actor->Tags.AddUnique(FName(*Tag));
		Actor->MarkPackageDirty();
		return ListActorTags(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::RemoveActorTag(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		FString Tag;
		if (!Args->TryGetStringField(TEXT("tag"), Tag) || Tag.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 tag。"));
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "RemoveActorTag", "MCP 移除 Actor 标签"));
		Actor->Modify();
		Actor->Tags.Remove(FName(*Tag));
		Actor->MarkPackageDirty();
		return ListActorTags(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetActorTags(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		const TArray<FString> Tags = ReadTags(Args);
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetActorTags", "MCP 设置 Actor 标签"));
		Actor->Modify();
		Actor->Tags.Reset();
		for (const FString& Tag : Tags)
		{
			if (!Tag.IsEmpty())
			{
				Actor->Tags.AddUnique(FName(*Tag));
			}
		}
		Actor->MarkPackageDirty();
		return ListActorTags(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ListActorTags(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
		Result->SetNumberField(TEXT("count"), Actor->Tags.Num());
		Result->SetArrayField(TEXT("tags"), MakeTagValues(Actor));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::DetachActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "DetachActor", "MCP 分离 Actor"));
		Actor->Modify();
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Actor->MarkPackageDirty();
		return MakeActorMutationResult(Actor, TEXT("detach_actor"));
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetActorMobility(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindRequiredActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			return ErrorJson(TEXT("Actor 没有 RootComponent。"));
		}
		FString MobilityText;
		if (!Args->TryGetStringField(TEXT("mobility"), MobilityText))
		{
			return ErrorJson(TEXT("缺少必填 mobility。"));
		}
		EComponentMobility::Type Mobility = EComponentMobility::Movable;
		if (MobilityText.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
			Mobility = EComponentMobility::Static;
		else if (MobilityText.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
			Mobility = EComponentMobility::Stationary;
		else if (!MobilityText.Equals(TEXT("Movable"), ESearchCase::IgnoreCase))
			return ErrorJson(TEXT("mobility 必须是 Static、Stationary 或 Movable。"));

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetActorMobility", "MCP 设置 Actor 移动性"));
		Root->Modify();
		Root->SetMobility(Mobility);
		Root->MarkPackageDirty();
		return MakeActorMutationResult(Actor, TEXT("set_actor_mobility"));
	}

}
