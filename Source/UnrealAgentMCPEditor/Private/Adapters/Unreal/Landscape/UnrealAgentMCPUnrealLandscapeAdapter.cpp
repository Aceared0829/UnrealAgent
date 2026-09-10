// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.cpp
 * @brief 地形对象解析、基础信息、图层和空间采样实现。
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
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"

#include "PhysicalMaterials/PhysicalMaterial.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool MatchesLandscape(const ALandscapeProxy* Proxy, const FString& Name)
		{
			return Proxy && (Name.IsEmpty() || Proxy->GetName().Equals(Name, ESearchCase::IgnoreCase) || Proxy->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase));
		}

		struct FLandscapeCandidate
		{
			FGuid Guid;
			ALandscapeProxy* Representative = nullptr;
			int32 LoadedProxyCount = 0;
			bool bNameMatched = false;
		};

		FString DescribeLandscapeCandidates(const TArray<FLandscapeCandidate>& Candidates)
		{
			TArray<FString> Summaries;
			Summaries.Reserve(Candidates.Num());
			for (const FLandscapeCandidate& Candidate : Candidates)
			{
				const ALandscapeProxy* Proxy = Candidate.Representative;
				Summaries.Add(FString::Printf(TEXT("label=%s,name=%s,landscapeGuid=%s,loadedProxies=%d"), Proxy ? *Proxy->GetActorLabel() : TEXT("<unknown>"),
					Proxy ? *Proxy->GetName() : TEXT("<unknown>"), *Candidate.Guid.ToString(EGuidFormats::DigitsWithHyphensLower), Candidate.LoadedProxyCount));
			}
			return FString::Join(Summaries, TEXT("; "));
		}

		TSharedRef<FJsonObject> MakeIntPointJson(const int32 X, const int32 Y)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetNumberField(TEXT("x"), X);
			Json->SetNumberField(TEXT("y"), Y);
			return Json;
		}

		TSharedRef<FJsonObject> MakeBoxJson(const FBox& Box)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetBoolField(TEXT("valid"), Box.IsValid != 0);
			if (Box.IsValid)
			{
				Json->SetObjectField(TEXT("min"), JsonConversion::MakeVectorObject(Box.Min));
				Json->SetObjectField(TEXT("max"), JsonConversion::MakeVectorObject(Box.Max));
				Json->SetObjectField(TEXT("center"), JsonConversion::MakeVectorObject(Box.GetCenter()));
				Json->SetObjectField(TEXT("extent"), JsonConversion::MakeVectorObject(Box.GetExtent()));
			}
			return Json;
		}

		TSharedRef<FJsonObject> MakeProxyJson(ALandscapeProxy* Proxy)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Proxy->GetName());
			Json->SetStringField(TEXT("label"), Proxy->GetActorLabel());
			Json->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
			Json->SetStringField(TEXT("landscapeGuid"), Proxy->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			Json->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Proxy->GetActorLocation()));
			Json->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Proxy->GetActorScale3D()));
			Json->SetNumberField(TEXT("componentCount"), Proxy->LandscapeComponents.Num());
			Json->SetNumberField(TEXT("componentSizeQuads"), Proxy->ComponentSizeQuads);
			Json->SetNumberField(TEXT("subsectionSizeQuads"), Proxy->SubsectionSizeQuads);
			Json->SetNumberField(TEXT("numSubsections"), Proxy->NumSubsections);
			Json->SetObjectField(TEXT("bounds"), MakeBoxJson(Proxy->GetComponentsBoundingBox(true)));
			return Json;
		}
	}

	ALandscapeProxy* FUnrealAgentMCPUnrealLandscapeAdapter::ResolveProxy(const TSharedPtr<FJsonObject>& Args, FString* OutError)
	{
		if (OutError)
			OutError->Reset();
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			if (OutError)
				*OutError = TEXT("当前编辑器世界不可用。");
			return nullptr;
		}
		FString Name;
		FString GuidText;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("actorLabel"), Name);
			if (Name.IsEmpty())
				Args->TryGetStringField(TEXT("name"), Name);
			Args->TryGetStringField(TEXT("landscapeGuid"), GuidText);
		}
		FGuid RequestedGuid;
		if (!GuidText.IsEmpty() && (!FGuid::Parse(GuidText, RequestedGuid) || !RequestedGuid.IsValid()))
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("landscapeGuid 不是有效 Guid：%s。"), *GuidText);
			}
			return nullptr;
		}

		TArray<FLandscapeCandidate> AllCandidates;
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Proxy = *It;
			const FGuid Guid = Proxy->GetLandscapeGuid();
			FLandscapeCandidate* Candidate = AllCandidates.FindByPredicate(
				[&Guid](const FLandscapeCandidate& Existing)
				{
					return Existing.Guid == Guid;
				});
			if (!Candidate)
			{
				Candidate = &AllCandidates.AddDefaulted_GetRef();
				Candidate->Guid = Guid;
				Candidate->Representative = Proxy;
			}
			else if (!Cast<ALandscape>(Candidate->Representative) && Cast<ALandscape>(Proxy))
			{
				Candidate->Representative = Proxy;
			}
			++Candidate->LoadedProxyCount;
			Candidate->bNameMatched |= MatchesLandscape(Proxy, Name);
		}

		TArray<FLandscapeCandidate> Matches;
		for (const FLandscapeCandidate& Candidate : AllCandidates)
		{
			if (RequestedGuid.IsValid() && Candidate.Guid != RequestedGuid)
			{
				continue;
			}
			if (!Name.IsEmpty() && !Candidate.bNameMatched)
				continue;
			Matches.Add(Candidate);
		}
		if (Matches.Num() == 1)
			return Matches[0].Representative;

		if (OutError)
		{
			if (Matches.IsEmpty())
			{
				*OutError = RequestedGuid.IsValid() || !Name.IsEmpty() ? FString::Printf(TEXT("未找到匹配的 Landscape。landscapeGuid=%s, name=%s。"),
																			 GuidText.IsEmpty() ? TEXT("<none>") : *GuidText, Name.IsEmpty() ? TEXT("<none>") : *Name)
																	   : TEXT("当前世界中未找到 Landscape。");
			}
			else
			{
				*OutError = FString::Printf(TEXT("存在多个 Landscape，必须传入 landscapeGuid 或唯一 actorLabel/name。候选：%s。"), *DescribeLandscapeCandidates(Matches));
			}
		}
		return nullptr;
	}

	ALandscape* FUnrealAgentMCPUnrealLandscapeAdapter::ResolveLandscape(const TSharedPtr<FJsonObject>& Args, FString* OutError)
	{
		ALandscapeProxy* Proxy = ResolveProxy(Args, OutError);
		return Proxy ? Proxy->GetLandscapeActor() : nullptr;
	}

	ULandscapeInfo* FUnrealAgentMCPUnrealLandscapeAdapter::ResolveInfo(const TSharedPtr<FJsonObject>& Args, ALandscapeProxy** OutProxy, FString* OutError)
	{
		ALandscapeProxy* Proxy = ResolveProxy(Args, OutError);
		if (OutProxy)
			*OutProxy = Proxy;
		return Proxy ? Proxy->GetLandscapeInfo() : nullptr;
	}

	ULandscapeLayerInfoObject* FUnrealAgentMCPUnrealLandscapeAdapter::ResolveLayerInfo(ALandscapeProxy* Proxy, const FString& LayerName)
	{
		if (!Proxy || LayerName.IsEmpty())
			return nullptr;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Proxy->GetTargetLayers())
		{
			ULandscapeLayerInfoObject* Layer = Pair.Value.LayerInfoObj;
			if (Pair.Key.ToString().Equals(LayerName, ESearchCase::IgnoreCase) ||
				(Layer &&
					(Layer->GetName().Equals(LayerName, ESearchCase::IgnoreCase) || Layer->GetPathName().Equals(LayerName, ESearchCase::IgnoreCase) ||
						Layer->GetLayerName().ToString().Equals(LayerName, ESearchCase::IgnoreCase))))
			{
				return Layer;
			}
		}
		return nullptr;
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::GetInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = nullptr;
		ULandscapeInfo* Info = ResolveInfo(Args, &Proxy, &Error);
		if (!Proxy || !Info)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);

		TSharedRef<FJsonObject> Result = MakeProxyJson(Proxy);
		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
		const bool bHasExtent = Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
		Result->SetBoolField(TEXT("hasExtent"), bHasExtent);
		if (bHasExtent)
		{
			TSharedRef<FJsonObject> Extent = MakeShared<FJsonObject>();
			Extent->SetObjectField(TEXT("min"), MakeIntPointJson(MinX, MinY));
			Extent->SetObjectField(TEXT("max"), MakeIntPointJson(MaxX, MaxY));
			Extent->SetNumberField(TEXT("vertexCountX"), MaxX - MinX + 1);
			Extent->SetNumberField(TEXT("vertexCountY"), MaxY - MinY + 1);
			Result->SetObjectField(TEXT("extent"), Extent);
		}
		if (ALandscape* Landscape = Proxy->GetLandscapeActor())
		{
			Result->SetNumberField(TEXT("editLayerCount"), Landscape->GetLayersConst().Num());
		}
		Result->SetNumberField(TEXT("targetLayerCount"), Proxy->GetTargetLayers().Num());
		Result->SetStringField(TEXT("materialPath"), Proxy->GetLandscapeMaterial() ? Proxy->GetLandscapeMaterial()->GetPathName() : FString());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::ListLayers(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = ResolveProxy(Args, &Error);
		if (!Proxy)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);

		TArray<TSharedPtr<FJsonValue>> EditLayers;
		if (ALandscape* Landscape = Proxy->GetLandscapeActor())
		{
			for (const FLandscapeLayer& Layer : Landscape->GetLayersConst())
			{
				if (!Layer.EditLayer)
					continue;
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("name"), Layer.EditLayer->GetName().ToString());
				Json->SetStringField(TEXT("guid"), Layer.EditLayer->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
				Json->SetStringField(TEXT("class"), Layer.EditLayer->GetClass()->GetName());
				Json->SetBoolField(TEXT("visible"), Layer.EditLayer->IsVisible());
				Json->SetBoolField(TEXT("locked"), Layer.EditLayer->IsLocked());
				EditLayers.Add(MakeShared<FJsonValueObject>(Json));
			}
		}

		TArray<TSharedPtr<FJsonValue>> TargetLayers;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Proxy->GetTargetLayers())
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Pair.Key.ToString());
			Json->SetBoolField(TEXT("hasLayerInfo"), Pair.Value.LayerInfoObj != nullptr);
			if (Pair.Value.LayerInfoObj)
			{
				Json->SetStringField(TEXT("layerInfoPath"), Pair.Value.LayerInfoObj->GetPathName());
				Json->SetNumberField(TEXT("hardness"), Pair.Value.LayerInfoObj->GetHardness());
				Json->SetStringField(TEXT("physicalMaterialPath"),
					Pair.Value.LayerInfoObj->GetPhysicalMaterial() ? Pair.Value.LayerInfoObj->GetPhysicalMaterial()->GetPathName() : FString());
			}
			TargetLayers.Add(MakeShared<FJsonValueObject>(Json));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetNumberField(TEXT("editLayerCount"), EditLayers.Num());
		Result->SetArrayField(TEXT("editLayers"), EditLayers);
		Result->SetNumberField(TEXT("targetLayerCount"), TargetLayers.Num());
		Result->SetArrayField(TEXT("targetLayers"), TargetLayers);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::Sample(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = nullptr;
		ULandscapeInfo* Info = ResolveInfo(Args, &Proxy, &Error);
		if (!Proxy || !Info)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);

		double WorldX = 0.0;
		double WorldY = 0.0;
		if (!Args->TryGetNumberField(TEXT("x"), WorldX) || !Args->TryGetNumberField(TEXT("y"), WorldY))
		{
			return ErrorJson(TEXT("缺少有效的 x、y 世界坐标。"));
		}
		const FTransform ToWorld = Proxy->LandscapeActorToWorld();
		const FVector Local = ToWorld.InverseTransformPosition(FVector(WorldX, WorldY, Proxy->GetActorLocation().Z));
		const int32 X = FMath::RoundToInt(Local.X);
		const int32 Y = FMath::RoundToInt(Local.Y);

		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
		if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY) || X < MinX || X > MaxX || Y < MinY || Y > MaxY)
		{
			return ErrorJson(TEXT("采样坐标位于 Landscape 范围之外。"));
		}

		uint16 Height = 0;
		FLandscapeEditDataInterface Edit(Info);
		Edit.GetHeightDataFast(X, Y, X, Y, &Height, 1);
		const FVector WorldPosition = ToWorld.TransformPosition(FVector(X, Y, LandscapeDataAccess::GetLocalHeight(Height)));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("worldPosition"), JsonConversion::MakeVectorObject(WorldPosition));
		Result->SetObjectField(TEXT("landscapeVertex"), MakeIntPointJson(X, Y));
		Result->SetNumberField(TEXT("rawHeight"), Height);
		Result->SetNumberField(TEXT("height"), WorldPosition.Z);

		TArray<TSharedPtr<FJsonValue>> Weights;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Proxy->GetTargetLayers())
		{
			if (!Pair.Value.LayerInfoObj)
				continue;
			uint8 Weight = 0;
			Edit.GetWeightDataFast(Pair.Value.LayerInfoObj, X, Y, X, Y, &Weight, 1);
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Pair.Key.ToString());
			Json->SetNumberField(TEXT("weight"), Weight);
			Json->SetNumberField(TEXT("normalizedWeight"), static_cast<double>(Weight) / 255.0);
			Weights.Add(MakeShared<FJsonValueObject>(Json));
		}
		Result->SetArrayField(TEXT("layers"), Weights);
		return SuccessJson(Result);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLandscapeAdapter::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("action"), Action))
		{
			return nullptr;
		}
		if (Action == TEXT("sculpt"))
		{
			const TSharedPtr<FJsonObject>* Center = nullptr;
			double Radius = 0.0;
			if (!Args->TryGetObjectField(TEXT("center"), Center) || Center == nullptr || !Center->IsValid() || !Args->TryGetNumberField(TEXT("radius"), Radius))
			{
				return nullptr;
			}

			TSharedRef<FJsonObject> BatchArgs = MakeShared<FJsonObject>(*Args);
			BatchArgs->SetStringField(TEXT("action"), TEXT("sculpt_batch"));
			BatchArgs->RemoveField(TEXT("center"));
			BatchArgs->RemoveField(TEXT("radius"));

			TSharedRef<FJsonObject> Stroke = MakeShared<FJsonObject>();
			Stroke->SetObjectField(TEXT("center"), *Center);
			Stroke->SetNumberField(TEXT("radius"), Radius);
			BatchArgs->SetArrayField(TEXT("strokes"), { MakeShared<FJsonValueObject>(Stroke) });
			return CreateSculptBatchStepper(BatchArgs);
		}
		if (Action == TEXT("sculpt_batch"))
		{
			return CreateSculptBatchStepper(Args);
		}
		if (Action == TEXT("sample_batch") || Action == TEXT("sample_grid") || Action == TEXT("sample_polyline"))
		{
			return CreateSamplingTaskStepper(Args);
		}
		if (Action == TEXT("paint_layer"))
		{
			return CreatePaintLayerTaskStepper(Args);
		}
		if (Action == TEXT("set_height_rect") || Action == TEXT("import_heightmap") || Action == TEXT("reset_heights"))
		{
			return CreateHeightWriteStepper(Args);
		}
		return nullptr;
	}
}
