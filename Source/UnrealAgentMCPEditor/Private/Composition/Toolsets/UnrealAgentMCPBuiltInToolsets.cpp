// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPBuiltInToolsets.cpp
 * @brief Unreal Agent 内置领域 Toolset 的 C++ 组合实现。
 */

#include "Composition/Toolsets/UnrealAgentMCPBuiltInToolsets.h"

#include "Adapters/Tooling/Compatibility/UnrealAgentMCPDomainCompatibility.h"
#include "Application/Actions/UnrealAgentMCPActionManifest.h"
#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"
#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"
#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.h"
#include "Adapters/Unreal/Demo/UnrealAgentMCPUnrealDemoAdapter.h"
#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"
#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"
#include "Adapters/Unreal/Chooser/UnrealAgentMCPUnrealChooserAdapter.h"
#include "Adapters/Unreal/Epic/UnrealAgentMCPUnrealEpicAdapter.h"
#include "Adapters/Unreal/Fab/UnrealAgentMCPUnrealFabAdapter.h"
#include "Adapters/Unreal/Feedback/UnrealAgentMCPUnrealFeedbackAdapter.h"
#include "Adapters/Unreal/Foliage/UnrealAgentMCPUnrealFoliageAdapter.h"
#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"
#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"
#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"
#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"
#include "Adapters/Unreal/Networking/UnrealAgentMCPUnrealNetworkingAdapter.h"
#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"
#include "Adapters/Unreal/Plugins/UnrealAgentMCPUnrealPluginsAdapter.h"
#include "Adapters/Unreal/Project/UnrealAgentMCPUnrealProjectAdapter.h"
#include "Adapters/Unreal/Reflection/UnrealAgentMCPUnrealReflectionAdapter.h"
#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"
#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"
#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"
#include "Adapters/Unreal/Whitebox/UnrealAgentMCPUnrealWhiteboxAdapter.h"
#include "Application/Domains/Chooser/UnrealAgentMCPChooserService.h"
#include "Application/Domains/Asset/UnrealAgentMCPAssetService.h"
#include "Application/Domains/Animation/UnrealAgentMCPAnimationService.h"
#include "Application/Domains/Blueprint/UnrealAgentMCPBlueprintService.h"
#include "Application/Domains/Gameplay/UnrealAgentMCPGameplayService.h"
#include "Application/Domains/Editor/UnrealAgentMCPEditorService.h"
#include "Application/Domains/Epic/UnrealAgentMCPEpicService.h"
#include "Application/Domains/Fab/UnrealAgentMCPFabService.h"
#include "Application/Domains/Foliage/UnrealAgentMCPFoliageService.h"
#include "Application/Domains/GAS/UnrealAgentMCPGASService.h"
#include "Application/Domains/Landscape/UnrealAgentMCPLandscapeService.h"
#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Domains/Networking/UnrealAgentMCPNetworkingService.h"
#include "Application/Domains/PCG/UnrealAgentMCPPCGService.h"
#include "Application/Domains/Project/UnrealAgentMCPProjectService.h"
#include "Application/Domains/Reflection/UnrealAgentMCPReflectionService.h"
#include "Application/Domains/Whitebox/UnrealAgentMCPWhiteboxService.h"
#include "Application/Ports/UnrealAgentMCPLandscapePort.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP::BuiltInToolsets
{
	void Register(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent"));
		if (!Plugin.IsValid())
		{
			OutErrors.Add(TEXT("无法定位 Unreal Agent 插件目录。"));
			return;
		}
		FString ActionManifestJson;
		const FString ActionManifestPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("ActionContracts.json"));
		if (!FFileHelper::LoadFileToString(ActionManifestJson, *ActionManifestPath))
		{
			OutErrors.Add(FString::Printf(TEXT("无法读取 action Manifest：%s。"), *ActionManifestPath));
			return;
		}
		const TSharedPtr<ActionContracts::FManifest> ActionManifest = ActionContracts::FManifest::Parse(ActionManifestJson, OutErrors);
		if (!ActionManifest.IsValid())
		{
			return;
		}

		const TSharedRef<FUnrealAgentMCPAssetService> Asset = MakeShared<FUnrealAgentMCPAssetService>(MakeShared<FUnrealAgentMCPUnrealAssetAdapter>());
		const TSharedRef<FUnrealAgentMCPUnrealAudioAdapter> Audio = MakeShared<FUnrealAgentMCPUnrealAudioAdapter>();
		const TSharedRef<FUnrealAgentMCPAnimationService> Animation = MakeShared<FUnrealAgentMCPAnimationService>(MakeShared<FUnrealAgentMCPUnrealAnimationAdapter>());
		const TSharedRef<FUnrealAgentMCPUnrealDemoAdapter> Demo = MakeShared<FUnrealAgentMCPUnrealDemoAdapter>();
		const TSharedRef<FUnrealAgentMCPBlueprintService> Blueprint = MakeShared<FUnrealAgentMCPBlueprintService>(MakeShared<FUnrealAgentMCPUnrealBlueprintAdapter>());
		const TSharedRef<FUnrealAgentMCPGameplayService> Gameplay = MakeShared<FUnrealAgentMCPGameplayService>(MakeShared<FUnrealAgentMCPUnrealGameplayAdapter>());
		const TSharedRef<FUnrealAgentMCPEditorService> Editor = MakeShared<FUnrealAgentMCPEditorService>(MakeShared<FUnrealAgentMCPUnrealEditorAdapter>());
		const TSharedRef<FUnrealAgentMCPLevelService> Level = MakeShared<FUnrealAgentMCPLevelService>(MakeShared<FUnrealAgentMCPUnrealLevelAdapter>());
		const TSharedRef<FUnrealAgentMCPProjectService> Project = MakeShared<FUnrealAgentMCPProjectService>(MakeShared<FUnrealAgentMCPUnrealProjectAdapter>());
		const TSharedRef<FUnrealAgentMCPReflectionService> Reflection = MakeShared<FUnrealAgentMCPReflectionService>(MakeShared<FUnrealAgentMCPUnrealReflectionAdapter>());
		const TSharedRef<FUnrealAgentMCPFoliageService> Foliage = MakeShared<FUnrealAgentMCPFoliageService>(MakeShared<FUnrealAgentMCPUnrealFoliageAdapter>());
		const TSharedRef<FUnrealAgentMCPNetworkingService> Networking = MakeShared<FUnrealAgentMCPNetworkingService>(MakeShared<FUnrealAgentMCPUnrealNetworkingAdapter>());
		const TSharedRef<FUnrealAgentMCPUnrealPluginsAdapter> Plugins = MakeShared<FUnrealAgentMCPUnrealPluginsAdapter>();
		const TSharedRef<FUnrealAgentMCPUnrealFeedbackAdapter> Feedback = MakeShared<FUnrealAgentMCPUnrealFeedbackAdapter>();
		const TSharedRef<FUnrealAgentMCPEpicService> Epic = MakeShared<FUnrealAgentMCPEpicService>(MakeShared<FUnrealAgentMCPUnrealEpicAdapter>());
		const TSharedRef<FUnrealAgentMCPChooserService> Chooser = MakeShared<FUnrealAgentMCPChooserService>(MakeShared<FUnrealAgentMCPUnrealChooserAdapter>());
		const TSharedRef<FUnrealAgentMCPFabService> Fab = MakeShared<FUnrealAgentMCPFabService>(MakeShared<FUnrealAgentMCPUnrealFabAdapter>());
		const TSharedRef<FUnrealAgentMCPUnrealLandscapeAdapter> LandscapeAdapter = MakeShared<FUnrealAgentMCPUnrealLandscapeAdapter>();
		const TSharedRef<IUnrealAgentMCPLandscapePort> LandscapePort = LandscapeAdapter;
		const TSharedRef<FUnrealAgentMCPLandscapeService> Landscape = MakeShared<FUnrealAgentMCPLandscapeService>(LandscapePort);
		const TSharedRef<FUnrealAgentMCPWhiteboxService> Whitebox = MakeShared<FUnrealAgentMCPWhiteboxService>(MakeShared<FUnrealAgentMCPUnrealWhiteboxAdapter>(LandscapePort));
		const TSharedRef<FUnrealAgentMCPPCGService> PCG = MakeShared<FUnrealAgentMCPPCGService>(MakeShared<FUnrealAgentMCPUnrealPCGAdapter>());
		const TSharedRef<FUnrealAgentMCPGASService> GAS = MakeShared<FUnrealAgentMCPGASService>(MakeShared<FUnrealAgentMCPUnrealGASAdapter>());
		const TSharedRef<FUnrealAgentMCPUnrealMaterialAdapter> Material = MakeShared<FUnrealAgentMCPUnrealMaterialAdapter>();
		const TSharedRef<FUnrealAgentMCPUnrealWidgetAdapter> Widget = MakeShared<FUnrealAgentMCPUnrealWidgetAdapter>();
		const TSharedRef<FUnrealAgentMCPUnrealNiagaraAdapter> Niagara = MakeShared<FUnrealAgentMCPUnrealNiagaraAdapter>();
		const TSharedRef<FUnrealAgentMCPUnrealStateTreeAdapter> StateTree = MakeShared<FUnrealAgentMCPUnrealStateTreeAdapter>();

		DomainCompatibility::RegisterTools(Registry, OutErrors, ActionManifest.ToSharedRef(), Asset, Editor, Level, Project, Reflection, Foliage, Networking, Plugins, Feedback,
			Epic, Chooser, Fab, Landscape, Whitebox, PCG, GAS, Material, Widget, Niagara, StateTree, Audio, Blueprint, Gameplay, Animation, Demo);
	}
}
