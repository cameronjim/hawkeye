#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlashbackReplayWidget.generated.h"

class UButton;
class UFlashbackDefinition;
class UFlashbackReplayEntryWidget;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlashbackReplayPickedSignature, UFlashbackDefinition*, Flashback);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFlashbackReplayBackSignature);

/**
 * The pause menu's "Replay flashbacks": every flashback the player has seen (the settings save's
 * SeenFlashbacks, across saves), oldest first, one button each, and Back. A pick plays the slides again;
 * the playable scene is not replayed. With nothing seen yet it says so.
 *
 * Only reports; AHawkeyePlayerController plays the pick and decides what Back means.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UFlashbackReplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Flashback")
	FOnFlashbackReplayPickedSignature OnFlashbackPicked;

	UPROPERTY(BlueprintAssignable, Category = "Flashback")
	FOnFlashbackReplayBackSignature OnBackRequested;

	/** Rebuilds the list from Seen (soft paths to UFlashbackDefinition); paths that do not load are skipped. */
	void SetFlashbacks(const TArray<FSoftObjectPath>& Seen);

	UFUNCTION(BlueprintPure, Category = "Flashback")
	int32 GetEntryCount() const { return Flashbacks.Num(); }

	/** The title a row shows: the definition's Title, or its asset name. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	FText GetEntryLabel(int32 Index) const;

	/** What a flashback is listed as: its Title, or its asset name when it has none. Pure. */
	static FText MakeEntryLabel(const UFlashbackDefinition* Flashback);

	/** Plays entry Index, as a click on its row does. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void PickEntry(int32 Index);

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleEntryPicked(int32 Index);

	UFUNCTION()
	void HandleBackClicked();

	/** Puts the rows (or the empty line) in the list box. */
	void RebuildRows();

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ListBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFlashbackDefinition>> Flashbacks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFlashbackReplayEntryWidget>> Rows;

private:
	bool bBound = false;
};
