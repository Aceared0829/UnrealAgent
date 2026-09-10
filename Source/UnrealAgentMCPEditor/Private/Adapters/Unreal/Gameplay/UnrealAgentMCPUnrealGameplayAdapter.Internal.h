#pragma once

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.Internal.h
 * @brief Gameplay 各实现单元共享的参数、资产与结果辅助函数。
 */

#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class UObject;

namespace UnrealAgentMCP::GameplayPrivate
{
	FString Success(const TSharedRef<FJsonObject>& Result);
	FString Error(const FString& Message);
	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& Default = FString());
	bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool Default);
	double NumberArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, double Default);
	FVector VectorArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const FVector& Default = FVector::ZeroVector);
	AActor* FindActor(const FString& Label);
	FString MakeObjectPath(const TSharedPtr<FJsonObject>& Args, const FString& Prefix);
	UObject* CreateAsset(UClass* Class, const TSharedPtr<FJsonObject>& Args, const FString& Prefix, FString& OutPath);
	bool Save(UObject* Asset);
}
