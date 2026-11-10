#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlashbackDefinition.generated.h"

class UTexture2D;
class USoundBase;
class UWorld;

/** One still frame of a flashback slideshow. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FFlashbackSlide
{
	GENERATED_BODY()

	/** Full-screen still. Soft so an unplayed flashback costs no memory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	TSoftObjectPtr<UTexture2D> Image;

	/** Caption shown under the image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (MultiLine = "true"))
	FText Caption;

	/** Seconds this slide stays up before advancing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float HoldSeconds = 4.f;

	/** Seconds spent fading in from the previous slide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CrossfadeSeconds = 1.f;

	/** Optional narration played when this slide appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	TSoftObjectPtr<USoundBase> VoiceLine;

	/**
	 * Multiplies the image. With no Image the slide is this colour, full screen: placeholder
	 * slides are solid colours until the art exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	FLinearColor Tint = FLinearColor::White;
};

/**
 * A slideshow told between missions (the origin story beats).
 * Create via Content Browser > Miscellaneous > Data Asset > FlashbackDefinition.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UFlashbackDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback")
	TArray<FFlashbackSlide> Slides;

	/** Looping bed played for the whole flashback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback")
	TSoftObjectPtr<USoundBase> AmbientLoop;

	/** When true any key dismisses the flashback early. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback")
	bool bSkippable = true;

	/**
	 * Optional small map played after the slideshow (claude-docs/gameplay-semantics.md,
	 * "Flashback"). The district is saved first; when that map's mission completes the save is
	 * loaded again and the player is put at ReturnPointLabel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback|Scene")
	TSoftObjectPtr<UWorld> PlayableScene;

	/**
	 * Label or tag of the actor in the district the player comes back to after PlayableScene.
	 * None, or no such actor, keeps the position the save had.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashback|Scene")
	FName ReturnPointLabel;

	/** True when a playable scene follows the slides. */
	UFUNCTION(BlueprintPure, Category = "Flashback|Scene")
	bool HasPlayableScene() const { return !PlayableScene.IsNull(); }

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
