#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SnowfallComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Light snow round the local player's camera (claude-docs/gameplay-semantics.md, "Effects"). Lives
 * on the player controller, so it follows whichever character is possessed. At BeginPlay it spawns
 * SnowSystem (NS_Snowfall: 800 GPU flakes the view recycler keeps inside the frustum) and every frame
 * puts it at HawkeyeVfxMath::ComputeSnowfallOrigin for the camera, so its bounds always hold the view.
 * Only outdoors: a map whose game mode has bOutdoorWeather off (a flashback's playable scene, an
 * interior) gets no snow at all.
 */
UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API USnowfallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USnowfallComponent();

	/** The snow (NS_Snowfall). Nothing falls when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snowfall")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	/** How far ahead of the camera, along the ground, the system sits, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snowfall", meta = (ClampMin = "0.0"))
	float AheadCm = 600.f;

	/** How far above the camera it sits, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snowfall")
	float HeightCm = 250.f;

	/** How much of the camera's motion it leads by, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snowfall", meta = (ClampMin = "0.0"))
	float LeadSeconds = 0.5f;

	/** The snow's component, or null before BeginPlay, without a local player, or when unset. */
	UFUNCTION(BlueprintPure, Category = "Snowfall")
	UNiagaraComponent* GetSnowComponent() const { return Snow; }

	/** Whether snow falls in World at all: only under a Hawkeye game mode with outdoor weather. */
	static bool ShouldSnowIn(const UWorld* World);

	/** Puts the snow where it belongs for a camera at Location looking along Forward. Tick calls it. */
	void FollowCamera(const FVector& Location, const FVector& Forward, float DeltaSeconds);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Snow = nullptr;

	FVector LastCamera = FVector::ZeroVector;
	bool bHasLastCamera = false;
};
