// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServerCatalog.cpp
 * @brief MCP 工具目录、资源目录与具体工具处理器适配。
 */

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"
#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolsetAdapter.h"
#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolProvider.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"
#include "Infrastructure/Audit/UnrealAgentMCPJsonlExecutionLedger.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Misc/Paths.h"
#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "Composition/Toolsets/UnrealAgentMCPBuiltInToolsets.h"
#include "Provider/UnrealAgentMCPToolExtensions.h"

using namespace UnrealAgentMCP;
using namespace UnrealAgentMCP::Execution;

namespace
{
	FMcpToolExecutionService& GetToolExecutionService();
	FMcpToolRuntimeRegistry& GetToolRuntimeRegistry();

	class FEditorMcpToolProviderHost final : public Extensions::IMcpToolProviderHost
	{
	public:
		virtual bool RegisterProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors) override
		{
			return GetToolRuntimeRegistry().RegisterProvider(Provider, OutErrors);
		}

		virtual int32 UnregisterProvider(const FName ProviderName) override
		{
			return GetToolRuntimeRegistry().UnregisterOwner(ProviderName.ToString());
		}

		virtual Extensions::FMcpToolCatalogState GetCatalogState() const override
		{
			const FMcpToolRegistrySnapshotRef Snapshot = GetToolRuntimeRegistry().GetSnapshot();
			Extensions::FMcpToolCatalogState State;
			State.Generation = Snapshot->Generation;
			State.CatalogHash = Snapshot->CatalogHash;
			State.ToolCount = Snapshot->Tools.Num();
			return State;
		}
	};

	TSharedPtr<FEditorMcpToolProviderHost, ESPMode::ThreadSafe> GToolProviderHost;

	FString GetRuntimeToolCatalogJson()
	{
		const FMcpToolRegistrySnapshotRef Snapshot = GetToolRuntimeRegistry().GetSnapshot();
		TArray<TSharedPtr<FJsonValue>> Tools;
		Tools.Reserve(Snapshot->Tools.Num());
		for (const FMcpToolDescriptor& Descriptor : Snapshot->Tools)
		{
			TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("name"), Descriptor.Name);
			Tool->SetStringField(TEXT("description"), Descriptor.Description);
			Tool->SetStringField(TEXT("qualifiedName"), Descriptor.QualifiedName);
			Tool->SetStringField(TEXT("provider"), Descriptor.Provider.ToString());
			Tool->SetStringField(TEXT("toolset"), Descriptor.Toolset);
			Tool->SetStringField(TEXT("risk"), ToolDescriptor::RiskToString(Descriptor.Risk));
			Tool->SetBoolField(TEXT("readOnly"), Descriptor.bReadOnly);
			Tool->SetBoolField(TEXT("requiresConfirmation"), Descriptor.bRequiresConfirmation);
			Tool->SetBoolField(TEXT("actionRouted"), static_cast<bool>(Descriptor.ActionContractResolver));
			Tool->SetBoolField(TEXT("resumable"), static_cast<bool>(Descriptor.StagedTaskFactory));
			Tools.Add(MakeShared<FJsonValueObject>(Tool));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("source"), TEXT("mcp_runtime_registry"));
		Result->SetStringField(TEXT("discoveryMethod"), TEXT("tools/list"));
		Result->SetStringField(TEXT("invocationMethod"), TEXT("tools/call"));
		Result->SetStringField(TEXT("reflectionCatalogRole"), TEXT("worlddata://toolsets/catalog is reflection-only and does not replace this catalog."));
		Result->SetNumberField(TEXT("generation"), static_cast<double>(Snapshot->Generation));
		Result->SetStringField(TEXT("catalogHash"), Snapshot->CatalogHash);
		Result->SetNumberField(TEXT("toolCount"), Tools.Num());
		Result->SetArrayField(TEXT("tools"), Tools);
		return JsonObjectToString(Result);
	}

	FString GetCurrentProjectInfoTool(const TSharedPtr<FJsonObject>&)
	{
		return FUnrealAgentMCPServer::GetProjectInfoJson();
	}

	FString SearchToolsTool(const TSharedPtr<FJsonObject>& Arguments)
	{
		FString Query;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("query"), Query))
		{
			return ErrorJson(TEXT("Missing required query."));
		}
		Query.TrimStartAndEndInline();
		if (Query.Len() < 2)
		{
			return ErrorJson(TEXT("query must contain at least two characters."));
		}

		int32 MaximumResults = 20;
		double RequestedMaximum = 0.0;
		if (Arguments->TryGetNumberField(TEXT("maxResults"), RequestedMaximum))
		{
			if (!FMath::IsFinite(RequestedMaximum) || RequestedMaximum != FMath::FloorToDouble(RequestedMaximum) || RequestedMaximum < 1.0 || RequestedMaximum > 100.0)
			{
				return ErrorJson(TEXT("maxResults must be an integer between 1 and 100."));
			}
			MaximumResults = static_cast<int32>(RequestedMaximum);
		}
		bool bIncludeSchema = false;
		Arguments->TryGetBoolField(TEXT("includeSchema"), bIncludeSchema);

		TArray<FString> Terms;
		Query.ParseIntoArrayWS(Terms);
		struct FScoredTool
		{
			const FMcpToolDescriptor* Descriptor = nullptr;
			int32 Score = 0;
		};
		const FMcpToolRegistrySnapshotRef Snapshot = GetToolRuntimeRegistry().GetSnapshot();
		TArray<FScoredTool> Matches;
		for (const FMcpToolDescriptor& Descriptor : Snapshot->Tools)
		{
			const FString SearchText =
				FString::Printf(TEXT("%s %s %s %s %s"), *Descriptor.Name, *Descriptor.QualifiedName, *Descriptor.Description, *Descriptor.Provider.ToString(), *Descriptor.Toolset);
			bool bMatchesEveryTerm = true;
			for (const FString& Term : Terms)
			{
				if (!SearchText.Contains(Term, ESearchCase::IgnoreCase))
				{
					bMatchesEveryTerm = false;
					break;
				}
			}
			if (!bMatchesEveryTerm)
			{
				continue;
			}
			int32 Score = 1;
			if (Descriptor.Name.Equals(Query, ESearchCase::IgnoreCase) || Descriptor.QualifiedName.Equals(Query, ESearchCase::IgnoreCase))
			{
				Score += 1000;
			}
			else if (Descriptor.Name.StartsWith(Query, ESearchCase::IgnoreCase))
			{
				Score += 500;
			}
			else if (Descriptor.Name.Contains(Query, ESearchCase::IgnoreCase))
			{
				Score += 250;
			}
			if (Descriptor.Toolset.Contains(Query, ESearchCase::IgnoreCase))
			{
				Score += 100;
			}
			Matches.Add({ &Descriptor, Score });
		}
		Matches.Sort(
			[](const FScoredTool& Left, const FScoredTool& Right)
			{
				return Left.Score != Right.Score ? Left.Score > Right.Score : Left.Descriptor->Name < Right.Descriptor->Name;
			});

		TArray<TSharedPtr<FJsonValue>> Values;
		const int32 ReturnedCount = FMath::Min(MaximumResults, Matches.Num());
		Values.Reserve(ReturnedCount);
		for (int32 Index = 0; Index < ReturnedCount; ++Index)
		{
			const FMcpToolDescriptor& Descriptor = *Matches[Index].Descriptor;
			TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
			Match->SetStringField(TEXT("name"), Descriptor.Name);
			Match->SetStringField(TEXT("qualifiedName"), Descriptor.QualifiedName);
			Match->SetStringField(TEXT("description"), Descriptor.Description);
			Match->SetStringField(TEXT("provider"), Descriptor.Provider.ToString());
			Match->SetStringField(TEXT("toolset"), Descriptor.Toolset);
			Match->SetStringField(TEXT("risk"), ToolDescriptor::RiskToString(Descriptor.Risk));
			Match->SetBoolField(TEXT("readOnly"), Descriptor.bReadOnly);
			Match->SetBoolField(TEXT("requiresConfirmation"), Descriptor.bRequiresConfirmation);
			if (bIncludeSchema)
			{
				Match->SetObjectField(TEXT("definition"), Descriptor.ToJsonObject());
			}
			Values.Add(MakeShared<FJsonValueObject>(Match));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("query"), Query);
		Result->SetNumberField(TEXT("matchedCount"), Matches.Num());
		Result->SetNumberField(TEXT("returnedCount"), ReturnedCount);
		Result->SetBoolField(TEXT("truncated"), Matches.Num() > ReturnedCount);
		Result->SetNumberField(TEXT("generation"), static_cast<double>(Snapshot->Generation));
		Result->SetStringField(TEXT("catalogHash"), Snapshot->CatalogHash);
		Result->SetArrayField(TEXT("matches"), Values);
		return JsonObjectToString(Result);
	}

	FString GetCodexPolicySnapshotTool(const TSharedPtr<FJsonObject>&)
	{
		return ServerEnvironment::GetCodexPolicySnapshotJson();
	}

	FString GetTaskTool(const TSharedPtr<FJsonObject>& Arguments)
	{
		FString TaskIdText;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("taskId"), TaskIdText))
		{
			return ErrorJson(TEXT("缺少 taskId。"));
		}
		FGuid TaskId;
		if (!FGuid::Parse(TaskIdText, TaskId))
		{
			return ErrorJson(TEXT("taskId 格式无效。"));
		}
		FMcpTaskSnapshot Snapshot;
		if (!GetToolExecutionService().TryReadTask(TaskId, Snapshot))
		{
			return ErrorJson(TEXT("没有找到指定任务。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("task"), Snapshot.ToJsonObject());
		return JsonObjectToString(Result);
	}

	bool ResolveNestedCallToolDescriptor(const TSharedPtr<FJsonObject>& Arguments, FMcpToolDescriptor& OutDescriptor, TSharedPtr<FJsonObject>& OutInput)
	{
		if (!Arguments.IsValid())
		{
			return false;
		}
		FString Toolset;
		FString Tool;
		if (!Arguments->TryGetStringField(TEXT("toolset"), Toolset) || !Arguments->TryGetStringField(TEXT("tool"), Tool) || Toolset.IsEmpty() || Tool.IsEmpty())
		{
			return false;
		}

		const FString QualifiedTool = Tool.StartsWith(Toolset + TEXT(".")) ? Tool : Toolset + TEXT(".") + Tool;
		if (QualifiedTool == TEXT("call_tool") || !GetToolRuntimeRegistry().TryGetDescriptor(QualifiedTool, OutDescriptor))
		{
			return false;
		}

		OutInput = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* InputObject = nullptr;
		if (Arguments->TryGetObjectField(TEXT("input"), InputObject) && InputObject && InputObject->IsValid())
		{
			OutInput = *InputObject;
		}
		return true;
	}

	void ConfigureCallToolDescriptor(FMcpToolDescriptor& Descriptor)
	{
		// 包装器本身并非破坏性操作；每次调用都解析内部原生描述符。
		// 参数无效或目标未知时按破坏性风险处理，保持失效关闭。
		Descriptor.bRequiresConfirmation = false;
		Descriptor.Risk = EMcpToolRisk::Destructive;
		Descriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Arguments)
		{
			FMcpToolDescriptor Nested;
			TSharedPtr<FJsonObject> NestedInput;
			if (!ResolveNestedCallToolDescriptor(Arguments, Nested, NestedInput))
			{
				return EMcpToolRisk::Destructive;
			}

			EMcpToolRisk Risk = Nested.Risk;
			FMcpResolvedToolContract Contract;
			if (Nested.ActionContractResolver && Nested.ActionContractResolver(NestedInput, Contract))
			{
				Risk = Contract.Risk;
			}
			else if (Nested.RiskResolver)
			{
				Risk = Nested.RiskResolver(NestedInput);
			}
			return Nested.bRequiresConfirmation ? EMcpToolRisk::Destructive : Risk;
		};
	}

	FString ListTasksTool(const TSharedPtr<FJsonObject>&)
	{
		TArray<TSharedPtr<FJsonValue>> TaskValues;
		for (const FMcpTaskSnapshot& Snapshot : GetToolExecutionService().ListTasks())
		{
			TaskValues.Add(MakeShared<FJsonValueObject>(Snapshot.ToJsonObject()));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("count"), TaskValues.Num());
		Result->SetArrayField(TEXT("tasks"), TaskValues);
		return JsonObjectToString(Result);
	}

	FString CancelTaskTool(const TSharedPtr<FJsonObject>& Arguments)
	{
		FString TaskIdText;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("taskId"), TaskIdText))
		{
			return ErrorJson(TEXT("缺少 taskId。"));
		}
		FGuid TaskId;
		if (!FGuid::Parse(TaskIdText, TaskId))
		{
			return ErrorJson(TEXT("taskId 格式无效。"));
		}
		FString Reason;
		Arguments->TryGetStringField(TEXT("reason"), Reason);
		const EMcpTaskCancelResult CancelResult = GetToolExecutionService().CancelTask(TaskId, MoveTemp(Reason));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), CancelResult != EMcpTaskCancelResult::NotFound && CancelResult != EMcpTaskCancelResult::NotCancelable);
		Result->SetStringField(TEXT("result"), CancelResultToString(CancelResult));
		return JsonObjectToString(Result);
	}

	FString ListToolAuditTool(const TSharedPtr<FJsonObject>& Arguments)
	{
		double RequestedMaximum = 200.0;
		if (Arguments.IsValid())
		{
			Arguments->TryGetNumberField(TEXT("maxResults"), RequestedMaximum);
		}
		const int32 Maximum = FMath::Clamp(static_cast<int32>(RequestedMaximum), 1, 1000);
		TArray<TSharedPtr<FJsonValue>> RecordValues;
		for (const Policy::FMcpAuditRecord& Record : GetToolExecutionService().ListAudit(Maximum))
		{
			TSharedRef<FJsonObject> RecordObject = MakeShared<FJsonObject>();
			RecordObject->SetStringField(TEXT("traceId"), Record.TraceId.ToString(EGuidFormats::DigitsWithHyphensLower));
			RecordObject->SetStringField(TEXT("requestId"), Record.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
			RecordObject->SetStringField(TEXT("timestamp"), Record.Timestamp.ToIso8601());
			RecordObject->SetStringField(TEXT("clientId"), Record.ClientId);
			RecordObject->SetStringField(TEXT("sessionId"), Record.SessionId);
			RecordObject->SetStringField(TEXT("source"), Record.Source);
			RecordObject->SetStringField(TEXT("tool"), Record.ToolName);
			RecordObject->SetStringField(TEXT("risk"), ToolDescriptor::RiskToString(Record.Risk));
			RecordObject->SetStringField(TEXT("outcome"), Policy::PolicyOutcomeToString(Record.Outcome));
			RecordObject->SetStringField(TEXT("decisionCode"), Record.DecisionCode);
			RecordObject->SetBoolField(TEXT("accepted"), Record.bAccepted);
			RecordValues.Add(MakeShared<FJsonValueObject>(RecordObject));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		FString LedgerError;
		const bool bIsLedgerHealthy = GetToolExecutionService().IsExecutionLedgerHealthy(LedgerError);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("durable"), true);
		Result->SetBoolField(TEXT("ledgerHealthy"), bIsLedgerHealthy);
		Result->SetStringField(TEXT("ledgerLocation"), GetToolExecutionService().GetExecutionLedgerLocation());
		if (!LedgerError.IsEmpty())
		{
			Result->SetStringField(TEXT("ledgerError"), LedgerError);
		}
		Result->SetNumberField(TEXT("count"), RecordValues.Num());
		Result->SetArrayField(TEXT("records"), RecordValues);
		return JsonObjectToString(Result);
	}

	const FMcpToolRegistration GCoreToolRegistrations[] = { { TEXT("get_current_project_info"), &GetCurrentProjectInfoTool }, { TEXT("search_tools"), &SearchToolsTool },
		{ TEXT("list_level_actors"), &Tools::ListLevelActors }, { TEXT("get_selected_actors"), &Tools::GetSelectedActors }, { TEXT("get_actor_details"), &Tools::GetActorDetails },
		{ TEXT("find_assets"), &Tools::FindAssets }, { TEXT("get_content_summary"), &Tools::GetContentSummary }, { TEXT("read_asset"), &Tools::ReadAsset },
		{ TEXT("select_actor"), &Tools::SelectActor }, { TEXT("spawn_actor"), &Tools::SpawnActor }, { TEXT("rename_actor"), &Tools::RenameActor },
		{ TEXT("transform_actor"), &Tools::TransformActor }, { TEXT("delete_actor"), &Tools::DeleteActor }, { TEXT("attach_actor"), &Tools::AttachActor },
		{ TEXT("set_actor_property"), &Tools::SetActorProperty }, { TEXT("save_current_level"), &Tools::SaveCurrentLevel }, { TEXT("create_asset"), &Tools::CreateAsset },
		{ TEXT("create_blueprint_asset"), &Tools::CreateBlueprintAsset }, { TEXT("modify_material_instance"), &Tools::ModifyMaterialInstance },
		{ TEXT("create_pcg_graph_from_recipe"), &Tools::CreatePcgGraphFromRecipe }, { TEXT("get_toolset_status"), &Toolsets::GetStatus },
		{ TEXT("list_toolsets"), &Toolsets::ListToolsets }, { TEXT("describe_toolset"), &Toolsets::DescribeToolset },
		{ TEXT("call_tool"), &Toolsets::CallTool, &ConfigureCallToolDescriptor }, { TEXT("get_toolset_call_result"), &Toolsets::GetCallResult },
		{ TEXT("get_codex_policy_snapshot"), &GetCodexPolicySnapshotTool }, { TEXT("get_task"), &GetTaskTool }, { TEXT("list_tasks"), &ListTasksTool },
		{ TEXT("cancel_task"), &CancelTaskTool }, { TEXT("list_tool_audit"), &ListToolAuditTool } };

	TConstArrayView<FMcpToolRegistration> GetCoreToolRegistrations()
	{
		return TConstArrayView<FMcpToolRegistration>(GCoreToolRegistrations, UE_ARRAY_COUNT(GCoreToolRegistrations));
	}
}

