#pragma once

#include "CoreMinimal.h"
#include "World/ThugCharacter.h"
#include "CrimeTypes.generated.h"

/** What a street crime asks of the player. */
UENUM(BlueprintType)
enum class ECrimeType : uint8
{
	/** Two thugs beating a civilian: stop them before the victim takes too many hits. */
	Mugging,
	/** Three thugs at a storefront, one running off with the loot: stop him before he gets away. */
	Robbery,
	/** Four thugs coming straight at Kate. */
	Ambush,
	/** Two thugs and an archer on a roof. */
	Rooftop
};

/** What a stopped crime hands out. */
UENUM(BlueprintType)
enum class ECrimeReward : uint8
{
	None,
	/** RewardArrows standard arrows into the quiver, up to its cap. */
	ArrowRefill
};

/** How a crime ended. */
UENUM(BlueprintType)
enum class ECrimeEndReason : uint8
{
	/** Every thug down, or the loot recovered. */
	Completed,
	/** The mugging's victim took VictimHitsToFail hits first. */
	VictimLost,
	/** The robbery's runner reached the escape point with the loot. */
	LootEscaped,
	/** The player went too far away, went down, or the world ended. */
	Abandoned
};

/** One line of a crime's roster: Count thugs of one weapon, spawned from ThugClass. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCrimeRosterEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Roster")
	EThugWeapon Weapon = EThugWeapon::Fists;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Roster", meta = (ClampMin = "0"))
	int32 Count = 1;

	/** BP_Thug, BP_Archer or BP_Thug_Heavy. Empty spawns the bare AThugCharacter (tests). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Roster")
	TSoftClassPtr<AThugCharacter> ThugClass;
};

/** A place a crime could start, as the spot picker sees it. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCrimeSpotCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Crime|Spot")
	FVector Location = FVector::ZeroVector;

	/** The player's camera has a clear line to it. Spots out of sight are preferred. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Spot")
	bool bInSight = false;
};

/** What a crime's frame looked like, handed to UCrimeTracker::Advance by the subsystem. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCrimeTickState
{
	GENERATED_BODY()

	/** Roster thugs dead or limp. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	int32 ThugsDown = 0;

	/** Mugging: thugs standing over the victim and not busy with the player. The victim's clock runs while any are. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	int32 ThugsOnVictim = 0;

	/** Robbery: the player has picked the dropped loot up. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	bool bLootRecovered = false;

	/** Robbery: a thug carrying the loot is at the escape point. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	bool bLootEscaped = false;

	/** The player's distance from the crime's spot on the ground plane, cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	float PlayerDistance = 0.f;

	/** The player is down or gone. */
	UPROPERTY(BlueprintReadWrite, Category = "Crime|Tick")
	bool bPlayerOut = false;
};

/** How a crime went, for the toast and the tests. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCrimeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	FName CrimeId;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	ECrimeType Type = ECrimeType::Mugging;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	ECrimeEndReason Reason = ECrimeEndReason::Abandoned;

	/** Seconds from the start to the end, pauses left out. */
	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	float Seconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	int32 ThugsDown = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	int32 ThugsTotal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	int32 VictimHits = 0;

	/** Arrows the reward actually put in the quiver (0 when it was full, or no reward). */
	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	int32 ArrowsGiven = 0;

	/** Completed crimes of this type, this one counted. */
	UPROPERTY(BlueprintReadOnly, Category = "Crime|Result")
	int32 Completions = 0;

	bool IsCompleted() const { return Reason == ECrimeEndReason::Completed; }
};

/**
 * The roaming clock between crimes: it runs only
 * while the player is roaming, and a crime is due once it reaches Interval. Pure, so the timing is
 * testable without a world.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCrimeScheduleClock
{
	GENERATED_BODY()

	/** Seconds of roaming before the next crime is due. */
	UPROPERTY(BlueprintReadOnly, Category = "Crime|Schedule")
	float Interval = 120.f;

	/** Seconds of roaming so far. */
	UPROPERTY(BlueprintReadOnly, Category = "Crime|Schedule")
	float Elapsed = 0.f;

	/** Starts over with a new interval. */
	void Restart(float NewInterval)
	{
		Interval = FMath::Max(NewInterval, 0.f);
		Elapsed = 0.f;
	}

	/** Runs the clock on when bRoaming. True once a crime is due (and stays due until Restart). */
	bool Advance(float DeltaSeconds, bool bRoaming)
	{
		if (bRoaming)
		{
			Elapsed += FMath::Max(DeltaSeconds, 0.f);
		}
		return IsDue();
	}

	bool IsDue() const { return Elapsed + KINDA_SMALL_NUMBER >= Interval; }
};
