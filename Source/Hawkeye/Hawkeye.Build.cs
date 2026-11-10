using UnrealBuildTool;

public class Hawkeye : ModuleRules
{
	public Hawkeye(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Lets every header be included relative to Source/Hawkeye
		// e.g. #include "Mission/MissionSubsystem.h"
		PublicIncludePaths.AddRange(new string[] { "Hawkeye" });

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			// The partner's brain: ST_Partner runs on a StateTreeAIComponent.
			"StateTreeModule",
			"GameplayStateTreeModule",
			"GameplayTags",
			// UHawkeyeNarrativeSettings: where the phone and dialogue tables live.
			"DeveloperSettings",
			// Campaign save: Plugins/SPUD (sinbad/SPUD, MIT) persists ISpudObject actors and globals.
			"SPUD",
			// Effects: every particle system is Niagara, built headless by create_vfx.py.
			"Niagara"
		});

		// Hawkeye.Audio.Smoke finds the MetaSounds through the asset registry.
		PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });

		// The world map ear-clips the building footprints (UHawkeyeMapWidget).
		PrivateDependencyModuleNames.Add("GeometryCore");

		// Strike clips warp toward the soft-lock target through the engine's motion warping, the same
		// component the Game Animation Sample's traversal uses.
		PrivateDependencyModuleNames.Add("MotionWarping");

		// Screenshots (the test passes and the playtest kit) write their PNGs off the game thread (World/HawkeyeCapture).
		PrivateDependencyModuleNames.Add("ImageCore");

		// The playtest kit writes notes.json and summary.json (Playtest/).
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "JsonUtilities" });

		// The game mode indexes the Game Animation Sample's motion-matching databases during the
		// load in uncooked runs, instead of on the first frame.
		PrivateDependencyModuleNames.Add("PoseSearch");

		// -HawkeyePerfLog reads the stat unit split (game and render thread, GPU) the engine keeps.
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });

		// UHawkeyePartnerTreeBuilder authors ST_Partner headless through the StateTree editor API;
		// UHawkeyeBowIKGraphBuilder authors the bow hands AnimBlueprints through the anim graph nodes;
		// UHawkeyeVfxBuilder authors the Niagara systems through the Niagara editor's stack view model;
		// UHawkeyeCombatMontageBuilder lays out the combat montages through the animation library's notify calls.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "StateTreeEditorModule", "PropertyBindingUtils", "UnrealEd",
				"AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "NiagaraEditor", "NiagaraCore", "AnimationBlueprintLibrary" });
			// UHawkeyeRetargetGraphBuilder authors a body look's AnimBlueprint: Retarget Pose From Mesh, the IK Rig plugin's node.
			PrivateDependencyModuleNames.AddRange(new string[] { "IKRig", "IKRigDeveloper" });
		}
	}
}
