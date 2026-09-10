// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Internal.h
 * @brief Niagara 各实现单元共享的内部数据结构与辅助函数。
 */

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class FJsonObject;
class FJsonValue;
class UEdGraphPin;
class UNiagaraComponent;
class UNiagaraEmitter;
class UNiagaraGraph;
class UNiagaraScript;
class UNiagaraSystem;
struct FVersionedNiagaraEmitterData;

namespace UnrealAgentMCP::NiagaraPrivate
{
	struct FResolvedEmitter
	{
		UNiagaraEmitter* Emitter = nullptr;
		FVersionedNiagaraEmitterData* Data = nullptr;
		FGuid Version;
		int32 HandleIndex = INDEX_NONE;
	};

	struct FScriptSlot
	{
		FString Context;
		UNiagaraScript* Script = nullptr;
	};

	TSharedRef<FJsonObject> SuccessObject();
	FString Serialize(const TSharedRef<FJsonObject>& Object);
	FString Failure(const FString& Error);
	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue);
	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue = FString());
	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, int32 DefaultValue = 0);
	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, bool DefaultValue = false);

	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass);
	UObject* CreateAsset(const FString& Name, const FString& PackagePath, UClass* AssetClass, const TCHAR* FactoryClassPath, const FString& OnConflict, bool& bOutCreated,
		FString& OutError);
	bool SaveAsset(UObject* Asset);
	bool SetReflectedProperty(void* Container, UStruct* Struct, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	FResolvedEmitter ResolveEmitter(UNiagaraSystem* System, const FString& EmitterName, int32 EmitterIndex);
	void CollectEmitterScripts(FVersionedNiagaraEmitterData* Data, const FString& StackContext, TArray<FScriptSlot>& OutScripts);
	UNiagaraGraph* GraphOfScript(UNiagaraScript* Script);
	TSharedRef<FJsonObject> PinToJson(const UEdGraphPin* Pin);
	UNiagaraComponent* FindComponent(const FString& ActorLabel);
}
