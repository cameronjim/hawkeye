#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Chase/ChaseTypes.h"
#include "ChaseRules.generated.h"

/**
 * A chase's rules, pure and world-free (claude-docs/gameplay-semantics.md, "Chases"): where along the route
 * each of them is, the lead and its band, the thief's speed, the lose clock, the catch, the beat's trigger
 * and the restore. UChaseSubsystem applies them; Hawkeye.Chase.* tests call them directly.
 */
UCLASS()
class HAWKEYE_API UChaseRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Distance along the route at each point: 0 at the first, the route's length at the last. */
	static TArray<float> Cumulative(const TArray<FVector>& Points);

	/** The point Along cm along the route (clamped to its ends), and the segment it is on. */
	static FVector PointAt(const TArray<FVector>& Points, const TArray<float>& Distances, float Along, int32* OutSegment = nullptr);

	/**
	 * Kate's progress: the route distance of the route point nearest Location, searched from PreviousAlong less
	 * BackWindow to PreviousAlong plus AheadWindow and only among points within OffRouteDistance (3D). False,
	 * with OutAlong left at PreviousAlong, when nothing in the window is that close (she is off the route).
	 */
	static bool Project(const TArray<FVector>& Points, const TArray<float>& Distances, const FVector& Location,
		float PreviousAlong, const FChaseTuning& Tuning, float& OutAlong);

	/**
	 * Her progress after a frame: never back, and never on by more than 1.5 times what she really moved plus
	 * 300 cm a second, so a projection that jumps to another part of the route is followed, not leapt to.
	 */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float StepProgress(float Previous, float Projected, float MovedCm, float DeltaSeconds);

	/** He can be caught (taken down, held, knocked down) only once he is cornered at the route's end. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool IsCatchable(bool bCornered);

	/** The lead: thief progress less Kate's, never under 0; off the route, the straight-line distance. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float Lead(float ThiefAlong, float KateAlong, bool bKateOnRoute, float StraightDistance);

	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static EChaseLeadBand Band(float Lead, const FChaseTuning& Tuning);

	/** The speed the rubber band wants on a Run leg at this lead, cm/s. Never 0. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float TargetSpeed(float Lead, const FChaseTuning& Tuning);

	/** Current moved toward Target by at most Rate * DeltaSeconds. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float StepSpeed(float Current, float Target, float Rate, float DeltaSeconds);

	/** The lose clock after DeltaSeconds: runs at LoseLead or more, 0 the moment the lead is under; held when bHeld. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float StepLoseClock(float Clock, float Lead, float DeltaSeconds, bool bHeld, const FChaseTuning& Tuning);

	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool IsLost(float Clock, const FChaseTuning& Tuning);

	/** How long a non-Run leg takes: Vault 0.5, Climb 0.8, JumpGap 0.45 + 0.06/m (at most 1.4), Drop height/900 + 0.3. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float LegSeconds(EChaseLeg Leg, float HorizontalLength, float Drop);

	/** Where his feet are Alpha (0..1) of the way through a leg from From to To: the leg's own arc. */
	static FVector LegPosition(EChaseLeg Leg, const FVector& From, const FVector& To, float Alpha);

	/** Caught: taken down, or downed by anything. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool IsCaught(bool bTakenDown, bool bLimp, bool bIncapacitated, bool bDead);

	/** The yaw a cornered thief faces: straight away from Kate. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float CorneredYaw(const FVector& Thief, const FVector& Kate);

	/** Where a load puts the thief: the smaller of his saved progress and Kate's plus RestoreMaxLead. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static float RestoredThiefProgress(float SavedThief, float KateProgress, const FChaseTuning& Tuning);

	/** Kate's progress has reached a waypoint at WaypointAlong. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool HasReached(float KateProgress, float WaypointAlong, const FChaseTuning& Tuning);

	/**
	 * The beat starts: not done yet, a zip started this frame from inside the zone, at an anchor within
	 * AnchorTolerance (300 cm) of the beat's.
	 */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool ShouldStartBeat(bool bBeatDone, bool bZipStarted, bool bStartInZone, float AnchorDistance);

	/** A playing beat may be skipped: only on a replay, and only 0.3 s or more into it. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool CanSkipBeat(bool bReplay, float BeatSeconds);

	/** A beat this old has been interrupted: put her on the exit. */
	UFUNCTION(BlueprintPure, Category = "Chase|Rules")
	static bool IsBeatStuck(float BeatSeconds);

	static constexpr float AnchorTolerance = 300.f;
	static constexpr float SkipAfterSeconds = 0.3f;
	static constexpr float BeatWatchdogSeconds = 5.f;
};
