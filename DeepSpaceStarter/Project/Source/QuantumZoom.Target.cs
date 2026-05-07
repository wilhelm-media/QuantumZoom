using UnrealBuildTool;

public class QuantumZoomTarget : TargetRules
{
	public QuantumZoomTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("QZoomGame");
	}
}
