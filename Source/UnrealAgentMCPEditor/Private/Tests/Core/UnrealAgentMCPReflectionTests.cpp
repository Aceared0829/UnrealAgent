// Copyright ZhaoZining. All Rights Reserved.

#include "Core/Reflection/UnrealAgentMCPReflection.h"
#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"
#include "Core/Results/UnrealAgentMCPAsyncResultStore.h"
#include "Core/Schema/UnrealAgentMCPJsonSchema.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Tests/Core/UnrealAgentMCPReflectionTestTypes.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPReflectionContractTest, "WorldData.UnrealAgent.Core.Reflection.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPReflectionContractTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::Reflection;

	const FDiscoveryResult Discovery = DiscoverClass(UUnrealAgentMCPReflectionTestToolset::StaticClass());
	TestTrue(TEXT("反射发现结果有效"), Discovery.IsValid());
	TestEqual(TEXT("发现两个反射工具"), Discovery.Tools.Num(), 2);
	if (Discovery.Tools.Num() != 2)
	{
		return false;
	}

	const FToolDescriptor* AddTool = Discovery.Tools.FindByPredicate(
		[](const FToolDescriptor& Candidate)
		{
			return Candidate.ToolName == TEXT("add");
		});
	TestNotNull(TEXT("找到整数相加工具"), AddTool);
	if (!AddTool)
	{
		return false;
	}
	TestEqual(TEXT("工具限定名称正确"), AddTool->QualifiedName, FString(TEXT("Tests.Math.add")));
	TestEqual(TEXT("显式风险元数据进入反射描述符"), AddTool->Risk, UnrealAgentMCP::EMcpToolRisk::ReadOnly);
	TestEqual(TEXT("显式事务元数据进入反射描述符"), AddTool->TransactionPolicy, UnrealAgentMCP::EMcpToolTransactionPolicy::ReadOnly);
	TestTrue(TEXT("显式幂等元数据进入反射描述符"), AddTool->bIdempotent);

	const FDiscoveryResult UnsafeDiscovery = DiscoverClass(UUnrealAgentMCPUnsafeReflectionTestToolset::StaticClass());
	TestFalse(TEXT("缺少安全元数据的反射工具 fail-closed"), UnsafeDiscovery.IsValid());
	TestEqual(TEXT("不注册缺少安全元数据的工具"), UnsafeDiscovery.Tools.Num(), 0);
	TestTrue(TEXT("缺失安全元数据产生稳定诊断"), UnsafeDiscovery.Errors.Num() == 1);

	const TSharedPtr<FJsonObject>* InputProperties = nullptr;
	TestTrue(TEXT("输入 Schema 包含属性"), AddTool->InputSchema->TryGetObjectField(TEXT("properties"), InputProperties));
	if (InputProperties && InputProperties->IsValid())
	{
		TestTrue(TEXT("存在 A 参数 Schema"), (*InputProperties)->HasField(TEXT("A")));
		TestTrue(TEXT("存在 B 参数 Schema"), (*InputProperties)->HasField(TEXT("B")));
		const TSharedPtr<FJsonObject>* BSchema = nullptr;
		double DefaultB = 0.0;
		TestTrue(TEXT("默认参数进入输入 Schema"),
			(*InputProperties)->TryGetObjectField(TEXT("B"), BSchema) && BSchema && BSchema->IsValid() && (*BSchema)->TryGetNumberField(TEXT("default"), DefaultB));
		TestEqual(TEXT("Schema 默认参数值正确"), DefaultB, 3.0);
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetNumberField(TEXT("A"), 5);
	TSharedPtr<FJsonObject> Result;
	FString Error;
	const bool bInvoked = Invoke(*AddTool, Arguments, Result, Error);
	TestTrue(TEXT("反射调用成功"), bInvoked);
	TestTrue(TEXT("反射调用没有错误"), Error.IsEmpty());
	if (!bInvoked)
	{
		AddError(FString::Printf(TEXT("反射调用错误：%s"), *Error));
		return false;
	}
	double ReturnValue = 0.0;
	TestTrue(TEXT("存在返回值"), Result.IsValid() && Result->TryGetNumberField(TEXT("ReturnValue"), ReturnValue));
	TestEqual(TEXT("默认参数已生效"), ReturnValue, 8.0);

	const FToolDescriptor* EchoTool = Discovery.Tools.FindByPredicate(
		[](const FToolDescriptor& Candidate)
		{
			return Candidate.ToolName == TEXT("echo_payload");
		});
	TestNotNull(TEXT("找到结构化载荷工具"), EchoTool);
	if (!EchoTool)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("Name"), TEXT("城墙批次"));
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueNumber>(11));
	Values.Add(MakeShared<FJsonValueNumber>(22));
	Payload->SetArrayField(TEXT("Values"), Values);
	TSharedPtr<FJsonObject> EchoArguments = MakeShared<FJsonObject>();
	EchoArguments->SetObjectField(TEXT("Payload"), Payload);
	TSharedPtr<FJsonObject> EchoResult;
	Error.Reset();
	TestTrue(TEXT("结构体与数组可以往返调用"), Invoke(*EchoTool, EchoArguments, EchoResult, Error));
	if (!EchoResult.IsValid())
	{
		AddError(FString::Printf(TEXT("结构化调用错误：%s"), *Error));
		return false;
	}

	const TSharedPtr<FJsonObject>* EchoedPayload = nullptr;
	TestTrue(TEXT("结构化返回值为对象"), EchoResult->TryGetObjectField(TEXT("ReturnValue"), EchoedPayload));
	if (EchoedPayload && EchoedPayload->IsValid())
	{
		FString EchoedName;
		TestTrue(TEXT("结构化名称可以读取"), (*EchoedPayload)->TryGetStringField(TEXT("Name"), EchoedName));
		TestEqual(TEXT("结构化名称保持不变"), EchoedName, FString(TEXT("城墙批次")));

		const TArray<TSharedPtr<FJsonValue>>* EchoedValues = nullptr;
		TestTrue(TEXT("结构化数组可以读取"), (*EchoedPayload)->TryGetArrayField(TEXT("Values"), EchoedValues));
		if (EchoedValues)
		{
			TestEqual(TEXT("结构化数组元素数量保持不变"), EchoedValues->Num(), 2);
		}
	}

	const TArray<FName> AdapterNames = FPropertyTypeAdapterRegistry::GetDefault().GetAdapterNames();
	TestEqual(TEXT("默认类型适配器数量"), AdapterNames.Num(), 7);
	TestTrue(TEXT("包含标量适配器"), AdapterNames.Contains(TEXT("Scalar")));
	TestTrue(TEXT("包含字符串适配器"), AdapterNames.Contains(TEXT("String")));
	TestTrue(TEXT("包含引用适配器"), AdapterNames.Contains(TEXT("Reference")));
	TestTrue(TEXT("包含 GUID 适配器"), AdapterNames.Contains(TEXT("Guid")));
	TestTrue(TEXT("包含结构体适配器"), AdapterNames.Contains(TEXT("Struct")));
	TestTrue(TEXT("包含容器适配器"), AdapterNames.Contains(TEXT("Container")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPropertyTypeMatrixTest, "WorldData.UnrealAgent.Core.Reflection.PropertyTypeMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPropertyTypeMatrixTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Reflection;

	FPropertyTypeAdapterRegistry& Registry = FPropertyTypeAdapterRegistry::GetDefault();
	UScriptStruct* MatrixStruct = FUnrealAgentMCPReflectionValueMatrix::StaticStruct();
	FUnrealAgentMCPReflectionValueMatrix Matrix;
	FString Error;

	FProperty* BoundedProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, Bounded));
	TestNotNull(TEXT("找到边界整数属性"), BoundedProperty);
	if (!BoundedProperty)
	{
		return false;
	}
	TSharedPtr<FJsonObject> BoundedSchema;
	TestTrue(TEXT("构建边界整数 Schema"), Registry.BuildSchema(BoundedProperty, BoundedSchema, Error));
	double Minimum = 0.0;
	double Maximum = 0.0;
	TestTrue(TEXT("ClampMin 转换为 minimum"), BoundedSchema.IsValid() && BoundedSchema->TryGetNumberField(TEXT("minimum"), Minimum));
	TestTrue(TEXT("ClampMax 转换为 maximum"), BoundedSchema.IsValid() && BoundedSchema->TryGetNumberField(TEXT("maximum"), Maximum));
	TestEqual(TEXT("整数最小值正确"), Minimum, 0.0);
	TestEqual(TEXT("整数最大值正确"), Maximum, 10.0);
	TestFalse(TEXT("超出 metadata 边界的整数被拒绝"),
		Registry.Read(MakeShared<FJsonValueNumber>(11.0), BoundedProperty, BoundedProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestEqual(TEXT("拒绝后整数保持原值"), Matrix.Bounded, 5);

	FArrayProperty* ValuesProperty = FindFProperty<FArrayProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, Values));
	TestNotNull(TEXT("找到数组属性"), ValuesProperty);
	if (!ValuesProperty)
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> InvalidValues;
	InvalidValues.Add(MakeShared<FJsonValueNumber>(1.0));
	InvalidValues.Add(MakeShared<FJsonValueString>(TEXT("invalid")));
	Error.Reset();
	TestFalse(TEXT("数组元素类型错误被拒绝"),
		Registry.Read(MakeShared<FJsonValueArray>(InvalidValues), ValuesProperty, ValuesProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestEqual(TEXT("失败转换不改变数组长度"), Matrix.Values.Num(), 2);
	TestEqual(TEXT("失败转换不改变数组首项"), Matrix.Values[0], 7);
	TestEqual(TEXT("失败转换不改变数组末项"), Matrix.Values[1], 8);

	TArray<TSharedPtr<FJsonValue>> ValidValues;
	ValidValues.Add(MakeShared<FJsonValueNumber>(1.0));
	ValidValues.Add(MakeShared<FJsonValueNumber>(2.0));
	ValidValues.Add(MakeShared<FJsonValueNumber>(3.0));
	Error.Reset();
	TestTrue(TEXT("合法数组可以原子写入"), Registry.Read(MakeShared<FJsonValueArray>(ValidValues), ValuesProperty, ValuesProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestEqual(TEXT("合法数组完整提交"), Matrix.Values.Num(), 3);

	FSetProperty* SetProperty = FindFProperty<FSetProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, UniqueValues));
	TestNotNull(TEXT("找到集合属性"), SetProperty);
	if (!SetProperty)
	{
		return false;
	}
	TSharedPtr<FJsonObject> SetSchema;
	Error.Reset();
	TestTrue(TEXT("构建集合 Schema"), Registry.BuildSchema(SetProperty, SetSchema, Error));
	TArray<TSharedPtr<FJsonValue>> DuplicateValues;
	DuplicateValues.Add(MakeShared<FJsonValueNumber>(1.0));
	DuplicateValues.Add(MakeShared<FJsonValueNumber>(1.0));
	TestFalse(TEXT("uniqueItems 拒绝重复集合元素"), JsonSchema::ValidateValue(SetSchema.ToSharedRef(), MakeShared<FJsonValueArray>(DuplicateValues)).IsValid());

	FMapProperty* MapProperty = FindFProperty<FMapProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, Scores));
	TestNotNull(TEXT("找到 Map 属性"), MapProperty);
	if (!MapProperty)
	{
		return false;
	}
	TSharedPtr<FJsonObject> MapSchema;
	Error.Reset();
	TestTrue(TEXT("构建 Map Schema"), Registry.BuildSchema(MapProperty, MapSchema, Error));
	TSharedRef<FJsonObject> InvalidMap = MakeShared<FJsonObject>();
	InvalidMap->SetStringField(TEXT("Wall"), TEXT("invalid"));
	TestFalse(TEXT("additionalProperties Schema 校验 Map Value"), JsonSchema::ValidateValue(MapSchema.ToSharedRef(), MakeShared<FJsonValueObject>(InvalidMap)).IsValid());

	FProperty* GuidProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, Identifier));
	TestNotNull(TEXT("找到 GUID 属性"), GuidProperty);
	if (!GuidProperty)
	{
		return false;
	}
	TSharedPtr<FJsonObject> GuidSchema;
	Error.Reset();
	TestTrue(TEXT("GUID 使用专用 Schema"), Registry.BuildSchema(GuidProperty, GuidSchema, Error) && GuidSchema->GetStringField(TEXT("format")) == TEXT("uuid"));
	const FGuid ExpectedGuid(0xE05FCC13, 0x4D379D7A, 0xE2388385, 0x9F29AD74);
	const FString GuidText = ExpectedGuid.ToString(EGuidFormats::DigitsWithHyphens);
	TestTrue(TEXT("合法 GUID 可以写入"), Registry.Read(MakeShared<FJsonValueString>(GuidText), GuidProperty, GuidProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("GUID 值保持精确"), Matrix.Identifier == ExpectedGuid);
	TSharedPtr<FJsonValue> GuidJson;
	TestTrue(TEXT("GUID 可以写回 JSON"), Registry.Write(GuidProperty, GuidProperty->ContainerPtrToValuePtr<void>(&Matrix), GuidJson, Error));
	TestEqual(TEXT("GUID 往返文本稳定"), GuidJson->AsString(), GuidText);

	FProperty* CodeProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, Code));
	TestNotNull(TEXT("找到字符串约束属性"), CodeProperty);
	if (!CodeProperty)
	{
		return false;
	}
	Error.Reset();
	TestFalse(TEXT("字符串 pattern 在写入前生效"),
		Registry.Read(MakeShared<FJsonValueString>(TEXT("lower")), CodeProperty, CodeProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestEqual(TEXT("pattern 拒绝后字符串保持原值"), Matrix.Code, FString(TEXT("UE")));

	UObject* ReferenceObject = NewObject<UUnrealAgentMCPReflectionTestToolset>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UUnrealAgentMCPReflectionTestToolset::StaticClass(), TEXT("UnrealAgentMCPReflectionReference")));
	TestNotNull(TEXT("创建引用测试对象"), ReferenceObject);
	if (!ReferenceObject)
	{
		return false;
	}
	const FString ReferencePath = ReferenceObject->GetPathName();

	FProperty* ObjectProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, ObjectReference));
	TestNotNull(TEXT("找到硬对象引用属性"), ObjectProperty);
	if (!ObjectProperty)
	{
		return false;
	}
	TSharedPtr<FJsonObject> ObjectSchema;
	Error.Reset();
	TestTrue(TEXT("对象引用 Schema 同时允许路径与 null"),
		Registry.BuildSchema(ObjectProperty, ObjectSchema, Error) && ObjectSchema->HasField(TEXT("anyOf")) &&
			ObjectSchema->GetStringField(TEXT("x-unreal-class")) == UObject::StaticClass()->GetPathName());
	TestTrue(TEXT("已加载对象路径可以写入硬引用"),
		Registry.Read(MakeShared<FJsonValueString>(ReferencePath), ObjectProperty, ObjectProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("硬引用指向解析对象"), Matrix.ObjectReference == ReferenceObject);
	TSharedPtr<FJsonValue> ReferenceJson;
	TestTrue(TEXT("硬引用写出稳定对象路径"), Registry.Write(ObjectProperty, ObjectProperty->ContainerPtrToValuePtr<void>(&Matrix), ReferenceJson, Error));
	TestEqual(TEXT("硬引用路径往返一致"), ReferenceJson->AsString(), ReferencePath);
	Error.Reset();
	TestFalse(TEXT("非法路径被引用适配器拒绝"),
		Registry.Read(MakeShared<FJsonValueString>(TEXT("not-an-object-path")), ObjectProperty, ObjectProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("非法路径拒绝后硬引用保持不变"), Matrix.ObjectReference == ReferenceObject);
	TestTrue(TEXT("JSON null 清空硬引用"), Registry.Read(MakeShared<FJsonValueNull>(), ObjectProperty, ObjectProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestNull(TEXT("硬引用已清空"), Matrix.ObjectReference.Get());

	FProperty* SoftProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, SoftReference));
	TestNotNull(TEXT("找到软对象引用属性"), SoftProperty);
	if (!SoftProperty)
	{
		return false;
	}
	const FString UnloadedPath = TEXT("/Game/MCP/NotLoaded.NotLoaded");
	Error.Reset();
	TestTrue(TEXT("软引用接受尚未加载的合法对象路径"),
		Registry.Read(MakeShared<FJsonValueString>(UnloadedPath), SoftProperty, SoftProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestEqual(TEXT("软引用不会为校验强制加载对象"), Matrix.SoftReference.ToSoftObjectPath().ToString(), UnloadedPath);

	FProperty* WeakProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, WeakReference));
	TestNotNull(TEXT("找到弱对象引用属性"), WeakProperty);
	if (!WeakProperty)
	{
		return false;
	}
	Error.Reset();
	TestTrue(TEXT("弱引用接受已加载对象"), Registry.Read(MakeShared<FJsonValueString>(ReferencePath), WeakProperty, WeakProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("弱引用解析正确"), Matrix.WeakReference.Get() == ReferenceObject);
	TestFalse(TEXT("弱引用拒绝尚未加载对象"), Registry.Read(MakeShared<FJsonValueString>(UnloadedPath), WeakProperty, WeakProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("弱引用拒绝后保持原值"), Matrix.WeakReference.Get() == ReferenceObject);

	FProperty* ClassProperty = FindFProperty<FProperty>(MatrixStruct, GET_MEMBER_NAME_CHECKED(FUnrealAgentMCPReflectionValueMatrix, ClassReference));
	TestNotNull(TEXT("找到类引用属性"), ClassProperty);
	if (!ClassProperty)
	{
		return false;
	}
	Error.Reset();
	TestTrue(TEXT("原生类路径可以写入 TSubclassOf"),
		Registry.Read(MakeShared<FJsonValueString>(UObject::StaticClass()->GetPathName()), ClassProperty, ClassProperty->ContainerPtrToValuePtr<void>(&Matrix), Error));
	TestTrue(TEXT("TSubclassOf 解析正确"), Matrix.ClassReference.Get() == UObject::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPropertyWriterIntegrationTest, "WorldData.UnrealAgent.Editor.Reflection.PropertyWriterIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPropertyWriterIntegrationTest::RunTest(const FString& Parameters)
{
	UUnrealAgentMCPReflectionPropertyHolder* Holder = NewObject<UUnrealAgentMCPReflectionPropertyHolder>();
	TestNotNull(TEXT("创建属性写入测试对象"), Holder);
	if (!Holder)
	{
		return false;
	}

	FProperty* CountProperty = FindFProperty<FProperty>(Holder->GetClass(), GET_MEMBER_NAME_CHECKED(UUnrealAgentMCPReflectionPropertyHolder, Count));
	FProperty* OffsetProperty = FindFProperty<FProperty>(Holder->GetClass(), GET_MEMBER_NAME_CHECKED(UUnrealAgentMCPReflectionPropertyHolder, Offset));
	FProperty* ValuesProperty = FindFProperty<FProperty>(Holder->GetClass(), GET_MEMBER_NAME_CHECKED(UUnrealAgentMCPReflectionPropertyHolder, Values));
	FProperty* ReadOnlyProperty = FindFProperty<FProperty>(Holder->GetClass(), GET_MEMBER_NAME_CHECKED(UUnrealAgentMCPReflectionPropertyHolder, ReadOnlyValue));
	TestNotNull(TEXT("找到整数属性"), CountProperty);
	TestNotNull(TEXT("找到向量属性"), OffsetProperty);
	TestNotNull(TEXT("找到数组属性"), ValuesProperty);
	TestNotNull(TEXT("找到只读属性"), ReadOnlyProperty);
	if (!CountProperty || !OffsetProperty || !ValuesProperty || !ReadOnlyProperty)
	{
		return false;
	}

	FString Error;
	TestFalse(TEXT("整数属性拒绝小数"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, CountProperty, MakeShared<FJsonValueNumber>(3.5), Error));
	TestEqual(TEXT("整数转换失败后保持旧值"), Holder->Count, 5);
	Error.Reset();
	TestFalse(TEXT("整数属性应用元数据上限"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, CountProperty, MakeShared<FJsonValueNumber>(12.0), Error));
	TestEqual(TEXT("越界失败后保持旧值"), Holder->Count, 5);

	TArray<TSharedPtr<FJsonValue>> VectorValues = { MakeShared<FJsonValueNumber>(10.0), MakeShared<FJsonValueNumber>(20.0), MakeShared<FJsonValueNumber>(30.0) };
	Error.Reset();
	TestTrue(TEXT("兼容向量数组表示"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, OffsetProperty, MakeShared<FJsonValueArray>(VectorValues), Error));
	TestTrue(TEXT("向量数组转换结果正确"), Holder->Offset.Equals(FVector(10.0, 20.0, 30.0)));

	TSharedRef<FJsonObject> PartialVector = MakeShared<FJsonObject>();
	PartialVector->SetNumberField(TEXT("x"), 40.0);
	Error.Reset();
	TestTrue(TEXT("兼容向量局部分量更新"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, OffsetProperty, MakeShared<FJsonValueObject>(PartialVector), Error));
	TestTrue(TEXT("未提供的向量分量保持不变"), Holder->Offset.Equals(FVector(40.0, 20.0, 30.0)));

	TArray<TSharedPtr<FJsonValue>> InvalidVectorValues = { MakeShared<FJsonValueNumber>(4.0), MakeShared<FJsonValueString>(TEXT("invalid")), MakeShared<FJsonValueNumber>(6.0) };
	Error.Reset();
	TestFalse(TEXT("向量分量拒绝字符串数字"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, OffsetProperty, MakeShared<FJsonValueArray>(InvalidVectorValues), Error));
	TestTrue(TEXT("向量转换失败后整体保持不变"), Holder->Offset.Equals(FVector(40.0, 20.0, 30.0)));

	TArray<TSharedPtr<FJsonValue>> InvalidArrayValues = { MakeShared<FJsonValueNumber>(1.0), MakeShared<FJsonValueString>(TEXT("invalid")) };
	Error.Reset();
	TestFalse(TEXT("容器元素失败时拒绝整个写入"),
		UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, ValuesProperty, MakeShared<FJsonValueArray>(InvalidArrayValues), Error));
	TestEqual(TEXT("容器失败后元素数量不变"), Holder->Values.Num(), 2);
	TestEqual(TEXT("容器失败后首元素不变"), Holder->Values[0], 7);
	TestEqual(TEXT("容器失败后尾元素不变"), Holder->Values[1], 8);

	Error.Reset();
	TestFalse(TEXT("只读属性仍由编辑层拒绝"), UnrealAgentMCP::PropertyWriter::SetPropertyFromJson(Holder, ReadOnlyProperty, MakeShared<FJsonValueNumber>(12.0), Error));
	TestEqual(TEXT("只读属性保持不变"), Holder->ReadOnlyValue, 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAsyncResultStoreTest, "WorldData.UnrealAgent.Core.Results.AsyncLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPAsyncResultStoreTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	(void)Parameters;

	FAsyncResultStore Store(FTimespan::FromSeconds(5));
	int32 CompletionCount = 0;
	EAsyncResultState LastCompletionState = EAsyncResultState::Pending;
	bool bCallbackCanReadCompletedResult = true;
	const FDelegateHandle CompletionHandle = Store.OnCompleted().AddLambda(
		[&](const FAsyncResultSnapshot Completed)
		{
			++CompletionCount;
			LastCompletionState = Completed.State;
			FAsyncResultSnapshot ReadBack;
			bCallbackCanReadCompletedResult = bCallbackCanReadCompletedResult && Store.TryRead(Completed.Id, ReadBack) && ReadBack.State == Completed.State;
		});
	const FGuid Id = Store.Create();
	TestTrue(TEXT("结果标识有效"), Id.IsValid());
	TestEqual(TEXT("Pending 数量"), Store.Num(), 1);

	FAsyncResultSnapshot Snapshot;
	TestTrue(TEXT("Pending 结果可读取"), Store.TryRead(Id, Snapshot));
	TestEqual(TEXT("初始状态"), Snapshot.State, EAsyncResultState::Pending);
	TestFalse(TEXT("非法 JSON 不能完成结果"), Store.Complete(Id, TEXT("not-json")));
	TestEqual(TEXT("非法完成不触发通知"), CompletionCount, 0);
	TestTrue(TEXT("合法 JSON 完成成功"), Store.Complete(Id, TEXT("{\"ok\":true}")));
	TestEqual(TEXT("首次完成只通知一次"), CompletionCount, 1);
	TestEqual(TEXT("完成通知携带成功状态"), LastCompletionState, EAsyncResultState::Succeeded);
	TestTrue(TEXT("完成回调可重入读取 Store"), bCallbackCanReadCompletedResult);
	TestFalse(TEXT("重复完成被拒绝"), Store.Fail(Id, TEXT("late")));
	TestEqual(TEXT("重复完成不再次通知"), CompletionCount, 1);

	TestTrue(TEXT("完成结果可消费"), Store.TryRead(Id, Snapshot, true));
	TestEqual(TEXT("完成状态"), Snapshot.State, EAsyncResultState::Succeeded);
	TestEqual(TEXT("完成载荷"), Snapshot.ValueJson, FString(TEXT("{\"ok\":true}")));
	const TSharedRef<FJsonObject> SnapshotJson = Snapshot.ToJsonObject();
	TestEqual(TEXT("结构化快照状态稳定"), SnapshotJson->GetStringField(TEXT("state")), FString(TEXT("Succeeded")));
	TestTrue(TEXT("结构化快照标记完成"), SnapshotJson->GetBoolField(TEXT("complete")));
	TestTrue(TEXT("结构化快照直接承载 JSON 值而非二次编码字符串"), SnapshotJson->GetObjectField(TEXT("value"))->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("消费后结果被删除"), Store.Num(), 0);

	const FGuid ExpiringId = Store.Create();
	const FDateTime ExpiredAt = FDateTime::UtcNow() + FTimespan::FromSeconds(6);
	TestEqual(TEXT("到期 Pending 结果先进入取消终态"), Store.PurgeExpired(ExpiredAt), 1);
	TestTrue(TEXT("到期结果在一个保留周期内仍可读取"), Store.TryRead(ExpiringId, Snapshot));
	TestEqual(TEXT("到期结果状态为 Cancelled"), Snapshot.State, EAsyncResultState::Cancelled);
	TestTrue(TEXT("到期结果保留明确诊断"), Snapshot.Error.Contains(TEXT("expired")));
	TestEqual(TEXT("到期转换触发完成通知"), CompletionCount, 2);
	TestEqual(TEXT("完成保留期结束后删除到期结果"), Store.PurgeExpired(ExpiredAt + FTimespan::FromSeconds(5)), 1);
	TestFalse(TEXT("二阶段清理后结果不存在"), Store.TryRead(ExpiringId, Snapshot));
	Store.OnCompleted().Remove(CompletionHandle);
	return true;
}
