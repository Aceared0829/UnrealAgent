// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPDomainCompatibility.cpp
 * @brief 第一批分类工具兼容路由；执行始终进入 Unreal Agent 自有 C++ 实现。
 */

#include "Adapters/Tooling/Compatibility/UnrealAgentMCPDomainCompatibility.h"

#include "Application/Actions/UnrealAgentMCPActionManifest.h"
#include "Application/Domains/Asset/UnrealAgentMCPAssetService.h"
#include "Application/Domains/Editor/UnrealAgentMCPEditorService.h"
#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Domains/Project/UnrealAgentMCPProjectService.h"
#include "Application/Domains/Reflection/UnrealAgentMCPReflectionService.h"
#include "Application/Domains/Foliage/UnrealAgentMCPFoliageService.h"
#include "Application/Domains/Networking/UnrealAgentMCPNetworkingService.h"
#include "Adapters/Unreal/Plugins/UnrealAgentMCPUnrealPluginsAdapter.h"
#include "Adapters/Unreal/Feedback/UnrealAgentMCPUnrealFeedbackAdapter.h"
#include "Application/Domains/Epic/UnrealAgentMCPEpicService.h"
#include "Application/Domains/Chooser/UnrealAgentMCPChooserService.h"
#include "Application/Domains/Fab/UnrealAgentMCPFabService.h"
#include "Application/Domains/Landscape/UnrealAgentMCPLandscapeService.h"
#include "Application/Domains/Whitebox/UnrealAgentMCPWhiteboxService.h"
#include "Application/Domains/PCG/UnrealAgentMCPPCGService.h"
#include "Application/Domains/GAS/UnrealAgentMCPGASService.h"
#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"
#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"
#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"
#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"
#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"
#include "Application/Domains/Blueprint/UnrealAgentMCPBlueprintService.h"
#include "Application/Domains/Gameplay/UnrealAgentMCPGameplayService.h"
#include "Application/Domains/Animation/UnrealAgentMCPAnimationService.h"
#include "Adapters/Unreal/Demo/UnrealAgentMCPUnrealDemoAdapter.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include <initializer_list>

namespace UnrealAgentMCP::DomainCompatibility
{
	namespace
	{
		FString GetAction(const TSharedPtr<FJsonObject>& Args)
		{
			FString Action;
			if (Args.IsValid())
			{
				Args->TryGetStringField(TEXT("action"), Action);
			}
			return Action;
		}

	}

	FString GetToolDefinitionsJson()
	{
		return TEXT("[]");
	}

