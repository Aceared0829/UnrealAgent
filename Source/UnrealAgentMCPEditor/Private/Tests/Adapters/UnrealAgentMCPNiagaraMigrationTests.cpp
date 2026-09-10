// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPNiagaraMigrationTests.cpp
 * @brief Niagara 三十一项资产、组件、发射器、渲染器与模块栈往返黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPNiagaraMigrationIntegrationTest, "WorldData.UnrealAgent.Niagara.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPNiagaraMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealNiagaraAdapter::GetImplementedActions();
		TestEqual(TEXT("Niagara action 数量"), Actions.Num(), 31);
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		const FString SystemName = TEXT("NS_MCP_") + Suffix;
		const FString EmitterName = TEXT("NE_MCP_") + Suffix;
		const FString ScratchName = TEXT("NM_Scratch_") + Suffix;
		const FString HlslName = TEXT("NM_Hlsl_") + Suffix;
		const FString SpecName = TEXT("NS_Spec_") + Suffix;
		const FString SystemPath = Directory + TEXT("/") + SystemName + TEXT(".") + SystemName;
		const FString EmitterPath = Directory + TEXT("/") + EmitterName + TEXT(".") + EmitterName;
		const FString ScratchPath = Directory + TEXT("/") + ScratchName + TEXT(".") + ScratchName;
		const FString HlslPath = Directory + TEXT("/") + HlslName + TEXT(".") + HlslName;
		const FString SpecPath = Directory + TEXT("/") + SpecName + TEXT(".") + SpecName;
		const FString ActorLabel = TEXT("MCP_Niagara_") + Suffix;

		FUnrealAgentMCPUnrealNiagaraAdapter Adapter;
		TSet<FString> Invoked;
		auto Execute = [&Adapter, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Adapter.Execute(Args);
			if (bExpectSuccess)
			{
				TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1200)), Result.Contains(TEXT("\"success\":true")));
			}
			else
			{
				TestTrue(*FString::Printf(TEXT("%s 返回可诊断错误"), *Action), Result.Contains(TEXT("\"success\":false")) && Result.Contains(TEXT("\"error\"")));
			}
			return Result;
		};
		auto ForSystem = [&SystemPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("systemPath"), SystemPath);
			return Args;
		};

		for (const TPair<FString, FString>& Creation : { TPair<FString, FString>(TEXT("create_emitter"), EmitterName), TPair<FString, FString>(TEXT("create"), SystemName),
				 TPair<FString, FString>(TEXT("create_scratch_module"), ScratchName) })
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), Creation.Value);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Execute(Creation.Key, Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), HlslName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Args->SetStringField(TEXT("hlsl"), TEXT("float MCPValue = 1.0;"));
			Execute(TEXT("create_module_from_hlsl"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("emitterPath"), EmitterPath);
			Execute(TEXT("add_emitter"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), Directory);
			Execute(TEXT("list"), Args);
			Execute(TEXT("list_modules"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), SystemPath);
			Execute(TEXT("get_info"), Args);
			Execute(TEXT("validate"), Args);
		}
		Execute(TEXT("list_emitters"), ForSystem());
		Execute(TEXT("list_renderers"), ForSystem());
		Execute(TEXT("inspect_data_interfaces"), ForSystem());
		Execute(TEXT("list_system_parameters"), ForSystem());
		Execute(TEXT("get_compiled_hlsl"), ForSystem());
		Execute(TEXT("list_module_inputs"), ForSystem());
		Execute(TEXT("list_static_switches"), ForSystem());
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), EmitterPath);
			Execute(TEXT("get_emitter_info"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("propertyName"), TEXT("enabled"));
			Args->SetBoolField(TEXT("value"), false);
			Execute(TEXT("set_emitter_property"), Args);
			Args->SetBoolField(TEXT("value"), true);
			Execute(TEXT("set_emitter_property"), Args);
		}
		int32 AddedRendererIndex = 0;
		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("rendererType"), TEXT("sprite"));
			const FString Result = Execute(TEXT("add_renderer"), Args);
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				double Index = 0.0;
				Parsed->TryGetNumberField(TEXT("rendererIndex"), Index);
				AddedRendererIndex = static_cast<int32>(Index);
			}
		}
		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetNumberField(TEXT("rendererIndex"), AddedRendererIndex);
			Args->SetStringField(TEXT("propertyName"), TEXT("bIsEnabled"));
			Args->SetBoolField(TEXT("value"), false);
			Execute(TEXT("set_renderer_property"), Args);
			Execute(TEXT("remove_renderer"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("modulePath"), ScratchPath);
			Args->SetStringField(TEXT("stackContext"), TEXT("ParticleSpawn"));
			Execute(TEXT("add_module"), Args);
		}
		for (const TPair<FString, FString>& Operation :
			{ TPair<FString, FString>(TEXT("set_module_input"), TEXT("inputName")), TPair<FString, FString>(TEXT("set_static_switch"), TEXT("switchName")) })
		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("moduleName"), TEXT("MissingModule"));
			Args->SetStringField(Operation.Value, TEXT("MissingInput"));
			Args->SetStringField(TEXT("value"), TEXT("1"));
			Execute(Operation.Key, Args, false);
		}

		for (const FString& SpawnAction : { TEXT("spawn"), TEXT("spawn_actor") })
		{
			TSharedRef<FJsonObject> Args = ForSystem();
			Args->SetStringField(TEXT("label"), SpawnAction == TEXT("spawn_actor") ? ActorLabel : ActorLabel + TEXT("_Auto"));
			TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 0.0);
			Location->SetNumberField(TEXT("y"), 0.0);
			Location->SetNumberField(TEXT("z"), 100.0);
			Args->SetObjectField(TEXT("location"), Location);
			Execute(SpawnAction, Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), ActorLabel);
			Execute(TEXT("reactivate"), Args);
			Args->SetStringField(TEXT("parameterName"), TEXT("User.MCPValue"));
			Args->SetStringField(TEXT("parameterType"), TEXT("float"));
			Args->SetNumberField(TEXT("value"), 2.5);
			Execute(TEXT("set_parameter"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), SpecName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			TSharedRef<FJsonObject> Emitter = MakeShared<FJsonObject>();
			Emitter->SetStringField(TEXT("path"), EmitterPath);
			Args->SetArrayField(TEXT("emitters"), { MakeShared<FJsonValueObject>(Emitter) });
			Execute(TEXT("create_system_from_spec"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
			Operation->SetStringField(TEXT("action"), TEXT("list"));
			Operation->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
			Args->SetArrayField(TEXT("ops"), { MakeShared<FJsonValueObject>(Operation) });
			Execute(TEXT("batch"), Args);
		}
		Execute(TEXT("remove_emitter"), ForSystem());

		for (const FString& Action : Actions)
		{
			TestTrue(*FString::Printf(TEXT("Niagara action 已黑盒调用：%s"), *Action), Invoked.Contains(Action));
		}

		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			TArray<AActor*> ActorsToDestroy;
			for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
			{
				if (It->GetActorLabel().StartsWith(ActorLabel))
				{
					ActorsToDestroy.Add(*It);
				}
			}
			for (AActor* Actor : ActorsToDestroy)
			{
				Actor->Destroy();
			}
		}
		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			TArray<UObject*> CreatedAssets;
			for (const FString& Path : { SystemPath, EmitterPath, ScratchPath, HlslPath, SpecPath })
			{
				if (UObject* Asset = Assets ? Assets->LoadAsset(Path) : nullptr)
				{
					CreatedAssets.Add(Asset);
				}
			}
			if (!CreatedAssets.IsEmpty())
			{
				ObjectTools::DeleteObjectsUnchecked(CreatedAssets);
			}
		}
		return true;
	}
}

#endif
