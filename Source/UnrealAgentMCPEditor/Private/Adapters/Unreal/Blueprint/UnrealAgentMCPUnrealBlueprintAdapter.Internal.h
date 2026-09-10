// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Internal.h
 * @brief Blueprint 各实现单元共享的内部解析与序列化函数。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UActorComponent;

namespace UnrealAgentMCP::BlueprintPrivate
{
	TSharedRef<FJsonObject> SuccessObject();
	FString Serialize(const TSharedRef<FJsonObject>& Object);
	FString Failure(const FString& Error);
	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& DefaultValue = FString());
	bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool DefaultValue);
	double NumberArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, double DefaultValue);
	UBlueprint* LoadBlueprint(const TSharedPtr<FJsonObject>& Args, FString& OutError);
	UClass* ResolveClass(const FString& Name, UClass* RequiredBase = nullptr);
	UEdGraph* ResolveGraph(UBlueprint* Blueprint, const FString& Name);
	UEdGraphNode* ResolveNode(UEdGraph* Graph, const FString& IdOrName);
	UActorComponent* ResolveComponent(UBlueprint* Blueprint, const FString& Name);
	bool SaveBlueprint(UBlueprint* Blueprint, bool bStructural = false);
	bool SetProperty(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	TSharedPtr<FJsonValue> ReadProperty(UObject* Object, const FString& PropertyName);
	TSharedRef<FJsonObject> NodeJson(const UEdGraphNode* Node, bool bIncludePins, bool bIncludeDefaults, bool bIncludeComments);
}
