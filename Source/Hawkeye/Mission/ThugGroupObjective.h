#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ThugGroupObjective.generated.h"

class AThugCharacter;
class UHealthComponent;

/**
 * Completes ObjectiveId when every thug carrying GroupTag is dead. Drop one in a level next to
 * the fight, set the tag on the thugs it counts, and the mission gets "clear the roof" without a
 * line of Blueprint. CH01's `clear_roof` counts the two thugs tagged RoofPair.
 *
 * A takedown counts: it kills through the health component like everything else.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AThugGroupObjective : public AActor
{
	GENERATED_BODY()

public:
	AThugGroupObjective();

	/** The mission objective completed when the group is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName ObjectiveId;

	/** Actor tag every thug in the group carries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName GroupTag;

	/**
	 * Finds every AThugCharacter in the world tagged GroupTag and starts counting their deaths.
	 * Runs at BeginPlay; safe to call again (thugs already counted are not counted twice).
	 * Returns how many thugs the group now has.
	 */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	int32 RegisterGroup();

	/** Adds one thug by hand. False if he was already in the group. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	bool RegisterThug(AThugCharacter* Thug);

	UFUNCTION(BlueprintPure, Category = "Objective")
	int32 GetGroupSize() const { return Members.Num(); }

	/** Members still alive. */
	UFUNCTION(BlueprintPure, Category = "Objective")
	int32 GetAliveCount() const;

	/** True once every member is down and the objective has been completed. */
	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsCleared() const { return bCleared; }

protected:
	/** Also registers the actor's location as ObjectiveId's marker point. */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleMemberDeath(UHealthComponent* Health, AActor* Killer);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AThugCharacter>> Members;

	bool bCleared = false;
};
