#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/HawkeyeAccessibility.h"
#include "UI/HawkeyeMapMath.h"
#include "HawkeyeMapWidget.generated.h"

class APlayerController;
class UCityMapData;

/**
 * The world map (claude-docs/gameplay-semantics.md, "World map"): the generated district drawn from
 * UCityMapData (footprints, the park, streets) on a near-black ground, north up, fitted with a 16 px
 * gutter, and the icons over it: the player and partner as arrows, the current objective, the safehouses,
 * the pedestals, the crime in progress, the interior door and other secondary markers. A legend row runs
 * along the bottom. A cursor (the mouse, or the left stick) over an icon names it with its distance;
 * Enter, A or a click on a safehouse or pedestal marks it (the same marker "Mark nearest" puts up, so
 * the world marker and the compass follow). The wheel or the right stick zooms 1x to 3x about the
 * cursor; a drag or the left stick at the edge pans. M, Escape, B or Menu close it.
 *
 * AHawkeyePlayerController opens it (M, or a hold of D-pad up) and pauses under it, like the inventory.
 * Everything is painted in NativePaint; there is no widget tree.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHawkeyeMapWidget(const FObjectInitializer& ObjectInitializer);

	/** Loads the map data (if needed), resets the view, puts the cursor on the player and takes focus. */
	UFUNCTION(BlueprintCallable, Category = "Map")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Map")
	void Close();

	UFUNCTION(BlueprintPure, Category = "Map")
	bool IsOpen() const { return bOpen; }

	/** Re-reads the icons from the world (the widget does this every tick while open). */
	UFUNCTION(BlueprintCallable, Category = "Map")
	void RefreshIcons();

	UFUNCTION(BlueprintPure, Category = "Map")
	const TArray<FHawkeyeMapIcon>& GetIcons() const { return Icons; }

	/** The icon under the cursor, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Map")
	int32 GetHoveredIndex() const { return HoveredIndex; }

	/** Moves the cursor onto the first icon of Kind (and hovers it). False when there is none. */
	bool MoveCursorToIcon(EHawkeyeMapIconKind Kind, int32 Nth = 0);

	/** Marks the hovered icon if it is a safehouse or pedestal (MarkIcon). */
	UFUNCTION(BlueprintCallable, Category = "Map")
	bool MarkHovered();

	UFUNCTION(BlueprintPure, Category = "Map")
	UCityMapData* GetMapData() const { return MapData; }

	const FHawkeyeMapView& GetView() const { return View; }

	/**
	 * The map's state from WorldContextObject's world as PC sees it: the pawn and partner, the objective,
	 * every safehouse (found from the save), pedestal (best medal from its record) and interior door, the
	 * crime in progress, the secondary markers, and which place is marked.
	 */
	static FHawkeyeMapState GatherState(const UObject* WorldContextObject, const APlayerController* PC);

	/**
	 * Marks Icon's safehouse or pedestal with the "Mark nearest" plumbing and takes down the other kind's
	 * marker, so one place is marked from the map at a time. On the one already marked it takes the marker
	 * down instead. False for any other icon or a place that is gone.
	 */
	static bool MarkIcon(const UObject* WorldContextObject, const FHawkeyeMapIcon& Icon);

	/** The map data asset; DA_EastVillage_Map by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map")
	TSoftObjectPtr<UCityMapData> MapDataAsset;

	/** Space kept clear round the map, px (times the HUD scale for the legend only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "0.0"))
	float Gutter = 16.f;

	/** The legend row's height, px before the HUD scale. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "16.0"))
	float LegendHeight = 40.f;

	/** Cursor speed at full left-stick deflection, px per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "50.0"))
	float CursorSpeed = 700.f;

	/** Zoom per second at full right-stick deflection (a factor: 2 doubles it in a second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "1.1"))
	float StickZoomRate = 2.5f;

	/** Zoom factor per wheel notch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "1.01"))
	float WheelZoomStep = 1.25f;

	/** How near the cursor has to be to an icon to hover it, px before the HUD scale. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "4.0"))
	float HoverRadius = 16.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor BackgroundColor = FLinearColor(0.004f, 0.004f, 0.006f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor FootprintColor = FLinearColor(0.045f, 0.045f, 0.05f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor ParkColor = FLinearColor(0.03f, 0.07f, 0.03f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor StreetColor = FLinearColor(0.1f, 0.1f, 0.11f, 1.f);

	/** The partner's arrow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor PartnerColor = FLinearColor(0.55f, 0.55f, 0.58f, 1.f);

	/** The interior door. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map|Colors")
	FLinearColor DoorColor = FLinearColor(0.75f, 0.72f, 0.66f, 1.f);

protected:
	//~ Begin UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	/** Asks the controller to close (it owns the pause). */
	void RequestClose();

	/** Loads MapData and triangulates its footprints and park once. */
	void EnsureMapData();

	/** Fits the view to Size (the widget's local size) with the gutter and the legend taken off. */
	void UpdateFrame(const FVector2D& Size);

	/** Moves the cursor by the stick and zooms by the right stick, real seconds. */
	void ApplySticks(float DeltaSeconds);

	/** The HUD scale and palette from the settings. */
	void ReadSettings();

	void PaintGround(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintIcon(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FHawkeyeMapIcon& Icon,
		const FVector2D& At, float Scale) const;
	void PaintCursor(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintLegend(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	/** Every triangle of every footprint (or the park) in world XY, three points each. */
	void PaintTriangles(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId,
		const TArray<FVector2D>& Triangles, const FLinearColor& Color) const;

	/** A filled arrow at At pointing along AngleRadians (clockwise from screen right). */
	void PaintArrow(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2D& At,
		float AngleRadians, float Length, const FLinearColor& Color) const;

	/** "123 m" from the player to Icon, or empty when there is no player. */
	FText GetDistanceText(const FHawkeyeMapIcon& Icon) const;

	/** The hover label: the name, the medal for a pedestal, the distance, and the mark hint. */
	FText GetHoverText(const FHawkeyeMapIcon& Icon) const;

	bool IsUsingGamepad() const;

	UPROPERTY(Transient)
	TObjectPtr<UCityMapData> MapData = nullptr;

	UPROPERTY(Transient)
	TArray<FHawkeyeMapIcon> Icons;

	/** Footprint and park triangles, world XY, three points per triangle. */
	TArray<FVector2D> FootprintTriangles;
	TArray<FVector2D> ParkTriangles;

	FHawkeyeMapView View;
	FHawkeyeMapState State;
	FHawkeyePalette Palette;
	float HudScale = 1.f;

	/** Widget-local px. */
	FVector2D Cursor = FVector2D::ZeroVector;
	FVector2D LeftStick = FVector2D::ZeroVector;
	FVector2D RightStick = FVector2D::ZeroVector;
	FVector2D LastSize = FVector2D::ZeroVector;
	int32 HoveredIndex = INDEX_NONE;

	/** "[Safehouse marked]" and the like, shown for StatusSeconds after a mark. */
	FText StatusText;
	float StatusSeconds = 0.f;

	bool bOpen = false;
	bool bPendingCentre = false;
	bool bDragging = false;
	bool bDragMoved = false;
	FVector2D DragLast = FVector2D::ZeroVector;
};
