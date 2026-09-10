// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Viewport.cpp
 * @brief 编辑器视口状态、相机与 Actor 聚焦操作。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "LevelEditorViewport.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FLevelEditorViewportClient* ResolveViewportClient()
		{
			if (GCurrentLevelEditingViewportClient)
			{
				return GCurrentLevelEditingViewportClient;
			}
			if (GEditor && !GEditor->GetLevelViewportClients().IsEmpty())
			{
				return GEditor->GetLevelViewportClients()[0];
			}
			return nullptr;
		}

		bool ReadVector(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FVector& OutValue)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(Field, Object) || !Object)
			{
				return false;
			}
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			(*Object)->TryGetNumberField(TEXT("x"), X);
			(*Object)->TryGetNumberField(TEXT("y"), Y);
			(*Object)->TryGetNumberField(TEXT("z"), Z);
			OutValue = FVector(X, Y, Z);
			return true;
		}

		bool ReadRotation(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FRotator& OutValue)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(Field, Object) || !Object)
			{
				return false;
			}
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			(*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*Object)->TryGetNumberField(TEXT("roll"), Roll);
			OutValue = FRotator(Pitch, Yaw, Roll);
			return true;
		}

		TSharedRef<FJsonObject> MakeVector(const FVector& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("x"), Value.X);
			Result->SetNumberField(TEXT("y"), Value.Y);
			Result->SetNumberField(TEXT("z"), Value.Z);
			return Result;
		}

		TSharedRef<FJsonObject> MakeRotation(const FRotator& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("pitch"), Value.Pitch);
			Result->SetNumberField(TEXT("yaw"), Value.Yaw);
			Result->SetNumberField(TEXT("roll"), Value.Roll);
			return Result;
		}

		bool ReadCaptureVector(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FVector& OutValue)
		{
			return ReadVector(Args, Field, OutValue);
		}

		bool ReadCaptureRotation(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FRotator& OutValue)
		{
			return ReadRotation(Args, Field, OutValue);
		}

		FString ResolveScreenshotBaseName(const TSharedPtr<FJsonObject>& Args, const FString& Prefix)
		{
			FString BaseName = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("filename"), TEXT("name"), TEXT("fileName"), TEXT("outputPath") });
			BaseName = FPaths::GetBaseFilename(BaseName);
			BaseName = FPaths::MakeValidFileName(BaseName);
			if (BaseName.IsEmpty())
			{
				BaseName = FString::Printf(TEXT("%s_%s_%lld"), *Prefix, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")), FDateTime::UtcNow().GetTicks());
			}
			return BaseName;
		}

		FString ScreenshotDirectory()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("UnrealAgent")));
		}

		class FViewportScreenshotTaskStepper final : public Execution::IMcpTaskStepper
		{
		public:
			explicit FViewportScreenshotTaskStepper(const TSharedPtr<FJsonObject>& InArguments)
				: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
			{
			}

			virtual ~FViewportScreenshotTaskStepper() override
			{
				RemoveProcessedDelegate();
			}

			virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
			{
				(void)FrameBudget;
				if (!bQueued)
				{
					if (FScreenshotRequest::IsScreenshotRequested())
					{
						Context.EnterWaiting();
						Context.ReportProgress(0.05, TEXT("等待当前截图请求完成。"));
						return Execution::FMcpTaskStepResult::Continue();
					}

					FLevelEditorViewportClient* Client = ResolveViewportClient();
					if (!Client)
					{
						return Execution::FMcpTaskStepResult::Failed(TEXT("没有可用的关卡编辑器视口。"));
					}
					const FString Directory = ScreenshotDirectory();
					if (!IFileManager::Get().MakeDirectory(*Directory, true))
					{
						return Execution::FMcpTaskStepResult::Failed(TEXT("无法创建项目截图目录。"));
					}
					const FString BaseName = ResolveScreenshotBaseName(Arguments, TEXT("Viewport"));
					OutputPath = FPaths::Combine(Directory, BaseName + TEXT(".png"));
					bool bShowUi = false;
					Arguments->TryGetBoolField(TEXT("show_ui"), bShowUi);
					ProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(this, &FViewportScreenshotTaskStepper::MarkProcessed);
					FScreenshotRequest::RequestScreenshot(OutputPath, bShowUi, false, false, FIntRect(), false);
					Client->Invalidate();
					bQueued = true;
					Context.ResumeRunning();
					Context.ReportProgress(0.25, TEXT("截图已排队，等待视口渲染。"));
					return Execution::FMcpTaskStepResult::Continue();
				}

				if (!bProcessed)
				{
					Context.EnterWaiting();
					Context.ReportProgress(0.75, TEXT("等待截图编码并写入磁盘。"));
					return Execution::FMcpTaskStepResult::Continue();
				}

				RemoveProcessedDelegate();
				const int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
				if (FileSize <= 0)
				{
					FileCheckAttempts++;
					if (FileCheckAttempts < 30)
					{
						Context.EnterWaiting();
						return Execution::FMcpTaskStepResult::Continue();
					}
					return Execution::FMcpTaskStepResult::Failed(TEXT("截图请求已处理，但 PNG 文件没有成功写入。"));
				}

				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("completed"), true);
				Result->SetBoolField(TEXT("queued"), false);
				Result->SetStringField(TEXT("path"), OutputPath);
				Result->SetNumberField(TEXT("size_bytes"), static_cast<double>(FileSize));
				Result->SetStringField(TEXT("scope"), TEXT("项目 Saved/Screenshots/UnrealAgent"));
				Context.ResumeRunning();
				Context.ReportProgress(1.0, TEXT("截图已写入磁盘。"));
				return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
			}

			virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
			{
				(void)Context;
				RemoveProcessedDelegate();
				if (bQueued && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename() == OutputPath)
				{
					FScreenshotRequest::Reset();
				}
				return Execution::FMcpTaskStepResult::Failed(MoveTemp(Reason));
			}

		private:
			void MarkProcessed()
			{
				bProcessed = true;
			}

			void RemoveProcessedDelegate()
			{
				if (ProcessedHandle.IsValid())
				{
					FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
					ProcessedHandle.Reset();
				}
			}

			TSharedRef<FJsonObject> Arguments;
			FString OutputPath;
			FDelegateHandle ProcessedHandle;
			int32 FileCheckAttempts = 0;
			bool bQueued = false;
			bool bProcessed = false;
		};

		class FDeprecatedSceneCaptureTaskStepper final : public Execution::IMcpTaskStepper
		{
		public:
			virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
			{
				(void)Context;
				(void)FrameBudget;
				return Execution::FMcpTaskStepResult::Failed(
					TEXT("capture_scene_png 已停用，因为同步 GPU 读回和 PNG 导出会阻塞 GameThread。请改用可恢复任务 capture_screenshot。"));
			}
		};
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealEditorAdapter::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		if (Action == TEXT("capture_screenshot"))
		{
			return MakeShared<FViewportScreenshotTaskStepper>(Args);
		}
		if (Action == TEXT("capture_scene_png"))
		{
			return MakeShared<FDeprecatedSceneCaptureTaskStepper>();
		}
		return nullptr;
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Viewport(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FLevelEditorViewportClient* Client = ResolveViewportClient();
		if (!Client)
		{
			return ErrorJson(TEXT("没有可用的关卡编辑器视口。"));
		}

		if (Action == TEXT("set_realtime"))
		{
			bool bEnabled = true;
			Args->TryGetBoolField(TEXT("enabled"), bEnabled);
			Client->SetRealtime(bEnabled);
			Client->Invalidate();
		}
		else if (Action == TEXT("set_viewport"))
		{
			FVector Location;
			if (ReadVector(Args, TEXT("location"), Location))
			{
				Client->SetViewLocation(Location);
			}
			FRotator Rotation;
			if (ReadRotation(Args, TEXT("rotation"), Rotation))
			{
				Client->SetViewRotation(Rotation);
			}
			double Fov = 0.0;
			if (Args->TryGetNumberField(TEXT("fov"), Fov) && Fov >= 5.0 && Fov <= 170.0)
			{
				Client->ViewFOV = static_cast<float>(Fov);
			}
			Client->Invalidate();
		}
		else if (Action == TEXT("focus_on_actor"))
		{
			const FString ActorLabel = GetString(Args, { TEXT("actorLabel"), TEXT("actorName") });
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			AActor* Target = nullptr;
			if (World)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) || It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
					{
						Target = *It;
						break;
					}
				}
			}
			if (!Target)
			{
				return ErrorJson(TEXT("未找到待聚焦 Actor。"));
			}
			const FBox Bounds = Target->GetComponentsBoundingBox(true);
			if (Bounds.IsValid)
			{
				Client->FocusViewportOnBox(Bounds);
			}
			else
			{
				const FVector TargetLocation = Target->GetActorLocation();
				const FVector Camera = TargetLocation + FVector(-500.0, 0.0, 200.0);
				Client->SetViewLocation(Camera);
				Client->SetViewRotation((TargetLocation - Camera).Rotation());
			}
		}
		else if (Action == TEXT("capture_screenshot"))
		{
			return ErrorJson(TEXT("capture_screenshot 必须通过可恢复任务执行器运行。"));
		}
		else if (Action == TEXT("capture_scene_png"))
		{
			return ErrorJson(TEXT("capture_scene_png is disabled because its synchronous GPU readback can block the Unreal Editor."));
		}
		else if (Action == TEXT("hit_test_viewport_pixel"))
		{
			double PixelX = 0.0;
			double PixelY = 0.0;
			if (!Args->TryGetNumberField(TEXT("x"), PixelX) || !Args->TryGetNumberField(TEXT("y"), PixelY) || !Client->Viewport)
			{
				return ErrorJson(TEXT("缺少 x、y，或当前视口尚未初始化。"));
			}
			FViewport* Viewport = Client->Viewport;
			const FIntPoint ViewportSize = Viewport->GetSizeXY();
			double InputWidth = ViewportSize.X;
			double InputHeight = ViewportSize.Y;
			Args->TryGetNumberField(TEXT("width"), InputWidth);
			Args->TryGetNumberField(TEXT("height"), InputHeight);
			if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || InputWidth <= 0.0 || InputHeight <= 0.0)
			{
				return ErrorJson(TEXT("视口尺寸无效。"));
			}
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, Client->GetScene(), Client->EngineShowFlags).SetRealtimeUpdate(Client->IsRealtime()));
			FSceneView* SceneView = Client->CalcSceneView(&ViewFamily);
			if (!SceneView)
			{
				return ErrorJson(TEXT("无法构造当前视口 SceneView。"));
			}
			const FVector2D ScreenPosition(static_cast<float>(PixelX * ViewportSize.X / InputWidth), static_cast<float>(PixelY * ViewportSize.Y / InputHeight));
			FVector RayOrigin;
			FVector RayDirection;
			SceneView->DeprojectFVector2D(ScreenPosition, RayOrigin, RayDirection);
			double MaximumDistance = 200000.0;
			Args->TryGetNumberField(TEXT("maxDistance"), MaximumDistance);
			MaximumDistance = FMath::Clamp(MaximumDistance, 1.0, 100000000.0);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(UnrealAgentMCPViewportHitTest), true);
			Query.bReturnFaceIndex = true;
			Query.bReturnPhysicalMaterial = true;
			FHitResult Hit;
			UWorld* World = Client->GetWorld();
			const bool bHit = World && World->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * MaximumDistance, ECC_Visibility, Query);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("hit"), bHit);
			Result->SetObjectField(TEXT("rayOrigin"), MakeVector(RayOrigin));
			Result->SetObjectField(TEXT("rayDirection"), MakeVector(RayDirection));
			if (bHit)
			{
				Result->SetObjectField(TEXT("location"), MakeVector(Hit.Location));
				Result->SetObjectField(TEXT("normal"), MakeVector(Hit.Normal));
				Result->SetNumberField(TEXT("distance"), Hit.Distance);
				Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
				if (AActor* HitActor = Hit.GetActor())
				{
					Result->SetStringField(TEXT("actorLabel"), HitActor->GetActorLabel());
					Result->SetStringField(TEXT("actorClass"), HitActor->GetClass()->GetPathName());
				}
				if (UPrimitiveComponent* HitComponent = Hit.GetComponent())
				{
					Result->SetStringField(TEXT("componentPath"), HitComponent->GetPathName());
					if (UMaterialInterface* Material = HitComponent->GetMaterial(0))
					{
						Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
					}
				}
				if (Hit.PhysMaterial.IsValid())
				{
					Result->SetStringField(TEXT("physicalMaterialPath"), Hit.PhysMaterial->GetPathName());
				}
			}
			return SuccessJson(Result);
		}
		else if (Action != TEXT("get_viewport"))
		{
			return ErrorJson(TEXT("视口动作没有有效实现分支。"));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("location"), MakeVector(Client->GetViewLocation()));
		Result->SetObjectField(TEXT("rotation"), MakeRotation(Client->GetViewRotation()));
		Result->SetNumberField(TEXT("fov"), Client->ViewFOV);
		Result->SetBoolField(TEXT("realtime"), Client->IsRealtime());
		if (Client->Viewport)
		{
			const FIntPoint Size = Client->Viewport->GetSizeXY();
			Result->SetNumberField(TEXT("width"), Size.X);
			Result->SetNumberField(TEXT("height"), Size.Y);
		}
		return SuccessJson(Result);
	}
}
