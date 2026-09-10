// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLandscapeService.cpp
 * @brief Landscape 应用服务实现，只依赖 Landscape Port 和 JSON 契约。
 */

#include "Application/Domains/Landscape/UnrealAgentMCPLandscapeService.h"

#include "Application/Ports/UnrealAgentMCPLandscapePort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPLandscapeService::FUnrealAgentMCPLandscapeService(TSharedRef<IUnrealAgentMCPLandscapePort> InLandscapePort) : LandscapePort(MoveTemp(InLandscapePort))
	{
	}

	TArray<FString> FUnrealAgentMCPLandscapeService::GetImplementedActions()
	{
		return { TEXT("get_info"), TEXT("list_layers"), TEXT("sample"), TEXT("sample_batch"), TEXT("sample_grid"), TEXT("sample_polyline"), TEXT("sculpt"), TEXT("sculpt_batch"),
			TEXT("set_height_rect"), TEXT("import_heightmap"), TEXT("reset_heights"), TEXT("paint_layer"), TEXT("list_splines"), TEXT("get_component"), TEXT("set_material"),
			TEXT("add_layer_info"), TEXT("create_layer_info"), TEXT("create"), TEXT("get_material_usage_summary"), TEXT("list_proxies"), TEXT("find_proxy_at") };
	}

	FString FUnrealAgentMCPLandscapeService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();

		if (Action == TEXT("get_info"))
			return LandscapePort->GetInfo(SafeArgs);
		if (Action == TEXT("list_layers"))
			return LandscapePort->ListLayers(SafeArgs);
		if (Action == TEXT("sample"))
			return LandscapePort->Sample(SafeArgs);
		if (Action == TEXT("sample_batch"))
			return LandscapePort->SampleBatch(SafeArgs);
		if (Action == TEXT("sample_grid"))
			return LandscapePort->SampleGrid(SafeArgs);
		if (Action == TEXT("sample_polyline"))
			return LandscapePort->SamplePolyline(SafeArgs);
		if (Action == TEXT("sculpt"))
			return LandscapePort->Sculpt(SafeArgs);
		if (Action == TEXT("sculpt_batch"))
			return ErrorJson(TEXT("sculpt_batch 必须通过可恢复任务执行器运行。"));
		if (Action == TEXT("set_height_rect") || Action == TEXT("import_heightmap") || Action == TEXT("reset_heights"))
		{
			return ErrorJson(FString::Printf(TEXT("%s 必须通过可恢复任务执行器运行。"), *Action));
		}
		if (Action == TEXT("paint_layer"))
			return LandscapePort->PaintLayer(SafeArgs);
		if (Action == TEXT("list_splines"))
			return LandscapePort->ListSplines(SafeArgs);
		if (Action == TEXT("get_component"))
			return LandscapePort->GetComponent(SafeArgs);
		if (Action == TEXT("set_material"))
			return LandscapePort->SetMaterial(SafeArgs);
		if (Action == TEXT("add_layer_info"))
			return LandscapePort->AddLayerInfo(SafeArgs);
		if (Action == TEXT("create_layer_info"))
			return LandscapePort->CreateLayerInfo(SafeArgs);
		if (Action == TEXT("create"))
			return LandscapePort->Create(SafeArgs);
		if (Action == TEXT("get_material_usage_summary"))
			return LandscapePort->GetMaterialUsageSummary(SafeArgs);
		if (Action == TEXT("list_proxies"))
			return LandscapePort->ListProxies(SafeArgs);
		if (Action == TEXT("find_proxy_at"))
			return LandscapePort->FindProxyAt(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("landscape"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Landscape action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPLandscapeService::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const
	{
		return LandscapePort->CreateTaskStepper(Args);
	}
}
