#include "Traversal/GrappleReachWorld.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "World/CityLedgeSpawner.h"
#include "World/CityMapData.h"
#include "World/GrappleAnchor.h"

const FName FGrappleReachWorld::BuildingTag(TEXT("CityBuilding"));

namespace HawkeyeGrappleReachWorld
{
	/** How far below an anchor's landing point its building is looked for (UGrappleComponent's SupportProbeDepth). */
	static constexpr float SupportProbeDepth = 150.f;

	/** A sidewalk candidate is traced from this high to this low, cm (Hawkeye.Grapple.Audit). */
	static constexpr float StreetTraceTop = 3000.f;
	static constexpr float StreetTraceBottom = -1000.f;

	/** The building under an anchor's landing point (Hawkeye.Grapple.Audit: 50 up, 150 down). */
	static constexpr float LandingProbeUp = 50.f;
	static constexpr float LandingProbeDepth = 150.f;
}

int32 FGrappleReachScene::FindRoofByOsm(const FString& OsmId) const
{
	const int32* Found = RoofByOsm.Find(OsmId);
	return Found ? *Found : INDEX_NONE;
}

// --- Profiles ------------------------------------------------------------------------------------

FGrappleReachProfile FGrappleReachWorld::MakeProfile(const APawn* Pawn)
{
	FGrappleReachProfile Profile;
	Profile.Source = Pawn ? Pawn->GetClass()->GetName() : FString(TEXT("defaults"));
	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Pawn);
	if (!Kate)
	{
		return Profile;
	}
	if (const UGrappleComponent* Grapple = Kate->GetGrappleComponent())
	{
		Profile.Rules = Grapple->MakeReachRules();
	}
	if (const UCapsuleComponent* Capsule = Kate->GetCapsuleComponent())
	{
		Profile.Rules.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		Profile.Rules.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		Profile.CapsuleObjectType = Capsule->GetCollisionObjectType();
		Profile.CapsuleResponses = Capsule->GetCollisionResponseToChannels();
	}
	if (const USpringArmComponent* Boom = Kate->GetCameraBoom())
	{
		Profile.Lens.PivotOffset = Boom->GetRelativeLocation();
		Profile.bProbeLens = Boom->bDoCollisionTest;
		Profile.ProbeChannel = Boom->ProbeChannel;
		Profile.ProbeSize = Boom->ProbeSize;
	}
	// ClampCameraPitch is the only public view of CameraPitchMin/Max.
	Profile.Lens.PitchMin = Kate->ClampCameraPitch(-90.f);
	Profile.Lens.PitchMax = Kate->ClampCameraPitch(90.f);
	TWeakObjectPtr<const AHawkeyeCharacter> WeakKate(Kate);
	Profile.Lens.Targets = [WeakKate](float PitchDegrees, float& OutArmLength, FVector& OutSocketOffset, float& OutPivotLift)
	{
		const AHawkeyeCharacter* Live = WeakKate.Get();
		if (!Live)
		{
			return;
		}
		const FHawkeyeCameraTargets Targets = Live->ComputeCameraTargets(/*bAiming=*/false, PitchDegrees);
		OutArmLength = Targets.ArmLength;
		OutSocketOffset = Targets.SocketOffset;
		OutPivotLift = Targets.PivotLift;
	};
	return Profile;
}

FGrappleReachProfile FGrappleReachWorld::MakePlayerProfile(const UWorld* World, TSubclassOf<APawn> PawnClass)
{
	if (!PawnClass || !PawnClass->IsChildOf(AHawkeyeCharacter::StaticClass()))
	{
		PawnClass = FindPlayerPawnClass(World);
	}
	if (!PawnClass || !PawnClass->IsChildOf(AHawkeyeCharacter::StaticClass()))
	{
		PawnClass = AHawkeyeCharacter::StaticClass();
	}
	return MakeProfile(PawnClass->GetDefaultObject<APawn>());
}

TSubclassOf<APawn> FGrappleReachWorld::FindPlayerPawnClass(const UWorld* World)
{
	const AWorldSettings* Settings = World ? World->GetWorldSettings(false, false) : nullptr;
	const TSubclassOf<AGameModeBase> GameMode = Settings ? Settings->DefaultGameMode : nullptr;
	const AGameModeBase* GameModeDefault = GameMode ? GameMode->GetDefaultObject<AGameModeBase>() : nullptr;
	return GameModeDefault ? GameModeDefault->DefaultPawnClass : nullptr;
}

