#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MissionEndCardWidget.generated.h"

class UBorder;
class UMissionDefinition;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEndCardFinishedSignature, UMissionDefinition*, Mission);

/**
 * The black card between the last objective and whatever comes next: the mission name, the
 * mission's EndCardLine underneath it, and nothing else.
 *
 * Reparent a UMG widget to this class and name widgets Background, MissionNameText, LineText and
 * PromptText to have them driven automatically; a subclass with no designer layout works too,
 * because RebuildWidget builds them itself (the same approach as UHawkeyeHudWidget).
 *
 * Timing runs from NativeTick, not a timer, because the card can be shown over a paused world.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UMissionEndCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Shows the card for Mission and starts the hold. A null mission still shows the card and
	 * still finishes, so a missing data asset never strands the player on a black screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "End card")
	void Play(UMissionDefinition* Mission);

	/**
	 * Holds the card open until a key is pressed instead of for EndCardSeconds. Used at the end
	 * of the campaign, where there is no next level to travel to.
	 */
	UFUNCTION(BlueprintCallable, Category = "End card")
	void PlayAndWaitForInput(UMissionDefinition* Mission, FText Prompt);

	/** Ends the card now and broadcasts OnEndCardFinished. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "End card")
	void Finish();

	UFUNCTION(BlueprintPure, Category = "End card")
	bool IsPlaying() const { return bIsPlaying; }

	/** Seconds spent elapsed / total, for a Blueprint that wants to fade the text. */
	UFUNCTION(BlueprintPure, Category = "End card")
	float GetElapsedSeconds() const { return ElapsedSeconds; }

	/** How long the card holds before it finishes on its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "End card", meta = (ClampMin = "0.0"))
	float EndCardSeconds = 4.f;

	UPROPERTY(BlueprintAssignable, Category = "End card")
	FOnEndCardFinishedSignature OnEndCardFinished;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	/** Writes the mission name, the line and the prompt into whichever text blocks exist. */
	void ApplyMissionText();

	UPROPERTY(BlueprintReadOnly, Category = "End card", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Background = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "End card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MissionNameText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "End card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LineText = nullptr;

	/** "Press any key", shown only by PlayAndWaitForInput. */
	UPROPERTY(BlueprintReadOnly, Category = "End card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "End card")
	TObjectPtr<UMissionDefinition> CurrentMission = nullptr;

	UPROPERTY(Transient)
	FText PromptLabel;

private:
	bool bIsPlaying = false;

	/** True while the card waits for a key instead of counting down. */
	bool bWaitForInput = false;

	float ElapsedSeconds = 0.f;
};
