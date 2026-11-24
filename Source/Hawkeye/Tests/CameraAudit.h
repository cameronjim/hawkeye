#pragma once

#include "CoreMinimal.h"
#include "Player/CameraRules.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The camera audit (claude-docs/gameplay-semantics.md, "Camera rules"; claude-docs/testing.md): an observer every
 * Hawkeye.Lap.* test runs under. Each frame, after the arm has ticked and the camera manager has cached the view, it
 * measures what the player's screen is looking from:
 *
 *   FloorUnderKate / FloorUnderLens  the lens is at least MinFloorClearance above the floor under her and under it
 *   LensOverlap                      a 15 cm sphere at the lens overlaps nothing that blocks the Camera channel
 *   NearPlane                        none of the four rays from the lens to the near plane's corners hits anything
 *   LensJump                         the lens moves faster than MaxLensSpeed relative to her, once the view's own
 *                                    orbit round her is taken out, unless the frame is a flagged cut
 *   AngularSpeed                     the view turns faster than MaxAngularSpeed, outside a flagged cut or scripted beat
 *   BodyToggle                       the body is hidden or shown twice in a second
 *
 * A cut is a teleport or snap (AHawkeyeCharacter::NotifyTeleported, the arm's own detection), a view-target blend or
 * cutscene (the view target is not her), or the frame after one. A scripted beat is NoteScriptedBeat, or a takedown.
 *
 * Every violation is collected with its time, place, mode and speed. The first of each rule fails the lap at once
 * (AddError on the running test); at the end the worst are written to Saved/Automation/CameraAudit/<lap>.json.
 * The thresholds are constants in FLimits: raising one to pass is a decision to tell the owner about, not a fix.
 *
 * FRuleBook is the rules on plain data, unit-tested (Hawkeye.Camera.AuditRules); Install() hooks the sampling.
 */
namespace HawkeyeCameraAudit
{
	enum class ERule : uint8
	{
		FloorUnderKate,
		FloorUnderLens,
		LensOverlap,
		NearPlane,
		LensJump,
		AngularSpeed,
		BodyToggle,
		Count
	};

	HAWKEYE_API const TCHAR* RuleName(ERule Rule);

	/** The thresholds. */
	struct FLimits
	{
		/** Lens above the floor under her and under it, cm. */
		float MinFloorClearance = 20.f;
		/** The overlap sphere at the lens, cm. */
		float OverlapRadius = 15.f;
		/**
		 * Lens speed relative to her beyond the orbit, cm/s. The arm's rates cap the arm at 1400 and the socket at 600,
		 * so legitimate layers add up to well under this; a sweep snapping the lens in is metres in one frame.
		 */
		float MaxLensSpeed = 3000.f;
		/** The view's turn rate, degrees/s: the aim bot's 540 and the arm's rotation lag sit under it. */
		float MaxAngularSpeed = 720.f;
		/** Body hide or show events in the sliding second. A violation at this many. */
		int32 BodyToggleCount = 2;
	};

	/** One frame's measurements: the world sampling fills it, the rules read it. */
	struct FFrame
	{
		double Time = 0.0;
		/** Seconds of the clock the player watches (dilation taken out). */
		float Delta = 0.f;
		FVector Lens = FVector::ZeroVector;
		FRotator View = FRotator::ZeroRotator;
		/** Her capsule centre. */
		FVector Pivot = FVector::ZeroVector;
		float KateSpeed = 0.f;
		FString Mode;
		bool bCut = false;
		bool bScriptedBeat = false;
		bool bBodyHidden = false;
		float FloorUnderKate = HawkeyeCameraRules::NoFloor;
		float FloorUnderLens = HawkeyeCameraRules::NoFloor;
		bool bLensOverlap = false;
		int32 NearPlaneHits = 0;
	};

	struct FViolation
	{
		ERule Rule = ERule::Count;
		double Time = 0.0;
		FVector Location = FVector::ZeroVector;
		FString Mode;
		float Speed = 0.f;
		/** What was measured and what it was held to. */
		float Value = 0.f;
		float Limit = 0.f;
		/** How bad, comparable within a rule only; the JSON lists the worst of each rule by it. */
		float Severity = 0.f;
	};

	/** The rules on frames, in order. */
	class HAWKEYE_API FRuleBook
	{
	public:
		explicit FRuleBook(const FLimits& InLimits = FLimits()) : Limits(InLimits) {}

		/** Appends this frame's violations to Out. */
		void Evaluate(const FFrame& Frame, TArray<FViolation>& Out);

		/** Forgets the previous frame (the next one is the first). */
		void Reset();

		const FLimits& GetLimits() const { return Limits; }

	private:
		FLimits Limits;
		bool bHasPrevious = false;
		FFrame Previous;
		TArray<double> ToggleTimes;
	};

	/** Hooks the audit to the automation framework and the world tick; safe to call twice. Called at module startup. */
	HAWKEYE_API void Install();

	/** The running lap's camera has a scripted beat for the next Seconds (a boss intro, a hand-placed shot): angular speed is not judged. */
	HAWKEYE_API void NoteScriptedBeat(float Seconds, const TCHAR* Why);

	/** Violations so far in the running lap's audit, or 0 with none running. */
	HAWKEYE_API int32 GetViolationCount();
}

#endif // WITH_DEV_AUTOMATION_TESTS
