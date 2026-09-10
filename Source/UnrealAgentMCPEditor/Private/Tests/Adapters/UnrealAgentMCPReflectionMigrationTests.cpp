// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPReflectionMigrationTests.cpp
 * @brief Reflection 应用服务与 Unreal Adapter 的真实编辑器集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Reflection/UnrealAgentMCPUnrealReflectionAdapter.h"
#include "Application/Domains/Reflection/UnrealAgentMCPReflectionService.h"
#include "Application/Ports/UnrealAgentMCPReflectionPort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/SaveGame.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPReflectionMigrationIntegrationTest, "WorldData.UnrealAgent.Reflection.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPReflectionMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		TSharedRef<IUnrealAgentMCPReflectionPort> Port = MakeShared<FUnrealAgentMCPUnrealReflectionAdapter>();
		FUnrealAgentMCPReflectionService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("className"), TEXT("Actor"));
			Args->SetBoolField(TEXT("includeInherited"), false);
			Execute(TEXT("reflect_class"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("structName"), TEXT("Vector"));
			Execute(TEXT("reflect_struct"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("enumName"), TEXT("EComponentMobility"));
			Execute(TEXT("reflect_enum"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("parentFilter"), TEXT("Actor"));
			Args->SetNumberField(TEXT("limit"), 20);
			Execute(TEXT("list_classes"), Args);
		}

		Execute(TEXT("list_tags"), MakeShared<FJsonObject>());

		FString TagsConfigPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGameplayTags.ini")));
		FPaths::NormalizeFilename(TagsConfigPath);
		const bool bTagsConfigExisted = IFileManager::Get().FileExists(*TagsConfigPath);
		FString OriginalTagsConfig;
		if (bTagsConfigExisted)
			FFileHelper::LoadFileToString(OriginalTagsConfig, *TagsConfigPath);
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("tag"), TEXT("UnrealAgentMCP.Automation.Reflection"));
			Args->SetStringField(TEXT("comment"), TEXT("Reflection 自动化测试标签"));
			Execute(TEXT("create_tag"), Args);
		}

		const FString EnumAssetPath = TEXT("/Game/UnrealAgentAutomation/EReflectionMigrationTest.EReflectionMigrationTest");
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), TEXT("EReflectionMigrationTest"));
			Args->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
			Args->SetStringField(TEXT("onConflict"), TEXT("overwrite"));
			Args->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueString>(TEXT("Wall")), MakeShared<FJsonValueString>(TEXT("House")) });
			Execute(TEXT("create_enum"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), EnumAssetPath);
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), TEXT("Castle"));
			Entry->SetStringField(TEXT("displayName"), TEXT("城堡"));
			Args->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueObject>(Entry), MakeShared<FJsonValueString>(TEXT("Village")) });
			Execute(TEXT("set_enum_entries"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("enumName"), EnumAssetPath);
			const FString Result = Execute(TEXT("reflect_enum"), Args);
			TestTrue(TEXT("更新后的枚举包含 Castle"), Result.Contains(TEXT("Castle")));
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("className"), TEXT("Actor"));
			const FString Result = Execute(TEXT("is_class_loaded"), Args);
			TestTrue(TEXT("Actor 类已加载且存在"), Result.Contains(TEXT("\"loaded\":true")) && Result.Contains(TEXT("\"exists\":true")));
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("moduleName"), TEXT("Engine"));
			Execute(TEXT("is_module_loaded"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("filter"), TEXT("Engine"));
			Args->SetBoolField(TEXT("loadedOnly"), true);
			Execute(TEXT("list_loaded_modules"), Args);
		}

		const FString SlotName = TEXT("UnrealAgentMCP_ReflectionMigration_Automation");
		UPackage* SaveGamePackage = CreatePackage(TEXT("/Game/UnrealAgentAutomation/BP_ReflectionSaveGameTest"));
		UBlueprint* SaveGameBlueprint = FKismetEditorUtilities::CreateBlueprint(USaveGame::StaticClass(), SaveGamePackage, FName(TEXT("BP_ReflectionSaveGameTest")), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("UnrealAgentMCPAutomation")));
		FKismetEditorUtilities::CompileBlueprint(SaveGameBlueprint);
		USaveGame* SaveGame = SaveGameBlueprint ? NewObject<USaveGame>(GetTransientPackage(), SaveGameBlueprint->GeneratedClass) : nullptr;
		TestNotNull(TEXT("临时 SaveGame Blueprint 实例创建成功"), SaveGame);
		TestTrue(TEXT("临时 SaveGame 写入成功"), SaveGame && UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0));
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("slotName"), SlotName);
			Args->SetNumberField(TEXT("userIndex"), 0);
			Execute(TEXT("inspect_save_game"), Args);
		}
		UGameplayStatics::DeleteGameInSlot(SlotName, 0);

		TArray<UObject*> ObjectsToDelete;
		if (UObject* EnumObject = LoadObject<UObject>(nullptr, *EnumAssetPath))
		{
			ObjectsToDelete.Add(EnumObject);
		}
		if (SaveGameBlueprint)
			ObjectsToDelete.Add(SaveGameBlueprint);
		ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		GConfig->UnloadFile(TagsConfigPath);
		if (bTagsConfigExisted)
		{
			FFileHelper::SaveStringToFile(OriginalTagsConfig, *TagsConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		else
		{
			IFileManager::Get().Delete(*TagsConfigPath, false, true, true);
		}
		return true;
	}
}

#endif
