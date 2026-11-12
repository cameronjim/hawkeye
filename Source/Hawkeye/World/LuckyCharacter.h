#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "World/LuckyRules.h"
#include "LuckyCharacter.generated.h"

class UAnimSequence;
class USkeletalMesh;

/**
 * Lucky, Clint's golden retriever (gameplay-semantics.md, "Lucky"). Placed by generate_city.py near the PlayerStart.
 * Plays Idle; when Kate is within NoticeRadius he walks or trots after her along the navmesh and stops near her; when
 * she stands still near him he sits. No gameplay: nothing collides with him but the world, nothing targets him.
 *
 * The mesh and clips are soft references to the git-ignored import (import_bodies.py); a clone without them hides him
 * and turns his tick off.
 */
UCLASS()
class HAWKEYE_API ALuckyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ALuckyCharacter();

	/** How far and how fast he follows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky")
	FLuckyFollowSettings Follow;

	/** His body (SK_Lucky, faces +X, feet at its origin). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Look")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	/** Standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Look")
	TSoftObjectPtr<UAnimSequence> IdleClip;

	/** The walk, in place; WalkClipSpeed is how fast its feet travel (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Look")
	TSoftObjectPtr<UAnimSequence> WalkClip;

	/** The trot, in place; TrotClipSpeed is how fast its feet travel (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Look")
	TSoftObjectPtr<UAnimSequence> TrotClip;

	/** Sitting (a seated idle, looped). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Look")
	TSoftObjectPtr<UAnimSequence> SitClip;

	/** cm/s the walk clip's feet cover at play rate 1 (measured in Blender: about 30). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Speed", meta = (ClampMin = "1.0"))
	float WalkClipSpeed = 30.f;

	/** cm/s the trot clip's feet cover at play rate 1 (measured in Blender: about 92). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Speed", meta = (ClampMin = "1.0"))
	float TrotClipSpeed = 92.f;

	/** The walk's top play rate: his walking speed is WalkClipSpeed times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Speed", meta = (ClampMin = "0.1"))
	float WalkRate = 1.3f;

	/** The trot's top play rate: his trotting speed is TrotClipSpeed times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lucky|Speed", meta = (ClampMin = "0.1"))
	float TrotRate = 1.6f;

	/** What he is doing now. */
	UFUNCTION(BlueprintPure, Category = "Lucky")
	ELuckyState GetLuckyState() const { return State; }

	/** Stops him thinking (the screenshot tests stand him); he holds his current clip. */
	void SetThinkingEnabled(bool bEnabled);

	/** Forces a state and its clip now (tests and screenshots). */
	void SetLuckyState(ELuckyState NewState);

	/** The ground speed (cm/s) a state moves him at: 0 for Idle and Sit. */
	float SpeedFor(ELuckyState InState) const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Kate (the player's pawn) and how long she has stood still. */
	APawn* FindKate() const;
	void UpdateStillTime(const APawn* Kate, float DeltaSeconds);
	void ApplyMovement(APawn* Kate);
	void UpdateClipRate();
	UAnimSequence* ClipFor(ELuckyState InState) const;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;

	ELuckyState State = ELuckyState::Idle;
	float KateStillSeconds = 0.f;
	float RepathSeconds = 0.f;
	bool bThinking = true;
	bool bHasBody = false;
};
