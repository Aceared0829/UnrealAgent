// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPActorTools.cpp
 * @brief 关卡 Actor 查询、选择、生成、变换、附加、删除与属性命令。
 */

#include "Adapters/Tooling/UnrealAgentMCPTools.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"

namespace UnrealAgentMCP::Tools
{
	using namespace ActorSupport;
	using namespace AssetSupport;
	using namespace JsonConversion;
	using namespace PropertyWriter;

	FString ListLevelActors(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("Editor world is not available."));
		}

		FString ClassFilter;
		Args->TryGetStringField(TEXT("classFilter"), ClassFilter);

		FString NameContains;
		Args->TryGetStringField(TEXT("nameContains"), NameContains);

		bool bSelectedOnly = false;
		Args->TryGetBoolField(TEXT("selectedOnly"), bSelectedOnly);

		double MaxResultsNumber = 200.0;
		Args->TryGetNumberField(TEXT("maxResults"), MaxResultsNumber);
		const int32 MaxResults = FMath::Clamp(static_cast<int32>(MaxResultsNumber), 1, 1000);

		TArray<TSharedPtr<FJsonValue>> Actors;
		int32 MatchedCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			if (bSelectedOnly && !Actor->IsSelected())
			{
				continue;
			}

			const FString ClassName = Actor->GetClass()->GetName();
			const FString Label = Actor->GetActorLabel();
			if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!NameContains.IsEmpty() && !Label.Contains(NameContains, ESearchCase::IgnoreCase) && !Actor->GetName().Contains(NameContains, ESearchCase::IgnoreCase))
			{
				continue;
			}

			++MatchedCount;
			if (Actors.Num() < MaxResults)
			{
				Actors.Add(MakeShared<FJsonValueObject>(MakeActorObject(Actor)));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Actors.Num());
		Result->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Result->SetBoolField(TEXT("truncated"), MatchedCount > Actors.Num());
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString SelectActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) && !Args->TryGetStringField(TEXT("label"), Name))
		{
			return ErrorJson(TEXT("Missing required field 'name'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Actor = FindActorByNameOrLabel(World, Name);
		if (!Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		if (GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString SpawnActor(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("Editor world is not available."));
		}

		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector::OneVector;
		TryGetVectorField(Args, TEXT("location"), Location);
		TryGetRotatorField(Args, TEXT("rotation"), Rotation);
		TryGetVectorField(Args, TEXT("scale"), Scale);

		FString Label;
		Args->TryGetStringField(TEXT("label"), Label);
		bool bSelectSpawnedActor = false;
		Args->TryGetBoolField(TEXT("select"), bSelectSpawnedActor);

		FString StaticMeshPath;
		Args->TryGetStringField(TEXT("staticMeshPath"), StaticMeshPath);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SpawnActor", "MCP Spawn Actor"));
		World->Modify();

		AActor* Actor = nullptr;
		if (!StaticMeshPath.IsEmpty())
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *NormalizeAssetObjectPath(StaticMeshPath));
			if (!Mesh)
			{
				return ErrorJson(FString::Printf(TEXT("Static mesh not found: %s"), *StaticMeshPath));
			}

			AStaticMeshActor* StaticMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation);
			if (!StaticMeshActor)
			{
				return ErrorJson(TEXT("Failed to spawn static mesh actor."));
			}

			StaticMeshActor->Modify();
			StaticMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor = StaticMeshActor;
		}
		else
		{
			FString ClassText = TEXT("Actor");
			Args->TryGetStringField(TEXT("class"), ClassText);

			UClass* ActorClass = ResolveActorClass(ClassText);
			if (!ActorClass)
			{
				return ErrorJson(FString::Printf(TEXT("Actor class not found or not spawnable: %s"), *ClassText));
			}

			Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation);
			if (!Actor)
			{
				return ErrorJson(FString::Printf(TEXT("Failed to spawn actor of class: %s"), *ActorClass->GetName()));
			}
			Actor->Modify();
		}

		Actor->SetActorScale3D(Scale);
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}

		World->MarkPackageDirty();
		if (bSelectSpawnedActor && GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString TransformActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) && !Args->TryGetStringField(TEXT("label"), Name))
		{
			return ErrorJson(TEXT("Missing required field 'name'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Actor = FindActorByNameOrLabel(World, Name);
		if (!Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		FVector Location = Actor->GetActorLocation();
		FRotator Rotation = Actor->GetActorRotation();
		FVector Scale = Actor->GetActorScale3D();
		const bool bHasLocation = TryGetVectorField(Args, TEXT("location"), Location);
		const bool bHasRotation = TryGetRotatorField(Args, TEXT("rotation"), Rotation);
		const bool bHasScale = TryGetVectorField(Args, TEXT("scale"), Scale);
		if (!bHasLocation && !bHasRotation && !bHasScale)
		{
			return ErrorJson(TEXT("Provide at least one of location, rotation, or scale."));
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "TransformActor", "MCP Transform Actor"));
		Actor->Modify();
		if (bHasLocation || bHasRotation)
		{
			Actor->SetActorLocationAndRotation(Location, Rotation);
		}
		if (bHasScale)
		{
			Actor->SetActorScale3D(Scale);
		}
		Actor->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString RenameActor(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("Editor world is not available."));
		}

		FString NewLabel;
		if (!Args->TryGetStringField(TEXT("newLabel"), NewLabel))
		{
			return ErrorJson(TEXT("Missing required field 'newLabel'."));
		}
		NewLabel.TrimStartAndEndInline();
		if (NewLabel.IsEmpty())
		{
			return ErrorJson(TEXT("Field 'newLabel' cannot be empty."));
		}

		FString Name;
		Args->TryGetStringField(TEXT("name"), Name);
		AActor* Actor = Name.IsEmpty() ? nullptr : FindActorByNameOrLabel(World, Name);
		if (Name.IsEmpty() && GEditor)
		{
			TArray<AActor*> SelectedActors;
			if (USelection* Selection = GEditor->GetSelectedActors())
			{
				Selection->GetSelectedObjects<AActor>(SelectedActors);
			}
			if (SelectedActors.Num() != 1)
			{
				return ErrorJson(TEXT("Omit 'name' only when exactly one actor is selected."));
			}
			Actor = SelectedActors[0];
		}
		if (!IsValid(Actor))
		{
			return ErrorJson(Name.IsEmpty() ? TEXT("No valid selected actor was found.") : FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Existing = *It;
			if (Existing != Actor && IsValid(Existing) && Existing->GetActorLabel().Equals(NewLabel, ESearchCase::IgnoreCase))
			{
				return ErrorJson(FString::Printf(TEXT("Actor label already exists: %s"), *NewLabel));
			}
		}

		const FString PreviousLabel = Actor->GetActorLabel();
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "RenameActor", "MCP Rename Actor"));
		Actor->Modify();
		Actor->SetActorLabel(NewLabel, true);
		const FString VerifiedLabel = Actor->GetActorLabel();
		if (VerifiedLabel != NewLabel)
		{
			Actor->SetActorLabel(PreviousLabel, true);
			return ErrorJson(FString::Printf(TEXT("Actor label readback mismatch: expected '%s', got '%s'."), *NewLabel, *VerifiedLabel));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("previousLabel"), PreviousLabel);
		Result->SetStringField(TEXT("newLabel"), NewLabel);
		Result->SetStringField(TEXT("verifiedLabel"), VerifiedLabel);
		Result->SetBoolField(TEXT("verified"), true);
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString DeleteActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) && !Args->TryGetStringField(TEXT("label"), Name))
		{
			return ErrorJson(TEXT("Missing required field 'name'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Actor = FindActorByNameOrLabel(World, Name);
		if (!World || !Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		TSharedPtr<FJsonObject> ActorBeforeDelete = MakeActorObject(Actor);
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "DeleteActor", "MCP Delete Actor"));
		World->Modify();
		Actor->Modify();
		if (GEditor)
		{
			GEditor->SelectActor(Actor, false, false);
		}
		const bool bDestroyed = World->EditorDestroyActor(Actor, true);
		World->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("deleted"), bDestroyed);
		Result->SetObjectField(TEXT("actor"), ActorBeforeDelete);
		return bDestroyed ? SuccessJson(Result) : ErrorJson(TEXT("EditorDestroyActor failed."));
	}

	FString AttachActor(const TSharedPtr<FJsonObject>& Args)
	{
		FString ChildName;
		if (!Args->TryGetStringField(TEXT("child"), ChildName) && !Args->TryGetStringField(TEXT("childName"), ChildName) && !Args->TryGetStringField(TEXT("name"), ChildName))
		{
			return ErrorJson(TEXT("Missing required field 'child'."));
		}

		FString ParentName;
		if (!Args->TryGetStringField(TEXT("parent"), ParentName) && !Args->TryGetStringField(TEXT("parentName"), ParentName))
		{
			return ErrorJson(TEXT("Missing required field 'parent'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Child = FindActorByNameOrLabel(World, ChildName);
		AActor* Parent = FindActorByNameOrLabel(World, ParentName);
		if (!Child)
		{
			return ErrorJson(FString::Printf(TEXT("Child actor not found: %s"), *ChildName));
		}
		if (!Parent)
		{
			return ErrorJson(FString::Printf(TEXT("Parent actor not found: %s"), *ParentName));
		}
		if (Child == Parent)
		{
			return ErrorJson(TEXT("An actor cannot be attached to itself."));
		}

		bool bKeepWorldTransform = true;
		Args->TryGetBoolField(TEXT("keepWorldTransform"), bKeepWorldTransform);

		FString Socket;
		Args->TryGetStringField(TEXT("socket"), Socket);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AttachActor", "MCP Attach Actor"));
		Child->Modify();
		Parent->Modify();
		const FAttachmentTransformRules Rules = bKeepWorldTransform ? FAttachmentTransformRules::KeepWorldTransform : FAttachmentTransformRules::KeepRelativeTransform;
		const bool bAttached = Child->AttachToActor(Parent, Rules, Socket.IsEmpty() ? NAME_None : FName(*Socket));
		Child->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("attached"), bAttached);
		Result->SetObjectField(TEXT("child"), MakeActorObject(Child));
		Result->SetObjectField(TEXT("parent"), MakeActorObject(Parent));
		return bAttached ? SuccessJson(Result) : ErrorJson(TEXT("AttachToActor failed."));
	}

	FString SetActorProperty(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) && !Args->TryGetStringField(TEXT("label"), Name))
		{
			return ErrorJson(TEXT("Missing required field 'name'."));
		}

		FString PropertyName;
		if (!Args->TryGetStringField(TEXT("property"), PropertyName))
		{
			return ErrorJson(TEXT("Missing required field 'property'."));
		}

		const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
		if (!Value.IsValid())
		{
			return ErrorJson(TEXT("Missing required field 'value'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Actor = FindActorByNameOrLabel(World, Name);
		if (!Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		UObject* Target = Actor;
		FString ComponentName;
		if (Args->TryGetStringField(TEXT("component"), ComponentName) && !ComponentName.IsEmpty())
		{
			Target = nullptr;
			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Component : Components)
			{
				if (IsValid(Component) &&
					(Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) || Component->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
				{
					Target = Component;
					break;
				}
			}
			if (!Target)
			{
				return ErrorJson(FString::Printf(TEXT("Component not found on actor '%s': %s"), *Name, *ComponentName));
			}
		}

		FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Property)
		{
			return ErrorJson(FString::Printf(TEXT("Property '%s' was not found on %s."), *PropertyName, *Target->GetName()));
		}

		FString Error;
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetActorProperty", "MCP Set Actor Property"));
		Target->Modify();
		Target->PreEditChange(Property);
		if (!SetPropertyFromJson(Target, Property, Value, Error))
		{
			return ErrorJson(Error);
		}
		FPropertyChangedEvent ChangeEvent(Property);
		Target->PostEditChangeProperty(ChangeEvent);
		Target->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("target"), Target->GetPathName());
		Result->SetStringField(TEXT("property"), PropertyName);
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));
		return SuccessJson(Result);
	}

	FString SaveCurrentLevel(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("Editor world is not available."));
		}

		UPackage* Package = World->GetOutermost();
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Package);
		const bool bSaved = SavePackages(PackagesToSave);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetStringField(TEXT("levelPackage"), Package ? Package->GetName() : FString());
		return bSaved ? SuccessJson(Result) : ErrorJson(TEXT("Failed to save current level package."));
	}

	FString GetSelectedActors(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<TSharedPtr<FJsonValue>> Actors;
		if (GEditor)
		{
			if (USelection* Selection = GEditor->GetSelectedActors())
			{
				TArray<AActor*> Selected;
				Selection->GetSelectedObjects<AActor>(Selected);
				for (AActor* Actor : Selected)
				{
					if (IsValid(Actor))
					{
						Actors.Add(MakeShared<FJsonValueObject>(MakeActorObject(Actor)));
					}
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Actors.Num());
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString GetActorDetails(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) && !Args->TryGetStringField(TEXT("label"), Name))
		{
			return ErrorJson(TEXT("Missing required field 'name'."));
		}

		UWorld* World = GetEditorWorld();
		AActor* Actor = FindActorByNameOrLabel(World, Name);
		if (!Actor)
		{
			return ErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeActorObject(Actor));

		TInlineComponentArray<UActorComponent*> Components(Actor);
		TArray<TSharedPtr<FJsonValue>> ComponentArray;
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			TSharedRef<FJsonObject> ComponentJson = MakeShared<FJsonObject>();
			ComponentJson->SetStringField(TEXT("name"), Component->GetName());
			ComponentJson->SetStringField(TEXT("class"), Component->GetClass()->GetName());

			if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				ComponentJson->SetObjectField(TEXT("relativeLocation"), MakeVectorObject(SceneComponent->GetRelativeLocation()));
				ComponentJson->SetObjectField(TEXT("relativeRotation"), MakeRotatorObject(SceneComponent->GetRelativeRotation()));
				ComponentJson->SetObjectField(TEXT("relativeScale"), MakeVectorObject(SceneComponent->GetRelativeScale3D()));
				ComponentJson->SetStringField(TEXT("mobility"), MobilityToString(SceneComponent->Mobility.GetValue()));
			}

			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				if (const UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh())
				{
					ComponentJson->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
				}
				ComponentJson->SetNumberField(TEXT("materialCount"), StaticMeshComponent->GetNumMaterials());
			}

			ComponentArray.Add(MakeShared<FJsonValueObject>(ComponentJson));
		}

		Result->SetNumberField(TEXT("componentCount"), ComponentArray.Num());
		Result->SetArrayField(TEXT("components"), ComponentArray);
		return SuccessJson(Result);
	}
}
