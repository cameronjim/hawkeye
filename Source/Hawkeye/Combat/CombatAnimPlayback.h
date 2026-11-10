#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
class UAnimMontage;
class UAnimNotifyState;
class UAnimSequenceBase;
class UCombatAnimSet;
class USkeletalMeshComponent;
enum class ECombatAnimRole : uint8;

/**
 * Where and how a combat montage plays (claude-docs/animation.md). Shared by the melee, bow,
 * finisher, dodge and hit-reaction code so the instance choice lives in one place:
 *
 *   - a montage whose slot is UpperBodySlot plays on the post-process instance (ABP_BowIK_Post),
 *     whose graph layers that slot over locomotion from spine_01 up;
 *   - any other montage plays full body on the main instance when it has a graph (the sample's
 *     AnimBP on Kate and Clint, whose DefaultSlot the traversal already uses), else on the
 *     post-process instance's own DefaultSlot (thugs, whose main instance is a single clip).
 *
 * Every function tolerates nulls and returns "did not play", which is what sends the caller down
 * its procedural fallback.
 */
namespace HawkeyeCombatAnim
{
	/** The upper-body slot's name, in the montages and in the post-process graph. */
	HAWKEYE_API extern const FName UpperBodySlot;

	/** The full-body slot's name. */
	HAWKEYE_API extern const FName FullBodySlot;

	/** True when Montage's first slot track is UpperBodySlot. */
	HAWKEYE_API bool IsUpperBodyMontage(const UAnimMontage* Montage);

	/** The instance Montage should play on for Mesh, or null when Mesh has none that can. */
	HAWKEYE_API UAnimInstance* PickInstance(const USkeletalMeshComponent* Mesh, const UAnimMontage* Montage);

	/**
	 * Plays Montage at PlayRate, from StartAtSeconds into it, on the instance PickInstance chooses. Returns that instance, or null
	 * when nothing played (no mesh, no instance, a skeleton that does not match).
	 */
	HAWKEYE_API UAnimInstance* Play(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float PlayRate = 1.f,
		float StartAtSeconds = 0.f);

	/**
	 * Plays Role's montage from Set on Mesh: at rate 1, or fitted to take FitToSeconds when that is
	 * positive. Returns the instance, or null when the role is empty or the clip did not play.
	 */
	HAWKEYE_API UAnimInstance* PlayRole(USkeletalMeshComponent* Mesh, const UCombatAnimSet* Set, ECombatAnimRole Role,
		float FitToSeconds = 0.f);

	/** Blends Montage out of Instance over BlendOutSeconds if it is playing there. */
	HAWKEYE_API void Stop(UAnimInstance* Instance, const UAnimMontage* Montage, float BlendOutSeconds);

	/** True while Montage plays (and is not blending out) on Instance. */
	HAWKEYE_API bool IsPlaying(const UAnimInstance* Instance, const UAnimMontage* Montage);

	/** The rate that makes a ClipSeconds clip take WantedSeconds, clamped to 0.25..4. 1 when either is not positive. */
	HAWKEYE_API float FitRate(float ClipSeconds, float WantedSeconds);

	/** How a strike clip plays so its hit lands on a telegraph's end: see FitHitToWindup. */
	struct FHitFit
	{
		/** The montage's play rate. */
		float Rate = 1.f;
		/** Where in the montage it starts, seconds: past the start of a wind-up too long to fit. */
		float StartAtSeconds = 0.f;
		/** Seconds it holds its first frame before playing: a wind-up too short to fit. */
		float HoldSeconds = 0.f;
	};

	/**
	 * Fits a clip whose hit window opens HitSeconds into it to a WindupSeconds telegraph, at a rate
	 * between MinRate and MaxRate: HitSeconds / WindupSeconds when that is inside them; above MaxRate it
	 * plays at MaxRate from later in the wind-up, below MinRate at MinRate after holding its first frame.
	 * Always HoldSeconds + (HitSeconds - StartAtSeconds) / Rate == WindupSeconds: the telegraph decides
	 * when the hit lands, the clip only how it looks. Rate 1 from the start when either time is not positive.
	 */
	HAWKEYE_API FHitFit FitHitToWindup(float HitSeconds, float WindupSeconds, float MinRate, float MaxRate);

	/**
	 * The first notify state of class NotifyClass on Animation: its start and end, seconds. False when
	 * there is none.
	 */
	HAWKEYE_API bool FindNotifyWindow(const UAnimSequenceBase* Animation, const UClass* NotifyClass, float& OutStart,
		float& OutEnd);

	/** True when Montage carries root motion the character movement will use (played on the main instance). */
	HAWKEYE_API bool DrivesRootMotion(const USkeletalMeshComponent* Mesh, const UAnimMontage* Montage);
}
