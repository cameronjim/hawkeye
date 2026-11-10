#include "UI/HawkeyeMapMath.h"

void FHawkeyeMapView::SetFrame(const FVector2D& InWorldMin, const FVector2D& InWorldMax, const FVector2D& InAreaTopLeft,
	const FVector2D& InAreaSize)
{
	const bool bNewWorld = !InWorldMin.Equals(WorldMin) || !InWorldMax.Equals(WorldMax);
	WorldMin = InWorldMin;
	WorldMax = FVector2D(FMath::Max(InWorldMax.X, InWorldMin.X + 1.0), FMath::Max(InWorldMax.Y, InWorldMin.Y + 1.0));
	AreaTopLeft = InAreaTopLeft;
	AreaSize = FVector2D(FMath::Max(InAreaSize.X, 1.0), FMath::Max(InAreaSize.Y, 1.0));
	if (bNewWorld)
	{
		Reset();
	}
	ClampCentre();
}

void FHawkeyeMapView::Reset()
{
	Zoom = ZoomMin;
	Centre = (WorldMin + WorldMax) * 0.5;
}

double FHawkeyeMapView::GetFitScale() const
{
	const FVector2D World = WorldMax - WorldMin;
	return FMath::Min(AreaSize.X / FMath::Max(World.X, 1.0), AreaSize.Y / FMath::Max(World.Y, 1.0));
}

FVector2D FHawkeyeMapView::WorldToMap(const FVector2D& World) const
{
	return AreaTopLeft + AreaSize * 0.5 + (World - Centre) * GetScale();
}

FVector2D FHawkeyeMapView::MapToWorld(const FVector2D& Map) const
{
	return Centre + (Map - AreaTopLeft - AreaSize * 0.5) / GetScale();
}

void FHawkeyeMapView::ZoomAbout(const FVector2D& MapPoint, float NewZoom)
{
	const FVector2D Anchor = MapToWorld(MapPoint);
	Zoom = FMath::Clamp(NewZoom, ZoomMin, ZoomMax);
	Centre = Anchor - (MapPoint - AreaTopLeft - AreaSize * 0.5) / GetScale();
	ClampCentre();
}

void FHawkeyeMapView::PanPixels(const FVector2D& Delta)
{
	Centre -= Delta / GetScale();
	ClampCentre();
}

void FHawkeyeMapView::ClampCentre()
{
	const FVector2D Half = AreaSize * 0.5 / GetScale();
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const double Low = WorldMin[Axis] + Half[Axis];
		const double High = WorldMax[Axis] - Half[Axis];
		// Wider than the district on this axis (always at zoom 1 on one of them): centre it.
		Centre[Axis] = Low >= High ? (WorldMin[Axis] + WorldMax[Axis]) * 0.5 : FMath::Clamp(Centre[Axis], Low, High);
	}
}

bool FHawkeyeMapView::ContainsMapPoint(const FVector2D& Map) const
{
	return Map.X >= AreaTopLeft.X && Map.Y >= AreaTopLeft.Y && Map.X <= AreaTopLeft.X + AreaSize.X
		&& Map.Y <= AreaTopLeft.Y + AreaSize.Y;
}

namespace HawkeyeMapIcons
{
	static FHawkeyeMapIcon Make(EHawkeyeMapIconKind Kind, const FVector& Location, const FText& Name)
	{
		FHawkeyeMapIcon Icon;
		Icon.Kind = Kind;
		Icon.Location = Location;
		Icon.Name = Name;
		return Icon;
	}

	static FHawkeyeMapIcon MakePlace(EHawkeyeMapIconKind Kind, const FHawkeyeMapPlace& Place)
	{
		FHawkeyeMapIcon Icon = Make(Kind, Place.Location, Place.Name);
		Icon.Id = Place.Id;
		Icon.bDiscovered = Place.bDiscovered;
		Icon.Medal = Place.Medal;
		Icon.bMarked = Place.bMarked;
		return Icon;
	}

