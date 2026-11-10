#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Civilian.generated.h"

class UAnimSequence;
class UTextRenderComponent;

/**
 * A mugging's victim (BP_Civilian: the old SK_Mannequin in a neutral grey). He says nothing: he
 * cowers while the thugs are on him, flinches at each hit, and once freed shows a "[thank you]"
 * placeholder over his head and runs off. No health: the crime counts his hits (UCrimeTracker).
 * Driven by UCrimeSubsystem; a plain AAIController walks him away.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ACivilian : public ACharacter
{
	GENERATED_BODY()

public:
	ACivilian();

	/** Hunched over, arms in: the thugs are on him. Off stands him back up. */
	UFUNCTION(BlueprintCallable, Category = "Civilian")
	void SetCowering(bool bInCowering);

	UFUNCTION(BlueprintPure, Category = "Civilian")
	bool IsCowering() const { return bCowering; }

	/** A thug hit him: a flinch away from By. Counted, nothing more. */
	UFUNCTION(BlueprintCallable, Category = "Civilian")
	void TakeHit(AActor* By);

	UFUNCTION(BlueprintPure, Category = "Civilian")
	int32 GetHitsTaken() const { return HitsTaken; }

	/**
	 * The thugs are down: stands up, shows ThanksText for ThanksSeconds, and runs FleeDistance away from
	 * Rescuer. Only the first call does anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "Civilian")
	void Free(AActor* Rescuer);

	UFUNCTION(BlueprintPure, Category = "Civilian")
	bool IsFreed() const { return bFreed; }

	/** Freed and still running. */
	UFUNCTION(BlueprintPure, Category = "Civilian")
	bool IsFleeing() const { return bFleeing; }

	UFUNCTION(BlueprintPure, Category = "Civilian")
	bool IsThanksShowing() const { return ThanksRemaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Civilian")
	UTextRenderComponent* GetThanksLabel() const { return ThanksLabel; }

	/** The floating text on rescue. A placeholder: civilians have no lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian")
	FText ThanksText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0"))
	float ThanksSeconds = 3.f;

	/** How far he runs once freed, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0"))
	float FleeDistance = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0"))
	float FleeSpeed = 450.f;

	/** How far the cower drops his body, cm, and how far it hunches him forward, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0"))
	float CowerDrop = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float CowerLeanDegrees = 20.f;

	/** The extra lean of a flinch, degrees, and how long it lasts, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.0"))
	float FlinchDegrees = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilian", meta = (ClampMin = "0.01"))
	float FlinchSeconds = 0.3f;

	/** Played on the mesh's single-node slot, as the thugs do (the mannequin pack's AnimBP does not compile headless). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Civilian|Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Civilian|Animation")
	TObjectPtr<UAnimSequence> RunAnim;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** The cower and the flinch on the mesh, built off its mount on the capsule. */
	void UpdatePose();

	/** Idle while standing or cowering, the run while fleeing, switched only on a change. */
	void UpdateAnimation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Civilian")
	TObjectPtr<UTextRenderComponent> ThanksLabel;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentAnim;

private:
	bool bCowering = false;
	bool bFreed = false;
	bool bFleeing = false;
	int32 HitsTaken = 0;
	float FlinchRemaining = 0.f;
	float ThanksRemaining = 0.f;
	FVector FleeGoal = FVector::ZeroVector;
	FTransform MeshMount = FTransform::Identity;
};
