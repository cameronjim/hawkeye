#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Crime/CrimeTypes.h"
#include "CrimeDefinition.generated.h"

class ACivilian;
class ACrimeLoot;

/**
 * One kind of repeatable street crime: who is in it, how it fails, what stopping it pays. The crimes
 * spawn while the player roams (UCrimeSubsystem) at the City_CrimeSpot_ actors generate_city.py
 * places, each of which lists the definitions that may happen there. Tools/Editor/create_crimes.py
 * writes one per type under /Game/Crimes; names are bracketed placeholders until written.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UCrimeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id, lowercase snake_case ("mugging"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime")
	FName Id;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime")
	ECrimeType Type = ECrimeType::Mugging;

	/** What the HUD line and the toast call it. A bracketed placeholder ("[Crime: mugging]") until written. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime")
	FText Name;

	/** The thugs, by weapon. Mugging and Robbery stand them round the victim or the loot; the others round the spot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime")
	TArray<FCrimeRosterEntry> Roster;

	/** How far from the spot the roster stands, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime", meta = (ClampMin = "50.0"))
	float Radius = 250.f;

	/**
	 * The victim variants' clock, seconds of the thugs standing over the victim. Mugging: the victim's
	 * VictimHitsToFail hits are spread evenly over it, so the last lands at TimeToFailSeconds. Robbery:
	 * the runner sets off with the loot after this long even if nobody has come.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime", meta = (ClampMin = "1.0"))
	float TimeToFailSeconds = 45.f;

	/** Mugging: hits the victim takes before the crime fails. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Mugging", meta = (ClampMin = "1"))
	int32 VictimHitsToFail = 3;

	/** Mugging: the civilian. Empty spawns the bare ACivilian (tests). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Mugging")
	TSoftClassPtr<ACivilian> VictimClass;

	/** Robbery: the loot the runner carries. Empty spawns ACrimeLoot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery")
	TSoftClassPtr<ACrimeLoot> LootClass;

	/** Robbery: how far off the escape point is when the spot has none of its own, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery", meta = (ClampMin = "0.0"))
	float EscapeDistance = 6000.f;

	/** Robbery: the runner within this of the escape point with the loot has got away, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery", meta = (ClampMin = "10.0"))
	float EscapeRadius = 250.f;

	/** Robbery: the runner's speed with the loot, cm/s (Kate runs at 500). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery", meta = (ClampMin = "50.0"))
	float RunnerSpeed = 380.f;

	/** Robbery: the runner sets off once the player is this close, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery", meta = (ClampMin = "0.0"))
	float RunnerStartDistance = 2000.f;

	/** The player within this of dropped loot picks it up, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Robbery", meta = (ClampMin = "10.0"))
	float LootPickupRadius = 180.f;

	/** Ambush: the roster knows where she is from the start and comes for her. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime")
	bool bAlertOnStart = false;

	/** The player further than this from the spot abandons the crime, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime", meta = (ClampMin = "100.0"))
	float AbandonDistance = 15000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Reward")
	ECrimeReward Reward = ECrimeReward::ArrowRefill;

	/** Standard arrows a stopped crime puts in the quiver, up to its cap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Reward", meta = (ClampMin = "0"))
	int32 RewardArrows = 5;

	/** After a stopped crime its thugs go this long after the end, once the player is DespawnFarDistance away, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Cleanup", meta = (ClampMin = "0.0"))
	float DespawnAfterCompleteSeconds = 60.f;

	/** After a failed crime its thugs go this long after the end, wherever the player is, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Cleanup", meta = (ClampMin = "0.0"))
	float DespawnAfterFailSeconds = 20.f;

	/** "Far from the player" for the stopped crime's cleanup, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crime|Cleanup", meta = (ClampMin = "0.0"))
	float DespawnFarDistance = 4000.f;

	/** Every roster line's Count, added up. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	int32 GetThugCount() const;

	/** Name, or "[Crime: <type>]" when a designer left it empty. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	FText GetDisplayName() const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
