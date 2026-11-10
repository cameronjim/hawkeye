#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlashbackReplayEntryWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlashbackEntryPickedSignature, int32, Index);

/**
 * One row of the "Replay flashbacks" list: a button with the flashback's title. It reports its own
 * index when clicked, which a bare UButton's OnClicked cannot.
 */
UCLASS()
class HAWKEYE_API UFlashbackReplayEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Flashback")
	FOnFlashbackEntryPickedSignature OnPicked;

	/** What the row says and which entry it is. Call before it is added to a panel. */
	void Setup(int32 InIndex, const FText& InLabel);

	FText GetLabel() const { return Label; }

	/** Greys the row out and stops it reporting clicks (a locked chapter, an undiscovered safehouse). */
	void SetEntryEnabled(bool bEnabled);

	bool IsEntryEnabled() const { return bEntryEnabled; }

	UButton* GetButton() const { return Button; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> Button = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText = nullptr;

private:
	int32 Index = INDEX_NONE;
	FText Label;
	bool bBound = false;
	bool bEntryEnabled = true;
};
