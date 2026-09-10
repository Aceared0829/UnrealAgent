// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.h
 * @brief 使用 Unreal Editor 公共 API 实现 Level 应用端口。
 */

#include "Application/Ports/UnrealAgentMCPLevelPort.h"
#include "WorldPartition/WorldPartitionHandle.h"

class UWorldPartition;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealLevelAdapter final : public IUnrealAgentMCPLevelPort
	{
	public:
		FString ListActors(const TSharedPtr<FJsonObject>& Args) override;
		FString GetSelectedActors(const TSharedPtr<FJsonObject>& Args) override;
		FString GetActorDetails(const TSharedPtr<FJsonObject>& Args) override;
		FString PlaceActor(const TSharedPtr<FJsonObject>& Args) override;
		FString DeleteActor(const TSharedPtr<FJsonObject>& Args) override;
		FString DeleteActors(const TSharedPtr<FJsonObject>& Args) override;
		TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) override;
		FString MoveActor(const TSharedPtr<FJsonObject>& Args) override;
		FString SelectActor(const TSharedPtr<FJsonObject>& Args) override;
		FString AttachActor(const TSharedPtr<FJsonObject>& Args) override;
		FString DetachActor(const TSharedPtr<FJsonObject>& Args) override;
		FString SetActorProperty(const TSharedPtr<FJsonObject>& Args) override;
		FString SaveLevel(const TSharedPtr<FJsonObject>& Args) override;
		FString SetEditorVisibility(const TSharedPtr<FJsonObject>& Args) override;
		FString GetActorsByClass(const TSharedPtr<FJsonObject>& Args) override;
		FString CountActorsByClass(const TSharedPtr<FJsonObject>& Args) override;
		FString GetActorBounds(const TSharedPtr<FJsonObject>& Args) override;
		FString GetComponentTree(const TSharedPtr<FJsonObject>& Args) override;
		FString GetRelativeTransform(const TSharedPtr<FJsonObject>& Args) override;
		FString ResolveActor(const TSharedPtr<FJsonObject>& Args) override;
		FString SetActorFolderPath(const TSharedPtr<FJsonObject>& Args) override;
		FString AddActorTag(const TSharedPtr<FJsonObject>& Args) override;
		FString RemoveActorTag(const TSharedPtr<FJsonObject>& Args) override;
		FString SetActorTags(const TSharedPtr<FJsonObject>& Args) override;
		FString ListActorTags(const TSharedPtr<FJsonObject>& Args) override;
		FString SetActorMobility(const TSharedPtr<FJsonObject>& Args) override;
		FString AimActorAt(const TSharedPtr<FJsonObject>& Args) override;
		FString NavProjectPoint(const TSharedPtr<FJsonObject>& Args) override;
		FString AddComponent(const TSharedPtr<FJsonObject>& Args) override;
		FString RemoveComponent(const TSharedPtr<FJsonObject>& Args) override;
		FString SetComponentProperty(const TSharedPtr<FJsonObject>& Args) override;
		FString GetComponentDetails(const TSharedPtr<FJsonObject>& Args) override;
		FString GetActorsByComponentClass(const TSharedPtr<FJsonObject>& Args) override;
		FString GetSplineInfo(const TSharedPtr<FJsonObject>& Args) override;
		FString SetSplinePoints(const TSharedPtr<FJsonObject>& Args) override;
		FString SetActorMaterial(const TSharedPtr<FJsonObject>& Args) override;
		FString ReadActorMotion(const TSharedPtr<FJsonObject>& Args) override;
		FString AddHISMCInstances(const TSharedPtr<FJsonObject>& Args) override;
		FString GetInstanceTransforms(const TSharedPtr<FJsonObject>& Args) override;
		FString UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Args) override;
		FString RemoveInstance(const TSharedPtr<FJsonObject>& Args) override;
		FString LineTrace(const TSharedPtr<FJsonObject>& Args) override;
		FString SnapActorToFloor(const TSharedPtr<FJsonObject>& Args) override;
		FString LoadLevel(const TSharedPtr<FJsonObject>& Args) override;
		FString CreateLevel(const TSharedPtr<FJsonObject>& Args) override;
		FString SpawnVolume(const TSharedPtr<FJsonObject>& Args) override;
		FString ListVolumes(const TSharedPtr<FJsonObject>& Args) override;
		FString SetVolumeProperties(const TSharedPtr<FJsonObject>& Args) override;
		FString SpawnLight(const TSharedPtr<FJsonObject>& Args) override;
		FString SetLightProperties(const TSharedPtr<FJsonObject>& Args) override;
		FString SetFogProperties(const TSharedPtr<FJsonObject>& Args) override;
		FString GetRuntimeVirtualTextureSummary(const TSharedPtr<FJsonObject>& Args) override;
		FString SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Args) override;
		FString BuildLighting(const TSharedPtr<FJsonObject>& Args) override;
		FString GetWorldSettings(const TSharedPtr<FJsonObject>& Args) override;
		FString SetWorldSettings(const TSharedPtr<FJsonObject>& Args) override;
		FString SetNaniteSettings(const TSharedPtr<FJsonObject>& Args) override;
		FString GetNaniteInfo(const TSharedPtr<FJsonObject>& Args) override;
		FString AddPostProcessBlendable(const TSharedPtr<FJsonObject>& Args) override;
		FString ExportActorFbx(const TSharedPtr<FJsonObject>& Args) override;
		FString SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Args) override;
		FString ListActorDescs(const TSharedPtr<FJsonObject>& Args) override;
		FString LoadActorDescs(const TSharedPtr<FJsonObject>& Args) override;
		FString GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args) override;
		FString SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Args) override;
		FString ListStreamingSublevels(const TSharedPtr<FJsonObject>& Args) override;
		FString AddStreamingSublevel(const TSharedPtr<FJsonObject>& Args) override;
		FString RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Args) override;
		FString SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Args) override;
		FString SpawnGrid(const TSharedPtr<FJsonObject>& Args) override;
		FString BatchTranslate(const TSharedPtr<FJsonObject>& Args) override;
		FString PlaceActorsBatch(const TSharedPtr<FJsonObject>& Args) override;

	private:
		static TSharedPtr<Execution::IMcpTaskStepper> CreateBatchTaskStepper(const TSharedPtr<FJsonObject>& Args);

		/** 持有显式固定的 World Partition Actor，避免一次调用结束后立即被卸载。 */
		TMap<FGuid, TUniquePtr<FWorldPartitionReference>> PinnedActorDescs;

		/** 固定句柄所属的分区；切换世界时必须先释放旧分区句柄。 */
		TWeakObjectPtr<UWorldPartition> PinnedWorldPartition;
	};
}