ACityLedgeSpawner* FGrappleReachWorld::FindSpawner(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACityLedgeSpawner> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FString FGrappleReachWorld::OsmOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}
	for (const FName& Tag : Actor->Tags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(TEXT("osm:")))
		{
			return Text.Mid(4);
		}
	}
	return FString();
}

FGrappleReachBlocker FGrappleReachWorld::DescribeBlocker(const AActor* Actor)
{
	FGrappleReachBlocker Blocker;
	if (!Actor)
	{
		Blocker.Kind = TEXT("nothing");
		return Blocker;
	}
	Blocker.Name = Actor->GetName();
	if (Actor->Tags.Contains(BuildingTag))
	{
		Blocker.Kind = TEXT("a building");
		Blocker.OsmId = OsmOf(Actor);
		return Blocker;
	}
	Blocker.Kind = Actor->GetClass()->GetName();
	Blocker.Kind.RemoveFromEnd(TEXT("_C"));
	return Blocker;
}

// --- The scene -----------------------------------------------------------------------------------

AActor* FGrappleReachWorld::FindBuildingUnder(const UWorld* World, const FVector& Point, float Up, float Depth, FVector* OutTop)
{
	if (!World)
	{
		return nullptr;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachUnder), false);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, Point + FVector(0.f, 0.f, Up), Point - FVector(0.f, 0.f, Depth),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Actor = Hit.GetActor();
		if (Actor && Actor->Tags.Contains(BuildingTag))
		{
			if (OutTop)
			{
				*OutTop = Hit.ImpactPoint;
			}
			return Actor;
		}
	}
	return nullptr;
}

FGrappleReachQuery::EStand FGrappleReachWorld::FindSidewalkFloor(const UWorld* World, const FVector2D& Point, float MaxFloorZ,
	FVector& OutFloor)
{
	using namespace HawkeyeGrappleReachWorld;
	if (!World)
	{
		return FGrappleReachQuery::EStand::NoGround;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachStreet), false);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, StreetTraceTop), FVector(Point.X, Point.Y, StreetTraceBottom),
			ECC_Visibility, Params)
		|| !Hit.GetActor())
	{
		return FGrappleReachQuery::EStand::NoGround;
	}
	if (Hit.GetActor()->Tags.Contains(BuildingTag) || Hit.ImpactPoint.Z > MaxFloorZ)
	{
		return FGrappleReachQuery::EStand::Blocked;
	}
	OutFloor = Hit.ImpactPoint;
	return FGrappleReachQuery::EStand::Stand;
}

