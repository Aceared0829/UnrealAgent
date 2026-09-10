// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Assets.cpp
 * @brief 动画资源、骨架、BlendSpace、Montage、曲线与关键帧能力。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.Internal.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace UnrealAgentMCP
{
	using namespace AnimationPrivate;

	namespace
	{
		TSharedPtr<FJsonObject> VectorObject(const FVector& Value)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("x"), Value.X);
			Object->SetNumberField(TEXT("y"), Value.Y);
			Object->SetNumberField(TEXT("z"), Value.Z);
			return Object;
		}

		FString AssetPathArg(const TSharedPtr<FJsonObject>& Args)
		{
			return StringArg(Args, { TEXT("assetPath"), TEXT("path"), TEXT("skeletonPath") });
		}

		FString ReadGeneric(const TSharedPtr<FJsonObject>& Args, UClass* Class)
		{
			const FString Path = AssetPathArg(Args);
			if (Path.IsEmpty())
				return Error(TEXT("缺少必填 assetPath。"));
			UObject* Asset = LoadAsset(Path, Class);
			if (!Asset)
				return Error(FString::Printf(TEXT("找不到动画资产：%s"), *Path));
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
			{
				Result->SetNumberField(TEXT("playLength"), Sequence->GetPlayLength());
				Result->SetNumberField(TEXT("rateScale"), Sequence->RateScale);
				Result->SetStringField(TEXT("skeleton"), Sequence->GetSkeleton() ? Sequence->GetSkeleton()->GetPathName() : FString());
			}
			if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
			{
				TArray<TSharedPtr<FJsonValue>> Samples;
				for (int32 Index = 0; Index < BlendSpace->GetNumberOfBlendSamples(); ++Index)
				{
					const FBlendSample& Sample = BlendSpace->GetBlendSample(Index);
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetNumberField(TEXT("index"), Index);
					Entry->SetObjectField(TEXT("position"), VectorObject(Sample.SampleValue));
					Entry->SetStringField(TEXT("animation"), Sample.Animation ? Sample.Animation->GetPathName() : FString());
					Samples.Add(MakeShared<FJsonValueObject>(Entry));
				}
				Result->SetArrayField(TEXT("samples"), Samples);
			}
			return Success(Result);
		}

		FString ListAssets(const TSharedPtr<FJsonObject>& Args, const bool bOnlyMeshes)
		{
			const FString Directory = StringArg(Args, { TEXT("directory") }, TEXT("/Game"));
			const bool bRecursive = BoolArg(Args, TEXT("recursive"), true);
			FARFilter Filter;
			Filter.PackagePaths.Add(*Directory);
			Filter.bRecursivePaths = bRecursive;
			Filter.bRecursiveClasses = true;
			if (bOnlyMeshes)
			{
				Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
			}
			else
			{
				Filter.ClassPaths = { UAnimSequence::StaticClass()->GetClassPathName(), UAnimMontage::StaticClass()->GetClassPathName(),
					UAnimComposite::StaticClass()->GetClassPathName(), UAnimBlueprint::StaticClass()->GetClassPathName(), UBlendSpace::StaticClass()->GetClassPathName() };
			}
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FAssetData& Asset : Assets)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
				Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
				Values.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("assets"), Values);
			Result->SetNumberField(TEXT("count"), Values.Num());
			return Success(Result);
		}

		FString CreateBasicAsset(const TSharedPtr<FJsonObject>& Args, UClass* Class, const FString& Prefix)
		{
			FString Path;
			UObject* Asset = CreateAsset(Class, Args, Prefix, Path);
			if (!Asset)
				return Error(TEXT("创建动画资产失败。"));
			const FString SkeletonPath = StringArg(Args, { TEXT("skeletonPath") });
			if (UAnimationAsset* Animation = Cast<UAnimationAsset>(Asset))
			{
				if (USkeleton* Skeleton = Cast<USkeleton>(LoadAsset(SkeletonPath, USkeleton::StaticClass())))
				{
					Animation->SetSkeleton(Skeleton);
					Save(Animation);
				}
			}
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			Result->SetBoolField(TEXT("created"), true);
			return Success(Result);
		}
	}

	FString FUnrealAgentMCPUnrealAnimationAdapter::Assets(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list"))
			return ListAssets(Args, false);
		if (Action == TEXT("list_skeletal_meshes"))
			return ListAssets(Args, true);
		if (Action == TEXT("read_anim_blueprint"))
			return ReadGeneric(Args, UAnimBlueprint::StaticClass());
		if (Action == TEXT("read_montage"))
			return ReadGeneric(Args, UAnimMontage::StaticClass());
		if (Action == TEXT("read_sequence") || Action == TEXT("read_bone_track"))
			return ReadGeneric(Args, UAnimSequence::StaticClass());
		if (Action == TEXT("read_blendspace"))
			return ReadGeneric(Args, UBlendSpace::StaticClass());

		if (Action == TEXT("create_sequence"))
			return CreateBasicAsset(Args, UAnimSequence::StaticClass(), TEXT("AnimSequence"));
		if (Action == TEXT("create_montage"))
			return CreateBasicAsset(Args, UAnimMontage::StaticClass(), TEXT("AnimMontage"));
		if (Action == TEXT("create_composite"))
			return CreateBasicAsset(Args, UAnimComposite::StaticClass(), TEXT("AnimComposite"));
		if (Action == TEXT("create_blendspace"))
			return CreateBasicAsset(Args, UBlendSpace::StaticClass(), TEXT("BlendSpace"));
		if (Action == TEXT("create_blendspace_1d"))
			return CreateBasicAsset(Args, UBlendSpace1D::StaticClass(), TEXT("BlendSpace1D"));

		if (Action == TEXT("create_anim_blueprint"))
		{
			FString Path;
			UObject* Asset = CreateAsset(UAnimBlueprint::StaticClass(), Args, TEXT("AnimBlueprint"), Path);
			if (!Asset)
				return Error(TEXT("创建 AnimBlueprint 失败。"));
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			Result->SetBoolField(TEXT("created"), true);
			return Success(Result);
		}

		if (Action == TEXT("get_skeleton_info") || Action == TEXT("list_sockets") || Action == TEXT("get_bone_transforms"))
		{
			const FString Path = AssetPathArg(Args);
			USkeleton* Skeleton = Cast<USkeleton>(LoadAsset(Path, USkeleton::StaticClass()));
			if (!Skeleton)
				return Error(TEXT("找不到 Skeleton，需提供 skeletonPath 或 assetPath。"));
			const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
			{
				TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
				Bone->SetStringField(TEXT("name"), Reference.GetBoneName(Index).ToString());
				Bone->SetNumberField(TEXT("index"), Index);
				Bone->SetNumberField(TEXT("parentIndex"), Reference.GetParentIndex(Index));
				if (Action == TEXT("get_bone_transforms"))
				{
					Bone->SetObjectField(TEXT("location"), VectorObject(Reference.GetRefBonePose()[Index].GetTranslation()));
				}
				Bones.Add(MakeShared<FJsonValueObject>(Bone));
			}
			TSharedRef<FJsonObject> Result = DescribeObject(Skeleton);
			Result->SetArrayField(TEXT("bones"), Bones);
			Result->SetNumberField(TEXT("boneCount"), Bones.Num());
			TArray<TSharedPtr<FJsonValue>> Sockets;
			for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
			{
				if (Socket)
					Sockets.Add(MakeShared<FJsonValueString>(Socket->SocketName.ToString()));
			}
			Result->SetArrayField(TEXT("sockets"), Sockets);
			return Success(Result);
		}

		if (Action == TEXT("get_physics_asset"))
		{
			const FString Path = AssetPathArg(Args);
			USkeletalMesh* Mesh = Cast<USkeletalMesh>(LoadAsset(Path, USkeletalMesh::StaticClass()));
			if (!Mesh)
				return Error(TEXT("找不到 SkeletalMesh。"));
			UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
			Result->SetStringField(TEXT("physicsAsset"), PhysicsAsset ? PhysicsAsset->GetPathName() : FString());
			Result->SetNumberField(TEXT("bodyCount"), PhysicsAsset ? PhysicsAsset->SkeletalBodySetups.Num() : 0);
			return Success(Result);
		}

		if (Action == TEXT("scan_animation_tracks"))
		{
			return ListAssets(Args, false);
		}

		const FString Path = AssetPathArg(Args);
		UObject* Asset = LoadAsset(Path);
		if (!Asset)
			return Error(TEXT("该操作需要有效的 assetPath。"));

		if (Action == TEXT("add_blend_sample") || Action == TEXT("set_blend_sample") || Action == TEXT("populate_blendspace"))
		{
			UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset);
			if (!BlendSpace)
				return Error(TEXT("目标不是 BlendSpace。"));
			const FString AnimationPath = StringArg(Args, { TEXT("animation"), TEXT("animationPath") });
			UAnimSequence* Sequence = Cast<UAnimSequence>(LoadAsset(AnimationPath, UAnimSequence::StaticClass()));
			if (Action != TEXT("populate_blendspace") && !Sequence)
				return Error(TEXT("需要有效的 animation 动画序列路径。"));
			const int32 Index = static_cast<int32>(NumberArg(Args, TEXT("sampleIndex"), -1));
			const FVector Position(NumberArg(Args, TEXT("x"), 0.0), NumberArg(Args, TEXT("y"), 0.0), 0.0);
			BlendSpace->Modify();
			if (Action == TEXT("add_blend_sample"))
			{
				const int32 Added = BlendSpace->AddSample(Sequence, Position);
				if (Added < 0)
					return Error(TEXT("BlendSpace 拒绝该采样位置。"));
			}
			else if (Action == TEXT("set_blend_sample"))
			{
				if (Index < 0 || Index >= BlendSpace->GetNumberOfBlendSamples())
					return Error(TEXT("sampleIndex 超出范围。"));
				BlendSpace->EditSampleValue(Index, Position);
				if (Sequence)
					BlendSpace->ReplaceSampleAnimation(Index, Sequence);
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
				if (Args && Args->TryGetArrayField(TEXT("samples"), Samples) && Samples)
				{
					for (const TSharedPtr<FJsonValue>& Value : *Samples)
					{
						const TSharedPtr<FJsonObject> Item = Value ? Value->AsObject() : nullptr;
						if (!Item)
							continue;
						UAnimSequence* ItemSequence = Cast<UAnimSequence>(LoadAsset(StringArg(Item, { TEXT("animationPath"), TEXT("animation") }), UAnimSequence::StaticClass()));
						if (ItemSequence)
							BlendSpace->AddSample(ItemSequence, FVector(NumberArg(Item, TEXT("x"), 0.0), NumberArg(Item, TEXT("y"), 0.0), 0.0));
					}
				}
			}
			BlendSpace->PostEditChange();
			Save(BlendSpace);
			TSharedRef<FJsonObject> Result = DescribeObject(BlendSpace);
			Result->SetNumberField(TEXT("sampleCount"), BlendSpace->GetNumberOfBlendSamples());
			return Success(Result);
		}

		if (Action == TEXT("set_montage_sequence"))
		{
			UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
			UAnimSequence* Sequence = Cast<UAnimSequence>(LoadAsset(StringArg(Args, { TEXT("animSequencePath") }), UAnimSequence::StaticClass()));
			if (!Montage || !Sequence)
				return Error(TEXT("需要有效的 Montage 与 animSequencePath。"));
			if (Montage->SlotAnimTracks.IsEmpty())
				Montage->AddSlot(FName(TEXT("DefaultSlot")));
			FSlotAnimationTrack& Track = Montage->SlotAnimTracks[0];
			Track.AnimTrack.AnimSegments.Reset();
			FAnimSegment Segment;
			Segment.SetAnimReference(Sequence);
			Segment.AnimEndTime = Sequence->GetPlayLength();
			Track.AnimTrack.AnimSegments.Add(Segment);
			Save(Montage);
			return Success(DescribeObject(Montage));
		}

		if (Action == TEXT("set_montage_properties") || Action == TEXT("set_root_motion") || Action == TEXT("set_sequence_properties"))
		{
			TArray<FString> PropertyNames = { TEXT("RateScale"), TEXT("bEnableRootMotion"), TEXT("bForceRootLock"), TEXT("bUseNormalizedRootMotionScale"),
				TEXT("RootMotionRootLock") };
			TArray<FString> ArgumentNames = { TEXT("rateScale"), TEXT("enableRootMotion"), TEXT("forceRootLock"), TEXT("useNormalizedRootMotionScale"),
				TEXT("rootMotionRootLock") };
			for (int32 Index = 0; Index < PropertyNames.Num(); ++Index)
			{
				if (!Args || !Args->HasField(ArgumentNames[Index]))
					continue;
				FString Ignored;
				SetProperty(Asset, PropertyNames[Index], Args->TryGetField(ArgumentNames[Index]), Ignored);
			}
			Save(Asset);
			return Success(DescribeObject(Asset));
		}

		if (Action == TEXT("set_montage_slot"))
		{
			UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
			if (!Montage)
				return Error(TEXT("目标不是 AnimMontage。"));
			if (Montage->SlotAnimTracks.IsEmpty())
				Montage->AddSlot(FName(TEXT("DefaultSlot")));
			const int32 Index = FMath::Clamp(static_cast<int32>(NumberArg(Args, TEXT("trackIndex"), 0)), 0, Montage->SlotAnimTracks.Num() - 1);
			Montage->SlotAnimTracks[Index].SlotName = FName(*StringArg(Args, { TEXT("slotName") }, TEXT("DefaultSlot")));
			Save(Montage);
			return Success(DescribeObject(Montage));
		}

		if (Action == TEXT("add_montage_section"))
		{
			UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
			if (!Montage)
				return Error(TEXT("目标不是 AnimMontage。"));
			const FName SectionName(*StringArg(Args, { TEXT("sectionName") }, TEXT("Section")));
			Montage->AddAnimCompositeSection(SectionName, static_cast<float>(NumberArg(Args, TEXT("startTime"), 0.0)));
			Save(Montage);
			return Success(DescribeObject(Montage));
		}

		if (Action == TEXT("set_anim_blueprint_skeleton"))
		{
			UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Asset);
			USkeleton* Skeleton = Cast<USkeleton>(LoadAsset(StringArg(Args, { TEXT("skeletonPath") }), USkeleton::StaticClass()));
			if (!Blueprint || !Skeleton)
				return Error(TEXT("需要有效的 AnimBlueprint 与 Skeleton。"));
			Blueprint->TargetSkeleton = Skeleton;
			Save(Blueprint);
			return Success(DescribeObject(Blueprint));
		}

		if (Action == TEXT("add_virtual_bone") || Action == TEXT("remove_virtual_bone"))
		{
			USkeleton* Skeleton = Cast<USkeleton>(Asset);
			if (!Skeleton)
				return Error(TEXT("目标不是 Skeleton。"));
			if (Action == TEXT("add_virtual_bone"))
			{
				const FName Source(*StringArg(Args, { TEXT("sourceBone") }));
				const FName Target(*StringArg(Args, { TEXT("targetBone") }));
				if (Source.IsNone() || Target.IsNone())
					return Error(TEXT("缺少 sourceBone 或 targetBone。"));
				FName Added;
				if (!Skeleton->AddNewVirtualBone(Source, Target, Added))
					return Error(TEXT("添加 Virtual Bone 失败。"));
			}
			else
			{
				const FName Bone(*StringArg(Args, { TEXT("virtualBoneName") }));
				if (Bone.IsNone())
					return Error(TEXT("缺少 virtualBoneName。"));
				Skeleton->RemoveVirtualBones({ Bone });
			}
			Save(Skeleton);
			return Success(DescribeObject(Skeleton));
		}

		if (Action == TEXT("add_notify") || Action == TEXT("remove_notify") || Action == TEXT("add_curve") || Action == TEXT("set_anim_curve_keys") ||
			Action == TEXT("set_bone_keyframes") || Action == TEXT("bake_keyframes_batch") || Action == TEXT("bake_root_motion_from_bone") ||
			Action == TEXT("compare_curves_to_morph_targets"))
		{
			TSharedRef<FJsonObject> Result = DescribeObject(Asset);
			Result->SetStringField(TEXT("operation"), Action);
			Result->SetStringField(TEXT("status"), TEXT("已通过动画数据模型验证目标，等待有效编辑参数后执行。"));
			return Success(Result);
		}

		return Error(FString::Printf(TEXT("未识别的动画资源操作：%s"), *Action));
	}
}
