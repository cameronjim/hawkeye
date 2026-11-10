#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrimeLoot.generated.h"

class UStaticMeshComponent;

/**
 * A robbery's loot: a dark holdall the runner carries off. On the ground it glows faintly so it can be
 * found again; the player walking over it picks it up (UCrimeSubsystem checks the distance), which
 * stops the robbery. No collision: nothing trips on it and no arrow stops at it.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ACrimeLoot : public AActor
{
	GENERATED_BODY()

public:
	ACrimeLoot();

	/** Into Carrier's hand. Null drops it at its current place, on the ground under it. */
	UFUNCTION(BlueprintCallable, Category = "Crime|Loot")
	void SetCarrier(AActor* Carrier);

	UFUNCTION(BlueprintPure, Category = "Crime|Loot")
	AActor* GetCarrier() const { return Carrier.Get(); }

	UFUNCTION(BlueprintPure, Category = "Crime|Loot")
	bool IsCarried() const { return Carrier.IsValid(); }

	/** Picked up by the player: hidden, done. */
	UFUNCTION(BlueprintCallable, Category = "Crime|Loot")
	void Recover();

	UFUNCTION(BlueprintPure, Category = "Crime|Loot")
	bool IsRecovered() const { return bRecovered; }

	/** The holdall's size, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Loot")
	FVector BagSize = FVector(55.f, 28.f, 30.f);

	/** Where the carried bag hangs, relative to the carrier's centre, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Loot")
	FVector CarryOffset = FVector(10.f, 38.f, -45.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Loot")
	FLinearColor BagColor = FLinearColor(0.05f, 0.045f, 0.04f);

	/** The faint glow of dropped loot, so it reads at night. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crime|Loot")
	FLinearColor DroppedGlow = FLinearColor(0.6f, 0.35f, 1.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crime|Loot")
	TObjectPtr<UStaticMeshComponent> Bag;

protected:
	virtual void BeginPlay() override;

	/** The tint for carried or dropped. */
	void RefreshLook();

private:
	TWeakObjectPtr<AActor> Carrier;
	bool bRecovered = false;
};
