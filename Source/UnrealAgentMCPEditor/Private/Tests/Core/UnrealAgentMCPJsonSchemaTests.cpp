// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPJsonSchemaTests.cpp
 * @brief 统一工具 Schema 校验器的基础契约测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/Schema/UnrealAgentMCPJsonSchema.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPJsonSchemaContractTest, "WorldData.UnrealAgent.Core.Schema.Contract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPJsonSchemaContractTest::RunTest(const FString& Parameters)
	{
		TSharedRef<FJsonObject> NameSchema = MakeShared<FJsonObject>();
		NameSchema->SetStringField(TEXT("type"), TEXT("string"));
		NameSchema->SetArrayField(TEXT("enum"), { MakeShared<FJsonValueString>(TEXT("wall")), MakeShared<FJsonValueString>(TEXT("house")) });

		TSharedRef<FJsonObject> CountSchema = MakeShared<FJsonObject>();
		CountSchema->SetStringField(TEXT("type"), TEXT("integer"));
		CountSchema->SetNumberField(TEXT("minimum"), 1);
		CountSchema->SetNumberField(TEXT("maximum"), 10);

		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("name"), NameSchema);
		Properties->SetObjectField(TEXT("count"), CountSchema);

		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Properties);
		Schema->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("name")) });
		Schema->SetBoolField(TEXT("additionalProperties"), false);

		TSharedRef<FJsonObject> Valid = MakeShared<FJsonObject>();
		Valid->SetStringField(TEXT("name"), TEXT("wall"));
		Valid->SetNumberField(TEXT("count"), 4);
		TestTrue(TEXT("有效对象通过 Schema"), JsonSchema::ValidateObject(Schema, Valid).IsValid());

		TSharedRef<FJsonObject> Missing = MakeShared<FJsonObject>();
		const JsonSchema::FValidationResult MissingResult = JsonSchema::ValidateObject(Schema, Missing);
		TestFalse(TEXT("缺少必填字段被拒绝"), MissingResult.IsValid());
		TestTrue(TEXT("错误路径指向缺失字段"), !MissingResult.Errors.IsEmpty() && MissingResult.Errors[0].Path == TEXT("$.name"));

		TSharedRef<FJsonObject> Invalid = MakeShared<FJsonObject>();
		Invalid->SetStringField(TEXT("name"), TEXT("castle"));
		Invalid->SetNumberField(TEXT("count"), 2.5);
		Invalid->SetBoolField(TEXT("unexpected"), true);
		const JsonSchema::FValidationResult InvalidResult = JsonSchema::ValidateObject(Schema, Invalid);
		TestFalse(TEXT("枚举、整数和额外字段错误被拒绝"), InvalidResult.IsValid());
		TestEqual(TEXT("三个独立错误均被报告"), InvalidResult.Errors.Num(), 3);
		const FString ErrorJson = JsonSchema::MakeValidationErrorJson(TEXT("schema_test"), InvalidResult);
		TestTrue(TEXT("错误结果包含稳定错误码"), ErrorJson.Contains(TEXT("\"code\":\"invalid_arguments\"")));
		TestTrue(TEXT("错误结果包含字段路径"), ErrorJson.Contains(TEXT("$.unexpected")));

		TSharedRef<FJsonObject> SelectorProperties = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ActionSchema = MakeShared<FJsonObject>();
		ActionSchema->SetStringField(TEXT("type"), TEXT("string"));
		SelectorProperties->SetObjectField(TEXT("action"), ActionSchema);
		TSharedRef<FJsonObject> SelectorSchema = MakeShared<FJsonObject>();
		SelectorSchema->SetStringField(TEXT("type"), TEXT("string"));
		SelectorProperties->SetObjectField(TEXT("selector"), SelectorSchema);
		TSharedRef<FJsonObject> SelectorBranch = MakeShared<FJsonObject>();
		SelectorBranch->SetStringField(TEXT("type"), TEXT("object"));
		SelectorBranch->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("selector")) });
		SelectorBranch->SetBoolField(TEXT("additionalProperties"), true);
		TSharedRef<FJsonObject> CombinedSchema = MakeShared<FJsonObject>();
		CombinedSchema->SetStringField(TEXT("type"), TEXT("object"));
		CombinedSchema->SetObjectField(TEXT("properties"), SelectorProperties);
		CombinedSchema->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("action")) });
		CombinedSchema->SetArrayField(TEXT("anyOf"), { MakeShared<FJsonValueObject>(SelectorBranch) });
		CombinedSchema->SetBoolField(TEXT("additionalProperties"), false);
		TSharedRef<FJsonObject> CombinedValue = MakeShared<FJsonObject>();
		CombinedValue->SetStringField(TEXT("action"), TEXT("delete"));
		CombinedValue->SetStringField(TEXT("selector"), TEXT("Wall"));
		TestTrue(TEXT("anyOf 与外层对象约束共同通过"), JsonSchema::ValidateObject(CombinedSchema, CombinedValue).IsValid());
		CombinedValue->SetStringField(TEXT("unexpected"), TEXT("rejected"));
		TestFalse(TEXT("anyOf 命中后仍执行外层 additionalProperties 约束"), JsonSchema::ValidateObject(CombinedSchema, CombinedValue).IsValid());

		FMcpToolDescriptor Descriptor;
		Descriptor.Name = TEXT("risk_annotation_test");
		Descriptor.QualifiedName = Descriptor.Name;
		Descriptor.Risk = EMcpToolRisk::FileMutation;
		TestTrue(TEXT("FileMutation 必须导出 destructiveHint"), Descriptor.ToJsonObject()->GetObjectField(TEXT("annotations"))->GetBoolField(TEXT("destructiveHint")));
		Descriptor.Risk = EMcpToolRisk::CodeExecution;
		TestTrue(TEXT("CodeExecution 必须导出 destructiveHint"), Descriptor.ToJsonObject()->GetObjectField(TEXT("annotations"))->GetBoolField(TEXT("destructiveHint")));
		Descriptor.Risk = EMcpToolRisk::ContentMutation;
		TestFalse(TEXT("普通内容变更不应错误标记为 destructiveHint"), Descriptor.ToJsonObject()->GetObjectField(TEXT("annotations"))->GetBoolField(TEXT("destructiveHint")));
		return true;
	}
}

#endif
