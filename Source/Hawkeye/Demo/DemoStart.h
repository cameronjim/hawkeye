#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;
class UWorld;
struct FBossArena;
struct FChapterSiteInteriorDoor;

/** The clips the owner records by hand (Tools/demo-record.ps1, docs/demo/recording-guide.md). */
enum class EHawkeyeDemoClip : uint8
{
	None,
	StreetFight,
	StealthTakedown,
	TomasPhase
};

/** Where a clip's start puts Kate: her feet on the ground, which way she faces, and what she looks at. */
struct HAWKEYE_API FHawkeyeDemoStand
{
	/** Her feet, world cm: a point a downward trace found on walkable ground. */
	FVector Feet = FVector::ZeroVector;

	float Yaw = 0.f;

	/** Camera pitch, degrees (negative looks down). */
	float Pitch = -8.f;

	/** The point she faces (the pair, the door, the crew). */
	FVector Face = FVector::ZeroVector;

	/** One clause for the log: where this is and what is in front of her. */
	FString Where;
};

/**
 * The start points of the three demo clips, and the placement maths the laps share. `-HawkeyeDemoStart=<clip>` on the
 * command line makes the game mode call Apply once, after the chapter has started and its opening has put Kate where the
 * chapter wants her (claude-docs/gameplay-semantics.md, "Demo starts"). The Plan functions only look at the world and
 * return a stand; PutKate moves her. `Hawkeye.Lap.StreetFight` plans its stand with PlanStreetFight, so the lap and the
 * switch cannot drift.
 */
struct HAWKEYE_API FHawkeyeDemoStart
{
	/** The switch's name on the command line, without the dash or the equals. */
	static const TCHAR* const SwitchName;

	/** The tag the generator puts on the bat, the gunner and the heavy (generate_city.py STREET_GROUP_TAG). */
	static const FName StreetGroupTag;

	/** How far from the patrolling pair the street fight stands her, toward the heavy, cm. */
	static constexpr float StreetFightDistance = 900.f;

	/** How far the stealth start looks for a street spot, from the door, cm (nearest first). */
	static constexpr float DoorApproachMin = 600.f;
	static constexpr float DoorApproachMax = 2500.f;
	static constexpr float DoorApproachStep = 100.f;

	/** The clear space round an arrival volume the stealth start keeps her outside of: her capsule and a step, cm. */
	static constexpr float VolumeMargin = 120.f;

	/** The arena entry on the roof: how far in from the parapet anchor it is tried, cm. */
	static constexpr float RoofEntryMin = 250.f;
	static constexpr float RoofEntryMax = 900.f;
	static constexpr float RoofEntryStep = 50.f;

	/** The roof entry keeps this far (2D) from every crew member's spawn, cm. */
	static constexpr float RoofCrewClearance = 400.f;

	// --- The command line (pure) --------------------------------------------------------------------------

	/** "street_fight", "stealth_takedown", "tomas_phase" (any case, '-' for '_'); anything else is None. */
	static EHawkeyeDemoClip ParseClip(const FString& Name);

	static const TCHAR* ClipName(EHawkeyeDemoClip Clip);

	/**
	 * The clip a command line asks for. OutAsked is the raw text after -HawkeyeDemoStart= (empty when the switch is not
	 * there), so a name that means no clip can be told from no switch at all.
	 */
	static EHawkeyeDemoClip ClipFromCommandLine(const TCHAR* CommandLine, FString* OutAsked = nullptr);

	// --- Geometry (pure) ----------------------------------------------------------------------------------

	/** The yaw, degrees, that looks from From at To along the ground. */
	static float YawFacing(const FVector& From, const FVector& To);

	/** The street fight's spot, XY (Z is PairMid's): StreetFightDistance from the pair's middle toward the heavy. */
	static FVector StreetFightSpot(const FVector& PairMid, const FVector& HeavyAt);

	/** XY inside the box (centre, half extent) grown by Margin on every side. */
	static bool IsInsideBox2D(const FVector& Point, const FVector& Centre, const FVector& HalfExtent, float Margin);

