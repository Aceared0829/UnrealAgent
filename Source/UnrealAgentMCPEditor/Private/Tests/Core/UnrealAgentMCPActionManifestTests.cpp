// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPActionManifestTests.cpp
 * @brief 全量 action 的成功契约与默认拒绝失败矩阵。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Application/Actions/UnrealAgentMCPActionManifest.h"
#include "Core/Schema/UnrealAgentMCPJsonSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		TSharedPtr<ActionContracts::FManifest> LoadActionManifest(FAutomationTestBase& Test)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent"));
			if (!Test.TestTrue(TEXT("Unreal Agent 插件可定位"), Plugin.IsValid()))
			{
				return nullptr;
			}
			FString Json;
			const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("ActionContracts.json"));
			if (!Test.TestTrue(TEXT("Action Manifest 可读取"), FFileHelper::LoadFileToString(Json, *Path)))
			{
				return nullptr;
			}
			TArray<FString> Errors;
			const TSharedPtr<ActionContracts::FManifest> Manifest = ActionContracts::FManifest::Parse(Json, Errors);
			for (const FString& Error : Errors)
			{
				Test.AddError(Error);
			}
			Test.TestTrue(TEXT("Action Manifest 可解析"), Manifest.IsValid());
			return Manifest;
		}

		TSharedPtr<FJsonValue> MakeSampleValue(const TSharedPtr<FJsonObject>& Schema)
		{
			if (!Schema.IsValid())
			{
				return MakeShared<FJsonValueNull>();
			}
			const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
			if (Schema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues != nullptr && !EnumValues->IsEmpty())
			{
				return (*EnumValues)[0];
			}
			const TArray<TSharedPtr<FJsonValue>>* AnyOf = nullptr;
			const bool bHasAnyOf = Schema->TryGetArrayField(TEXT("anyOf"), AnyOf) && AnyOf != nullptr && !AnyOf->IsEmpty();
			FString Type;
			Schema->TryGetStringField(TEXT("type"), Type);
			if (Type.IsEmpty() && bHasAnyOf)
			{
				return MakeSampleValue((*AnyOf)[0]->AsObject());
			}
			if (Type == TEXT("string"))
				return MakeShared<FJsonValueString>(TEXT("sample"));
			if (Type == TEXT("number") || Type == TEXT("integer"))
			{
				double Minimum = 0.0;
				Schema->TryGetNumberField(TEXT("minimum"), Minimum);
				return MakeShared<FJsonValueNumber>(Minimum);
			}
			if (Type == TEXT("boolean"))
				return MakeShared<FJsonValueBoolean>(false);
			if (Type == TEXT("array"))
			{
				double MinItemsNumber = 0.0;
				Schema->TryGetNumberField(TEXT("minItems"), MinItemsNumber);
				const int32 MinItems = FMath::Clamp(FMath::CeilToInt(MinItemsNumber), 0, 10000);
				const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
				TArray<TSharedPtr<FJsonValue>> Items;
				if (MinItems > 0 && Schema->TryGetObjectField(TEXT("items"), ItemSchema) && ItemSchema != nullptr && ItemSchema->IsValid())
				{
					for (int32 Index = 0; Index < MinItems; ++Index)
					{
						Items.Add(MakeSampleValue(*ItemSchema));
					}
				}
				return MakeShared<FJsonValueArray>(MoveTemp(Items));
			}
			if (Type == TEXT("object"))
			{
				TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
				const TSharedPtr<FJsonObject>* Properties = nullptr;
				Schema->TryGetObjectField(TEXT("properties"), Properties);
				auto AddRequiredFields = [&Value, Properties](const TSharedPtr<FJsonObject>& RequirementSchema)
				{
					if (!RequirementSchema.IsValid())
					{
						return;
					}
					const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
					if (!RequirementSchema->TryGetArrayField(TEXT("required"), Required) || Required == nullptr)
					{
						return;
					}
					const TSharedPtr<FJsonObject>* LocalProperties = nullptr;
					RequirementSchema->TryGetObjectField(TEXT("properties"), LocalProperties);
					const TSharedPtr<FJsonObject>* AvailableProperties = LocalProperties != nullptr && LocalProperties->IsValid() ? LocalProperties : Properties;
					if (AvailableProperties == nullptr || !AvailableProperties->IsValid())
					{
						return;
					}
					for (const TSharedPtr<FJsonValue>& RequiredValue : *Required)
					{
						FString Name;
						if (!RequiredValue.IsValid() || !RequiredValue->TryGetString(Name))
						{
							continue;
						}
						if (Value->HasField(Name))
						{
							continue;
						}
						const TSharedPtr<FJsonValue> Property = (*AvailableProperties)->TryGetField(Name);
						if (Property.IsValid() && Property->Type == EJson::Object)
						{
							Value->SetField(Name, MakeSampleValue(Property->AsObject()));
						}
					}
				};
				AddRequiredFields(Schema);
				if (bHasAnyOf && (*AnyOf)[0].IsValid())
				{
					AddRequiredFields((*AnyOf)[0]->AsObject());
				}
				return MakeShared<FJsonValueObject>(Value);
			}
			return MakeShared<FJsonValueNull>();
		}

		TSharedPtr<FJsonObject> MakeSampleObject(const TSharedPtr<FJsonObject>& Schema)
		{
			const TSharedPtr<FJsonValue> Value = MakeSampleValue(Schema);
			return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPActionManifestContractMatrixTest, "WorldData.UnrealAgent.Core.ActionManifest.ContractMatrix",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPActionManifestContractMatrixTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<ActionContracts::FManifest> Manifest = LoadActionManifest(*this);
		if (!Manifest.IsValid())
		{
			return false;
		}
		TestTrue(TEXT("Manifest 至少包含一个领域"), Manifest->GetDomainCount() > 0);
		TestTrue(TEXT("Manifest 至少包含一个 action"), Manifest->GetActionCount() > 0);
		TestTrue(TEXT("Manifest Hash 是完整 SHA-256"), Manifest->GetContractHash().Len() == 71 && Manifest->GetContractHash().StartsWith(TEXT("sha256:")));

		int32 ValidatedActions = 0;
		TSet<FString> ToolIds;
		for (const TPair<FString, ActionContracts::FDomainContract>& DomainPair : Manifest->GetDomains())
		{
			const ActionContracts::FDomainContract& Domain = DomainPair.Value;
			for (const TPair<FString, ActionContracts::FActionContract>& ActionPair : Domain.Actions)
			{
				const ActionContracts::FActionContract& Action = ActionPair.Value;
				const TSharedPtr<FJsonObject> Sample = MakeSampleObject(Action.InputSchema);
				const JsonSchema::FValidationResult ActionValidation = JsonSchema::ValidateObject(Action.InputSchema.ToSharedRef(), Sample);
				if (!ActionValidation.IsValid())
				{
					AddError(FString::Printf(TEXT("Action 成功契约不可满足：%s。"), *Action.ToolId));
					continue;
				}
				const JsonSchema::FValidationResult DomainValidation = JsonSchema::ValidateObject(Domain.InputSchema.ToSharedRef(), Sample);
				if (!DomainValidation.IsValid())
				{
					AddError(FString::Printf(TEXT("分类 Schema 未包含 action：%s。"), *Action.ToolId));
					continue;
				}
				if (ToolIds.Contains(Action.ToolId))
				{
					AddError(TEXT("ToolId 重复：") + Action.ToolId);
					continue;
				}
				ToolIds.Add(Action.ToolId);
				++ValidatedActions;
			}
		}
		TestEqual(TEXT("全部成功契约均可满足"), ValidatedActions, Manifest->GetActionCount());
		TestEqual(TEXT("全部 ToolId 均唯一"), ToolIds.Num(), Manifest->GetActionCount());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPWhiteboxActionSchemaTest, "WorldData.UnrealAgent.Core.ActionManifest.WhiteboxActionSchema",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPWhiteboxActionSchemaTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<ActionContracts::FManifest> Manifest = LoadActionManifest(*this);
		if (!Manifest.IsValid())
		{
			return false;
		}

		for (const FString& ActionName : { TEXT("build_city_wall"), TEXT("clear_folder"), TEXT("align_to_landscape") })
		{
			const ActionContracts::FActionContract* Action = Manifest->FindAction(TEXT("whitebox"), ActionName);
			if (!TestNotNull(*FString::Printf(TEXT("Whitebox action exists: %s"), *ActionName), Action))
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s uses the task executor"), *ActionName), Action->ExecutionMode, EMcpToolExecutionMode::Task);
			TestTrue(*FString::Printf(TEXT("%s is cancelable"), *ActionName), Action->bCancelable);
		}

		const ActionContracts::FActionContract* Build = Manifest->FindAction(TEXT("whitebox"), TEXT("build_city_wall"));
		if (TestNotNull(TEXT("build_city_wall schema exists"), Build))
		{
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("action"), TEXT("build_city_wall"));
			Request->SetNumberField(TEXT("halfExtentCm"), 18000.0);
			Request->SetNumberField(TEXT("baseThicknessM"), 24.0);
			Request->SetBoolField(TEXT("flattenFootprint"), true);
			Request->SetStringField(TEXT("landscapeGuid"), FGuid::NewGuid().ToString());
			TestTrue(TEXT("build_city_wall accepts its typed massing spec"), JsonSchema::ValidateObject(Build->InputSchema.ToSharedRef(), Request).IsValid());
			Request->SetStringField(TEXT("arbitraryPython"), TEXT("print('no')"));
			TestFalse(TEXT("build_city_wall rejects untyped escape-hatch parameters"), JsonSchema::ValidateObject(Build->InputSchema.ToSharedRef(), Request).IsValid());
			Request->RemoveField(TEXT("arbitraryPython"));
			Request->SetNumberField(TEXT("merlonSpacingM"), 0.01);
			TestFalse(TEXT("build_city_wall rejects pathological instance spacing"), JsonSchema::ValidateObject(Build->InputSchema.ToSharedRef(), Request).IsValid());
		}

		const ActionContracts::FActionContract* Clear = Manifest->FindAction(TEXT("whitebox"), TEXT("clear_folder"));
		if (TestNotNull(TEXT("clear_folder schema exists"), Clear))
		{
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("action"), TEXT("clear_folder"));
			Request->SetStringField(TEXT("folder"), TEXT("CityWall_Whitebox"));
			Request->SetStringField(TEXT("confirmation"), TEXT("clear_whitebox_folder"));
			TestTrue(TEXT("clear_folder accepts the fixed confirmation enum"), JsonSchema::ValidateObject(Clear->InputSchema.ToSharedRef(), Request).IsValid());
			Request->SetStringField(TEXT("confirmation"), TEXT("yes"));
			TestFalse(TEXT("clear_folder rejects free-form confirmation text"), JsonSchema::ValidateObject(Clear->InputSchema.ToSharedRef(), Request).IsValid());
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelDeletionActionSchemaTest, "WorldData.UnrealAgent.Level.DeletionActionSchema",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelDeletionActionSchemaTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<ActionContracts::FManifest> Manifest = LoadActionManifest(*this);
		if (!Manifest.IsValid())
		{
			return false;
		}

		const ActionContracts::FActionContract* DeleteActors = Manifest->FindAction(TEXT("level"), TEXT("delete_actors"));
		const ActionContracts::FActionContract* DeleteByFolder = Manifest->FindAction(TEXT("level"), TEXT("delete_by_folder"));
		if (!TestNotNull(TEXT("Level delete_actors action 存在"), DeleteActors) || !TestNotNull(TEXT("Level delete_by_folder action 存在"), DeleteByFolder))
		{
			return false;
		}

		TestEqual(TEXT("delete_actors 使用可恢复任务"), DeleteActors->ExecutionMode, EMcpToolExecutionMode::Task);
		TestTrue(TEXT("delete_actors 可取消"), DeleteActors->bCancelable);
		TestEqual(TEXT("delete_by_folder 使用可恢复任务"), DeleteByFolder->ExecutionMode, EMcpToolExecutionMode::Task);
		TestTrue(TEXT("delete_by_folder 可取消"), DeleteByFolder->bCancelable);
		TSharedRef<FJsonObject> DeleteActorsRequest = MakeShared<FJsonObject>();
		DeleteActorsRequest->SetStringField(TEXT("action"), TEXT("delete_actors"));
		TestFalse(TEXT("delete_actors 契约拒绝无选择器请求"), JsonSchema::ValidateObject(DeleteActors->InputSchema.ToSharedRef(), DeleteActorsRequest).IsValid());
		DeleteActorsRequest->SetArrayField(TEXT("actorLabels"), { MakeShared<FJsonValueString>(TEXT("Wall_001")) });
		TestTrue(TEXT("delete_actors 契约接受精确 Actor 标签"), JsonSchema::ValidateObject(DeleteActors->InputSchema.ToSharedRef(), DeleteActorsRequest).IsValid());

		TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("action"), TEXT("delete_by_folder"));
		Request->SetStringField(TEXT("folderPath"), TEXT("CityWall_Whitebox"));
		Request->SetStringField(TEXT("confirmation"), TEXT("delete_folder_contents"));
		TestTrue(TEXT("delete_by_folder 接受固定确认枚举"), JsonSchema::ValidateObject(DeleteByFolder->InputSchema.ToSharedRef(), Request).IsValid());
		Request->SetStringField(TEXT("confirmation"), TEXT("yes"));
		TestFalse(TEXT("delete_by_folder 拒绝自由文本确认串"), JsonSchema::ValidateObject(DeleteByFolder->InputSchema.ToSharedRef(), Request).IsValid());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPActionManifestFailureMatrixTest, "WorldData.UnrealAgent.Core.ActionManifest.FailureMatrix",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPActionManifestFailureMatrixTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<ActionContracts::FManifest> Manifest = LoadActionManifest(*this);
		if (!Manifest.IsValid())
		{
			return false;
		}
		int32 RejectedAdditionalFields = 0;
		int32 RejectedUnknownActions = 0;
		for (const TPair<FString, ActionContracts::FDomainContract>& DomainPair : Manifest->GetDomains())
		{
			const ActionContracts::FDomainContract& Domain = DomainPair.Value;
			TSharedRef<FJsonObject> UnknownAction = MakeShared<FJsonObject>();
			UnknownAction->SetStringField(TEXT("action"), TEXT("__unknown_action__"));
			if (!JsonSchema::ValidateObject(Domain.InputSchema.ToSharedRef(), UnknownAction).IsValid())
			{
				++RejectedUnknownActions;
			}
			for (const TPair<FString, ActionContracts::FActionContract>& ActionPair : Domain.Actions)
			{
				const ActionContracts::FActionContract& Action = ActionPair.Value;
				const TSharedPtr<FJsonObject> Sample = MakeSampleObject(Action.InputSchema);
				Sample->SetStringField(TEXT("__unexpected_parameter__"), TEXT("rejected"));
				const bool bRejected = !JsonSchema::ValidateObject(Action.InputSchema.ToSharedRef(), Sample).IsValid();
				if (bRejected)
				{
					++RejectedAdditionalFields;
				}
				else
				{
					AddError(FString::Printf(TEXT("Action accepts an unexpected parameter: %s.%s"), *Domain.Name, *Action.Name));
				}
			}
		}
		TestEqual(TEXT("全部领域均拒绝未知 action"), RejectedUnknownActions, Manifest->GetDomainCount());
		TestEqual(TEXT("全部 action 均拒绝未知参数"), RejectedAdditionalFields, Manifest->GetActionCount());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLandscapeActionSchemaParityTest, "WorldData.UnrealAgent.Landscape.ActionSchemaParity",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLandscapeActionSchemaParityTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<ActionContracts::FManifest> Manifest = LoadActionManifest(*this);
		if (!Manifest.IsValid())
		{
			return false;
		}

		const TArray<FString> TargetedActions = { TEXT("get_info"), TEXT("list_layers"), TEXT("sample"), TEXT("sample_batch"), TEXT("sample_grid"), TEXT("sample_polyline"),
			TEXT("sculpt"), TEXT("sculpt_batch"), TEXT("set_height_rect"), TEXT("import_heightmap"), TEXT("reset_heights"), TEXT("paint_layer"), TEXT("list_splines"),
			TEXT("get_component"), TEXT("set_material"), TEXT("add_layer_info") };
		for (const FString& ActionName : TargetedActions)
		{
			const ActionContracts::FActionContract* Action = Manifest->FindAction(TEXT("landscape"), ActionName);
			if (!TestNotNull(*FString::Printf(TEXT("Landscape action 存在：%s"), *ActionName), Action))
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			const bool bHasProperties =
				Action->InputSchema.IsValid() && Action->InputSchema->TryGetObjectField(TEXT("properties"), Properties) && Properties != nullptr && Properties->IsValid();
			TestTrue(*FString::Printf(TEXT("%s 具有参数定义"), *ActionName), bHasProperties);
			if (bHasProperties)
			{
				TestTrue(*FString::Printf(TEXT("%s 接受 actorLabel"), *ActionName), (*Properties)->HasField(TEXT("actorLabel")));
				TestTrue(*FString::Printf(TEXT("%s 接受 name"), *ActionName), (*Properties)->HasField(TEXT("name")));
				TestTrue(*FString::Printf(TEXT("%s 接受 landscapeGuid"), *ActionName), (*Properties)->HasField(TEXT("landscapeGuid")));
			}
		}

		const ActionContracts::FActionContract* Create = Manifest->FindAction(TEXT("landscape"), TEXT("create"));
		if (!TestNotNull(TEXT("Landscape create action 存在"), Create))
		{
			return false;
		}
		TSharedRef<FJsonObject> NumericScale = MakeShared<FJsonObject>();
		NumericScale->SetStringField(TEXT("action"), TEXT("create"));
		NumericScale->SetNumberField(TEXT("scale"), 100.0);
		TestTrue(TEXT("create 接受数值 scale"), JsonSchema::ValidateObject(Create->InputSchema.ToSharedRef(), NumericScale).IsValid());

		TSharedRef<FJsonObject> VectorScale = MakeShared<FJsonObject>();
		VectorScale->SetNumberField(TEXT("x"), 100.0);
		VectorScale->SetNumberField(TEXT("y"), 100.0);
		VectorScale->SetNumberField(TEXT("z"), 100.0);
		TSharedRef<FJsonObject> ObjectScale = MakeShared<FJsonObject>();
		ObjectScale->SetStringField(TEXT("action"), TEXT("create"));
		ObjectScale->SetObjectField(TEXT("scale"), VectorScale);
		ObjectScale->SetStringField(TEXT("label"), TEXT("MCP_RecreatedLandscape"));
		ObjectScale->SetBoolField(TEXT("replaceExisting"), true);
		TestTrue(TEXT("create 接受向量 scale 和显式替换语义"), JsonSchema::ValidateObject(Create->InputSchema.ToSharedRef(), ObjectScale).IsValid());

		const ActionContracts::FActionContract* Sculpt = Manifest->FindAction(TEXT("landscape"), TEXT("sculpt"));
		if (TestNotNull(TEXT("Landscape sculpt action 存在"), Sculpt))
		{
			TSharedRef<FJsonObject> Center = MakeShared<FJsonObject>();
			Center->SetNumberField(TEXT("x"), 0.0);
			Center->SetNumberField(TEXT("y"), 0.0);
			TSharedRef<FJsonObject> SetHeight = MakeShared<FJsonObject>();
			SetHeight->SetStringField(TEXT("action"), TEXT("sculpt"));
			SetHeight->SetObjectField(TEXT("center"), Center);
			SetHeight->SetNumberField(TEXT("radius"), 500.0);
			SetHeight->SetStringField(TEXT("mode"), TEXT("set"));
			SetHeight->SetNumberField(TEXT("targetWorldZ"), 1000.0);
			TestTrue(TEXT("sculpt 接受绝对世界高度"), JsonSchema::ValidateObject(Sculpt->InputSchema.ToSharedRef(), SetHeight).IsValid());
		}

		const ActionContracts::FActionContract* SculptBatch = Manifest->FindAction(TEXT("landscape"), TEXT("sculpt_batch"));
		if (TestNotNull(TEXT("Landscape sculpt_batch action 存在"), SculptBatch))
		{
			TSharedRef<FJsonObject> Center = MakeShared<FJsonObject>();
			Center->SetNumberField(TEXT("x"), 0.0);
			Center->SetNumberField(TEXT("y"), 0.0);
			TSharedRef<FJsonObject> Stroke = MakeShared<FJsonObject>();
			Stroke->SetObjectField(TEXT("center"), Center);
			Stroke->SetNumberField(TEXT("radius"), 500.0);
			Stroke->SetStringField(TEXT("mode"), TEXT("set"));
			Stroke->SetNumberField(TEXT("targetWorldZ"), 1000.0);
			TArray<TSharedPtr<FJsonValue>> Strokes;
			Strokes.Add(MakeShared<FJsonValueObject>(Stroke));
			TSharedRef<FJsonObject> Batch = MakeShared<FJsonObject>();
			Batch->SetStringField(TEXT("action"), TEXT("sculpt_batch"));
			Batch->SetArrayField(TEXT("strokes"), Strokes);
			TestTrue(TEXT("sculpt_batch 接受绝对高度笔刷数组"), JsonSchema::ValidateObject(SculptBatch->InputSchema.ToSharedRef(), Batch).IsValid());
		}

		const ActionContracts::FActionContract* SetHeightRect = Manifest->FindAction(TEXT("landscape"), TEXT("set_height_rect"));
		if (TestNotNull(TEXT("Landscape set_height_rect action 存在"), SetHeightRect))
		{
			TSharedRef<FJsonObject> Min = MakeShared<FJsonObject>();
			Min->SetNumberField(TEXT("x"), 0);
			Min->SetNumberField(TEXT("y"), 0);
			TSharedRef<FJsonObject> Max = MakeShared<FJsonObject>();
			Max->SetNumberField(TEXT("x"), 1);
			Max->SetNumberField(TEXT("y"), 1);
			TArray<TSharedPtr<FJsonValue>> Heights;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Heights.Add(MakeShared<FJsonValueNumber>(32768 + Index));
			}
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("action"), TEXT("set_height_rect"));
			Request->SetObjectField(TEXT("min"), Min);
			Request->SetObjectField(TEXT("max"), Max);
			Request->SetArrayField(TEXT("rawHeights"), Heights);
			TestTrue(TEXT("set_height_rect 接受行优先 raw16 高度块"), JsonSchema::ValidateObject(SetHeightRect->InputSchema.ToSharedRef(), Request).IsValid());
		}

		const ActionContracts::FActionContract* ImportHeightmap = Manifest->FindAction(TEXT("landscape"), TEXT("import_heightmap"));
		if (TestNotNull(TEXT("Landscape import_heightmap action 存在"), ImportHeightmap))
		{
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("action"), TEXT("import_heightmap"));
			Request->SetNumberField(TEXT("width"), 2);
			Request->SetNumberField(TEXT("height"), 2);
			Request->SetStringField(TEXT("base64"), TEXT("AIAAgACAAIA="));
			TestTrue(TEXT("import_heightmap 接受 base64 raw16 高度图"), JsonSchema::ValidateObject(ImportHeightmap->InputSchema.ToSharedRef(), Request).IsValid());
		}

		const ActionContracts::FActionContract* ResetHeights = Manifest->FindAction(TEXT("landscape"), TEXT("reset_heights"));
		if (TestNotNull(TEXT("Landscape reset_heights action 存在"), ResetHeights))
		{
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("action"), TEXT("reset_heights"));
			Request->SetNumberField(TEXT("targetWorldZ"), 1000.0);
			TestTrue(TEXT("reset_heights 接受绝对世界高度"), JsonSchema::ValidateObject(ResetHeights->InputSchema.ToSharedRef(), Request).IsValid());
		}

		const TArray<FString> HeavyActions = { TEXT("sample_batch"), TEXT("sample_grid"), TEXT("sample_polyline"), TEXT("sculpt"), TEXT("sculpt_batch"), TEXT("set_height_rect"),
			TEXT("import_heightmap"), TEXT("reset_heights"), TEXT("paint_layer"), TEXT("create") };
		for (const FString& ActionName : HeavyActions)
		{
			const ActionContracts::FActionContract* Action = Manifest->FindAction(TEXT("landscape"), ActionName);
			if (!TestNotNull(*FString::Printf(TEXT("Landscape heavy action exists: %s"), *ActionName), Action))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s returns a task receipt"), *ActionName), Action->ExecutionMode == EMcpToolExecutionMode::Task);
			TestTrue(*FString::Printf(TEXT("%s can be cancelled before execution"), *ActionName), Action->bCancelable);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPActionManifestIntegrityFailureTest, "WorldData.UnrealAgent.Core.ActionManifest.IntegrityFailure",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPActionManifestIntegrityFailureTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent"));
		if (!TestTrue(TEXT("Unreal Agent 插件可定位"), Plugin.IsValid()))
		{
			return false;
		}
		FString Json;
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("ActionContracts.json"));
		if (!TestTrue(TEXT("Action Manifest 可读取"), FFileHelper::LoadFileToString(Json, *Path)))
		{
			return false;
		}
		const int32 ReplacementCount = Json.ReplaceInline(TEXT("worlddata.project.get_status"), TEXT("worlddata.project.get_status_tampered"), ESearchCase::CaseSensitive);
		TestTrue(TEXT("测试样本已篡改"), ReplacementCount > 0);

		TArray<FString> Errors;
		const TSharedPtr<ActionContracts::FManifest> Manifest = ActionContracts::FManifest::Parse(Json, Errors);
		TestFalse(TEXT("篡改后的 Manifest 被拒绝"), Manifest.IsValid());
		TestTrue(TEXT("拒绝原因包含 contractHash"),
			Errors.ContainsByPredicate(
				[](const FString& Error)
				{
					return Error.Contains(TEXT("contractHash"));
				}));
		return true;
	}
}

#endif
