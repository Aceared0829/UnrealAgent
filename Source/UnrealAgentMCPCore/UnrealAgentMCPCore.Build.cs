using UnrealBuildTool;

public class UnrealAgentMCPCore : ModuleRules
{
	public UnrealAgentMCPCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Json"
		});

		PrivateDependencyModuleNames.Add("JsonUtilities");
	}
}