	bool RegisterTools(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors, TSharedRef<const ActionContracts::FManifest> ActionManifest,
		TSharedRef<FUnrealAgentMCPAssetService> AssetService, TSharedRef<FUnrealAgentMCPEditorService> EditorService, TSharedRef<FUnrealAgentMCPLevelService> LevelService,
		TSharedRef<FUnrealAgentMCPProjectService> ProjectService, TSharedRef<FUnrealAgentMCPReflectionService> ReflectionService,
		TSharedRef<FUnrealAgentMCPFoliageService> FoliageService, TSharedRef<FUnrealAgentMCPNetworkingService> NetworkingService,
		TSharedRef<FUnrealAgentMCPUnrealPluginsAdapter> PluginsAdapter, TSharedRef<FUnrealAgentMCPUnrealFeedbackAdapter> FeedbackAdapter,
		TSharedRef<FUnrealAgentMCPEpicService> EpicService, TSharedRef<FUnrealAgentMCPChooserService> ChooserService, TSharedRef<FUnrealAgentMCPFabService> FabService,
		TSharedRef<FUnrealAgentMCPLandscapeService> LandscapeService, TSharedRef<FUnrealAgentMCPWhiteboxService> WhiteboxService, TSharedRef<FUnrealAgentMCPPCGService> PCGService,
		TSharedRef<FUnrealAgentMCPGASService> GASService, TSharedRef<FUnrealAgentMCPUnrealMaterialAdapter> MaterialAdapter,
		TSharedRef<FUnrealAgentMCPUnrealWidgetAdapter> WidgetAdapter, TSharedRef<FUnrealAgentMCPUnrealNiagaraAdapter> NiagaraAdapter,
		TSharedRef<FUnrealAgentMCPUnrealStateTreeAdapter> StateTreeAdapter, TSharedRef<FUnrealAgentMCPUnrealAudioAdapter> AudioAdapter,
		TSharedRef<FUnrealAgentMCPBlueprintService> BlueprintService, TSharedRef<FUnrealAgentMCPGameplayService> GameplayService,
		TSharedRef<FUnrealAgentMCPAnimationService> AnimationService, TSharedRef<FUnrealAgentMCPUnrealDemoAdapter> DemoAdapter)
	{
		auto MakeDomainDescriptor = [&ActionManifest](const FName Provider, FString Toolset, FString Name, FString Description, const EMcpToolRisk Risk,
										const EMcpToolTransactionPolicy TransactionPolicy, FMcpDynamicToolHandler Invoker)
		{
			FMcpToolDescriptor Descriptor;
			Descriptor.Provider = Provider;
			Descriptor.Toolset = MoveTemp(Toolset);
			Descriptor.Name = Name;
			Descriptor.QualifiedName = MoveTemp(Name);
			Descriptor.Description = MoveTemp(Description);
			const FString DomainName = Descriptor.Name;
			const ActionContracts::FDomainContract* Domain = ActionManifest->FindDomain(DomainName);
			if (Domain != nullptr)
			{
				Descriptor.InputSchema = Domain->InputSchema;
			}
			Descriptor.OutputSchema = MakeShared<FJsonObject>();
			Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
			Descriptor.OutputSchema->SetBoolField(TEXT("additionalProperties"), true);
			Descriptor.bOutputSchemaExplicit = false;
			Descriptor.Risk = Risk;
			Descriptor.bReadOnly = Risk == EMcpToolRisk::ReadOnly;
			Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
			Descriptor.TransactionPolicy = TransactionPolicy;
			Descriptor.bInputSchemaExplicit = true;
			Descriptor.bInputSchemaEnforced = true;
			Descriptor.ActionContractResolver = [ActionManifest, DomainName](const TSharedPtr<FJsonObject>& Arguments, FMcpResolvedToolContract& OutContract)
			{
				const ActionContracts::FActionContract* Contract = ActionManifest->FindAction(DomainName, GetAction(Arguments));
				if (Contract == nullptr)
				{
					return false;
				}
				OutContract.CanonicalToolId = Contract->ToolId;
				OutContract.InputSchema = Contract->InputSchema;
				OutContract.OutputSchema = Contract->OutputSchema;
				OutContract.Risk = Contract->Risk;
				OutContract.ExecutionMode = Contract->ExecutionMode;
				OutContract.TransactionPolicy = Contract->TransactionPolicy;
				OutContract.bCancelable = Contract->bCancelable;
				return true;
			};
			Descriptor.Invoker = [ActionManifest, DomainName, Invoker = MoveTemp(Invoker)](const TSharedPtr<FJsonObject>& Arguments)
			{
				const ActionContracts::FActionContract* Contract = ActionManifest->FindAction(DomainName, GetAction(Arguments));
				if (Contract == nullptr)
				{
					return ErrorJson(FString::Printf(TEXT("未知 action：%s.%s。"), *DomainName, *GetAction(Arguments)));
				}

				return Invoker(Arguments);
			};
			return Descriptor;
		};

		FString AssetError;
		FMcpToolDescriptor AssetDescriptor = MakeDomainDescriptor(TEXT("Application.Asset"), TEXT("UnrealAgentMCP.Asset"), TEXT("asset"),
			TEXT("资产发现、检查、生命周期、依赖关系、目录与协作锁操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[AssetService](const TSharedPtr<FJsonObject>& Args)
			{
				return AssetService->Execute(Args);
			});
		AssetDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("path"), TEXT("sourcePath"), TEXT("directory"), TEXT("packagePath") };
		AssetDescriptor.FilePathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("import_")))
			{
				return TArray<FString>{ TEXT("sourcePath"), TEXT("filePath"), TEXT("filename"), TEXT("files"), TEXT("filenames") };
			}
			if (Action == TEXT("export") || Action == TEXT("export_texture"))
			{
				return TArray<FString>{ TEXT("outputPath"), TEXT("destinationPath") };
			}
			if (Action == TEXT("migrate"))
			{
				return TArray<FString>{ TEXT("destinationPath") };
			}
			return TArray<FString>();
		};
		AssetDescriptor.AssetPathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			TArray<FString> Names{ TEXT("assetPath"), TEXT("path"), TEXT("directory"), TEXT("packagePath") };
			if (Action.StartsWith(TEXT("import_")))
			{
				Names.Add(TEXT("destinationPath"));
			}
			else if (Action != TEXT("export") && Action != TEXT("export_texture") && Action != TEXT("migrate"))
			{
				Names.Add(TEXT("sourcePath"));
				Names.Add(TEXT("destinationPath"));
			}
			return Names;
		};
		AssetDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action == TEXT("search") || Action.StartsWith(TEXT("list")) || Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("get")) ||
				Action == TEXT("health_check") || Action == TEXT("diagnose_registry"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("lock") || Action == TEXT("unlock") || Action == TEXT("unlock_all"))
			{
				return EMcpToolRisk::EditorState;
			}
			if (Action.StartsWith(TEXT("delete")) || Action.StartsWith(TEXT("remove")) || Action == TEXT("force_reload"))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		AssetDescriptor.ExecutionModeResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			return Action == TEXT("bulk_rename") || Action == TEXT("delete_batch") || Action == TEXT("move_folder") || Action.StartsWith(TEXT("import_")) ||
					Action == TEXT("set_texture_settings_by_type")
				? EMcpToolExecutionMode::Task
				: EMcpToolExecutionMode::Synchronous;
		};
		AssetDescriptor.bCancelable = true;
		const bool bAssetRegistered = Registry.RegisterDescriptor(MoveTemp(AssetDescriptor), AssetError);
		if (!bAssetRegistered)
		{
			OutErrors.Add(AssetError);
		}

		FString EditorError;
		FMcpToolDescriptor EditorDescriptor = MakeDomainDescriptor(TEXT("Application.Editor"), TEXT("UnrealAgentMCP.Editor"), TEXT("editor"),
			TEXT("编辑器会话、对象反射、PIE、视口、性能、日志与保存操作。"), EMcpToolRisk::EditorState, EMcpToolTransactionPolicy::None,
			[EditorService](const TSharedPtr<FJsonObject>& Args)
			{
				return EditorService->Execute(Args);
			});
		EditorDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("get_")) || Action.StartsWith(TEXT("list_")) || Action.StartsWith(TEXT("search_")) || Action == TEXT("describe_object"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("execute_command") || Action == TEXT("execute_python") || Action == TEXT("run_python_file") || Action == TEXT("purge_python_modules") ||
				Action.StartsWith(TEXT("invoke_")) || Action == TEXT("hot_reload"))
			{
				return EMcpToolRisk::CodeExecution;
			}
			if (Action == TEXT("save_dirty"))
			{
				return EMcpToolRisk::FileMutation;
			}
			if (Action == TEXT("set_property"))
			{
				return EMcpToolRisk::ContentMutation;
			}
			return EMcpToolRisk::EditorState;
		};
		EditorDescriptor.FilePathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("run_python_file") ? TArray<FString>{ TEXT("path"), TEXT("filePath") } : TArray<FString>();
		};
		EditorDescriptor.AssetPathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action == TEXT("run_python_file") || Action == TEXT("execute_python") || Action == TEXT("execute_command"))
			{
				return TArray<FString>();
			}
			return TArray<FString>{ TEXT("objectPath"), TEXT("assetPath"), TEXT("path"), TEXT("sequencePath") };
		};
		EditorDescriptor.RequiresResumableTaskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("capture_screenshot");
		};
		EditorDescriptor.StagedTaskFactory = [EditorService](const TSharedPtr<FJsonObject>& Args)
		{
			return EditorService->CreateTaskStepper(Args);
		};
		const bool bEditorRegistered = Registry.RegisterDescriptor(MoveTemp(EditorDescriptor), EditorError);
		if (!bEditorRegistered)
		{
			OutErrors.Add(EditorError);
		}

		FString LevelError;
		FMcpToolDescriptor LevelDescriptor = MakeDomainDescriptor(TEXT("Application.Level"), TEXT("UnrealAgentMCP.Level"), TEXT("level"),
			TEXT("关卡、Actor、World Partition、流送、灯光、体积与批处理操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::ScopedTransaction,
			[LevelService](const TSharedPtr<FJsonObject>& Args)
			{
				return LevelService->Execute(Args);
			});
		LevelDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("list_")) || Action.StartsWith(TEXT("get_")) || Action.StartsWith(TEXT("find_")) || Action.StartsWith(TEXT("query_")))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("select_actor") || Action == TEXT("focus_actor"))
			{
				return EMcpToolRisk::EditorState;
			}
			if (Action.Contains(TEXT("delete")) || Action.Contains(TEXT("remove")) || Action.Contains(TEXT("destroy")))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		LevelDescriptor.StagedTaskFactory = [LevelService](const TSharedPtr<FJsonObject>& Args)
		{
			return LevelService->CreateTaskStepper(Args);
		};
		LevelDescriptor.RequiresResumableTaskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			return Action == TEXT("delete_actors") || Action == TEXT("delete_by_folder") || Action == TEXT("place_actors_batch");
		};
		const bool bLevelRegistered = Registry.RegisterDescriptor(MoveTemp(LevelDescriptor), LevelError);
		if (!bLevelRegistered)
		{
			OutErrors.Add(LevelError);
		}

		FString ProjectError;
		FMcpToolDescriptor ProjectDescriptor = MakeDomainDescriptor(TEXT("Application.Project"), TEXT("UnrealAgentMCP.Project"), TEXT("project"),
			TEXT("项目源码、配置、构建与模块操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[ProjectService](const TSharedPtr<FJsonObject>& Args)
			{
				return ProjectService->Execute(Args);
			});
		const bool bProjectRegistered = Registry.RegisterDescriptor(MoveTemp(ProjectDescriptor), ProjectError);
		if (!bProjectRegistered)
		{
			OutErrors.Add(ProjectError);
		}
		FString ReflectionError;
		FMcpToolDescriptor ReflectionDescriptor = MakeDomainDescriptor(TEXT("Application.Reflection"), TEXT("UnrealAgentMCP.Reflection"), TEXT("reflection"),
			TEXT("Unreal 类型、模块、Gameplay Tag 与 SaveGame 反射操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::ReadOnly,
			[ReflectionService](const TSharedPtr<FJsonObject>& Args)
			{
				return ReflectionService->Execute(Args);
			});
		ReflectionDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			return Action.StartsWith(TEXT("create_")) || Action.StartsWith(TEXT("set_")) ? EMcpToolRisk::ContentMutation : EMcpToolRisk::ReadOnly;
		};
		const bool bReflectionRegistered = Registry.RegisterDescriptor(MoveTemp(ReflectionDescriptor), ReflectionError);
		if (!bReflectionRegistered)
		{
			OutErrors.Add(ReflectionError);
		}
		FString FoliageError;
		FMcpToolDescriptor FoliageDescriptor = MakeDomainDescriptor(TEXT("Application.Foliage"), TEXT("UnrealAgentMCP.Foliage"), TEXT("foliage"),
			TEXT("植被类型、设置与空间实例采样。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[FoliageService](const TSharedPtr<FJsonObject>& Args)
			{
				return FoliageService->Execute(Args);
			});
		const bool bFoliageRegistered = Registry.RegisterDescriptor(MoveTemp(FoliageDescriptor), FoliageError);
		if (!bFoliageRegistered)
		{
			OutErrors.Add(FoliageError);
		}
		FString NetworkingError;
		FMcpToolDescriptor NetworkingDescriptor = MakeDomainDescriptor(TEXT("Application.Networking"), TEXT("UnrealAgentMCP.Networking"), TEXT("networking"),
			TEXT("Actor Blueprint 复制默认值与变量复制操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[NetworkingService](const TSharedPtr<FJsonObject>& Args)
			{
				return NetworkingService->Execute(Args);
			});
		const bool bNetworkingRegistered = Registry.RegisterDescriptor(MoveTemp(NetworkingDescriptor), NetworkingError);
		if (!bNetworkingRegistered)
		{
			OutErrors.Add(NetworkingError);
		}
		FString PluginsError;
		FMcpToolDescriptor PluginsDescriptor = MakeDomainDescriptor(TEXT("Application.Plugins"), TEXT("UnrealAgentMCP.Plugins"), TEXT("plugins"),
			TEXT("Unreal 插件、模块与依赖检查。"), EMcpToolRisk::ReadOnly, EMcpToolTransactionPolicy::ReadOnly,
			[PluginsAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return PluginsAdapter->Execute(Args);
			});
		const bool bPluginsRegistered = Registry.RegisterDescriptor(MoveTemp(PluginsDescriptor), PluginsError);
		if (!bPluginsRegistered)
		{
			OutErrors.Add(PluginsError);
		}
		FString FeedbackError;
		FMcpToolDescriptor FeedbackDescriptor = MakeDomainDescriptor(TEXT("Application.Feedback"), TEXT("UnrealAgentMCP.Feedback"), TEXT("feedback"),
			TEXT("本地反馈路由与项目归档。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[FeedbackAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return FeedbackAdapter->Execute(Args);
			});
		const bool bFeedbackRegistered = Registry.RegisterDescriptor(MoveTemp(FeedbackDescriptor), FeedbackError);
		if (!bFeedbackRegistered)
		{
			OutErrors.Add(FeedbackError);
		}
		FString EpicError;
		FMcpToolDescriptor EpicDescriptor = MakeDomainDescriptor(TEXT("Application.Epic"), TEXT("UnrealAgentMCP.Epic"), TEXT("epic"), TEXT("独立反射 Toolset 的发现与调用。"),
			EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[EpicService](const TSharedPtr<FJsonObject>& Args)
			{
				return EpicService->Execute(Args);
			});
		const bool bEpicRegistered = Registry.RegisterDescriptor(MoveTemp(EpicDescriptor), EpicError);
		if (!bEpicRegistered)
		{
			OutErrors.Add(EpicError);
		}
		FString ChooserError;
		FMcpToolDescriptor ChooserDescriptor = MakeDomainDescriptor(TEXT("Application.Chooser"), TEXT("UnrealAgentMCP.Chooser"), TEXT("chooser"),
			TEXT("ChooserTable 列、行与嵌套对象引用操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[ChooserService](const TSharedPtr<FJsonObject>& Args)
			{
				return ChooserService->Execute(Args);
			});
		const bool bChooserRegistered = Registry.RegisterDescriptor(MoveTemp(ChooserDescriptor), ChooserError);
		if (!bChooserRegistered)
		{
			OutErrors.Add(ChooserError);
		}
		FString FabError;
		FMcpToolDescriptor FabDescriptor = MakeDomainDescriptor(TEXT("Application.Fab"), TEXT("UnrealAgentMCP.Fab"), TEXT("fab"), TEXT("Fab 会话发现、独立缓存与本地内容导入。"),
			EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[FabService](const TSharedPtr<FJsonObject>& Args)
			{
				return FabService->Execute(Args);
			});
		const bool bFabRegistered = Registry.RegisterDescriptor(MoveTemp(FabDescriptor), FabError);
		if (!bFabRegistered)
		{
			OutErrors.Add(FabError);
		}
		FString LandscapeError;
		FMcpToolDescriptor LandscapeDescriptor = MakeDomainDescriptor(TEXT("Application.Landscape"), TEXT("UnrealAgentMCP.Landscape"), TEXT("landscape"),
			TEXT("Landscape 查询、创建、雕刻、绘制、材质、图层与样条操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[LandscapeService](const TSharedPtr<FJsonObject>& Args)
			{
				return LandscapeService->Execute(Args);
			});
		LandscapeDescriptor.StagedTaskFactory = [LandscapeService](const TSharedPtr<FJsonObject>& Args)
		{
			return LandscapeService->CreateTaskStepper(Args);
		};
		LandscapeDescriptor.RequiresResumableTaskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			return Action == TEXT("sculpt") || Action == TEXT("sculpt_batch") || Action == TEXT("sample_batch") || Action == TEXT("sample_grid") ||
				Action == TEXT("sample_polyline") || Action == TEXT("paint_layer") || Action == TEXT("set_height_rect") || Action == TEXT("import_heightmap") ||
				Action == TEXT("reset_heights");
		};
		LandscapeDescriptor.FilePathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("import_heightmap") ? TArray<FString>{ TEXT("filePath") } : TArray<FString>();
		};
		const bool bLandscapeRegistered = Registry.RegisterDescriptor(MoveTemp(LandscapeDescriptor), LandscapeError);
		if (!bLandscapeRegistered)
		{
			OutErrors.Add(LandscapeError);
		}
		FString WhiteboxError;
		FMcpToolDescriptor WhiteboxDescriptor = MakeDomainDescriptor(TEXT("Application.Whitebox"), TEXT("UnrealAgentMCP.Whitebox"), TEXT("whitebox"),
			TEXT("Semantic Whitebox and massing workflows backed by HISM and Landscape ports."), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::ScopedTransaction,
			[WhiteboxService](const TSharedPtr<FJsonObject>& Args)
			{
				return WhiteboxService->Execute(Args);
			});
		WhiteboxDescriptor.StagedTaskFactory = [WhiteboxService](const TSharedPtr<FJsonObject>& Args)
		{
			return WhiteboxService->CreateTaskStepper(Args);
		};
		WhiteboxDescriptor.bRequiresResumableTask = true;
		WhiteboxDescriptor.AssetPathArguments = { TEXT("meshPath"), TEXT("materialPath") };
		WhiteboxDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("clear_folder") ? EMcpToolRisk::Destructive : EMcpToolRisk::ContentMutation;
		};
		const bool bWhiteboxRegistered = Registry.RegisterDescriptor(MoveTemp(WhiteboxDescriptor), WhiteboxError);
		if (!bWhiteboxRegistered)
		{
			OutErrors.Add(WhiteboxError);
		}
		FString PCGError;
		FMcpToolDescriptor PCGDescriptor = MakeDomainDescriptor(TEXT("Application.PCG"), TEXT("UnrealAgentMCP.PCG"), TEXT("pcg"),
			TEXT("PCG 图、节点、连线、组件、体积、生成与往返导入导出操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[PCGService](const TSharedPtr<FJsonObject>& Args)
			{
				return PCGService->Execute(Args);
			});
		PCGDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("graphPath"), TEXT("packagePath") };
		PCGDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("list_")) || Action.StartsWith(TEXT("read_")) || Action.StartsWith(TEXT("get_")) || Action.StartsWith(TEXT("export_")))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action.StartsWith(TEXT("remove_")) || Action.StartsWith(TEXT("disconnect_")) || Action == TEXT("cleanup"))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		PCGDescriptor.ExecutionModeResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			return Action == TEXT("execute") || Action == TEXT("force_regenerate") ? EMcpToolExecutionMode::Task : EMcpToolExecutionMode::Synchronous;
		};
		PCGDescriptor.bCancelable = true;
		const bool bPCGRegistered = Registry.RegisterDescriptor(MoveTemp(PCGDescriptor), PCGError);
		if (!bPCGRegistered)
		{
			OutErrors.Add(PCGError);
		}
		FString GASError;
		FMcpToolDescriptor GASDescriptor = MakeDomainDescriptor(TEXT("Application.GAS"), TEXT("UnrealAgentMCP.GAS"), TEXT("gas"),
			TEXT("GAS Blueprint、ASC、Attribute、Effect 与状态检查。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::None,
			[GASService](const TSharedPtr<FJsonObject>& Args)
			{
				return GASService->Execute(Args);
			});
		const bool bGASRegistered = Registry.RegisterDescriptor(MoveTemp(GASDescriptor), GASError);
		if (!bGASRegistered)
		{
			OutErrors.Add(GASError);
		}
		FString MaterialError;
		FMcpToolDescriptor MaterialDescriptor = MakeDomainDescriptor(TEXT("Application.Material"), TEXT("UnrealAgentMCP.Material"), TEXT("material"),
			TEXT("材质、实例、参数、表达式图、编译、验证与预览操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[MaterialAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return MaterialAdapter->Execute(Args);
			});
		MaterialDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("materialPath"), TEXT("parentPath"), TEXT("newParentPath"), TEXT("texturePath"), TEXT("sourcePath"),
			TEXT("destinationPath"), TEXT("functionPath"), TEXT("packagePath") };
		MaterialDescriptor.FilePathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("render_preview") ? TArray<FString>{ TEXT("outputPath") } : TArray<FString>();
		};
		MaterialDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("list")) || Action == TEXT("validate") || Action == TEXT("get_shader_stats") ||
				Action == TEXT("export_graph"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("delete_expression") || Action == TEXT("disconnect_property") || Action == TEXT("clear_instance_parameters") || Action == TEXT("import_graph"))
			{
				return EMcpToolRisk::Destructive;
			}
			if (Action == TEXT("render_preview"))
			{
				return EMcpToolRisk::FileMutation;
			}
			return EMcpToolRisk::ContentMutation;
		};
		const bool bMaterialRegistered = Registry.RegisterDescriptor(MoveTemp(MaterialDescriptor), MaterialError);
		if (!bMaterialRegistered)
		{
			OutErrors.Add(MaterialError);
		}
		FString WidgetError;
		FMcpToolDescriptor WidgetDescriptor = MakeDomainDescriptor(TEXT("Application.Widget"), TEXT("UnrealAgentMCP.Widget"), TEXT("widget"),
			TEXT("Widget 蓝图树、属性、绑定、Editor Utility 与运行态 UI 操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[WidgetAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return WidgetAdapter->Execute(Args);
			});
		WidgetDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("path"), TEXT("packagePath"), TEXT("widgetClass") };
		WidgetDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("get")) || Action.StartsWith(TEXT("list")))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action.StartsWith(TEXT("run_")) || Action == TEXT("add_to_viewport") || Action == TEXT("invoke_runtime_function"))
			{
				return EMcpToolRisk::EditorState;
			}
			if (Action == TEXT("remove_widget") || Action == TEXT("clear_binding"))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		const bool bWidgetRegistered = Registry.RegisterDescriptor(MoveTemp(WidgetDescriptor), WidgetError);
		if (!bWidgetRegistered)
		{
			OutErrors.Add(WidgetError);
		}
		FString NiagaraError;
		FMcpToolDescriptor NiagaraDescriptor = MakeDomainDescriptor(TEXT("Application.Niagara"), TEXT("UnrealAgentMCP.Niagara"), TEXT("niagara"),
			TEXT("Niagara 系统、发射器、运行时组件、渲染器与模块栈操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[NiagaraAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return NiagaraAdapter->Execute(Args);
			});
		NiagaraDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("systemPath"), TEXT("emitterPath"), TEXT("modulePath"), TEXT("templatePath"), TEXT("packagePath") };
		NiagaraDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("list")) || Action.StartsWith(TEXT("get")) || Action.StartsWith(TEXT("inspect")) || Action == TEXT("validate"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("reactivate") || Action == TEXT("set_parameter"))
			{
				return EMcpToolRisk::EditorState;
			}
			if (Action.StartsWith(TEXT("remove")))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		const bool bNiagaraRegistered = Registry.RegisterDescriptor(MoveTemp(NiagaraDescriptor), NiagaraError);
		if (!bNiagaraRegistered)
		{
			OutErrors.Add(NiagaraError);
		}
		FString StateTreeError;
		FMcpToolDescriptor StateTreeDescriptor = MakeDomainDescriptor(TEXT("Application.StateTree"), TEXT("UnrealAgentMCP.StateTree"), TEXT("statetree"),
			TEXT("StateTree 状态层级、节点、Transition、Binding 与参数操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[StateTreeAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return StateTreeAdapter->Execute(Args);
			});
		StateTreeDescriptor.AssetPathArguments = { TEXT("assetPath") };
		StateTreeDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("list")) || Action == TEXT("read") || Action == TEXT("validate"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action.StartsWith(TEXT("remove")) || Action == TEXT("clear_state_nodes"))
			{
				return EMcpToolRisk::Destructive;
			}
			return EMcpToolRisk::ContentMutation;
		};
		const bool bStateTreeRegistered = Registry.RegisterDescriptor(MoveTemp(StateTreeDescriptor), StateTreeError);
		if (!bStateTreeRegistered)
		{
			OutErrors.Add(StateTreeError);
		}
		FString AudioError;
		FMcpToolDescriptor AudioDescriptor = MakeDomainDescriptor(TEXT("Application.Audio"), TEXT("UnrealAgentMCP.Audio"), TEXT("audio"),
			TEXT("音频资产、SoundCue、MetaSound、Submix、路由与编辑器试听操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[AudioAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return AudioAdapter->Execute(Args);
			});
		AudioDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("soundWavePath"), TEXT("submixPath"), TEXT("parentPath"), TEXT("effectPath"), TEXT("soundClassPath"),
			TEXT("attenuationPath"), TEXT("concurrencyPath"), TEXT("packagePath") };
		AudioDescriptor.FilePathArgumentsResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			return GetAction(Args) == TEXT("import_audio") ? TArray<FString>{ TEXT("filePath") } : TArray<FString>();
		};
		AudioDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action == TEXT("list") || Action == TEXT("extract_pcm") || Action.EndsWith(TEXT("get_graph")) || Action.EndsWith(TEXT("list_node_classes")))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action == TEXT("play_at_location") || Action == TEXT("spawn_ambient"))
			{
				return EMcpToolRisk::EditorState;
			}
			return EMcpToolRisk::ContentMutation;
		};
		const bool bAudioRegistered = Registry.RegisterDescriptor(MoveTemp(AudioDescriptor), AudioError);
		if (!bAudioRegistered)
		{
			OutErrors.Add(AudioError);
		}
		FString BlueprintError;
		FMcpToolDescriptor BlueprintDescriptor = MakeDomainDescriptor(TEXT("Application.Blueprint"), TEXT("UnrealAgentMCP.Blueprint"), TEXT("blueprint"),
			TEXT("Blueprint 资产、变量、函数、图节点、Pin、组件树、CDO 与批量编排操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[BlueprintService](const TSharedPtr<FJsonObject>& Args)
			{
				return BlueprintService->Execute(Args);
			});
		BlueprintDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("blueprintPath"), TEXT("path"), TEXT("otherPath"), TEXT("targetPath"), TEXT("interfacePath") };
		BlueprintDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("list")) || Action.StartsWith(TEXT("get_")) || Action == TEXT("diff") ||
				Action == TEXT("resolve_graph") || Action == TEXT("validate") || Action == TEXT("export_nodes_t3d") || Action == TEXT("search_node_types"))
			{
				return EMcpToolRisk::ReadOnly;
			}
			if (Action.StartsWith(TEXT("delete")) || Action.StartsWith(TEXT("remove")) || Action == TEXT("cleanup_graph"))
				return EMcpToolRisk::Destructive;
			return EMcpToolRisk::ContentMutation;
		};
		const bool bBlueprintRegistered = Registry.RegisterDescriptor(MoveTemp(BlueprintDescriptor), BlueprintError);
		if (!bBlueprintRegistered)
			OutErrors.Add(BlueprintError);
		FString GameplayError;
		FMcpToolDescriptor GameplayDescriptor = MakeDomainDescriptor(TEXT("Application.Gameplay"), TEXT("UnrealAgentMCP.Gameplay"), TEXT("gameplay"),
			TEXT("物理、碰撞、导航、Enhanced Input、AI 资产、StateTree、SmartObject 与 Game Framework 操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[GameplayService](const TSharedPtr<FJsonObject>& Args)
			{
				return GameplayService->Execute(Args);
			});
		GameplayDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("imcPath"), TEXT("inputActionPath"), TEXT("newInputActionPath"), TEXT("blackboardPath"),
			TEXT("parentPath"), TEXT("behaviorTreePath"), TEXT("blueprintPath"), TEXT("gameModePath") };
		GameplayDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("list")) || Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("get_")) || Action == TEXT("find_nav_path") ||
				Action == TEXT("project_to_nav"))
				return EMcpToolRisk::ReadOnly;
			if (Action.StartsWith(TEXT("remove")))
				return EMcpToolRisk::Destructive;
			if (Action == TEXT("rebuild_navigation") || Action == TEXT("add_impulse"))
				return EMcpToolRisk::EditorState;
			return EMcpToolRisk::ContentMutation;
		};
		const bool bGameplayRegistered = Registry.RegisterDescriptor(MoveTemp(GameplayDescriptor), GameplayError);
		if (!bGameplayRegistered)
			OutErrors.Add(GameplayError);
		FString AnimationError;
		FMcpToolDescriptor AnimationDescriptor = MakeDomainDescriptor(TEXT("Application.Animation"), TEXT("UnrealAgentMCP.Animation"), TEXT("animation"),
			TEXT("动画资产、骨架、AnimGraph、IK Rig、重定向、PoseSearch 与场景骨骼操作。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[AnimationService](const TSharedPtr<FJsonObject>& Args)
			{
				return AnimationService->Execute(Args);
			});
		AnimationDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("path"), TEXT("skeletonPath"), TEXT("skeletalMeshPath"), TEXT("animSequencePath"), TEXT("animationPath"),
			TEXT("schemaPath"), TEXT("sourceRig"), TEXT("targetRig"), TEXT("sequencePath") };
		AnimationDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action.StartsWith(TEXT("read")) || Action.StartsWith(TEXT("list")) || Action.StartsWith(TEXT("get_")) || Action.StartsWith(TEXT("scan")) ||
				Action.StartsWith(TEXT("inspect")) || Action.StartsWith(TEXT("compare")))
				return EMcpToolRisk::ReadOnly;
			if (Action.StartsWith(TEXT("remove")))
				return EMcpToolRisk::Destructive;
			if (Action == TEXT("preview_animation") || Action == TEXT("rebind_leader_pose"))
				return EMcpToolRisk::EditorState;
			return EMcpToolRisk::ContentMutation;
		};
		const bool bAnimationRegistered = Registry.RegisterDescriptor(MoveTemp(AnimationDescriptor), AnimationError);
		if (!bAnimationRegistered)
			OutErrors.Add(AnimationError);
		FString DemoError;
		FMcpToolDescriptor DemoDescriptor = MakeDomainDescriptor(TEXT("Application.Demo"), TEXT("UnrealAgentMCP.Demo"), TEXT("demo"),
			TEXT("Neon Shrine 十九步示例场景构建、回家与隔离清理。"), EMcpToolRisk::ContentMutation, EMcpToolTransactionPolicy::Atomic,
			[DemoAdapter](const TSharedPtr<FJsonObject>& Args)
			{
				return DemoAdapter->Execute(Args);
			});
		DemoDescriptor.AssetPathArguments = { TEXT("rootPath"), TEXT("homeLevelPath") };
		DemoDescriptor.RiskResolver = [](const TSharedPtr<FJsonObject>& Args)
		{
			const FString Action = GetAction(Args);
			if (Action == TEXT("get_steps") || (Action == TEXT("step") && (!Args || (!Args->HasField(TEXT("stepIndex")) && !Args->HasField(TEXT("step"))))))
				return EMcpToolRisk::ReadOnly;
			if (Action == TEXT("cleanup"))
				return EMcpToolRisk::Destructive;
			if (Action == TEXT("go_home"))
				return EMcpToolRisk::EditorState;
			return EMcpToolRisk::ContentMutation;
		};
		const bool bDemoRegistered = Registry.RegisterDescriptor(MoveTemp(DemoDescriptor), DemoError);
		if (!bDemoRegistered)
			OutErrors.Add(DemoError);
		return bAssetRegistered && bEditorRegistered && bLevelRegistered && bProjectRegistered && bReflectionRegistered && bFoliageRegistered && bNetworkingRegistered &&
			bPluginsRegistered && bFeedbackRegistered && bEpicRegistered && bChooserRegistered && bFabRegistered && bLandscapeRegistered && bWhiteboxRegistered && bPCGRegistered &&
			bGASRegistered && bMaterialRegistered && bWidgetRegistered && bNiagaraRegistered && bStateTreeRegistered && bAudioRegistered && bBlueprintRegistered &&
			bGameplayRegistered && bAnimationRegistered && bDemoRegistered;
	}

}
