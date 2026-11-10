#pragma once

#include "CoreMinimal.h"
#include "HawkeyeMusicRules.generated.h"

/**
 * What the music is doing (claude-docs/gameplay-semantics.md, "Audio", music). Roam, Alert, Fight and
 * Duel are levels, in that order: a rise is immediate, a fall waits. Win and Downed stand outside them.
 */
UENUM(BlueprintType)
enum class EHawkeyeMusicState : uint8
{
	/** Nothing, or the pad at night and in a challenge. */
	Roam,
	/** One thug has seen Kate, or a crime is on nearby: the pulse. */
	Alert,
	/** Two or more alerted, or one alerted within 8 m: pulse and percussion. */
	Fight,
	/** An alerted archer or boss, or the heavy with two or more others alerted: pulse, percussion and the motif. */
	Duel,
	/** The last thug of the fight went down: the 2 s resolve, then Roam. */
	Win,
	/** Kate is downed: the drone. */
	Downed
};

/** What the music reads from the world each frame. Gathered by UHawkeyeMusicSubsystem::GatherInputs. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMusicInputs
{
	GENERATED_BODY()

	/** Thugs alerted to Kate and still standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	int32 AlertedStanding = 0;

	/** Distance to the nearest of them, cm; huge when there is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float NearestAlertedCm = UE_BIG_NUMBER;

	/** One of them is an archer or carries a boss phase component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bBossAlerted = false;

	/** One of them is the heavy (the shield). A duel only with HeavyDuelOthers or more others alerted beside him. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bHeavyAlerted = false;

	/** Thugs alerted at any point in this fight who are still up (they may have lost her since). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	int32 EngagedStanding = 0;

	/** Thugs alerted in this fight who are down: dead, limp or gone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	int32 EngagedDown = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bPlayerDowned = false;

	/** A street crime is on within CrimeNearbyCm of Kate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bCrimeNearby = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bChallengeRunning = false;

	/** The time of day in force is Night. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	bool bNight = true;
};

/** A gain per layer of MS_Music_Score, 0..1; the names match the MetaSound's inputs. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMusicLayers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Pad = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Pulse = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Perc = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Motif = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Drone = 0.f;
};

namespace HawkeyeMusic
{
	/** An alerted thug this close makes it a fight on his own. */
	static constexpr float FightRangeCm = 800.f;

	/** The heavy makes it a duel only with at least this many other thugs alerted beside him. */
	static constexpr int32 HeavyDuelOthers = 2;

	/** A crime on within this of Kate brings the pulse in before anyone has seen her. */
	static constexpr float CrimeNearbyCm = 4000.f;

	/** How long the level must be lower before it drops one step. */
	static constexpr float CooldownSeconds = 4.f;

	/** The win sting's length. */
	static constexpr float WinSeconds = 2.f;

	static constexpr float FadeInSeconds = 1.5f;
	static constexpr float FadeOutSeconds = 2.f;

	/** Into Win and Downed everything else clears faster, so the sting and the drone read clean. */
	static constexpr float FastFadeOutSeconds = 1.f;

	/** The pad's gain in Roam when it plays (night, or a challenge). */
	static constexpr float RoamPadGain = 0.5f;

	/** Roam 0, Alert 1, Fight 2, Duel 3; Win and Downed are not levels (-1). */
	HAWKEYE_API int32 GetLevel(EHawkeyeMusicState State);

	/** The level state for a level number, clamped to Roam..Duel. */
	HAWKEYE_API EHawkeyeMusicState FromLevel(int32 Level);

	/** What the world asks for right now, before any cooldown: Downed, Duel, Fight, Alert or Roam. */
	HAWKEYE_API EHawkeyeMusicState ComputeTarget(const FHawkeyeMusicInputs& Inputs);

	/** The layer gains a state holds. */
	HAWKEYE_API FHawkeyeMusicLayers GetLayersFor(EHawkeyeMusicState State, const FHawkeyeMusicInputs& Inputs);

	/** Every layer moved toward Target: up at 1/FadeInSeconds a second, down at 1/FadeOutSeconds (1/FastFadeOutSeconds when Fast). */
	HAWKEYE_API FHawkeyeMusicLayers AdvanceLayers(const FHawkeyeMusicLayers& Current, const FHawkeyeMusicLayers& Target,
		float DeltaSeconds, bool bFast);

	HAWKEYE_API const TCHAR* GetStateName(EHawkeyeMusicState State);
}

/**
 * The music's state machine: rises at once, falls one level after CooldownSeconds lower, Win when every
 * engaged thug is down, Downed while Kate is. Pure (no world, no sound), so the rules are tested headless.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMusicDirector
{
	GENERATED_BODY()

	/** Runs the rules on by DeltaSeconds. True when the state changed in this step. */
	bool Step(const FHawkeyeMusicInputs& Inputs, float DeltaSeconds);

	EHawkeyeMusicState GetState() const { return State; }

	/** Seconds the level has been lower than the state, toward CooldownSeconds. */
	float GetCooldown() const { return Cooldown; }

	/** Seconds of the win sting left. */
	float GetWinRemaining() const { return State == EHawkeyeMusicState::Win ? WinRemaining : 0.f; }

	/** The state the last changing Step came from. */
	EHawkeyeMusicState GetPreviousState() const { return Previous; }

	void Reset()
	{
		State = EHawkeyeMusicState::Roam;
		Previous = EHawkeyeMusicState::Roam;
		Cooldown = 0.f;
		WinRemaining = 0.f;
		bWinSpent = false;
	}

private:
	bool SetState(EHawkeyeMusicState NewState);

	EHawkeyeMusicState State = EHawkeyeMusicState::Roam;
	EHawkeyeMusicState Previous = EHawkeyeMusicState::Roam;
	float Cooldown = 0.f;
	float WinRemaining = 0.f;

	/** This fight's sting has played: not again until someone new is engaged or the fight is forgotten (nothing engaged). */
	bool bWinSpent = false;
};
