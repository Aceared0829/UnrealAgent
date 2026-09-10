// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAudioMigrationTests.cpp
 * @brief Audio 三十六项资产、图编排、路由与试听能力的黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/AmbientSound.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAudioMigrationIntegrationTest, "WorldData.UnrealAgent.Audio.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAudioMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealAudioAdapter::GetImplementedActions();
		TestEqual(TEXT("Audio action 数量"), Actions.Num(), 36);

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		auto MakePath = [&Directory](const FString& Name)
		{
			return Directory + TEXT("/") + Name + TEXT(".") + Name;
		};
		const FString CueName = TEXT("SC_MCP_") + Suffix;
		const FString MetaName = TEXT("MS_MCP_") + Suffix;
		const FString ParentSubmixName = TEXT("SM_Parent_") + Suffix;
		const FString ChildSubmixName = TEXT("SM_Child_") + Suffix;
		const FString ClassName = TEXT("SC_Class_") + Suffix;
		const FString MixName = TEXT("SMix_MCP_") + Suffix;
		const FString ConcurrencyName = TEXT("SC_Concurrency_") + Suffix;
		const FString AttenuationName = TEXT("SA_MCP_") + Suffix;
		const FString CuePath = MakePath(CueName);
		const FString MetaPath = MakePath(MetaName);
		const FString ParentSubmixPath = MakePath(ParentSubmixName);
		const FString ChildSubmixPath = MakePath(ChildSubmixName);
		const FString ClassPath = MakePath(ClassName);
		const FString MixPath = MakePath(MixName);
		const FString ConcurrencyPath = MakePath(ConcurrencyName);
		const FString AttenuationPath = MakePath(AttenuationName);
		const FString AmbientLabel = TEXT("MCP_Audio_") + Suffix;

		FUnrealAgentMCPUnrealAudioAdapter Adapter;
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
				TestTrue(*FString::Printf(TEXT("%s 返回可诊断错误：%s"), *Action, *Result.Left(1200)),
					Result.Contains(TEXT("\"success\":false")) && Result.Contains(TEXT("\"error\"")));
			}
			return Result;
		};
		auto ParseString = [](const FString& Json, const FString& Field)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			FString Value;
			if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
			{
				Object->TryGetStringField(Field, Value);
			}
			return Value;
		};
		auto Named = [&Directory](const FString& Name)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), Name);
			Args->SetStringField(TEXT("packagePath"), Directory);
			return Args;
		};
		auto ForSound = [&CuePath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), CuePath);
			return Args;
		};
		auto ForMeta = [&MetaPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), MetaPath);
			return Args;
		};

		Execute(TEXT("create_cue"), Named(CueName));
		Execute(TEXT("create_metasound"), Named(MetaName));
		Execute(TEXT("create_submix"), Named(ParentSubmixName));
		Execute(TEXT("create_submix"), Named(ChildSubmixName));
		Execute(TEXT("create_sound_class"), Named(ClassName));
		Execute(TEXT("create_sound_mix"), Named(MixName));
		Execute(TEXT("create_concurrency"), Named(ConcurrencyName));
		Execute(TEXT("create_attenuation"), Named(AttenuationName));

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("path"), Directory);
			Execute(TEXT("list"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForSound();
			Args->SetStringField(TEXT("propertyName"), TEXT("VolumeMultiplier"));
			Args->SetNumberField(TEXT("value"), 0.75);
			Execute(TEXT("set_property"), Args);
		}
		Execute(TEXT("extract_pcm"), ForSound(), false);
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("filePath"), TEXT("Z:/不存在/音频.wav"));
			Execute(TEXT("import_audio"), Args, false);
		}

		int32 MixerIndex = INDEX_NONE;
		int32 WaveIndex = INDEX_NONE;
		{
			TSharedRef<FJsonObject> Args = ForSound();
			Args->SetStringField(TEXT("nodeClass"), TEXT("Mixer"));
			const FString Result = Execute(TEXT("cue_add_node"), Args);
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				double Value = INDEX_NONE;
				Parsed->TryGetNumberField(TEXT("nodeIndex"), Value);
				MixerIndex = static_cast<int32>(Value);
			}
			Args = ForSound();
			Args->SetStringField(TEXT("nodeClass"), TEXT("WavePlayer"));
			const FString WaveResult = Execute(TEXT("cue_add_node"), Args);
			Parsed.Reset();
			const TSharedRef<TJsonReader<>> WaveReader = TJsonReaderFactory<>::Create(WaveResult);
			if (FJsonSerializer::Deserialize(WaveReader, Parsed) && Parsed.IsValid())
			{
				double Value = INDEX_NONE;
				Parsed->TryGetNumberField(TEXT("nodeIndex"), Value);
				WaveIndex = static_cast<int32>(Value);
			}
		}
		{
			TSharedRef<FJsonObject> Args = ForSound();
			Args->SetNumberField(TEXT("fromNodeIndex"), MixerIndex);
			Args->SetNumberField(TEXT("toNodeIndex"), WaveIndex);
			Execute(TEXT("cue_connect"), Args);
		}
		Execute(TEXT("cue_get_graph"), ForSound());
		{
			TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
			Operation->SetStringField(TEXT("action"), TEXT("cue_get_graph"));
			TSharedRef<FJsonObject> Args = ForSound();
			Args->SetArrayField(TEXT("operations"), { MakeShared<FJsonValueObject>(Operation) });
			Execute(TEXT("cue_author"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForSound();
			TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 0.0);
			Location->SetNumberField(TEXT("y"), 0.0);
			Location->SetNumberField(TEXT("z"), 100.0);
			Args->SetObjectField(TEXT("location"), Location);
			Execute(TEXT("play_at_location"), Args);
			Args->SetStringField(TEXT("label"), AmbientLabel);
			Execute(TEXT("spawn_ambient"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), ChildSubmixPath);
			Args->SetStringField(TEXT("parentPath"), ParentSubmixPath);
			Execute(TEXT("set_submix_parent"), Args);
			Args->SetStringField(TEXT("effectPath"), TEXT("/Game/Missing.Missing"));
			Execute(TEXT("add_submix_effect"), Args, false);
		}
		auto RouteSound = [&ForSound, &Execute](const FString& Action, const FString& Field, const FString& Target)
		{
			TSharedRef<FJsonObject> Args = ForSound();
			Args->SetStringField(Field, Target);
			Execute(Action, Args);
		};
		RouteSound(TEXT("set_sound_submix"), TEXT("submixPath"), ChildSubmixPath);
		RouteSound(TEXT("add_sound_submix_send"), TEXT("submixPath"), ParentSubmixPath);
		RouteSound(TEXT("set_sound_class"), TEXT("soundClassPath"), ClassPath);
		RouteSound(TEXT("set_sound_attenuation"), TEXT("attenuationPath"), AttenuationPath);
		RouteSound(TEXT("set_sound_concurrency"), TEXT("concurrencyPath"), ConcurrencyPath);

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("search"), TEXT("Sine"));
			Execute(TEXT("metasound_list_node_classes"), Args);
		}
		Execute(TEXT("metasound_get_graph"), ForMeta());
		{
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetStringField(TEXT("name"), TEXT("MCPFrequency"));
			Args->SetStringField(TEXT("dataType"), TEXT("Float"));
			Args->SetNumberField(TEXT("defaultValue"), 440.0);
			Execute(TEXT("metasound_add_input"), Args);
			Args = ForMeta();
			Args->SetStringField(TEXT("name"), TEXT("MCPValue"));
			Args->SetStringField(TEXT("dataType"), TEXT("Float"));
			Execute(TEXT("metasound_add_output"), Args);
		}
		FString SineNodeId;
		{
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetStringField(TEXT("className"), TEXT("Sine"));
			Args->SetStringField(TEXT("namespace"), TEXT("UE"));
			Args->SetStringField(TEXT("variant"), TEXT("Audio"));
			SineNodeId = ParseString(Execute(TEXT("metasound_add_node"), Args), TEXT("nodeId"));
		}
		{
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetStringField(TEXT("nodeId"), SineNodeId);
			Args->SetStringField(TEXT("inputName"), TEXT("Frequency"));
			Args->SetStringField(TEXT("dataType"), TEXT("Float"));
			Args->SetNumberField(TEXT("value"), 220.0);
			Execute(TEXT("metasound_set_default"), Args);
			Args->SetStringField(TEXT("graphInput"), TEXT("MCPFrequency"));
			Execute(TEXT("metasound_connect_input"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetStringField(TEXT("nodeId"), SineNodeId);
			Args->SetStringField(TEXT("outputName"), TEXT("Audio"));
			Execute(TEXT("metasound_connect_audio_out"), Args);
			Args->SetStringField(TEXT("graphOutput"), TEXT("MCPValue"));
			Execute(TEXT("metasound_connect_output"), Args, false);
		}
		{
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetStringField(TEXT("fromNodeId"), SineNodeId);
			Args->SetStringField(TEXT("toNodeId"), SineNodeId);
			Args->SetStringField(TEXT("outputName"), TEXT("Audio"));
			Args->SetStringField(TEXT("inputName"), TEXT("Frequency"));
			Execute(TEXT("metasound_connect"), Args, false);
		}
		{
			TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
			Operation->SetStringField(TEXT("action"), TEXT("metasound_get_graph"));
			TSharedRef<FJsonObject> Args = ForMeta();
			Args->SetArrayField(TEXT("operations"), { MakeShared<FJsonValueObject>(Operation) });
			Execute(TEXT("metasound_author"), Args);
		}
		Execute(TEXT("metasound_build"), ForMeta());

		TestEqual(TEXT("全部 Audio action 均已执行"), Invoked.Num(), Actions.Num());
		for (const FString& Action : Actions)
		{
			TestTrue(*FString::Printf(TEXT("已覆盖 action：%s"), *Action), Invoked.Contains(Action));
		}

		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			TArray<AActor*> Actors;
			for (TActorIterator<AAmbientSound> It(GEditor->GetEditorWorldContext().World()); It; ++It)
			{
				if (It->GetActorLabel() == AmbientLabel)
				{
					Actors.Add(*It);
				}
			}
			for (AActor* Actor : Actors)
			{
				Actor->Destroy();
			}
		}
		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			const TArray<FString> CreatedAssetPaths = { CuePath, MetaPath, ParentSubmixPath, ChildSubmixPath, ClassPath, MixPath, ConcurrencyPath, AttenuationPath };
			TArray<UObject*> CreatedAssets;
			for (const FString& Path : CreatedAssetPaths)
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
			for (const FString& Path : CreatedAssetPaths)
			{
				const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
				if (Assets && FPackageName::DoesPackageExist(PackageName))
				{
					Assets->DeleteAsset(PackageName);
				}
				TestFalse(*FString::Printf(TEXT("测试资产已清理：%s"), *Path), FPackageName::DoesPackageExist(PackageName));
			}
		}
		return true;
	}
}

#endif
