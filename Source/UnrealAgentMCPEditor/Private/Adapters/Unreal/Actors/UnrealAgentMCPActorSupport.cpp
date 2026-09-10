// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPActorSupport.cpp
 * @brief MCP Actor 查找、类解析与 JSON 序列化实现。
 */

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"

namespace UnrealAgentMCP::ActorSupport
{
	using namespace JsonConversion;

	UWorld* GetEditorWorld()
	{
		if (GEditor)
		{
			return GEditor->GetEditorWorldContext().World();
		}
		return GWorld;
	}

	AActor* FindActorByNameOrLabel(UWorld* World, const FString& NameOrLabel)
	{
		if (!World || NameOrLabel.IsEmpty())
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			if (Actor->GetName().Equals(NameOrLabel, ESearchCase::IgnoreCase) || Actor->GetActorLabel().Equals(NameOrLabel, ESearchCase::IgnoreCase))
			{
				return Actor;
			}
		}

		return nullptr;
	}

	UClass* ResolveActorClass(const FString& ClassText)
	{
		if (ClassText.IsEmpty())
		{
			return AActor::StaticClass();
		}

		UClass* ActorClass = FindObject<UClass>(nullptr, *ClassText);
		if (!ActorClass)
		{
			ActorClass = LoadObject<UClass>(nullptr, *ClassText);
		}

		if (!ActorClass && !ClassText.StartsWith(TEXT("/")))
		{
			const FString EngineClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassText);
			ActorClass = FindObject<UClass>(nullptr, *EngineClassPath);
		}

		if (!ActorClass && ClassText.StartsWith(TEXT("/")))
		{
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *NormalizeAssetObjectPath(ClassText));
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
			{
				ActorClass = Blueprint->GeneratedClass;
			}
		}

		return ActorClass && ActorClass->IsChildOf(AActor::StaticClass()) ? ActorClass : nullptr;
	}

	FString MobilityToString(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static:
			return TEXT("Static");
		case EComponentMobility::Stationary:
			return TEXT("Stationary");
		case EComponentMobility::Movable:
			return TEXT("Movable");
		default:
			return TEXT("Unknown");
		}
	}

	int32 CountActorComponents(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return 0;
		}
		TInlineComponentArray<UActorComponent*> Components(Actor);
		return Components.Num();
	}

	TSharedPtr<FJsonObject> MakeActorObject(AActor* Actor)
	{
		TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
		if (!IsValid(Actor))
		{
			return ActorJson;
		}

		ActorJson->SetStringField(TEXT("name"), Actor->GetName());
		ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorJson->SetStringField(TEXT("path"), Actor->GetPathName());

		const FName FolderPath = Actor->GetFolderPath();
		ActorJson->SetStringField(TEXT("folderPath"), FolderPath.IsNone() ? FString() : FolderPath.ToString());

		ActorJson->SetObjectField(TEXT("location"), MakeVectorObject(Actor->GetActorLocation()));
		ActorJson->SetObjectField(TEXT("rotation"), MakeRotatorObject(Actor->GetActorRotation()));
		ActorJson->SetObjectField(TEXT("scale"), MakeVectorObject(Actor->GetActorScale3D()));

		if (const USceneComponent* Root = Actor->GetRootComponent())
		{
			ActorJson->SetStringField(TEXT("mobility"), MobilityToString(Root->Mobility.GetValue()));
		}

		ActorJson->SetNumberField(TEXT("componentCount"), CountActorComponents(Actor));
		ActorJson->SetBoolField(TEXT("selected"), Actor->IsSelected());

		if (Actor->Tags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FName& Tag : Actor->Tags)
			{
				Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			ActorJson->SetArrayField(TEXT("tags"), Tags);
		}

		return ActorJson;
	}
}
