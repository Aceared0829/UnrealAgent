#pragma once

/**
 * @file UnrealAgentMCPActorSupport.h
 * @brief MCP Actor 工具共享的查找、类解析与 JSON 序列化服务。
 */

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class UClass;
class UWorld;

namespace UnrealAgentMCP::ActorSupport
{
	UWorld* GetEditorWorld();
	AActor* FindActorByNameOrLabel(UWorld* World, const FString& NameOrLabel);
	UClass* ResolveActorClass(const FString& ClassText);

	FString MobilityToString(EComponentMobility::Type Mobility);
	int32 CountActorComponents(AActor* Actor);
	TSharedPtr<FJsonObject> MakeActorObject(AActor* Actor);
}
