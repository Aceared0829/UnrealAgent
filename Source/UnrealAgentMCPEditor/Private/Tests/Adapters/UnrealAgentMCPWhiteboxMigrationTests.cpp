// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPWhiteboxMigrationTests.cpp
 * @brief 可恢复受管白膜城墙工作流的集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"
#include "Adapters/Unreal/Whitebox/UnrealAgentMCPUnrealWhiteboxAdapter.h"
#include "Application/Domains/Whitebox/UnrealAgentMCPWhiteboxService.h"
#include "Application/Ports/UnrealAgentMCPLandscapePort.h"
#include "Application/Ports/UnrealAgentMCPWhiteboxPort.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPWhiteboxMigrationIntegrationTest, "WorldData.UnrealAgent.Whitebox.ManagedCityWallIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPWhiteboxMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("Editor world is available"), World))
		{
			return false;
		}
		UPackage* Package = World->GetOutermost();
		const bool bWasDirty = Package && Package->IsDirty();
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Folder = TEXT("MCPTests/Whitebox/") + Suffix;
		const FString Label = TEXT("MCP_CityWall_") + Suffix;

		const TSharedRef<IUnrealAgentMCPLandscapePort> LandscapePort = MakeShared<FUnrealAgentMCPUnrealLandscapeAdapter>();
		const TSharedRef<IUnrealAgentMCPWhiteboxPort> WhiteboxPort = MakeShared<FUnrealAgentMCPUnrealWhiteboxAdapter>(LandscapePort);
		FUnrealAgentMCPWhiteboxService Service(WhiteboxPort);

		auto ExecuteTask = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Execution::FMcpTaskSnapshot Snapshot;
			Args->SetStringField(TEXT("action"), Action);
			TSharedPtr<Execution::IMcpTaskStepper> Stepper = Service.CreateTaskStepper(Args);
			if (!TestTrue(*FString::Printf(TEXT("%s creates a resumable stepper"), *Action), Stepper.IsValid()))
			{
				return Snapshot;
			}

			Execution::FMcpTaskManager Manager;
			Execution::FMcpTaskRequest Request;
			Request.ToolName = TEXT("worlddata.whitebox.") + Action;
			Request.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
			Request.bCancelable = true;
			Request.Timeout = FTimespan::FromSeconds(10);
			Request.Stepper = MoveTemp(Stepper);
			FGuid TaskId;
			FString SubmitError;
			if (!TestTrue(*FString::Printf(TEXT("%s submits to the task manager"), *Action), Manager.Submit(MoveTemp(Request), TaskId, SubmitError)))
			{
				AddError(SubmitError);
				return Snapshot;
			}

			const double Deadline = FPlatformTime::Seconds() + 10.0;
			do
			{
				FTSTicker::GetCoreTicker().Tick(0.016f);
				Manager.Tick();
				if (Manager.TryRead(TaskId, Snapshot) && Snapshot.IsTerminal())
				{
					break;
				}
				FPlatformProcess::SleepNoStats(0.001f);
			} while (FPlatformTime::Seconds() < Deadline);

			TestEqual(*FString::Printf(TEXT("%s completes"), *Action), Snapshot.State, Execution::EMcpTaskState::Completed);
			if (Snapshot.State != Execution::EMcpTaskState::Completed)
			{
				AddError(Snapshot.Error);
			}
			return Snapshot;
		};

		auto MakeBuildArgs = [&Folder, &Label]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Center = MakeShared<FJsonObject>();
			Center->SetNumberField(TEXT("x"), 500000.0);
			Center->SetNumberField(TEXT("y"), 500000.0);
			Center->SetNumberField(TEXT("z"), 10000.0);
			Args->SetObjectField(TEXT("center"), Center);
			Args->SetStringField(TEXT("folder"), Folder);
			Args->SetStringField(TEXT("actorLabel"), Label);
			Args->SetNumberField(TEXT("halfExtentCm"), 3000.0);
			Args->SetNumberField(TEXT("baseThicknessM"), 4.0);
			Args->SetNumberField(TEXT("topThicknessM"), 2.0);
			Args->SetNumberField(TEXT("wallHeightM"), 6.0);
			Args->SetNumberField(TEXT("gateOpeningWidthM"), 5.0);
			Args->SetNumberField(TEXT("mamianPerSide"), 1.0);
			Args->SetNumberField(TEXT("merlonSpacingM"), 2.0);
			Args->SetBoolField(TEXT("snapBaseToLandscape"), false);
			Args->SetBoolField(TEXT("replaceExisting"), true);
			return Args;
		};

		const Execution::FMcpTaskSnapshot FirstBuild = ExecuteTask(TEXT("build_city_wall"), MakeBuildArgs());
		TestTrue(TEXT("build result reports one Actor and HISM instances"),
			FirstBuild.ValueJson.Contains(TEXT("\"actorCount\":1")) && FirstBuild.ValueJson.Contains(TEXT("\"instanceCount\":")));

		AActor* WallActor = ActorSupport::FindActorByNameOrLabel(World, Label);
		if (TestNotNull(TEXT("Managed city wall Actor exists"), WallActor))
		{
			TestTrue(TEXT("City wall carries the Whitebox ownership tag"), WallActor->Tags.Contains(FName(TEXT("UnrealAgentMCP.Whitebox"))));
			TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Components(WallActor);
			TestEqual(TEXT("City wall owns four semantic HISM components"), Components.Num(), 4);
			int32 InstanceCount = 0;
			for (const UHierarchicalInstancedStaticMeshComponent* Component : Components)
			{
				InstanceCount += Component->GetInstanceCount();
			}
			TestTrue(TEXT("City wall uses instancing for its repeated masses"), InstanceCount > 20);
		}

		TSharedRef<FJsonObject> PatchArgs = MakeShared<FJsonObject>();
		PatchArgs->SetStringField(TEXT("folder"), Folder);
		PatchArgs->SetStringField(TEXT("actor_label"), Label);
		TSharedRef<FJsonObject> ElementPatch = MakeShared<FJsonObject>();
		ElementPatch->SetStringField(TEXT("element_id"), TEXT("merlon"));
		ElementPatch->SetStringField(TEXT("operation"), TEXT("replace"));
		TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), 0.0);
		Location->SetNumberField(TEXT("y"), 0.0);
		Location->SetNumberField(TEXT("z"), 800.0);
		Transform->SetObjectField(TEXT("location"), Location);
		ElementPatch->SetArrayField(TEXT("transforms"), { MakeShared<FJsonValueObject>(Transform) });
		PatchArgs->SetArrayField(TEXT("elements"), { MakeShared<FJsonValueObject>(ElementPatch) });
		const Execution::FMcpTaskSnapshot Patch = ExecuteTask(TEXT("patch_city_wall"), PatchArgs);
		TestTrue(TEXT("patch_city_wall reports one stable element patch"), Patch.ValueJson.Contains(TEXT("\"patched_element_count\":1")));
		if (WallActor)
		{
			TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Components(WallActor);
			for (const UHierarchicalInstancedStaticMeshComponent* Component : Components)
			{
				if (Component->GetFName() == TEXT("MerlonHISM"))
				{
					TestEqual(TEXT("Stable merlon element is replaced without rebuilding the wall"), Component->GetInstanceCount(), 1);
				}
			}
		}

		ExecuteTask(TEXT("build_city_wall"), MakeBuildArgs());
		int32 MatchingActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label && It->Tags.Contains(FName(TEXT("UnrealAgentMCP.Whitebox"))))
			{
				++MatchingActorCount;
			}
		}
		TestEqual(TEXT("Rebuilding the same spec remains idempotent"), MatchingActorCount, 1);

		TSharedRef<FJsonObject> ClearArgs = MakeShared<FJsonObject>();
		ClearArgs->SetStringField(TEXT("folder"), Folder);
		ClearArgs->SetStringField(TEXT("confirmation"), TEXT("clear_whitebox_folder"));
		const Execution::FMcpTaskSnapshot Clear = ExecuteTask(TEXT("clear_folder"), ClearArgs);
		TestTrue(TEXT("clear_folder reports the managed Actor deletion"),
			Clear.ValueJson.Contains(TEXT("\"deletedCount\":1")) && Clear.ValueJson.Contains(TEXT("\"remainingCount\":0")));
		TestNull(TEXT("Managed city wall Actor is removed"), ActorSupport::FindActorByNameOrLabel(World, Label));

		if (Package)
		{
			Package->SetDirtyFlag(bWasDirty);
		}
		return true;
	}
}

#endif
