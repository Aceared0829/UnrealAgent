// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLandscapeMigrationTests.cpp
 * @brief Landscape 迁移能力的真实地形黑盒集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"
#include "Application/Domains/Landscape/UnrealAgentMCPLandscapeService.h"
#include "Application/Ports/UnrealAgentMCPLandscapePort.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Landscape.h"
#include "LandscapeLayerInfoObject.h"
#include "Misc/Base64.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLandscapeMigrationIntegrationTest, "WorldData.UnrealAgent.Landscape.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLandscapeMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
			return false;
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString LandscapeLabel = TEXT("MCP_Landscape_") + Suffix;
		const FString LayerName = TEXT("MCP_Layer_") + Suffix;
		const FString LayerAssetName = TEXT("LI_") + LayerName;
		const FString LayerAssetPath = TEXT("/Game/UnrealAgentAutomation/") + LayerAssetName + TEXT(".") + LayerAssetName;
		const FVector Location(240000.0, 240000.0, 1000.0);

		TSharedRef<IUnrealAgentMCPLandscapePort> Port = MakeShared<FUnrealAgentMCPUnrealLandscapeAdapter>();
		FUnrealAgentMCPLandscapeService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1600)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForLandscape = [&LandscapeLabel]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), LandscapeLabel);
			return Args;
		};
		auto SetCenter = [&Location](const TSharedRef<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Center = MakeShared<FJsonObject>();
			Center->SetNumberField(TEXT("x"), Location.X);
			Center->SetNumberField(TEXT("y"), Location.Y);
			Args->SetObjectField(TEXT("center"), Center);
		};
		auto MakePoint = [](const double X, const double Y)
		{
			TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
			Point->SetNumberField(TEXT("x"), X);
			Point->SetNumberField(TEXT("y"), Y);
			return Point;
		};
		auto ExecuteTask = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Execution::FMcpTaskSnapshot Snapshot;
			Args->SetStringField(TEXT("action"), Action);
			TSharedPtr<Execution::IMcpTaskStepper> Stepper = Service.CreateTaskStepper(Args);
			if (!TestTrue(*FString::Printf(TEXT("%s 创建可恢复任务执行器"), *Action), Stepper.IsValid()))
			{
				return Snapshot;
			}

			Execution::FMcpTaskManager Manager;
			Execution::FMcpTaskRequest Request;
			Request.ToolName = TEXT("worlddata.landscape.") + Action;
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

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> LocationJson = MakeShared<FJsonObject>();
			LocationJson->SetNumberField(TEXT("x"), Location.X);
			LocationJson->SetNumberField(TEXT("y"), Location.Y);
			LocationJson->SetNumberField(TEXT("z"), Location.Z);
			Args->SetObjectField(TEXT("location"), LocationJson);
			Args->SetNumberField(TEXT("scale"), 100.0);
			Args->SetNumberField(TEXT("componentCountX"), 1);
			Args->SetNumberField(TEXT("componentCountY"), 1);
			Args->SetNumberField(TEXT("subsectionSizeQuads"), 7);
			Args->SetNumberField(TEXT("numSubsections"), 1);
			Args->SetNumberField(TEXT("heightOffset"), 32768);
			Args->SetStringField(TEXT("label"), LandscapeLabel);
			const FString Result = Execute(TEXT("create"), Args);
			TestTrue(TEXT("创建结果报告一个真实组件"), Result.Contains(TEXT("\"componentCount\":1")));
		}

		ALandscape* Landscape = nullptr;
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			if (It->GetActorLabel() == LandscapeLabel)
			{
				Landscape = *It;
				break;
			}
		}
		TestNotNull(TEXT("创建的 ALandscape 可发现"), Landscape);
		const FString LandscapeGuid = Landscape ? Landscape->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower) : FString();

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("landscapeGuid"), LandscapeGuid);
			const FString Result = Execute(TEXT("get_info"), Args);
			TestTrue(TEXT("可用 landscapeGuid 精确选择地形"), Result.Contains(LandscapeGuid));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("action"), TEXT("get_info"));
			Args->SetStringField(TEXT("landscapeGuid"), TEXT("00000000-0000-0000-0000-000000000000"));
			const FString Result = Service.Execute(Args);
			TestTrue(TEXT("拒绝零 landscapeGuid"), Result.Contains(TEXT("不是有效 Guid")));
		}

		Execute(TEXT("get_info"), ForLandscape());
		Execute(TEXT("list_layers"), ForLandscape());

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetNumberField(TEXT("x"), Location.X);
			Args->SetNumberField(TEXT("y"), Location.Y);
			Execute(TEXT("sample"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			TArray<TSharedPtr<FJsonValue>> Points;
			Points.Add(MakeShared<FJsonValueObject>(MakePoint(Location.X, Location.Y)));
			Points.Add(MakeShared<FJsonValueObject>(MakePoint(Location.X + 100.0, Location.Y)));
			Args->SetArrayField(TEXT("points"), Points);
			const FString Result = Execute(TEXT("sample_batch"), Args);
			TestTrue(TEXT("批量点采样保留两个结果"), Result.Contains(TEXT("\"sampleCount\":2")));
		}
		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetObjectField(TEXT("origin"), MakePoint(Location.X, Location.Y));
			Args->SetNumberField(TEXT("spacing"), 100.0);
			Args->SetNumberField(TEXT("countX"), 2);
			Args->SetNumberField(TEXT("countY"), 2);
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("sample_grid"), Args);
			TestTrue(TEXT("合法 sample_grid 通过可恢复任务完成并返回四个结果"),
				Snapshot.State == Execution::EMcpTaskState::Completed && Snapshot.ValueJson.Contains(TEXT("\"sampleCount\":4")));
		}
		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			TArray<TSharedPtr<FJsonValue>> Points;
			Points.Add(MakeShared<FJsonValueObject>(MakePoint(Location.X, Location.Y)));
			Points.Add(MakeShared<FJsonValueObject>(MakePoint(Location.X + 200.0, Location.Y)));
			Args->SetArrayField(TEXT("points"), Points);
			Args->SetNumberField(TEXT("spacing"), 100.0);
			const FString Result = Execute(TEXT("sample_polyline"), Args);
			TestTrue(TEXT("折线采样包含起点、中点、终点"), Result.Contains(TEXT("\"sampleCount\":3")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			SetCenter(Args);
			Args->SetNumberField(TEXT("radius"), 300.0);
			Args->SetStringField(TEXT("mode"), TEXT("raise"));
			Args->SetNumberField(TEXT("amount"), 100.0);
			Args->SetNumberField(TEXT("falloff"), 0.5);
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			const FString Result = Execute(TEXT("sculpt"), Args);
			TestFalse(TEXT("地形雕刻至少修改一个顶点"), Result.Contains(TEXT("\"changedVertexCount\":0")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			SetCenter(Args);
			Args->SetNumberField(TEXT("radius"), 300.0);
			Args->SetStringField(TEXT("mode"), TEXT("set"));
			Args->SetNumberField(TEXT("targetWorldZ"), Location.Z + 250.0);
			Args->SetNumberField(TEXT("falloff"), 0.0);
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			const FString Result = Execute(TEXT("sculpt"), Args);
			TestTrue(TEXT("绝对高度雕刻报告目标高度"), Result.Contains(TEXT("\"targetWorldZ\":1250")));
			TestTrue(TEXT("正常目标高度没有触顶或触底"), Result.Contains(TEXT("\"clampedVertexCount\":0")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetStringField(TEXT("action"), TEXT("sculpt_batch"));
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			TArray<TSharedPtr<FJsonValue>> Strokes;
			for (int32 StrokeIndex = 0; StrokeIndex < 2; ++StrokeIndex)
			{
				TSharedRef<FJsonObject> Stroke = MakeShared<FJsonObject>();
				Stroke->SetObjectField(TEXT("center"), MakePoint(Location.X + StrokeIndex * 100.0, Location.Y));
				Stroke->SetNumberField(TEXT("radius"), 300.0);
				Stroke->SetStringField(TEXT("mode"), TEXT("raise"));
				Stroke->SetNumberField(TEXT("amount"), 25.0);
				Stroke->SetNumberField(TEXT("falloff"), 0.25);
				Strokes.Add(MakeShared<FJsonValueObject>(Stroke));
			}
			Args->SetArrayField(TEXT("strokes"), Strokes);

			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("sculpt_batch"), Args);
			TestTrue(TEXT("sculpt_batch 报告两笔笔刷与合并更新"),
				Snapshot.ValueJson.Contains(TEXT("\"strokeCount\":2")) && Snapshot.ValueJson.Contains(TEXT("\"heightReadPassCount\":1")) &&
					Snapshot.ValueJson.Contains(TEXT("\"contentUpdateCount\":1")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetObjectField(TEXT("min"), MakePoint(0, 0));
			Args->SetObjectField(TEXT("max"), MakePoint(1, 1));
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			TArray<TSharedPtr<FJsonValue>> Heights;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Heights.Add(MakeShared<FJsonValueNumber>(33000 + Index));
			}
			Args->SetArrayField(TEXT("rawHeights"), Heights);
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("set_height_rect"), Args);
			TestTrue(TEXT("set_height_rect 一次写入四个顶点并合并更新"),
				Snapshot.ValueJson.Contains(TEXT("\"vertexCount\":4")) && Snapshot.ValueJson.Contains(TEXT("\"rawHeightMin\":33000")) &&
					Snapshot.ValueJson.Contains(TEXT("\"contentUpdateCount\":1")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetObjectField(TEXT("min"), MakePoint(2, 0));
			Args->SetNumberField(TEXT("width"), 2);
			Args->SetNumberField(TEXT("height"), 2);
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			const TArray<uint8> RawBytes = { 0x4c, 0x81, 0x4d, 0x81, 0x4e, 0x81, 0x4f, 0x81 };
			Args->SetStringField(TEXT("base64"), FBase64::Encode(RawBytes));
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("import_heightmap"), Args);
			TestTrue(TEXT("import_heightmap 在后台解码 raw16 并写入四个顶点"),
				Snapshot.ValueJson.Contains(TEXT("\"vertexCount\":4")) && Snapshot.ValueJson.Contains(TEXT("\"rawHeightMin\":33100")) &&
					Snapshot.ValueJson.Contains(TEXT("\"rawHeightMax\":33103")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetNumberField(TEXT("rawHeight"), 32768);
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			const Execution::FMcpTaskSnapshot Snapshot = ExecuteTask(TEXT("reset_heights"), Args);
			TestTrue(TEXT("reset_heights 保留拓扑并重置全部六十四个顶点"),
				Snapshot.ValueJson.Contains(TEXT("\"vertexCount\":64")) && Snapshot.ValueJson.Contains(TEXT("\"rawHeightMin\":32768")) &&
					Snapshot.ValueJson.Contains(TEXT("\"rawHeightMax\":32768")));
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("layerName"), LayerName);
			Args->SetStringField(TEXT("name"), LayerAssetName);
			Args->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
			Args->SetNumberField(TEXT("hardness"), 0.35);
			const FString Result = Execute(TEXT("create_layer_info"), Args);
			TestTrue(TEXT("LayerInfo 已持久化到测试路径"), Result.Contains(LayerAssetPath));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetStringField(TEXT("layerName"), LayerName);
			Args->SetStringField(TEXT("name"), LayerAssetName);
			Args->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
			Execute(TEXT("add_layer_info"), Args);
		}

		{
			const FString Result = Execute(TEXT("list_layers"), ForLandscape());
			TestTrue(TEXT("目标图层列表包含新增 LayerInfo"), Result.Contains(LayerName));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			SetCenter(Args);
			Args->SetStringField(TEXT("layerName"), LayerName);
			Args->SetNumberField(TEXT("radius"), 300.0);
			Args->SetNumberField(TEXT("strength"), 0.75);
			Args->SetNumberField(TEXT("falloff"), 0.25);
			Args->SetNumberField(TEXT("maxVertices"), 1000);
			const FString Result = Execute(TEXT("paint_layer"), Args);
			TestFalse(TEXT("图层绘制至少修改一个顶点"), Result.Contains(TEXT("\"changedVertexCount\":0")));
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetStringField(TEXT("materialPath"),
				TEXT("/Engine/EngineMaterials/"
					 "WorldGridMaterial.WorldGridMaterial"));
			Execute(TEXT("set_material"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForLandscape();
			Args->SetNumberField(TEXT("componentIndex"), 0);
			Execute(TEXT("get_component"), Args);
		}
		Execute(TEXT("list_splines"), ForLandscape());
		Execute(TEXT("get_material_usage_summary"), MakeShared<FJsonObject>());
		Execute(TEXT("list_proxies"), MakeShared<FJsonObject>());
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetNumberField(TEXT("worldX"), Location.X);
			Args->SetNumberField(TEXT("worldY"), Location.Y);
			Execute(TEXT("find_proxy_at"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("action"), TEXT("create"));
			Args->SetStringField(TEXT("label"), LandscapeLabel);
			const FString Result = Service.Execute(Args);
			TestTrue(TEXT("create 默认拒绝重复 Landscape 标签"), Result.Contains(TEXT("replaceExisting=true")));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> LocationJson = MakeShared<FJsonObject>();
			LocationJson->SetNumberField(TEXT("x"), Location.X);
			LocationJson->SetNumberField(TEXT("y"), Location.Y);
			LocationJson->SetNumberField(TEXT("z"), Location.Z);
			Args->SetObjectField(TEXT("location"), LocationJson);
			Args->SetNumberField(TEXT("scale"), 100.0);
			Args->SetNumberField(TEXT("componentCountX"), 1);
			Args->SetNumberField(TEXT("componentCountY"), 1);
			Args->SetNumberField(TEXT("subsectionSizeQuads"), 7);
			Args->SetNumberField(TEXT("numSubsections"), 1);
			Args->SetNumberField(TEXT("heightOffset"), 32768);
			Args->SetStringField(TEXT("label"), LandscapeLabel);
			Args->SetBoolField(TEXT("replaceExisting"), true);
			const FString Result = Execute(TEXT("create"), Args);
			TestTrue(TEXT("create 显式替换先成功导入新地形再删除旧地形"), Result.Contains(TEXT("\"replacedExisting\":true")) && Result.Contains(LandscapeGuid));

			Landscape = nullptr;
			int32 MatchingLandscapeCount = 0;
			for (TActorIterator<ALandscape> It(World); It; ++It)
			{
				if (It->GetActorLabel() == LandscapeLabel)
				{
					Landscape = *It;
					++MatchingLandscapeCount;
				}
			}
			TestEqual(TEXT("替换后同标签主 Landscape 保持唯一"), MatchingLandscapeCount, 1);
		}

		if (Landscape)
			World->EditorDestroyActor(Landscape, false);
		if (ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerAssetPath))
		{
			TArray<UObject*> ObjectsToDelete = { LayerInfo };
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}
		if (WorldPackage)
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		return true;
	}
}

#endif
