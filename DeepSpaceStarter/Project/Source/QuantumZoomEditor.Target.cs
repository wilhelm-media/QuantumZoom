using UnrealBuildTool;

public class QuantumZoomEditorTarget : TargetRules
{
	public QuantumZoomEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("QZoomGame");
	}
}
