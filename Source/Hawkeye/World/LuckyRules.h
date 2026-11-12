#pragma once

#include "CoreMinimal.h"
#include "LuckyRules.generated.h"

/** What Lucky is doing: which clip plays and whether he moves (gameplay-semantics.md, "Lucky"). */
UENUM(BlueprintType)
enum class ELuckyState : uint8
{
	Idle,
	Walk,
	Trot,
	Sit
};

/** The distances and times Lucky follows Kate by. Every number is a property on ALuckyCharacter. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FLuckyFollowSettings
{
	GENERATED_BODY()

	/** Kate further than this (cm, along the ground) and Lucky does not follow: he stays where he is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float NoticeRadius = 1200.f;

	/** Kate more than this far above or below him (cm, a roof, a fire escape) and she is out of his reach. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float MaxHeightGap = 250.f;

	/** A standing or sitting Lucky sets off after her once she is further than this (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float StartFollowDistance = 350.f;

	/** A following Lucky stops once he is this close (cm). Under StartFollowDistance, so he does not stutter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float StopDistance = 200.f;

	/** Following and further than this (cm): he trots; nearer, he walks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float TrotDistance = 500.f;

	/** Seconds she has to stand still near him before he sits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky", meta = (ClampMin = "0.0"))
	float SitDelay = 3.f;
};

/** Lucky's state choice and clip speeds: pure functions, tested in Hawkeye.Lucky.*. */
struct HAWKEYE_API FLuckyRules
{
	/**
	 * The state for this moment. Distance is along the ground (cm), HeightGap the vertical gap, KateStillSeconds how
	 * long she has stood still. Hysteresis: he sets off past StartFollowDistance and stops inside StopDistance.
	 */
	static ELuckyState ChooseState(const FLuckyFollowSettings& Settings, ELuckyState Current, float Distance,
		float HeightGap, float KateStillSeconds);

	/** True for the states that move him. */
	static bool IsMoving(ELuckyState State)
	{
		return State == ELuckyState::Walk || State == ELuckyState::Trot;
	}

	/**
	 * The clip's play rate for a ground speed (cm/s) so the feet do not slide: speed over the clip's own speed,
	 * clamped to [MinRate, MaxRate]. 1 for a clip with no speed (idle, sit).
	 */
	static float PlayRateFor(float Speed, float ClipSpeed, float MinRate, float MaxRate);
};
