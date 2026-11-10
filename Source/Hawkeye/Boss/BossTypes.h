#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/MeleeComponent.h"
#include "World/ThugCharacter.h"
#include "BossTypes.generated.h"

class AThugCharacter;
class UMaterialInterface;
class UStaticMesh;

/** Where a boss fight is (claude-docs/gameplay-semantics.md, "Bosses"). */
UENUM(BlueprintType)
enum class EBossFightState : uint8
{
	/** No boss in this chapter, or it has not started. */
	Idle,
	/** The boss and his crew are on the roof, calm; the meet objective waits for Kate to arrive. */
	Approach,
	/** She is there: the pre-fight lines. Nobody fights until they end (or she hits someone). */
	Intro,
	/** A phase is running. */
	Fight,
	/** He is down: the after lines, then the last objective. */
	Defeated,
	/** Every objective the fight owns is complete. */
	Done
};

/** Moments a boss phase can have a line for. */
UENUM(BlueprintType)
enum class EBossBarkEvent : uint8
{
	/** The boss's swing lands on Kate. */
	BossHitsPlayer,
	/** Kate dodges while the boss is winding up or swinging within reach. */
	PlayerDodges,
	/** The boss goes winded after his combo (the finisher window opens). */
	Winded,
	/** Kate's finisher lands on the boss. */
	FinisherOnBoss,
	/** Kate parries anyone in the fight. */
	PlayerParries,
	/** One of the crew goes down. */
	CrewMemberDown,
	/** A crew member with a bat starts his first swing. */
	CrewSwings,
	/** Kate looses one of the phase's granted arrows. */
	GrantedArrowUsed
};

/** One line (or a few, spoken in order) for a moment of a phase. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossBark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	EBossBarkEvent Event = EBossBarkEvent::BossHitsPlayer;

	/** DT_Dialogue rows, spoken one after another (UDialogueSubsystem::PlayLine: lines speak during a fight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	TArray<FName> Lines;

	/** At most once a fight. Off, it may come again after CooldownSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	bool bOnce = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 10.f;
};

/** One of a phase's crew: what he is, what he holds and where he stands when the fight is set up. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossCrewMember
{
	GENERATED_BODY()

	/** Empty: AThugCharacter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Crew")
	TSoftClassPtr<AThugCharacter> ThugClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Crew")
	EThugWeapon Weapon = EThugWeapon::Fists;

	/** Feet, world cm (the arena is planned from the district, so its places are too). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Crew")
	FVector Location = FVector::ZeroVector;
};

/** The boss's own fighting: his combo, the winded window after it, and how he takes hits. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossMoveSet
{
	GENERATED_BODY()

	/** His swings in order, the combo; the next swing after the last starts it again. Long wind-ups: he telegraphs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves")
	TArray<FHawkeyeMeleeAttack> Combo;

	/** Swings started before he is winded (usually the combo's length). 0: never winded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0"))
	int32 WindedAfterSwings = 3;

	/** How long he stands there getting his breath back: no swings, the finisher open, hits land harder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0.0"))
	float WindedSeconds = 3.f;

	/** Damage he takes while winded, times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "1.0"))
	float WindedDamageScale = 1.5f;

	/** A finisher on him takes this fraction of his max health instead of killing him (it kills when it is enough). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FinisherDamageFraction = 0.15f;

	/** Melee damage above this staggers him (a thug's is 10): Kate's light strikes do not, so he swings through them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0.0"))
	float StaggerThreshold = 40.f;

	/** Kate's heavy strike does not knock him down (AThugCharacter::bResistsKnockdown); a blast or a bola still does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves")
	bool bResistsKnockdown = true;

	/** Seconds between one swing ending and the next starting (the thug's MeleeCooldownSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0.0"))
	float SwingCooldownSeconds = 0.6f;

	/** Walking speed while he fights, cm/s (the thug's RushSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Moves", meta = (ClampMin = "0.0"))
	float RushSpeed = 380.f;
};

/** The arena hazard's clock and its bite. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossHazardTuning
{
	GENERATED_BODY()

	/** Seconds after it comes loose before the first telegraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.0"))
	float FirstDelaySeconds = 1.5f;

	/** The warning before every sweep: the lane lights up and it shudders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.1"))
	float TelegraphSeconds = 1.5f;

	/** One sweep end to end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.1"))
	float SweepSeconds = 2.5f;

	/** At rest at the far end before the next telegraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.0"))
	float RestSeconds = 3.f;

	/** Damage of one hit, before the difficulty's thug melee damage scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	/** A second hit on the same victim waits this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.0"))
	float HitCooldownSeconds = 2.f;

	/** Half the lane's width: within this of the line through it, the hazard reaches her, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "1.0"))
	float HalfWidth = 160.f;

	/** How far along the sweep, either side of its body, a hit reaches, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "1.0"))
	float HalfLength = 150.f;

	/** A hit shoves her this hard out of the lane, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Hazard", meta = (ClampMin = "0.0"))
	float KnockbackSpeed = 700.f;
};

/** Where the hazard is in its cycle. */
UENUM(BlueprintType)
enum class EBossHazardPhase : uint8
{
	/** Not loose yet (tethered), or loose and waiting for its first telegraph. */
	Waiting,
	Telegraph,
	Sweep,
	Rest
};

/** The hazard's cycle at a moment: which part, how far through it, and which way it is going. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossHazardState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Hazard")
	EBossHazardPhase Phase = EBossHazardPhase::Waiting;

	/** 0..1 through the current part. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Hazard")
	float Alpha = 0.f;

	/** 0 at the path's start, 1 at its end: where the body is. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Hazard")
	float Along = 0.f;

	/** Sweeps completed before this moment. Even: the next sweep goes start to end; odd: back. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Hazard")
	int32 Sweeps = 0;
};

/** One greybox prop of the arena. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossArenaProp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Blocks Kate and the thugs. Off for things overhead (the string lights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	bool bBlocks = true;
};

/** A small coloured light (a bulb of the string lights, the speaker's glow). */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossArenaLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena", meta = (ClampMin = "0.0"))
	float Lumens = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena", meta = (ClampMin = "0.0"))
	float Radius = 600.f;
};

/** The roof the fight is on and what stands on it, planned from the district by a script. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossArena
{
	GENERATED_BODY()

	/** The OSM id of the roof, for the log and the scripts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FName RoofId;

	/** The roof's outline, world cm. Kate inside it and on it is "on the roof". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TArray<FVector2D> Outline;

	/** The roof's floor, world cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	float RoofZ = 0.f;

	/** The middle of the fight space (feet height), the objective marker, and where the fight centres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector Centre = FVector::ZeroVector;

	/** The biggest circle round Centre clear of the parapets, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena", meta = (ClampMin = "0.0"))
	float FightRadius = 600.f;

	/** Where the chapter's opening puts Kate (feet): on the street, in grapple range of ApproachAnchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector PlayerStart = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	float PlayerStartYaw = 0.f;

	/** The parapet anchor the street stand's grapple reaches (the generator places it; this is its marker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector ApproachAnchor = FVector::ZeroVector;

	/** Where the boss stands and watches, feet, and his yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector BossSpot = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	float BossYaw = 0.f;

	/** The hazard's sweep, start then end, feet height. It is tethered at the start until it comes loose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector HazardStart = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	FVector HazardEnd = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TSoftObjectPtr<UStaticMesh> HazardMesh;

	/** The lane's warning strip on the floor during a telegraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TSoftObjectPtr<UMaterialInterface> HazardLaneMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TArray<FBossArenaProp> Props;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Arena")
	TArray<FBossArenaLight> Lights;
};
