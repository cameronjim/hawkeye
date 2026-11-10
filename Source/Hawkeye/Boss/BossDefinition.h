#pragma once

#include "CoreMinimal.h"
#include "Boss/BossTypes.h"
#include "Engine/DataAsset.h"
#include "BossDefinition.generated.h"

class AThugCharacter;

/** One phase of a boss fight as data: the phase component's rule, plus what the fight does while it runs. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossPhaseDefinition
{
	GENERATED_BODY()

	/** Name, threshold (a fraction of max health), entry, invulnerability, transition: UBossPhaseComponent's rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	FBossPhase Phase;

	/** Completed when this phase ends (the next one opens, or for the last, after the boss is down). None: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	FName ObjectiveId;

	/** The boss fights (thinking on, alerted, his move set). Off, he stands at his spot, facing Kate, and comments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	bool bBossFights = true;

	/** Spawned when the phase starts, alerted on Kate. Every one down opens a next phase whose Entry is CrewDown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	TArray<FBossCrewMember> Crew;

	/** The arena's hazard comes loose when this phase starts and runs until the boss is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	bool bHazardActive = false;

	/** DT_Dialogue rows spoken as the phase starts, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	TArray<FName> StartLines;

	/** Spoken as the phase ends (for the last phase: as the boss goes down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	TArray<FName> EndLines;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Lines")
	TArray<FBossBark> Barks;

	/** Arrows added to Kate's quiver as the phase starts (the chapter's unlock), with their line on first use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase")
	TArray<FHawkeyeArrowGrant> ArrowGrants;
};

/**
 * A boss and his fight (claude-docs/gameplay-semantics.md, "Bosses"): who he is, how he fights, the phases with their
 * thresholds, crews, hazard and lines, and the roof it happens on. A chapter names one on UMissionDefinition::Boss and
 * UBossFightSubsystem runs it. A second boss is a second asset: DA_Boss_<chapter>_<name>, written by a script.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UBossDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	FText BossName;

	/** His body: a thug class with his look (BP_Boss_<Name>). Empty: AThugCharacter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	TSoftClassPtr<AThugCharacter> BossClass;

	/** Max health on Normal; the difficulty's thug health scale applies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "1.0"))
	float MaxHealth = 400.f;

	/** He is drawn (and his capsule is) this much bigger than a thug. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float BodyScale = 1.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	FBossMoveSet Moves;

	/** In order; the first is open from the start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	TArray<FBossPhaseDefinition> Phases;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Arena")
	FBossArena Arena;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Arena")
	FBossHazardTuning Hazard;

	/** Completed when Kate stands on the arena's roof; the fight's lines start then. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Flow")
	FName MeetObjective;

	/** DT_DialogueSequences' sequence played when she arrives. The fight starts when it is over. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Flow")
	FName PreFightSequence;

	/** The longest the pre-fight lines hold the fight back, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Flow", meta = (ClampMin = "0.0"))
	float IntroMaxSeconds = 45.f;

	/** Played once he is down. The last phase's objective (and so the chapter) completes when it is over. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Flow")
	FName AfterSequence;

	/** The longest the after lines hold the chapter's end back, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Flow", meta = (ClampMin = "0.0"))
	float AfterMaxSeconds = 60.f;

	/** His lines are drafts (docs/chapters/ACT1-script.md) waiting for the owner's edit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	bool bDraftText = false;

	/** The phase component's rules, in order. */
	TArray<FBossPhase> GetPhaseRules() const;

	/** Each phase's objective, in order (None where a phase has none). */
	TArray<FName> GetPhaseObjectives() const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
