using System.IO;
using UnrealBuildTool;

/**
 * The editor-only module: the traversal debugger tab and the headless traversal audit commandlet. Everything with
 * rules lives in the game module (Source/Hawkeye/Traversal) so the game and the tests share it; this module is UI,
 * editor world plumbing and the commandlet's command line.
 */
public class HawkeyeEditor : ModuleRules
{
	public HawkeyeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The game module's headers are written relative to Source/Hawkeye ("Traversal/GrappleAudit.h") and include
		// each other that way, so this module needs the same root on its include path.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Hawkeye"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			// GEditor, the level viewport client, selection, FEditorDelegates, UEditorLoadingAndSavingUtils.
			"UnrealEd",
			// UEditorSubsystem.
			"EditorSubsystem",
			// The Tools menu entry.
			"ToolMenus",
			"Hawkeye"
		});
	}
}
