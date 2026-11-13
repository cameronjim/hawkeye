#include "Traversal/GrappleReachWorld.h"

#include "AssetCompilingManager.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Hawkeye.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Traversal/GrappleTraces.h"
#include "World/CityLedgeSpawner.h"
#include "World/CityMapData.h"
#include "World/FireEscapeLanding.h"
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
	TActorIterator<ACityLedgeSpawner> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
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

AActor* FGrappleReachWorld::FindBuildingUnder(const UWorld* World, const FVector& Point, float Up, float Depth, FVector* OutTop,
	const FGrappleIgnoredActors& Ignore)
{
	if (!World)
	{
		return nullptr;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachUnder), false);
	Params.AddIgnoredActors(Ignore);
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
	FVector& OutFloor, const FGrappleIgnoredActors& Ignore)
{
	using namespace HawkeyeGrappleReachWorld;
	if (!World)
	{
		return FGrappleReachQuery::EStand::NoGround;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachStreet), false);
	Params.AddIgnoredActors(Ignore);
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

void FGrappleReachWorld::FlushPendingPhysicsState(UWorld* World)
{
	FPhysScene_Chaos* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysicsScene)
	{
		return;
	}
#if WITH_EDITOR
	// In the editor a static mesh still compiling gets no physics state (UStaticMeshComponent::ShouldCreatePhysicsState);
	// the compile's end recreates it. The editor's tick finishes them; a commandlet has to ask.
	if (const int32 Compiling = FAssetCompilingManager::Get().GetNumRemainingAssets())
	{
		UE_LOG(LogHawkeye, Log, TEXT("Grapple reach: finishing %d asset compilations before tracing."), Compiling);
		FAssetCompilingManager::Get().FinishAllCompilation();
	}
#endif
	// Bodies whose collision mesh was not cooked yet when their level loaded wait in the deferred list.
	PhysicsScene->ProcessDeferredCreatePhysicsState();
	// Each pass may queue more (a body that waited on another); a few passes empty it.
	for (int32 Pass = 0; Pass < 8 && PhysicsScene->HasAsyncPhysicsStateJobs(); ++Pass)
	{
		PhysicsScene->ProcessAsyncPhysicsStateJobs(/*bWaitForCompletion=*/true);
	}
}

namespace HawkeyeGrappleReachWorld
{
	/** Roofs: every generated building, its outline from the map where the map has it, else its bounds. */
	static void AddRoofs(UWorld* World, const UCityMapData* Map, FGrappleReachScene& OutScene)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(FGrappleReachWorld::BuildingTag))
			{
				continue;
			}
			FGrappleReachRoof Roof;
			Roof.OsmId = FGrappleReachWorld::OsmOf(*It);
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
	}

	static TMap<const AActor*, int32> RoofIndexByActor(const FGrappleReachScene& Scene)
	{
		TMap<const AActor*, int32> Out;
		for (int32 Index = 0; Index < Scene.RoofActors.Num(); ++Index)
		{
			Out.Add(Scene.RoofActors[Index].Get(), Index);
		}
		return Out;
	}

	/** Streets and bounds from the map; objective volumes carry the roof they stand on. */
	static void AddStreetsAndObjectives(UWorld* World, const UCityMapData* Map, FGrappleReachScene& OutScene)
	{
		for (const FCityMapStreet& Street : Map->Streets)
		{
			FGrappleReachStreet& Out = OutScene.Streets.AddDefaulted_GetRef();
			Out.Points = Street.Points;
			Out.WidthM = Street.WidthM;
		}
		OutScene.Bounds = FBox2D(Map->BoundsMin, Map->BoundsMax);

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const FString Osm = FGrappleReachWorld::OsmOf(*It);
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
	}
}

