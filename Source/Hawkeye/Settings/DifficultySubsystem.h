#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Settings/HawkeyeSettings.h"
#include "DifficultySubsystem.generated.h"

/** Every number the difficulty moves. The values per level are in UDifficultySubsystem::GetTableValue. */
UENUM(BlueprintType)
enum class EDifficultyStat : uint8
{
	/** Multiplier on a thug's melee swing and a gunner's shot: 0.6 / 1.0 / 1.4. */
	ThugDamage,
	/** Multiplier on a thug's max health, at spawn: 0.8 / 1.0 / 1.2. */
	ThugHealth,
	/** An archer's full draw, seconds: 1.5 / 1.2 / 1.0. */
	ArcherDrawSeconds,
	/** Seconds added to the parry window: +0.15 (a tap that early still counts) / 0 / -0.1 (not the first 0.1 s). */
	ParryWindowSeconds,
	/** Seconds without damage before Kate's health comes back: 3 / 5 / 8. */
	RegenDelaySeconds,
	/** Multiplier on fall damage: 0.5 / 1.0 / 1.0. */
	FallDamage,
	/** Arrows added to every trick arrow's cap: 2 / 0 / 0. */
	TrickArrowCapBonus,
	/** How late an archer or gunner sees Kate, seconds: 0.4 / 0.32 / 0.25. */
	RangedTrackingLagSeconds,
	/** Multiplier on an archer's or gunner's scatter cone: 1.4 / 1.0 / 0.7. */
	RangedConeScale,
	/** The shortest and longest gap between an archer's release and his next draw, seconds: 3.0-4.0 / 1.5-2.5 / 1.2-1.8. */
	ArcherShotGapMinSeconds,
	ArcherShotGapMaxSeconds,
	/** Multiplier on every cone, pull and bend of the bow's aim assist: 1.25 / 1.0 / 0.5. */
	AimAssistScale,
	/** Thugs who may be swinging at one target at once (the melee attack tokens): 1 / 1 / 2. */
	MeleeTokens,
	/** Thugs who may be shooting or drawing on one target at once: 1 / 1 / 2. */
	RangedTokens,
	/** After a token is given back, seconds before its budget grants again: 0.9 / 0.6 / 0.4. */
	TokenGapSeconds,
	/** The shortest telegraph of an attack granted from off her screen, seconds: 1.2 / 1.0 / 0.8. */
	OffScreenTelegraphSeconds
};

/**
 * The difficulty: one setting (FHawkeyeSettings::Difficulty, persisted with the rest) and one table of
 * what it changes, so tuning lives in one place. Code asks GetScalarFor(this, Stat) where the number is
 * used; with no game instance (automation worlds) that answers from the settings test override, or
 * Normal.
 *
 * A -Difficulty=Story|Normal|Hard command line argument overrides the stored choice without saving it,
 * so the scripted fights can be rerun at each level.
 */
UCLASS()
class HAWKEYE_API UDifficultySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDifficultySubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem interface

	/** The difficulty in force. */
	UFUNCTION(BlueprintPure, Category = "Difficulty")
	EHawkeyeDifficulty GetDifficulty() const;

	/** Stat's value at the difficulty in force. */
	UFUNCTION(BlueprintPure, Category = "Difficulty")
	float GetScalar(EDifficultyStat Stat) const;

	/** The table: Stat's value at Difficulty. */
	UFUNCTION(BlueprintPure, Category = "Difficulty")
	static float GetTableValue(EHawkeyeDifficulty Difficulty, EDifficultyStat Stat);

	/** Stat's value for WorldContextObject's game (UHawkeyeSettingsSubsystem::GetCurrentSettings). */
	static float GetScalarFor(const UObject* WorldContextObject, EDifficultyStat Stat);

	/** The command line's -Difficulty= if it names a level, else Stored. */
	static EHawkeyeDifficulty ResolveDifficulty(EHawkeyeDifficulty Stored);

	/** "Story", "Normal" or "Hard" (case does not matter) into OutDifficulty. False for anything else. */
	static bool ParseDifficulty(const FString& Text, EHawkeyeDifficulty& OutDifficulty);

	/** The player-facing name: "Story", "Normal", "Hard". */
	UFUNCTION(BlueprintPure, Category = "Difficulty")
	static FText GetDifficultyName(EHawkeyeDifficulty Difficulty);

	/** One line for the prompt and the settings row: what the level does. */
	UFUNCTION(BlueprintPure, Category = "Difficulty")
	static FText GetDifficultyBlurb(EHawkeyeDifficulty Difficulty);
};
