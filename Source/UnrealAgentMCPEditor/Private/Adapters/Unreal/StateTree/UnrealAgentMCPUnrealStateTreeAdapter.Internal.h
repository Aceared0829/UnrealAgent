// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Internal.h
 * @brief StateTree 适配器内部公共契约。
 */

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "StateTreeEditorNode.h"

class UStateTree;
class UStateTreeEditorData;
class UStateTreeState;
struct FInstancedPropertyBag;

namespace UnrealAgentMCP::StateTreePrivate
{
	TSharedRef<FJsonObject> SuccessObject();
	FString Serialize(const TSharedRef<FJsonObject>& Object);
	FString Failure(const FString& Error);
	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue);
	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue = FString());
	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, int32 DefaultValue = INDEX_NONE);
	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, bool DefaultValue);
	UStateTree* LoadStateTree(const FString& AssetPath);
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree);
	UStateTreeState* ResolveState(UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Args);
	FGuid ParseGuid(const FString& Text);
	FString GuidString(const FGuid& Guid);
	bool SaveStateTree(UStateTree* StateTree);
	void MarkModified(UStateTree* StateTree, UObject* Object);
	UScriptStruct* ResolveNodeStruct(const FString& StructType, const UScriptStruct* BaseStruct);
	bool AddNode(TArray<FStateTreeEditorNode>& Nodes, UObject* Outer, const FString& StructType, const UScriptStruct* BaseStruct, FStateTreeEditorNode*& OutNode,
		FString& OutError);
	bool SetNodeProperty(FStateTreeEditorNode& Node, bool bInstance, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	TSharedRef<FJsonObject> SerializeNode(const FStateTreeEditorNode& Node);
	TSharedRef<FJsonObject> SerializeState(const UStateTreeState* State);
	TArray<TSharedPtr<FJsonValue>> SerializePropertyBag(const FInstancedPropertyBag& Bag);
	bool AddBagProperty(FInstancedPropertyBag& Bag, const FString& Name, const FString& Type, FString& OutError);
	bool SetBagValue(FInstancedPropertyBag& Bag, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError);
}