bool FGrappleReachWorld::BuildSceneFromActors(UWorld* World, const UCityMapData* Map, const AActor* Ignore, FGrappleReachScene& OutScene,
	FString& OutError)
{
	using namespace HawkeyeGrappleReachWorld;
	OutScene = FGrappleReachScene();
	if (!World || !Map)
	{
		OutError = FString::Printf(TEXT("needs a world (%s) and DA_EastVillage_Map (%s)"), World ? TEXT("ok") : TEXT("missing"),
			Map ? TEXT("ok") : TEXT("missing"));
		return false;
	}
	FlushPendingPhysicsState(World);
	AddRoofs(World, Map, OutScene);
	const TMap<const AActor*, int32> RoofOfActor = RoofIndexByActor(OutScene);
	FGrappleIgnoredActors IgnoreList;
	if (Ignore)
	{
		IgnoreList.Add(Ignore);
	}

	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		FGrappleReachAnchor Anchor;
		Anchor.Origin = It->GetActorLocation();
		Anchor.Marker = It->GetMarkerLocation();
		Anchor.Landing = It->GetLandingLocation();
		Anchor.bEnabled = It->bEnabled;
		Anchor.Index = OutScene.Anchors.Num();
		Anchor.Name = It->GetName();
		const AActor* Under = FindBuildingUnder(World, Anchor.Landing, LandingProbeUp, LandingProbeDepth, nullptr, IgnoreList);
		const int32* Roof = Under ? RoofOfActor.Find(Under) : nullptr;
		Anchor.Roof = Roof ? *Roof : INDEX_NONE;
		OutScene.Anchors.Add(MoveTemp(Anchor));
		OutScene.AnchorActors.Add(*It);
	}
	for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
	{
		FCityFireEscapeRecord Escape = It->GetRecord();
		Escape.Transform = It->GetActorTransform();
		OutScene.FireEscapes.Add(MoveTemp(Escape));
	}
	AddStreetsAndObjectives(World, Map, OutScene);
	return true;
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
	FlushPendingPhysicsState(World);
	OutScene.SeeThrough = FindChapterActors(World);
	AddRoofs(World, Map, OutScene);
	const TMap<const AActor*, int32> RoofOfActor = RoofIndexByActor(OutScene);

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
		const AActor* Under = FindBuildingUnder(World, Anchor.Landing, LandingProbeUp, LandingProbeDepth, nullptr, OutScene.SeeThrough);
		const int32* ByTrace = Under ? RoofOfActor.Find(Under) : nullptr;
		Anchor.Roof = ByTrace ? *ByTrace : OutScene.FindRoofByOsm(Record.OsmId);
		OutScene.Anchors.Add(MoveTemp(Anchor));
		OutScene.AnchorActors.Add(bSpawnedMatch ? TWeakObjectPtr<AActor>(Spawned[Index]) : TWeakObjectPtr<AActor>());
	}

	OutScene.Ledges = Props->Ledges;
	OutScene.FireEscapes = Props->FireEscapes;
	AddStreetsAndObjectives(World, Map, OutScene);
	return true;
}

FGrappleIgnoredActors FGrappleReachWorld::FindChapterActors(UWorld* World)
{
	FGrappleIgnoredActors Out;
	for (TActorIterator<AActor> It(World); World && It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("objective:")))
			{
				Out.Add(*It);
				break;
			}
		}
	}
	return Out;
}

// --- The oracle ----------------------------------------------------------------------------------

