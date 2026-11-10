#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyeSafehouseWidget.generated.h"

class UButton;
class UFlashbackReplayEntryWidget;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSafehouseChoiceSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSafehouseListPickedSignature, int32, Index);

/** One row of a safehouse sub-list (the fast-travel list, chapter select). */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMenuListRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Safehouse")
	FText Label;

	/** False greys the row out; it cannot be picked. */
	UPROPERTY(BlueprintReadWrite, Category = "Safehouse")
	bool bEnabled = true;
};

/**
 * The safehouse menu: Refill arrows, Save, Fast travel, Chapter select, Leave, with the address and a
 * status line under the title for what the last choice did ("Arrows refilled").
 *
 * Fast travel and Chapter select swap the buttons for a list (ShowList): one row per entry, greyed when
 * it cannot be picked, and Back. Reports choices only; AHawkeyePlayerController acts on them.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeSafehouseWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnRefillClicked;

	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnSaveClicked;

	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnFastTravelClicked;

	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnChapterSelectClicked;

	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnLeaveClicked;

	/** An enabled row of the shown list was picked. */
	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseListPickedSignature OnListRowPicked;

	/** Back on the shown list; the widget has already returned to the buttons. */
	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnSafehouseChoiceSignature OnListBackClicked;

	/** The heading: the safehouse's name. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SetSafehouseName(const FText& Name);

	/** The small line under the heading: the address. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SetSubtitle(const FText& Subtitle);

	/** The line under the address. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SetStatus(const FText& Status);

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	FText GetStatus() const { return StatusLine; }

	/** Replaces the buttons with Heading, Rows and Back. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void ShowList(const FText& Heading, const TArray<FHawkeyeMenuListRow>& Rows);

	/** Back to the buttons. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void ShowMain();

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	bool IsListShown() const { return bListShown; }

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	int32 GetListRowCount() const { return ListRows.Num(); }

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	FHawkeyeMenuListRow GetListRow(int32 Index) const
	{
		return ListRows.IsValidIndex(Index) ? ListRows[Index] : FHawkeyeMenuListRow();
	}

	/** What clicking row Index does: OnListRowPicked when it is enabled. False when it is not. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	bool PickListRow(int32 Index);

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleRefillClicked();

	UFUNCTION()
	void HandleSaveClicked();

	UFUNCTION()
	void HandleFastTravelClicked();

	UFUNCTION()
	void HandleChapterSelectClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleRowPicked(int32 Index);

	/** Any button under the cursor or pad focus: the hover blip. */
	UFUNCTION()
	void HandleButtonHovered();

	/** Any button pressed: the click. */
	UFUNCTION()
	void HandleButtonPressed();

	/** Fills ListBox from ListHeadingText and ListRows. */
	void RebuildList();

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RefillButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SaveButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FastTravelButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ChapterSelectButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText = nullptr;

	/** The five buttons. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MainBox = nullptr;

	/** The list's heading and rows (Back sits under it in ListFrame). */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ListBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ListFrame = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFlashbackReplayEntryWidget>> RowWidgets;

private:
	FText SafehouseName;
	FText SubtitleLine;
	FText StatusLine;
	FText ListHeading;
	TArray<FHawkeyeMenuListRow> ListRows;
	bool bListShown = false;
	bool bBound = false;
};
