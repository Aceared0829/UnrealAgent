// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPDomainCompatibility.h
 * @brief 外部分类型工具入口到 Unreal Agent 自有业务实现的兼容适配器。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPAssetService;
	class FUnrealAgentMCPEditorService;
	class FUnrealAgentMCPLevelService;
	class FUnrealAgentMCPProjectService;
	class FUnrealAgentMCPReflectionService;
	class FUnrealAgentMCPFoliageService;
	class FUnrealAgentMCPNetworkingService;
	class FUnrealAgentMCPUnrealPluginsAdapter;
	class FUnrealAgentMCPUnrealFeedbackAdapter;
	class FUnrealAgentMCPEpicService;
	class FUnrealAgentMCPChooserService;
	class FUnrealAgentMCPFabService;
	class FUnrealAgentMCPLandscapeService;
	class FUnrealAgentMCPWhiteboxService;
	class FUnrealAgentMCPPCGService;
	class FUnrealAgentMCPGASService;
	class FUnrealAgentMCPUnrealMaterialAdapter;
	class FUnrealAgentMCPUnrealWidgetAdapter;
	class FUnrealAgentMCPUnrealNiagaraAdapter;
	class FUnrealAgentMCPUnrealStateTreeAdapter;
	class FUnrealAgentMCPUnrealAudioAdapter;
	class FUnrealAgentMCPBlueprintService;
	class FUnrealAgentMCPGameplayService;
	class FUnrealAgentMCPAnimationService;
	class FUnrealAgentMCPUnrealDemoAdapter;
	class FMcpToolRuntimeRegistry;
	namespace ActionContracts
	{
		class FManifest;
	}

	namespace DomainCompatibility
	{
		/** 注册已迁移完成或正在扩展的独立分类工具。 */
		bool RegisterTools(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors, TSharedRef<const ActionContracts::FManifest> ActionManifest,
			TSharedRef<FUnrealAgentMCPAssetService> AssetService, TSharedRef<FUnrealAgentMCPEditorService> EditorService, TSharedRef<FUnrealAgentMCPLevelService> LevelService,
			TSharedRef<FUnrealAgentMCPProjectService> ProjectService, TSharedRef<FUnrealAgentMCPReflectionService> ReflectionService,
			TSharedRef<FUnrealAgentMCPFoliageService> FoliageService, TSharedRef<FUnrealAgentMCPNetworkingService> NetworkingService,
			TSharedRef<FUnrealAgentMCPUnrealPluginsAdapter> PluginsAdapter, TSharedRef<FUnrealAgentMCPUnrealFeedbackAdapter> FeedbackAdapter,
			TSharedRef<FUnrealAgentMCPEpicService> EpicService, TSharedRef<FUnrealAgentMCPChooserService> ChooserService, TSharedRef<FUnrealAgentMCPFabService> FabService,
			TSharedRef<FUnrealAgentMCPLandscapeService> LandscapeService, TSharedRef<FUnrealAgentMCPWhiteboxService> WhiteboxService,
			TSharedRef<FUnrealAgentMCPPCGService> PCGService, TSharedRef<FUnrealAgentMCPGASService> GASService, TSharedRef<FUnrealAgentMCPUnrealMaterialAdapter> MaterialAdapter,
			TSharedRef<FUnrealAgentMCPUnrealWidgetAdapter> WidgetAdapter, TSharedRef<FUnrealAgentMCPUnrealNiagaraAdapter> NiagaraAdapter,
			TSharedRef<FUnrealAgentMCPUnrealStateTreeAdapter> StateTreeAdapter, TSharedRef<FUnrealAgentMCPUnrealAudioAdapter> AudioAdapter,
			TSharedRef<FUnrealAgentMCPBlueprintService> BlueprintService, TSharedRef<FUnrealAgentMCPGameplayService> GameplayService,
			TSharedRef<FUnrealAgentMCPAnimationService> AnimationService, TSharedRef<FUnrealAgentMCPUnrealDemoAdapter> DemoAdapter);

		/** 返回分类工具的 MCP Schema 目录。 */
		FString GetToolDefinitionsJson();

	}
}
