#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeAccessibility.generated.h"

/**
 * The HUD's accent colours for one palette. Green is the grapple marker, purple is Kate's (the reticle,
 * the secondary markers, the combo bonus, the phone badge), cream is the objective marker, the compass
 * caret and the thugs' "!" / "?", the health bar is the thin bar over a hurt thug, and danger is the
 * telegraph glyph over a thug about to strike.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyePalette
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	FLinearColor Green = FLinearColor(0.22f, 1.f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	FLinearColor Purple = FLinearColor(0.78f, 0.55f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	FLinearColor Cream = FLinearColor(1.f, 0.93f, 0.78f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	FLinearColor HealthBar = FLinearColor(0.95f, 0.3f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	FLinearColor Danger = FLinearColor(1.f, 0.16f, 0.03f, 1.f);
};

/**
 * The accessibility rules that are only numbers: palettes, subtitle sizes, the shake and flash scales,
 * and hold-versus-toggle. Pure, so the tests check them without a HUD; the widgets and the character
 * apply them from FHawkeyeSettings.
 */
UCLASS()
class HAWKEYE_API UHawkeyeAccessibility : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** What camera shakes and flashes are scaled to with the reduce toggles on. */
	static constexpr float ReducedShakeScale = 0.3f;
	static constexpr float ReducedFlashScale = 0.3f;

	/** The accent colours for Palette. The colour-blind palettes keep every pair apart for that deficiency. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static FHawkeyePalette GetPalette(EHawkeyeColorPalette Palette);

	/** 20, 26 or 34 px. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static int32 GetSubtitleFontSize(EHawkeyeSubtitleSize Size);

	/** 1, or ReducedShakeScale with the toggle on. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static float GetCameraShakeScale(bool bReduceCameraShake) { return bReduceCameraShake ? ReducedShakeScale : 1.f; }

	/** 1, or ReducedFlashScale with the toggle on. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static float GetFlashScale(bool bReduceFlashing) { return bReduceFlashing ? ReducedFlashScale : 1.f; }

	/** GetFlashScale for WorldContextObject's settings (UHawkeyeSettingsSubsystem::GetCurrentSettings). */
	static float GetFlashScaleFor(const UObject* WorldContextObject);

	/**
	 * Whether the thing (aim, crouch) should be on after its button is pressed, given whether it is on
	 * now: held, a press always turns it on; toggled, a press flips it.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static bool ResolvePress(bool bToggle, bool bActive) { return bToggle ? !bActive : true; }

	/** Whether it should be on after its button is let go: held, off; toggled, unchanged. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static bool ResolveRelease(bool bToggle, bool bActive) { return bToggle ? bActive : false; }

	/** "Default", "Deuteranopia", "Protanopia", "Tritanopia". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static FText GetPaletteName(EHawkeyeColorPalette Palette);

	/** "Small", "Medium", "Large". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Accessibility")
	static FText GetSubtitleSizeName(EHawkeyeSubtitleSize Size);
};
