// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Internal.h
 * @brief Audio 各实现单元共享的内部辅助函数。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UFactory;
class USoundBase;
class UMetaSoundBuilderBase;

namespace UnrealAgentMCP::AudioPrivate
{
	TSharedRef<FJsonObject> SuccessObject();
	FString Serialize(const TSharedRef<FJsonObject>& Object);
	FString Failure(const FString& Error);
	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue);
	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue = FString());
	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, int32 DefaultValue = 0);
	double OptionalNumber(const TSharedPtr<FJsonObject>& Args, const FString& Name, double DefaultValue = 0.0);
	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, bool DefaultValue = false);
	FVector OptionalVector(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FVector& DefaultValue = FVector::ZeroVector);

	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass);
	UObject* CreateAsset(const FString& Name, const FString& PackagePath, UClass* AssetClass, UFactory* Factory, const FString& OnConflict, bool& bOutCreated, FString& OutError);
	bool SaveAsset(UObject* Asset);
	bool SetReflectedProperty(void* Container, UStruct* Struct, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	USoundBase* RequireSound(const TSharedPtr<FJsonObject>& Args, FString& OutError);
	UMetaSoundBuilderBase* RequireMetaSoundBuilder(const TSharedPtr<FJsonObject>& Args, FString& OutError);
}
