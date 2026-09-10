// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFoliageMigrationTests.cpp
 * @brief Foliage 应用服务与 Unreal Adapter 的真实植被资产和实例集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Foliage/UnrealAgentMCPUnrealFoliageAdapter.h"
#include "Application/Domains/Foliage/UnrealAgentMCPFoliageService.h"
#include "Application/Ports/UnrealAgentMCPFoliagePort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPFoliageMigrationIntegrationTest, "WorldData.UnrealAgent.Foliage.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPFoliageMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
			return false;
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();

		TSharedRef<IUnrealAgentMCPFoliagePort> Port = MakeShared<FUnrealAgentMCPUnrealFoliageAdapter>();
		FUnrealAgentMCPFoliageService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		const FString TypePath = TEXT("/Game/UnrealAgentAutomation/FT_FoliageMigrationTest.FT_FoliageMigrationTest");
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("meshPath"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			Args->SetStringField(TEXT("name"), TEXT("FT_FoliageMigrationTest"));
			Args->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
			const FString Result = Execute(TEXT("create_type"), Args);
			TestTrue(TEXT("创建结果包含植被类型资产路径"), Result.Contains(TEXT("FT_FoliageMigrationTest")));
		}

		UFoliageType_InstancedStaticMesh* Type = LoadObject<UFoliageType_InstancedStaticMesh>(nullptr, *TypePath);
		if (!TestNotNull(TEXT("植被类型资产已加载"), Type))
			return false;

		AInstancedFoliageActor* ExistingActor = AInstancedFoliageActor::Get(World, false, World->GetCurrentLevel(), FVector(120.0, 80.0, 30.0));
		AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::Get(World, true, World->GetCurrentLevel(), FVector(120.0, 80.0, 30.0));
		if (!TestNotNull(TEXT("InstancedFoliageActor 可用"), FoliageActor))
		{
			ObjectTools::DeleteObjectsUnchecked({ Type });
			return false;
		}

		FFoliageInfo* Info = FoliageActor->AddMesh(Type);
		if (!TestNotNull(TEXT("植被实例实现已初始化"), Info))
		{
			ObjectTools::DeleteObjectsUnchecked({ Type });
			return false;
		}
		FFoliageInstance Instance;
		Instance.SetInstanceWorldTransform(FTransform(FRotator(0.0, 25.0, 0.0), FVector(120.0, 80.0, 30.0), FVector(1.25, 1.25, 1.25)));
		Info->AddInstance(Type, Instance);

		const FString ListResult = Execute(TEXT("list_types"), MakeShared<FJsonObject>());
		TestTrue(TEXT("关卡植被类型列表包含测试类型"), ListResult.Contains(TEXT("FT_FoliageMigrationTest")));

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("foliageTypeName"), TEXT("FT_FoliageMigrationTest"));
			Execute(TEXT("get_settings"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Center = MakeShared<FJsonObject>();
			Center->SetNumberField(TEXT("x"), 100.0);
			Center->SetNumberField(TEXT("y"), 100.0);
			Center->SetNumberField(TEXT("z"), 30.0);
			Args->SetObjectField(TEXT("center"), Center);
			Args->SetNumberField(TEXT("radius"), 500.0);
			Args->SetStringField(TEXT("foliageType"), TEXT("FT_FoliageMigrationTest"));
			const FString Result = Execute(TEXT("sample"), Args);
			TestTrue(TEXT("空间采样至少返回一个真实实例"), Result.Contains(TEXT("\"matchedCount\":1")) || Result.Contains(TEXT("\"matchedCount\": 1")));
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("foliageTypeName"), TypePath);
			TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetNumberField(TEXT("density"), 42.0);
			Settings->SetNumberField(TEXT("radius"), 160.0);
			Args->SetObjectField(TEXT("settings"), Settings);
			Execute(TEXT("set_settings"), Args);
			TestEqual(TEXT("Density 已通过反射设置写入"), Type->Density, 42.0f);
			TestEqual(TEXT("Radius 已通过反射设置写入"), Type->Radius, 160.0f);
		}

		UFoliageType* TypesToRemove[] = { Type };
		FoliageActor->RemoveFoliageType(TypesToRemove, UE_ARRAY_COUNT(TypesToRemove));
		if (!ExistingActor)
			World->EditorDestroyActor(FoliageActor, false);
		ObjectTools::DeleteObjectsUnchecked({ Type });
		if (WorldPackage)
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		return true;
	}
}

#endif
