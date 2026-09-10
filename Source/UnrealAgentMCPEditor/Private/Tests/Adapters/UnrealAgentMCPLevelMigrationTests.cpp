// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLevelMigrationTests.cpp
 * @brief Level 应用服务与 Unreal Adapter 的真实编辑器场景集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"
#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Ports/UnrealAgentMCPLevelPort.h"
#include "Components/SceneComponent.h"
#include "Containers/Ticker.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelMigrationIntegrationTest, "WorldData.UnrealAgent.Level.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
		{
			return false;
		}
		UPackage* Package = World->GetOutermost();
		const bool bWasDirty = Package && Package->IsDirty();

		AStaticMeshActor* Parent = World->SpawnActor<AStaticMeshActor>();
		AStaticMeshActor* Child = World->SpawnActor<AStaticMeshActor>();
		if (!TestNotNull(TEXT("父测试 Actor 创建成功"), Parent) || !TestNotNull(TEXT("子测试 Actor 创建成功"), Child))
		{
			if (Parent)
				World->EditorDestroyActor(Parent, false);
			if (Child)
				World->EditorDestroyActor(Child, false);
			if (Package)
				Package->SetDirtyFlag(bWasDirty);
			return false;
		}

		const FString ParentLabel = TEXT("UnrealAgentMCP_LevelMigration_Parent");
		const FString ChildLabel = TEXT("UnrealAgentMCP_LevelMigration_Child");
		Parent->SetActorLabel(ParentLabel);
		Child->SetActorLabel(ChildLabel);
		Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);

		TSharedRef<IUnrealAgentMCPLevelPort> Port = MakeShared<FUnrealAgentMCPUnrealLevelAdapter>();
		FUnrealAgentMCPLevelService Service(Port);

		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果"), *Action), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ExecuteTask = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Execution::FMcpTaskSnapshot Snapshot;
			Arguments->SetStringField(TEXT("action"), Action);
			TSharedPtr<Execution::IMcpTaskStepper> Stepper = Service.CreateTaskStepper(Arguments);
			if (!TestTrue(*FString::Printf(TEXT("%s 创建可恢复任务执行器"), *Action), Stepper.IsValid()))
			{
				return Snapshot;
			}

			Execution::FMcpTaskManager Manager;
			Execution::FMcpTaskRequest Request;
			Request.ToolName = TEXT("worlddata.level.") + Action;
			Request.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
			Request.bCancelable = true;
			Request.Timeout = FTimespan::FromSeconds(5);
			Request.Stepper = MoveTemp(Stepper);
			FGuid TaskId;
			FString SubmitError;
			if (!TestTrue(*FString::Printf(TEXT("%s 提交到统一任务管理器"), *Action), Manager.Submit(MoveTemp(Request), TaskId, SubmitError)))
			{
				AddError(SubmitError);
				return Snapshot;
			}

			const double Deadline = FPlatformTime::Seconds() + 5.0;
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

			TestEqual(*FString::Printf(TEXT("%s 可恢复任务完成"), *Action), Snapshot.State, Execution::EMcpTaskState::Completed);
			return Snapshot;
		};

		auto ChildArguments = [&ChildLabel]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), ChildLabel);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetBoolField(TEXT("visible"), false);
			Execute(TEXT("set_editor_visibility"), Args);
			TestTrue(TEXT("Actor 已在编辑器中隐藏"), Child->IsTemporarilyHiddenInEditor());
		}

		Execute(TEXT("get_actor_bounds"), ChildArguments());
		Execute(TEXT("get_component_tree"), ChildArguments());
		Execute(TEXT("get_relative_transform"), ChildArguments());
		Execute(TEXT("resolve_actor"), ChildArguments());

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetStringField(TEXT("folderPath"), TEXT("MCPTests/LevelMigration"));
			Execute(TEXT("set_actor_folder_path"), Args);
			TestEqual(TEXT("Actor 文件夹已写入"), Child->GetFolderPath(), FName(TEXT("MCPTests/LevelMigration")));
		}

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetStringField(TEXT("tag"), TEXT("MigratedLevelAction"));
			Execute(TEXT("add_actor_tag"), Args);
			TestTrue(TEXT("Actor 标签已添加"), Child->Tags.Contains(FName(TEXT("MigratedLevelAction"))));
		}

		Execute(TEXT("list_actor_tags"), ChildArguments());

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetArrayField(TEXT("tags"), { MakeShared<FJsonValueString>(TEXT("Wall")), MakeShared<FJsonValueString>(TEXT("House")) });
			Execute(TEXT("set_actor_tags"), Args);
			TestEqual(TEXT("Actor 标签被整体替换"), Child->Tags.Num(), 2);
		}

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetStringField(TEXT("tag"), TEXT("House"));
			Execute(TEXT("remove_actor_tag"), Args);
			TestFalse(TEXT("指定 Actor 标签已移除"), Child->Tags.Contains(FName(TEXT("House"))));
		}

		{
			TSharedRef<FJsonObject> Args = ChildArguments();
			Args->SetStringField(TEXT("mobility"), TEXT("Movable"));
			Execute(TEXT("set_actor_mobility"), Args);
			TestEqual(TEXT("RootComponent 移动性已更新"), Child->GetRootComponent()->Mobility.GetValue(), EComponentMobility::Movable);
		}

		Execute(TEXT("detach_actor"), ChildArguments());
		TestNull(TEXT("Actor 已与父级分离"), Child->GetAttachParentActor());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("className"), TEXT("StaticMeshActor"));
			Execute(TEXT("get_actors_by_class"), Args);
			Execute(TEXT("count_actors_by_class"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetArrayField(TEXT("actorLabels"), { MakeShared<FJsonValueString>(ParentLabel), MakeShared<FJsonValueString>(ChildLabel) });
			Args->SetBoolField(TEXT("dryRun"), true);
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("delete_actors"), Args);
			TestTrue(TEXT("delete_actors dryRun 精确预览两个 Actor"),
				Snapshot.ValueJson.Contains(TEXT("\"matchedCount\":2")) && Snapshot.ValueJson.Contains(TEXT("\"deletedCount\":0")) &&
					Snapshot.ValueJson.Contains(TEXT("\"remainingCount\":2")));
		}

		{
			TSharedRef<FJsonObject> ParentArgs = MakeShared<FJsonObject>();
			ParentArgs->SetStringField(TEXT("actorLabel"), ParentLabel);
			ParentArgs->SetStringField(TEXT("folderPath"), TEXT("MCPTests/FolderDelete"));
			Execute(TEXT("set_actor_folder_path"), ParentArgs);
			TSharedRef<FJsonObject> ChildArgs = ChildArguments();
			ChildArgs->SetStringField(TEXT("folderPath"), TEXT("MCPTests/FolderDelete/Nested"));
			Execute(TEXT("set_actor_folder_path"), ChildArgs);

			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("folderPath"), TEXT("MCPTests/FolderDelete"));
			Args->SetStringField(TEXT("confirmation"), TEXT("delete_folder_contents"));
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("delete_by_folder"), Args);
			TestTrue(TEXT("delete_by_folder 递归删除并回读零剩余"),
				Snapshot.ValueJson.Contains(TEXT("\"matchedCount\":2")) && Snapshot.ValueJson.Contains(TEXT("\"deletedCount\":2")) &&
					Snapshot.ValueJson.Contains(TEXT("\"remainingCount\":0")));
		}

		TestNull(TEXT("父测试 Actor 已清理"), ActorSupport::FindActorByNameOrLabel(World, ParentLabel));
		TestNull(TEXT("子测试 Actor 已清理"), ActorSupport::FindActorByNameOrLabel(World, ChildLabel));
		if (Package)
		{
			Package->SetDirtyFlag(bWasDirty);
		}
		return true;
	}
}

#endif
