using UnrealBuildTool;

public class QuantumZoom : ModuleRules
{
    public QuantumZoom(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DisplayCluster",
            "LevelSequence",
            "MovieScene",
            "CinematicCamera"
        });
    }
}
