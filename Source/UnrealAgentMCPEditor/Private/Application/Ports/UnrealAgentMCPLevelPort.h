// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPLevelPort.h
 * @brief Level 应用服务访问 Unreal 场景能力的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPLevelPort
	{
	public:
		virtual ~IUnrealAgentMCPLevelPort() = default;

		virtual FString ListActors(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetSelectedActors(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetActorDetails(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString PlaceActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DeleteActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DeleteActors(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString MoveActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SelectActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AttachActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DetachActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetActorProperty(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SaveLevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetEditorVisibility(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetActorsByClass(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CountActorsByClass(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetActorBounds(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetComponentTree(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetRelativeTransform(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ResolveActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetActorFolderPath(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddActorTag(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemoveActorTag(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetActorTags(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListActorTags(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetActorMobility(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AimActorAt(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString NavProjectPoint(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddComponent(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemoveComponent(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetComponentProperty(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetComponentDetails(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetActorsByComponentClass(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetSplineInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetSplinePoints(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetActorMaterial(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadActorMotion(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddHISMCInstances(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetInstanceTransforms(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemoveInstance(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString LineTrace(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SnapActorToFloor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString LoadLevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateLevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SpawnVolume(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListVolumes(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetVolumeProperties(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SpawnLight(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetLightProperties(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetFogProperties(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetRuntimeVirtualTextureSummary(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString BuildLighting(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetWorldSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetWorldSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetNaniteSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetNaniteInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddPostProcessBlendable(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ExportActorFbx(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListActorDescs(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString LoadActorDescs(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListStreamingSublevels(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddStreamingSublevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SpawnGrid(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString BatchTranslate(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString PlaceActorsBatch(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
