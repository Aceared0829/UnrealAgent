// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Mesh.cpp
 * @brief StaticMesh、SkeletalMesh、Socket、骨骼与碰撞信息实现。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "Animation/Skeleton.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> VectorToMeshJson(const FVector& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("x"), Value.X);
			Result->SetNumberField(TEXT("y"), Value.Y);
			Result->SetNumberField(TEXT("z"), Value.Z);
			return Result;
		}

		TSharedRef<FJsonObject> RotatorToMeshJson(const FRotator& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("pitch"), Value.Pitch);
			Result->SetNumberField(TEXT("yaw"), Value.Yaw);
			Result->SetNumberField(TEXT("roll"), Value.Roll);
			return Result;
		}

		bool TryReadMeshVector(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, FVector& OutValue)
		{
			if (!Args.IsValid())
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args->TryGetObjectField(Name, Object) || !Object || !Object->IsValid())
			{
				return false;
			}
			double X = OutValue.X;
			double Y = OutValue.Y;
			double Z = OutValue.Z;
			(*Object)->TryGetNumberField(TEXT("x"), X);
			(*Object)->TryGetNumberField(TEXT("y"), Y);
			(*Object)->TryGetNumberField(TEXT("z"), Z);
			OutValue = FVector(X, Y, Z);
			return true;
		}

		bool TryReadMeshRotator(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, FRotator& OutValue)
		{
			if (!Args.IsValid())
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args->TryGetObjectField(Name, Object) || !Object || !Object->IsValid())
			{
				return false;
			}
			double Pitch = OutValue.Pitch;
			double Yaw = OutValue.Yaw;
			double Roll = OutValue.Roll;
			(*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*Object)->TryGetNumberField(TEXT("roll"), Roll);
			OutValue = FRotator(Pitch, Yaw, Roll);
			return true;
		}

		TSharedRef<FJsonObject> StaticSocketToJson(const UStaticMeshSocket* Socket)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			Result->SetObjectField(TEXT("location"), VectorToMeshJson(Socket->RelativeLocation));
			Result->SetObjectField(TEXT("rotation"), RotatorToMeshJson(Socket->RelativeRotation));
			Result->SetObjectField(TEXT("scale"), VectorToMeshJson(Socket->RelativeScale));
			Result->SetStringField(TEXT("owner"), TEXT("StaticMesh"));
			return Result;
		}

		TSharedRef<FJsonObject> SkeletalSocketToJson(const USkeletalMeshSocket* Socket)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			Result->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
			Result->SetObjectField(TEXT("location"), VectorToMeshJson(Socket->RelativeLocation));
			Result->SetObjectField(TEXT("rotation"), RotatorToMeshJson(Socket->RelativeRotation));
			Result->SetObjectField(TEXT("scale"), VectorToMeshJson(Socket->RelativeScale));
			Result->SetStringField(TEXT("owner"), TEXT("SkeletalMesh"));
			return Result;
		}

		TSharedRef<FJsonObject> BoundsToJson(const FBoxSphereBounds& Bounds)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("origin"), VectorToMeshJson(Bounds.Origin));
			Result->SetObjectField(TEXT("boxExtent"), VectorToMeshJson(Bounds.BoxExtent));
			Result->SetNumberField(TEXT("sphereRadius"), Bounds.SphereRadius);
			return Result;
		}

		TSharedRef<FJsonObject> CollisionToJson(const UBodySetup* BodySetup)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("hasBodySetup"), BodySetup != nullptr);
			if (!BodySetup)
			{
				return Result;
			}
			Result->SetStringField(TEXT("traceFlag"), StaticEnum<ECollisionTraceFlag>()->GetNameStringByValue(static_cast<int64>(BodySetup->CollisionTraceFlag)));
			Result->SetNumberField(TEXT("boxCount"), BodySetup->AggGeom.BoxElems.Num());
			Result->SetNumberField(TEXT("sphereCount"), BodySetup->AggGeom.SphereElems.Num());
			Result->SetNumberField(TEXT("capsuleCount"), BodySetup->AggGeom.SphylElems.Num());
			Result->SetNumberField(TEXT("convexCount"), BodySetup->AggGeom.ConvexElems.Num());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Meshes(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		const FString AssetPath = GetStringArgument(Args, { TEXT("assetPath"), TEXT("path"), TEXT("meshPath") });
		UObject* Asset = Subsystem ? Subsystem->LoadAsset(AssetPath) : nullptr;
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset);
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset);
		if (!StaticMesh && !SkeletalMesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh 或 SkeletalMesh：%s"), *AssetPath));
		}

		if (Action == TEXT("get_mesh_bounds"))
		{
			return SuccessJson(BoundsToJson(StaticMesh ? StaticMesh->GetBounds() : SkeletalMesh->GetBounds()));
		}

		if (Action == TEXT("get_mesh_info"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
			Result->SetStringField(TEXT("meshType"), StaticMesh ? TEXT("StaticMesh") : TEXT("SkeletalMesh"));
			Result->SetObjectField(TEXT("bounds"), BoundsToJson(StaticMesh ? StaticMesh->GetBounds() : SkeletalMesh->GetBounds()));
			Result->SetNumberField(TEXT("materialSlotCount"), StaticMesh ? StaticMesh->GetStaticMaterials().Num() : SkeletalMesh->GetMaterials().Num());
			Result->SetNumberField(TEXT("socketCount"), StaticMesh ? StaticMesh->Sockets.Num() : SkeletalMesh->NumSockets());
			if (SkeletalMesh)
			{
				Result->SetNumberField(TEXT("boneCount"), SkeletalMesh->GetRefSkeleton().GetNum());
			}
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_mesh_collision"))
		{
			return SuccessJson(CollisionToJson(StaticMesh ? StaticMesh->GetBodySetup() : SkeletalMesh->GetBodySetup()));
		}

		if (Action == TEXT("read_import_sources"))
		{
			const UAssetImportData* ImportData = StaticMesh ? StaticMesh->GetAssetImportData() : SkeletalMesh->GetAssetImportData();
			const TArray<FString> Files = ImportData ? ImportData->ExtractFilenames() : TArray<FString>();
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& File : Files)
			{
				Values.Add(MakeShared<FJsonValueString>(File));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Values.Num());
			Result->SetArrayField(TEXT("sourceFiles"), Values);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_skeleton_bones"))
		{
			if (!SkeletalMesh)
			{
				return ErrorJson(TEXT("list_skeleton_bones 需要 SkeletalMesh。"));
			}
			const FReferenceSkeleton& Skeleton = SkeletalMesh->GetRefSkeleton();
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
			{
				TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
				Bone->SetNumberField(TEXT("index"), Index);
				Bone->SetStringField(TEXT("name"), Skeleton.GetBoneName(Index).ToString());
				Bone->SetNumberField(TEXT("parentIndex"), Skeleton.GetParentIndex(Index));
				Bones.Add(MakeShared<FJsonValueObject>(Bone));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Bones.Num());
			Result->SetArrayField(TEXT("bones"), Bones);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_sockets"))
		{
			TArray<TSharedPtr<FJsonValue>> Sockets;
			if (StaticMesh)
			{
				for (const UStaticMeshSocket* Socket : StaticMesh->Sockets)
				{
					if (Socket)
					{
						Sockets.Add(MakeShared<FJsonValueObject>(StaticSocketToJson(Socket)));
					}
				}
			}
			else
			{
				for (int32 Index = 0; Index < SkeletalMesh->NumSockets(); ++Index)
				{
					if (const USkeletalMeshSocket* Socket = SkeletalMesh->GetSocketByIndex(Index))
					{
						Sockets.Add(MakeShared<FJsonValueObject>(SkeletalSocketToJson(Socket)));
					}
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Sockets.Num());
			Result->SetArrayField(TEXT("sockets"), Sockets);
			return SuccessJson(Result);
		}

		const FString SocketNameText = GetStringArgument(Args, { TEXT("socketName"), TEXT("name") });
		const FName SocketName(*SocketNameText);
		if (Action == TEXT("add_socket"))
		{
			if (SocketName.IsNone())
			{
				return ErrorJson(TEXT("缺少 socketName。"));
			}
			Asset->Modify();
			if (StaticMesh)
			{
				if (StaticMesh->FindSocket(SocketName))
				{
					return ErrorJson(TEXT("Socket 已存在。"));
				}
				UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(StaticMesh);
				Socket->SocketName = SocketName;
				TryReadMeshVector(Args, TEXT("location"), Socket->RelativeLocation);
				TryReadMeshRotator(Args, TEXT("rotation"), Socket->RelativeRotation);
				TryReadMeshVector(Args, TEXT("scale"), Socket->RelativeScale);
				StaticMesh->AddSocket(Socket);
			}
			else
			{
				if (SkeletalMesh->FindSocket(SocketName))
				{
					return ErrorJson(TEXT("Socket 已存在。"));
				}
				USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(SkeletalMesh);
				Socket->SocketName = SocketName;
				Socket->BoneName = FName(*GetStringArgument(Args, { TEXT("boneName"), TEXT("parentBone") }));
				TryReadMeshVector(Args, TEXT("location"), Socket->RelativeLocation);
				TryReadMeshRotator(Args, TEXT("rotation"), Socket->RelativeRotation);
				TryReadMeshVector(Args, TEXT("scale"), Socket->RelativeScale);
				SkeletalMesh->AddSocket(Socket, false);
			}
			Asset->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("socketName"), SocketNameText);
			Result->SetBoolField(TEXT("created"), true);
			return SuccessJson(Result);
		}

		if (Action == TEXT("remove_socket"))
		{
			Asset->Modify();
			bool bRemoved = false;
			if (StaticMesh)
			{
				if (UStaticMeshSocket* Socket = StaticMesh->FindSocket(SocketName))
				{
					StaticMesh->RemoveSocket(Socket);
					bRemoved = true;
				}
			}
			else
			{
				if (USkeletalMeshSocket* Socket = SkeletalMesh->FindSocket(SocketName))
				{
					bRemoved = SkeletalMesh->GetMeshOnlySocketList().Remove(Socket) > 0;
					SkeletalMesh->RebuildSocketMap();
				}
			}
			Asset->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("socketName"), SocketNameText);
			Result->SetBoolField(TEXT("removed"), bRemoved);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_socket_transform"))
		{
			Asset->Modify();
			if (StaticMesh)
			{
				UStaticMeshSocket* Socket = StaticMesh->FindSocket(SocketName);
				if (!Socket)
				{
					return ErrorJson(TEXT("未找到 Socket。"));
				}
				TryReadMeshVector(Args, TEXT("location"), Socket->RelativeLocation);
				TryReadMeshRotator(Args, TEXT("rotation"), Socket->RelativeRotation);
				TryReadMeshVector(Args, TEXT("scale"), Socket->RelativeScale);
			}
			else
			{
				USkeletalMeshSocket* Socket = SkeletalMesh->FindSocket(SocketName);
				if (!Socket)
				{
					return ErrorJson(TEXT("未找到 Socket。"));
				}
				TryReadMeshVector(Args, TEXT("location"), Socket->RelativeLocation);
				TryReadMeshRotator(Args, TEXT("rotation"), Socket->RelativeRotation);
				TryReadMeshVector(Args, TEXT("scale"), Socket->RelativeScale);
			}
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("socketName"), SocketNameText);
			Result->SetBoolField(TEXT("changed"), true);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_mesh_material"))
		{
			double IndexNumber = 0.0;
			Args->TryGetNumberField(TEXT("materialIndex"), IndexNumber);
			const int32 Index = static_cast<int32>(IndexNumber);
			const FString MaterialPath = GetStringArgument(Args, { TEXT("materialPath"), TEXT("material") });
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material)
			{
				return ErrorJson(TEXT("无法加载 materialPath。"));
			}
			Asset->Modify();
			if (StaticMesh)
			{
				StaticMesh->SetMaterial(Index, Material);
			}
			else
			{
				TArray<FSkeletalMaterial> Materials = SkeletalMesh->GetMaterials();
				if (!Materials.IsValidIndex(Index))
				{
					Materials.SetNum(Index + 1);
				}
				Materials[Index].MaterialInterface = Material;
				SkeletalMesh->SetMaterials(Materials);
			}
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("materialIndex"), Index);
			Result->SetStringField(TEXT("materialPath"), MaterialPath);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_sk_material_slots"))
		{
			if (!SkeletalMesh)
			{
				return ErrorJson(TEXT("set_sk_material_slots 需要 SkeletalMesh。"));
			}
			const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
			if (!Args->TryGetArrayField(TEXT("slots"), Slots) || !Slots)
			{
				return ErrorJson(TEXT("缺少 slots 数组。"));
			}
			TArray<FSkeletalMaterial> Materials;
			for (const TSharedPtr<FJsonValue>& Value : *Slots)
			{
				const TSharedPtr<FJsonObject> Slot = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Slot.IsValid())
				{
					continue;
				}
				const FString MaterialPath = GetStringArgument(Slot, { TEXT("materialPath"), TEXT("material") });
				const FString SlotName = GetStringArgument(Slot, { TEXT("slotName"), TEXT("name") });
				Materials.Add(FSkeletalMaterial(LoadObject<UMaterialInterface>(nullptr, *MaterialPath), FName(*SlotName)));
			}
			SkeletalMesh->Modify();
			SkeletalMesh->SetMaterials(Materials);
			SkeletalMesh->PostEditChange();
			SkeletalMesh->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("materialSlotCount"), Materials.Num());
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_mesh_nav"))
		{
			if (!StaticMesh)
			{
				return ErrorJson(TEXT("set_mesh_nav 需要 StaticMesh。"));
			}
			bool bEnabled = true;
			Args->TryGetBoolField(TEXT("enabled"), bEnabled);
			StaticMesh->Modify();
			StaticMesh->bHasNavigationData = bEnabled;
			if (!bEnabled)
			{
				StaticMesh->MarkAsNotHavingNavigationData();
			}
			StaticMesh->PostEditChange();
			StaticMesh->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("enabled"), bEnabled);
			return SuccessJson(Result);
		}

		return ErrorJson(FString::Printf(TEXT("Mesh 动作未进入有效分支：%s"), *Action));
	}
}
