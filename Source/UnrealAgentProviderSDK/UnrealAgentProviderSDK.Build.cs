using UnrealBuildTool;

public class UnrealAgentProviderSDK : ModuleRules
{
	public UnrealAgentProviderSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"UnrealAgentMCPCore"
		});
	}
}
