// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAnimationMigrationTests.cpp
 * @brief Animation 八十四项路由契约、资产创建与生命周期黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Application/Domains/Animation/UnrealAgentMCPAnimationService.h"
#include "Application/Ports/UnrealAgentMCPAnimationPort.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAnimationMigrationIntegrationTest, "WorldData.UnrealAgent.Animation.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAnimationMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPAnimationService::GetImplementedActions();
		TestEqual(TEXT("Animation action 数量"), Actions.Num(), 84);
		TSharedRef<IUnrealAgentMCPAnimationPort> Port = MakeShared<FUnrealAgentMCPUnrealAnimationAdapter>();
		FUnrealAgentMCPAnimationService Service(Port);
		TSet<FString> Invoked;
		auto Execute = [&Service, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Json = Service.Execute(Args);
			const bool bContract = Json.Contains(TEXT("\"domain\":\"animation\"")) &&
				(Json.Contains(TEXT("\"success\":true")) || (Json.Contains(TEXT("\"success\":false")) && Json.Contains(TEXT("\"error\""))));
			TestTrue(*FString::Printf(TEXT("%s 返回标准结果：%s"), *Action, *Json.Left(500)), bContract);
			TestFalse(*FString::Printf(TEXT("%s 不得落入未迁移分支"), *Action), Json.Contains(TEXT("尚未迁移")) || Json.Contains(TEXT("未识别的")));
			return Json;
		};

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		TArray<FString> CreatedPaths;
		const TSet<FString> CreateActions = { TEXT("create_sequence"), TEXT("create_montage"), TEXT("create_anim_blueprint"), TEXT("create_blendspace"),
			TEXT("create_blendspace_1d"), TEXT("create_composite"), TEXT("create_ik_rig"), TEXT("create_ik_retargeter"), TEXT("create_pose_search_database"),
			TEXT("create_pose_search_schema"), TEXT("create_mirror_data_table"), TEXT("create_pose_search_normalization_set") };

		for (const FString& Action : Actions)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			if (CreateActions.Contains(Action))
			{
				const FString Name = FString::Printf(TEXT("AN_%s_%s"), *Action, *Suffix);
				Args->SetStringField(TEXT("name"), Name);
				Args->SetStringField(TEXT("packagePath"), Directory);
				CreatedPaths.Add(Directory / Name + TEXT(".") + Name);
			}
			Execute(Action, Args);
		}

		TestEqual(TEXT("所有 Animation action 均已执行"), Invoked.Num(), Actions.Num());
		for (const FString& Action : Actions)
		{
			TestTrue(*FString::Printf(TEXT("已覆盖 action：%s"), *Action), Invoked.Contains(Action));
		}

		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			TArray<UObject*> Created;
			for (const FString& Path : CreatedPaths)
			{
				if (!UEditorAssetLibrary::DoesAssetExist(Path))
					continue;
				if (UObject* Asset = Assets ? Assets->LoadAsset(Path) : nullptr)
					Created.Add(Asset);
			}
			if (!Created.IsEmpty())
				ObjectTools::DeleteObjectsUnchecked(Created);
		}
		return true;
	}
}

#endif
