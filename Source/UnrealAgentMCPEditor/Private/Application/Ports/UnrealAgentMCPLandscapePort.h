// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPLandscapePort.h
 * @brief 地形查询、资产配置与笔刷编辑的稳定应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPLandscapePort
	{
	public:
		virtual ~IUnrealAgentMCPLandscapePort() = default;

		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListLayers(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Sample(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SampleBatch(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SampleGrid(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SamplePolyline(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Sculpt(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString PaintLayer(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListSplines(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetComponent(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetMaterial(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddLayerInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateLayerInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Create(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListProxies(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString FindProxyAt(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
