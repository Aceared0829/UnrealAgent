// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEditorMigrationTests.cpp
 * @brief 编辑器 Toolset 独立应用服务与首批原生动作黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"
#include "Application/Domains/Editor/UnrealAgentMCPEditorService.h"
#include "Application/Ports/UnrealAgentMCPEditorPort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/Adapters/UnrealAgentMCPAssetTestTypes.h"
#include "Tests/Core/UnrealAgentMCPReflectionTestTypes.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEditorActionContractTest, "WorldData.UnrealAgent.Editor.ActionContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPEditorActionContractTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPEditorService::GetImplementedActions();
		TestEqual(TEXT("第四批编辑器动作累计数量"), Actions.Num(), 71);
		TestTrue(TEXT("包含对象反射调用"), Actions.Contains(TEXT("invoke_object_function")));
		TestTrue(TEXT("包含视口状态读取"), Actions.Contains(TEXT("get_viewport")));
		TestTrue(TEXT("包含脏包审计"), Actions.Contains(TEXT("list_dirty_packages")));
		TestTrue(TEXT("包含 PIE 配置能力"), Actions.Contains(TEXT("configure_pie")) && Actions.Contains(TEXT("get_pie_config")));
		TestTrue(TEXT("包含项目崩溃诊断"), Actions.Contains(TEXT("list_crashes")) && Actions.Contains(TEXT("get_crash_info")));
		TestTrue(TEXT("包含地图构建与对话框策略"), Actions.Contains(TEXT("build_all")) && Actions.Contains(TEXT("set_dialog_policy")));
		TestTrue(TEXT("包含完整 Sequencer 编辑闭环"), Actions.Contains(TEXT("create_sequence")) && Actions.Contains(TEXT("set_sequence_keyframes")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEditorIntegrationTest, "WorldData.UnrealAgent.Editor.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPEditorIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString AssetName = TEXT("DA_Editor_") + Suffix;
		const FString PackageName = TEXT("/Game/UnrealAgentAutomation/Editor_") + Suffix + TEXT("/") + AssetName;
		const FString AssetPath = PackageName + TEXT(".") + AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		UUnrealAgentMCPAssetTestData* Asset = NewObject<UUnrealAgentMCPAssetTestData>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		Asset->Label = TEXT("编辑器迁移测试");
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();

		TSharedRef<FUnrealAgentMCPUnrealEditorAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealEditorAdapter>();
		TSharedRef<IUnrealAgentMCPEditorPort> Port = Adapter;
		FUnrealAgentMCPEditorService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Args);
			if (bExpectSuccess)
			{
				TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1600)), Result.Contains(TEXT("\"success\":true")));
			}
			return Result;
		};
		auto ForAsset = [&AssetPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("objectPath"), AssetPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("propertyName"), TEXT("bEnabled"));
			Args->SetField(TEXT("value"), MakeShared<FJsonValueBoolean>(true));
			Execute(TEXT("set_property"), Args);
			TestTrue(TEXT("编辑器属性工具真实写入 UObject"), Asset->bEnabled);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("propertyName"), TEXT("bEnabled"));
			const FString Result = Execute(TEXT("get_property"), Args);
			TestTrue(TEXT("属性读取返回布尔真值"), Result.Contains(TEXT("\"value\":true")));
		}
		{
			const FString Result = Execute(TEXT("describe_object"), ForAsset());
			TestTrue(TEXT("对象说明包含测试属性"), Result.Contains(TEXT("bEnabled")) && Result.Contains(TEXT("Label")));
		}
		{
			const FString Result = Execute(TEXT("get_object_properties"), ForAsset());
			TestTrue(TEXT("对象属性快照包含中文字符串"), Result.Contains(TEXT("编辑器迁移测试")));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("objectPath"), UUnrealAgentMCPReflectionTestToolset::StaticClass()->GetPathName());
			Args->SetStringField(TEXT("functionName"), TEXT("Add"));
			TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
			Arguments->SetNumberField(TEXT("A"), 7);
			Arguments->SetNumberField(TEXT("B"), 5);
			Args->SetObjectField(TEXT("arguments"), Arguments);
			const FString Result = Execute(TEXT("invoke_object_function"), Args);
			TestTrue(TEXT("反射函数返回 12"), Result.Contains(TEXT("\"ReturnValue\":12")));
		}

		Execute(TEXT("list_function_libraries"), MakeShared<FJsonObject>());
		Execute(TEXT("get_perf_stats"), MakeShared<FJsonObject>());
		Execute(TEXT("get_build_status"), MakeShared<FJsonObject>());
		{
			const FString Result = Execute(TEXT("get_pie_config"), MakeShared<FJsonObject>());
			TestTrue(TEXT("PIE 配置由反射适配器真实读取"), Result.Contains(TEXT("PlayNumberOfClients")));
		}
		{
			int32 ExistingClients = 1;
			GetDefault<ULevelEditorPlaySettings>()->GetPlayNumberOfClients(ExistingClients);
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetNumberField(TEXT("numPlayers"), ExistingClients);
			Args->SetObjectField(TEXT("settings"), Settings);
			const FString Result = Execute(TEXT("configure_pie"), Args);
			TestTrue(TEXT("PIE 配置别名映射到原生配置字段"), Result.Contains(TEXT("PlayNumberOfClients")));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("classFilter"), TEXT("WorldSettings"));
			Args->SetArrayField(TEXT("properties"), { MakeShared<FJsonValueString>(TEXT("actorLabel")), MakeShared<FJsonValueString>(TEXT("location")) });
			const FString Result = Execute(TEXT("get_runtime_values"), Args);
			TestTrue(TEXT("批量运行时读取可回退到编辑器世界"), Result.Contains(TEXT("\"playInEditor\":false")) && Result.Contains(TEXT("\"rows\":[")));
		}
		Execute(TEXT("get_message_log"), MakeShared<FJsonObject>());
		Execute(TEXT("list_crashes"), MakeShared<FJsonObject>());
		Execute(TEXT("check_for_crashes"), MakeShared<FJsonObject>());
		{
			const FString Result = Execute(TEXT("reload_bridge"), MakeShared<FJsonObject>());
			TestTrue(TEXT("原生静态注册表明确报告无外部缓存依赖"), Result.Contains(TEXT("registryMode")) && Result.Contains(TEXT("moduleLoaded")));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("pattern"), TEXT("Unreal Agent 自动化测试"));
			Args->SetStringField(TEXT("response"), TEXT("cancel"));
			Execute(TEXT("set_dialog_policy"), Args);
			const FString Policies = Execute(TEXT("get_dialog_policy"), MakeShared<FJsonObject>());
			TestTrue(TEXT("对话框策略可原生注册并读取"), Policies.Contains(TEXT("Unreal Agent 自动化测试")) && Policies.Contains(TEXT("cancel")));
			Execute(TEXT("list_dialogs"), MakeShared<FJsonObject>());
			Execute(TEXT("clear_dialog_policy"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), TEXT("/Game;Quit"));
			const FString Result = Execute(TEXT("validate_assets"), Args, false);
			TestTrue(TEXT("资产验证拒绝命令分隔符"), Result.Contains(TEXT("\"success\":false")));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("platform"), TEXT("Windows&Quit"));
			const FString Result = Execute(TEXT("cook_content"), Args, false);
			TestTrue(TEXT("Cook 平台参数拒绝命令注入"), Result.Contains(TEXT("\"success\":false")));
		}
		{
			const FString SequenceName = TEXT("LS_Editor_") + Suffix;
			const FString SequenceDirectory = TEXT("/Game/UnrealAgentAutomation/Editor_") + Suffix;
			const FString SequencePath = SequenceDirectory + TEXT("/") + SequenceName + TEXT(".") + SequenceName;
			TSharedRef<FJsonObject> CreateArgs = MakeShared<FJsonObject>();
			CreateArgs->SetStringField(TEXT("name"), SequenceName);
			CreateArgs->SetStringField(TEXT("packagePath"), SequenceDirectory);
			Execute(TEXT("create_sequence"), CreateArgs);

			const FString WorldSettingsLabel = GEditor->GetEditorWorldContext().World()->GetWorldSettings()->GetActorLabel();
			TSharedRef<FJsonObject> TrackArgs = MakeShared<FJsonObject>();
			TrackArgs->SetStringField(TEXT("assetPath"), SequencePath);
			TrackArgs->SetStringField(TEXT("trackType"), TEXT("Transform"));
			TrackArgs->SetStringField(TEXT("actorLabel"), WorldSettingsLabel);
			Execute(TEXT("add_sequence_track"), TrackArgs);

			TSharedRef<FJsonObject> SectionArgs = MakeShared<FJsonObject>();
			SectionArgs->SetStringField(TEXT("assetPath"), SequencePath);
			SectionArgs->SetStringField(TEXT("trackType"), TEXT("Transform"));
			SectionArgs->SetNumberField(TEXT("startFrame"), 0);
			SectionArgs->SetNumberField(TEXT("endFrame"), 60);
			Execute(TEXT("add_sequence_section"), SectionArgs);

			TSharedRef<FJsonObject> KeyArgs = MakeShared<FJsonObject>();
			KeyArgs->SetStringField(TEXT("assetPath"), SequencePath);
			KeyArgs->SetStringField(TEXT("trackType"), TEXT("Transform"));
			KeyArgs->SetStringField(TEXT("channel"), TEXT("Location.X"));
			TSharedRef<FJsonObject> Key0 = MakeShared<FJsonObject>();
			Key0->SetNumberField(TEXT("frame"), 0);
			Key0->SetNumberField(TEXT("value"), 10);
			TSharedRef<FJsonObject> Key1 = MakeShared<FJsonObject>();
			Key1->SetNumberField(TEXT("frame"), 30);
			Key1->SetNumberField(TEXT("value"), 50);
			KeyArgs->SetArrayField(TEXT("keys"), { MakeShared<FJsonValueObject>(Key0), MakeShared<FJsonValueObject>(Key1) });
			Execute(TEXT("set_sequence_keyframes"), KeyArgs);

			TSharedRef<FJsonObject> RangeArgs = MakeShared<FJsonObject>();
			RangeArgs->SetStringField(TEXT("assetPath"), SequencePath);
			RangeArgs->SetNumberField(TEXT("startFrame"), 0);
			RangeArgs->SetNumberField(TEXT("endFrame"), 90);
			Execute(TEXT("set_sequence_playback_range"), RangeArgs);
			const FString SequenceInfo = Execute(TEXT("get_sequence_info"), RangeArgs);
			TestTrue(TEXT("Sequencer 轨道、区段和播放范围均可回读"), SequenceInfo.Contains(TEXT("MovieScene3DTransformTrack")) && SequenceInfo.Contains(TEXT("\"endFrame\":90")));

			if (ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath))
			{
				Sequence->ClearFlags(RF_Public | RF_Standalone);
				Sequence->GetOutermost()->SetDirtyFlag(false);
				FAssetRegistryModule::AssetDeleted(Sequence);
			}
		}
		{
			const FString Result = Execute(TEXT("read_bone_transforms"), MakeShared<FJsonObject>(), false);
			TestTrue(TEXT("无 PIE 时运行时骨骼动作返回结构化错误"), Result.Contains(TEXT("\"success\":false")) && Result.Contains(TEXT("PIE")));
		}
		{
			const FString Result = Execute(TEXT("list_dirty_packages"), MakeShared<FJsonObject>());
			TestTrue(TEXT("脏包列表包含本轮测试资产"), Result.Contains(PackageName));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), TEXT("WorldData.UnrealAgent.Editor"));
			Args->SetNumberField(TEXT("limit"), 20);
			Execute(TEXT("search_log"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetObjectField(TEXT("cvars"),
				[]()
				{
					TSharedRef<FJsonObject> Cvars = MakeShared<FJsonObject>();
					Cvars->SetNumberField(TEXT("t.MaxFPS"), 0);
					return Cvars;
				}());
			Execute(TEXT("set_cvars"), Args);
		}

		Asset->ClearFlags(RF_Public | RF_Standalone);
		Package->SetDirtyFlag(false);
		FAssetRegistryModule::AssetDeleted(Asset);
		return true;
	}
}

#endif
