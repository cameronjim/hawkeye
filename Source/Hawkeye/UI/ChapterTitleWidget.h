#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/ChapterTitleClock.h"
#include "ChapterTitleWidget.generated.h"

class UBorder;
class UMissionDefinition;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChapterTitleFinishedSignature, UMissionDefinition*, Mission);

/**
 * The full-screen card at a chapter's start: the chapter number, the mission's OpeningTitle and
 * OpeningSubtitle over a dark backdrop, faded in, held and faded out (FHawkeyeTitleCardClock).
 * Play is not paused and input is not taken: the player can move under it, and any key after the
 * lockout (AHawkeyePlayerController forwards them) starts the fade out.
 *
 * Name widgets Backdrop, ChapterNumberText, TitleText and SubtitleText in a designer layout to
 * have them driven; with none, RebuildWidget builds them.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UChapterTitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows Mission's opening card and starts the clock. */
	UFUNCTION(BlueprintCallable, Category = "Title")
	void Play(UMissionDefinition* Mission);

	/** Starts the fade out, unless inside the lockout. True when it did. */
	UFUNCTION(BlueprintCallable, Category = "Title")
	bool Skip();

	UFUNCTION(BlueprintPure, Category = "Title")
	bool IsPlaying() const { return Clock.IsRunning(); }

	UFUNCTION(BlueprintPure, Category = "Title")
	float GetAlpha() const { return Clock.GetAlpha(); }

	/** "[Chapter {0}]" with the mission number, what the small line shows. */
	UFUNCTION(BlueprintPure, Category = "Title")
	FText GetChapterNumberText() const;

	UFUNCTION(BlueprintPure, Category = "Title")
	FText GetTitleShown() const;

	/** Fade and hold lengths. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title")
	FHawkeyeTitleCardClock Clock;

	/** Format for the chapter number line; {0} is the mission number. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title")
	FText ChapterNumberFormat;

	/** The backdrop's opacity at full card: the street stays faintly visible behind it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackdropOpacity = 0.85f;

	UPROPERTY(BlueprintAssignable, Category = "Title")
	FOnChapterTitleFinishedSignature OnTitleFinished;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	void Finish();

	UPROPERTY(BlueprintReadOnly, Category = "Title", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Backdrop = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Title", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChapterNumberText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Title", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Title", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMissionDefinition> CurrentMission = nullptr;
};