bool FGrappleReachWorld::BuildScene(UWorld* World, const UCityMapData* Map, const UCityLedgeData* Props, float MarkerHeight,
	FGrappleReachScene& OutScene, FString& OutError)
{
	using namespace HawkeyeGrappleReachWorld;
	OutScene = FGrappleReachScene();
	if (!World || !Map || !Props)
	{
		OutError = FString::Printf(TEXT("needs a world (%s), DA_EastVillage_Map (%s) and the city props (%s)"),
			World ? TEXT("ok") : TEXT("missing"), Map ? TEXT("ok") : TEXT("missing"), Props ? TEXT("ok") : TEXT("missing"));
		return false;
	}

	// Roofs: every generated building, its outline from the map where the map has it, else its bounds.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->Tags.Contains(BuildingTag))
		{
			continue;
		}
		FGrappleReachRoof Roof;
		Roof.OsmId = OsmOf(*It);
		Roof.ActorName = It->GetName();
		const FCityMapPolygon* Footprint = Map->FindFootprint(Roof.OsmId);
		if (Footprint && Footprint->Points.Num() >= 3)
		{
			Roof.Ring = Footprint->Points;
			Roof.HeightM = Footprint->HeightM;
		}
		else
		{
			FVector Origin, Extent;
			It->GetActorBounds(false, Origin, Extent);
			Roof.Ring = { FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y), FVector2D(Origin.X + Extent.X, Origin.Y - Extent.Y),
				FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y), FVector2D(Origin.X - Extent.X, Origin.Y + Extent.Y) };
			Roof.HeightM = static_cast<float>((Origin.Z + Extent.Z) / 100.0);
		}
		Roof.UpdateBox();
		OutScene.RoofByOsm.Add(Roof.OsmId, OutScene.Roofs.Num());
		OutScene.RoofActors.Add(*It);
		OutScene.Roofs.Add(MoveTemp(Roof));
	}
	TMap<const AActor*, int32> RoofOfActor;
	for (int32 Index = 0; Index < OutScene.RoofActors.Num(); ++Index)
	{
		RoofOfActor.Add(OutScene.RoofActors[Index].Get(), Index);
	}

	// Anchors from the data, on the roof under the landing point (as the audit finds it), else the record's osm id.
	const ACityLedgeSpawner* Spawner = FindSpawner(World);
	const TArray<AGrappleAnchor*> Spawned = Spawner ? Spawner->GetSpawnedAnchors() : TArray<AGrappleAnchor*>();
	const bool bSpawnedMatch = Spawned.Num() == Props->Anchors.Num();
	for (int32 Index = 0; Index < Props->Anchors.Num(); ++Index)
	{
		const FCityAnchorRecord& Record = Props->Anchors[Index];
		FGrappleReachAnchor Anchor;
		Anchor.Origin = Record.Transform.GetLocation();
		Anchor.Marker = Anchor.Origin + Record.Transform.GetRotation().GetUpVector() * MarkerHeight;
		Anchor.Landing = Record.Transform.TransformPosition(Record.LandingOffset);
		Anchor.Index = Record.Index;
		Anchor.Name = FString::Printf(TEXT("City_Anchor_%d"), Record.Index);
		const AActor* Under = FindBuildingUnder(World, Anchor.Landing, LandingProbeUp, LandingProbeDepth);
		const int32* ByTrace = Under ? RoofOfActor.Find(Under) : nullptr;
		Anchor.Roof = ByTrace ? *ByTrace : OutScene.FindRoofByOsm(Record.OsmId);
		OutScene.Anchors.Add(MoveTemp(Anchor));
		OutScene.AnchorActors.Add(bSpawnedMatch ? TWeakObjectPtr<AActor>(Spawned[Index]) : TWeakObjectPtr<AActor>());
	}

	OutScene.Ledges = Props->Ledges;
	OutScene.FireEscapes = Props->FireEscapes;
	for (const FCityMapStreet& Street : Map->Streets)
	{
		FGrappleReachStreet& Out = OutScene.Streets.AddDefaulted_GetRef();
		Out.Points = Street.Points;
		Out.WidthM = Street.WidthM;
	}
	OutScene.Bounds = FBox2D(Map->BoundsMin, Map->BoundsMax);

	// Objective volumes carry the roof they stand on.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString Osm = OsmOf(*It);
		if (Osm.IsEmpty())
		{
			continue;
		}
		for (const FName& Tag : It->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(TEXT("objective:")))
			{
				OutScene.ObjectiveRoofs.Add(Text.Mid(10), Osm);
			}
		}
	}
	return true;
}

// --- The oracle ----------------------------------------------------------------------------------

