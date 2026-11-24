using System.IO;
using UnrealBuildTool;

/**
 * The graph node of the Hawkeye Body Pose animation node (the thug bodies' hands, Source/Hawkeye/Player/BodyPoseNode.h).
 * Uncooked-only, not editor-only: the AnimBlueprint's editor graph loads in the standalone -game runs on editor
 * binaries too, and a node class missing there leaves the graph broken. A cooked game never loads it.
 */
public class HawkeyeAnimEditor : ModuleRules
{
	public HawkeyeAnimEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Hawkeye"));
		PrivateIncludePaths.Add(ModuleDirectory);
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraph",
			"AnimGraphRuntime",
			"BlueprintGraph",
			"UnrealEd",
			"Hawkeye"
		});
	}
}
