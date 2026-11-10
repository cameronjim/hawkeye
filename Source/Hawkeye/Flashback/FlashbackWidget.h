#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlashbackWidget.generated.h"

class UImage;
class UTextBlock;
class UAudioComponent;
class UFlashbackDefinition;
class UFlashbackSequencer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlashbackFinishedSignature, UFlashbackDefinition*, Flashback);

/**
 * Full-screen crossfading slideshow.
 *
 * Reparent a UMG widget to this class and (optionally) name widgets SlideImageA, SlideImageB and
 * CaptionText to have them driven automatically. A subclass with no designer layout at all works
 * too: RebuildWidget builds an Overlay with the three widgets itself.
 *
 * Timing lives in UFlashbackSequencer (testable without UMG). It is driven from NativeTick rather
 * than FTimerManager because the widget pauses the game while it plays, and paused worlds do not
 * tick their timer manager.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UFlashbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Adds the widget to the viewport (if needed), pauses the game and starts the slideshow.
	 * A null definition, or one with no slides, fires OnFlashbackFinished once and never pauses.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void Play(UFlashbackDefinition* InFlashback);

	/** Ends the whole flashback early; ignored if not skippable or inside the skip lockout. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void Skip();

	/** Stops playback, restores input/pause state and broadcasts OnFlashbackFinished. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void Finish();

	UFUNCTION(BlueprintPure, Category = "Flashback")
	bool IsPlaying() const { return bIsPlaying; }

	UFUNCTION(BlueprintPure, Category = "Flashback")
	UFlashbackSequencer* GetSequencer() const { return Sequencer; }

	UPROPERTY(BlueprintAssignable, Category = "Flashback")
	FOnFlashbackFinishedSignature OnFlashbackFinished;

	/** Blueprint hook fired each time a new slide starts (for extra VFX, letterboxing, etc.). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flashback")
	void OnSlideStarted(int32 SlideIndex);

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	/** Puts slide SlideIndex on layer A and the one after it on layer B, and plays the voice line. */
	void ShowSlide(int32 SlideIndex);

	/** Applies the sequencer's blend alpha to the two image layers. */
	void ApplyBlend(float Alpha);

	void ApplyPlaybackInputMode(bool bEnable);

	UAudioComponent* PlaySound2DDuringPause(USoundBase* Sound, bool bLooping);

	/** Layer showing the current slide. */
	UPROPERTY(BlueprintReadOnly, Category = "Flashback", meta = (BindWidgetOptional))
	TObjectPtr<UImage> SlideImageA = nullptr;

	/** Layer showing the next slide, faded up across the crossfade. */
	UPROPERTY(BlueprintReadOnly, Category = "Flashback", meta = (BindWidgetOptional))
	TObjectPtr<UImage> SlideImageB = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Flashback", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CaptionText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Flashback")
	TObjectPtr<UFlashbackDefinition> Flashback = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UFlashbackSequencer> Sequencer = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientAudio = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceAudio = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Flashback")
	int32 CurrentSlideIndex = INDEX_NONE;

private:
	bool bIsPlaying = false;

	/** Pause/cursor state captured on Play so Finish can restore it. */
	bool bRestoreCursor = false;
	bool bWasPausedBeforePlay = false;
};
