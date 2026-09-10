// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Live.cpp
 * @brief 场景中 SkeletalMeshComponent 的骨骼查询与预览控制。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.Internal.h"

#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace UnrealAgentMCP
{
	using namespace AnimationPrivate;

	namespace
	{
		AActor* FindActor(const FString& Label)
		{
			if (!GEditor || !GEditor->GetEditorWorldContext().World())
				return nullptr;
			for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
			{
				if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase) || It->GetName().Equals(Label, ESearchCase::IgnoreCase))
					return *It;
			}
			return nullptr;
		}

		USkeletalMeshComponent* FindComponent(AActor* Actor, const FString& Name)
		{
			if (!Actor)
				return nullptr;
			TArray<USkeletalMeshComponent*> Components;
			Actor->GetComponents(Components);
			if (!Name.IsEmpty())
			{
				for (USkeletalMeshComponent* Component : Components)
				{
					if (Component && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
						return Component;
				}
			}
			return Components.IsEmpty() ? nullptr : Components[0];
		}

		TSharedPtr<FJsonObject> TransformObject(const FTransform& Transform)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			const FVector Location = Transform.GetLocation();
			const FRotator Rotation = Transform.Rotator();
			const FVector Scale = Transform.GetScale3D();
			TSharedRef<FJsonObject> LocationObject = MakeShared<FJsonObject>();
			LocationObject->SetNumberField(TEXT("x"), Location.X);
			LocationObject->SetNumberField(TEXT("y"), Location.Y);
			LocationObject->SetNumberField(TEXT("z"), Location.Z);
			TSharedRef<FJsonObject> RotationObject = MakeShared<FJsonObject>();
			RotationObject->SetNumberField(TEXT("pitch"), Rotation.Pitch);
			RotationObject->SetNumberField(TEXT("yaw"), Rotation.Yaw);
			RotationObject->SetNumberField(TEXT("roll"), Rotation.Roll);
			TSharedRef<FJsonObject> ScaleObject = MakeShared<FJsonObject>();
			ScaleObject->SetNumberField(TEXT("x"), Scale.X);
			ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
			ScaleObject->SetNumberField(TEXT("z"), Scale.Z);
			Result->SetObjectField(TEXT("location"), LocationObject);
			Result->SetObjectField(TEXT("rotation"), RotationObject);
			Result->SetObjectField(TEXT("scale"), ScaleObject);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAnimationAdapter::Live(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		const FString Label = StringArg(Args, { TEXT("actorLabel"), TEXT("actorName") });
		if (Label.IsEmpty())
			return Error(TEXT("缺少必填 actorLabel。"));
		AActor* Actor = FindActor(Label);
		if (!Actor)
			return Error(FString::Printf(TEXT("找不到 Actor：%s"), *Label));
		USkeletalMeshComponent* Component = FindComponent(Actor, StringArg(Args, { TEXT("componentName"), TEXT("bodyComponent") }));
		if (!Component)
			return Error(TEXT("Actor 上没有 SkeletalMeshComponent。"));

		if (Action == TEXT("get_bone_transform"))
		{
			const FName Bone(*StringArg(Args, { TEXT("boneName") }));
			if (Bone.IsNone())
				return Error(TEXT("缺少必填 boneName。"));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Result->SetStringField(TEXT("component"), Component->GetName());
			Result->SetStringField(TEXT("boneName"), Bone.ToString());
			Result->SetObjectField(TEXT("transform"), TransformObject(Component->GetSocketTransform(Bone, RTS_World)));
			return Success(Result);
		}

		if (Action == TEXT("list_bones"))
		{
			const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
			if (!Mesh)
				return Error(TEXT("组件尚未配置 SkeletalMesh。"));
			const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
			{
				TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
				Bone->SetStringField(TEXT("name"), Reference.GetBoneName(Index).ToString());
				Bone->SetNumberField(TEXT("index"), Index);
				Bone->SetNumberField(TEXT("parentIndex"), Reference.GetParentIndex(Index));
				Bones.Add(MakeShared<FJsonValueObject>(Bone));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("bones"), Bones);
			Result->SetNumberField(TEXT("count"), Bones.Num());
			return Success(Result);
		}

		if (Action == TEXT("rebind_leader_pose"))
		{
			TArray<USkeletalMeshComponent*> Components;
			Actor->GetComponents(Components);
			int32 Count = 0;
			for (USkeletalMeshComponent* Secondary : Components)
			{
				if (Secondary && Secondary != Component)
				{
					Secondary->SetLeaderPoseComponent(Component);
					++Count;
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("reboundComponents"), Count);
			return Success(Result);
		}

		const bool bEnabled = BoolArg(Args, TEXT("enabled"), true);
		TArray<USkeletalMeshComponent*> Components;
		Actor->GetComponents(Components);
		for (USkeletalMeshComponent* Item : Components)
		{
			if (!Item)
				continue;
			Item->SetUpdateAnimationInEditor(bEnabled);
			Item->VisibilityBasedAnimTickOption =
				bEnabled ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			Item->MarkRenderStateDirty();
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("enabled"), bEnabled);
		Result->SetNumberField(TEXT("componentCount"), Components.Num());
		return Success(Result);
	}
}
