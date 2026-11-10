#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeResultsWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChallengeResultsChoiceSignature);

/**
 * The card at the end of a side challenge: its name, how it ended, the time, the score (archery) or
 * the checkpoints reached (traversal), the medal, the best with a "New best" mark, and "[Retry]" and
 * "[Leave]". Reports the choice only; AHawkeyePlayerController acts on it.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UChallengeResultsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Challenge")
	FOnChallengeResultsChoiceSignature OnRetryClicked;

	UPROPERTY(BlueprintAssignable, Category = "Challenge")
	FOnChallengeResultsChoiceSignature OnLeaveClicked;

	/** Fills the card from Result. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void SetResult(const FChallengeResult& Result);

	/** The card's lines for Result, top to bottom: title, outcome, time, score or checkpoints, medal, best. Pure. */
	static TArray<FText> FormatLines(const FChallengeResult& Result);

	/** What the card shows now, top to bottom. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	TArray<FText> GetLines() const { return Lines; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleRetryClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	UFUNCTION()
	void HandleButtonHovered();

	UFUNCTION()
	void HandleButtonPressed();

	/** Writes Lines into the text blocks. */
	void ApplyLines();

	UPROPERTY(BlueprintReadOnly, Category = "Challenge", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RetryButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge", meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> LineTexts;

private:
	TArray<FText> Lines;
	bool bBound = false;
};
