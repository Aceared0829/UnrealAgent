// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.Heightmap.cpp
 * @brief Landscape 矩形高度写入、外部高度图导入与高度重置的可恢复任务实现。
 */

#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"

#include "Async/Async.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"

namespace UnrealAgentMCP
{
	namespace
	{
		enum class ELandscapeHeightWritePhase : uint8
		{
			Resolve,
			AwaitSource,
			Convert,
			Apply,
			Complete
		};

		enum class ELandscapeHeightSource : uint8
		{
			RawJson,
			WorldJson,
			UniformRaw,
			UniformWorld,
			ImportedRaw
		};

		struct FLandscapeHeightmapDecodeRequest
		{
			FString FilePath;
			FString Base64;
			FString ByteOrder = TEXT("little_endian");
			int32 VertexCount = 0;
		};

		struct FLandscapeHeightmapDecodeResult
		{
			TArray<uint16> Heights;
			FString Error;

			bool IsValid() const
			{
				return Error.IsEmpty();
			}
		};

		bool TryReadIntPoint(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FIntPoint& OutPoint)
		{
			const TSharedPtr<FJsonObject>* Point = nullptr;
			if (!Args->TryGetObjectField(FieldName, Point) || !Point || !Point->IsValid())
			{
				return false;
			}
			double X = 0.0;
			double Y = 0.0;
			if (!(*Point)->TryGetNumberField(TEXT("x"), X) || !(*Point)->TryGetNumberField(TEXT("y"), Y) || !FMath::IsNearlyEqual(X, FMath::RoundToDouble(X)) ||
				!FMath::IsNearlyEqual(Y, FMath::RoundToDouble(Y)))
			{
				return false;
			}
			OutPoint = FIntPoint(FMath::RoundToInt(X), FMath::RoundToInt(Y));
			return true;
		}

		FGuid ResolveHeightEditLayerGuid(const TSharedPtr<FJsonObject>& Args, ALandscape* Landscape, FString& OutLayerName, FString& OutError)
		{
			if (!Landscape)
			{
				OutError = TEXT("目标 Landscape 主 Actor 不可用。");
				return FGuid();
			}
			if (Landscape->GetLayersConst().IsEmpty())
			{
				Landscape->CreateDefaultLayer();
			}

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

		FLandscapeHeightmapDecodeResult DecodeHeightmap(FLandscapeHeightmapDecodeRequest Request)
		{
			FLandscapeHeightmapDecodeResult Result;
			const int64 ExpectedBytes = static_cast<int64>(Request.VertexCount) * 2;
			TArray<uint8> Bytes;
			if (!Request.FilePath.IsEmpty())
			{
				const int64 FileSize = IFileManager::Get().FileSize(*Request.FilePath);
				if (FileSize != ExpectedBytes)
				{
					Result.Error = FString::Printf(TEXT("高度图文件大小必须为 %lld 字节，实际为 %lld 字节。"), ExpectedBytes, FileSize);
					return Result;
				}
				if (!FFileHelper::LoadFileToArray(Bytes, *Request.FilePath))
				{
					Result.Error = FString::Printf(TEXT("无法读取高度图文件：%s"), *Request.FilePath);
					return Result;
				}
			}
			else if (!FBase64::Decode(Request.Base64, Bytes))
			{
				Result.Error = TEXT("base64 不是有效的高度图数据。");
				return Result;
			}

			if (Bytes.Num() != ExpectedBytes)
			{
				Result.Error = FString::Printf(TEXT("解码后的高度图必须为 %lld 字节，实际为 %d 字节。"), ExpectedBytes, Bytes.Num());
				return Result;
			}
			const bool bLittleEndian = Request.ByteOrder == TEXT("little_endian");
			Result.Heights.SetNumUninitialized(Request.VertexCount);
			for (int32 Index = 0; Index < Request.VertexCount; ++Index)
			{
				const uint8 First = Bytes[Index * 2];
				const uint8 Second = Bytes[Index * 2 + 1];
				Result.Heights[Index] = bLittleEndian ? static_cast<uint16>(First | (Second << 8)) : static_cast<uint16>((First << 8) | Second);
			}
			return Result;
		}
	}

	class FLandscapeHeightWriteStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLandscapeHeightWriteStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
			Arguments->TryGetStringField(TEXT("action"), Action);
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("Landscape 高度写入必须在 GameThread 上执行。"));
			}
			if (Phase != ELandscapeHeightWritePhase::Resolve && (!IsValid(Proxy) || !IsValid(Info) || !IsValid(Landscape)))
			{
				return Fail(TEXT("高度写入期间目标 Landscape 已失效。"));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
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
				case ELandscapeHeightWritePhase::Resolve:
					if (!ResolveRequest(Context))
					{
						return Fail(Error);
					}
					break;
				case ELandscapeHeightWritePhase::AwaitSource:
					if (!DecodeFuture.IsReady())
					{
						Context.ReportProgress(0.1, TEXT("Decoding landscape heightmap"));
						return Execution::FMcpTaskStepResult::Continue();
					}
					if (!AcceptDecodedSource())
					{
						return Fail(Error);
					}
					break;
				case ELandscapeHeightWritePhase::Convert:
					if (!ConvertNextChunk(Context))
					{
						return Fail(Error);
					}
					break;
				case ELandscapeHeightWritePhase::Apply:
					ApplyNextChunk(Context);
					break;
				case ELandscapeHeightWritePhase::Complete:
					return Succeed();
				default:
					return Fail(TEXT("Landscape 高度写入进入未知阶段。"));
				}
				bAdvanced = true;
			} while (FPlatformTime::Seconds() < StopTime || !bAdvanced);

			return Phase == ELandscapeHeightWritePhase::Complete ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			return Fail(Reason.IsEmpty() ? TEXT("Landscape 高度写入已取消。") : MoveTemp(Reason));
		}

	private:
		bool ResolveRequest(Execution::FMcpTaskExecutionContext& Context)
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

			EditLayerGuid = ResolveHeightEditLayerGuid(Arguments, Landscape, EditLayerName, Error);
			if (!EditLayerGuid.IsValid())
			{
				return false;
			}
			if (!Info->GetLandscapeExtent(LandscapeMin.X, LandscapeMin.Y, LandscapeMax.X, LandscapeMax.Y))
			{
				Error = TEXT("Landscape 没有可写入范围。");
				return false;
			}

			ToWorld = Proxy->LandscapeActorToWorld();
			WorldLocalZ = ToWorld.TransformVector(FVector::UpVector);
			if (Action == TEXT("set_height_rect"))
			{
				return ResolveSetHeightRect(Context);
			}
			if (Action == TEXT("import_heightmap"))
			{
				return ResolveImportHeightmap(Context);
			}
			if (Action == TEXT("reset_heights"))
			{
				return ResolveResetHeights(Context);
			}
			Error = FString::Printf(TEXT("不支持的高度写入 action：%s"), *Action);
			return false;
		}

		bool ResolveSetHeightRect(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!TryReadIntPoint(Arguments, TEXT("min"), Rect.Min) || !TryReadIntPoint(Arguments, TEXT("max"), Rect.Max))
			{
				Error = TEXT("min 和 max 必须包含整数 x、y Landscape 顶点坐标。");
				return false;
			}
			if (!ValidateRect())
			{
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* RawValues = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* WorldValues = nullptr;
			const bool bHasRaw = Arguments->TryGetArrayField(TEXT("rawHeights"), RawValues) && RawValues;
			const bool bHasWorld = Arguments->TryGetArrayField(TEXT("worldZValues"), WorldValues) && WorldValues;
			if (bHasRaw == bHasWorld)
			{
				Error = TEXT("rawHeights 和 worldZValues 必须且只能提供一个。");
				return false;
			}
			SourceValues = bHasRaw ? RawValues : WorldValues;
			if (SourceValues->Num() != VertexCount)
			{
				Error = FString::Printf(TEXT("高度数组数量必须为 %d，实际为 %d。"), VertexCount, SourceValues->Num());
				return false;
			}
			Source = bHasRaw ? ELandscapeHeightSource::RawJson : ELandscapeHeightSource::WorldJson;
			if (Source == ELandscapeHeightSource::WorldJson && !ValidateWorldHeightTransform())
			{
				return false;
			}
			Heights.SetNumUninitialized(VertexCount);
			Phase = ELandscapeHeightWritePhase::Convert;
			Context.ReportProgress(0.05, TEXT("Validated landscape height rectangle"));
			return true;
		}

		bool ResolveImportHeightmap(Execution::FMcpTaskExecutionContext& Context)
		{
			double WidthNumber = 0.0;
			double HeightNumber = 0.0;
			if (!Arguments->TryGetNumberField(TEXT("width"), WidthNumber) || !Arguments->TryGetNumberField(TEXT("height"), HeightNumber) ||
				!FMath::IsNearlyEqual(WidthNumber, FMath::RoundToDouble(WidthNumber)) || !FMath::IsNearlyEqual(HeightNumber, FMath::RoundToDouble(HeightNumber)) ||
				WidthNumber <= 0.0 || HeightNumber <= 0.0 || WidthNumber > 4000000.0 || HeightNumber > 4000000.0)
			{
				Error = TEXT("width 和 height 必须为 1 到 4000000 的整数。");
				return false;
			}
			FIntPoint Origin = LandscapeMin;
			const TSharedPtr<FJsonObject>* Ignored = nullptr;
			if (Arguments->TryGetObjectField(TEXT("min"), Ignored) && !TryReadIntPoint(Arguments, TEXT("min"), Origin))
			{
				Error = TEXT("min 必须包含整数 x、y Landscape 顶点坐标。");
				return false;
			}
			const int32 ImportWidth = FMath::RoundToInt(WidthNumber);
			const int32 ImportHeight = FMath::RoundToInt(HeightNumber);
			const int64 MaxX = static_cast<int64>(Origin.X) + ImportWidth - 1;
			const int64 MaxY = static_cast<int64>(Origin.Y) + ImportHeight - 1;
			if (MaxX > MAX_int32 || MaxY > MAX_int32)
			{
				Error = TEXT("高度图写入范围超过整数坐标上限。");
				return false;
			}
			Rect.Min = Origin;
			Rect.Max = FIntPoint(static_cast<int32>(MaxX), static_cast<int32>(MaxY));
			if (!ValidateRect())
			{
				return false;
			}

			FLandscapeHeightmapDecodeRequest Request;
			Arguments->TryGetStringField(TEXT("filePath"), Request.FilePath);
			Arguments->TryGetStringField(TEXT("base64"), Request.Base64);
			Arguments->TryGetStringField(TEXT("byteOrder"), Request.ByteOrder);
			Request.ByteOrder.ToLowerInline();
			if (Request.ByteOrder.IsEmpty())
			{
				Request.ByteOrder = TEXT("little_endian");
			}
			if ((Request.FilePath.IsEmpty() && Request.Base64.IsEmpty()) || (!Request.FilePath.IsEmpty() && !Request.Base64.IsEmpty()))
			{
				Error = TEXT("filePath 和 base64 必须且只能提供一个。");
				return false;
			}
			if (Request.ByteOrder != TEXT("little_endian") && Request.ByteOrder != TEXT("big_endian"))
			{
				Error = TEXT("byteOrder 必须为 little_endian 或 big_endian。");
				return false;
			}
			Request.VertexCount = VertexCount;
			DecodeFuture = Async(EAsyncExecution::ThreadPool,
				[Request = MoveTemp(Request)]() mutable
				{
					return DecodeHeightmap(MoveTemp(Request));
				});
			Source = ELandscapeHeightSource::ImportedRaw;
			Phase = ELandscapeHeightWritePhase::AwaitSource;
			Context.ReportProgress(0.05, TEXT("Queued landscape heightmap decode"));
			return true;
		}

		bool ResolveResetHeights(Execution::FMcpTaskExecutionContext& Context)
		{
			Rect.Min = LandscapeMin;
			Rect.Max = LandscapeMax;
			if (!ValidateRect())
			{
				return false;
			}
			double RawHeightNumber = 32768.0;
			const bool bHasRaw = Arguments->TryGetNumberField(TEXT("rawHeight"), RawHeightNumber);
			const bool bHasWorld = Arguments->TryGetNumberField(TEXT("targetWorldZ"), UniformWorldZ);
			if (bHasRaw && bHasWorld)
			{
				Error = TEXT("rawHeight 和 targetWorldZ 不能同时提供。");
				return false;
			}
			if (bHasWorld)
			{
				if (!ValidateWorldHeightTransform())
				{
					return false;
				}
				Source = ELandscapeHeightSource::UniformWorld;
			}
			else
			{
				if (RawHeightNumber < 0.0 || RawHeightNumber > MAX_uint16 || !FMath::IsNearlyEqual(RawHeightNumber, FMath::RoundToDouble(RawHeightNumber)))
				{
					Error = TEXT("rawHeight 必须为 0 到 65535 的整数。");
					return false;
				}
				UniformRawHeight = static_cast<uint16>(FMath::RoundToInt(RawHeightNumber));
				Source = ELandscapeHeightSource::UniformRaw;
			}
			Heights.SetNumUninitialized(VertexCount);
			Phase = ELandscapeHeightWritePhase::Convert;
			Context.ReportProgress(0.05, TEXT("Prepared landscape height reset"));
			return true;
		}

		bool ValidateRect()
		{
			if (Rect.Min.X > Rect.Max.X || Rect.Min.Y > Rect.Max.Y || Rect.Min.X < LandscapeMin.X || Rect.Min.Y < LandscapeMin.Y || Rect.Max.X > LandscapeMax.X ||
				Rect.Max.Y > LandscapeMax.Y)
			{
				Error = FString::Printf(TEXT("写入范围 [%d,%d]-[%d,%d] 超出 Landscape [%d,%d]-[%d,%d]。"), Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y, LandscapeMin.X,
					LandscapeMin.Y, LandscapeMax.X, LandscapeMax.Y);
				return false;
			}
			Width = Rect.Max.X - Rect.Min.X + 1;
			Height = Rect.Max.Y - Rect.Min.Y + 1;
			const int64 Count = static_cast<int64>(Width) * Height;
			double MaxVerticesNumber = 4000000.0;
			Arguments->TryGetNumberField(TEXT("maxVertices"), MaxVerticesNumber);
			const int64 MaxVertices = FMath::Clamp<int64>(static_cast<int64>(MaxVerticesNumber), 1, 4000000);
			if (Count > MaxVertices)
			{
				Error = FString::Printf(TEXT("高度写入包含 %lld 个顶点，超过 maxVertices=%lld。"), Count, MaxVertices);
				return false;
			}
			VertexCount = static_cast<int32>(Count);
			return true;
		}

		bool ValidateWorldHeightTransform()
		{
			if (!FMath::IsNearlyZero(WorldLocalZ.Z))
			{
				return true;
			}
			Error = TEXT("Landscape 变换无法把世界 Z 映射到局部高度。");
			return false;
		}

		bool AcceptDecodedSource()
		{
			FLandscapeHeightmapDecodeResult Decoded = DecodeFuture.Get();
			if (!Decoded.IsValid())
			{
				Error = MoveTemp(Decoded.Error);
				return false;
			}
			Heights = MoveTemp(Decoded.Heights);
			if (Heights.Num() != VertexCount)
			{
				Error = TEXT("高度图解码结果尺寸不一致。");
				return false;
			}
			Phase = ELandscapeHeightWritePhase::Convert;
			return true;
		}

		bool ConvertNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			if (NextValueIndex >= VertexCount)
			{
				Edit = MakeUnique<FLandscapeEditDataInterface>(Info, EditLayerGuid);
				NextApplyY = Rect.Min.Y;
				Phase = ELandscapeHeightWritePhase::Apply;
				return true;
			}

			const int32 EndIndex = FMath::Min(NextValueIndex + ValuesPerChunk, VertexCount);
			for (; NextValueIndex < EndIndex; ++NextValueIndex)
			{
				uint16 RawHeight = 0;
				if (Source == ELandscapeHeightSource::RawJson)
				{
					if (!TryReadRawJsonHeight(NextValueIndex, RawHeight))
					{
						return false;
					}
					Heights[NextValueIndex] = RawHeight;
				}
				else if (Source == ELandscapeHeightSource::WorldJson)
				{
					if (!TryReadWorldJsonHeight(NextValueIndex, RawHeight))
					{
						return false;
					}
					Heights[NextValueIndex] = RawHeight;
				}
				else if (Source == ELandscapeHeightSource::UniformRaw)
				{
					RawHeight = UniformRawHeight;
					Heights[NextValueIndex] = RawHeight;
				}
				else if (Source == ELandscapeHeightSource::UniformWorld)
				{
					RawHeight = WorldZToRawHeight(NextValueIndex, UniformWorldZ);
					Heights[NextValueIndex] = RawHeight;
				}
				else
				{
					RawHeight = Heights[NextValueIndex];
				}
				UpdateMetrics(NextValueIndex, RawHeight);
			}
			Context.ReportProgress(0.1 + 0.55 * static_cast<double>(NextValueIndex) / FMath::Max(VertexCount, 1), TEXT("Converting landscape heights"));
			return true;
		}

		bool TryReadRawJsonHeight(const int32 Index, uint16& OutHeight)
		{
			const TSharedPtr<FJsonValue>& Value = (*SourceValues)[Index];
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				Error = FString::Printf(TEXT("rawHeights[%d] 必须为数字。"), Index);
				return false;
			}
			const double Number = Value->AsNumber();
			if (Number < 0.0 || Number > MAX_uint16 || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
			{
				Error = FString::Printf(TEXT("rawHeights[%d] 必须为 0 到 65535 的整数。"), Index);
				return false;
			}
			OutHeight = static_cast<uint16>(FMath::RoundToInt(Number));
			return true;
		}

		bool TryReadWorldJsonHeight(const int32 Index, uint16& OutHeight)
		{
			const TSharedPtr<FJsonValue>& Value = (*SourceValues)[Index];
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				Error = FString::Printf(TEXT("worldZValues[%d] 必须为数字。"), Index);
				return false;
			}
			OutHeight = WorldZToRawHeight(Index, Value->AsNumber());
			return true;
		}

		uint16 WorldZToRawHeight(const int32 Index, const double WorldZ)
		{
			if (FMath::IsNearlyZero(WorldLocalZ.Z))
			{
				Error = TEXT("Landscape 变换无法把世界 Z 映射到局部高度。");
				return 0;
			}
			const int32 LocalX = Rect.Min.X + Index % Width;
			const int32 LocalY = Rect.Min.Y + Index / Width;
			const double WorldZAtLocalZero = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0)).Z;
			const float LocalHeight = static_cast<float>((WorldZ - WorldZAtLocalZero) / WorldLocalZ.Z);
			const float MinLocalHeight = LandscapeDataAccess::GetLocalHeight(0);
			const float MaxLocalHeight = LandscapeDataAccess::GetLocalHeight(MAX_uint16);
			if (LocalHeight < MinLocalHeight)
			{
				++ClampedLowCount;
			}
			else if (LocalHeight > MaxLocalHeight)
			{
				++ClampedHighCount;
			}
			return LandscapeDataAccess::GetTexHeight(LocalHeight);
		}

		void UpdateMetrics(const int32 Index, const uint16 RawHeight)
		{
			RawHeightMin = FMath::Min(RawHeightMin, RawHeight);
			RawHeightMax = FMath::Max(RawHeightMax, RawHeight);
			const int32 LocalX = Rect.Min.X + Index % Width;
			const int32 LocalY = Rect.Min.Y + Index / Width;
			const double WorldZ = ToWorld.TransformPosition(FVector(LocalX, LocalY, LandscapeDataAccess::GetLocalHeight(RawHeight))).Z;
			WorldZMin = FMath::Min(WorldZMin, WorldZ);
			WorldZMax = FMath::Max(WorldZMax, WorldZ);
		}

		void ApplyNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!bLandscapeModified)
			{
				Landscape->Modify();
				bLandscapeModified = true;
			}
			const int32 ChunkMaxY = FMath::Min(NextApplyY + RowsPerIoChunk - 1, Rect.Max.Y);
			Edit->SetHeightData(Rect.Min.X, NextApplyY, Rect.Max.X, ChunkMaxY, Heights.GetData() + (NextApplyY - Rect.Min.Y) * Width, Width, true);
			++WriteChunkCount;
			NextApplyY = ChunkMaxY + 1;
			Context.ReportProgress(0.65 + 0.33 * static_cast<double>(NextApplyY - Rect.Min.Y) / FMath::Max(Height, 1), TEXT("Applying landscape height rectangle"));
			if (NextApplyY <= Rect.Max.Y)
			{
				return;
			}

			Edit->Flush();
			Edit.Reset();
			Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All_Modes);
			Landscape->MarkPackageDirty();
			Phase = ELandscapeHeightWritePhase::Complete;
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
			Result->SetStringField(TEXT("editLayer"), EditLayerName);
			Result->SetNumberField(TEXT("vertexCount"), VertexCount);
			Result->SetNumberField(TEXT("rawHeightMin"), RawHeightMin);
			Result->SetNumberField(TEXT("rawHeightMax"), RawHeightMax);
			Result->SetNumberField(TEXT("worldZMin"), WorldZMin);
			Result->SetNumberField(TEXT("worldZMax"), WorldZMax);
			Result->SetNumberField(TEXT("clampedLowVertexCount"), ClampedLowCount);
			Result->SetNumberField(TEXT("clampedHighVertexCount"), ClampedHighCount);
			Result->SetNumberField(TEXT("clampedVertexCount"), ClampedLowCount + ClampedHighCount);
			Result->SetNumberField(TEXT("heightWritePassCount"), 1);
			Result->SetNumberField(TEXT("heightWriteChunkCount"), WriteChunkCount);
			Result->SetNumberField(TEXT("contentUpdateCount"), 1);
			TSharedRef<FJsonObject> RectJson = MakeShared<FJsonObject>();
			RectJson->SetNumberField(TEXT("minX"), Rect.Min.X);
			RectJson->SetNumberField(TEXT("minY"), Rect.Min.Y);
			RectJson->SetNumberField(TEXT("maxX"), Rect.Max.X);
			RectJson->SetNumberField(TEXT("maxY"), Rect.Max.Y);
			Result->SetObjectField(TEXT("rect"), RectJson);
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Reason)
		{
			Edit.Reset();
			if (Reason.IsEmpty())
			{
				Reason = Error.IsEmpty() ? TEXT("Landscape 高度写入失败。") : Error;
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Reason);
			Result.ValueJson = ErrorJson(Reason);
			return Result;
		}

		static constexpr int32 ValuesPerChunk = 1024;
		static constexpr int32 RowsPerIoChunk = 8;
		TSharedRef<FJsonObject> Arguments;
		FString Action;
		FString Error;
		ELandscapeHeightWritePhase Phase = ELandscapeHeightWritePhase::Resolve;
		ELandscapeHeightSource Source = ELandscapeHeightSource::RawJson;
		ALandscapeProxy* Proxy = nullptr;
		ALandscape* Landscape = nullptr;
		ULandscapeInfo* Info = nullptr;
		FGuid EditLayerGuid;
		FString EditLayerName;
		FIntPoint LandscapeMin;
		FIntPoint LandscapeMax;
		FIntRect Rect;
		int32 Width = 0;
		int32 Height = 0;
		int32 VertexCount = 0;
		int32 NextValueIndex = 0;
		int32 NextApplyY = 0;
		int32 WriteChunkCount = 0;
		int32 ClampedLowCount = 0;
		int32 ClampedHighCount = 0;
		uint16 UniformRawHeight = 32768;
		uint16 RawHeightMin = MAX_uint16;
		uint16 RawHeightMax = 0;
		double UniformWorldZ = 0.0;
		double WorldZMin = TNumericLimits<double>::Max();
		double WorldZMax = TNumericLimits<double>::Lowest();
		bool bLandscapeModified = false;
		FTransform ToWorld;
		FVector WorldLocalZ = FVector::UpVector;
		const TArray<TSharedPtr<FJsonValue>>* SourceValues = nullptr;
		TArray<uint16> Heights;
		TFuture<FLandscapeHeightmapDecodeResult> DecodeFuture;
		TUniquePtr<FLandscapeEditDataInterface> Edit;
	};

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLandscapeAdapter::CreateHeightWriteStepper(const TSharedPtr<FJsonObject>& Args)
	{
		return MakeShared<FLandscapeHeightWriteStepper>(Args);
	}
}
