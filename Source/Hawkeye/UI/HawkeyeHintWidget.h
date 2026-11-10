#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/HawkeyeHints.h"
#include "HawkeyeHintWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UWidget;

/**
 * The first-time hint line, lower centre just above the hotbar: plain text with each key drawn as a keycap
 * ("Hold [Shift] to sprint"), on a dark backing. It shows what UHawkeyeHintSubsystem holds, resolved for the
 * device the player last touched, fading in over FadeSeconds and out over the same when it goes. Built into
 * the HUD's overlay by UHawkeyeHudWidget; hit-test invisible, so it never takes a click or a key.
 */
UCLASS()
class HAWKEYE_API UHawkeyeHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The hotbar the line sits above: its height (times its render scale) sets the line's bottom padding. */
	void SetHotbar(UWidget* InHotbar) { Hotbar = InHotbar; }

	/** The HUD scale, about the line's bottom centre. */
	void SetHudScale(float Scale);

	/** The line as it reads now, keys in brackets ("Hold [Shift] to sprint"); empty when nothing is up. */
	UFUNCTION(BlueprintPure, Category = "HUD|Hints")
	FString GetShownText() const { return ShownText; }

	/** The id of the hint on the line (fading out included), or None. */
	UFUNCTION(BlueprintPure, Category = "HUD|Hints")
	FName GetShownId() const { return ShownId; }

	/** 0 to 1: the fade. */
	UFUNCTION(BlueprintPure, Category = "HUD|Hints")
	float GetLineOpacity() const { return Opacity; }

	/** The line's bottom edge, px up from the HUD's bottom: the hotbar's scaled top plus GapAboveHotbar. */
	float GetBottomPadding() const;

	/**
	 * The top of the band the line owns, px up from the HUD's bottom: its bottom plus its height (as last
	 * measured with a line up, never less than ReservedLineHeight) times the HUD scale. Reserved whether or not
	 * a hint is up, so the subtitle the HUD stacks over it never jumps when one comes or goes.
	 */
	float GetStackTop() const;

	/** The band's height at scale 1 before a line has been measured: one line of FontSize with a keycap and its padding (45 px measured). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hints")
	float ReservedLineHeight = 45.f;

	/** Gap between the hotbar's top and the line's bottom, px. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hints")
	float GapAboveHotbar = 12.f;

	/** The hotbar's own bottom padding in the HUD, px. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hints")
	float HotbarBottom = 24.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hints")
	float FadeSeconds = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hints")
	int32 FontSize = 22;

	/** Runs the line against Rule (null: nothing up) for one frame. The HUD's tick calls it with the subsystem's. */
	void UpdateLine(const FHawkeyeHintRule* Rule, float Age, const FHawkeyeHintContext& Context, float DeltaSeconds);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;

private:
	void Rebuild(const TArray<FHawkeyeHintSegment>& Segments);
	void PlaceAboveHotbar();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Backing = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> Row = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Hotbar = nullptr;

	FName ShownId;
	FString ShownText;
	float Opacity = 0.f;
	float LastPadding = -1.f;
	float MeasuredLineHeight = 0.f;
	float HudScale = 1.f;
	bool bBackingOpen = false;
};
