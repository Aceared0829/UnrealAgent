// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.Brush.cpp
 * @brief 地形高度雕刻与目标图层绘制的安全笔刷实现。
 */

#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool ShouldStopLandscapeTask(FString& OutError)
		{
			const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
			if (!Context || !Context->ShouldStop())
			{
				return false;
			}
			OutError = Context->IsDeadlineExceeded() ? TEXT("Landscape task deadline exceeded.") : Context->GetCancellationReason();
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Landscape task cancelled.");
			}
			return true;
		}

		void ReportLandscapeTaskProgress(const double Fraction, const TCHAR* Message)
		{
			if (const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent())
			{
				Context->ReportProgress(Fraction, Message);
			}
		}

		struct FLandscapeBrushRegion
		{
			FIntRect Rect;
			FVector2D Center = FVector2D::ZeroVector;
			double Radius = 500.0;
			double Falloff = 0.5;
			int64 VertexCount = 0;
		};

		struct FLandscapeSculptStroke
		{
			FLandscapeBrushRegion Region;
			FString Mode = TEXT("raise");
			double Amount = 100.0;
			double LocalAmount = 100.0;
			double TargetWorldZ = 0.0;
			bool bHasTargetWorldZ = false;
			bool bInitialized = false;
			int32 NextLocalY = 0;
			uint16 FlattenHeight = 0;
			int32 EvaluatedCount = 0;
			int32 ChangedCount = 0;
			int32 ClampedLowCount = 0;
			int32 ClampedHighCount = 0;
			uint16 RawHeightMinBefore = MAX_uint16;
			uint16 RawHeightMaxBefore = 0;
			uint16 RawHeightMinAfter = MAX_uint16;
			uint16 RawHeightMaxAfter = 0;
			double WorldZMinBefore = TNumericLimits<double>::Max();
			double WorldZMaxBefore = TNumericLimits<double>::Lowest();
			double WorldZMinAfter = TNumericLimits<double>::Max();
			double WorldZMaxAfter = TNumericLimits<double>::Lowest();
		};

		enum class ELandscapeSculptBatchPhase : uint8
		{
			Prepare,
			Read,
			Compute,
			Apply,
			Complete
		};

		bool TryGetCenter(const TSharedPtr<FJsonObject>& Args, FVector2D& OutCenter)
		{
			const TSharedPtr<FJsonObject>* Center = nullptr;
			if (!Args->TryGetObjectField(TEXT("center"), Center) || !Center || !Center->IsValid())
			{
				return false;
			}
			double X = 0.0;
			double Y = 0.0;
			if (!(*Center)->TryGetNumberField(TEXT("x"), X) || !(*Center)->TryGetNumberField(TEXT("y"), Y))
			{
				return false;
			}
			OutCenter = FVector2D(X, Y);
			return true;
		}

		bool BuildBrushRegion(const TSharedPtr<FJsonObject>& Args, ALandscapeProxy* Proxy, ULandscapeInfo* Info, FLandscapeBrushRegion& OutRegion, FString& OutError)
		{
			if (!TryGetCenter(Args, OutRegion.Center))
			{
				OutError = TEXT("center 必须包含有效的 x、y 世界坐标。");
				return false;
			}
			Args->TryGetNumberField(TEXT("radius"), OutRegion.Radius);
			Args->TryGetNumberField(TEXT("falloff"), OutRegion.Falloff);
			if (OutRegion.Radius <= 0.0)
			{
				OutError = TEXT("radius 必须大于零。");
				return false;
			}
			OutRegion.Falloff = FMath::Clamp(OutRegion.Falloff, 0.0, 1.0);

			const FTransform ToWorld = Proxy->LandscapeActorToWorld();
			const FVector LocalCenter = ToWorld.InverseTransformPosition(FVector(OutRegion.Center.X, OutRegion.Center.Y, Proxy->GetActorLocation().Z));
			const FVector Scale = ToWorld.GetScale3D().GetAbs();
			const double RadiusX = OutRegion.Radius / FMath::Max(static_cast<double>(Scale.X), UE_DOUBLE_SMALL_NUMBER);
			const double RadiusY = OutRegion.Radius / FMath::Max(static_cast<double>(Scale.Y), UE_DOUBLE_SMALL_NUMBER);

			int32 LandscapeMinX = 0;
			int32 LandscapeMinY = 0;
			int32 LandscapeMaxX = 0;
			int32 LandscapeMaxY = 0;
			if (!Info->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY))
			{
				OutError = TEXT("Landscape 没有可编辑范围。");
				return false;
			}
			OutRegion.Rect.Min.X = FMath::Clamp(FMath::FloorToInt(LocalCenter.X - RadiusX), LandscapeMinX, LandscapeMaxX);
			OutRegion.Rect.Min.Y = FMath::Clamp(FMath::FloorToInt(LocalCenter.Y - RadiusY), LandscapeMinY, LandscapeMaxY);
			OutRegion.Rect.Max.X = FMath::Clamp(FMath::CeilToInt(LocalCenter.X + RadiusX), LandscapeMinX, LandscapeMaxX);
			OutRegion.Rect.Max.Y = FMath::Clamp(FMath::CeilToInt(LocalCenter.Y + RadiusY), LandscapeMinY, LandscapeMaxY);
			const int32 Width = OutRegion.Rect.Max.X - OutRegion.Rect.Min.X + 1;
			const int32 Height = OutRegion.Rect.Max.Y - OutRegion.Rect.Min.Y + 1;
			OutRegion.VertexCount = static_cast<int64>(Width) * Height;
			double MaxVerticesNumber = 4000000.0;
			Args->TryGetNumberField(TEXT("maxVertices"), MaxVerticesNumber);
			const int64 MaxVertices = FMath::Clamp<int64>(static_cast<int64>(MaxVerticesNumber), 1, 4000000);
			if (OutRegion.VertexCount > MaxVertices)
			{
				OutError = FString::Printf(TEXT("笔刷范围包含 %lld 个顶点，超过 maxVertices=%lld。"), OutRegion.VertexCount, MaxVertices);
				return false;
			}
			return true;
		}

		FGuid ResolveEditLayerGuid(const TSharedPtr<FJsonObject>& Args, ALandscape* Landscape, FString& OutLayerName, FString& OutError)
		{
			if (!Landscape)
			{
				OutError = TEXT("目标 Landscape 主 Actor 不可用。");
				return FGuid();
			}
			if (Landscape->GetLayersConst().IsEmpty())
				Landscape->CreateDefaultLayer();

			FString RequestedName;
			Args->TryGetStringField(TEXT("editLayer"), RequestedName);
			const FLandscapeLayer* Layer = nullptr;
			if (!RequestedName.IsEmpty())
			{
				Layer = Landscape->GetLayerConst(FName(*RequestedName));
			}
			else
			{
				double IndexNumber = 0.0;
				Args->TryGetNumberField(TEXT("editLayerIndex"), IndexNumber);
				Layer = Landscape->GetLayerConst(static_cast<int32>(IndexNumber));
			}
			if (!Layer || !Layer->EditLayer)
			{
				OutError = RequestedName.IsEmpty() ? TEXT("无效 editLayerIndex。") : FString::Printf(TEXT("未找到编辑层：%s"), *RequestedName);
				return FGuid();
			}
			OutLayerName = Layer->EditLayer->GetName().ToString();
			return Layer->EditLayer->GetGuid();
		}

		double BrushAlpha(const FVector2D& Position, const FLandscapeBrushRegion& Region)
		{
			const double Distance = FVector2D::Distance(Position, Region.Center);
			if (Distance > Region.Radius)
				return 0.0;
			if (Region.Falloff <= UE_DOUBLE_SMALL_NUMBER)
				return 1.0;
			const double InnerRadius = Region.Radius * (1.0 - Region.Falloff);
			if (Distance <= InnerRadius)
				return 1.0;
			const double T = FMath::Clamp((Distance - InnerRadius) / FMath::Max(Region.Radius - InnerRadius, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
			return 1.0 - FMath::SmoothStep(0.0, 1.0, T);
		}
	}

	class FLandscapeSculptBatchStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLandscapeSculptBatchStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("sculpt_batch 必须在 GameThread 上执行。"));
			}
			if (Phase != ELandscapeSculptBatchPhase::Prepare && (!IsValid(Proxy) || !IsValid(Info) || !IsValid(Landscape)))
			{
				return Fail(TEXT("sculpt_batch 执行期间目标 Landscape 已失效。"));
			}

			const double BudgetSeconds = FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			const double StopTime = FPlatformTime::Seconds() + BudgetSeconds;
			bool bAdvanced = false;
			do
			{
				if (Context.ShouldStop())
				{
					const FString Reason = Context.IsDeadlineExceeded() ? TEXT("Landscape task deadline exceeded.") : Context.GetCancellationReason();
					return Fail(Reason);
				}

				switch (Phase)
				{
				case ELandscapeSculptBatchPhase::Prepare:
					if (!Prepare(Context))
					{
						return Fail(Error);
					}
					break;
				case ELandscapeSculptBatchPhase::Read:
					ReadNextChunk(Context);
					break;
				case ELandscapeSculptBatchPhase::Compute:
					if (!ComputeNextRow(Context))
					{
						return Fail(Error);
					}
					break;
				case ELandscapeSculptBatchPhase::Apply:
					ApplyNextChunk(Context);
					break;
				case ELandscapeSculptBatchPhase::Complete:
					return Succeed();
				default:
					return Fail(TEXT("sculpt_batch 进入未知执行阶段。"));
				}
				bAdvanced = true;
			} while (FPlatformTime::Seconds() < StopTime || !bAdvanced);

			return Phase == ELandscapeSculptBatchPhase::Complete ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			if (Reason.IsEmpty())
			{
				Reason = TEXT("sculpt_batch 已取消。");
			}
			return Fail(MoveTemp(Reason));
		}

	private:
		bool Prepare(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!bPreparationInitialized && !InitializePreparation())
			{
				return false;
			}
			if (NextStrokeToPrepare < StrokeValues.Num())
			{
				if (!PrepareNextStroke())
				{
					return false;
				}
				const double Fraction = static_cast<double>(NextStrokeToPrepare) / FMath::Max(StrokeValues.Num(), 1);
				Context.ReportProgress(0.02 * Fraction, TEXT("Preparing landscape strokes"));
				return true;
			}
			return FinalizePreparation(Context);
		}

		bool InitializePreparation()
		{
			Info = FUnrealAgentMCPUnrealLandscapeAdapter::ResolveInfo(Arguments, &Proxy, &Error);
			Landscape = Proxy ? Proxy->GetLandscapeActor() : nullptr;
			if (!Proxy || !Info || !Landscape)
			{
				if (Error.IsEmpty())
				{
					Error = TEXT("当前世界中未找到目标 Landscape。");
				}
				return false;
			}

			EditLayerGuid = ResolveEditLayerGuid(Arguments, Landscape, EditLayerName, Error);
			if (!EditLayerGuid.IsValid())
			{
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* RequestedStrokes = nullptr;
			if (!Arguments->TryGetArrayField(TEXT("strokes"), RequestedStrokes) || !RequestedStrokes || RequestedStrokes->IsEmpty())
			{
				Error = TEXT("strokes 必须至少包含一个笔刷。");
				return false;
			}
			if (RequestedStrokes->Num() > 512)
			{
				Error = TEXT("sculpt_batch 单次最多接受 512 个笔刷。");
				return false;
			}

			StrokeValues = *RequestedStrokes;
			Strokes.Reserve(StrokeValues.Num());
			ToWorld = Proxy->LandscapeActorToWorld();
			ScaleZ = FMath::Max(static_cast<double>(FMath::Abs(ToWorld.GetScale3D().Z)), UE_DOUBLE_SMALL_NUMBER);
			WorldLocalZ = ToWorld.TransformVector(FVector::UpVector);
			bPreparationInitialized = true;
			return true;
		}

		bool PrepareNextStroke()
		{
			const int32 Index = NextStrokeToPrepare;
			const TSharedPtr<FJsonObject> StrokeObject = StrokeValues[Index].IsValid() ? StrokeValues[Index]->AsObject() : nullptr;
			if (!StrokeObject.IsValid())
			{
				Error = FString::Printf(TEXT("strokes[%d] 必须是对象。"), Index);
				return false;
			}

			TSharedRef<FJsonObject> StrokeArgs = MakeShared<FJsonObject>(*Arguments);
			StrokeArgs->RemoveField(TEXT("strokes"));
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : StrokeObject->Values)
			{
				StrokeArgs->SetField(Field.Key, Field.Value);
			}

			FLandscapeSculptStroke& Stroke = Strokes.AddDefaulted_GetRef();
			if (!BuildBrushRegion(StrokeArgs, Proxy, Info, Stroke.Region, Error))
			{
				Error = FString::Printf(TEXT("strokes[%d]：%s"), Index, *Error);
				return false;
			}
			StrokeArgs->TryGetStringField(TEXT("mode"), Stroke.Mode);
			Stroke.Mode.ToLowerInline();
			if (Stroke.Mode.IsEmpty())
			{
				Stroke.Mode = TEXT("raise");
			}
			if (Stroke.Mode != TEXT("raise") && Stroke.Mode != TEXT("lower") && Stroke.Mode != TEXT("flatten") && Stroke.Mode != TEXT("set"))
			{
				Error = FString::Printf(TEXT("strokes[%d].mode 必须为 raise、lower、flatten 或 set。"), Index);
				return false;
			}
			StrokeArgs->TryGetNumberField(TEXT("amount"), Stroke.Amount);
			if (Stroke.Amount < 0.0)
			{
				Error = FString::Printf(TEXT("strokes[%d].amount 必须为非负数。"), Index);
				return false;
			}
			Stroke.bHasTargetWorldZ = StrokeArgs->TryGetNumberField(TEXT("targetWorldZ"), Stroke.TargetWorldZ);
			if (Stroke.Mode == TEXT("set") && !Stroke.bHasTargetWorldZ)
			{
				Error = FString::Printf(TEXT("strokes[%d] 使用 mode=set 时必须提供 targetWorldZ。"), Index);
				return false;
			}
			if (Stroke.bHasTargetWorldZ && FMath::IsNearlyZero(WorldLocalZ.Z))
			{
				Error = TEXT("Landscape 变换无法把 targetWorldZ 映射到局部高度。");
				return false;
			}
			Stroke.LocalAmount = Stroke.Amount / ScaleZ;
			Stroke.NextLocalY = Stroke.Region.Rect.Min.Y;

			if (Index == 0)
			{
				DirtyRect = Stroke.Region.Rect;
			}
			else
			{
				DirtyRect.Min.X = FMath::Min(DirtyRect.Min.X, Stroke.Region.Rect.Min.X);
				DirtyRect.Min.Y = FMath::Min(DirtyRect.Min.Y, Stroke.Region.Rect.Min.Y);
				DirtyRect.Max.X = FMath::Max(DirtyRect.Max.X, Stroke.Region.Rect.Max.X);
				DirtyRect.Max.Y = FMath::Max(DirtyRect.Max.Y, Stroke.Region.Rect.Max.Y);
			}
			++NextStrokeToPrepare;
			return true;
		}

		bool FinalizePreparation(Execution::FMcpTaskExecutionContext& Context)
		{
			Width = DirtyRect.Max.X - DirtyRect.Min.X + 1;
			Height = DirtyRect.Max.Y - DirtyRect.Min.Y + 1;
			const int64 VertexCount = static_cast<int64>(Width) * Height;
			double MaxVerticesNumber = 4000000.0;
			Arguments->TryGetNumberField(TEXT("maxVertices"), MaxVerticesNumber);
			const int64 MaxVertices = FMath::Clamp<int64>(static_cast<int64>(MaxVerticesNumber), 1, 4000000);
			if (VertexCount > MaxVertices)
			{
				Error = FString::Printf(TEXT("合并笔刷范围包含 %lld 个顶点，超过 maxVertices=%lld。"), VertexCount, MaxVertices);
				return false;
			}

			Heights.SetNumUninitialized(VertexCount);
			TouchedVertices.Init(false, VertexCount);
			Edit = MakeUnique<FLandscapeEditDataInterface>(Info, EditLayerGuid);
			NextReadY = DirtyRect.Min.Y;
			Phase = ELandscapeSculptBatchPhase::Read;
			Context.ReportProgress(0.02, TEXT("Prepared landscape sculpt batch"));
			return true;
		}

		void ReadNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			const int32 ChunkMaxY = FMath::Min(NextReadY + RowsPerIoChunk - 1, DirtyRect.Max.Y);
			Edit->GetHeightDataFast(DirtyRect.Min.X, NextReadY, DirtyRect.Max.X, ChunkMaxY, Heights.GetData() + (NextReadY - DirtyRect.Min.Y) * Width, Width);
			++ReadChunkCount;
			NextReadY = ChunkMaxY + 1;
			const double ReadFraction = static_cast<double>(NextReadY - DirtyRect.Min.Y) / FMath::Max(Height, 1);
			Context.ReportProgress(0.02 + 0.18 * ReadFraction, TEXT("Reading landscape heights"));
			if (NextReadY > DirtyRect.Max.Y)
			{
				Phase = ELandscapeSculptBatchPhase::Compute;
			}
		}

		bool ComputeNextRow(Execution::FMcpTaskExecutionContext& Context)
		{
			if (CurrentStrokeIndex >= Strokes.Num())
			{
				Phase = TotalChangedCount > 0 ? ELandscapeSculptBatchPhase::Apply : ELandscapeSculptBatchPhase::Complete;
				NextApplyY = DirtyRect.Min.Y;
				return true;
			}

			FLandscapeSculptStroke& Stroke = Strokes[CurrentStrokeIndex];
			if (!Stroke.bInitialized)
			{
				const FVector LocalCenter = ToWorld.InverseTransformPosition(FVector(Stroke.Region.Center.X, Stroke.Region.Center.Y, Proxy->GetActorLocation().Z));
				const int32 CenterX = FMath::Clamp(FMath::RoundToInt(LocalCenter.X), DirtyRect.Min.X, DirtyRect.Max.X);
				const int32 CenterY = FMath::Clamp(FMath::RoundToInt(LocalCenter.Y), DirtyRect.Min.Y, DirtyRect.Max.Y);
				Stroke.FlattenHeight = Heights[(CenterY - DirtyRect.Min.Y) * Width + CenterX - DirtyRect.Min.X];
				Stroke.bInitialized = true;
			}

			const float MinLocalHeight = LandscapeDataAccess::GetLocalHeight(0);
			const float MaxLocalHeight = LandscapeDataAccess::GetLocalHeight(MAX_uint16);
			const int32 LocalY = Stroke.NextLocalY;
			for (int32 LocalX = Stroke.Region.Rect.Min.X; LocalX <= Stroke.Region.Rect.Max.X; ++LocalX)
			{
				const int32 Index = (LocalY - DirtyRect.Min.Y) * Width + LocalX - DirtyRect.Min.X;
				const FVector WorldPoint = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0));
				const double Alpha = BrushAlpha(FVector2D(WorldPoint.X, WorldPoint.Y), Stroke.Region);
				if (Alpha <= 0.0)
				{
					continue;
				}

				++Stroke.EvaluatedCount;
				++TotalEvaluatedCount;
				const uint16 CurrentHeight = Heights[Index];
				const float CurrentLocalHeight = LandscapeDataAccess::GetLocalHeight(CurrentHeight);
				Stroke.RawHeightMinBefore = FMath::Min(Stroke.RawHeightMinBefore, CurrentHeight);
				Stroke.RawHeightMaxBefore = FMath::Max(Stroke.RawHeightMaxBefore, CurrentHeight);
				const double CurrentWorldZ = ToWorld.TransformPosition(FVector(LocalX, LocalY, CurrentLocalHeight)).Z;
				Stroke.WorldZMinBefore = FMath::Min(Stroke.WorldZMinBefore, CurrentWorldZ);
				Stroke.WorldZMaxBefore = FMath::Max(Stroke.WorldZMaxBefore, CurrentWorldZ);

				float NewLocalHeight = CurrentLocalHeight;
				if (Stroke.Mode == TEXT("raise"))
				{
					NewLocalHeight += static_cast<float>(Stroke.LocalAmount * Alpha);
				}
				else if (Stroke.Mode == TEXT("lower"))
				{
					NewLocalHeight -= static_cast<float>(Stroke.LocalAmount * Alpha);
				}
				else
				{
					float Target = LandscapeDataAccess::GetLocalHeight(Stroke.FlattenHeight);
					if (Stroke.bHasTargetWorldZ)
					{
						const double WorldZAtLocalZero = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0)).Z;
						Target = static_cast<float>((Stroke.TargetWorldZ - WorldZAtLocalZero) / WorldLocalZ.Z);
					}
					NewLocalHeight = FMath::Lerp(CurrentLocalHeight, Target, static_cast<float>(Alpha));
				}

				if (NewLocalHeight < MinLocalHeight)
				{
					++Stroke.ClampedLowCount;
					++TotalClampedLowCount;
				}
				else if (NewLocalHeight > MaxLocalHeight)
				{
					++Stroke.ClampedHighCount;
					++TotalClampedHighCount;
				}
				const uint16 NewHeight = LandscapeDataAccess::GetTexHeight(NewLocalHeight);
				Stroke.RawHeightMinAfter = FMath::Min(Stroke.RawHeightMinAfter, NewHeight);
				Stroke.RawHeightMaxAfter = FMath::Max(Stroke.RawHeightMaxAfter, NewHeight);
				const float AppliedLocalHeight = LandscapeDataAccess::GetLocalHeight(NewHeight);
				const double AppliedWorldZ = ToWorld.TransformPosition(FVector(LocalX, LocalY, AppliedLocalHeight)).Z;
				Stroke.WorldZMinAfter = FMath::Min(Stroke.WorldZMinAfter, AppliedWorldZ);
				Stroke.WorldZMaxAfter = FMath::Max(Stroke.WorldZMaxAfter, AppliedWorldZ);
				if (NewHeight != CurrentHeight)
				{
					Heights[Index] = NewHeight;
					++Stroke.ChangedCount;
					++TotalChangedCount;
					if (!TouchedVertices[Index])
					{
						TouchedVertices[Index] = true;
						++TouchedVertexCount;
					}
				}
			}

			++Stroke.NextLocalY;
			if (Stroke.NextLocalY > Stroke.Region.Rect.Max.Y)
			{
				if (Stroke.EvaluatedCount == 0)
				{
					Error = FString::Printf(TEXT("strokes[%d] 没有覆盖 Landscape 顶点。"), CurrentStrokeIndex);
					return false;
				}
				++CurrentStrokeIndex;
			}

			double CompletedStrokeUnits = FMath::Min(CurrentStrokeIndex, Strokes.Num());
			if (CurrentStrokeIndex < Strokes.Num())
			{
				const FLandscapeSculptStroke& ActiveStroke = Strokes[CurrentStrokeIndex];
				const int32 StrokeHeight = ActiveStroke.Region.Rect.Max.Y - ActiveStroke.Region.Rect.Min.Y + 1;
				CompletedStrokeUnits += static_cast<double>(ActiveStroke.NextLocalY - ActiveStroke.Region.Rect.Min.Y) / FMath::Max(StrokeHeight, 1);
			}
			const double StrokeFraction = CompletedStrokeUnits / FMath::Max(Strokes.Num(), 1);
			Context.ReportProgress(0.2 + 0.6 * StrokeFraction, TEXT("Computing landscape strokes"));
			return true;
		}

		void ApplyNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!bLandscapeModified)
			{
				Landscape->Modify();
				bLandscapeModified = true;
			}
			const int32 ChunkMaxY = FMath::Min(NextApplyY + RowsPerIoChunk - 1, DirtyRect.Max.Y);
			Edit->SetHeightData(DirtyRect.Min.X, NextApplyY, DirtyRect.Max.X, ChunkMaxY, Heights.GetData() + (NextApplyY - DirtyRect.Min.Y) * Width, Width, true);
			++WriteChunkCount;
			NextApplyY = ChunkMaxY + 1;
			const double ApplyFraction = static_cast<double>(NextApplyY - DirtyRect.Min.Y) / FMath::Max(Height, 1);
			Context.ReportProgress(0.8 + 0.18 * ApplyFraction, TEXT("Applying landscape heights"));
			if (NextApplyY <= DirtyRect.Max.Y)
			{
				return;
			}

			Edit->Flush();
			Edit.Reset();
			Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All_Modes);
			Landscape->MarkPackageDirty();
			Phase = ELandscapeSculptBatchPhase::Complete;
		}

		TSharedRef<FJsonObject> MakeStrokeResult(const FLandscapeSculptStroke& Stroke, const int32 Index) const
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("index"), Index);
			Result->SetStringField(TEXT("mode"), Stroke.Mode);
			Result->SetNumberField(TEXT("amount"), Stroke.Amount);
			if (Stroke.bHasTargetWorldZ)
			{
				Result->SetNumberField(TEXT("targetWorldZ"), Stroke.TargetWorldZ);
			}
			Result->SetNumberField(TEXT("evaluatedVertexCount"), Stroke.EvaluatedCount);
			Result->SetNumberField(TEXT("changedVertexCount"), Stroke.ChangedCount);
			Result->SetNumberField(TEXT("clampedLowVertexCount"), Stroke.ClampedLowCount);
			Result->SetNumberField(TEXT("clampedHighVertexCount"), Stroke.ClampedHighCount);
			Result->SetNumberField(TEXT("clampedVertexCount"), Stroke.ClampedLowCount + Stroke.ClampedHighCount);
			Result->SetNumberField(TEXT("rawHeightMinBefore"), Stroke.RawHeightMinBefore);
			Result->SetNumberField(TEXT("rawHeightMaxBefore"), Stroke.RawHeightMaxBefore);
			Result->SetNumberField(TEXT("rawHeightMinAfter"), Stroke.RawHeightMinAfter);
			Result->SetNumberField(TEXT("rawHeightMaxAfter"), Stroke.RawHeightMaxAfter);
			Result->SetNumberField(TEXT("worldZMinBefore"), Stroke.WorldZMinBefore);
			Result->SetNumberField(TEXT("worldZMaxBefore"), Stroke.WorldZMaxBefore);
			Result->SetNumberField(TEXT("worldZMinAfter"), Stroke.WorldZMinAfter);
			Result->SetNumberField(TEXT("worldZMaxAfter"), Stroke.WorldZMaxAfter);
			return Result;
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
			Result->SetStringField(TEXT("editLayer"), EditLayerName);
			Result->SetNumberField(TEXT("strokeCount"), Strokes.Num());
			Result->SetNumberField(TEXT("evaluatedVertexCount"), TotalEvaluatedCount);
			Result->SetNumberField(TEXT("changedVertexCount"), TotalChangedCount);
			Result->SetNumberField(TEXT("touchedVertexCount"), TouchedVertexCount);
			Result->SetNumberField(TEXT("clampedLowVertexCount"), TotalClampedLowCount);
			Result->SetNumberField(TEXT("clampedHighVertexCount"), TotalClampedHighCount);
			Result->SetNumberField(TEXT("clampedVertexCount"), TotalClampedLowCount + TotalClampedHighCount);
			Result->SetNumberField(TEXT("mergedVertexCount"), static_cast<double>(Width) * Height);
			Result->SetNumberField(TEXT("heightReadPassCount"), 1);
			Result->SetNumberField(TEXT("heightReadChunkCount"), ReadChunkCount);
			Result->SetNumberField(TEXT("heightWritePassCount"), TotalChangedCount > 0 ? 1 : 0);
			Result->SetNumberField(TEXT("heightWriteChunkCount"), WriteChunkCount);
			Result->SetNumberField(TEXT("contentUpdateCount"), TotalChangedCount > 0 ? 1 : 0);

			TArray<TSharedPtr<FJsonValue>> StrokeResults;
			StrokeResults.Reserve(Strokes.Num());
			for (int32 Index = 0; Index < Strokes.Num(); ++Index)
			{
				StrokeResults.Add(MakeShared<FJsonValueObject>(MakeStrokeResult(Strokes[Index], Index)));
			}
			Result->SetArrayField(TEXT("strokeResults"), StrokeResults);
			Edit.Reset();
			Phase = ELandscapeSculptBatchPhase::Complete;
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Reason)
		{
			Edit.Reset();
			if (Reason.IsEmpty())
			{
				Reason = TEXT("sculpt_batch 执行失败。");
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Reason);
			Result.ValueJson = ErrorJson(Reason);
			return Result;
		}

		static constexpr int32 RowsPerIoChunk = 8;
		TSharedRef<FJsonObject> Arguments;
		ELandscapeSculptBatchPhase Phase = ELandscapeSculptBatchPhase::Prepare;
		ALandscapeProxy* Proxy = nullptr;
		ALandscape* Landscape = nullptr;
		ULandscapeInfo* Info = nullptr;
		FGuid EditLayerGuid;
		FString EditLayerName;
		FString Error;
		FTransform ToWorld;
		FVector WorldLocalZ = FVector::UpVector;
		double ScaleZ = 1.0;
		FIntRect DirtyRect;
		int32 Width = 0;
		int32 Height = 0;
		int32 NextReadY = 0;
		int32 NextApplyY = 0;
		int32 NextStrokeToPrepare = 0;
		int32 CurrentStrokeIndex = 0;
		int32 TotalEvaluatedCount = 0;
		int32 TotalChangedCount = 0;
		int32 TouchedVertexCount = 0;
		int32 TotalClampedLowCount = 0;
		int32 TotalClampedHighCount = 0;
		int32 ReadChunkCount = 0;
		int32 WriteChunkCount = 0;
		bool bPreparationInitialized = false;
		bool bLandscapeModified = false;
		TArray<TSharedPtr<FJsonValue>> StrokeValues;
		TArray<FLandscapeSculptStroke> Strokes;
		TArray<uint16> Heights;
		TBitArray<> TouchedVertices;
		TUniquePtr<FLandscapeEditDataInterface> Edit;
	};

	class FLandscapePaintLayerStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLandscapePaintLayerStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("paint_layer 必须在 GameThread 上执行。"));
			}
			if (Phase != EPhase::Prepare && (!IsValid(Proxy) || !IsValid(Info) || !IsValid(Landscape) || !IsValid(LayerInfo)))
			{
				return Fail(TEXT("paint_layer 执行期间目标 Landscape 或图层已失效。"));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			do
			{
				if (Context.ShouldStop())
				{
					return Fail(Context.IsDeadlineExceeded() ? TEXT("Landscape task deadline exceeded.") : Context.GetCancellationReason());
				}

				switch (Phase)
				{
				case EPhase::Prepare:
					if (!Prepare(Context))
					{
						return Fail(Error);
					}
					break;
				case EPhase::Read:
					ReadNextChunk(Context);
					break;
				case EPhase::Compute:
					ComputeNextRow(Context);
					break;
				case EPhase::Apply:
					ApplyNextChunk(Context);
					break;
				case EPhase::Complete:
					return Succeed();
				default:
					return Fail(TEXT("paint_layer 进入未知执行阶段。"));
				}
			} while (FPlatformTime::Seconds() < StopTime);

			return Phase == EPhase::Complete ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			return Fail(Reason.IsEmpty() ? TEXT("paint_layer 已取消。") : MoveTemp(Reason));
		}

	private:
		enum class EPhase : uint8
		{
			Prepare,
			Read,
			Compute,
			Apply,
			Complete
		};

		bool Prepare(Execution::FMcpTaskExecutionContext& Context)
		{
			Info = FUnrealAgentMCPUnrealLandscapeAdapter::ResolveInfo(Arguments, &Proxy, &Error);
			Landscape = Proxy ? Proxy->GetLandscapeActor() : nullptr;
			if (!Proxy || !Info || !Landscape)
			{
				if (Error.IsEmpty())
				{
					Error = TEXT("当前世界中未找到目标 Landscape。");
				}
				return false;
			}

			if (!Arguments->TryGetStringField(TEXT("layerName"), LayerName) || LayerName.IsEmpty())
			{
				Error = TEXT("缺少必填 layerName。");
				return false;
			}
			LayerInfo = FUnrealAgentMCPUnrealLandscapeAdapter::ResolveLayerInfo(Proxy, LayerName);
			if (!LayerInfo)
			{
				Error = FString::Printf(TEXT("目标层未绑定 LayerInfo：%s"), *LayerName);
				return false;
			}

			if (!BuildBrushRegion(Arguments, Proxy, Info, Region, Error))
			{
				return false;
			}
			EditLayerGuid = ResolveEditLayerGuid(Arguments, Landscape, EditLayerName, Error);
			if (!EditLayerGuid.IsValid())
			{
				return false;
			}

			Arguments->TryGetNumberField(TEXT("strength"), Strength);
			Strength = FMath::Clamp(Strength, 0.0, 1.0);
			TargetWeight = static_cast<uint8>(FMath::RoundToInt(Strength * 255.0));
			Width = Region.Rect.Max.X - Region.Rect.Min.X + 1;
			Height = Region.Rect.Max.Y - Region.Rect.Min.Y + 1;
			Weights.SetNumUninitialized(Region.VertexCount);
			Edit = MakeUnique<FLandscapeEditDataInterface>(Info, EditLayerGuid);
			ToWorld = Proxy->LandscapeActorToWorld();
			NextReadY = Region.Rect.Min.Y;
			Phase = EPhase::Read;
			Context.ReportProgress(0.02, TEXT("Prepared landscape paint"));
			return true;
		}

		void ReadNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			const int32 ChunkMaxY = FMath::Min(NextReadY + RowsPerIoChunk - 1, Region.Rect.Max.Y);
			Edit->GetWeightDataFast(LayerInfo, Region.Rect.Min.X, NextReadY, Region.Rect.Max.X, ChunkMaxY, Weights.GetData() + (NextReadY - Region.Rect.Min.Y) * Width, Width);
			NextReadY = ChunkMaxY + 1;
			const double Fraction = static_cast<double>(NextReadY - Region.Rect.Min.Y) / FMath::Max(Height, 1);
			Context.ReportProgress(0.02 + 0.18 * Fraction, TEXT("Reading landscape paint weights"));
			if (NextReadY > Region.Rect.Max.Y)
			{
				NextComputeY = Region.Rect.Min.Y;
				Phase = EPhase::Compute;
			}
		}

		void ComputeNextRow(Execution::FMcpTaskExecutionContext& Context)
		{
			const int32 LocalY = NextComputeY;
			for (int32 LocalX = Region.Rect.Min.X; LocalX <= Region.Rect.Max.X; ++LocalX)
			{
				const int32 Index = (LocalY - Region.Rect.Min.Y) * Width + LocalX - Region.Rect.Min.X;
				const FVector WorldPoint = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0));
				const double Alpha = BrushAlpha(FVector2D(WorldPoint.X, WorldPoint.Y), Region);
				if (Alpha <= 0.0)
				{
					continue;
				}
				++EvaluatedCount;
				const uint8 NewWeight = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<double>(Weights[Index]), static_cast<double>(TargetWeight), Alpha)));
				if (NewWeight != Weights[Index])
				{
					Weights[Index] = NewWeight;
					++ChangedCount;
				}
			}

			++NextComputeY;
			const double Fraction = static_cast<double>(NextComputeY - Region.Rect.Min.Y) / FMath::Max(Height, 1);
			Context.ReportProgress(0.2 + 0.6 * Fraction, TEXT("Computing landscape paint"));
			if (NextComputeY > Region.Rect.Max.Y)
			{
				NextApplyY = Region.Rect.Min.Y;
				Phase = ChangedCount > 0 ? EPhase::Apply : EPhase::Complete;
			}
		}

		void ApplyNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!bLandscapeModified)
			{
				Landscape->Modify();
				bLandscapeModified = true;
			}
			const int32 ChunkMaxY = FMath::Min(NextApplyY + RowsPerIoChunk - 1, Region.Rect.Max.Y);
			Edit->SetAlphaData(LayerInfo, Region.Rect.Min.X, NextApplyY, Region.Rect.Max.X, ChunkMaxY, Weights.GetData() + (NextApplyY - Region.Rect.Min.Y) * Width, Width,
				ELandscapeLayerPaintingRestriction::None);
			NextApplyY = ChunkMaxY + 1;
			const double Fraction = static_cast<double>(NextApplyY - Region.Rect.Min.Y) / FMath::Max(Height, 1);
			Context.ReportProgress(0.8 + 0.18 * Fraction, TEXT("Applying landscape paint"));
			if (NextApplyY <= Region.Rect.Max.Y)
			{
				return;
			}

			Edit->Flush();
			Edit.Reset();
			Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All_Modes);
			Landscape->MarkPackageDirty();
			Phase = EPhase::Complete;
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
			Result->SetStringField(TEXT("layerName"), LayerName);
			Result->SetStringField(TEXT("editLayer"), EditLayerName);
			Result->SetNumberField(TEXT("strength"), Strength);
			Result->SetNumberField(TEXT("evaluatedVertexCount"), EvaluatedCount);
			Result->SetNumberField(TEXT("changedVertexCount"), ChangedCount);
			Result->SetNumberField(TEXT("contentUpdateCount"), ChangedCount > 0 ? 1 : 0);
			Edit.Reset();
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Reason)
		{
			Edit.Reset();
			if (Reason.IsEmpty())
			{
				Reason = TEXT("paint_layer 执行失败。");
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Reason);
			Result.ValueJson = ErrorJson(Reason);
			return Result;
		}

		static constexpr int32 RowsPerIoChunk = 8;
		TSharedRef<FJsonObject> Arguments;
		EPhase Phase = EPhase::Prepare;
		ALandscapeProxy* Proxy = nullptr;
		ALandscape* Landscape = nullptr;
		ULandscapeInfo* Info = nullptr;
		ULandscapeLayerInfoObject* LayerInfo = nullptr;
		FGuid EditLayerGuid;
		FString EditLayerName;
		FString LayerName;
		FString Error;
		FLandscapeBrushRegion Region;
		FTransform ToWorld;
		double Strength = 1.0;
		uint8 TargetWeight = MAX_uint8;
		int32 Width = 0;
		int32 Height = 0;
		int32 NextReadY = 0;
		int32 NextComputeY = 0;
		int32 NextApplyY = 0;
		int32 EvaluatedCount = 0;
		int32 ChangedCount = 0;
		bool bLandscapeModified = false;
		TArray<uint8> Weights;
		TUniquePtr<FLandscapeEditDataInterface> Edit;
	};

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLandscapeAdapter::CreateSculptBatchStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("action"), Action) || Action != TEXT("sculpt_batch"))
		{
			return nullptr;
		}
		return MakeShared<FLandscapeSculptBatchStepper>(Args);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLandscapeAdapter::CreatePaintLayerTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("action"), Action) || Action != TEXT("paint_layer"))
		{
			return nullptr;
		}
		return MakeShared<FLandscapePaintLayerStepper>(Args);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::Sculpt(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = nullptr;
		ULandscapeInfo* Info = ResolveInfo(Args, &Proxy, &Error);
		ALandscape* Landscape = Proxy ? Proxy->GetLandscapeActor() : nullptr;
		if (!Proxy || !Info || !Landscape)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);

		FLandscapeBrushRegion Region;
		if (!BuildBrushRegion(Args, Proxy, Info, Region, Error))
		{
			return ErrorJson(Error);
		}
		FString EditLayerName;
		const FGuid EditLayerGuid = ResolveEditLayerGuid(Args, Landscape, EditLayerName, Error);
		if (!EditLayerGuid.IsValid())
			return ErrorJson(Error);

		FString Mode = TEXT("raise");
		Args->TryGetStringField(TEXT("mode"), Mode);
		Mode.ToLowerInline();
		if (Mode != TEXT("raise") && Mode != TEXT("lower") && Mode != TEXT("flatten") && Mode != TEXT("set"))
		{
			return ErrorJson(TEXT("mode 必须为 raise、lower、flatten 或 set。"));
		}
		double Amount = 100.0;
		Args->TryGetNumberField(TEXT("amount"), Amount);
		if (Amount < 0.0)
			return ErrorJson(TEXT("amount 必须为非负数。"));
		double TargetWorldZ = 0.0;
		const bool bHasTargetWorldZ = Args->TryGetNumberField(TEXT("targetWorldZ"), TargetWorldZ);
		if (Mode == TEXT("set") && !bHasTargetWorldZ)
		{
			return ErrorJson(TEXT("mode=set 时必须提供 targetWorldZ。"));
		}

		const int32 Width = Region.Rect.Max.X - Region.Rect.Min.X + 1;
		const int32 Height = Region.Rect.Max.Y - Region.Rect.Min.Y + 1;
		TArray<uint16> Heights;
		Heights.SetNumUninitialized(Region.VertexCount);
		FLandscapeEditDataInterface Edit(Info, EditLayerGuid);
		Edit.GetHeightDataFast(Region.Rect.Min.X, Region.Rect.Min.Y, Region.Rect.Max.X, Region.Rect.Max.Y, Heights.GetData(), Width);
		if (ShouldStopLandscapeTask(Error))
		{
			return ErrorJson(Error);
		}
		ReportLandscapeTaskProgress(0.2, TEXT("Preparing landscape sculpt"));
		const FTransform ToWorld = Proxy->LandscapeActorToWorld();
		const double ScaleZ = FMath::Max(static_cast<double>(FMath::Abs(ToWorld.GetScale3D().Z)), UE_DOUBLE_SMALL_NUMBER);
		const double LocalAmount = Amount / ScaleZ;
		const int32 CenterLocalX = FMath::Clamp(FMath::RoundToInt(ToWorld.InverseTransformPosition(FVector(Region.Center.X, Region.Center.Y, Proxy->GetActorLocation().Z)).X),
			Region.Rect.Min.X, Region.Rect.Max.X);
		const int32 CenterLocalY = FMath::Clamp(FMath::RoundToInt(ToWorld.InverseTransformPosition(FVector(Region.Center.X, Region.Center.Y, Proxy->GetActorLocation().Z)).Y),
			Region.Rect.Min.Y, Region.Rect.Max.Y);
		const uint16 FlattenHeight = Heights[(CenterLocalY - Region.Rect.Min.Y) * Width + CenterLocalX - Region.Rect.Min.X];
		const float MinLocalHeight = LandscapeDataAccess::GetLocalHeight(0);
		const float MaxLocalHeight = LandscapeDataAccess::GetLocalHeight(MAX_uint16);
		const FVector WorldLocalZ = ToWorld.TransformVector(FVector::UpVector);
		if (bHasTargetWorldZ && FMath::IsNearlyZero(WorldLocalZ.Z))
		{
			return ErrorJson(TEXT("Landscape 变换无法把 targetWorldZ 映射到局部高度。"));
		}

		int32 ChangedCount = 0;
		int32 EvaluatedCount = 0;
		int32 ClampedLowCount = 0;
		int32 ClampedHighCount = 0;
		uint16 RawHeightMinBefore = MAX_uint16;
		uint16 RawHeightMaxBefore = 0;
		uint16 RawHeightMinAfter = MAX_uint16;
		uint16 RawHeightMaxAfter = 0;
		double WorldZMinBefore = TNumericLimits<double>::Max();
		double WorldZMaxBefore = TNumericLimits<double>::Lowest();
		double WorldZMinAfter = TNumericLimits<double>::Max();
		double WorldZMaxAfter = TNumericLimits<double>::Lowest();
		for (int32 LocalY = Region.Rect.Min.Y; LocalY <= Region.Rect.Max.Y; ++LocalY)
		{
			const int32 RowIndex = LocalY - Region.Rect.Min.Y;
			if ((RowIndex & 31) == 0)
			{
				if (ShouldStopLandscapeTask(Error))
				{
					return ErrorJson(Error);
				}
				ReportLandscapeTaskProgress(0.2 + 0.6 * static_cast<double>(RowIndex) / FMath::Max(Height, 1), TEXT("Computing landscape sculpt"));
			}
			for (int32 LocalX = Region.Rect.Min.X; LocalX <= Region.Rect.Max.X; ++LocalX)
			{
				const int32 Index = (LocalY - Region.Rect.Min.Y) * Width + LocalX - Region.Rect.Min.X;
				const FVector WorldPoint = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0));
				const double Alpha = BrushAlpha(FVector2D(WorldPoint.X, WorldPoint.Y), Region);
				if (Alpha <= 0.0)
					continue;
				++EvaluatedCount;
				const float CurrentLocalHeight = LandscapeDataAccess::GetLocalHeight(Heights[Index]);
				RawHeightMinBefore = FMath::Min(RawHeightMinBefore, Heights[Index]);
				RawHeightMaxBefore = FMath::Max(RawHeightMaxBefore, Heights[Index]);
				const double CurrentWorldZ = ToWorld.TransformPosition(FVector(LocalX, LocalY, CurrentLocalHeight)).Z;
				WorldZMinBefore = FMath::Min(WorldZMinBefore, CurrentWorldZ);
				WorldZMaxBefore = FMath::Max(WorldZMaxBefore, CurrentWorldZ);
				float NewLocalHeight = CurrentLocalHeight;
				if (Mode == TEXT("raise"))
				{
					NewLocalHeight += static_cast<float>(LocalAmount * Alpha);
				}
				else if (Mode == TEXT("lower"))
				{
					NewLocalHeight -= static_cast<float>(LocalAmount * Alpha);
				}
				else
				{
					float Target = LandscapeDataAccess::GetLocalHeight(FlattenHeight);
					if (bHasTargetWorldZ)
					{
						const double WorldZAtLocalZero = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0)).Z;
						Target = static_cast<float>((TargetWorldZ - WorldZAtLocalZero) / WorldLocalZ.Z);
					}
					NewLocalHeight = FMath::Lerp(CurrentLocalHeight, Target, static_cast<float>(Alpha));
				}
				if (NewLocalHeight < MinLocalHeight)
					++ClampedLowCount;
				else if (NewLocalHeight > MaxLocalHeight)
					++ClampedHighCount;
				const uint16 NewHeight = LandscapeDataAccess::GetTexHeight(NewLocalHeight);
				RawHeightMinAfter = FMath::Min(RawHeightMinAfter, NewHeight);
				RawHeightMaxAfter = FMath::Max(RawHeightMaxAfter, NewHeight);
				const float AppliedLocalHeight = LandscapeDataAccess::GetLocalHeight(NewHeight);
				const double AppliedWorldZ = ToWorld.TransformPosition(FVector(LocalX, LocalY, AppliedLocalHeight)).Z;
				WorldZMinAfter = FMath::Min(WorldZMinAfter, AppliedWorldZ);
				WorldZMaxAfter = FMath::Max(WorldZMaxAfter, AppliedWorldZ);
				if (NewHeight != Heights[Index])
				{
					Heights[Index] = NewHeight;
					++ChangedCount;
				}
			}
		}
		if (EvaluatedCount == 0)
		{
			return ErrorJson(TEXT("笔刷没有覆盖 Landscape 顶点。"));
		}
		if (ShouldStopLandscapeTask(Error))
		{
			return ErrorJson(Error);
		}
		const Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "LandscapeSculpt", "Unreal Agent 地形雕刻"));
		Landscape->Modify();
		ReportLandscapeTaskProgress(0.85, TEXT("Applying landscape sculpt"));
		Edit.SetHeightData(Region.Rect.Min.X, Region.Rect.Min.Y, Region.Rect.Max.X, Region.Rect.Max.Y, Heights.GetData(), Width, true);
		Edit.Flush();
		Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All_Modes);
		Landscape->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetStringField(TEXT("mode"), Mode);
		Result->SetStringField(TEXT("editLayer"), EditLayerName);
		Result->SetNumberField(TEXT("amount"), Amount);
		if (bHasTargetWorldZ)
		{
			Result->SetNumberField(TEXT("targetWorldZ"), TargetWorldZ);
		}
		Result->SetNumberField(TEXT("evaluatedVertexCount"), EvaluatedCount);
		Result->SetNumberField(TEXT("changedVertexCount"), ChangedCount);
		Result->SetNumberField(TEXT("clampedLowVertexCount"), ClampedLowCount);
		Result->SetNumberField(TEXT("clampedHighVertexCount"), ClampedHighCount);
		Result->SetNumberField(TEXT("clampedVertexCount"), ClampedLowCount + ClampedHighCount);
		Result->SetNumberField(TEXT("rawHeightMinBefore"), RawHeightMinBefore);
		Result->SetNumberField(TEXT("rawHeightMaxBefore"), RawHeightMaxBefore);
		Result->SetNumberField(TEXT("rawHeightMinAfter"), RawHeightMinAfter);
		Result->SetNumberField(TEXT("rawHeightMaxAfter"), RawHeightMaxAfter);
		Result->SetNumberField(TEXT("worldZMinBefore"), WorldZMinBefore);
		Result->SetNumberField(TEXT("worldZMaxBefore"), WorldZMaxBefore);
		Result->SetNumberField(TEXT("worldZMinAfter"), WorldZMinAfter);
		Result->SetNumberField(TEXT("worldZMaxAfter"), WorldZMaxAfter);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::PaintLayer(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = nullptr;
		ULandscapeInfo* Info = ResolveInfo(Args, &Proxy, &Error);
		ALandscape* Landscape = Proxy ? Proxy->GetLandscapeActor() : nullptr;
		if (!Proxy || !Info || !Landscape)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		FString LayerName;
		if (!Args->TryGetStringField(TEXT("layerName"), LayerName) || LayerName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 layerName。"));
		}
		ULandscapeLayerInfoObject* LayerInfo = ResolveLayerInfo(Proxy, LayerName);
		if (!LayerInfo)
		{
			return ErrorJson(FString::Printf(TEXT("目标层未绑定 LayerInfo：%s"), *LayerName));
		}

		FLandscapeBrushRegion Region;
		if (!BuildBrushRegion(Args, Proxy, Info, Region, Error))
		{
			return ErrorJson(Error);
		}
		FString EditLayerName;
		const FGuid EditLayerGuid = ResolveEditLayerGuid(Args, Landscape, EditLayerName, Error);
		if (!EditLayerGuid.IsValid())
			return ErrorJson(Error);
		double Strength = 1.0;
		Args->TryGetNumberField(TEXT("strength"), Strength);
		Strength = FMath::Clamp(Strength, 0.0, 1.0);

		const int32 Width = Region.Rect.Max.X - Region.Rect.Min.X + 1;
		const int32 Height = Region.Rect.Max.Y - Region.Rect.Min.Y + 1;
		TArray<uint8> Weights;
		Weights.SetNumUninitialized(Region.VertexCount);
		FLandscapeEditDataInterface Edit(Info, EditLayerGuid);
		Edit.GetWeightDataFast(LayerInfo, Region.Rect.Min.X, Region.Rect.Min.Y, Region.Rect.Max.X, Region.Rect.Max.Y, Weights.GetData(), Width);
		if (ShouldStopLandscapeTask(Error))
		{
			return ErrorJson(Error);
		}
		ReportLandscapeTaskProgress(0.2, TEXT("Preparing landscape paint"));
		const FTransform ToWorld = Proxy->LandscapeActorToWorld();
		const uint8 TargetWeight = static_cast<uint8>(FMath::RoundToInt(Strength * 255.0));
		int32 ChangedCount = 0;
		for (int32 LocalY = Region.Rect.Min.Y; LocalY <= Region.Rect.Max.Y; ++LocalY)
		{
			const int32 RowIndex = LocalY - Region.Rect.Min.Y;
			if ((RowIndex & 31) == 0)
			{
				if (ShouldStopLandscapeTask(Error))
				{
					return ErrorJson(Error);
				}
				ReportLandscapeTaskProgress(0.2 + 0.6 * static_cast<double>(RowIndex) / FMath::Max(Height, 1), TEXT("Computing landscape paint"));
			}
			for (int32 LocalX = Region.Rect.Min.X; LocalX <= Region.Rect.Max.X; ++LocalX)
			{
				const int32 Index = (LocalY - Region.Rect.Min.Y) * Width + LocalX - Region.Rect.Min.X;
				const FVector WorldPoint = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0));
				const double Alpha = BrushAlpha(FVector2D(WorldPoint.X, WorldPoint.Y), Region);
				if (Alpha <= 0.0)
					continue;
				const uint8 NewWeight = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<double>(Weights[Index]), static_cast<double>(TargetWeight), Alpha)));
				if (NewWeight != Weights[Index])
				{
					Weights[Index] = NewWeight;
					++ChangedCount;
				}
			}
		}
		if (ShouldStopLandscapeTask(Error))
		{
			return ErrorJson(Error);
		}
		const Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "LandscapePaintLayer", "Unreal Agent 地形图层绘制"));
		Landscape->Modify();
		ReportLandscapeTaskProgress(0.85, TEXT("Applying landscape paint"));
		Edit.SetAlphaData(LayerInfo, Region.Rect.Min.X, Region.Rect.Min.Y, Region.Rect.Max.X, Region.Rect.Max.Y, Weights.GetData(), Width,
			ELandscapeLayerPaintingRestriction::None);
		Edit.Flush();
		Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All_Modes);
		Landscape->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetStringField(TEXT("layerName"), LayerName);
		Result->SetStringField(TEXT("editLayer"), EditLayerName);
		Result->SetNumberField(TEXT("strength"), Strength);
		Result->SetNumberField(TEXT("evaluatedVertexCount"), Region.VertexCount);
		Result->SetNumberField(TEXT("changedVertexCount"), ChangedCount);
		return SuccessJson(Result);
	}
}
