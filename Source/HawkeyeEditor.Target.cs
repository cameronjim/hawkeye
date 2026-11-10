using UnrealBuildTool;
using System.Collections.Generic;

public class HawkeyeEditorTarget : TargetRules
{
	public HawkeyeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.AddRange(new string[] { "Hawkeye", "HawkeyeEditor" });
	}
}
