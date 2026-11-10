#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyeQuiverWheelWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UInventoryComponent;
class UTextBlock;

/**
 * The radial quiver (claude-docs/gameplay-semantics.md, "trick arrows"): a ring with one box per
 * quiver slot, segment 1 at the top and the rest clockwise (UQuiverWheelMath), each showing the
 * arrow's short name and its count. The highlighted segment is Kate purple, empty ones are dim,
 * and the middle names what a release would nock.
 *
 * Built in C++ inside the HUD's overlay, collapsed until AHawkeyeCharacter opens it; like the
 * hotbar, every label is a pure function of the bound inventory so a test can read them.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeQuiverWheelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Binds to Inventory's change delegate and repaints. Passing null unbinds. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Quiver Wheel")
	void BindToInventory(UInventoryComponent* Inventory);

	/** Finds the owning pawn's inventory and binds to it. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Quiver Wheel")
	void BindToOwningPawn();

	/** Shows or collapses the wheel and marks QuiverSlot (0 for none) as the one under the cursor. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Quiver Wheel")
	void SetWheelState(bool bOpen, int32 QuiverSlot);

	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	bool IsWheelOpen() const { return bWheelOpen; }

	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	int32 GetHighlightedSlot() const { return HighlightedSlot; }

	/** The arrow's short name, or "--" for an empty slot. */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	FText GetSegmentNameText(int32 QuiverSlot) const;

	/** "30" for standard arrows, "count/cap" for trick arrows, the infinity for the grapple, empty for an empty slot. */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	FText GetSegmentCountText(int32 QuiverSlot) const;

	/** Highlighted, nocked, filled or empty. */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	FLinearColor GetSegmentColor(int32 QuiverSlot) const;

	/** The middle: the highlighted arrow's full name, or the nocked one's when nothing is highlighted. */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	FText GetCentreText() const;

	/** Repaints every segment. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Quiver Wheel")
	void RefreshSegments();

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleInventoryChanged();

	/** Radius the segment boxes sit on, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel", meta = (ClampMin = "10.0"))
	float SegmentRadius = 170.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel", meta = (ClampMin = "10.0"))
	float SegmentWidth = 104.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel")
	FLinearColor HighlightColor = FLinearColor(0.52f, 0.2f, 0.88f, 0.95f);

	/** The slot already nocked, when it is not the highlighted one: a purple outline. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel")
	FLinearColor NockedColor = FLinearColor(0.2f, 0.08f, 0.32f, 0.85f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel")
	FLinearColor FilledColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Quiver Wheel")
	FLinearColor EmptyColor = FLinearColor(0.f, 0.f, 0.f, 0.3f);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UImage> Ring = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CentreText = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SegmentBoxes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SegmentNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SegmentCounts;

	UPROPERTY(Transient)
	TObjectPtr<UInventoryComponent> BoundInventory = nullptr;

	bool bWheelOpen = false;
	int32 HighlightedSlot = 0;
};
