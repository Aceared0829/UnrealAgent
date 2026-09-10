// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPReflectedToolsetAdapter.cpp
 * @brief 独立反射工具网关及旧网关兼容实现。
 */

#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolsetAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Reflection/UnrealAgentMCPReflection.h"
#include "Core/Results/UnrealAgentMCPAsyncResultStore.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP::Toolsets
{
	namespace
	{
		using Reflection::FDiscoveryResult;
		using Reflection::FToolDescriptor;

		FAsyncResultStore& GetAsyncResultStore()
		{
			static FAsyncResultStore Store(FTimespan::FromMinutes(10));
			return Store;
		}

		FString RequiredString(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name)
		{
			FString Value;
			if (Arguments.IsValid())
			{
				Arguments->TryGetStringField(Name, Value);
			}
			Value.TrimStartAndEndInline();
			return Value;
		}

		FDiscoveryResult Discover()
		{
			return Reflection::DiscoverLoadedClasses();
		}

		TMap<FString, TArray<const FToolDescriptor*>> GroupByToolset(const FDiscoveryResult& Discovery)
		{
			TMap<FString, TArray<const FToolDescriptor*>> Groups;
			for (const FToolDescriptor& Tool : Discovery.Tools)
			{
				Groups.FindOrAdd(Tool.ToolsetName).Add(&Tool);
			}
			return Groups;
		}

		const FToolDescriptor* FindTool(const FDiscoveryResult& Discovery, const FString& ToolsetName, const FString& ToolName)
		{
			for (const FToolDescriptor& Tool : Discovery.Tools)
			{
				if (Tool.ToolsetName.Equals(ToolsetName, ESearchCase::IgnoreCase) &&
					(Tool.ToolName.Equals(ToolName, ESearchCase::IgnoreCase) || Tool.QualifiedName.Equals(ToolName, ESearchCase::IgnoreCase)))
				{
					return &Tool;
				}
			}
			return nullptr;
		}

		TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Object))
			{
				Object.Reset();
			}
			return Object;
		}

		FString CallIdToString(const FGuid& CallId)
		{
			return CallId.ToString(EGuidFormats::DigitsWithHyphensLower);
		}

		bool ScheduleDeferredCall(const FString& ToolsetName, const FString& ToolName, const FString& InputJson, FGuid& OutCallId, FString& OutError)
		{
			FAsyncResultStore& Store = GetAsyncResultStore();
			Store.PurgeExpired();
			OutCallId = Store.Create();
			const FTSTicker::FDelegateHandle TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[ToolsetName, ToolName, InputJson, CallId = OutCallId](const float)
				{
					FAsyncResultStore& ActiveStore = GetAsyncResultStore();
					const TSharedPtr<FJsonObject> Input = ParseJsonObject(InputJson);
					if (!Input.IsValid())
					{
						ActiveStore.Fail(CallId, TEXT("Deferred tool input is no longer valid JSON."));
						return false;
					}

					const FDiscoveryResult Discovery = Discover();
					const FToolDescriptor* Tool = FindTool(Discovery, ToolsetName, ToolName);
					if (!Tool)
					{
						ActiveStore.Fail(CallId, FString::Printf(TEXT("Reflected tool became unavailable: %s.%s"), *ToolsetName, *ToolName));
						return false;
					}

					TSharedPtr<FJsonObject> Value;
					FString Error;
					if (!Reflection::Invoke(*Tool, Input, Value, Error))
					{
						ActiveStore.Fail(CallId, MoveTemp(Error));
						return false;
					}
					if (!Value.IsValid() || !ActiveStore.Complete(CallId, JsonObjectToString(Value.ToSharedRef())))
					{
						ActiveStore.Fail(CallId, TEXT("Unable to publish the reflected tool result."));
					}
					return false;
				}));
			if (TickerHandle.IsValid())
			{
				return true;
			}

			OutError = TEXT("Unable to schedule the reflected tool call.");
			Store.Fail(OutCallId, OutError);
			return false;
		}

		TSharedRef<FJsonObject> ToolJson(const FToolDescriptor& Tool, const bool bIncludeSchemas)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Tool.QualifiedName);
			Object->SetStringField(TEXT("shortName"), Tool.ToolName);
			Object->SetStringField(TEXT("description"), Tool.Description);
			if (bIncludeSchemas)
			{
				Object->SetObjectField(TEXT("inputSchema"), Tool.InputSchema);
				Object->SetObjectField(TEXT("outputSchema"), Tool.OutputSchema);
			}
			return Object;
		}
	}

	FString GetStatus(const TSharedPtr<FJsonObject>&)
	{
		const FDiscoveryResult Discovery = Discover();
		const int32 ToolsetCount = GroupByToolset(Discovery).Num();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("available"), true);
		Result->SetBoolField(TEXT("independent"), true);
		Result->SetStringField(TEXT("implementation"), TEXT("UnrealAgent"));
		Result->SetStringField(TEXT("registry"), TEXT("WorldDataReflection"));
		Result->SetNumberField(TEXT("toolsetCount"), ToolsetCount);
		Result->SetNumberField(TEXT("toolCount"), Discovery.Tools.Num());
		Result->SetNumberField(TEXT("discoveryErrorCount"), Discovery.Errors.Num());
		Result->SetStringField(TEXT("discoveryMode"), TEXT("list_toolsets/describe_toolset/call_tool"));
		return SuccessJson(Result);
	}

	FString ListToolsets(const TSharedPtr<FJsonObject>& Arguments)
	{
		FString NameFilter;
		bool bIncludeSchemas = false;
		double RequestedMaxResults = 100.0;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);
			Arguments->TryGetBoolField(TEXT("includeSchemas"), bIncludeSchemas);
			Arguments->TryGetNumberField(TEXT("maxResults"), RequestedMaxResults);
		}
		const int32 MaxResults = FMath::Clamp(FMath::RoundToInt(RequestedMaxResults), 1, 500);

		const FDiscoveryResult Discovery = Discover();
		const TMap<FString, TArray<const FToolDescriptor*>> Groups = GroupByToolset(Discovery);
		TArray<FString> Names;
		Groups.GetKeys(Names);
		Names.Sort();

		TArray<TSharedPtr<FJsonValue>> Toolsets;
		for (const FString& Name : Names)
		{
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const TArray<const FToolDescriptor*>& Tools = Groups[Name];
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("version"), TEXT("1.0"));
			Entry->SetNumberField(TEXT("toolCount"), Tools.Num());
			TArray<TSharedPtr<FJsonValue>> ToolValues;
			for (const FToolDescriptor* Tool : Tools)
			{
				ToolValues.Add(MakeShared<FJsonValueObject>(ToolJson(*Tool, bIncludeSchemas)));
			}
			Entry->SetArrayField(TEXT("tools"), MoveTemp(ToolValues));
			Toolsets.Add(MakeShared<FJsonValueObject>(Entry));
			if (Toolsets.Num() >= MaxResults)
			{
				break;
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("independent"), true);
		Result->SetNumberField(TEXT("toolsetCount"), Toolsets.Num());
		Result->SetBoolField(TEXT("schemasIncluded"), bIncludeSchemas);
		Result->SetArrayField(TEXT("toolsets"), MoveTemp(Toolsets));
		return SuccessJson(Result);
	}

	FString DescribeToolset(const TSharedPtr<FJsonObject>& Arguments)
	{
		const FString RequestedName = RequiredString(Arguments, TEXT("toolset"));
		if (RequestedName.IsEmpty())
		{
			return ErrorJson(TEXT("Missing required argument: toolset."));
		}

		const FDiscoveryResult Discovery = Discover();
		TArray<TSharedPtr<FJsonValue>> Tools;
		for (const FToolDescriptor& Tool : Discovery.Tools)
		{
			if (Tool.ToolsetName.Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				Tools.Add(MakeShared<FJsonValueObject>(ToolJson(Tool, true)));
			}
		}
		if (Tools.IsEmpty())
		{
			return ErrorJson(FString::Printf(TEXT("Unknown Unreal Agent toolset: %s"), *RequestedName));
		}

		TSharedRef<FJsonObject> Toolset = MakeShared<FJsonObject>();
		Toolset->SetStringField(TEXT("name"), RequestedName);
		Toolset->SetStringField(TEXT("version"), TEXT("1.0"));
		Toolset->SetArrayField(TEXT("tools"), MoveTemp(Tools));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("toolset"), Toolset);
		return SuccessJson(Result);
	}

	FString CallTool(const TSharedPtr<FJsonObject>& Arguments)
	{
		const FString ToolsetName = RequiredString(Arguments, TEXT("toolset"));
		const FString ToolName = RequiredString(Arguments, TEXT("tool"));
		if (ToolsetName.IsEmpty() || ToolName.IsEmpty())
		{
			return ErrorJson(TEXT("Missing required arguments: toolset and tool."));
		}

		TSharedPtr<FJsonObject> Input = MakeShared<FJsonObject>();
		if (Arguments.IsValid())
		{
			const TSharedPtr<FJsonObject>* InputObject = nullptr;
			if (Arguments->TryGetObjectField(TEXT("input"), InputObject) && InputObject && InputObject->IsValid())
			{
				Input = *InputObject;
			}
			else
			{
				FString InputJson;
				Arguments->TryGetStringField(TEXT("inputJson"), InputJson);
				if (!InputJson.IsEmpty())
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InputJson);
					if (!FJsonSerializer::Deserialize(Reader, Input) || !Input.IsValid())
					{
						return ErrorJson(TEXT("inputJson is not a JSON object."));
					}
				}
			}
		}

		const FDiscoveryResult Discovery = Discover();
		const FToolDescriptor* Tool = FindTool(Discovery, ToolsetName, ToolName);
		if (!Tool)
		{
			return ErrorJson(FString::Printf(TEXT("Unknown Unreal Agent reflected tool: %s.%s"), *ToolsetName, *ToolName));
		}

		bool bDefer = false;
		if (Arguments.IsValid())
		{
			Arguments->TryGetBoolField(TEXT("defer"), bDefer);
		}
		if (bDefer)
		{
			FGuid CallId;
			FString Error;
			if (!ScheduleDeferredCall(Tool->ToolsetName, Tool->ToolName, JsonObjectToString(Input.ToSharedRef()), CallId, Error))
			{
				return ErrorJson(Error);
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("pending"), true);
			Result->SetStringField(TEXT("state"), TEXT("Pending"));
			Result->SetStringField(TEXT("callId"), CallIdToString(CallId));
			Result->SetStringField(TEXT("toolset"), Tool->ToolsetName);
			Result->SetStringField(TEXT("tool"), Tool->ToolName);
			return SuccessJson(Result);
		}

		TSharedPtr<FJsonObject> Value;
		FString Error;
		if (!Reflection::Invoke(*Tool, Input, Value, Error))
		{
			return ErrorJson(Error);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("pending"), false);
		Result->SetStringField(TEXT("toolset"), Tool->ToolsetName);
		Result->SetStringField(TEXT("tool"), Tool->ToolName);
		Result->SetObjectField(TEXT("result"), Value);
		return SuccessJson(Result);
	}

	FString GetCallResult(const TSharedPtr<FJsonObject>& Arguments)
	{
		const FString CallIdText = RequiredString(Arguments, TEXT("callId"));
		if (CallIdText.IsEmpty())
		{
			return ErrorJson(TEXT("Missing required argument: callId."));
		}

		FGuid CallId;
		if (!FGuid::Parse(CallIdText, CallId) || !CallId.IsValid())
		{
			return ErrorJson(TEXT("callId is not a valid GUID."));
		}

		bool bConsume = false;
		if (Arguments.IsValid())
		{
			Arguments->TryGetBoolField(TEXT("consume"), bConsume);
		}

		FAsyncResultStore& Store = GetAsyncResultStore();
		Store.PurgeExpired();
		FAsyncResultSnapshot Snapshot;
		if (!Store.TryRead(CallId, Snapshot, bConsume))
		{
			return ErrorJson(FString::Printf(TEXT("Unknown or expired Unreal Agent callId: %s"), *CallIdText));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("callId"), CallIdToString(CallId));
		Result->SetStringField(TEXT("state"), AsyncResultStateToString(Snapshot.State));
		Result->SetBoolField(TEXT("pending"), !Snapshot.IsComplete());
		Result->SetStringField(TEXT("createdAt"), Snapshot.CreatedAt.ToIso8601());
		if (Snapshot.CompletedAt != FDateTime())
		{
			Result->SetStringField(TEXT("completedAt"), Snapshot.CompletedAt.ToIso8601());
		}
		if (Snapshot.State == EAsyncResultState::Succeeded)
		{
			const TSharedPtr<FJsonObject> Value = ParseJsonObject(Snapshot.ValueJson);
			if (!Value.IsValid())
			{
				return ErrorJson(TEXT("Stored reflected tool result is not a JSON object."));
			}
			Result->SetObjectField(TEXT("result"), Value);
		}
		else if (!Snapshot.Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), Snapshot.Error);
		}
		return SuccessJson(Result);
	}
}
