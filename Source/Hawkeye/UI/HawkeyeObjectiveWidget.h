#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "UI/ObjectiveMarkerMath.h"
#include "HawkeyeObjectiveWidget.generated.h"

class UMissionDefinition;
class UMissionObjective;

/**
 * Where the current objective is: a cream diamond projected onto its point with the distance
 * under it (clamped to the screen edge with an arrow when off screen, hidden within
 * HideWithinDistance), a compass strip at the top centre with the objective on it, and the
 * "Objective complete" / "New objective" toasts. Built into UHawkeyeHudWidget's overlay.
 *
 * Everything is painted in NativePaint from state NativeTick computes, so there is no widget tree
 * to keep in step; the arithmetic is in UObjectiveMarkerMath and tested there.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeObjectiveWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Queues a toast: a small heading ("Objective complete") over the objective's title. Sound plays
	 * when it comes up (at once, or when the one in front of it goes).
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Objective")
	void PushToast(FText Heading, FText Title, EHawkeyeUISound Sound = EHawkeyeUISound::Toast);

	/** A completed objective gets the "Objective complete" toast, unless it is an unmarked optional one. Pure. */
	static bool ShouldToastCompletion(const UMissionObjective* Objective);

	/** Drops every queued toast, the showing one included (a chapter ending supersedes them). */
	UFUNCTION(BlueprintCallable, Category = "HUD|Objective")
	void ClearToasts();

	/** Counts the showing toast down and moves on to the next queued one. Held toasts do not count. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Objective")
	void AdvanceToasts(float DeltaSeconds);

	/**
	 * Holds the queue: nothing shows, nothing counts down and no sound plays until released, when the
	 * front toast comes up for its full ToastSeconds with its sound. The chapter title card holds it
	 * (NativeTick asks the owning AHawkeyePlayerController), so "New objective" plays after the card.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Objective")
	void SetToastsHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	bool AreToastsHeld() const { return bToastsHeld; }

	/** A toast is on screen: queued and not held. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	bool IsToastVisible() const { return ToastQueue.Num() > 0 && !bToastsHeld; }

	/** A toast is waiting or showing. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	bool IsToastQueued() const { return ToastQueue.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FText GetToastHeading() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FText GetToastTitle() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	bool IsMarkerVisible() const { return bMarkerVisible; }

	/** This frame's placement, in viewport pixels. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FObjectiveMarkerPlacement GetMarkerPlacement() const { return Placement; }

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FText GetDistanceText() const { return DistanceText; }

	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	float GetViewBearing() const { return ViewBearing; }

	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	bool IsCompassIconVisible() const { return bCompassIconVisible; }

	/** Offset of the objective on the strip from its centre, px. */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	float GetCompassIconOffset() const { return CompassIconOffset; }

	/** The strip's width on screen, px (its CompassWidth times the HUD scale). */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	float GetCompassWidth() const { return CompassWidth * HudScale; }

	/** The diamond's size on screen, px (MarkerSizePixels times the HUD scale). */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	float GetMarkerSize() const { return MarkerSizePixels * HudScale; }

	/**
	 * Scales the markers, the compass and the toasts (0.8 to 1.4, the HUD scale setting). The markers
	 * stay on the points they mark; the compass and toasts grow about the top centre.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Objective")
	void SetHudScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	float GetHudScale() const { return HudScale; }

	/** The palette's cream (the marker, the compass caret) and purple (secondary markers); bDesign puts the built ones back. */
	void SetAccentColors(const FLinearColor& Cream, const FLinearColor& Purple, bool bDesign);

	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FLinearColor GetMarkerColor() const { return MarkerColor; }

	/** Secondary markers drawn this frame (they replace the objective's marker while there are any). */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	int32 GetSecondaryMarkerCount() const { return Secondary.Num(); }

	/** This frame's placement of secondary marker Index, in viewport pixels. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FObjectiveMarkerPlacement GetSecondaryPlacement(int32 Index) const
	{
		return Secondary.IsValidIndex(Index) ? Secondary[Index].Placement : FObjectiveMarkerPlacement();
	}

	/** Safehouses drawn on the compass this frame (a small house each: filled once found, hollow before). */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	int32 GetCompassSafehouseCount() const { return SafehouseIcons.Num(); }

	/** Whether compass house Index is a found safehouse (filled) rather than an unfound one (hollow). */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	bool IsCompassSafehouseDiscovered(int32 Index) const
	{
		return SafehouseIcons.IsValidIndex(Index) && SafehouseIcons[Index].bDiscovered;
	}

	/** Challenge pedestals within ChallengeCompassRange, drawn on the compass this frame (a small medal each). */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	int32 GetCompassChallengeCount() const { return ChallengeIcons.Num(); }

	/** Pedestals with a world marker (medal and distance) this frame. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	int32 GetChallengeMarkerCount() const { return ChallengeMarks.Num(); }

	/** The name under the marked safehouse's marker ("[Unknown safehouse]" before it is found); empty when none is marked. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	FText GetMarkedSafehouseLabel() const { return MarkedSafehouseLabel; }

	/** The downed ring is up this frame (the player is down and the fade has not started). */
	UFUNCTION(BlueprintPure, Category = "HUD|Downed")
	bool IsDownedRingVisible() const { return bDownedRing; }

	/** The line under the ring: "[Clint is coming]", "[Hold on]", or the key hint when nobody is. */
	UFUNCTION(BlueprintPure, Category = "HUD|Downed")
	FText GetDownedText() const { return DownedText; }

	/** "[You're down]" over the fade to the last save; empty otherwise. */
	UFUNCTION(BlueprintPure, Category = "HUD|Downed")
	FText GetDeathLine() const { return DeathLine; }

	/** Pedestals this close show on the compass, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass", meta = (ClampMin = "0.0"))
	float ChallengeCompassRange = 15000.f;

	/** Pedestals this close get a world marker when no objective is nearer, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "0.0"))
	float ChallengeMarkerRange = 8000.f;

	/** The partner this close to a downed player is reviving her ("[Hold on]"), not on his way, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Downed", meta = (ClampMin = "0.0"))
	float ReviverBesideDistance = 250.f;

	/** Offset from the strip's centre of safehouse icon Index, px. */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	float GetCompassSafehouseOffset(int32 Index) const
	{
		return SafehouseIcons.IsValidIndex(Index) ? SafehouseIcons[Index].Offset : 0.f;
	}

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	//~ End UUserWidget interface

	/** Projects the current objective's point and fills the marker and compass state. */
	void UpdateMarker(const FGeometry& MyGeometry);

	/**
	 * Projects the mission's secondary markers (a challenge's targets or next checkpoint). True when
	 * there are any, in which case they take the objective marker's place.
	 */
	bool UpdateSecondaryMarkers(APlayerController* PC, const FVector& PawnLocation, const FVector& CameraLocation);

	void PaintSecondary(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	/** Puts every safehouse on the compass strip, found or not. */
	void UpdateSafehouseIcons(const FVector& PawnLocation, const FVector& CameraLocation);

	/**
	 * Pedestals on the compass (within ChallengeCompassRange) and in the world (within ChallengeMarkerRange
	 * and nearer than ObjectiveDistance). None while a challenge runs.
	 */
	void UpdateChallengeIcons(APlayerController* PC, const FVector& PawnLocation, const FVector& CameraLocation,
		float ObjectiveDistance);

	/** The ring and its line while the pawn is down; the death line once the fade has started. */
	void UpdateDowned(const APawn* Pawn);

	void PaintChallengeIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintDowned(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	void PaintSafehouseIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	void PaintMarker(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintCompass(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintToast(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	/** Queues "New objective" when the current objective is no longer the one last seen. */
	void NoteCurrentObjective();

	/** Secondary markers: Kate's purple, smaller than the objective's diamond. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective")
	FLinearColor SecondaryColor = FLinearColor(0.78f, 0.55f, 1.f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "4.0"))
	float SecondarySizePixels = 12.f;

	/** Warm off-white: distinct from Kate's purple reticle and the grapple marker's green. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective")
	FLinearColor MarkerColor = FLinearColor(1.f, 0.93f, 0.78f, 1.f);

	/** Point to point size of the diamond, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "4.0"))
	float MarkerSizePixels = 16.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "0.5"))
	float MarkerLineWidth = 2.f;

	/** Closer than this (cm) the marker and the compass icon hide: you are there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "0.0"))
	float HideWithinDistance = 300.f;

	/** How far in from the screen edge an off-screen marker is held, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "0.0"))
	float EdgeMargin = 48.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass", meta = (ClampMin = "50.0"))
	float CompassWidth = 400.f;

	/** Degrees of heading the strip shows across its width. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass", meta = (ClampMin = "30.0", ClampMax = "360.0"))
	float CompassSpanDegrees = 180.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass", meta = (ClampMin = "5.0"))
	float CompassTickDegrees = 15.f;

	/** Distance from the top of the screen to the strip, px. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass")
	float CompassTop = 24.f;

	/**
	 * The world yaw that points north. The district generator puts north along -Y (East 11th
	 * Street has a smaller Y than East 6th), so -90.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass")
	float NorthYawDegrees = -90.f;

	/** The strip's backing: the hotbar's filled-slot black, lighter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Compass")
	FLinearColor CompassBackColor = FLinearColor(0.f, 0.f, 0.f, 0.35f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective", meta = (ClampMin = "0.1"))
	float ToastSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective")
	FText ObjectiveCompleteHeading;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Objective")
	FText NewObjectiveHeading;

private:
	struct FToast
	{
		FText Heading;
		FText Title;
		EHawkeyeUISound Sound = EHawkeyeUISound::Toast;
	};

	TArray<FToast> ToastQueue;

	/** Seconds the front toast has been up. */
	float ToastElapsed = 0.f;

	/** See SetToastsHeld. */
	bool bToastsHeld = false;

	/** Id of the objective that was current when last looked, so a change can be announced. */
	FName LastCurrentObjectiveId;

	/** What a secondary marker marks, which picks its glyph. */
	enum class ESecondaryKind : uint8 { Plain, Safehouse, Challenge };

	struct FSecondaryMark
	{
		FObjectiveMarkerPlacement Placement;
		float CompassOffset = 0.f;
		bool bCompassClamped = false;
		float Distance = 0.f;
		ESecondaryKind Kind = ESecondaryKind::Plain;
		/** A safehouse marker on one not found yet: a "?" in the house. */
		bool bUnknown = false;
	};

	struct FSafehouseIcon
	{
		float Offset = 0.f;
		bool bClamped = false;
		bool bDiscovered = false;
	};

	/** This frame's safehouse houses on the compass. */
	TArray<FSafehouseIcon> SafehouseIcons;

	/** This frame's pedestal medals on the compass (Offset and bClamped as for the houses). */
	TArray<FSafehouseIcon> ChallengeIcons;

	struct FChallengeMark
	{
		FObjectiveMarkerPlacement Placement;
		FText DistanceText;
	};

	/** This frame's pedestal world markers. */
	TArray<FChallengeMark> ChallengeMarks;

	FText MarkedSafehouseLabel;

	bool bDownedRing = false;
	float DownedFraction = 0.f;
	FText DownedText;
	FText DeathLine;

	/** This frame's secondary markers; the nearest one carries the distance. */
	TArray<FSecondaryMark> Secondary;
	int32 NearestSecondary = INDEX_NONE;
	FText SecondaryDistanceText;

	bool bMarkerVisible = false;
	FObjectiveMarkerPlacement Placement;
	FText DistanceText;

	/** Placement is in viewport pixels; this converts it to the widget's own units. */
	float ViewportScale = 1.f;
	FVector2D ViewRectMin = FVector2D::ZeroVector;

	float ViewBearing = 0.f;
	bool bCompassIconVisible = false;
	bool bCompassIconClamped = false;
	float CompassIconOffset = 0.f;

	float HudScale = 1.f;

	FLinearColor DesignMarkerColor = FLinearColor::White;
	FLinearColor DesignSecondaryColor = FLinearColor::White;
	bool bDesignColorsKept = false;

	bool bBound = false;
};
