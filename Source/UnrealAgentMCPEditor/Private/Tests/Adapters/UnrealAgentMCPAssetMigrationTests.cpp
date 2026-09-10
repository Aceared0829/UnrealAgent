// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAssetMigrationTests.cpp
 * @brief 资产第一批原生迁移动作的真实生命周期黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"
#include "Application/Domains/Asset/UnrealAgentMCPAssetService.h"
#include "Application/Ports/UnrealAgentMCPAssetPort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Tests/Adapters/UnrealAgentMCPAssetTestTypes.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetActionContractTest, "WorldData.UnrealAgent.Asset.ActionContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetActionContractTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPAssetService::GetImplementedActions();
		TestEqual(TEXT("当前资产动作数量"), Actions.Num(), 102);
		TestTrue(TEXT("包含依赖查询"), Actions.Contains(TEXT("get_dependencies")));
		TestTrue(TEXT("包含批量删除"), Actions.Contains(TEXT("delete_batch")));
		TestTrue(TEXT("包含协作锁查询"), Actions.Contains(TEXT("list_locks")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetDefinitionsIntegrationTest, "WorldData.UnrealAgent.Asset.DefinitionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetDefinitionsIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = TEXT("/Game/UnrealAgentAutomation/Definitions_") + Suffix;
		const FString EnumPath = Root + TEXT("/E_Test.E_Test");
		const FString StructPath = Root + TEXT("/S_Test.S_Test");
		const FString MappingName = TEXT("MCP_Test_") + Suffix;

		TSharedRef<FUnrealAgentMCPUnrealAssetAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealAssetAdapter>();
		TSharedRef<IUnrealAgentMCPAssetPort> Port = Adapter;
		FUnrealAgentMCPAssetService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Args);
			TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1400)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForAsset = [](const FString& AssetPath)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("type"), TEXT("action"));
			Args->SetStringField(TEXT("name"), MappingName);
			Args->SetStringField(TEXT("key"), TEXT("F9"));
			Execute(TEXT("add_input_mapping"), Args);
			const FString List = Execute(TEXT("list_input_mappings"), MakeShared<FJsonObject>());
			TestTrue(TEXT("输入映射列表包含临时映射"), List.Contains(MappingName));
			Execute(TEXT("remove_input_mapping"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForAsset(EnumPath);
			Args->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueString>(TEXT("Idle")), MakeShared<FJsonValueString>(TEXT("Running")) });
			Execute(TEXT("create_user_defined_enum"), Args);
			const FString List = Execute(TEXT("list_enum_values"), ForAsset(EnumPath));
			TestTrue(TEXT("枚举包含 Running"), List.Contains(TEXT("Running")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(EnumPath);
			Args->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueString>(TEXT("Low")), MakeShared<FJsonValueString>(TEXT("High")) });
			Execute(TEXT("edit_user_defined_enum"), Args);
			const FString List = Execute(TEXT("list_enum_values"), ForAsset(EnumPath));
			TestTrue(TEXT("枚举编辑后包含 High"), List.Contains(TEXT("High")));
		}

		{
			TSharedRef<FJsonObject> Args = ForAsset(StructPath);
			TSharedRef<FJsonObject> Health = MakeShared<FJsonObject>();
			Health->SetStringField(TEXT("name"), TEXT("Health"));
			Health->SetStringField(TEXT("type"), TEXT("int"));
			TSharedRef<FJsonObject> Label = MakeShared<FJsonObject>();
			Label->SetStringField(TEXT("name"), TEXT("Label"));
			Label->SetStringField(TEXT("type"), TEXT("string"));
			Args->SetArrayField(TEXT("fields"), { MakeShared<FJsonValueObject>(Health), MakeShared<FJsonValueObject>(Label) });
			Execute(TEXT("create_user_defined_struct"), Args);
			const FString List = Execute(TEXT("list_struct_fields"), ForAsset(StructPath));
			TestTrue(TEXT("结构体包含 Health 与 Label"), List.Contains(TEXT("Health")) && List.Contains(TEXT("Label")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(StructPath);
			Args->SetStringField(TEXT("oldName"), TEXT("Health"));
			Args->SetStringField(TEXT("newName"), TEXT("CurrentHealth"));
			const FString Result = Execute(TEXT("rename_struct_field"), Args);
			TestTrue(TEXT("结构体字段已经重命名"), Result.Contains(TEXT("CurrentHealth")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(StructPath);
			TSharedRef<FJsonObject> Speed = MakeShared<FJsonObject>();
			Speed->SetStringField(TEXT("name"), TEXT("Speed"));
			Speed->SetStringField(TEXT("type"), TEXT("float"));
			Args->SetArrayField(TEXT("fields"), { MakeShared<FJsonValueObject>(Speed) });
			const FString Result = Execute(TEXT("edit_user_defined_struct"), Args);
			TestTrue(TEXT("结构体全量编辑后仅保留 Speed"), Result.Contains(TEXT("Speed")));
		}

		Execute(TEXT("delete"), ForAsset(StructPath));
		Execute(TEXT("delete"), ForAsset(EnumPath));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetMeshIntegrationTest, "WorldData.UnrealAgent.Asset.MeshIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetMeshIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = TEXT("/Game/UnrealAgentAutomation/Mesh_") + Suffix;
		const FString AssetName = TEXT("SM_Test_") + Suffix;
		const FString AssetPackage = PackageName + TEXT("/") + AssetName;
		const FString AssetPath = AssetPackage + TEXT(".") + AssetName;
		UPackage* Package = CreatePackage(*AssetPackage);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial."
				 "BasicShapeMaterial"));
		if (Material)
		{
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		}
		FAssetRegistryModule::AssetCreated(Mesh);
		Mesh->MarkPackageDirty();

		TSharedRef<FUnrealAgentMCPUnrealAssetAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealAssetAdapter>();
		TSharedRef<IUnrealAgentMCPAssetPort> Port = Adapter;
		FUnrealAgentMCPAssetService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Args);
			TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1400)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForMesh = [&AssetPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};

		Execute(TEXT("get_mesh_info"), ForMesh());
		Execute(TEXT("get_mesh_bounds"), ForMesh());
		Execute(TEXT("get_mesh_collision"), ForMesh());
		Execute(TEXT("read_import_sources"), ForMesh());
		{
			TSharedRef<FJsonObject> Args = ForMesh();
			Args->SetStringField(TEXT("socketName"), TEXT("Grip"));
			TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 10.0);
			Location->SetNumberField(TEXT("y"), 20.0);
			Location->SetNumberField(TEXT("z"), 30.0);
			Args->SetObjectField(TEXT("location"), Location);
			Execute(TEXT("add_socket"), Args);
		}
		{
			const FString Result = Execute(TEXT("list_sockets"), ForMesh());
			TestTrue(TEXT("Socket 列表包含 Grip"), Result.Contains(TEXT("Grip")));
		}
		{
			TSharedRef<FJsonObject> Args = ForMesh();
			Args->SetStringField(TEXT("socketName"), TEXT("Grip"));
			TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
			Rotation->SetNumberField(TEXT("pitch"), 1.0);
			Rotation->SetNumberField(TEXT("yaw"), 2.0);
			Rotation->SetNumberField(TEXT("roll"), 3.0);
			Args->SetObjectField(TEXT("rotation"), Rotation);
			Execute(TEXT("set_socket_transform"), Args);
			TestEqual(TEXT("Socket Yaw 已真实写入"), Mesh->FindSocket(TEXT("Grip"))->RelativeRotation.Yaw, 2.0);
		}
		if (Material)
		{
			TSharedRef<FJsonObject> Args = ForMesh();
			Args->SetNumberField(TEXT("materialIndex"), 0);
			Args->SetStringField(TEXT("materialPath"), Material->GetPathName());
			Execute(TEXT("set_mesh_material"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMesh();
			Args->SetBoolField(TEXT("enabled"), false);
			Execute(TEXT("set_mesh_nav"), Args);
			TestFalse(TEXT("StaticMesh 导航数据开关已关闭"), Mesh->bHasNavigationData);
		}
		{
			TSharedRef<FJsonObject> Args = ForMesh();
			Args->SetStringField(TEXT("socketName"), TEXT("Grip"));
			Execute(TEXT("remove_socket"), Args);
			TestNull(TEXT("Socket 已真实删除"), Mesh->FindSocket(TEXT("Grip")));
		}
		Execute(TEXT("delete"), ForMesh());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetTablesIntegrationTest, "WorldData.UnrealAgent.Asset.TablesAndTextureIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetTablesIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = TEXT("/Game/UnrealAgentAutomation/Tables_") + Suffix;
		const FString DataTablePath = Root + TEXT("/DT_Test.DT_Test");
		const FString CurveTablePath = Root + TEXT("/CT_Test.CT_Test");
		const FString StringTablePath = Root + TEXT("/ST_Test.ST_Test");
		const FString TexturePackage = Root + TEXT("/TX_Test");
		const FString TexturePath = TexturePackage + TEXT(".TX_Test");

		TSharedRef<FUnrealAgentMCPUnrealAssetAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealAssetAdapter>();
		TSharedRef<IUnrealAgentMCPAssetPort> Port = Adapter;
		FUnrealAgentMCPAssetService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Args);
			TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1400)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForAsset = [](const FString& AssetPath)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = ForAsset(DataTablePath);
			Args->SetStringField(TEXT("rowStruct"), FUnrealAgentMCPAssetTableRow::StaticStruct()->GetPathName());
			Execute(TEXT("create_datatable"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(DataTablePath);
			Args->SetStringField(TEXT("rowName"), TEXT("First"));
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("Count"), 7);
			Row->SetStringField(TEXT("Label"), TEXT("甲"));
			Args->SetObjectField(TEXT("row"), Row);
			Execute(TEXT("add_datatable_row"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(DataTablePath);
			Args->SetStringField(TEXT("rowName"), TEXT("First"));
			Args->SetStringField(TEXT("columnName"), TEXT("Count"));
			Args->SetField(TEXT("value"), MakeShared<FJsonValueNumber>(12));
			Execute(TEXT("set_datatable_cell"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(DataTablePath);
			Args->SetStringField(TEXT("rowName"), TEXT("First"));
			Args->SetStringField(TEXT("newRowName"), TEXT("Renamed"));
			Execute(TEXT("rename_datatable_row"), Args);
			const FString Result = Execute(TEXT("get_datatable_row"),
				[&]()
				{
					TSharedRef<FJsonObject> Query = ForAsset(DataTablePath);
					Query->SetStringField(TEXT("rowName"), TEXT("Renamed"));
					return Query;
				}());
			TestTrue(TEXT("DataTable 单元格值已经更新"), Result.Contains(TEXT("\"Count\":12")));
		}
		Execute(TEXT("read_datatable"), ForAsset(DataTablePath));

		Execute(TEXT("create_curvetable"), ForAsset(CurveTablePath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(CurveTablePath);
			Args->SetStringField(TEXT("rowName"), TEXT("Speed"));
			TSharedRef<FJsonObject> KeyA = MakeShared<FJsonObject>();
			KeyA->SetNumberField(TEXT("time"), 0.0);
			KeyA->SetNumberField(TEXT("value"), 10.0);
			TSharedRef<FJsonObject> KeyB = MakeShared<FJsonObject>();
			KeyB->SetNumberField(TEXT("time"), 1.0);
			KeyB->SetNumberField(TEXT("value"), 20.0);
			Args->SetArrayField(TEXT("keys"), { MakeShared<FJsonValueObject>(KeyA), MakeShared<FJsonValueObject>(KeyB) });
			Execute(TEXT("add_curvetable_row"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(CurveTablePath);
			Args->SetStringField(TEXT("rowName"), TEXT("Speed"));
			Args->SetNumberField(TEXT("time"), 2.0);
			Args->SetNumberField(TEXT("value"), 30.0);
			const FString Result = Execute(TEXT("add_curvetable_key"), Args);
			TestTrue(TEXT("CurveTable 包含三枚关键帧"), Result.Contains(TEXT("\"keyCount\":3")));
		}
		Execute(TEXT("read_curvetable"), ForAsset(CurveTablePath));

		Execute(TEXT("create_stringtable"), ForAsset(StringTablePath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(StringTablePath);
			Args->SetStringField(TEXT("key"), TEXT("Greeting"));
			Args->SetStringField(TEXT("value"), TEXT("你好"));
			Execute(TEXT("set_stringtable_entry"), Args);
			const FString Result = Execute(TEXT("get_stringtable_entry"), Args);
			TestTrue(TEXT("StringTable 返回中文值"), Result.Contains(TEXT("你好")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(StringTablePath);
			TSharedRef<FJsonObject> Entries = MakeShared<FJsonObject>();
			Entries->SetStringField(TEXT("Second"), TEXT("第二项"));
			Entries->SetStringField(TEXT("Third"), TEXT("第三项"));
			Args->SetObjectField(TEXT("entries"), Entries);
			const FString Result = Execute(TEXT("import_stringtable"), Args);
			TestTrue(TEXT("StringTable 已导入三项"), Result.Contains(TEXT("\"count\":3")));
		}

		UPackage* TextureOuter = CreatePackage(*TexturePackage);
		UTexture2D* Texture = NewObject<UTexture2D>(TextureOuter, TEXT("TX_Test"), RF_Public | RF_Standalone | RF_Transactional);
		Texture->SRGB = true;
		FAssetRegistryModule::AssetCreated(Texture);
		Texture->MarkPackageDirty();
		Execute(TEXT("get_texture_info"), ForAsset(TexturePath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(TexturePath);
			TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetBoolField(TEXT("SRGB"), false);
			Args->SetObjectField(TEXT("settings"), Settings);
			Execute(TEXT("set_texture_settings"), Args);
			TestFalse(TEXT("纹理 SRGB 已真实关闭"), Texture->SRGB);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), Root);
			const FString Result = Execute(TEXT("list_textures"), Args);
			TestTrue(TEXT("纹理列表包含测试纹理"), Result.Contains(TEXT("TX_Test")));
		}

		for (const FString& Path : { TexturePath, StringTablePath, CurveTablePath, DataTablePath })
		{
			Execute(TEXT("delete"), ForAsset(Path));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetAdvancedIntegrationTest, "WorldData.UnrealAgent.Asset.AdvancedIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetAdvancedIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = TEXT("/Game/UnrealAgentAutomation/Advanced_") + Suffix;
		const FString LeftName = TEXT("DA_Left_") + Suffix;
		const FString RightName = TEXT("DA_Right_") + Suffix;
		const FString LeftPackage = Root + TEXT("/") + LeftName;
		const FString RightPackage = Root + TEXT("/") + RightName;
		const FString LeftPath = LeftPackage + TEXT(".") + LeftName;
		const FString RightPath = RightPackage + TEXT(".") + RightName;
		const FString TextureAName = TEXT("TX_A_") + Suffix;
		const FString TextureBName = TEXT("TX_B_") + Suffix;
		const FString TextureAPackage = Root + TEXT("/") + TextureAName;
		const FString TextureBPackage = Root + TEXT("/") + TextureBName;
		const FString TextureAPath = TextureAPackage + TEXT(".") + TextureAName;
		const FString TextureBPath = TextureBPackage + TEXT(".") + TextureBName;
		const FString SkeletalName = TEXT("SK_Empty_") + Suffix;
		const FString SkeletalPackage = Root + TEXT("/") + SkeletalName;
		const FString SkeletalPath = SkeletalPackage + TEXT(".") + SkeletalName;
		const FString PipelineName = TEXT("IP_Test_") + Suffix;
		const FString PipelinePath = Root + TEXT("/") + PipelineName + TEXT(".") + PipelineName;
		const FString PivotName = TEXT("SM_Pivot_") + Suffix;
		const FString PivotPath = Root + TEXT("/") + PivotName + TEXT(".") + PivotName;

		auto CreateTestData = [](const FString& PackageName, const FString& AssetName, const FString& Label)
		{
			UPackage* Package = CreatePackage(*PackageName);
			UUnrealAgentMCPAssetTestData* Asset = NewObject<UUnrealAgentMCPAssetTestData>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			Asset->Label = Label;
			FAssetRegistryModule::AssetCreated(Asset);
			Asset->MarkPackageDirty();
			return Asset;
		};
		auto CreateTexture = [](const FString& PackageName, const FString& AssetName, const uint8 Seed)
		{
			UPackage* Package = CreatePackage(*PackageName);
			UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			uint8 Pixels[16] = { Seed, 0, 0, 255, 0, Seed, 0, 255, 0, 0, Seed, 255, Seed, Seed, Seed, 255 };
			Texture->Source.Init(2, 2, 1, 1, TSF_BGRA8, Pixels);
			Texture->SRGB = true;
			Texture->PostEditChange();
			FAssetRegistryModule::AssetCreated(Texture);
			Texture->MarkPackageDirty();
			return Texture;
		};

		CreateTestData(LeftPackage, LeftName, TEXT("左侧"));
		CreateTestData(RightPackage, RightName, TEXT("右侧"));
		CreateTexture(TextureAPackage, TextureAName, 64);
		CreateTexture(TextureBPackage, TextureBName, 192);
		{
			UPackage* Package = CreatePackage(*SkeletalPackage);
			USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *SkeletalName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Mesh);
			Mesh->MarkPackageDirty();
		}

		TSharedRef<FUnrealAgentMCPUnrealAssetAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealAssetAdapter>();
		TSharedRef<IUnrealAgentMCPAssetPort> Port = Adapter;
		FUnrealAgentMCPAssetService Service(Port);
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
		auto ForAsset = [](const FString& AssetPath)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};

		Execute(TEXT("reindex_fts"), MakeShared<FJsonObject>());
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), Suffix);
			const FString Result = Execute(TEXT("search_fts"), Args);
			TestTrue(TEXT("全文索引能找到本轮测试资产"), Result.Contains(LeftName) && Result.Contains(TextureAName));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(LeftPath);
			Args->SetStringField(TEXT("otherAssetPath"), RightPath);
			const FString Result = Execute(TEXT("diff"), Args);
			TestTrue(TEXT("不同资产能生成结构化差异"), Result.Contains(TEXT("\"identical\":false")) && Result.Contains(TEXT("\"sameClass\":true")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(TextureAPath);
			Args->SetStringField(TEXT("otherAssetPath"), TextureBPath);
			const FString Result = Execute(TEXT("compare_textures"), Args);
			TestTrue(TEXT("不同源数据的纹理不会被判为相同"), Result.Contains(TEXT("\"identical\":false")));
		}
		Execute(TEXT("read_cloth_data"), ForAsset(SkeletalPath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(SkeletalPath);
			Args->SetNumberField(TEXT("clothIndex"), 0);
			Args->SetStringField(TEXT("propertyName"), TEXT("MassValue"));
			Args->SetNumberField(TEXT("value"), 1.0);
			const FString Result = Execute(TEXT("set_cloth_config"), Args, false);
			TestTrue(TEXT("无布料配置时返回结构化错误"), Result.Contains(TEXT("\"success\":false")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(PipelinePath);
			const FString Result = Execute(TEXT("create_interchange_pipeline"), Args);
			TestTrue(TEXT("Interchange Pipeline 已由本插件创建"), Result.Contains(PipelineName));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
			Args->SetStringField(TEXT("destinationPath"), PivotPath);
			Execute(TEXT("duplicate"), Args);
			Execute(TEXT("recenter_pivot"), ForAsset(PivotPath));
		}

		const FString ExportPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/UnrealAgentMCP"), TextureAName + TEXT(".png")));
		{
			TSharedRef<FJsonObject> Args = ForAsset(TextureAPath);
			Args->SetStringField(TEXT("outputPath"), ExportPath);
			Execute(TEXT("export_texture"), Args);
			TestTrue(TEXT("纹理导出文件已经生成"), IFileManager::Get().FileExists(*ExportPath));
		}
		const FString ImportedPath = Root + TEXT("/Imported/") + TextureAName + TEXT(".") + TextureAName;
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("sourcePath"), ExportPath);
			Args->SetStringField(TEXT("destinationPath"), Root + TEXT("/Imported"));
			Execute(TEXT("import_texture"), Args);
			Execute(TEXT("reimport"), ForAsset(ImportedPath));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetArrayField(TEXT("files"), { MakeShared<FJsonValueString>(ExportPath) });
			Args->SetStringField(TEXT("destinationPath"), Root + TEXT("/Imported"));
			Args->SetBoolField(TEXT("replaceExisting"), true);
			Execute(TEXT("import_texture_batch"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), Root);
			Execute(TEXT("delete_folder"), Args);
		}
		IFileManager::Get().Delete(*ExportPath, false, true);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetLifecycleIntegrationTest, "WorldData.UnrealAgent.Asset.LifecycleIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetLifecycleIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = TEXT("/Game/UnrealAgentAutomation/Asset_") + Suffix;
		const FString SourceName = TEXT("DA_Source_") + Suffix;
		const FString CopyName = TEXT("DA_Copy_") + Suffix;
		const FString RenamedName = TEXT("DA_Renamed_") + Suffix;
		const FString SourcePackage = Root + TEXT("/") + SourceName;
		const FString CopyPackage = Root + TEXT("/") + CopyName;
		const FString RenamedPackage = Root + TEXT("/") + RenamedName;
		const FString SourcePath = SourcePackage + TEXT(".") + SourceName;
		const FString CopyPath = CopyPackage + TEXT(".") + CopyName;
		const FString RenamedPath = RenamedPackage + TEXT(".") + RenamedName;

		UPackage* Package = CreatePackage(*SourcePackage);
		UUnrealAgentMCPAssetTestData* Source = NewObject<UUnrealAgentMCPAssetTestData>(Package, *SourceName, RF_Public | RF_Standalone | RF_Transactional);
		Source->Label = TEXT("初始值");
		FAssetRegistryModule::AssetCreated(Source);
		Source->MarkPackageDirty();

		TSharedRef<FUnrealAgentMCPUnrealAssetAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealAssetAdapter>();
		TSharedRef<IUnrealAgentMCPAssetPort> Port = Adapter;
		FUnrealAgentMCPAssetService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Args);
			if (bExpectSuccess)
			{
				TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1200)), Result.Contains(TEXT("\"success\":true")));
			}
			return Result;
		};
		auto ForAsset = [](const FString& AssetPath)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};

		Execute(TEXT("health_check"), MakeShared<FJsonObject>());
		{
			const FString Result = Execute(TEXT("list_properties"), ForAsset(SourcePath));
			TestTrue(TEXT("属性列表包含测试布尔属性"), Result.Contains(TEXT("bEnabled")));
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(SourcePath);
			Args->SetStringField(TEXT("propertyName"), TEXT("bEnabled"));
			Args->SetField(TEXT("value"), MakeShared<FJsonValueBoolean>(true));
			Execute(TEXT("set_property"), Args);
			TestTrue(TEXT("属性已真实写入 UObject"), Source->bEnabled);
		}
		Execute(TEXT("save"), ForAsset(SourcePath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(SourcePath);
			Args->SetStringField(TEXT("destinationPath"), CopyPath);
			Execute(TEXT("duplicate"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset(CopyPath);
			Args->SetStringField(TEXT("destinationPath"), RenamedPath);
			Execute(TEXT("rename"), Args);
		}
		Execute(TEXT("get_dependencies"), ForAsset(SourcePath));
		Execute(TEXT("get_referencers"), ForAsset(SourcePath));
		{
			TSharedRef<FJsonObject> Args = ForAsset(SourcePath);
			Args->SetStringField(TEXT("owner"), TEXT("Automation"));
			Execute(TEXT("lock"), Args);
			const FString Locks = Execute(TEXT("list_locks"), MakeShared<FJsonObject>());
			TestTrue(TEXT("锁列表包含测试资产"), Locks.Contains(SourcePath));
			Execute(TEXT("unlock"), Args);
		}

		Execute(TEXT("delete"), ForAsset(RenamedPath));
		Execute(TEXT("delete"), ForAsset(SourcePath));

		UEditorAssetSubsystem* Subsystem = Adapter->GetAssetSubsystem();
		TestNotNull(TEXT("资产子系统可用"), Subsystem);
		if (Subsystem)
		{
			TestFalse(TEXT("源资产已经删除"), Subsystem->DoesAssetExist(SourcePath));
			TestFalse(TEXT("重命名后的副本已经删除"), Subsystem->DoesAssetExist(RenamedPath));
		}
		return true;
	}
}

#endif
