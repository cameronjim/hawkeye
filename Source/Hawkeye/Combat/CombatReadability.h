#pragma once

#include "CoreMinimal.h"
#include "CombatReadability.generated.h"

class AActor;
class AThugCharacter;

/**
 * The marker on the thug the melee assist picked (claude-docs/gameplay-semantics.md, "Combat
 * readability"): full while her swing at him runs, then fading out over FadeSeconds. A new pick
 * replaces it; a dead or removed target clears it. Pure but for the weak pointer; tested.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeTargetMarker
{
	GENERATED_BODY()

	/** Seconds the marker takes to fade once the swing is over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readability", meta = (ClampMin = "0.0"))
	float FadeSeconds = 0.3f;

	/** The assist picked Target for a swing that started: the marker is on him, full. Null clears it. */
	void Mark(AActor* Target);

	/** Moves the fade on; while bSwingActive it holds full. */
	void Advance(float DeltaSeconds, bool bSwingActive);

	void Clear();

	/** The marked thug, or null. */
	AActor* GetTarget() const { return Target.Get(); }

	/** 1 while the swing runs, falling to 0 over FadeSeconds after it; 0 with nobody marked. */
	float GetAlpha() const;

private:
	TWeakObjectPtr<AActor> Target;
	float FadeElapsed = 0.f;
	bool bFading = false;
};

/**
 * The fight camera (gameplay-semantics.md, "Combat readability"): with MinThugs or more alerted thugs
 * within Radius of her (and any others within FarRadius, once one is within Radius), the hip boom lengthens by ExtraArmLength and the lens tips down PitchDrop,
 * blended in over BlendInSeconds and back out over BlendOutSeconds.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeFightCameraSettings
{
	GENERATED_BODY()

	/** An alerted thug this close (flat), cm, starts the count... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float Radius = 900.f;

	/** ...and once one is that close, alerted thugs up to this far (flat), cm, count too (a gunner hanging back). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float FarRadius = 1500.f;

	/** ...and at most this far above or below her, cm, count as the fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float MaxHeight = 300.f;

	/** This many of them make a fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "1"))
	int32 MinThugs = 2;

	/** How much longer the hip boom gets, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float ExtraArmLength = 70.f;

	/** How far the lens tips down, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float PitchDrop = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float BlendInSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Fight", meta = (ClampMin = "0.0"))
	float BlendOutSeconds = 1.f;
};

/** How the telegraph glyph over a winding-up thug is drawn this frame. */
struct HAWKEYE_API FHawkeyeTelegraphLook
{
	/** Size against the glyph's base size: grows over the wind-up, with the pulse on top. */
	float Scale = 1.f;
	/** Opacity: the pulse dips it a little. */
	float Alpha = 1.f;
};

/** The combat readability rules that are only numbers. Pure; the tests check them without a fight. */
namespace HawkeyeCombatReadability
{
	/** Of Positions (alerted thugs), how many are within Radius of Origin (flat) and MaxHeight above or below. */
	HAWKEYE_API int32 CountEngaged(const FVector& Origin, const TArray<FVector>& Positions, float Radius, float MaxHeight, float FarRadius = 0.f);

	/** True when Engaged thugs make a fight under Settings. */
	HAWKEYE_API bool IsFight(int32 Engaged, const FHawkeyeFightCameraSettings& Settings);

	/**
	 * The fight camera's blend moved on by DeltaSeconds toward 1 (bFight) or 0, at a constant rate so it
	 * takes exactly BlendInSeconds in and BlendOutSeconds out.
	 */
	HAWKEYE_API float AdvanceFightAlpha(float Alpha, bool bFight, float DeltaSeconds, const FHawkeyeFightCameraSettings& Settings);

	/**
	 * What the fight camera adds at Alpha (smoothstepped), cm of arm and degrees of pitch drop, given how far
	 * the aim blend is in (the bow's camera is left alone) and how far a finisher's push-in is in (the push
	 * reads from the plain hip camera, so the fight's extra gives way to it).
	 */
	HAWKEYE_API void ComputeFightOffsets(float Alpha, float AimAlpha, float PushAlpha, const FHawkeyeFightCameraSettings& Settings,
		float& OutExtraArm, float& OutPitchDrop);

	/**
	 * The heavy's camera punch: the lens goes Distance cm in toward her and back over Seconds, a half sine,
	 * times ShakeScale (0.3 with reduce camera shake). 0 outside the punch.
	 */
	HAWKEYE_API float ComputeCameraPunch(float Elapsed, float Seconds, float Distance, float ShakeScale);

	/**
	 * The telegraph glyph Elapsed seconds into a WindupSeconds telegraph: from BaseScale growing to
	 * BaseScale * 1.3 by the hit, pulsing PulseHz times a second by up to PulseAmount (times FlashScale:
	 * reduce flashing keeps the growth and drops most of the pulse).
	 */
	HAWKEYE_API FHawkeyeTelegraphLook ComputeTelegraphLook(float Elapsed, float WindupSeconds, float FlashScale = 1.f,
		float PulseHz = 5.f, float PulseAmount = 0.12f);

	/**
	 * Where the bottom of what stacks over a thug's health bar goes on screen (y down, px): the stack's bottom is
	 * AnchorY (his head bone plus a little, projected). With no bar that is where the glyph (or the parry line
	 * under a telegraph) sits; with the bar showing (BarHeight px tall, its edge included, bottom on AnchorY) it
	 * sits Gap px over the bar's top, so the "!" or "?" never covers the bar and the whole stack stays just over
	 * his head.
	 */
	HAWKEYE_API float ComputeGlyphBottom(float AnchorY, bool bBarShown, float BarHeight, float Gap = 4.f);

	/**
	 * Whether Thug is telegraphing a strike the telegraph glyph is for: a melee swing or bash in its wind-up,
	 * or a gunner's raised pistol within GunnerRange of Viewer (further off, his glint is the tell). Never an
	 * archer (the purple glint). Gives the seconds into it and its length.
	 */
	HAWKEYE_API bool GetStrikeTelegraph(const AThugCharacter* Thug, const FVector& Viewer, float GunnerRange, float& OutElapsed,
		float& OutWindupSeconds);
}
