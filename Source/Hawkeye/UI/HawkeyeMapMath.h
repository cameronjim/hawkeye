#pragma once

#include "CoreMinimal.h"
#include "Challenge/ChallengeTypes.h"
#include "HawkeyeMapMath.generated.h"

/** What a world map icon stands for, which picks its glyph (claude-docs/gameplay-semantics.md, "World map"). */
UENUM(BlueprintType)
enum class EHawkeyeMapIconKind : uint8
{
	/** The character being played: a purple arrow along her heading. */
	Player,
	/** The other Hawkeye: a small grey arrow. */
	Partner,
	/** The current objective: the HUD's cream diamond. */
	Objective,
	/** A house, filled once found, hollow before. Markable. */
	Safehouse,
	/** A challenge pedestal's medal, in the colour of the best medal earned. Markable. */
	Challenge,
	/** The street crime in progress. */
	Crime,
	/** A door into an interior. */
	InteriorDoor,
	/** Any other secondary marker (a challenge's targets or next ring). */
	Secondary
};

/** One icon on the world map, in world space. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMapIcon
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Map")
	EHawkeyeMapIconKind Kind = EHawkeyeMapIconKind::Secondary;

	UPROPERTY(BlueprintReadOnly, Category = "Map")
	FVector Location = FVector::ZeroVector;

	/** World yaw, degrees (the arrows). */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	float Yaw = 0.f;

	/** What hovering shows. */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	FText Name;

	/** The safehouse or challenge id; none for the rest. */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	FName Id;

	/** A safehouse that has been found (drawn filled). */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	bool bDiscovered = false;

	/** A pedestal's best medal. */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	EChallengeMedal Medal = EChallengeMedal::None;

	/** The marked safehouse or pedestal (a ring round it). */
	UPROPERTY(BlueprintReadOnly, Category = "Map")
	bool bMarked = false;

	/** Safehouses and pedestals: Enter / A marks them. */
	bool IsMarkable() const { return Kind == EHawkeyeMapIconKind::Safehouse || Kind == EHawkeyeMapIconKind::Challenge; }
};

/** A safehouse, pedestal or door as the map sees it. */
struct HAWKEYE_API FHawkeyeMapPlace
{
	FName Id;
	FText Name;
	FVector Location = FVector::ZeroVector;
	bool bDiscovered = false;
	EChallengeMedal Medal = EChallengeMedal::None;
	bool bMarked = false;
};

/** Everything the map shows, read from the world once a frame (UHawkeyeMapWidget::GatherState). */
struct HAWKEYE_API FHawkeyeMapState
{
	bool bHasPlayer = false;
	FVector PlayerLocation = FVector::ZeroVector;
	float PlayerYaw = 0.f;
	FText PlayerName;

	bool bHasPartner = false;
	FVector PartnerLocation = FVector::ZeroVector;
	float PartnerYaw = 0.f;
	FText PartnerName;

	bool bHasObjective = false;
	FVector ObjectiveLocation = FVector::ZeroVector;
	FText ObjectiveName;

	TArray<FHawkeyeMapPlace> Safehouses;
	TArray<FHawkeyeMapPlace> Challenges;
	TArray<FHawkeyeMapPlace> Doors;

	bool bCrimeActive = false;
	FVector CrimeLocation = FVector::ZeroVector;
	FText CrimeName;

	/** Every secondary marker (the marked place's and the crime's among them). */
	TArray<FVector> SecondaryMarkers;
};

/**
 * The world map's view: the district's bounds fitted into the map area (north up: world -Y is screen
 * up, world +X is screen right, so the two frames share handedness), zoomed ZoomMin to ZoomMax about
 * a point and panned, never past the district's edge. Map points are widget-local pixels. Pure.
 */
struct HAWKEYE_API FHawkeyeMapView
{
	static constexpr float ZoomMin = 1.f;
	static constexpr float ZoomMax = 3.f;

	/** The world rectangle, cm. */
	FVector2D WorldMin = FVector2D::ZeroVector;
	FVector2D WorldMax = FVector2D(1.f, 1.f);

	/** The map area's top left and size, widget px (the widget less its gutter and legend). */
	FVector2D AreaTopLeft = FVector2D::ZeroVector;
	FVector2D AreaSize = FVector2D(1.f, 1.f);

	float Zoom = 1.f;

	/** The world point at the centre of the area. */
	FVector2D Centre = FVector2D(0.5f, 0.5f);

	/** Sets the bounds and the area, keeping the zoom and centre (clamped). */
	void SetFrame(const FVector2D& InWorldMin, const FVector2D& InWorldMax, const FVector2D& InAreaTopLeft,
		const FVector2D& InAreaSize);

	/** Zoom 1, centred on the district. */
	void Reset();

	/** Pixels per cm at zoom 1: the whole district fits the area. */
	double GetFitScale() const;

	/** Pixels per cm now. */
	double GetScale() const { return GetFitScale() * Zoom; }

	FVector2D WorldToMap(const FVector2D& World) const;
	FVector2D MapToWorld(const FVector2D& Map) const;

	/** A world yaw (degrees) as an angle on the map (radians, clockwise from screen right). */
	static float YawToMapAngle(float WorldYaw) { return FMath::DegreesToRadians(WorldYaw); }

	/** Zooms to NewZoom (clamped) keeping the world point under MapPoint where it is. */
	void ZoomAbout(const FVector2D& MapPoint, float NewZoom);

	/** Moves the drawn map by Delta px (a drag: the map follows the mouse). */
	void PanPixels(const FVector2D& Delta);

	/** Keeps the view inside the district: at zoom 1 it is centred. */
	void ClampCentre();

	bool ContainsMapPoint(const FVector2D& Map) const;
};

/** The icon set and the lookups the map widget and its tests share. Pure. */
struct HAWKEYE_API FHawkeyeMapIcons
{
	/**
	 * The icons for State, drawn in this order (the player last, on top): doors, other secondary markers,
	 * safehouses, pedestals, the crime, the objective, the partner, the player. A secondary marker within
	 * SameSpotCm (2D) of the marked place or the crime is theirs, not a separate icon.
	 */
	static TArray<FHawkeyeMapIcon> Build(const FHawkeyeMapState& State);

	/** The icon nearest MapPoint within RadiusPx on View, preferring markable ones; INDEX_NONE if none. */
	static int32 FindAt(const TArray<FHawkeyeMapIcon>& Icons, const FHawkeyeMapView& View, const FVector2D& MapPoint,
		float RadiusPx);

	/** A medal's colour: gold, silver, bronze, or Unearned for none. */
	static FLinearColor GetMedalColor(EChallengeMedal Medal, const FLinearColor& Unearned);

	static constexpr float SameSpotCm = 150.f;
};
