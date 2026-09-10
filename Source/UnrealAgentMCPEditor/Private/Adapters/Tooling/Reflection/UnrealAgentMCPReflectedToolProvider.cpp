// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPReflectedToolProvider.cpp
 * @brief 自有反射描述符到统一工具描述符的转换实现。
 */

#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolProvider.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Reflection/UnrealAgentMCPReflection.h"
#include "Dom/JsonObject.h"

namespace UnrealAgentMCP
{
	FName FUnrealAgentMCPReflectedToolProvider::GetProviderName() const
	{
		return TEXT("Reflection");
	}

	void FUnrealAgentMCPReflectedToolProvider::EnumerateTools(TArray<FMcpToolDescriptor>& OutTools, TArray<FString>& OutErrors)
	{
		const Reflection::FDiscoveryResult Discovery = Reflection::DiscoverLoadedClasses();
		OutErrors.Append(Discovery.Errors);
		for (const Reflection::FToolDescriptor& Reflected : Discovery.Tools)
		{
			FMcpToolDescriptor Descriptor;
			Descriptor.Provider = GetProviderName();
			Descriptor.Toolset = Reflected.ToolsetName;
			Descriptor.Name = Reflected.QualifiedName;
			Descriptor.QualifiedName = Reflected.QualifiedName;
			Descriptor.Description = Reflected.Description;
			Descriptor.InputSchema = Reflected.InputSchema;
			// Invoker 使用统一成功信封；发布真实线格式，避免拿整个信封
			// 去校验仅描述 UFUNCTION 返回值的内层 Schema。
			TSharedRef<FJsonObject> OutputSchema = MakeShared<FJsonObject>();
			OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> OutputProperties = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> SuccessSchema = MakeShared<FJsonObject>();
			SuccessSchema->SetStringField(TEXT("type"), TEXT("boolean"));
			OutputProperties->SetObjectField(TEXT("success"), SuccessSchema);
			TSharedRef<FJsonObject> ToolSchema = MakeShared<FJsonObject>();
			ToolSchema->SetStringField(TEXT("type"), TEXT("string"));
			OutputProperties->SetObjectField(TEXT("tool"), ToolSchema);
			OutputProperties->SetObjectField(TEXT("result"), Reflected.OutputSchema);
			OutputSchema->SetObjectField(TEXT("properties"), OutputProperties);
			TArray<TSharedPtr<FJsonValue>> Required;
			Required.Add(MakeShared<FJsonValueString>(TEXT("success")));
			Required.Add(MakeShared<FJsonValueString>(TEXT("tool")));
			Required.Add(MakeShared<FJsonValueString>(TEXT("result")));
			OutputSchema->SetArrayField(TEXT("required"), Required);
			OutputSchema->SetBoolField(TEXT("additionalProperties"), false);
			Descriptor.OutputSchema = OutputSchema;
			Descriptor.Risk = Reflected.Risk;
			Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
			Descriptor.TransactionPolicy = Reflected.TransactionPolicy;
			Descriptor.bReadOnly = Reflected.bReadOnly;
			Descriptor.bIdempotent = Reflected.bIdempotent;
			Descriptor.Invoker = [Reflected](const TSharedPtr<FJsonObject>& Arguments)
			{
				TSharedPtr<FJsonObject> Value;
				FString Error;
				if (!Reflection::Invoke(Reflected, Arguments, Value, Error))
				{
					return ErrorJson(Error);
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("tool"), Reflected.QualifiedName);
				Result->SetObjectField(TEXT("result"), Value);
				return SuccessJson(Result);
			};
			OutTools.Add(MoveTemp(Descriptor));
		}
	}
}
