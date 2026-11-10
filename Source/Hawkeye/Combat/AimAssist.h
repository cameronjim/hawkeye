#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Settings/HawkeyeSettings.h"
#include "AimAssist.generated.h"

class AActor;
class UWorld;

/** The bow's aim assist at one setting and difficulty (gameplay-semantics.md, "Bow aim assist"). All zero when off. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeAimAssistTuning
{
	GENERATED_BODY()

	/** Half-angle round the view inside which a thug's chest slows and pulls the look, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float MagnetismConeDegrees = 0.f;

	/** Half-angle round the reticle the aim press snaps across, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float SnapConeDegrees = 0.f;

	/** The most a release's launch direction turns toward a thug, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float BendMaxDegrees = 0.f;

	/** The look rate right on his chest, pad and mouse (1 is no slowdown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float PadSlowdown = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float MouseSlowdown = 1.f;

	/** How fast the view is pulled toward his chest near the centre of the cone, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float PadPullDegreesPerSecond = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim assist")
	float MousePullDegreesPerSecond = 0.f;

	bool IsOff() const { return MagnetismConeDegrees <= 0.f && SnapConeDegrees <= 0.f && BendMaxDegrees <= 0.f; }
};

/** One thug the assist can aim at: where his chest is and the line his body stands on. */
struct HAWKEYE_API FHawkeyeAimAssistCandidate
{
	TWeakObjectPtr<AActor> Actor;
	FVector Chest = FVector::ZeroVector;
	FVector Feet = FVector::ZeroVector;
	FVector Head = FVector::ZeroVector;
	float Radius = 34.f;
	FVector Velocity = FVector::ZeroVector;
};

/**
 * The bow's aim assist rules (gameplay-semantics.md, "Bow aim assist"): the tuning per setting and
 * difficulty, the cone, the magnetism's slowdown and pull, the aim press's snap and the release's bend.
 * The geometry is pure; GatherCandidates is the one world query.
 */
UCLASS()
class HAWKEYE_API UHawkeyeAimAssist : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The tuning for Level at Difficulty: Normal 4 / 12 / 2 degrees, Strong 7 / 12 / 4, times the difficulty's scale. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim assist")
	static FHawkeyeAimAssistTuning GetTuning(EHawkeyeAimAssist Level, EHawkeyeDifficulty Difficulty);

	/** "Off", "Normal", "Strong". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim assist")
	static FText GetLevelName(EHawkeyeAimAssist Level);

	/** The tuning for WorldContextObject's settings (UHawkeyeSettingsSubsystem::GetCurrentSettings). */
	static FHawkeyeAimAssistTuning GetTuningFor(const UObject* WorldContextObject);

	/** Degrees between Direction from From and the way to Point. */
	static float AngleToPoint(const FVector& From, const FVector& Direction, const FVector& Point);

	/** Index of the candidate whose chest is nearest Direction (by angle) within ConeDegrees and MaxRange; INDEX_NONE if none. */
	static int32 FindBestIndex(const FVector& From, const FVector& Direction, const TArray<FHawkeyeAimAssistCandidate>& Candidates,
		float ConeDegrees, float MaxRange);

	/** True when the line from From along Direction passes within Candidate's radius of his feet-to-head line, in front. */
	static bool IsLineOnBody(const FVector& From, const FVector& Direction, const FHawkeyeAimAssistCandidate& Candidate);

	/** The look input's scale Angle degrees off his chest: Slowdown at 0 easing to 1 at ConeDegrees and beyond. */
	static float ComputeLookScale(float AngleDegrees, float ConeDegrees, float Slowdown);

	/**
	 * The view after DeltaSeconds of pull toward Point: at PullDegreesPerSecond times how near the centre of
	 * ConeDegrees it is, never past the point. ViewRotation unchanged outside the cone.
	 */
	static FRotator ComputePull(const FRotator& ViewRotation, const FVector& ViewLocation, const FVector& Point, float ConeDegrees,
		float PullDegreesPerSecond, float DeltaSeconds);

	/** The view rotation that looks from ViewLocation straight at Point (no roll). */
	static FRotator ComputeSnapRotation(const FVector& ViewLocation, const FVector& Point);

	/** Aim turned toward Wanted by at most MaxDegrees (all of the way when they are closer than that). */
	static FVector BendToward(const FVector& Aim, const FVector& Wanted, float MaxDegrees);

	/**
	 * Every living, standing thug within MaxRange of From that From has a clear line to (Visibility, ignoring
	 * IgnoreActor and him), as candidates.
	 */
	static void GatherCandidates(const UWorld* World, const FVector& From, float MaxRange, const AActor* IgnoreActor,
		TArray<FHawkeyeAimAssistCandidate>& OutCandidates);

	/** How far the assist looks, cm. */
	static constexpr float MaxRange = 6000.f;

	/** Chest height over the capsule centre, cm (the archers aim at the same). */
	static constexpr float ChestHeight = 30.f;

	/** The aim press's snap takes this long, s. */
	static constexpr float SnapSeconds = 0.1f;
};
