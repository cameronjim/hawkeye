#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyeThugOverheadWidget.generated.h"

class AHawkeyeCharacter;
class AThugCharacter;
class APlayerController;

/** One thug's overhead read for this frame: where, which glyph, how opaque the health bar. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeThugOverhead
{
	GENERATED_BODY()

	/** Just over his head bone, in the HUD's local space: the bottom centre of the stack (the health bar's bottom). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	FVector2D Position = FVector2D::ZeroVector;

	/** The same point: the glyph's bottom centre with no bar, the bar's with one (the glyph then stacks above it). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	FVector2D GlyphPosition = FVector2D::ZeroVector;

	/** "!", "?" or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	FString Glyph;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	float GlyphAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	float HealthFraction = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	float BarAlpha = 0.f;

	/** He is winding up a strike: the glyph is the telegraph's, in the danger colour, at TelegraphScale. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	bool bTelegraph = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	float TelegraphScale = 1.f;

	/** A tap now would parry him: the short line under the telegraph glyph. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	bool bParryCue = false;

	/** The melee assist's pick: the ring at his feet and the brighter bar, at this opacity (0 for none). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Thugs")
	float MarkerAlpha = 0.f;

	/** The ring round his feet, projected (closed: the last point joins the first). */
	TArray<FVector2D> MarkerRing;
};

/**
 * Over every thug's head (claude-docs/gameplay-semantics.md, "thug readability" and "Combat readability"):
 * a small cream "!" for 0.6 s when he goes Alerted, a "?" when he goes Suspicious, and a thin health bar
 * only while he is hurt, within 1500 cm, fading 3 s after the last hit. While he winds up a strike the
 * glyph is a larger "!" in the danger colour, growing and pulsing over the wind-up, with a short line under
 * it while a tap would parry him. The thug Kate's last swing was aimed at gets a thin ring at his feet in
 * her purple and a brighter bar, fading 0.3 s after the swing. The rules live on AThugCharacter, Kate and
 * HawkeyeCombatReadability (tested there); this only projects and paints. Built into UHawkeyeHudWidget's
 * overlay.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeThugOverheadWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** This frame's entries, one per thug with something to show. */
	UFUNCTION(BlueprintPure, Category = "HUD|Thugs")
	const TArray<FHawkeyeThugOverhead>& GetEntries() const { return Entries; }

	/** Glyph colour: the objective marker's cream. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FLinearColor GlyphColor = FLinearColor(1.f, 0.97f, 0.88f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FLinearColor BarColor = FLinearColor(0.95f, 0.3f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FLinearColor BarBackColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	/** The bar, px. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FVector2D BarSize = FVector2D(46.f, 4.f);

	/**
	 * A thin dark outline round the glyph, px, so the "!" and "?" read by shape on any background and
	 * in any palette, not by colour alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0"))
	int32 GlyphOutlineSize = 1;

	/** The font the glyphs are painted with, outline included. */
	UFUNCTION(BlueprintPure, Category = "HUD|Thugs")
	FSlateFontInfo GetGlyphFont() const;

	/**
	 * The palette's cream for the glyphs and its health-bar colour. bDesign puts back the colours the
	 * widget was built with (the default palette).
	 */
	void SetAccentColors(const FLinearColor& Glyph, const FLinearColor& Bar, bool bDesign);

	/** The palette's danger colour (the telegraph glyph) and Kate's purple (the target ring). bDesign: the built ones. */
	void SetThreatColors(const FLinearColor& Danger, const FLinearColor& Marker, bool bDesign);

	/** The telegraph glyph: red-orange in the default palette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FLinearColor TelegraphColor = FLinearColor(1.f, 0.16f, 0.03f, 1.f);

	/** The ring at the assist's pick's feet: Kate's purple. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FLinearColor MarkerColor = FLinearColor(0.78f, 0.55f, 1.f, 1.f);

	/** The telegraph glyph's size before it grows, px. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "6"))
	int32 TelegraphFontSize = 36;

	/** The parry line under it, px. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs")
	FVector2D ParryCueSize = FVector2D(18.f, 3.f);

	/** The gap between the bar, the parry line and the glyph stacked over his head, px (edges included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0"))
	float StackGap = 4.f;

	/** A gunner's raised pistol gets the telegraph glyph only this close to Kate, cm; further off his glint is the tell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0"))
	float GunnerTelegraphRange = 600.f;

	/** The ring's radius past his capsule, cm, and its line, px. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0"))
	float MarkerRingPadding = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.5"))
	float MarkerRingThickness = 2.5f;

	/** The dark edge drawn under the ring, px each side, so the lavender reads on bright snow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0"))
	float MarkerRingEdge = 1.f;

	/** How much whiter the marked thug's bar is (0 none, 1 white). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MarkerBarBrighten = 0.3f;

	/** The telegraph glyph's font at Scale of TelegraphFontSize, outline included. */
	FSlateFontInfo GetTelegraphFont(float Scale) const;

	/** The glyph's font size. Small: a flag, not a banner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "6"))
	int32 GlyphFontSize = 20;

	/** Thugs further than this from the camera are not considered at all, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Thugs", meta = (ClampMin = "0.0"))
	float MaxGlyphDistance = 4000.f;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	//~ End UUserWidget interface

	/** Rebuilds Entries from the thugs in the world. */
	void GatherEntries();

	/** The telegraph glyph, the parry cue and the target ring for Thug, onto Entry. True when any shows. */
	bool GatherCombatReads(const AThugCharacter* Thug, const AHawkeyeCharacter* Kate, APlayerController* PC,
		FHawkeyeThugOverhead& Entry) const;

	/** One glyph centred on X with its bottom at Bottom; returns its height. */
	float PaintGlyph(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& AllottedGeometry, const FString& Glyph,
		const FSlateFontInfo& Font, const FLinearColor& Color, float X, float Bottom) const;

	TArray<FHawkeyeThugOverhead> Entries;

	/** The colours the widget was built with, kept for the default palette. */
	FLinearColor DesignGlyphColor = FLinearColor::White;
	FLinearColor DesignBarColor = FLinearColor::White;
	bool bDesignColorsKept = false;
	FLinearColor DesignTelegraphColor = FLinearColor::White;
	FLinearColor DesignMarkerColor = FLinearColor::White;
	bool bDesignThreatColorsKept = false;
};
