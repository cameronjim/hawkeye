#pragma once

#include "CoreMinimal.h"
#include "Traversal/GrappleAudit.h"

class UWorld;

/**
 * The editor-world glue both the traversal debugger and UHawkeyeTraversalAuditCommandlet use: find the map's city
 * data and player pawn, build the profile and scene, spawn the city props for the duration of an audit (fire escapes
 * and clutter collide; the anchors' own actors are ignored by sight as the game does), run FGrappleAudit, and tidy up.
 */
struct FTraversalAuditRunner
{
	struct FOptions
	{
		/** Spawn the props (ACityLedgeSpawner::SpawnAll) if they are not out, and destroy them after. */
		bool bSpawnProps = true;
		/** A pawn class path to read the rules from instead of the map's game mode's default pawn. */
		FString PawnClassPath;
		/** Objective ids whose roofs must chain, in order. */
		TArray<FString> ObjectiveChain;
		/** Chain objectives that may be reached down a fire escape and up from the street (FGrappleAuditInputs). */
		TArray<FString> StreetLinkObjectives;
		/**
		 * Optional pair probe, run on the same scene and props as the audit: a text file of "x y anchor" lines (a street
		 * point in world cm and an anchor's generator index, # comments) and where to write one tab-separated row per line
		 * with the floor, verdict, distance, angle, lens, and what blocked the sight line and the zip. How the generator's
		 * reach model (Tools/Editor/_reach.py) is compared with the game's traces pair by pair.
		 */
		FString ProbeIn;
		FString ProbeOut;
	};

	/** Evaluates the probe pairs in ProbeIn against Scene and writes ProbeOut. False with OutError when it cannot. */
	static bool RunProbe(UWorld* World, const FGrappleReachScene& Scene, const FGrappleReachProfile& Profile, const FString& ProbeIn,
		const FString& ProbeOut, FString& OutError);

	/** The profile of the pawn PawnClassPath names, else the map's player pawn, else AHawkeyeCharacter. */
	static FGrappleReachProfile MakeProfile(const UWorld* World, const FString& PawnClassPath);

	/** The scene from World's buildings and its spawner's data. False with OutError when the data is missing. */
	static bool BuildScene(UWorld* World, FGrappleReachScene& OutScene, FString& OutError);

	/** Runs the audit on World. OutScene is the scene the report's indices refer to. */
	static bool Run(UWorld* World, const FOptions& Options, FGrappleReachScene& OutScene, FGrappleAuditReport& OutReport,
		FString& OutError);

	/** Splits "a, b,c" into ids. */
	static TArray<FString> ParseList(const FString& Text);
};
