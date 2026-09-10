// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.h
 * @brief 通过 Unreal Landscape 公共 API 实现完整地形操作。
 */

#include "Application/Ports/UnrealAgentMCPLandscapePort.h"

class ALandscape;
class ALandscapeProxy;
class ULandscapeInfo;
class ULandscapeLayerInfoObject;

namespace UnrealAgentMCP
{
	class FLandscapeSculptBatchStepper;
	class FLandscapeSamplingTaskStepper;
	class FLandscapePaintLayerStepper;

	class FUnrealAgentMCPUnrealLandscapeAdapter final : public IUnrealAgentMCPLandscapePort
	{
	public:
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListLayers(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Sample(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SampleBatch(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SampleGrid(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SamplePolyline(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Sculpt(const TSharedPtr<FJsonObject>& Args) override;
		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString PaintLayer(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListSplines(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetComponent(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetMaterial(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddLayerInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateLayerInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Create(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListProxies(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString FindProxyAt(const TSharedPtr<FJsonObject>& Args) override;

	private:
		friend class FLandscapeSculptBatchStepper;
		friend class FLandscapeHeightWriteStepper;
		friend class FLandscapeSamplingTaskStepper;
		friend class FLandscapePaintLayerStepper;

		static TSharedPtr<Execution::IMcpTaskStepper> CreateSculptBatchStepper(const TSharedPtr<FJsonObject>& Args);
		static TSharedPtr<Execution::IMcpTaskStepper> CreateHeightWriteStepper(const TSharedPtr<FJsonObject>& Args);
		static TSharedPtr<Execution::IMcpTaskStepper> CreateSamplingTaskStepper(const TSharedPtr<FJsonObject>& Args);
		static TSharedPtr<Execution::IMcpTaskStepper> CreatePaintLayerTaskStepper(const TSharedPtr<FJsonObject>& Args);

		static ALandscapeProxy* ResolveProxy(const TSharedPtr<FJsonObject>& Args, FString* OutError = nullptr);
		static ALandscape* ResolveLandscape(const TSharedPtr<FJsonObject>& Args, FString* OutError = nullptr);
		static ULandscapeInfo* ResolveInfo(const TSharedPtr<FJsonObject>& Args, ALandscapeProxy** OutProxy = nullptr, FString* OutError = nullptr);
		static ULandscapeLayerInfoObject* ResolveLayerInfo(ALandscapeProxy* Proxy, const FString& LayerName);
		static FString SamplePoints(const TSharedPtr<FJsonObject>& Args, const TArray<FVector2D>& Points, const FString& Shape);
	};
}
