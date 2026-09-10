// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPCGAdapter.Component.cpp
 * @brief PCGComponent 生成、清理、图谱切换和 PCGVolume 创建实现。
 */

#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool ResolveActorComponent(const TSharedPtr<FJsonObject>& Args, FUnrealAgentMCPUnrealPCGAdapter* Adapter, FString& OutActorLabel, AActor*& OutActor,
			UPCGComponent*& OutComponent, FString& OutError)
		{
			if (!Args->TryGetStringField(TEXT("actorLabel"), OutActorLabel) || OutActorLabel.IsEmpty())
			{
				OutError = TEXT("缺少必填 actorLabel。");
				return false;
			}
			OutComponent = FUnrealAgentMCPUnrealPCGAdapter::ResolveComponent(OutActorLabel, &OutActor);
			if (!OutComponent || !OutActor)
			{
				OutError = FString::Printf(TEXT("Actor 没有 PCGComponent：%s"), *OutActorLabel);
				return false;
			}
			return true;
		}

		TSharedRef<FJsonObject> MakeOperationResult(const FString& ActorLabel, UPCGComponent* Component)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actorLabel"), ActorLabel);
			Result->SetStringField(TEXT("graphPath"), Component->GetGraph() ? Component->GetGraph()->GetPathName() : FString());
			Result->SetBoolField(TEXT("generated"), Component->bGenerated);
			Result->SetBoolField(TEXT("generating"), Component->IsGenerating());
			Result->SetBoolField(TEXT("cleaningUp"), Component->IsCleaningUp());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString ActorLabel;
		FString Error;
		AActor* Actor = nullptr;
		UPCGComponent* Component = nullptr;
		if (!ResolveActorComponent(Args, this, ActorLabel, Actor, Component, Error))
		{
			return ErrorJson(Error);
		}
		if (!Component->GetGraph())
			return ErrorJson(TEXT("PCGComponent 尚未绑定图谱。"));
		Component->Modify();
		Component->GenerateLocal(false);
		TSharedRef<FJsonObject> Result = MakeOperationResult(ActorLabel, Component);
		Result->SetNumberField(TEXT("taskId"), Component->GetGenerationTaskId());
		Result->SetBoolField(TEXT("force"), false);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ForceRegenerate(const TSharedPtr<FJsonObject>& Args)
	{
		FString ActorLabel;
		FString Error;
		AActor* Actor = nullptr;
		UPCGComponent* Component = nullptr;
		if (!ResolveActorComponent(Args, this, ActorLabel, Actor, Component, Error))
		{
			return ErrorJson(Error);
		}
		UPCGGraph* Graph = Component->GetGraph();
		if (!Graph)
			return ErrorJson(TEXT("PCGComponent 尚未绑定图谱。"));
		Component->Modify();
		Component->CleanupLocalImmediate(true, true);
		Component->SetGraphLocal(nullptr);
		Component->SetGraphLocal(Graph);
		Component->GenerateLocal(true);
		TSharedRef<FJsonObject> Result = MakeOperationResult(ActorLabel, Component);
		Result->SetNumberField(TEXT("taskId"), Component->GetGenerationTaskId());
		Result->SetBoolField(TEXT("force"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::Cleanup(const TSharedPtr<FJsonObject>& Args)
	{
		FString ActorLabel;
		FString Error;
		AActor* Actor = nullptr;
		UPCGComponent* Component = nullptr;
		if (!ResolveActorComponent(Args, this, ActorLabel, Actor, Component, Error))
		{
			return ErrorJson(Error);
		}
		bool bRemoveComponents = true;
		Args->TryGetBoolField(TEXT("removeComponents"), bRemoveComponents);
		Component->Modify();
		Component->CleanupLocalImmediate(bRemoveComponents, bRemoveComponents);
		TSharedRef<FJsonObject> Result = MakeOperationResult(ActorLabel, Component);
		Result->SetBoolField(TEXT("removeComponents"), bRemoveComponents);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::ToggleGraph(const TSharedPtr<FJsonObject>& Args)
	{
		FString ActorLabel;
		FString Error;
		AActor* Actor = nullptr;
		UPCGComponent* Component = nullptr;
		if (!ResolveActorComponent(Args, this, ActorLabel, Actor, Component, Error))
		{
			return ErrorJson(Error);
		}
		UPCGGraph* Graph = Component->GetGraph();
		FString GraphPath;
		if (Args->TryGetStringField(TEXT("graphPath"), GraphPath) && !GraphPath.IsEmpty())
		{
			Graph = LoadObject<UPCGGraph>(nullptr, *JsonConversion::NormalizeAssetObjectPath(GraphPath));
			if (!Graph)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 PCGGraph：%s"), *GraphPath));
			}
		}
		if (!Graph)
			return ErrorJson(TEXT("没有可重新绑定的 PCGGraph。"));
		Component->Modify();
		Component->SetGraphLocal(nullptr);
		Component->SetGraphLocal(Graph);
		Component->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeOperationResult(ActorLabel, Component);
		Result->SetBoolField(TEXT("toggled"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPCGAdapter::AddVolume(const TSharedPtr<FJsonObject>& Args)
	{
		FString GraphPath;
		if (!Args->TryGetStringField(TEXT("graphPath"), GraphPath) || GraphPath.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 graphPath。"));
		}
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *JsonConversion::NormalizeAssetObjectPath(GraphPath));
		if (!Graph)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 PCGGraph：%s"), *GraphPath));
		}
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));
		FVector Location = FVector::ZeroVector;
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		FVector Extent(500.0, 500.0, 500.0);
		JsonConversion::TryGetVectorField(Args, TEXT("extent"), Extent);
		Extent.X = FMath::Max(FMath::Abs(Extent.X), 1.0);
		Extent.Y = FMath::Max(FMath::Abs(Extent.Y), 1.0);
		Extent.Z = FMath::Max(FMath::Abs(Extent.Z), 1.0);

		APCGVolume* Volume = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Volume || !Volume->PCGComponent)
		{
			if (Volume)
				World->EditorDestroyActor(Volume, false);
			return ErrorJson(TEXT("PCGVolume 创建失败。"));
		}
		Volume->SetActorLabel(FString::Printf(TEXT("PCGVolume_%s"), *Graph->GetName()));
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Extent.X * 2.0;
		Builder->Y = Extent.Y * 2.0;
		Builder->Z = Extent.Z * 2.0;
		UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
		Volume->PCGComponent->SetGraphLocal(Graph);
		Volume->PCGComponent->bActivated = true;
		Volume->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Volume->GetActorLabel());
		Result->SetStringField(TEXT("actorName"), Volume->GetName());
		Result->SetStringField(TEXT("graphPath"), Graph->GetPathName());
		Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Location));
		Result->SetObjectField(TEXT("extent"), JsonConversion::MakeVectorObject(Extent));
		return SuccessJson(Result);
	}
}
