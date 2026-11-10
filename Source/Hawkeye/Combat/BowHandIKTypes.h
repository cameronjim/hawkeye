#pragma once

#include "CoreMinimal.h"
#include "BowHandIKTypes.generated.h"

/**
 * Where the hands go on a bow, measured in the aim frame: origin at AnchorBone (the head), X along
 * the aim, Y to the archer's right, Z up, in world centimetres. The bow hand reaches for the grip;
 * the string hand moves from the string beside the bow (no draw) back to the cheek (full draw).
 * Elbow hints pull each elbow the way a real archer's points: the bow arm's down and out, the draw
 * arm's out to the right and, at full draw, back behind the head in line with the arrow.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBowHandIKSettings
{
	GENERATED_BODY()

	/** Bone the aim frame is measured from. "head" on both mannequins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FName AnchorBone = FName(TEXT("head"));

	/** The bow hand (hand_l) on the grip, raised to the aim in front of the left shoulder, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector GripOffset = FVector(50.f, -10.f, -4.f);

	/** The string hand (hand_r) at no draw: on the string beside the bow, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringHandRestOffset = FVector(34.f, -2.f, -6.f);

	/** The string hand at full draw: fingers at the right cheek, the wrist just behind, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringHandFullOffset = FVector(-2.f, 10.f, -8.f);

	/** Where the bow arm's elbow points, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector BowElbowHint = FVector(20.f, -50.f, -40.f);

	/** Where the draw arm's elbow points at no draw, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringElbowHintRest = FVector(10.f, 45.f, -40.f);

	/** Where the draw arm's elbow points at full draw: behind, out to the right, near shoulder height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringElbowHintFull = FVector(-45.f, 40.f, -8.f);

	/** Seconds each hand takes to blend onto (or off) the bow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands", meta = (ClampMin = "0.0"))
	float BlendSeconds = 0.15f;

	/** Extra yaw the spine turns while the bow is up, so the bow shoulder leads, degrees. The neck turns it back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	float SideOnDegrees = 15.f;

	/** Largest turn of the spine toward the aim when the body faces elsewhere, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxAimTwistDegrees = 45.f;

	/** Socket (or bone) where the string hand holds the string. Falls back to hand_r. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FName StringHandSocket = FName(TEXT("palm_r_Socket"));

	/** Bone the bow attaches to when the bow definition's HandSocket is not on this skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FName GripFallbackBone = FName(TEXT("hand_l"));

	// --- Under a bow clip (gameplay-semantics.md, "Combat animation clips") ----------------------------

	/** How far the clip's string hand may be off the arrow line before the IK corrects it at all, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0"))
	float ClipCorrectionDeadZone = 3.f;

	/** How far off the line the correction reaches its most, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0"))
	float ClipCorrectionFullDistance = 15.f;

	/** The most the IK ever moves the clip's string hand toward the line: a correction, never an override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClipCorrectionMaxAlpha = 0.5f;

	/** The shortest draw the arrow line keeps behind the bow, cm (the string hand at the rest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0"))
	float ClipMinDrawLength = 10.f;

	/** The aim offset's yaw is held within this either side of the body, degrees (and within its asset's range). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAimOffsetYaw = 90.f;

	/** The aim offset's pitch is held within this up and down, degrees (and within its asset's range). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxAimOffsetPitch = 90.f;

	/**
	 * The aim offset's pitch input is an upward aim's pitch times this (a downward one's times
	 * AimOffsetPitchScaleDown), plus AimOffsetPitchBias, before the clamps (2026-09-29). Sparrow's up and down poses
	 * sit at +-90 on the axis but aim far less (and not alike), its centre pose holds the arrow 4 degrees low, and the
	 * string hand's correction takes back part of any change, so the plain pitch left the nocked arrow 6 degrees under
	 * at 30 up and 5 at 30 down. Fitted on Hawkeye.Screenshot.BowDraw's draw_1.00, aim_up_30 and aim_down_30 so a
	 * 30 degree aim gives a 30 degree arrow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float AimOffsetPitchScale = 1.57f;

	/** The same for an aim below level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float AimOffsetPitchScaleDown = 1.85f;

	/** Degrees added to the aim offset's pitch input after the scale (the centre pose's own droop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands|Clip", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float AimOffsetPitchBias = 5.f;
};

/** One frame of hand and elbow targets, in whatever space the aim frame was given in. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBowHandIKTargets
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bow|Hands")
	FVector BowHand = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Bow|Hands")
	FVector BowElbow = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringHand = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Bow|Hands")
	FVector StringElbow = FVector::ZeroVector;
};
