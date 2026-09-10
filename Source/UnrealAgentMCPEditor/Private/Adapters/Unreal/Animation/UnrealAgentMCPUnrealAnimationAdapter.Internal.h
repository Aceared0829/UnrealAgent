// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Internal.h
 * @brief 动画适配器内部公共契约。
 */

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UObject;
class UClass;

namespace UnrealAgentMCP::AnimationPrivate
{
	FString Success(const TSharedRef<FJsonObject>& Result);
	FString Error(const FString& Message);
	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& Default = FString());
	bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool Default);
	double NumberArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, double Default);
	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass = nullptr);
	UClass* FindClass(const FString& NameOrPath);
	UObject* CreateAsset(UClass* Class, const TSharedPtr<FJsonObject>& Args, const FString& Prefix, FString& OutPath);
	bool Save(UObject* Asset);
	bool SetProperty(UObject* Object, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	TSharedRef<FJsonObject> DescribeObject(UObject* Object);
}
