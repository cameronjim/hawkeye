#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChallengePanelWidget.generated.h"

class UBorder;
class UChallengeTracker;
class UTextBlock;

/**
 * The side-challenge panel, top right of the HUD while a run is on: the challenge's name, the clock
 * (time left of the limit), the score or "Checkpoint 3/8", and the best so far. Collapsed otherwise.
 * Built into UHawkeyeHudWidget's overlay; reads UChallengeSubsystem every frame.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UChallengePanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The four lines for Tracker's run, as the panel shows them. Pure so a test can read the format. */
	static void FormatLines(const UChallengeTracker* Tracker, FText& OutTitle, FText& OutClock, FText& OutProgress, FText& OutBest);

	UFUNCTION(BlueprintPure, Category = "HUD|Challenge")
	bool IsPanelVisible() const { return bShown; }

	UFUNCTION(BlueprintPure, Category = "HUD|Challenge")
	FText GetClockText() const { return ClockShown; }

	UFUNCTION(BlueprintPure, Category = "HUD|Challenge")
	FText GetProgressText() const { return ProgressShown; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	//~ End UUserWidget interface

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ClockText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ProgressText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BestText = nullptr;

	/** Kate purple for the name, cream for the numbers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Challenge")
	FLinearColor TitleColor = FLinearColor(0.78f, 0.55f, 1.f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Challenge")
	FLinearColor TextColor = FLinearColor(1.f, 0.93f, 0.78f, 1.f);

	/** The clock under ten seconds left. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Challenge")
	FLinearColor HurryColor = FLinearColor(1.f, 0.35f, 0.25f, 1.f);

private:
	bool bShown = false;
	FText ClockShown;
	FText ProgressShown;
};
