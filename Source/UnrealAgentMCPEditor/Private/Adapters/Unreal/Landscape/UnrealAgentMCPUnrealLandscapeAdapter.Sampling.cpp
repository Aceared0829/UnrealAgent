// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.Sampling.cpp
 * @brief 地形离散点、规则网格与折线的批量高度采样。
 */

#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool TryGetPoint(const TSharedPtr<FJsonObject>& Json, FVector2D& OutPoint)
		{
			double X = 0.0;
			double Y = 0.0;
			if (!Json.IsValid() || !Json->TryGetNumberField(TEXT("x"), X) || !Json->TryGetNumberField(TEXT("y"), Y))
			{
				return false;
			}
			OutPoint = FVector2D(X, Y);
			return true;
		}

		bool TryGetPointArray(const TSharedPtr<FJsonObject>& Args, TArray<FVector2D>& OutPoints, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("points"), Values) || !Values || Values->IsEmpty())
			{
				OutError = TEXT("points 必须是非空坐标数组。");
				return false;
			}
			OutPoints.Reserve(Values->Num());
			for (int32 Index = 0; Index < Values->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& Value = (*Values)[Index];
				FVector2D Point;
				if (!Value.IsValid() || Value->Type != EJson::Object || !TryGetPoint(Value->AsObject(), Point))
				{
					OutError = FString::Printf(TEXT("points[%d] 必须包含有效的 x、y。"), Index);
					return false;
				}
				OutPoints.Add(Point);
			}
			return true;
		}

		int32 ResolveMaxPoints(const TSharedPtr<FJsonObject>& Args)
		{
			double Value = 4096.0;
			Args->TryGetNumberField(TEXT("maxPoints"), Value);
			return FMath::Clamp(FMath::FloorToInt(Value), 1, 4096);
		}

		bool TryBuildSamplingPoints(const FString& Action, const TSharedPtr<FJsonObject>& Args, TArray<FVector2D>& OutPoints, FString& OutShape, FString& OutError)
		{
			OutPoints.Reset();
			OutShape.Reset();
			OutError.Reset();
			if (Action == TEXT("sample_batch"))
			{
				if (!TryGetPointArray(Args, OutPoints, OutError))
				{
					return false;
				}
				if (OutPoints.Num() > ResolveMaxPoints(Args))
				{
					OutError = FString::Printf(TEXT("采样点数量 %d 超过 maxPoints。"), OutPoints.Num());
					return false;
				}
				OutShape = TEXT("batch");
				return true;
			}

			if (Action == TEXT("sample_grid"))
			{
				const TSharedPtr<FJsonObject>* OriginJson = nullptr;
				FVector2D Origin;
				if (!Args->TryGetObjectField(TEXT("origin"), OriginJson) || !OriginJson || !TryGetPoint(*OriginJson, Origin))
				{
					OutError = TEXT("origin 必须包含有效的 x、y 世界坐标。");
					return false;
				}
				double Spacing = 0.0;
				double CountXNumber = 0.0;
				double CountYNumber = 0.0;
				if (!Args->TryGetNumberField(TEXT("spacing"), Spacing) || Spacing <= 0.0)
				{
					OutError = TEXT("spacing 必须大于零。");
					return false;
				}
				if (!Args->TryGetNumberField(TEXT("countX"), CountXNumber) || !Args->TryGetNumberField(TEXT("countY"), CountYNumber) || CountXNumber < 1.0 || CountYNumber < 1.0 ||
					!FMath::IsNearlyEqual(CountXNumber, FMath::RoundToDouble(CountXNumber)) || !FMath::IsNearlyEqual(CountYNumber, FMath::RoundToDouble(CountYNumber)))
				{
					OutError = TEXT("countX、countY 必须是正整数。");
					return false;
				}
				const int32 CountX = FMath::RoundToInt(CountXNumber);
				const int32 CountY = FMath::RoundToInt(CountYNumber);
				const int64 NumPoints = static_cast<int64>(CountX) * CountY;
				if (NumPoints > ResolveMaxPoints(Args))
				{
					OutError = FString::Printf(TEXT("网格采样点数量 %lld 超过 maxPoints。"), NumPoints);
					return false;
				}
				OutPoints.Reserve(static_cast<int32>(NumPoints));
				for (int32 Y = 0; Y < CountY; ++Y)
				{
					for (int32 X = 0; X < CountX; ++X)
					{
						OutPoints.Add(Origin + FVector2D(X * Spacing, Y * Spacing));
					}
				}
				OutShape = TEXT("grid");
				return true;
			}

			if (Action == TEXT("sample_polyline"))
			{
				TArray<FVector2D> ControlPoints;
				if (!TryGetPointArray(Args, ControlPoints, OutError))
				{
					return false;
				}
				if (ControlPoints.Num() < 2)
				{
					OutError = TEXT("折线至少需要两个 points。");
					return false;
				}
				double Spacing = 1000.0;
				Args->TryGetNumberField(TEXT("spacing"), Spacing);
				if (Spacing <= 0.0)
				{
					OutError = TEXT("spacing 必须大于零。");
					return false;
				}

				const int32 MaxPoints = ResolveMaxPoints(Args);
				OutPoints.Add(ControlPoints[0]);
				for (int32 SegmentIndex = 1; SegmentIndex < ControlPoints.Num(); ++SegmentIndex)
				{
					const FVector2D Start = ControlPoints[SegmentIndex - 1];
					const FVector2D End = ControlPoints[SegmentIndex];
					const double Length = FVector2D::Distance(Start, End);
					const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(Length / Spacing));
					if (static_cast<int64>(OutPoints.Num()) + NumSteps > MaxPoints)
					{
						OutError = FString::Printf(TEXT("折线采样点数量超过 maxPoints=%d。"), MaxPoints);
						return false;
					}
					for (int32 Step = 1; Step <= NumSteps; ++Step)
					{
						OutPoints.Add(FMath::Lerp(Start, End, static_cast<double>(Step) / NumSteps));
					}
				}
				OutShape = TEXT("polyline");
				return true;
			}

			OutError = FString::Printf(TEXT("不支持的 Landscape 采样 action：%s。"), *Action);
			return false;
		}

		bool ShouldStopSampling(FString& OutError)
		{
			const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
			if (!Context || !Context->ShouldStop())
			{
				return false;
			}
			OutError = Context->IsDeadlineExceeded() ? TEXT("Landscape sampling task deadline exceeded.") : Context->GetCancellationReason();
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Landscape sampling task cancelled.");
			}
			return true;
		}

		void ReportSamplingProgress(const double Fraction, const TCHAR* Message)
		{
			if (const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent())
			{
				Context->ReportProgress(Fraction, Message);
			}
		}

		TSharedRef<FJsonObject> MakePointJson(const FVector2D& Point)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetNumberField(TEXT("x"), Point.X);
			Json->SetNumberField(TEXT("y"), Point.Y);
			return Json;
		}

		TSharedRef<FJsonObject> MakeVertexJson(const FIntPoint& Point)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetNumberField(TEXT("x"), Point.X);
			Json->SetNumberField(TEXT("y"), Point.Y);
			return Json;
		}
	}

	class FLandscapeSamplingTaskStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLandscapeSamplingTaskStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
			Arguments->TryGetStringField(TEXT("action"), Action);
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			(void)FrameBudget;
			if (!bInitialized)
			{
				FString Error;
				if (!TryBuildSamplingPoints(Action, Arguments, Points, Shape, Error))
				{
					return Execution::FMcpTaskStepResult::Failed(MoveTemp(Error));
				}
				bInitialized = true;
				Context.ReportProgress(0.02, TEXT("Landscape 采样请求已验证。"));
				return Execution::FMcpTaskStepResult::Continue();
			}

			if (NextPointIndex < Points.Num())
			{
				constexpr int32 PointsPerStep = 16;
				const int32 NumPoints = FMath::Min(PointsPerStep, Points.Num() - NextPointIndex);
				TArray<FVector2D> Chunk;
				Chunk.Append(Points.GetData() + NextPointIndex, NumPoints);
				TSharedRef<FJsonObject> ChunkArguments = MakeShared<FJsonObject>(*Arguments);
				double RequestedMaxReadVertices = 16384.0;
				Arguments->TryGetNumberField(TEXT("maxReadVertices"), RequestedMaxReadVertices);
				ChunkArguments->SetNumberField(TEXT("maxReadVertices"), FMath::Clamp(RequestedMaxReadVertices, 1.0, 16384.0));
				const FString ChunkJson = FUnrealAgentMCPUnrealLandscapeAdapter::SamplePoints(ChunkArguments, Chunk, Shape);
				TSharedPtr<FJsonObject> ChunkResult;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ChunkJson);
				if (!FJsonSerializer::Deserialize(Reader, ChunkResult) || !ChunkResult.IsValid())
				{
					return Execution::FMcpTaskStepResult::Failed(TEXT("Landscape 采样步骤返回了无效 JSON。"));
				}
				bool bSucceeded = false;
				ChunkResult->TryGetBoolField(TEXT("success"), bSucceeded);
				if (!bSucceeded)
				{
					FString Error;
					ChunkResult->TryGetStringField(TEXT("error"), Error);
					return Execution::FMcpTaskStepResult::Failed(Error.IsEmpty() ? TEXT("Landscape 采样步骤失败。") : MoveTemp(Error));
				}

				if (LandscapeLabel.IsEmpty())
				{
					ChunkResult->TryGetStringField(TEXT("landscape"), LandscapeLabel);
				}
				double ChunkReadVertices = 0.0;
				ChunkResult->TryGetNumberField(TEXT("readVertexCount"), ChunkReadVertices);
				ReadVertexCount += static_cast<int64>(ChunkReadVertices);

				const TArray<TSharedPtr<FJsonValue>>* ChunkSamples = nullptr;
				if (!ChunkResult->TryGetArrayField(TEXT("samples"), ChunkSamples) || ChunkSamples == nullptr)
				{
					return Execution::FMcpTaskStepResult::Failed(TEXT("Landscape 采样步骤缺少 samples。"));
				}
				for (int32 LocalIndex = 0; LocalIndex < ChunkSamples->Num(); ++LocalIndex)
				{
					const TSharedPtr<FJsonObject> Sample = (*ChunkSamples)[LocalIndex]->AsObject();
					if (!Sample.IsValid())
					{
						continue;
					}
					Sample->SetNumberField(TEXT("index"), NextPointIndex + LocalIndex);
					bool bInsideLandscape = false;
					Sample->TryGetBoolField(TEXT("insideLandscape"), bInsideLandscape);
					if (bInsideLandscape)
					{
						double Height = 0.0;
						if (Sample->TryGetNumberField(TEXT("height"), Height))
						{
							HeightMin = FMath::Min(HeightMin, Height);
							HeightMax = FMath::Max(HeightMax, Height);
							HeightSum += Height;
							ValidSampleCount++;
						}
					}
					else
					{
						OutOfBoundsCount++;
					}
					Samples.Add(MakeShared<FJsonValueObject>(Sample));
				}
				NextPointIndex += NumPoints;
				Context.ReportProgress(0.05 + 0.9 * static_cast<double>(NextPointIndex) / FMath::Max(Points.Num(), 1), TEXT("分批读取 Landscape 高度。"));
				return Execution::FMcpTaskStepResult::Continue();
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("landscape"), LandscapeLabel);
			Result->SetStringField(TEXT("shape"), Shape);
			Result->SetStringField(TEXT("readMode"), TEXT("chunked"));
			Result->SetNumberField(TEXT("sampleCount"), Points.Num());
			Result->SetNumberField(TEXT("validSampleCount"), ValidSampleCount);
			Result->SetNumberField(TEXT("outOfBoundsCount"), OutOfBoundsCount);
			Result->SetNumberField(TEXT("readVertexCount"), static_cast<double>(ReadVertexCount));
			if (ValidSampleCount > 0)
			{
				Result->SetNumberField(TEXT("heightMin"), HeightMin);
				Result->SetNumberField(TEXT("heightMax"), HeightMax);
				Result->SetNumberField(TEXT("heightMean"), HeightSum / ValidSampleCount);
			}
			Result->SetArrayField(TEXT("samples"), Samples);
			Context.ReportProgress(1.0, TEXT("Landscape 采样完成。"));
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

	private:
		TSharedRef<FJsonObject> Arguments;
		FString Action;
		FString Shape;
		FString LandscapeLabel;
		TArray<FVector2D> Points;
		TArray<TSharedPtr<FJsonValue>> Samples;
		int32 NextPointIndex = 0;
		int32 ValidSampleCount = 0;
		int32 OutOfBoundsCount = 0;
		int64 ReadVertexCount = 0;
		double HeightMin = TNumericLimits<double>::Max();
		double HeightMax = TNumericLimits<double>::Lowest();
		double HeightSum = 0.0;
		bool bInitialized = false;
	};

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLandscapeAdapter::CreateSamplingTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		return MakeShared<FLandscapeSamplingTaskStepper>(Args);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::SampleBatch(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FVector2D> Points;
		FString Shape;
		FString Error;
		if (!TryBuildSamplingPoints(TEXT("sample_batch"), Args, Points, Shape, Error))
		{
			return ErrorJson(Error);
		}
		return SamplePoints(Args, Points, Shape);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::SampleGrid(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FVector2D> Points;
		FString Shape;
		FString Error;
		if (!TryBuildSamplingPoints(TEXT("sample_grid"), Args, Points, Shape, Error))
		{
			return ErrorJson(Error);
		}
		return SamplePoints(Args, Points, Shape);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::SamplePolyline(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FVector2D> Points;
		FString Shape;
		FString Error;
		if (!TryBuildSamplingPoints(TEXT("sample_polyline"), Args, Points, Shape, Error))
		{
			return ErrorJson(Error);
		}
		return SamplePoints(Args, Points, Shape);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::SamplePoints(const TSharedPtr<FJsonObject>& Args, const TArray<FVector2D>& Points, const FString& Shape)
	{
		FString Error;
		ALandscapeProxy* Proxy = nullptr;
		ULandscapeInfo* Info = ResolveInfo(Args, &Proxy, &Error);
		if (!Proxy || !Info)
		{
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		}

		int32 LandscapeMinX = 0;
		int32 LandscapeMinY = 0;
		int32 LandscapeMaxX = 0;
		int32 LandscapeMaxY = 0;
		if (!Info->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY))
		{
			return ErrorJson(TEXT("Landscape 没有可采样范围。"));
		}

		const FTransform ToWorld = Proxy->LandscapeActorToWorld();
		TArray<FIntPoint> Vertices;
		TArray<bool> InsideFlags;
		Vertices.Reserve(Points.Num());
		InsideFlags.Reserve(Points.Num());
		FIntRect ReadRect;
		bool bHasReadRect = false;
		int32 NumValidSamples = 0;
		for (const FVector2D& Point : Points)
		{
			const FVector Local = ToWorld.InverseTransformPosition(FVector(Point.X, Point.Y, Proxy->GetActorLocation().Z));
			const FIntPoint Vertex(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y));
			const bool bInside = Vertex.X >= LandscapeMinX && Vertex.X <= LandscapeMaxX && Vertex.Y >= LandscapeMinY && Vertex.Y <= LandscapeMaxY;
			Vertices.Add(Vertex);
			InsideFlags.Add(bInside);
			if (!bInside)
			{
				continue;
			}
			++NumValidSamples;
			if (!bHasReadRect)
			{
				ReadRect.Min = Vertex;
				ReadRect.Max = Vertex;
				bHasReadRect = true;
			}
			else
			{
				ReadRect.Min.X = FMath::Min(ReadRect.Min.X, Vertex.X);
				ReadRect.Min.Y = FMath::Min(ReadRect.Min.Y, Vertex.Y);
				ReadRect.Max.X = FMath::Max(ReadRect.Max.X, Vertex.X);
				ReadRect.Max.Y = FMath::Max(ReadRect.Max.Y, Vertex.Y);
			}
		}

		double MaxReadVerticesNumber = 500000.0;
		Args->TryGetNumberField(TEXT("maxReadVertices"), MaxReadVerticesNumber);
		const int64 MaxReadVertices = FMath::Clamp<int64>(static_cast<int64>(MaxReadVerticesNumber), 1, 2000000);
		const int32 ReadWidth = bHasReadRect ? ReadRect.Max.X - ReadRect.Min.X + 1 : 0;
		const int32 ReadHeight = bHasReadRect ? ReadRect.Max.Y - ReadRect.Min.Y + 1 : 0;
		const int64 NumReadVertices = static_cast<int64>(ReadWidth) * ReadHeight;
		const bool bUseRectRead = bHasReadRect && NumReadVertices <= MaxReadVertices;

		FLandscapeEditDataInterface Edit(Info);
		TArray<uint16> RectHeights;
		if (bUseRectRead)
		{
			RectHeights.SetNumUninitialized(NumReadVertices);
			ReportSamplingProgress(0.2, TEXT("Reading landscape height region"));
			Edit.GetHeightDataFast(ReadRect.Min.X, ReadRect.Min.Y, ReadRect.Max.X, ReadRect.Max.Y, RectHeights.GetData(), ReadWidth);
		}

		TMap<FIntPoint, uint16> PointHeightCache;
		TArray<TSharedPtr<FJsonValue>> Samples;
		Samples.Reserve(Points.Num());
		int32 NumOutOfBoundsSamples = 0;
		double HeightMin = TNumericLimits<double>::Max();
		double HeightMax = TNumericLimits<double>::Lowest();
		double HeightSum = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if ((Index & 255) == 0)
			{
				if (ShouldStopSampling(Error))
				{
					return ErrorJson(Error);
				}
				ReportSamplingProgress(0.25 + 0.7 * static_cast<double>(Index) / FMath::Max(Points.Num(), 1), TEXT("Collecting landscape samples"));
			}
			TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
			Sample->SetNumberField(TEXT("index"), Index);
			Sample->SetObjectField(TEXT("requested"), MakePointJson(Points[Index]));
			Sample->SetObjectField(TEXT("landscapeVertex"), MakeVertexJson(Vertices[Index]));
			Sample->SetBoolField(TEXT("insideLandscape"), InsideFlags[Index]);
			if (!InsideFlags[Index])
			{
				++NumOutOfBoundsSamples;
				Samples.Add(MakeShared<FJsonValueObject>(Sample));
				continue;
			}

			uint16 Height = 0;
			if (bUseRectRead)
			{
				Height = RectHeights[(Vertices[Index].Y - ReadRect.Min.Y) * ReadWidth + Vertices[Index].X - ReadRect.Min.X];
			}
			else if (const uint16* Cached = PointHeightCache.Find(Vertices[Index]))
			{
				Height = *Cached;
			}
			else
			{
				Edit.GetHeightDataFast(Vertices[Index].X, Vertices[Index].Y, Vertices[Index].X, Vertices[Index].Y, &Height, 1);
				PointHeightCache.Add(Vertices[Index], Height);
			}
			const FVector WorldPosition = ToWorld.TransformPosition(FVector(Vertices[Index].X, Vertices[Index].Y, LandscapeDataAccess::GetLocalHeight(Height)));
			Sample->SetNumberField(TEXT("rawHeight"), Height);
			Sample->SetNumberField(TEXT("height"), WorldPosition.Z);
			Sample->SetObjectField(TEXT("worldPosition"), JsonConversion::MakeVectorObject(WorldPosition));
			HeightMin = FMath::Min(HeightMin, WorldPosition.Z);
			HeightMax = FMath::Max(HeightMax, WorldPosition.Z);
			HeightSum += WorldPosition.Z;
			Samples.Add(MakeShared<FJsonValueObject>(Sample));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetStringField(TEXT("shape"), Shape);
		Result->SetStringField(TEXT("readMode"), bUseRectRead ? TEXT("region") : TEXT("point_cache"));
		Result->SetNumberField(TEXT("sampleCount"), Points.Num());
		Result->SetNumberField(TEXT("validSampleCount"), NumValidSamples);
		Result->SetNumberField(TEXT("outOfBoundsCount"), NumOutOfBoundsSamples);
		Result->SetNumberField(TEXT("readVertexCount"), bUseRectRead ? NumReadVertices : PointHeightCache.Num());
		if (NumValidSamples > 0)
		{
			Result->SetNumberField(TEXT("heightMin"), HeightMin);
			Result->SetNumberField(TEXT("heightMax"), HeightMax);
			Result->SetNumberField(TEXT("heightMean"), HeightSum / NumValidSamples);
		}
		Result->SetArrayField(TEXT("samples"), Samples);
		return SuccessJson(Result);
	}
}