namespace
{
	FString GetCoreToolDefinitionsJson()
	{
		// MSVC 会截断超大的单个宽字符串字面量（C2026）。每段保持在约
		// 5,000 字符以内并在运行时拼接，新增工具时也必须继续分段。
		static const FString LocalToolsJson = FString(TEXT(R"JSON([
{"name":"get_current_project_info","description":"Return the UE project identity and MCP endpoint for this editor session.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Get Project Info","readOnlyHint":true,"openWorldHint":false}},
{"name":"search_tools","description":"Search the live MCP runtime catalog without loading every schema into the model context. Returns compact ranked matches; includeSchema expands only the selected matches.","inputSchema":{"type":"object","properties":{"query":{"type":"string","minLength":2,"description":"Case-insensitive terms matched against tool name, qualified name, description, provider, and toolset."},"maxResults":{"type":"integer","minimum":1,"maximum":100,"default":20},"includeSchema":{"type":"boolean","default":false,"description":"Include the complete callable definition only for returned matches."}},"required":["query"],"additionalProperties":false},"annotations":{"title":"Search Runtime Tools","readOnlyHint":true,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"list_level_actors","description":"List actors in the currently loaded editor world with transforms, folder, mobility, and selection state.","inputSchema":{"type":"object","properties":{"classFilter":{"type":"string","description":"Optional case-insensitive class-name substring."},"nameContains":{"type":"string","description":"Optional case-insensitive actor name or label substring."},"selectedOnly":{"type":"boolean","description":"When true, only return currently selected actors."},"maxResults":{"type":"number","description":"Maximum returned actors. Default 200, capped at 1000."}}},"annotations":{"title":"List Level Actors","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_selected_actors","description":"Return the actors currently selected in the editor viewport/outliner.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Get Selected Actors","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_actor_details","description":"Read one actor in depth: transform, tags, and its components with classes and relative transforms.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Actor name or editor label."},"label":{"type":"string","description":"Alias for name."}},"required":["name"]},"annotations":{"title":"Get Actor Details","readOnlyHint":true,"openWorldHint":false}},
{"name":"find_assets","description":"Search assets under a content path without loading them.","inputSchema":{"type":"object","properties":{"searchTerm":{"type":"string","description":"Optional case-insensitive asset name or path substring."},"classFilter":{"type":"string","description":"Optional class-name substring such as StaticMesh, Blueprint, World, Material."},"path":{"type":"string","description":"Content root to search. Default /Game."},"maxResults":{"type":"number","description":"Maximum returned assets. Default 50, capped at 500."}}},"annotations":{"title":"Find Assets","readOnlyHint":true,"openWorldHint":false}},
{"name":"read_asset","description":"Read basic Asset Registry metadata for one asset.","inputSchema":{"type":"object","properties":{"assetPath":{"type":"string","description":"Asset object path or package path, for example /Game/Foo/Bar.Bar or /Game/Foo/Bar."}},"required":["assetPath"]},"annotations":{"title":"Read Asset","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_content_summary","description":"Summarize the Asset Registry under a content path: total asset count and a histogram of asset counts by class.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Content root to summarize. Default /Game."},"maxClasses":{"type":"number","description":"Maximum class buckets returned. Default 30, capped at 200."}}},"annotations":{"title":"Get Content Summary","readOnlyHint":true,"openWorldHint":false}},
{"name":"select_actor","description":"Select an actor in the editor by name or label.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Actor name or editor label."},"label":{"type":"string","description":"Alias for name."}}},"annotations":{"title":"Select Actor","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"spawn_actor","description":"Spawn an actor into the current editor world. Use staticMeshPath to create a StaticMeshActor. Selection is opt-in to avoid rebuilding Editor selection state during bulk generation.","inputSchema":{"type":"object","properties":{"class":{"type":"string","description":"Actor class name/path. Default Actor. Blueprint asset paths are supported."},"staticMeshPath":{"type":"string","description":"Optional StaticMesh asset path. When supplied, spawns a StaticMeshActor."},"label":{"type":"string","description":"Optional editor label."},"location":{"type":"object","description":"Object {x,y,z} or array [x,y,z]."},"rotation":{"type":"object","description":"Object {pitch,yaw,roll} or array [pitch,yaw,roll]."},"scale":{"type":"object","description":"Object {x,y,z} or array [x,y,z]."},"select":{"type":"boolean","default":false,"description":"Select the spawned actor. Keep false for bulk generation."}}},"annotations":{"title":"Spawn Actor","readOnlyHint":false,"destructiveHint":false,"idempotentHint":false,"openWorldHint":false}},
)JSON")) + TEXT(R"JSON(
{"name":"rename_actor","description":"Rename one editor actor and read the label back in the same transaction. Pass name (object name or current label), or omit it only when exactly one actor is selected. Params: name?, newLabel.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Current actor object name or label. Optional only with exactly one selected actor."},"newLabel":{"type":"string","description":"Required new editor label; duplicate labels are rejected."}},"required":["newLabel"]},"annotations":{"title":"Rename Actor","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"transform_actor","description":"Set an actor world transform by name or label.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Actor name or editor label."},"label":{"type":"string","description":"Alias for name."},"location":{"type":"object","description":"Object {x,y,z} or array [x,y,z]."},"rotation":{"type":"object","description":"Object {pitch,yaw,roll} or array [pitch,yaw,roll]."},"scale":{"type":"object","description":"Object {x,y,z} or array [x,y,z]."}},"required":["name"]},"annotations":{"title":"Transform Actor","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"delete_actor","description":"Delete an actor from the current editor world by name or label.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Actor name or editor label."},"label":{"type":"string","description":"Alias for name."}},"required":["name"]},"annotations":{"title":"Delete Actor","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"attach_actor","description":"Attach one actor to another in the editor world.","inputSchema":{"type":"object","properties":{"child":{"type":"string","description":"Child actor name or label."},"childName":{"type":"string"},"parent":{"type":"string","description":"Parent actor name or label."},"parentName":{"type":"string"},"socket":{"type":"string"},"keepWorldTransform":{"type":"boolean","description":"Default true."}},"required":["child","parent"]},"annotations":{"title":"Attach Actor","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"set_actor_property","description":"Set an editable actor or component property using reflection. Supports primitive values, enum names, FVector, FRotator, FLinearColor, and UObject asset references.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Actor name or editor label."},"label":{"type":"string","description":"Alias for name."},"component":{"type":"string","description":"Optional component name or class name."},"property":{"type":"string","description":"Property name."},"value":{"description":"JSON value compatible with the property type."}},"required":["name","property","value"]},"annotations":{"title":"Set Actor Property","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"save_current_level","description":"Save the current level package to disk.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Save Current Level","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"create_asset","description":"Create a UObject asset under /Game with the supplied class. Useful for simple data assets and reflected asset classes such as PCGGraph.","inputSchema":{"type":"object","properties":{"assetPath":{"type":"string","description":"Long package path under /Game, for example /Game/MCP/NewAsset."},"path":{"type":"string","description":"Alias for assetPath."},"class":{"type":"string","description":"UObject class name or path. Default DataAsset."},"save":{"type":"boolean","description":"When true, save the created asset package to disk."}},"required":["assetPath"]},"annotations":{"title":"Create Asset","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"create_blueprint_asset","description":"Create a Blueprint asset under /Game using a reflected parent class. Graph node construction is not exposed yet.","inputSchema":{"type":"object","properties":{"assetPath":{"type":"string","description":"Long package path under /Game."},"path":{"type":"string","description":"Alias for assetPath."},"parentClass":{"type":"string","description":"Parent class name/path. Default Actor."},"save":{"type":"boolean","description":"When true, save the created asset package to disk."}},"required":["assetPath"]},"annotations":{"title":"Create Blueprint Asset","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"modify_material_instance","description":"Modify scalar and vector parameters on a MaterialInstanceConstant asset.","inputSchema":{"type":"object","properties":{"assetPath":{"type":"string","description":"MaterialInstanceConstant object path or package path."},"path":{"type":"string","description":"Alias for assetPath."},"scalarParameters":{"type":"object","additionalProperties":{"type":"number"}},"vectorParameters":{"type":"object","description":"Map of parameter name to {r,g,b,a} object or [r,g,b,a] array."},"save":{"type":"boolean","description":"When true, save the modified asset package to disk."}},"required":["assetPath"]},"annotations":{"title":"Modify Material Instance","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
)JSON") + TEXT(R"JSON(
{"name":"create_pcg_graph_from_recipe","description":"Create a PCGGraph asset under /Game from recipe metadata. If sourceGraph/source_graph is supplied, duplicates that existing PCGGraph including nodes and edges; otherwise creates an empty PCGGraph container and reports recipeApplied=false.","inputSchema":{"type":"object","properties":{"assetPath":{"type":"string","description":"PCGGraph package path under /Game."},"path":{"type":"string","description":"Alias for assetPath."},"recipe_id":{"type":"string"},"id":{"type":"string"},"sourceGraph":{"type":"string","description":"Optional source PCGGraph asset path to duplicate."},"source_graph":{"type":"string","description":"Alias for sourceGraph."},"recipe":{"type":"object","description":"Optional recipe object containing recipe_id/id and source_graph."},"save":{"type":"boolean","description":"When true, save the created graph asset package to disk."}},"required":["assetPath"]},"annotations":{"title":"Create PCG Graph From Recipe","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"get_toolset_status","description":"Report the independent Unreal Agent reflection registry status and discoverable tool counts.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Unreal Agent Toolset Status","readOnlyHint":true,"openWorldHint":false}},
{"name":"list_toolsets","description":"List independent Unreal Agent reflected toolsets. By default returns compact tool records; includeSchemas returns generated input and output schemas.","inputSchema":{"type":"object","properties":{"nameFilter":{"type":"string","description":"Optional case-insensitive toolset-name substring."},"includeSchemas":{"type":"boolean","description":"Include generated input/output schemas."},"maxResults":{"type":"number","description":"Maximum toolsets. Default 100, capped at 500."}}},"annotations":{"title":"List Unreal Agent Toolsets","readOnlyHint":true,"openWorldHint":false}},
{"name":"describe_toolset","description":"Return the generated schema for one independent Unreal Agent reflected toolset.","inputSchema":{"type":"object","properties":{"toolset":{"type":"string","description":"Qualified toolset name returned by list_toolsets."}},"required":["toolset"]},"annotations":{"title":"Describe Unreal Agent Toolset","readOnlyHint":true,"openWorldHint":false}},
{"name":"call_tool","description":"Execute one independent Unreal Agent reflected C++ tool in-process. Set defer=true to stage execution on the next game-thread tick and receive a callId.","inputSchema":{"type":"object","properties":{"toolset":{"type":"string","description":"Qualified toolset name."},"tool":{"type":"string","description":"Short or fully qualified tool name."},"input":{"type":"object","description":"Arguments matching the generated input schema."},"inputJson":{"type":"string","description":"Raw JSON object alternative to input."},"defer":{"type":"boolean","description":"Stage execution on the next game-thread tick and return a pollable callId. Default false."}},"required":["toolset","tool"]},"annotations":{"title":"Call Unreal Agent Tool","readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
{"name":"get_toolset_call_result","description":"Poll a deferred Unreal Agent reflected call by callId.","inputSchema":{"type":"object","properties":{"callId":{"type":"string","description":"Opaque callId returned by call_tool with defer=true."},"consume":{"type":"boolean","description":"Remove a terminal result after this read. Default false."}},"required":["callId"]},"annotations":{"title":"Get Unreal Agent Tool Result","readOnlyHint":true,"openWorldHint":false}},
)JSON") + TEXT(R"JSON(
{"name":"get_codex_policy_snapshot","description":"Read a redacted snapshot of explicit local Codex config policy such as approval_policy, sandbox_mode, active profile, model, and MCP server blocks. Does not expose hidden prompts or env secrets.","inputSchema":{"type":"object","properties":{}},"annotations":{"title":"Codex Policy Snapshot","readOnlyHint":true,"openWorldHint":false}},
{"name":"get_task","description":"读取 Unreal Agent 统一执行内核中的任务状态、进度与结果。","inputSchema":{"type":"object","properties":{"taskId":{"type":"string"}},"required":["taskId"],"additionalProperties":false},"annotations":{"title":"读取任务","readOnlyHint":true,"openWorldHint":false}},
{"name":"list_tasks","description":"列出 Unreal Agent 统一执行内核中仍在保留期内的任务。","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"title":"列出任务","readOnlyHint":true,"openWorldHint":false}},
{"name":"cancel_task","description":"请求取消一个可取消任务；未开始任务会保证取消，运行中任务在安全点结束。","inputSchema":{"type":"object","properties":{"taskId":{"type":"string"},"reason":{"type":"string"}},"required":["taskId"],"additionalProperties":false},"annotations":{"title":"取消任务","readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
{"name":"list_tool_audit","description":"读取服务端统一策略层产生的脱敏工具调用审计；不包含原始参数、Token 或密钥。","inputSchema":{"type":"object","properties":{"maxResults":{"type":"integer","minimum":1,"maximum":1000}},"additionalProperties":false},"annotations":{"title":"工具调用审计","readOnlyHint":true,"openWorldHint":false}}
])JSON");
		return LocalToolsJson;
	}

	struct FToolRuntimeRegistryBootstrap
	{
		FMcpToolRuntimeRegistry Registry;

		FToolRuntimeRegistryBootstrap()
		{
			TArray<FString> Errors;
			Registry.RegisterCatalog(TEXT("Composition.CoreTools"), GetCoreToolDefinitionsJson(), GetCoreToolRegistrations(), Errors);
			ExtractedTools::RegisterTools(Registry, Errors);
			BuiltInToolsets::Register(Registry, Errors);
			Registry.RegisterProvider(MakeShared<FUnrealAgentMCPReflectedToolProvider>(), Errors);
			Registry.OnChanged().AddLambda(
				[](const FMcpToolRegistrySnapshotRef&)
				{
					FUnrealAgentMCPServer::ScheduleToolsListChangedBroadcast();
				});
			if (!Errors.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("Unreal Agent runtime tool registry bootstrap failed: %s"), *FString::Join(Errors, TEXT(" | ")));
			}
		}
	};

	FMcpToolRuntimeRegistry& GetToolRuntimeRegistry()
	{
		static FToolRuntimeRegistryBootstrap Bootstrap;
		return Bootstrap.Registry;
	}

	FMcpToolExecutionService& GetToolExecutionService()
	{
		static const Policy::FMcpServerPolicy ServerPolicy = Policy::FMcpServerPolicy::LocalProject(FPaths::ProjectDir());
		static FMcpToolExecutionService Service(GetToolRuntimeRegistry(), FTimespan::FromMinutes(10), ServerPolicy, Transactions::CreateEditorTransactionCoordinator(),
			Audit::CreateJsonlExecutionLedger());
		return Service;
	}
}

FString FUnrealAgentMCPServer::GetToolDefinitionsJson()
{
	return GetToolRuntimeRegistry().GetToolDefinitionsJson();
}

TArray<FMcpToolDescriptor> FUnrealAgentMCPServer::GetToolDescriptors()
{
	return GetToolRuntimeRegistry().GetDescriptors();
}

bool FUnrealAgentMCPServer::TryExecuteToolInProcess(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, const FMcpToolExecutionOptions& Options,
	FString& OutResultJson)
{
	return GetToolExecutionService().Execute(ToolName, Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>(), OutResultJson, Options);
}

UnrealAgentMCP::Execution::FMcpToolInvocationResult FUnrealAgentMCPServer::ExecuteToolInProcessTyped(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolExecutionOptions& Options)
{
	return GetToolExecutionService().ExecuteTyped(ToolName, Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>(), Options);
}

bool FUnrealAgentMCPServer::IsExecutionLedgerHealthy(FString& OutError)
{
	return GetToolExecutionService().IsExecutionLedgerHealthy(OutError);
}

bool FUnrealAgentMCPServer::TryReadToolTask(const FGuid& TaskId, FMcpTaskSnapshot& OutSnapshot, const bool bConsumeTerminal)
{
	return GetToolExecutionService().TryReadTask(TaskId, OutSnapshot, bConsumeTerminal);
}

UnrealAgentMCP::Execution::EMcpTaskCancelResult FUnrealAgentMCPServer::CancelToolTask(const FGuid& TaskId, FString Reason)
{
	return GetToolExecutionService().CancelTask(TaskId, MoveTemp(Reason));
}

int32 FUnrealAgentMCPServer::CancelToolTasksByClientId(const FString& ClientId, FString Reason)
{
	return GetToolExecutionService().CancelTasksByClientId(ClientId, MoveTemp(Reason));
}

void FUnrealAgentMCPServer::ShutdownExecution()
{
	const bool bWasDrained = GetToolExecutionService().ShutdownAndWait(TEXT("UnrealAgent Editor module is shutting down."), FTimespan::FromSeconds(30));
	ensureAlwaysMsgf(bWasDrained, TEXT("UnrealAgent execution did not drain before module shutdown."));
}

bool FUnrealAgentMCPServer::DescribeToolApprovalRequest(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FUnrealAgentMCPToolApprovalRequest& OutRequest)
{
	FString EffectiveToolName = ToolName;
	TSharedPtr<FJsonObject> EffectiveArguments = Arguments;
	if (ToolName == TEXT("call_tool") && Arguments.IsValid())
	{
		FString Toolset;
		FString Tool;
		if (Arguments->TryGetStringField(TEXT("toolset"), Toolset) && Arguments->TryGetStringField(TEXT("tool"), Tool) && !Toolset.IsEmpty() && !Tool.IsEmpty())
		{
			EffectiveToolName = Tool.StartsWith(Toolset + TEXT(".")) ? Tool : Toolset + TEXT(".") + Tool;
			const TSharedPtr<FJsonObject>* NestedInput = nullptr;
			if (Arguments->TryGetObjectField(TEXT("input"), NestedInput) && NestedInput && NestedInput->IsValid())
			{
				EffectiveArguments = *NestedInput;
			}
		}
	}

	FMcpToolDescriptor Descriptor;
	if (EffectiveToolName.IsEmpty() || !GetToolRuntimeRegistry().TryGetDescriptor(EffectiveToolName, Descriptor))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> SafeArguments = EffectiveArguments.IsValid() ? EffectiveArguments : MakeShared<FJsonObject>();

	// 与策略引擎保持同一条风险解析顺序，避免审批界面低估实际风险。
	EMcpToolRisk Risk = Descriptor.Risk;
	FMcpResolvedToolContract ResolvedContract;
	if (Descriptor.ActionContractResolver && Descriptor.ActionContractResolver(SafeArguments, ResolvedContract))
	{
		Risk = ResolvedContract.Risk;
	}
	else if (Descriptor.RiskResolver)
	{
		Risk = Descriptor.RiskResolver(SafeArguments);
	}

	OutRequest = FUnrealAgentMCPToolApprovalRequest();
	OutRequest.ToolName = EffectiveToolName;
	OutRequest.DisplayToolName = EffectiveToolName;
	OutRequest.Description = Descriptor.Description;
	OutRequest.Risk = ToolDescriptor::RiskToString(Risk);
	OutRequest.ConfirmationArgument = Descriptor.ConfirmationArgument;
	OutRequest.ConfirmationValue = Descriptor.ConfirmationValue;
	OutRequest.bReadOnly = Risk == EMcpToolRisk::ReadOnly;
	OutRequest.bHighRisk = Risk == EMcpToolRisk::FileMutation || Risk == EMcpToolRisk::Destructive || Risk == EMcpToolRisk::CodeExecution || Risk == EMcpToolRisk::ExternalProcess;
	OutRequest.bRequiresConfirmation = Descriptor.bRequiresConfirmation || GetToolExecutionService().GetPolicy().ConfirmationRisks.Contains(Risk);

	FString Operation;
	if ((!SafeArguments->TryGetStringField(TEXT("action"), Operation) || Operation.IsEmpty()) &&
		(!SafeArguments->TryGetStringField(TEXT("tool"), Operation) || Operation.IsEmpty()))
	{
		SafeArguments->TryGetStringField(TEXT("name"), Operation);
	}
	if (!Operation.IsEmpty() && !Operation.Equals(EffectiveToolName, ESearchCase::IgnoreCase))
	{
		OutRequest.DisplayToolName += TEXT(".") + Operation;
	}
	return true;
}

TArray<FString> FUnrealAgentMCPServer::GetRegisteredToolHandlerNames()
{
	return GetToolRuntimeRegistry().GetRegisteredToolNames();
}

void FUnrealAgentMCPServer::BindToolProviderHost()
{
	if (!GToolProviderHost.IsValid())
	{
		GToolProviderHost = MakeShared<FEditorMcpToolProviderHost, ESPMode::ThreadSafe>();
	}
	Extensions::BindToolProviderHost(GToolProviderHost.ToSharedRef());
}

void FUnrealAgentMCPServer::UnbindToolProviderHost()
{
	Extensions::UnbindToolProviderHost(GToolProviderHost.Get());
	GToolProviderHost.Reset();
}

FString FUnrealAgentMCPServer::GetResourceListJson()
{
	TArray<TSharedPtr<FJsonValue>> Resources;
	auto AddResource = [&Resources](const FString& Uri, const FString& Name, const FString& Description)
	{
		TSharedRef<FJsonObject> Resource = MakeShared<FJsonObject>();
		Resource->SetStringField(TEXT("uri"), Uri);
		Resource->SetStringField(TEXT("name"), Name);
		Resource->SetStringField(TEXT("description"), Description);
		Resource->SetStringField(TEXT("mimeType"), TEXT("application/json"));
		Resources.Add(MakeShared<FJsonValueObject>(Resource));
	};

	AddResource(TEXT("worlddata://context/bootstrap"), TEXT("Bootstrap Context"), TEXT("Recommended first-read order and compact editor state."));
	AddResource(TEXT("worlddata://project/info"), TEXT("Project Info"), TEXT("Project identity, paths, MCP endpoint, and process details."));
	AddResource(TEXT("worlddata://codex/policy-snapshot"), TEXT("Codex Policy Snapshot"), TEXT("Redacted local Codex config policy and MCP server configuration."));
	AddResource(TEXT("worlddata://level/actors"), TEXT("Level Actors"), TEXT("Current editor-world actors, labels, classes, and transforms."));
	AddResource(TEXT("worlddata://editor/selection"), TEXT("Editor Selection"), TEXT("Actors currently selected in the editor."));
	AddResource(TEXT("worlddata://content/assets"), TEXT("Content Assets"), TEXT("Compact asset registry survey under /Game."));
	AddResource(TEXT("worlddata://content/summary"), TEXT("Content Summary"), TEXT("Asset counts by class under /Game."));
	AddResource(TEXT("worlddata://tools/catalog"), TEXT("MCP Runtime Tool Catalog"),
		TEXT("Complete compact catalog of top-level MCP tools from the same runtime registry used by tools/list."));
	AddResource(TEXT("worlddata://toolsets/catalog"), TEXT("Reflected C++ Toolset Catalog"),
		TEXT("Reflection-only C++ toolsets; this is not the complete MCP tools/list catalog."));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("recommendedFirstRead"), TEXT("worlddata://context/bootstrap"));
	Result->SetArrayField(TEXT("resources"), Resources);
	return JsonObjectToString(Result);
}

FString FUnrealAgentMCPServer::ReadResource(const FString& Uri)
{
	if (Uri == TEXT("worlddata://context/bootstrap"))
	{
		return Tools::GetBootstrapContextJson();
	}
	if (Uri == TEXT("worlddata://project/info"))
	{
		return GetProjectInfoJson();
	}
	if (Uri == TEXT("worlddata://codex/policy-snapshot"))
	{
		return ServerEnvironment::GetCodexPolicySnapshotJson();
	}
	if (Uri == TEXT("worlddata://level/actors"))
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetNumberField(TEXT("maxResults"), 300);
		return Tools::ListLevelActors(Arguments);
	}
	if (Uri == TEXT("worlddata://editor/selection"))
	{
		return Tools::GetSelectedActors(MakeShared<FJsonObject>());
	}
	if (Uri == TEXT("worlddata://content/assets"))
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("path"), TEXT("/Game"));
		Arguments->SetNumberField(TEXT("maxResults"), 300);
		return Tools::FindAssets(Arguments);
	}
	if (Uri == TEXT("worlddata://content/summary"))
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("path"), TEXT("/Game"));
		return Tools::GetContentSummary(Arguments);
	}
	if (Uri == TEXT("worlddata://tools/catalog"))
	{
		return GetRuntimeToolCatalogJson();
	}
	if (Uri == TEXT("worlddata://toolsets/catalog"))
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetBoolField(TEXT("includeSchemas"), false);
		Arguments->SetNumberField(TEXT("maxResults"), 200);
		return Toolsets::ListToolsets(Arguments);
	}

	return ErrorJson(FString::Printf(TEXT("Unknown resource: %s"), *Uri));
}

FString FUnrealAgentMCPServer::DispatchTool(const FString& ToolName, const FString& ArgumentsJson, const FMcpToolExecutionOptions& Options)
{
	const TSharedPtr<FJsonObject> Arguments = ServerEnvironment::ParseJsonObject(ArgumentsJson);
	if (!Arguments.IsValid())
	{
		return ErrorJson(TEXT("Invalid arguments JSON."));
	}

	FString ToolResult;
	if (GetToolExecutionService().Execute(ToolName, Arguments, ToolResult, Options))
	{
		return ToolResult;
	}
	return ErrorJson(FString::Printf(TEXT("Unknown Unreal Agent tool: %s"), *ToolName));
}