	/**
	 * Street spots to try in front of a door, nearest first: DoorFloor plus a direction within 60 degrees either side
	 * of OutYaw (straight out first at each distance), every Step from Min to Max, never inside the volume (grown by
	 * VolumeMargin) so that walking toward the door enters it. Z is DoorFloor's.
	 */
	static TArray<FVector> DoorApproachCandidates(const FVector& DoorFloor, float OutYaw, const FVector& VolumeCentre,
		const FVector& VolumeHalfExtent);

	/** Spots to try on a roof, nearest the parapet first: Anchor pulled toward Centre by Min to Max in Step, at Anchor's Z. */
	static TArray<FVector> RoofEntryCandidates(const FVector& Anchor, const FVector& Centre);

	// --- The world ------------------------------------------------------------------------------------------

	/** The first thing under XY looking down from FromZ (a visibility trace); false when nothing is. */
	static bool FindGround(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& OutGround,
		AActor** OutActor = nullptr);

	/** Kate's capsule fits standing with her feet at Feet: nothing that blocks a pawn overlaps it. */
	static bool CapsuleIsFree(UWorld* World, const APawn* Pawn, const FVector& Feet);

	/**
	 * The street fight's stand: on the Avenue A sidewalk StreetFightDistance from the middle of the StreetGroup's bat
	 * and gunner, on the heavy's side, facing the pair. Reads the living StreetGroup thugs in the world (the bat, the
	 * gunner, the heavy). False and Why when they are not all there or no ground is under the spot.
	 */
	static bool PlanStreetFight(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why);

	/**
	 * The stealth start: on the street in front of the chapter site's first door into an interior, as near as 6 m (further
	 * where the current objective's arrival volume reaches), facing the door, outside the volume. Reads the running
	 * chapter's site. False and Why when the chapter has no site, no door, or no free ground in front of it.
	 */
	static bool PlanStealthTakedown(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why);

	/**
	 * PlanStealthTakedown's search for one door and one arrival volume (centre and half extent; zero extent when the
	 * objective has none): the first spot of DoorApproachCandidates with ground level with the door, free for Ignore's
	 * capsule (when Ignore is a pawn) and the door in view.
	 */
	static bool PlanDoorStand(UWorld* World, const AActor* Ignore, const FChapterSiteInteriorDoor& Door, const FVector& VolumeCentre,
		const FVector& VolumeHalfExtent, FHawkeyeDemoStand& Out, FString& Why);

	/**
	 * The Tomas start: on the club roof at the arena's entry (the parapet anchor's side), facing the crew, so that the
	 * fight's own watch sees her on the roof and completes meet_tomas; failing that, on the street stand below the
	 * anchor facing it (bOnRoof false). Reads the running boss fight's arena.
	 */
	static bool PlanTomasPhase(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why, bool& bOnRoof);

	/**
	 * PlanTomasPhase's search for one arena and the places its first phase's crew spawn: a roof spot from
	 * RoofEntryCandidates that is on the roof floor (UBossRules::IsOnRoof), RoofCrewClearance from the crew and the
	 * boss, and free for Ignore's capsule (when Ignore is a pawn); else the street stand.
	 */
	static bool PlanRoofEntry(UWorld* World, const AActor* Ignore, const FBossArena& Arena, const TArray<FVector>& CrewAt,
		FHawkeyeDemoStand& Out, FString& Why, bool& bOnRoof);

	/** Teleports Kate to Stand and turns her (and her camera) to face it; CutCamera snaps the view there. */
	static void PutKate(APawn* Kate, const FHawkeyeDemoStand& Stand, bool bCutCamera);

	/**
	 * What the game mode does for the switch: plans the clip's stand, puts Kate on it, and for the street fight puts the
	 * chapter's chase away (the thief runs off and losing him would reload the level). OutWhat says what was done, or
	 * why not. False when the clip could not be placed.
	 */
	static bool Apply(UWorld* World, EHawkeyeDemoClip Clip, FString& OutWhat);
};
