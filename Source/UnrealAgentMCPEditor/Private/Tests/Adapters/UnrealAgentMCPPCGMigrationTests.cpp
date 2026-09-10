// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPCGMigrationTests.cpp
 * @brief PCG 十九项迁移能力的真实图谱、连线和组件黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"
#include "Application/Domains/PCG/UnrealAgentMCPPCGService.h"
#include "Application/Ports/UnrealAgentMCPPCGPort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ObjectTools.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGVolume.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPCGMigrationIntegrationTest, "WorldData.UnrealAgent.PCG.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPPCGMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
			return false;
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString GraphName = TEXT("PCG_MCP_") + Suffix;
		const FString GraphPath = TEXT("/Game/UnrealAgentAutomation/") + GraphName + TEXT(".") + GraphName;

		TSharedRef<FUnrealAgentMCPUnrealPCGAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealPCGAdapter>();
		TSharedRef<IUnrealAgentMCPPCGPort> Port = Adapter;
		FUnrealAgentMCPPCGService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1800)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForGraph = [&GraphPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), GraphPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), GraphName);
			Args->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
			const FString Result = Execute(TEXT("create_graph"), Args);
			TestTrue(TEXT("PCGGraph 已创建到测试路径"), Result.Contains(GraphPath));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), TEXT("/Game/UnrealAgentAutomation"));
			Args->SetBoolField(TEXT("recursive"), true);
			const FString Result = Execute(TEXT("list_graphs"), Args);
			TestTrue(TEXT("图谱列表包含新建资产"), Result.Contains(GraphName));
		}

		auto AddNode = [&Execute, &ForGraph](const FString& Type, const FString& Name)
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("nodeType"), Type);
			Args->SetStringField(TEXT("nodeName"), Name);
			return Execute(TEXT("add_node"), Args);
		};
		AddNode(TEXT("PCGTransformPointsSettings"), TEXT("Transform"));
		AddNode(TEXT("PCGStaticMeshSpawnerSettings"), TEXT("Spawner"));

		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (!TestNotNull(TEXT("真实 PCGGraph 可加载"), Graph))
			return false;
		UPCGNode* Transform = Adapter->ResolveNode(Graph, TEXT("Transform"));
		UPCGNode* Spawner = Adapter->ResolveNode(Graph, TEXT("Spawner"));
		if (!TestNotNull(TEXT("Transform 节点存在"), Transform) || !TestNotNull(TEXT("Spawner 节点存在"), Spawner))
		{
			return false;
		}
		if (!TestTrue(TEXT("测试节点具有可连接 Pin"), !Transform->OutputPinProperties().IsEmpty() && !Spawner->InputPinProperties().IsEmpty()))
		{
			return false;
		}
		const FString SourcePin = Transform->OutputPinProperties()[0].Label.ToString();
		const FString TargetPin = Spawner->InputPinProperties()[0].Label.ToString();

		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("sourceNode"), TEXT("Transform"));
			Args->SetStringField(TEXT("sourcePin"), SourcePin);
			Args->SetStringField(TEXT("targetNode"), TEXT("Spawner"));
			Args->SetStringField(TEXT("targetPin"), TargetPin);
			const FString Result = Execute(TEXT("connect_nodes"), Args);
			TestTrue(TEXT("连线完成后执行实际边验证"), Result.Contains(TEXT("\"edgeVerified\":true")));
		}
		Execute(TEXT("read_graph"), ForGraph());
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("nodeName"), TEXT("Transform"));
			Execute(TEXT("read_node_settings"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("nodeName"), TEXT("Transform"));
			TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetNumberField(TEXT("seed"), 12345);
			Args->SetObjectField(TEXT("settings"), Settings);
			const FString Result = Execute(TEXT("set_node_settings"), Args);
			TestTrue(TEXT("反射式设置写回新 Seed"), Result.Contains(TEXT("12345")));
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("nodeName"), TEXT("Spawner"));
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			Entry->SetNumberField(TEXT("weight"), 3);
			Args->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueObject>(Entry) });
			Args->SetBoolField(TEXT("replace"), true);
			const FString Result = Execute(TEXT("set_static_mesh_spawner_meshes"), Args);
			TestTrue(TEXT("Spawner 包含一个真实 MeshEntry"), Result.Contains(TEXT("\"entryCount\":1")));
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetBoolField(TEXT("includeSettings"), true);
			const FString Result = Execute(TEXT("export_graph"), Args);
			TestTrue(TEXT("导出包含节点与连线"), Result.Contains(TEXT("\"nodeCount\":2")) && Result.Contains(TEXT("\"connectionCount\":1")));
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("sourceNode"), TEXT("Transform"));
			Args->SetStringField(TEXT("targetNode"), TEXT("Spawner"));
			const FString Result = Execute(TEXT("disconnect_nodes"), Args);
			TestTrue(TEXT("断开一个真实 UPCGEdge"), Result.Contains(TEXT("\"removedEdges\":1")));
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
			Node->SetStringField(TEXT("name"), TEXT("Imported"));
			Node->SetStringField(TEXT("class"), TEXT("PCGTransformPointsSettings"));
			Node->SetNumberField(TEXT("posX"), 800);
			Node->SetNumberField(TEXT("posY"), 200);
			Args->SetArrayField(TEXT("nodes"), { MakeShared<FJsonValueObject>(Node) });
			Args->SetArrayField(TEXT("connections"), {});
			Args->SetBoolField(TEXT("replace"), false);
			const FString Result = Execute(TEXT("import_graph"), Args);
			TestTrue(TEXT("批量导入创建一个节点"), Result.Contains(TEXT("\"createdNodeCount\":1")));
		}
		for (const FString& Name : { TEXT("Imported"), TEXT("Spawner"), TEXT("Transform") })
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("nodeName"), Name);
			Execute(TEXT("remove_node"), Args);
		}

		FString VolumeLabel;
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("graphPath"), GraphPath);
			TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 360000.0);
			Location->SetNumberField(TEXT("y"), 360000.0);
			Location->SetNumberField(TEXT("z"), 1000.0);
			Args->SetObjectField(TEXT("location"), Location);
			TSharedRef<FJsonObject> Extent = MakeShared<FJsonObject>();
			Extent->SetNumberField(TEXT("x"), 400.0);
			Extent->SetNumberField(TEXT("y"), 500.0);
			Extent->SetNumberField(TEXT("z"), 200.0);
			Args->SetObjectField(TEXT("extent"), Extent);
			const FString Result = Execute(TEXT("add_volume"), Args);
			VolumeLabel = TEXT("PCGVolume_") + GraphName;
			TestTrue(TEXT("真实 PCGVolume 已创建"), Result.Contains(VolumeLabel));
		}
		Execute(TEXT("get_components"), MakeShared<FJsonObject>());
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Execute(TEXT("get_component_details"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Args->SetStringField(TEXT("graphPath"), GraphPath);
			Execute(TEXT("toggle_graph"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Execute(TEXT("execute"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Args->SetBoolField(TEXT("removeComponents"), true);
			Execute(TEXT("cleanup"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Execute(TEXT("force_regenerate"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Args->SetBoolField(TEXT("removeComponents"), true);
			Execute(TEXT("cleanup"), Args);
		}

		for (TActorIterator<APCGVolume> It(World); It; ++It)
		{
			if (It->GetActorLabel() == VolumeLabel)
			{
				if (It->PCGComponent)
					It->PCGComponent->CleanupLocalImmediate(true, true);
				World->EditorDestroyActor(*It, false);
				break;
			}
		}
		if (Graph)
		{
			TArray<UObject*> ObjectsToDelete = { Graph };
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}
		if (WorldPackage)
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		return true;
	}
}

#endif
