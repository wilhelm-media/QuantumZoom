using UnrealBuildTool;

public class QZoom : ModuleRules
{
    public QZoom(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine",
            "DisplayCluster", "LevelSequence", "MovieScene", "CinematicCamera"
        });
    }
}
