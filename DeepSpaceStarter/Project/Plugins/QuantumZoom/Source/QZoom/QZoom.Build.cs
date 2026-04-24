using UnrealBuildTool;

public class QZoom : ModuleRules
{
    public QZoom(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "InputCore",
            "DisplayCluster", "LevelSequence", "MovieScene", "CinematicCamera"
        });
    }
}
