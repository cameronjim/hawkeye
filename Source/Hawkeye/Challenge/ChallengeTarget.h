#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeTarget.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;
class USceneComponent;

/**
 * An archery challenge's target: a 60 cm face of concentric rings (cream outer, red ring, gold
 * bullseye) on a thin post, spawned by UChallengeSubsystem for one run and removed after. The origin is
 * the foot of the post; the face looks along the actor's +X, tilted by the spawn's pitch. A moving
 * target slides back and forth along its local Y.
 *
 * Only the outer face collides, and only with arrows and the aim trace: AArrowProjectile hands the hit
 * to HandleArrowHit, which measures how far from the centre it landed and reports it to the subsystem.
 * A hit target tips over backwards and stays down.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AChallengeTarget : public AActor
{
	GENERATED_BODY()

public:
	AChallengeTarget();

	/** Places the target and sizes its rings. Index is its slot in the challenge's Targets. */
	void InitTarget(int32 InIndex, const FChallengeTargetSpawn& Spawn, float InOuterRadius, float InRingRadius,
		float InBullseyeRadius);

	/** An arrow struck the face at ImpactPoint: reports the distance from the centre to the challenge. */
	void HandleArrowHit(const FVector& ImpactPoint, AActor* Shooter);

	/** Tips it over: it has been scored. */
	void KnockDown(int32 Points);

	UFUNCTION(BlueprintPure, Category = "Challenge|Target")
	bool IsDown() const { return bDown; }

	UFUNCTION(BlueprintPure, Category = "Challenge|Target")
	int32 GetTargetIndex() const { return TargetIndex; }

	/** The bullseye's centre, where the face is now. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Target")
	FVector GetFaceCentre() const;

	/** The way the face looks. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Target")
	FVector GetFaceNormal() const;

	/** How fast it is sliding, cm/s (zero for a still one). What a shooter leads by. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Target")
	FVector GetSlideVelocity() const { return SlideVelocity; }

	/**
	 * The face's glow, so the rings read across a street at night: M_Emissive (Color, Intensity), one
	 * instance per ring. The plain shape material when it is not there.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Target")
	TSoftObjectPtr<UMaterialInterface> FaceMaterial;

	/** The rings' emissive intensity; a scored target drops to a tenth of it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Target", meta = (ClampMin = "0.0"))
	float FaceGlow = 0.8f;

	/** The face's centre this high above the foot of the post, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Target", meta = (ClampMin = "0.0"))
	float FaceHeight = 150.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Takes the arrows stuck in it along. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Sizes and places the post and the three discs from the radii. */
	void LayoutParts();

	/** Colours one part through its own instance of its material, and sets its glow when it has one. */
	static void Tint(UStaticMeshComponent* Part, const FLinearColor& Color, float Glow = -1.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<USceneComponent> Root;

	/** Slides along the track; carries the post and the face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<USceneComponent> Slider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<UStaticMeshComponent> Post;

	/** Hinged at the bottom of the face, so a hit tips it over backwards. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<USceneComponent> Hinge;

	/** The face's centre; +X out of it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<USceneComponent> FacePivot;

	/** The whole 60 cm face; the only part that collides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<UStaticMeshComponent> OuterDisc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<UStaticMeshComponent> RingDisc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	TObjectPtr<UStaticMeshComponent> BullseyeDisc;

	int32 TargetIndex = INDEX_NONE;
	float OuterRadius = 30.f;
	float RingRadius = 18.f;
	float BullseyeRadius = 8.f;
	float FacePitch = 0.f;

	bool bMoving = false;
	float TrackLength = 0.f;
	float TrackSpeed = 0.f;
	/** Where along the track it is, cm from the centre, and which way it is going. */
	float TrackOffset = 0.f;
	float TrackDirection = 1.f;
	FVector SlideVelocity = FVector::ZeroVector;

	bool bDown = false;
	/** The rings wear FaceMaterial (and so take an intensity). */
	bool bGlowing = false;
	/** 0 to 1 over the fall. */
	float FallAlpha = 0.f;
};
