#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/HawkeyeAccessibility.h"
#include "Settings/HawkeyeSettings.h"
#include "Player/GrappleComponent.h"
#include "HawkeyeHudWidget.generated.h"

/** How the grapple marker looks for one target state (UHawkeyeHudWidget::GetGrappleMarkerLook). */
struct FHawkeyeGrappleMarkerLook
{
	/** The diamond is drawn at all. */
	bool bVisible = false;
	/** Green (a press fires) rather than dim grey. */
	bool bReady = false;
	/** The key under the diamond, whenever a press fires (grapple arrows have no count, so never "none left"). */
	bool bHint = false;
};

class UBorder;
class UBowComponent;
class UChallengePanelWidget;
class UCrimePanelWidget;
class UCanvasPanel;
class UHawkeyeHintWidget;
class UHawkeyeHotbarWidget;
class UHawkeyeObjectiveWidget;
class UHawkeyeQuiverWheelWidget;
class UHawkeyeThugOverheadWidget;
class UGrappleComponent;
class UImage;
class UInventoryComponent;
class UOverlay;
class UTextBlock;
class UWidget;
class UTakedownComponent;
class UMissionDefinition;
class UMissionObjective;
class UWeaponComponent;

/**
 * Bare gameplay HUD: current objective, ammo, an interaction prompt, a third-person reticle and
 * an optional movement debug line.
 *
 * Reparent a UMG widget to this class and name widgets ObjectiveText, AmmoText, PromptText,
 * DebugText and Reticle to have them driven automatically; a subclass with no designer layout
 * works too, because RebuildWidget builds them itself (same approach as UFlashbackWidget).
 *
 * All of the state comes from delegates: the mission subsystem drives the objective line, the
 * pawn's UWeaponComponent drives the ammo line, and UInteractionComponent drives the prompt.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Subscribes to the mission subsystem and to OwningPawn's weapon. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToGame();

	/** Drops every subscription. Called from NativeDestruct. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void UnbindFromGame();

	/** Shows Prompt on the prompt line ("Open", "Press F to take down"). Empty text clears it. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetPrompt(FText Prompt);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ClearPrompt();

	/**
	 * Swaps a keyboard-flavoured hint ("[F] Take down") for its gamepad reading ("[Y] Take down")
	 * when the owning controller last saw a Gamepad_* key. Public so other widgets building their
	 * own hint strings (not routed through SetPrompt) can reuse it.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText ApplyGamepadHint(FText KeyboardHint) const;

	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetPrompt() const { return CurrentPrompt; }

	/** Re-reads the current objective and repaints the objective line. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshObjective();

	/** Re-reads the pawn's weapon and repaints the ammo line (blank while unarmed). */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshAmmo();

	/** "12 / 24", or empty when the pawn has no weapon. Pure so a test can assert on it. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	static FText FormatAmmo(int32 Magazine, int32 Reserve);

	// --- Hotbar ---------------------------------------------------------------------------------

	/** Hotbar built into the HUD. WBP_Hotbar when the content script has wired one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Hotbar")
	TSubclassOf<UHawkeyeHotbarWidget> HotbarWidgetClass;

	UFUNCTION(BlueprintPure, Category = "HUD|Hotbar")
	UHawkeyeHotbarWidget* GetHotbar() const { return Hotbar; }

	/** The first-time hint line just above the hotbar (UHawkeyeHintSubsystem's). */
	UFUNCTION(BlueprintPure, Category = "HUD|Hints")
	UHawkeyeHintWidget* GetHintLine() const { return HintLine; }

	// --- Quiver wheel ---------------------------------------------------------------------------

	/** The radial quiver, built into the HUD's overlay and collapsed until the pawn opens it. */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	UHawkeyeQuiverWheelWidget* GetQuiverWheel() const { return QuiverWheel; }

	/** Shows or hides the wheel with QuiverSlot highlighted (0 for none). AHawkeyeCharacter drives it. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Quiver Wheel")
	void SetQuiverWheelState(bool bOpen, int32 QuiverSlot);

	// --- Objective marker and compass -----------------------------------------------------------

	/** The objective marker, compass and toasts, built into the HUD's overlay. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	UHawkeyeObjectiveWidget* GetObjectiveMarker() const { return ObjectiveMarker; }

	/** The side-challenge panel (timer, score or checkpoint, best), top right while a run is on. */
	UFUNCTION(BlueprintPure, Category = "HUD|Challenge")
	UChallengePanelWidget* GetChallengePanel() const { return ChallengePanel; }

	/** The street crime's line ("[Crime: mugging] 38 m") and its state, top right while a crime is on. */
	UFUNCTION(BlueprintPure, Category = "HUD|Crime")
	UCrimePanelWidget* GetCrimePanel() const { return CrimePanel; }

	/** The thugs' "!" / "?" glyphs and health bars. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UHawkeyeThugOverheadWidget* GetThugOverhead() const { return ThugOverhead; }

	// --- Reticle --------------------------------------------------------------------------------

	/** Shows the reticle while aiming, hides it otherwise. Driven from the pawn every frame. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Reticle")
	void SetReticleAiming(bool bNewAiming);

	/**
	 * Third person has no crosshair at the hip: the body and the camera say where you face. The
	 * dot only appears while aiming, where the arrow will go.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	bool IsReticleVisible() const { return bReticleAiming; }

	/** Side of the square dot, in pixels. */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	float GetReticleSize() const { return ReticleSizePixels; }

	/** White while a hit marker is flashing, ReticleColor otherwise. Pure so a test can read it. */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	FLinearColor GetReticleColor() const;

	/** Flashes the dot white for HitFlashSeconds. Bound to UWeaponComponent::OnHit and UBowComponent::OnHit. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Reticle")
	void FlashHitMarker();

	/**
	 * Radius in widget pixels of a cone of SpreadDegrees seen at Distance: the circle it covers
	 * there (Distance * tan(spread)), projected through a camera of FovDegrees horizontal FOV onto a
	 * viewport ViewportWidth wide. Pure so the ring's size is testable.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	static float ComputeSpreadRingRadius(float SpreadDegrees, float FovDegrees, float ViewportWidth, float Distance = 1500.f);

	/** Shows the spread ring (InRingRadius widget px) and the draw bar. Driven from the pawn's bow every frame. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Reticle")
	void SetDrawState(bool bInDrawing, float InFraction, float InRingRadius, bool bInPerfect);

	/** True while the bow is drawn: the ring and the draw bar are up. */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	bool IsDrawIndicatorVisible() const { return bDrawing; }

	/** Current ring radius in widget pixels (0 while not drawing). */
	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	float GetReticleRingRadius() const { return RingRadius; }

	UFUNCTION(BlueprintPure, Category = "HUD|Reticle")
	float GetDrawBarFraction() const { return DrawFraction; }

	// --- Grapple marker -------------------------------------------------------------------------

	/**
	 * True while the pawn's grapple has a target on screen: green with the key hint when a press
	 * fires, dim grey when it would not (one still in flight). Hidden during a
	 * zip until the chain window opens, so the next anchor can be picked on the way in.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	bool IsGrappleMarkerVisible() const { return bGrappleMarkerVisible; }

	/** The marker is green: a press fires at it. */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	bool IsGrappleMarkerReady() const { return bGrappleMarkerVisible && GrappleLook.bReady; }

	/** The grey diamond on an anchor in view that the zip line cannot reach. */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	bool IsGrappleBlockedMarkerVisible() const { return bGrappleBlockedVisible; }

	/** The line under the marker now: the key, or empty. */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	FText GetGrappleHintShown() const { return GrappleHintShown; }

	/** The marker's look for a target state: the rule the HUD draws by, pure so a test can read it. */
	static FHawkeyeGrappleMarkerLook GetGrappleMarkerLook(EGrappleTargetState State);

	/** Centre of the marker in viewport widget space, where the anchor projects to. */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	FVector2D GetGrappleMarkerPosition() const { return GrappleMarkerPosition; }

	/** The line under the marker: always the key while a press fires. */
	UFUNCTION(BlueprintPure, Category = "HUD|Grapple")
	bool IsGrappleHintVisible() const;

	// --- Partner and switching --------------------------------------------------------------------

	/** The playable character's name, bottom left ("Kate"). The pawn's name wins on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Partner")
	void SetCharacterName(FText Name);

	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	FText GetCharacterNameShown() const { return CharacterNameShown; }

	/** The partner line under it ("Clint: following"); empty hides it. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Partner")
	void SetPartnerStatus(FText Status);

	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	FText GetPartnerStatusShown() const { return PartnerStatusShown; }

	/** "Clint: following". Pure so a test can read the format. */
	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	static FText FormatPartnerStatus(FText PartnerName, FText Status);

	/** The mark's key at the end of the partner line: "[T] send", or "[R3] send" when the last input was a pad. */
	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	static FText FormatPartnerMarkHint(bool bGamepad);

	/** A banter line, bottom centre, "Kate: ..." for Seconds. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Partner")
	void ShowSubtitle(FText Speaker, FText Line, float Seconds);

	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	bool IsSubtitleVisible() const { return SubtitleRemaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	FText GetSubtitleShown() const { return SubtitleShown; }

	/**
	 * The bottom of the screen is one stack: hotbar, the hint line's band over it, the subtitle over that.
	 * Places the subtitle SubtitleGapAboveHint over the hint band's top (UHawkeyeHintWidget::GetStackTop),
	 * never lower than SubtitleMinBottom, so the two never overlap at any HUD scale or subtitle size.
	 * Run every tick and on a scale change.
	 */
	void LayOutBottomStack();

	/** Where LayOutBottomStack put the subtitle's bottom edge, px up from the HUD's bottom. */
	float GetSubtitleBottom() const { return SubtitleBottom; }

	/** The subtitle's bottom edge: MinBottom, or Gap over HintTop when that is higher. */
	static float ComputeSubtitleBottom(float HintTop, float MinBottom, float Gap);

	/** The subtitle's lowest bottom edge, px (where it sits at HUD scale 1). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Partner")
	float SubtitleMinBottom = 150.f;

	/** Clear space between the hint line's band and the subtitle, px. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Partner")
	float SubtitleGapAboveHint = 8.f;

	/** True while the partner's name tag is drawn over his head (beyond PartnerTagMinDistance, on screen). */
	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	bool IsPartnerTagVisible() const { return bPartnerTagVisible; }

	/** The partner is far enough away for his name tag. */
	UFUNCTION(BlueprintPure, Category = "HUD|Partner")
	static bool ShouldShowPartnerTag(float Distance, float MinDistance) { return Distance > MinDistance; }

	/** Rebinds the weapon, takedown and hotbar to the owning player's current pawn after a switch. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Partner")
	void RebindToPawn();

	/** The name tag shows beyond this, cm: the near edge of the partner's follow band. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Partner", meta = (ClampMin = "0.0"))
	float PartnerTagMinDistance = 400.f;

	/** How far above the partner's capsule centre the tag floats, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Partner")
	float PartnerTagHeight = 120.f;

	// --- Phone badge ----------------------------------------------------------------------------

	/** The unread count the phone badge shows (0 hides the number; the phone outline stays). */
	UFUNCTION(BlueprintPure, Category = "HUD|Phone")
	int32 GetPhoneBadgeCount() const { return PhoneBadgeCount; }

	/** The combo counter's text ("x3"), empty under two hits. Pure so a test can read it. */
	UFUNCTION(BlueprintPure, Category = "HUD|Combo")
	static FText FormatComboCount(int32 Count);

	/** The count the combo counter shows now. */
	UFUNCTION(BlueprintPure, Category = "HUD|Combo")
	int32 GetShownComboCount() const { return ShownComboCount; }

	// --- Settings -------------------------------------------------------------------------------

	/**
	 * The HUD's share of the settings: the accent palette (reticle, grapple marker, markers, glyphs,
	 * health bars), the subtitle size and backing, and the HUD scale on the text lines, hotbar,
	 * markers and compass. Bound to the settings subsystem; tests call it directly.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Settings")
	void ApplySettings(const FHawkeyeSettings& Settings);

	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	float GetHudScale() const { return HudScale; }

	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	int32 GetSubtitleFontSize() const { return SubtitleFontSize; }

	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	float GetSubtitleBackgroundOpacity() const { return SubtitleBackgroundOpacity; }

	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	EHawkeyeColorPalette GetPaletteShown() const { return PaletteShown; }

	/** The palette's colours as the HUD is using them. */
	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	FHawkeyePalette GetActivePalette() const { return ActivePalette; }

	/** The grapple marker's colour now (the palette's green). */
	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	FLinearColor GetGrappleMarkerColor() const { return GrappleMarkerColor; }

	/** The render scale on the hotbar now. */
	UFUNCTION(BlueprintPure, Category = "HUD|Settings")
	float GetHotbarRenderScale() const;

	// --- Movement debug -------------------------------------------------------------------------

	/** The debug line's text, or empty while hawkeye.DebugMovement is 0 or there is no player. */
	UFUNCTION(BlueprintPure, Category = "HUD|Debug")
	FText GetMovementDebugText() const;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	//~ End UUserWidget interface

	/** Builds the dot into a screen-filling canvas so it lands on the exact centre pixel. */
	void BuildReticle(UOverlay* Root);

	/** Re-applies size, colour and visibility to the dot. */
	void RefreshReticle();

	/** Reads aim off the pawn; the HUD has no input of its own to listen to. */
	void PollPawnReticleState();

	/** A screen-filling canvas holding the hollow diamond and its key hint. */
	void BuildGrappleMarker(UOverlay* Root);

	/** Projects the pawn's grapple target onto the screen and moves (or hides) the marker. */
	void UpdateGrappleMarker();

	/** Shows or hides the debug line and repaints it. */
	void RefreshMovementDebug();

	/** A small phone outline bottom right with the unread count beside it. */
	void BuildPhoneBadge(UOverlay* Root);

	/** The combo counter's text block. */
	void BuildComboCounter(UOverlay* Root);

	/** Reads the pawn's combo count and bonus and updates the counter on a change. */
	void UpdateComboCounter();

	/** Reads UPhoneSubsystem's unread count and repaints the badge when it changed. */
	void UpdatePhoneBadge();

	/** Builds the name, partner line, subtitle and the name tag canvas. */
	void BuildPartnerWidgets(UOverlay* Root);

	/** Reads the pawn's name and the partner's status and position every frame. */
	void UpdatePartnerWidgets(float DeltaSeconds);

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleSettingsChanged(FHawkeyeSettings Settings);

	/** Recolours everything drawn in an accent: the dot, ring, bar, grapple marker, badge, combo, tag. */
	void ApplyPalette(EHawkeyeColorPalette Palette);

	/** Scales the HUD's parts about their own anchors, so each stays where it is on the screen. */
	void ApplyHudScale(float Scale);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleMissionComplete(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleAmmoChanged(int32 Magazine, int32 Reserve);

	UFUNCTION()
	void HandleTakedownPerformed(AActor* Target);

	UFUNCTION()
	void HandleWeaponHit(AActor* HitActor, float DamageDealt);

	UFUNCTION()
	void HandleBowHit(AActor* HitActor, float Damage, bool bHeadshot);

	/** The owning pawn's bow, or null. */
	UBowComponent* FindPawnBow() const;

	/** Reads the draw off the pawn's bow and the camera FOV, and resizes the ring and bar. */
	void PollPawnDrawState(const FGeometry& MyGeometry);

	/** Re-applies the ring's size and colour and the bar's fill and flash. */
	void RefreshDrawIndicator();

	/** The weapon the HUD is currently listening to; rebound whenever the pawn picks one up. */
	UWeaponComponent* FindPawnWeapon() const;

	/** Player-facing objective line ("Find a weapon"), or empty when there is nothing to show. */
	FText BuildObjectiveText() const;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ObjectiveText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmmoText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText = nullptr;

	/** hawkeye.DebugMovement line, under the objective. Collapsed while the cvar is 0. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Debug", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DebugText = nullptr;

	/** Screen-filling canvas the dot is anchored to. Built in RebuildWidget. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Reticle", meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> Reticle = nullptr;

	/** The dot itself: a coloured border, no texture. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Reticle")
	TObjectPtr<UBorder> ReticleDot = nullptr;

	/** The spread ring around the dot while drawing: an outlined circle. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Reticle")
	TObjectPtr<UImage> ReticleRing = nullptr;

	/** Thin bar under the reticle: its track, and the fill that grows to full draw. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Reticle")
	TObjectPtr<UBorder> DrawBarBack = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Reticle")
	TObjectPtr<UBorder> DrawBarFill = nullptr;

	/** Distance the spread ring is measured at, cm. At 15 m the ring is the circle an arrow lands in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "100.0"))
	float ReticleSpreadDistance = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "0.5"))
	float RingLineWidth = 1.5f;

	/** Radius of the white ring a hit flashes around the dot when the bow is not drawn, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "1.0"))
	float HitRingRadius = 9.f;

	/** Smallest the ring is drawn, so it never collapses onto the dot, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "0.0"))
	float MinRingRadius = 6.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "1.0"))
	float DrawBarWidth = 64.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "1.0"))
	float DrawBarHeight = 3.f;

	/** How far below the centre the bar sits, px: under the widest ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle")
	float DrawBarOffset = 84.f;

	/** The bar during the perfect-release window. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle")
	FLinearColor PerfectColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

	UPROPERTY(Transient)
	bool bDrawing = false;

	UPROPERTY(Transient)
	bool bPerfectWindow = false;

	UPROPERTY(Transient)
	float DrawFraction = 0.f;

	UPROPERTY(Transient)
	float RingRadius = 0.f;

	/** Bow currently bound to OnHit. */
	UPROPERTY(Transient)
	TObjectPtr<UBowComponent> BoundBow = nullptr;

	/** Kate purple, light enough to read against a night sky and a grey wall alike. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle")
	FLinearColor ReticleColor = FLinearColor(0.78f, 0.55f, 1.f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle")
	FLinearColor HitMarkerColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "1.0"))
	float ReticleSizePixels = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Reticle", meta = (ClampMin = "0.0"))
	float HitFlashSeconds = 0.1f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Reticle")
	bool bReticleAiming = false;

	/** Seconds of hit-marker flash left to run. */
	UPROPERTY(Transient)
	float HitFlashRemaining = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Grapple")
	TObjectPtr<UCanvasPanel> GrappleCanvas = nullptr;

	/** A square outline turned 45 degrees: a hollow diamond. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Grapple")
	TObjectPtr<UImage> GrappleMarker = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Grapple")
	TObjectPtr<UTextBlock> GrappleHint = nullptr;

	/** The dim grey diamond on an anchor whose zip line is blocked. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Grapple")
	TObjectPtr<UImage> GrappleBlockedMarker = nullptr;

	/** Why it is grey, under it, only with hawkeye.DebugGrapple 1. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Grapple")
	TObjectPtr<UTextBlock> GrappleBlockedText = nullptr;

	/** A grapple diamond a press would not fire at. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Grapple")
	FLinearColor GrappleUnavailableColor = FLinearColor(0.45f, 0.45f, 0.45f, 0.7f);

	/** The green of the old first-person crosshair: nothing else on screen is that colour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Grapple")
	FLinearColor GrappleMarkerColor = FLinearColor(0.22f, 1.f, 0.08f, 1.f);

	/** Point to point size of the diamond, in pixels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Grapple", meta = (ClampMin = "2.0"))
	float GrappleMarkerSizePixels = 14.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Grapple", meta = (ClampMin = "0.5"))
	float GrappleMarkerLineWidth = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Grapple")
	FText GrappleHintText;

	UPROPERTY(Transient)
	bool bGrappleMarkerVisible = false;

	FHawkeyeGrappleMarkerLook GrappleLook;
	bool bGrappleBlockedVisible = false;
	FText GrappleHintShown;
	/** The colour the marker's outline was last painted, so the brush is only rebuilt on a change. */
	FLinearColor GrappleMarkerPainted = FLinearColor::Transparent;

	UPROPERTY(Transient)
	FVector2D GrappleMarkerPosition = FVector2D::ZeroVector;

	/** Shown on the objective line once every required objective is done. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	FText MissionCompleteText;

	UPROPERTY(Transient)
	FText CurrentPrompt;

	/** Weapon currently bound to OnAmmoChanged, so the binding can be swapped on pickup. */
	UPROPERTY(Transient)
	TObjectPtr<UWeaponComponent> BoundWeapon = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Objective")
	TObjectPtr<UHawkeyeObjectiveWidget> ObjectiveMarker = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UHawkeyeThugOverheadWidget> ThugOverhead = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UChallengePanelWidget> ChallengePanel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCrimePanelWidget> CrimePanel = nullptr;

	/** The hotbar along the bottom of the screen. Built into the HUD's own overlay. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Hotbar")
	TObjectPtr<UHawkeyeHotbarWidget> Hotbar = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Hints")
	TObjectPtr<UHawkeyeHintWidget> HintLine = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Quiver Wheel")
	TObjectPtr<UHawkeyeQuiverWheelWidget> QuiverWheel = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UTextBlock> CharacterNameText = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UTextBlock> PartnerStatusText = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UTextBlock> SubtitleText = nullptr;

	/** The box behind the subtitle; its opacity is the setting's. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UBorder> SubtitleBox = nullptr;

	/** The phone outline and its count, bottom right. */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> PhoneBadge = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UCanvasPanel> PartnerTagCanvas = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD|Partner")
	TObjectPtr<UTextBlock> PartnerTag = nullptr;

	/** The takedown component OnTakedownPerformed is bound on, so a switch unbinds the right one. */
	UPROPERTY(Transient)
	TObjectPtr<UTakedownComponent> BoundTakedown = nullptr;

	UPROPERTY(Transient)
	FText CharacterNameShown;

	UPROPERTY(Transient)
	FText PartnerStatusShown;

	UPROPERTY(Transient)
	FText SubtitleShown;

	UPROPERTY(Transient)
	float SubtitleRemaining = 0.f;

	UPROPERTY(Transient)
	bool bPartnerTagVisible = false;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PhoneIcon = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PhoneBadgeText = nullptr;

	UPROPERTY(Transient)
	int32 PhoneBadgeCount = -1;

	/** The combo counter: small, right of centre, cream, purple once the bonus is on. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ComboText = nullptr;

	UPROPERTY(Transient)
	int32 ShownComboCount = -1;

	UPROPERTY(Transient)
	bool bShownComboBonus = false;

	/** The counter's colour, and its colour at the bonus. */
	UPROPERTY(EditAnywhere, Category = "HUD|Combo")
	FLinearColor ComboColor = FLinearColor(0.96f, 0.92f, 0.82f, 1.f);

	UPROPERTY(EditAnywhere, Category = "HUD|Combo")
	FLinearColor ComboBonusColor = FLinearColor(0.72f, 0.35f, 1.f, 1.f);

	UPROPERTY(Transient)
	float HudScale = 1.f;

	UPROPERTY(Transient)
	int32 SubtitleFontSize = 26;

	UPROPERTY(Transient)
	float SubtitleBottom = -1.f;

	UPROPERTY(Transient)
	float SubtitleBackgroundOpacity = 0.f;

	UPROPERTY(Transient)
	EHawkeyeColorPalette PaletteShown = EHawkeyeColorPalette::Default;

	UPROPERTY(Transient)
	FHawkeyePalette ActivePalette;

	/** The designer's accent colours, kept so the default palette can put them back. */
	FLinearColor DesignReticleColor = FLinearColor::White;
	FLinearColor DesignGrappleColor = FLinearColor::White;
	FLinearColor DesignComboColor = FLinearColor::White;
	FLinearColor DesignComboBonusColor = FLinearColor::White;
	bool bDesignColorsKept = false;

	bool bBound = false;
};
