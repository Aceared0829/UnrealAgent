using UnrealBuildTool;

public class UnrealAgentMCPEditor : ModuleRules
{
	public UnrealAgentMCPEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// 各领域实现单元包含同名私有辅助函数，禁止 Unity 合并以维持编译边界。
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"UnrealAgentMCPCore",
			"UnrealAgentProviderSDK"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"AIModule",
			"AnimGraph",
			"AppFramework",
			"AudioEditor",
			"AudioExtensions",
			"AssetTools",
			"AssetRegistry",
			"BlueprintGraph",
			"BlueprintEditorLibrary",
			"Blutility",
			"Chooser",
			"ClothingSystemRuntimeInterface",
			"CoreUObject",
			"DesktopPlatform",
			"Engine",
			"EditorWidgets",
			"EditorScriptingUtilities",
			"EnhancedInput",
			"Foliage",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTagsEditor",
			"GameplayStateTreeModule",
			"GameplayTasks",
			"HTTP",
			"HTTPServer",
			"InputCore",
			"IKRig",
			"Json",
			"JsonUtilities",
			"Landscape",
			"LevelEditor",
			"LevelSequence",
			"MaterialEditor",
			"MetasoundEditor",
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundGraphCore",
			"MeshDescription",
			"MovieScene",
			"MovieSceneTracks",
			"NavigationSystem",
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",
			"PhysicsCore",
			"PlatformCrypto",
			"PlatformCryptoContext",
			"PCG",
			"PoseSearch",
			"Projects",
			"PythonScriptPlugin",
			"Settings",
			"SignalProcessing",
			"Slate",
			"SlateCore",
			"Sockets",
			"SmartObjectsModule",
			"StructUtils",
			"StateTreeModule",
			"StateTreeEditorModule",
			"PropertyBindingUtils",
			"StaticMeshDescription",
			"ToolMenus",
			"UMG",
			"UMGEditor",
			"ControlRig",
			"WorkspaceMenuStructure",
			"UnrealEd"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Advapi32.lib");
		}
	}
}
