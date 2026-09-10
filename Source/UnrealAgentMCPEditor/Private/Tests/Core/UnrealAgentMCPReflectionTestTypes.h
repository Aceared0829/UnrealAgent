// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "UnrealAgentMCPReflectionTestTypes.generated.h"

UENUM()
enum class EUnrealAgentMCPReflectionMode : uint8
{
	Disabled,
	Enabled
};

USTRUCT()
struct FUnrealAgentMCPReflectionValueMatrix
{
	GENERATED_BODY()

	UPROPERTY(meta = (ClampMin = "0", ClampMax = "10"))
	int32 Bounded = 5;

	UPROPERTY(meta = (MCPMinItems = "1", MCPMaxItems = "4"))
	TArray<int32> Values = { 7, 8 };

	UPROPERTY()
	TSet<int32> UniqueValues;

	UPROPERTY()
	TMap<FString, int32> Scores;

	UPROPERTY()
	EUnrealAgentMCPReflectionMode Mode = EUnrealAgentMCPReflectionMode::Disabled;

	UPROPERTY()
	FGuid Identifier;

	UPROPERTY(meta = (MCPMinLength = "2", MCPMaxLength = "8", MCPPattern = "^[A-Z]+$"))
	FString Code = TEXT("UE");

	UPROPERTY()
	TObjectPtr<UObject> ObjectReference;

	UPROPERTY()
	TSoftObjectPtr<UObject> SoftReference;

	UPROPERTY()
	TWeakObjectPtr<UObject> WeakReference;

	UPROPERTY()
	TSubclassOf<UObject> ClassReference;
};

UCLASS()
class UUnrealAgentMCPReflectionPropertyHolder : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Unreal Agent Tests", meta = (ClampMin = "0", ClampMax = "10"))
	int32 Count = 5;

	UPROPERTY(EditAnywhere, Category = "Unreal Agent Tests")
	FVector Offset = FVector(1.0, 2.0, 3.0);

	UPROPERTY(EditAnywhere, Category = "Unreal Agent Tests")
	TArray<int32> Values = { 7, 8 };

	UPROPERTY(VisibleAnywhere, Category = "Unreal Agent Tests")
	int32 ReadOnlyValue = 11;
};

USTRUCT()
struct FUnrealAgentMCPReflectionPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	TArray<int32> Values;
};

UCLASS(meta = (UnrealAgentMCPToolset = "Tests.Math", UnrealAgentMCPInternal))
class UUnrealAgentMCPReflectionTestToolset : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (UnrealAgentMCPTool, UnrealAgentMCPToolName = "add", UnrealAgentMCPToolDescription = "将两个整数相加。", UnrealAgentMCPRisk = "ReadOnly",
				  UnrealAgentMCPTransaction = "ReadOnly", UnrealAgentMCPIdempotent = "true", UnrealAgentMCPDefault_B = "3"))
	int32 Add(int32 A, int32 B = 3) const;

	UFUNCTION(meta = (UnrealAgentMCPTool, UnrealAgentMCPToolName = "echo_payload", UnrealAgentMCPToolDescription = "原样返回结构化载荷。", UnrealAgentMCPRisk = "ReadOnly",
				  UnrealAgentMCPTransaction = "ReadOnly", UnrealAgentMCPIdempotent = "true"))
	FUnrealAgentMCPReflectionPayload EchoPayload(FUnrealAgentMCPReflectionPayload Payload) const;
};

UCLASS(meta = (UnrealAgentMCPToolset = "Tests.Unsafe", UnrealAgentMCPInternal))
class UUnrealAgentMCPUnsafeReflectionTestToolset : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (UnrealAgentMCPTool, UnrealAgentMCPToolName = "missing_policy"))
	FString ReadWithoutPolicy() const
	{
		return TEXT("unsafe");
	}
};
