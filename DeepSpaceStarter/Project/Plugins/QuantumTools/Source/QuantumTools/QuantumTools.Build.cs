using UnrealBuildTool;

public class QuantumTools : ModuleRules
{
    public QuantumTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DisplayCluster"
        });
    }
}
