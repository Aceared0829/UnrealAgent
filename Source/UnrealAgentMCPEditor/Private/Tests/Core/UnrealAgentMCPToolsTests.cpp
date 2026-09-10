// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPToolsTests.cpp
 * @brief MCP 工具纯转换与资产路径失败分支自动化测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPJsonConversionTest, "WorldData.UnrealAgent.Tools.JsonConversion.PreservesProtocolCompatibleValueRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPJsonConversionTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::JsonConversion;

	TestEqual(TEXT("缺少对象名时应补全 Package.Asset"), NormalizeAssetObjectPath(TEXT(" /Game/Characters/Hero ")), FString(TEXT("/Game/Characters/Hero.Hero")));
	TestEqual(TEXT("完整对象路径必须保持不变"), NormalizeAssetObjectPath(TEXT("/Game/Characters/Hero.Hero")), FString(TEXT("/Game/Characters/Hero.Hero")));

	bool bParsedBool = false;
	TestTrue(TEXT("协议兼容 yes 字符串布尔值"), TryValueToBool(MakeShared<FJsonValueString>(TEXT("yes")), bParsedBool));
	TestTrue(TEXT("yes 应转换为 true"), bParsedBool);
	TestFalse(TEXT("数字 JSON 值不应隐式转换为布尔值"), TryValueToBool(MakeShared<FJsonValueNumber>(1.0), bParsedBool));

	TArray<TSharedPtr<FJsonValue>> VectorValues;
	VectorValues.Add(MakeShared<FJsonValueNumber>(10.0));
	VectorValues.Add(MakeShared<FJsonValueString>(TEXT("20")));
	VectorValues.Add(MakeShared<FJsonValueNumber>(30.0));
	FVector ParsedVector = FVector::ZeroVector;
	TestTrue(TEXT("向量数组应兼容数字和数字字符串"), TryValueToVector(MakeShared<FJsonValueArray>(VectorValues), ParsedVector));
	TestTrue(TEXT("向量数组应保持 XYZ 顺序"), ParsedVector.Equals(FVector(10.0, 20.0, 30.0)));

	TSharedRef<FJsonObject> PartialVectorObject = MakeShared<FJsonObject>();
	PartialVectorObject->SetNumberField(TEXT("X"), 7.0);
	ParsedVector = FVector(1.0, 2.0, 3.0);
	TestTrue(TEXT("向量对象字段名应忽略大小写"), TryValueToVector(MakeShared<FJsonValueObject>(PartialVectorObject), ParsedVector));
	TestTrue(TEXT("缺失分量应保留调用方提供的默认值"), ParsedVector.Equals(FVector(7.0, 2.0, 3.0)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetPathValidationTest, "WorldData.UnrealAgent.Tools.AssetPathValidation.PreservesFieldsAndErrorMessages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPAssetPathValidationTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::AssetSupport;

	FString PackageName;
	FString AssetName;
	FString Error;
	TestTrue(TEXT("有效对象路径应成功拆分"), SplitAssetPath(TEXT("/Game/Generated/BP_Test.BP_Test"), PackageName, AssetName, Error));
	TestEqual(TEXT("包名不应包含对象后缀"), PackageName, FString(TEXT("/Game/Generated/BP_Test")));
	TestEqual(TEXT("资产名应来自包路径末段"), AssetName, FString(TEXT("BP_Test")));
	TestTrue(TEXT("成功时错误文本应为空"), Error.IsEmpty());

	PackageName.Reset();
	AssetName.Reset();
	Error.Reset();
	TestFalse(TEXT("空路径必须失败"), SplitAssetPath(FString(), PackageName, AssetName, Error));
	TestEqual(TEXT("空路径错误文本属于 MCP 协议的一部分"), Error, FString(TEXT("assetPath is required.")));

	PackageName.Reset();
	AssetName.Reset();
	Error.Reset();
	TestFalse(TEXT("插件不能通过该工具向 /Game 以外写入"), SplitAssetPath(TEXT("/Engine/Generated/Forbidden"), PackageName, AssetName, Error));
	TestEqual(TEXT("越界路径错误文本必须保持兼容"), Error, FString(TEXT("assetPath must be under /Game.")));

	return true;
}

#endif
