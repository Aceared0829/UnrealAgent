#pragma once

/**
 * @file UnrealAgentMCPAssetTestTypes.h
 * @brief 资产 Toolset 黑盒测试使用的最小可保存数据资产。
 */

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"

#include "UnrealAgentMCPAssetTestTypes.generated.h"

USTRUCT()
struct FUnrealAgentMCPAssetTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "测试")
	int32 Count = 0;

	UPROPERTY(EditAnywhere, Category = "测试")
	FString Label;
};

UCLASS()
class UUnrealAgentMCPAssetTestData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "测试")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, Category = "测试")
	FString Label;
};
