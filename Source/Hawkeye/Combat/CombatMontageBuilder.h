#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CombatMontageBuilder.generated.h"

class UAnimMontage;
class UAnimNotifyState;
class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;

/**
 * How one combat montage is laid out. Windows are fractions of the source sequence's length (0..1); a
 * start below 0 leaves that window out. The montage may play only part of the sequence
 * (ClipStartSeconds to ClipEndSeconds) at PlayRate, which the builder bakes into the montage's segment,
 * so the montage itself is short and fast and plays at rate 1; the windows are mapped from the
 * sequence's time into the montage's ((t - ClipStartSeconds) / PlayRate) and clamped inside it.
 * Tools/Data/Anims/manifest.json gives these per clip, with per-role defaults in
 * Tools/Editor/import_combat_anims.py.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeCombatMontageSpec
{
	GENERATED_BODY()

	/** DefaultSlot (full body) or UpperBody (the bow clips, layered over locomotion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	FName SlotName = FName(TEXT("DefaultSlot"));

	/** ANS_HitWindow: when the swing sweeps for its target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float HitStart = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float HitEnd = -1.f;

	/** ANS_ComboWindow: when the next strike may cut this one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float ComboStart = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float ComboEnd = -1.f;

	/** A motion warping window from the start to here (skew warp to WarpTargetName). Below 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float WarpEnd = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	FName WarpTargetName = FName(TEXT("CombatTarget"));

	/** Where in the sequence the montage starts, seconds (skips a clip's long wind-up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage", meta = (ClampMin = "0.0"))
	float ClipStartSeconds = 0.f;

	/** Where in the sequence the montage ends, seconds. At or below ClipStartSeconds: the sequence's end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	float ClipEndSeconds = -1.f;

	/** The segment's play rate, baked into the montage (a 1.6 here makes a 0.8 s stretch a 0.5 s montage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage", meta = (ClampMin = "0.05"))
	float PlayRate = 1.f;

	/** The one section loops (BowAimIdle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	bool bLoop = false;

	/** No auto blend-out: the last frame holds until the montage is stopped (Knockdown). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage")
	bool bHoldLastFrame = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage", meta = (ClampMin = "0.0"))
	float BlendInSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Montage", meta = (ClampMin = "0.0"))
	float BlendOutSeconds = 0.2f;
};

/**
 * Builds the AM_<Role>_<Variant> combat montages headless from retargeted sequences
 * (Tools/Editor/import_combat_anims.py calls it), through the engine's own montage factory and
 * UAnimationBlueprintLibrary's notify calls. Editor builds only; in a cooked game it logs an error
 * and returns null.
 */
UCLASS()
class HAWKEYE_API UHawkeyeCombatMontageBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or relays out in place) the montage at PackageName (e.g.
	 * "/Game/Characters/UEFN_Mannequin/Animations/Combat/AM_Light1_Jab") over Sequence, with Spec's
	 * slot, notify windows, warp window, loop and blends. The slot is registered on the skeleton. An
	 * existing montage over a different sequence is refused (null); delete it first. Returns the
	 * montage dirty and unsaved; the caller saves it and the skeleton.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimMontage* BuildCombatMontage(UAnimSequence* Sequence, const FString& PackageName,
		const FHawkeyeCombatMontageSpec& Spec);

	/**
	 * "slot=DefaultSlot hit=0.18-0.30 combo=0.35-0.60 warp=0.00-0.18 loop=0 hold=0 rootmotion=1
	 * clip=0.60-1.70x1.70 length=0.65" for the verify scripts and the import log. Times in the montage's
	 * seconds (what the game sees at rate 1); clip is the stretch of the sequence and its rate; a missing
	 * window reads "-".
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static FString DescribeMontage(UAnimMontage* Montage);

	/**
	 * Where Bone is at Time in Sequence's raw data, component space (cm): each bone's own track, or
	 * its reference pose where it has none, walked up the skeleton's hierarchy by name. The import
	 * self-test reads poses this way because it does not depend on how the evaluated pose indexes bones.
	 * bRootLocked puts the skeleton's root bone at its reference pose instead of its track, which is
	 * where the game holds it (root motion extracted, or the root locked): a clip that parks the
	 * pelvis's height on the root reads as standing without it and as sitting on the floor with it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static FVector GetRawBoneLocation(UAnimSequence* Sequence, FName Bone, float Time, bool bRootLocked = false);

	/** Bone names of Sequence's skeleton, in hierarchy order. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static TArray<FName> GetSkeletonBoneNames(UAnimSequence* Sequence);

	/**
	 * Makes Pose additive in mesh space (rotation offsets) against BasePose's first frame, what an aim
	 * offset's samples must be. True when it changed anything; false when it already was, or on null.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static bool MakeMeshSpaceAdditive(UAnimSequence* Pose, UAnimSequence* BasePose);

	/**
	 * Creates (or refills in place) the aim offset blend space at PackageName on Poses' skeleton, laid out
	 * as Layout (a source aim offset, e.g. Sparrow's AO_idle): its axes, ranges and grid, and one sample per
	 * Layout sample whose animation is LayoutPoses[i], played by Poses[i] (the same pose retargeted). Layout
	 * samples with no counterpart are left out. The poses must already be mesh-space additive. Returns the
	 * blend space dirty and unsaved, or null (reasons logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UBlendSpace* BuildAimOffset(const FString& PackageName, UBlendSpace* Layout, const TArray<UAnimSequence*>& LayoutPoses,
		const TArray<UAnimSequence*>& Poses);

	/** "Yaw -90..90 (4), Pitch -90..90 (4); 9 samples: A_X (0, 0), ..." for the import log. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static FString DescribeBlendSpace(UBlendSpace* BlendSpace);

	/** Animation's first window of NotifyClass: start and end in seconds. False when it has none. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static bool GetNotifyWindow(UAnimSequenceBase* Animation, TSubclassOf<UAnimNotifyState> NotifyClass, float& Start,
		float& End);
};