	/** True when Point is one of the places or the crime already drawn as its own icon. */
	static bool IsOwnedMarker(const FVector& Point, const FHawkeyeMapState& State)
	{
		auto Near = [&Point](const FVector& Other) { return FVector::Dist2D(Point, Other) < FHawkeyeMapIcons::SameSpotCm; };
		for (const TArray<FHawkeyeMapPlace>* List : { &State.Safehouses, &State.Challenges })
		{
			for (const FHawkeyeMapPlace& Place : *List)
			{
				if (Place.bMarked && Near(Place.Location))
				{
					return true;
				}
			}
		}
		return State.bCrimeActive && Near(State.CrimeLocation);
	}
}

TArray<FHawkeyeMapIcon> FHawkeyeMapIcons::Build(const FHawkeyeMapState& State)
{
	using namespace HawkeyeMapIcons;
	TArray<FHawkeyeMapIcon> Icons;
	for (const FHawkeyeMapPlace& Door : State.Doors)
	{
		Icons.Add(MakePlace(EHawkeyeMapIconKind::InteriorDoor, Door));
	}
	for (const FVector& Point : State.SecondaryMarkers)
	{
		if (!IsOwnedMarker(Point, State))
		{
			Icons.Add(Make(EHawkeyeMapIconKind::Secondary, Point, NSLOCTEXT("Hawkeye", "MapSecondary", "[Marker]")));
		}
	}
	for (const FHawkeyeMapPlace& Safehouse : State.Safehouses)
	{
		Icons.Add(MakePlace(EHawkeyeMapIconKind::Safehouse, Safehouse));
	}
	for (const FHawkeyeMapPlace& Challenge : State.Challenges)
	{
		Icons.Add(MakePlace(EHawkeyeMapIconKind::Challenge, Challenge));
	}
	if (State.bCrimeActive)
	{
		Icons.Add(Make(EHawkeyeMapIconKind::Crime, State.CrimeLocation, State.CrimeName));
	}
	if (State.bHasObjective)
	{
		Icons.Add(Make(EHawkeyeMapIconKind::Objective, State.ObjectiveLocation, State.ObjectiveName));
	}
	if (State.bHasPartner)
	{
		FHawkeyeMapIcon& Partner = Icons.Add_GetRef(Make(EHawkeyeMapIconKind::Partner, State.PartnerLocation, State.PartnerName));
		Partner.Yaw = State.PartnerYaw;
	}
	if (State.bHasPlayer)
	{
		FHawkeyeMapIcon& Player = Icons.Add_GetRef(Make(EHawkeyeMapIconKind::Player, State.PlayerLocation, State.PlayerName));
		Player.Yaw = State.PlayerYaw;
	}
	return Icons;
}

int32 FHawkeyeMapIcons::FindAt(const TArray<FHawkeyeMapIcon>& Icons, const FHawkeyeMapView& View, const FVector2D& MapPoint,
	float RadiusPx)
{
	int32 Best = INDEX_NONE;
	double BestDistance = RadiusPx;
	bool bBestMarkable = false;
	for (int32 Index = 0; Index < Icons.Num(); ++Index)
	{
		const double Distance = FVector2D::Distance(View.WorldToMap(FVector2D(Icons[Index].Location)), MapPoint);
		const bool bMarkable = Icons[Index].IsMarkable();
		if (Distance > RadiusPx || (bBestMarkable && !bMarkable))
		{
			continue;
		}
		// A markable icon in reach beats a nearer one that is not: the cursor is there to pick places.
		if (Distance <= BestDistance || (bMarkable && !bBestMarkable))
		{
			Best = Index;
			BestDistance = Distance;
			bBestMarkable = bMarkable;
		}
	}
	return Best;
}

FLinearColor FHawkeyeMapIcons::GetMedalColor(EChallengeMedal Medal, const FLinearColor& Unearned)
{
	switch (Medal)
	{
	case EChallengeMedal::Gold:
		return FLinearColor(1.f, 0.72f, 0.12f, 1.f);
	case EChallengeMedal::Silver:
		return FLinearColor(0.78f, 0.8f, 0.84f, 1.f);
	case EChallengeMedal::Bronze:
		return FLinearColor(0.72f, 0.36f, 0.14f, 1.f);
	default:
		return Unearned;
	}
}