FGrappleReachOracle FGrappleReachWorld::MakeOracle(UWorld* World, const FGrappleReachProfile& Profile, const FGrappleReachScene* Scene)
{
	using namespace HawkeyeGrappleReachWorld;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const FGrappleReachRules Rules = Profile.Rules;
	TMap<int32, TWeakObjectPtr<AActor>> AnchorActors;
	const FGrappleIgnoredActors SeeThrough = Scene ? Scene->SeeThrough : FGrappleIgnoredActors();
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
	Oracle.HasLineOfSight = [WeakWorld, Rules, AnchorActors, SeeThrough](const FVector& Lens, const FGrappleReachAnchor& Anchor,
		FGrappleReachBlocker* OutBlocker) -> bool
	{
		const UWorld* Live = WeakWorld.Get();
		if (!Live)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachSight), false);
		Params.AddIgnoredActors(SeeThrough);
		const TWeakObjectPtr<AActor>* Spawned = AnchorActors.Find(Anchor.Index);
		if (Spawned && Spawned->IsValid())
		{
			Params.AddIgnoredActor(Spawned->Get());
		}
		FHitResult Hit;
		if (HawkeyeGrappleTraces::HasLineOfSight(*Live, Lens, Anchor.Marker, Rules.SightTolerance, Params, &Hit))
		{
			return true;
		}
		if (OutBlocker)
		{
			*OutBlocker = DescribeBlocker(Hit.GetActor());
			OutBlocker->Location = Hit.ImpactPoint;
		}
		return false;
	};

	const TEnumAsByte<ECollisionChannel> ObjectType = Profile.CapsuleObjectType;
	const FCollisionResponseContainer Responses = Profile.CapsuleResponses;
	Oracle.IsZipClear = [WeakWorld, Rules, ObjectType, Responses, AnchorActors, SeeThrough](const FVector& From, const FGrappleReachAnchor& Anchor,
		bool bFromGround, FGrappleReachBlocker* OutBlocker) -> bool
	{
		const UWorld* Live = WeakWorld.Get();
		if (!Live)
		{
			return false;
		}
		// The spawned anchor itself never blocks its own zip (UGrappleComponent::IsZipClear ignores it).
		const TWeakObjectPtr<AActor>* Spawned = AnchorActors.Find(Anchor.Index);
		AActor* AnchorActor = Spawned ? Spawned->Get() : nullptr;
		const FVector Launch = FGrappleReachQuery::ComputeZipLaunch(Rules, From, bFromGround);
		const FVector End = FGrappleReachQuery::ComputeZipEnd(Rules, Anchor.Landing);

		// The anchor's supports: the building under its landing point and any static actor within SupportRadius.
		TArray<AActor*> AnchorSupports;
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachSupport), true, AnchorActor);
			Params.AddIgnoredActors(SeeThrough);
			HawkeyeGrappleTraces::FindAnchorSupports(*Live, Anchor.Landing, Anchor.Origin, SupportProbeDepth, Rules.SupportRadius,
				Params, AnchorSupports);
		}
		// The start's supports: what she stands on and the static actors within ZipStartIgnoreRadius.
		TArray<AActor*> StartSupports;
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachStartSupport), true);
			Params.AddIgnoredActors(SeeThrough);
			HawkeyeGrappleTraces::FindStartSupports(*Live, From, Rules.CapsuleHalfHeight, Rules.ZipStartIgnoreRadius, Params,
				StartSupports);
		}

		const FVector ReleasePoint = FGrappleReachQuery::ComputeStartReleasePoint(From, Launch, End, Rules.GetStartReleaseDistance());
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Rules.CapsuleRadius, Rules.CapsuleHalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachZip), false, AnchorActor);
		Params.AddIgnoredActors(SeeThrough);
		FHitResult Hit;
		if (HawkeyeGrappleTraces::IsZipClear(*Live, Launch, ReleasePoint, End, Shape, ObjectType, FCollisionResponseParams(Responses),
				Params, AnchorSupports, StartSupports, &Hit))
		{
			return true;
		}
		if (OutBlocker)
		{
			*OutBlocker = DescribeBlocker(Hit.GetActor());
			OutBlocker->Location = Hit.ImpactPoint;
		}
		return false;
	};

	if (Profile.bProbeLens)
	{
		const TEnumAsByte<ECollisionChannel> ProbeChannel = Profile.ProbeChannel;
		const float ProbeSize = Profile.ProbeSize;
		Oracle.ProbeLens = [WeakWorld, ProbeChannel, ProbeSize, SeeThrough](const FVector& Origin, const FVector& Desired) -> FVector
		{
			const UWorld* Live = WeakWorld.Get();
			if (!Live)
			{
				return Desired;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleReachProbe), false);
			Params.AddIgnoredActors(SeeThrough);
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
