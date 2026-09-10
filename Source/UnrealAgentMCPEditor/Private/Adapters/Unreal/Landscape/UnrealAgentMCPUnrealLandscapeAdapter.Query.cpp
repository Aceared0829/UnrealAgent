// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.Query.cpp
 * @brief 地形组件、样条、材质使用和代理空间查询实现。
 */

#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeStreamingProxy.h"
#include "Materials/MaterialInterface.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> MakeBoundsJson(const FBox& Box)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetBoolField(TEXT("valid"), Box.IsValid != 0);
			if (Box.IsValid)
			{
				Json->SetObjectField(TEXT("min"), JsonConversion::MakeVectorObject(Box.Min));
				Json->SetObjectField(TEXT("max"), JsonConversion::MakeVectorObject(Box.Max));
			}
			return Json;
		}

		TSharedRef<FJsonObject> MakeProxySummary(ALandscapeProxy* Proxy)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Proxy->GetName());
			Json->SetStringField(TEXT("label"), Proxy->GetActorLabel());
			Json->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
			Json->SetStringField(TEXT("landscapeGuid"), Proxy->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			Json->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Proxy->GetActorLocation()));
			Json->SetNumberField(TEXT("componentCount"), Proxy->LandscapeComponents.Num());
			Json->SetObjectField(TEXT("bounds"), MakeBoundsJson(Proxy->GetComponentsBoundingBox(true)));
			return Json;
		}
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::GetComponent(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = ResolveProxy(Args, &Error);
		if (!Proxy)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		double IndexNumber = 0.0;
		Args->TryGetNumberField(TEXT("componentIndex"), IndexNumber);
		const int32 Index = static_cast<int32>(IndexNumber);
		if (!Proxy->LandscapeComponents.IsValidIndex(Index) || !Proxy->LandscapeComponents[Index])
		{
			return ErrorJson(FString::Printf(TEXT("无效 componentIndex：%d。"), Index));
		}
		ULandscapeComponent* Component = Proxy->LandscapeComponents[Index];
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("name"), Component->GetName());
		Result->SetObjectField(TEXT("sectionBase"),
			[Component]()
			{
				const FIntPoint Point = Component->GetSectionBase();
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetNumberField(TEXT("x"), Point.X);
				Json->SetNumberField(TEXT("y"), Point.Y);
				return Json;
			}());
		Result->SetNumberField(TEXT("componentSizeQuads"), Component->ComponentSizeQuads);
		Result->SetNumberField(TEXT("subsectionSizeQuads"), Component->SubsectionSizeQuads);
		Result->SetNumberField(TEXT("numSubsections"), Component->NumSubsections);
		Result->SetObjectField(TEXT("bounds"), MakeBoundsJson(Component->Bounds.GetBox()));
		Result->SetStringField(TEXT("materialPath"), Component->GetLandscapeMaterial() ? Component->GetLandscapeMaterial()->GetPathName() : FString());

		TArray<TSharedPtr<FJsonValue>> Allocations;
		for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("layer"), Allocation.LayerInfo ? Allocation.LayerInfo->GetLayerName().ToString() : FString());
			Json->SetNumberField(TEXT("textureIndex"), Allocation.WeightmapTextureIndex);
			Json->SetNumberField(TEXT("channel"), Allocation.WeightmapTextureChannel);
			Allocations.Add(MakeShared<FJsonValueObject>(Json));
		}
		Result->SetArrayField(TEXT("weightmapAllocations"), Allocations);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::ListSplines(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = ResolveProxy(Args, &Error);
		if (!Proxy)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		ULandscapeSplinesComponent* Splines = Proxy->GetSplinesComponent();
		TArray<TSharedPtr<FJsonValue>> ControlPoints;
		TArray<TSharedPtr<FJsonValue>> Segments;
		if (Splines)
		{
			const FTransform ToWorld = Splines->GetComponentTransform();
			for (int32 Index = 0; Index < Splines->GetControlPoints().Num(); ++Index)
			{
				ULandscapeSplineControlPoint* Point = Splines->GetControlPoints()[Index];
				if (!Point)
					continue;
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetNumberField(TEXT("index"), Index);
				Json->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(ToWorld.TransformPosition(Point->Location)));
				Json->SetNumberField(TEXT("width"), Point->Width);
				Json->SetNumberField(TEXT("sideFalloff"), Point->SideFalloff);
				Json->SetNumberField(TEXT("connectedSegmentCount"), Point->ConnectedSegments.Num());
				ControlPoints.Add(MakeShared<FJsonValueObject>(Json));
			}
			for (int32 Index = 0; Index < Splines->GetSegments().Num(); ++Index)
			{
				ULandscapeSplineSegment* Segment = Splines->GetSegments()[Index];
				if (!Segment)
					continue;
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetNumberField(TEXT("index"), Index);
				Json->SetNumberField(TEXT("interpolationPointCount"), Segment->GetPoints().Num());
				for (int32 End = 0; End < 2; ++End)
				{
					const ULandscapeSplineControlPoint* Point = Segment->Connections[End].ControlPoint;
					if (Point)
					{
						Json->SetObjectField(End == 0 ? TEXT("start") : TEXT("end"), JsonConversion::MakeVectorObject(ToWorld.TransformPosition(Point->Location)));
					}
				}
				Segments.Add(MakeShared<FJsonValueObject>(Json));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("hasSplineComponent"), Splines != nullptr);
		Result->SetNumberField(TEXT("controlPointCount"), ControlPoints.Num());
		Result->SetArrayField(TEXT("controlPoints"), ControlPoints);
		Result->SetNumberField(TEXT("segmentCount"), Segments.Num());
		Result->SetArrayField(TEXT("segments"), Segments);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));
		TMap<UMaterialInterface*, int32> MaterialUsage;
		int32 ProxyCount = 0;
		int32 ComponentCount = 0;
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			++ProxyCount;
			ComponentCount += It->LandscapeComponents.Num();
			TSet<UMaterialInterface*> Materials;
			It->RetrieveAllLandscapeMaterials(Materials);
			for (UMaterialInterface* Material : Materials)
			{
				if (Material)
					MaterialUsage.FindOrAdd(Material) += It->LandscapeComponents.Num();
			}
		}
		TArray<TSharedPtr<FJsonValue>> Materials;
		for (const TPair<UMaterialInterface*, int32>& Pair : MaterialUsage)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Pair.Key->GetName());
			Json->SetStringField(TEXT("path"), Pair.Key->GetPathName());
			Json->SetNumberField(TEXT("componentReferences"), Pair.Value);
			Materials.Add(MakeShared<FJsonValueObject>(Json));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("proxyCount"), ProxyCount);
		Result->SetNumberField(TEXT("componentCount"), ComponentCount);
		Result->SetNumberField(TEXT("materialCount"), Materials.Num());
		Result->SetArrayField(TEXT("materials"), Materials);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::ListProxies(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));
		TArray<TSharedPtr<FJsonValue>> Proxies;
		for (TActorIterator<ALandscapeStreamingProxy> It(World); It; ++It)
		{
			Proxies.Add(MakeShared<FJsonValueObject>(MakeProxySummary(*It)));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Proxies.Num());
		Result->SetArrayField(TEXT("proxies"), Proxies);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::FindProxyAt(const TSharedPtr<FJsonObject>& Args)
	{
		double X = 0.0;
		double Y = 0.0;
		if (!Args->TryGetNumberField(TEXT("worldX"), X) || !Args->TryGetNumberField(TEXT("worldY"), Y))
		{
			return ErrorJson(TEXT("缺少有效 worldX、worldY。"));
		}
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));
		for (TActorIterator<ALandscapeStreamingProxy> It(World); It; ++It)
		{
			const FBox Bounds = It->GetComponentsBoundingBox(true);
			if (Bounds.IsValid && X >= Bounds.Min.X && X <= Bounds.Max.X && Y >= Bounds.Min.Y && Y <= Bounds.Max.Y)
			{
				TSharedRef<FJsonObject> Result = MakeProxySummary(*It);
				Result->SetBoolField(TEXT("found"), true);
				return SuccessJson(Result);
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("found"), false);
		Result->SetNumberField(TEXT("worldX"), X);
		Result->SetNumberField(TEXT("worldY"), Y);
		return SuccessJson(Result);
	}
}