FGrappleReachOracle FGrappleReachWorld::MakeOracle(UWorld* World, const FGrappleReachProfile& Profile, const FGrappleReachScene* Scene)
{
	using namespace HawkeyeGrappleReachWorld;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const FGrappleReachRules Rules = Profile.Rules;
	TMap<int32, TWeakObjectPtr<AActor>> AnchorActors;
	if (Scene)
	{
		for (int32 Index = 0; Index < Scene->Anchors.Num() && Index < Scene->AnchorActors.Num(); ++Index)
		{
			if (Scene->AnchorActors[Index].IsValid())
			{
				AnchorActors.Add(Scene->Anchors[Index].Index, Scene->AnchorActors[Index]);
			}
		}
	}

	FGrappleReachOracle Oracle;
	Oracle.HasLineOfSight = [WeakWorld, Rules, AnchorActors](const FVector& Lens, const FGrappleReachAnchor& Anchor,
		FGrappleReachBlocker* OutBlocker) -> bool
	{
		const UWorld* Live = WeakWorld.Get();
		if (!Live)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachSight), false);
		const TWeakObjectPtr<AActor>* Spawned = AnchorActors.Find(Anchor.Index);
		if (Spawned && Spawned->IsValid())
		{
			Params.AddIgnoredActor(Spawned->Get());
		}
		FHitResult Hit;
		if (!Live->LineTraceSingleByChannel(Hit, Lens, Anchor.Marker, ECC_Visibility, Params))
		{
			return true;
		}
		// The anchor sits on a parapet; a hit on the stone right under it still means it is in view.
		if (Hit.Distance >= FVector::Dist(Lens, Anchor.Marker) - Rules.SightTolerance)
		{
			return true;
		}
		if (OutBlocker)
		{
			*OutBlocker = DescribeBlocker(Hit.GetActor());
		}
		return false;
	};

	const TEnumAsByte<ECollisionChannel> ObjectType = Profile.CapsuleObjectType;
	const FCollisionResponseContainer Responses = Profile.CapsuleResponses;
	Oracle.IsZipClear = [WeakWorld, Rules, ObjectType, Responses](const FVector& From, const FGrappleReachAnchor& Anchor, bool bFromGround,
		FGrappleReachBlocker* OutBlocker) -> bool
	{
		const UWorld* Live = WeakWorld.Get();
		if (!Live)
		{
			return false;
		}
		const FVector Launch = FGrappleReachQuery::ComputeZipLaunch(Rules, From, bFromGround);
		const FVector End = FGrappleReachQuery::ComputeZipEnd(Rules, Anchor.Landing);

		// The anchor's supports: the building under its landing point and any static actor within SupportRadius.
		TArray<AActor*> AnchorSupports;
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachSupport), true);
			FHitResult Hit;
			if (Live->LineTraceSingleByChannel(Hit, Anchor.Landing + FVector(0.f, 0.f, 50.f),
					Anchor.Landing - FVector(0.f, 0.f, SupportProbeDepth), ECC_WorldStatic, Params)
				&& Hit.GetActor())
			{
				AnchorSupports.AddUnique(Hit.GetActor());
			}
			TArray<FOverlapResult> Overlaps;
			Live->OverlapMultiByObjectType(Overlaps, Anchor.Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
				FCollisionShape::MakeSphere(Rules.SupportRadius), Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (AActor* Actor = Overlap.GetActor())
				{
					AnchorSupports.AddUnique(Actor);
				}
			}
		}
		// The start's supports: what she stands on and the static actors within ZipStartIgnoreRadius.
		TArray<AActor*> StartSupports;
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachStartSupport), true);
			FHitResult Hit;
			if (Live->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, Rules.CapsuleHalfHeight + 50.f), ECC_WorldStatic, Params)
				&& Hit.GetActor())
			{
				StartSupports.AddUnique(Hit.GetActor());
			}
			TArray<FOverlapResult> Overlaps;
			Live->OverlapMultiByObjectType(Overlaps, From, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
				FCollisionShape::MakeSphere(Rules.ZipStartIgnoreRadius), Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (AActor* Actor = Overlap.GetActor())
				{
					StartSupports.AddUnique(Actor);
				}
			}
		}

		const FVector ReleasePoint = FGrappleReachQuery::ComputeStartReleasePoint(From, Launch, End, Rules.GetStartReleaseDistance());
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Rules.CapsuleRadius, Rules.CapsuleHalfHeight);
		const FCollisionResponseParams ResponseParams(Responses);
		auto Sweep = [&](const FVector& A, const FVector& B, bool bIgnoreStart) -> bool
		{
			if (A.Equals(B, 0.1f))
			{
				return true;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachZip), false);
			Params.AddIgnoredActors(AnchorSupports);
			if (bIgnoreStart)
			{
				Params.AddIgnoredActors(StartSupports);
			}
			FHitResult Hit;
			if (Live->SweepSingleByChannel(Hit, A, B, FQuat::Identity, ObjectType, Shape, Params, ResponseParams))
			{
				if (OutBlocker)
				{
					*OutBlocker = DescribeBlocker(Hit.GetActor());
				}
				return false;
			}
			return true;
		};
		return Sweep(Launch, ReleasePoint, true) && Sweep(ReleasePoint, End, false);
	};

	if (Profile.bProbeLens)
	{
		const TEnumAsByte<ECollisionChannel> ProbeChannel = Profile.ProbeChannel;
		const float ProbeSize = Profile.ProbeSize;
		Oracle.ProbeLens = [WeakWorld, ProbeChannel, ProbeSize](const FVector& Origin, const FVector& Desired) -> FVector
		{
			const UWorld* Live = WeakWorld.Get();
			if (!Live)
			{
				return Desired;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachProbe), false);
			FHitResult Hit;
			if (Live->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, ProbeChannel, FCollisionShape::MakeSphere(ProbeSize), Params))
			{
				return Hit.Location;
			}
			return Desired;
		};
	}
	return Oracle;
}
