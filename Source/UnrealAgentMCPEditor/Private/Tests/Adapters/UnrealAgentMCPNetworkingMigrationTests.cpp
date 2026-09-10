// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPNetworkingMigrationTests.cpp
 * @brief Networking 应用服务与 Actor Blueprint 的真实复制配置集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Networking/UnrealAgentMCPUnrealNetworkingAdapter.h"
#include "Application/Domains/Networking/UnrealAgentMCPNetworkingService.h"
#include "Application/Ports/UnrealAgentMCPNetworkingPort.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPNetworkingMigrationIntegrationTest, "WorldData.UnrealAgent.Networking.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPNetworkingMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString BlueprintPath = TEXT("/Game/UnrealAgentAutomation/BP_NetworkingMigrationTest.BP_NetworkingMigrationTest");
		if (UObject* Existing = LoadObject<UObject>(nullptr, *BlueprintPath))
		{
			ObjectTools::DeleteObjectsUnchecked({ Existing });
		}

		UPackage* Package = CreatePackage(TEXT("/Game/UnrealAgentAutomation/BP_NetworkingMigrationTest"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(TEXT("BP_NetworkingMigrationTest")), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("UnrealAgentMCPAutomation")));
		if (!TestNotNull(TEXT("测试 Actor Blueprint 创建成功"), Blueprint))
		{
			return false;
		}

		FEdGraphPinType VariableType;
		VariableType.PinCategory = UEdGraphSchema_K2::PC_Float;
		TestTrue(TEXT("测试成员变量创建成功"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("Health")), VariableType));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		TSharedRef<IUnrealAgentMCPNetworkingPort> Port = MakeShared<FUnrealAgentMCPUnrealNetworkingAdapter>();
		FUnrealAgentMCPNetworkingService Service(Port);
		auto MakeArgs = [&BlueprintPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), BlueprintPath);
			return Args;
		};
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetBoolField(TEXT("replicates"), true);
			Execute(TEXT("set_replicates"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetStringField(TEXT("variableName"), TEXT("Health"));
			Args->SetStringField(TEXT("replicationType"), TEXT("RepNotify"));
			Execute(TEXT("set_property_replicated"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetNumberField(TEXT("netUpdateFrequency"), 33.0);
			Args->SetNumberField(TEXT("minNetUpdateFrequency"), 7.0);
			Execute(TEXT("configure_net_frequency"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetStringField(TEXT("dormancy"), TEXT("Initial"));
			Execute(TEXT("set_dormancy"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetBoolField(TEXT("loadOnClient"), false);
			Execute(TEXT("set_net_load_on_client"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetBoolField(TEXT("alwaysRelevant"), true);
			Execute(TEXT("set_always_relevant"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetBoolField(TEXT("onlyRelevantToOwner"), false);
			Execute(TEXT("set_only_relevant_to_owner"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetNumberField(TEXT("netCullDistanceSquared"), 90000.0);
			Execute(TEXT("configure_cull_distance"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetNumberField(TEXT("netPriority"), 2.5);
			Execute(TEXT("set_priority"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeArgs();
			Args->SetBoolField(TEXT("replicateMovement"), true);
			Execute(TEXT("set_replicate_movement"), Args);
		}
		const FString InfoResult = Execute(TEXT("get_info"), MakeArgs());
		TestTrue(TEXT("网络信息包含 RepNotify 与对应函数"), InfoResult.Contains(TEXT("RepNotify")) && InfoResult.Contains(TEXT("OnRep_Health")));

		AActor* Defaults = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (TestNotNull(TEXT("Actor CDO 可用"), Defaults))
		{
			TestTrue(TEXT("Actor 已启用复制"), Defaults->GetIsReplicated());
			TestEqual(TEXT("网络更新频率写入成功"), Defaults->GetNetUpdateFrequency(), 33.0f);
			TestEqual(TEXT("最小网络更新频率写入成功"), Defaults->GetMinNetUpdateFrequency(), 7.0f);
			TestEqual(TEXT("网络优先级写入成功"), Defaults->NetPriority, 2.5f);
		}

		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("Health")));
		if (TestTrue(TEXT("复制成员变量仍存在"), Blueprint->NewVariables.IsValidIndex(VariableIndex)))
		{
			const FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
			TestTrue(TEXT("成员变量带有 Net 与 RepNotify 标记"), (Variable.PropertyFlags & CPF_Net) != 0 && (Variable.PropertyFlags & CPF_RepNotify) != 0);
		}

		ObjectTools::DeleteObjectsUnchecked({ Blueprint });
		return true;
	}
}

#endif
