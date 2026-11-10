#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/GrappleAuditKit.h"
#include "World/ChapterEndInteractable.h"
#include "World/FireEscapeLanding.h"
#include "World/CityMapData.h"
#include "World/GrappleAnchor.h"
#include "Hawkeye.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Grapple.Audit (2026-09-29, after "many street-side anchors are never targeted" and a campaign lap that
 * found no street spot from which the find_arrow roof's one anchor could be seen): every grapple anchor in the
 * district against a set of standing spots, with the game's own rules. The spots: the sidewalks every 10 m (both
 * sides of every street centre line on the world map, at the middle of the 4 m sidewalk, or nearer the centre line
 * where that is inside a building) and, on every roof, its centre and the middle of every parapet 3 m or longer
 * (1 m in). A roof no 10 m spot reaches is tried again from a sidewalk spot every 2.5 m. From each spot, for each anchor on another roof:
 * (a) in range (UGrappleComponent MinRange to Range from the capsule centre), (b) seen: the hip camera turned
 * to put the marker in the middle of the screen (HawkeyeGrappleView::PredictLens: the spring arm's sums, the pitch
 * clamp, the probe) has it inside the cone and passes HasLineOfSight (SightTolerance included), (c) zip-clear
 * (IsZipClear from the ground). An anchor that passes all three is reachable from that spot; the picker is then
 * asked (SelectBestAnchorFrom with that view) whether it would mark it.
 *
 * Per roof over 8 m (the anchored ones): anchors reachable from the street, anchors reachable from another roof,
 * and for every neighbour across a gap of 1 to 25 m whether any of its spots reaches this roof. Then the three
 * chapter 1 roofs and the find_arrow roof: reachable from the street, and each from the one before (a chain of
 * reachable or walkable roofs). Anchors reachable from nowhere, and anchors reachable but never picked, are listed.
 * Neighbours are roofs 1 to 25 m apart whose nearest points see each other over no third building as high as the
 * lower of the two (generate_city.GrappleReach.across); a pair none of whose spots is within range is counted apart.
 * Writes Saved/Automation/grapple_audit.json. Fails on an objective roof the street or the previous objective
 * cannot reach (find_arrow may be reached down a fire escape and up from the street: the street between it and
 * cross_block is wider than the grapple's range). verify_city.py holds the district-wide guarantees. Needs the
 * district loaded: run it from the standalone game (-game), like the laps.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleAudit, "Hawkeye.Grapple.Audit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeGrappleAudit
{
	static const FName BuildingTag(TEXT("CityBuilding"));
	static constexpr float AnchoredHeightM = 8.f;      // generate_city.ANCHOR_MIN_HEIGHT_M
	static constexpr float StreetStep = 1000.f;        // cm between sidewalk spots
	static constexpr float SidewalkMiddle = 200.f;     // cm out from the carriageway's edge (4 m sidewalks)
	static constexpr float SidewalkBackOff = 800.f;    // cm towards the centre line a spot may move to be off the roofs
	static constexpr float FineStreetStep = 250.f;     // cm; the second pass for a roof the 10 m spots do not reach
	static constexpr float RoofSpotEdge = 300.f;       // cm; the middle of every parapet this long is a roof spot
	static constexpr float NeighbourGap = 2500.f;      // cm; roofs this close across a street or gap are neighbours
	static constexpr float TouchingGap = 100.f;        // cm; closer than this the roofs share a wall
	static constexpr float WalkableRise = 250.f;       // cm; a touching roof within this is walked or mantled onto

	static UWorld* FindWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	static FString OsmOf(const AActor* Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			const FString S = Tag.ToString();
			if (S.StartsWith(TEXT("osm:")))
			{
				return S.Mid(4);
			}
		}
		return FString();
	}

	struct FRoof
	{
		AActor* Actor = nullptr;
		FString Osm;
		float HeightM = 0.f;
		TArray<FVector2D> Ring;
		FBox2D Box = FBox2D(ForceInit);
		TArray<int32> Anchors;
		TSet<int32> FromStreet;
		TSet<int32> FromRoofs;
		TSet<int32> ReachedFromRoof;
		int32 StreetSpotsInRange = 0;
	};

	struct FSpot
	{
		FVector Centre = FVector::ZeroVector;
		int32 Roof = INDEX_NONE;
	};

	struct FAnchorStat
	{
		AGrappleAnchor* Anchor = nullptr;
		int32 Roof = INDEX_NONE;
		int32 InRange = 0;
		int32 Seen = 0;
		int32 Clear = 0;
		int32 Reach = 0;
		int32 Picked = 0;
		int32 FromStreet = 0;
		FString Miss;
	};

	static float SegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float L = AB.SizeSquared();
		const float T = L > 0.f ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L, 0.f, 1.f) : 0.f;
		return FVector2D::Distance(P, A + AB * T);
	}

	static bool Inside(const FVector2D& P, const TArray<FVector2D>& Ring)
	{
		bool bIn = false;
		for (int32 I = 0, J = Ring.Num() - 1; I < Ring.Num(); J = I++)
		{
			if ((Ring[I].Y > P.Y) != (Ring[J].Y > P.Y)
				&& P.X < (Ring[J].X - Ring[I].X) * (P.Y - Ring[I].Y) / (Ring[J].Y - Ring[I].Y) + Ring[I].X)
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	static float RingGap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
	{
		float Best = BIG_NUMBER;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			for (int32 J = 0; J < B.Num(); ++J)
			{
				Best = FMath::Min(Best, SegmentDistance(A[I], B[J], B[(J + 1) % B.Num()]));
				Best = FMath::Min(Best, SegmentDistance(B[J], A[I], A[(I + 1) % A.Num()]));
			}
		}
		return Best;
	}

	/** The first building under Point within Depth, by a multi trace so clutter and anchors on top do not hide it. */
	static AActor* BuildingUnder(UWorld* World, const FVector& Point, const AActor* Ignore, float Up, float Depth, FVector* OutTop = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleAuditUnder), false, Ignore);
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, Point + FVector(0.f, 0.f, Up), Point - FVector(0.f, 0.f, Depth),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag))
			{
				if (OutTop)
				{
					*OutTop = Hit.ImpactPoint;
				}
				return Hit.GetActor();
			}
		}
		return nullptr;
	}

	/** Runs the audit and writes the report; a broken guarantee is an AddError. */
	static void Run(FAutomationTestBase* Test, UWorld* World)
	{
		const double Began = FPlatformTime::Seconds();
		APlayerController* PC = World->GetFirstPlayerController();
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		const UCityMapData* Map = LoadObject<UCityMapData>(nullptr, UCityMapData::DefaultPath);
		if (!Grapple || !Map)
		{
			Test->AddError(TEXT("Audit: needs Kate with a grapple and DA_EastVillage_Map in the district."));
			return;
		}
		Grapple->RebuildAnchorGrid();
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		// Roofs.
		TArray<FRoof> Roofs;
		TMap<const AActor*, int32> RoofOf;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(BuildingTag))
			{
				continue;
			}
			FRoof Roof;
			Roof.Actor = *It;
			Roof.Osm = OsmOf(*It);
			const FCityMapPolygon* Footprint = Map->FindFootprint(Roof.Osm);
			FVector Origin, Extent;
			It->GetActorBounds(false, Origin, Extent);
			if (Footprint && Footprint->Points.Num() >= 3)
			{
				Roof.Ring = Footprint->Points;
				Roof.HeightM = Footprint->HeightM;
			}
			else
			{
				Roof.Ring = { FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y), FVector2D(Origin.X + Extent.X, Origin.Y - Extent.Y),
					FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y), FVector2D(Origin.X - Extent.X, Origin.Y + Extent.Y) };
				Roof.HeightM = (Origin.Z + Extent.Z) / 100.f;
			}
			for (const FVector2D& P : Roof.Ring)
			{
				Roof.Box += P;
			}
			RoofOf.Add(*It, Roofs.Num());
			Roofs.Add(MoveTemp(Roof));
		}

		// Anchors and the roof each lands on.
		TArray<FAnchorStat> Anchors;
		TMap<FIntPoint, TArray<int32>> AnchorCells;
		const float Cell = Grapple->Range;
		int32 Homeless = 0;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			FAnchorStat Stat;
			Stat.Anchor = *It;
			const AActor* Under = BuildingUnder(World, It->GetLandingLocation(), Kate, 50.f, 150.f);
			const int32* Index = Under ? RoofOf.Find(Under) : nullptr;
			Stat.Roof = Index ? *Index : INDEX_NONE;
			Homeless += Index ? 0 : 1;
			const int32 Id = Anchors.Add(Stat);
			if (Index)
			{
				Roofs[*Index].Anchors.Add(Id);
			}
			const FVector M = It->GetMarkerLocation();
			AnchorCells.FindOrAdd(FIntPoint(FMath::FloorToInt(M.X / Cell), FMath::FloorToInt(M.Y / Cell))).Add(Id);
		}

		// Standing spots: the sidewalks, then every roof's centre and the middle of each parapet 3 m or longer.
		// A sidewalk spot is the sidewalk's middle, or nearer the centre line where the tagged width runs the
		// carriageway into the buildings (the avenues' 25 m default): the first point off every roof.
		auto SidewalkSpots = [&](float Step, TArray<FVector>& Out)
		{
			TSet<FIntPoint> Taken;
			for (const FCityMapStreet& Street : Map->Streets)
			{
				const float Middle = Street.WidthM * 50.f + SidewalkMiddle;
				float Carry = 0.f;
				for (int32 I = 0; I + 1 < Street.Points.Num(); ++I)
				{
					const FVector2D A = Street.Points[I];
					const FVector2D B = Street.Points[I + 1];
					const float Length = FVector2D::Distance(A, B);
					if (Length < 1.f)
					{
						continue;
					}
					const FVector2D Dir = (B - A) / Length;
					const FVector2D Normal(-Dir.Y, Dir.X);
					float T = Carry;
					for (; T <= Length; T += Step)
					{
						for (const float Side : { -1.f, 1.f })
						{
							for (float Back = 0.f; Back <= SidewalkBackOff; Back += 100.f)
							{
								const FVector2D P = A + Dir * T + Normal * (Middle - Back) * Side;
								FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleAuditStreet), false, Kate);
								FHitResult Hit;
								if (!World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, 3000.f), FVector(P.X, P.Y, -1000.f), ECC_Visibility, Params)
									|| !Hit.GetActor())
								{
									break;
								}
								if (Hit.GetActor()->Tags.Contains(BuildingTag) || Hit.ImpactPoint.Z > 300.f)
								{
									continue;
								}
								const FIntPoint Key(FMath::FloorToInt(P.X / (Step * 0.5f)), FMath::FloorToInt(P.Y / (Step * 0.5f)));
								if (!Taken.Contains(Key) && P.X >= Map->BoundsMin.X && P.Y >= Map->BoundsMin.Y && P.X <= Map->BoundsMax.X
									&& P.Y <= Map->BoundsMax.Y)
								{
									Taken.Add(Key);
									Out.Add(Hit.ImpactPoint + FVector(0.f, 0.f, Half + 2.f));
								}
								break;
							}
						}
					}
					Carry = T - Length;
				}
			}
		};
		TArray<FSpot> Spots;
		TArray<FVector> Sidewalk;
		SidewalkSpots(StreetStep, Sidewalk);
		for (const FVector& C : Sidewalk)
		{
			Spots.Add({ C, INDEX_NONE });
		}
		const int32 StreetSpots = Sidewalk.Num();
		int32 RoofSpots = 0;
		for (int32 R = 0; R < Roofs.Num(); ++R)
		{
			const TArray<FVector2D>& Ring = Roofs[R].Ring;
			// Area-weighted centroid and the ring's winding (for the inward normal).
			double Area = 0.0;
			FVector2D C(0.f, 0.f);
			for (int32 I = 0; I < Ring.Num(); ++I)
			{
				const FVector2D& P = Ring[I];
				const FVector2D& Q = Ring[(I + 1) % Ring.Num()];
				const double Cross = double(P.X) * Q.Y - double(Q.X) * P.Y;
				Area += Cross;
				C += (P + Q) * float(Cross);
			}
			if (FMath::Abs(Area) < 1.0)
			{
				continue;
			}
			C /= float(3.0 * Area);
			const float Winding = Area > 0.0 ? 1.f : -1.f;
			TArray<FVector2D> Points = { C };
			for (int32 E = 0; E < Ring.Num(); ++E)
			{
				const FVector2D& P = Ring[E];
				const FVector2D& Q = Ring[(E + 1) % Ring.Num()];
				if (FVector2D::Distance(P, Q) < RoofSpotEdge)
				{
					continue;
				}
				const FVector2D Dir = (Q - P).GetSafeNormal();
				// Counter-clockwise in X east / Y south: the inside is to the left of the edge.
				Points.Add((P + Q) * 0.5f + FVector2D(-Dir.Y, Dir.X) * Winding * 100.f);
			}
			for (const FVector2D& P : Points)
			{
				if (!Inside(P, Ring))
				{
					continue;
				}
				FVector Top;
				const AActor* Under = BuildingUnder(World, FVector(P.X, P.Y, Roofs[R].HeightM * 100.f), Kate, 600.f, 800.f, &Top);
				if (Under != Roofs[R].Actor)
				{
					continue;
				}
				Spots.Add({ Top + FVector(0.f, 0.f, Half + 2.f), R });
				++RoofSpots;
			}
		}

		// Every spot against every anchor in range on another roof.
		int64 Pairs = 0;
		int64 SeenPairs = 0;
		int64 ClearPairs = 0;
		int64 ReachPairs = 0;
		int64 PickedPairs = 0;
		TMap<FString, int32> MissReasons;
		int64 RoughViews = 0;
		int64 PickerErrors = 0;   // dead-on views where the picker and the full check disagree
		int64 RoughMismatches = 0;
		TMap<FString, int32> RoughReasons;
		for (const FSpot& Spot : Spots)
		{
			const FIntPoint Home(FMath::FloorToInt(Spot.Centre.X / Cell), FMath::FloorToInt(Spot.Centre.Y / Cell));
			TSet<int32> StreetRoofsInRange;
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const TArray<int32>* Bucket = AnchorCells.Find(Home + FIntPoint(DX, DY));
					if (!Bucket)
					{
						continue;
					}
					for (const int32 Id : *Bucket)
					{
						FAnchorStat& Stat = Anchors[Id];
						if (Stat.Roof == INDEX_NONE || Stat.Roof == Spot.Roof || !Stat.Anchor->bEnabled)
						{
							continue;
						}
						const FVector Marker = Stat.Anchor->GetMarkerLocation();
						const float Distance = FVector::Dist(Spot.Centre, Marker);
						if (Distance > Grapple->Range || Distance < Grapple->MinRange)
						{
							continue;
						}
						++Pairs;
						++Stat.InRange;
						if (Spot.Roof == INDEX_NONE)
						{
							StreetRoofsInRange.Add(Stat.Roof);
						}
						FVector Lens, Forward;
						HawkeyeGrappleView::PredictLens(Kate, Spot.Centre, Marker, Lens, Forward);
						const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
							FVector::DotProduct(Forward, (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
						const bool bSeen = Angle <= Grapple->ConeDegrees && Grapple->IsOnScreen(Lens, Forward, Marker)
							&& Grapple->HasLineOfSight(Stat.Anchor, Lens);
						const bool bClear = Grapple->IsZipClear(Spot.Centre, Stat.Anchor, /*bFromGround=*/true);
						Stat.Seen += bSeen ? 1 : 0;
						Stat.Clear += bClear ? 1 : 0;
						SeenPairs += bSeen ? 1 : 0;
						ClearPairs += bClear ? 1 : 0;
						if (!bSeen || !bClear)
						{
							continue;
						}
						++Stat.Reach;
						++ReachPairs;
						FRoof& Target = Roofs[Stat.Roof];
						if (Spot.Roof == INDEX_NONE)
						{
							++Stat.FromStreet;
							Target.FromStreet.Add(Id);
						}
						else
						{
							Target.FromRoofs.Add(Id);
							Target.ReachedFromRoof.Add(Spot.Roof);
						}
						// The pick by the full check: every anchor in range through every rule the picker has, in full,
						// then UGrappleComponent::ChoosePick (nearest the middle, ties within PickTieDegrees by distance).
						auto FullCheck = [&](const FVector& ViewForward) -> const AGrappleAnchor*
						{
							TArray<FVector2f> Passing;
							TArray<const AGrappleAnchor*> PassingAnchors;
							for (int32 EX = -1; EX <= 1; ++EX)
							{
								for (int32 EY = -1; EY <= 1; ++EY)
								{
									const TArray<int32>* Near = AnchorCells.Find(Home + FIntPoint(EX, EY));
									if (!Near)
									{
										continue;
									}
									for (const int32 Other : *Near)
									{
										const AGrappleAnchor* O = Anchors[Other].Anchor;
										const FVector OM = O->GetMarkerLocation();
										const float OD = FVector::Dist(Spot.Centre, OM);
										if (!O->bEnabled || OD > Grapple->Range || OD < Grapple->MinRange)
										{
											continue;
										}
										const float OA = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
											FVector::DotProduct(ViewForward, (OM - Lens).GetSafeNormal()), -1.f, 1.f)));
										if (OA > Grapple->ConeDegrees || !Grapple->IsOnScreen(Lens, ViewForward, OM)
											|| !Grapple->HasLineOfSight(O, Lens) || !Grapple->IsZipClear(Spot.Centre, O, true))
										{
											continue;
										}
										Passing.Add(FVector2f(OA, OD));
										PassingAnchors.Add(O);
									}
								}
							}
							const int32 Choice = UGrappleComponent::ChoosePick(Passing, Grapple->PickTieDegrees);
							return Choice != INDEX_NONE ? PassingAnchors[Choice] : nullptr;
						};

						// A player never aims dead on: 6.3 degrees off each way, the picker has to agree with the full check.
						for (const FRotator& Off : { FRotator(6.3f, 0.f, 0.f), FRotator(-6.3f, 0.f, 0.f), FRotator(0.f, 6.3f, 0.f),
								 FRotator(0.f, -6.3f, 0.f) })
						{
							const FRotator View = Forward.Rotation() + Off;
							const FVector RoughForward = FRotator(Kate->ClampCameraPitch(View.Pitch), View.Yaw, 0.f).Vector();
							const AGrappleAnchor* Best = FullCheck(RoughForward);
							AGrappleAnchor* RoughBlocked = nullptr;
							FString RoughReason;
							const AGrappleAnchor* RoughPick = Grapple->SelectBestAnchorFrom(Spot.Centre, true, Lens, RoughForward, RoughBlocked,
								RoughReason);
							++RoughViews;
							if (RoughPick != Best)
							{
								++RoughMismatches;
								RoughReasons.FindOrAdd(!RoughPick ? TEXT("picker found nothing")
									: (!Best ? TEXT("picker marked one the full check rejects") : TEXT("picker marked a different anchor")))++;
								if (RoughMismatches <= 5)
								{
									UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: aimed off from %s, the picker marked %s, the full check %s"),
										*Spot.Centre.ToCompactString(), *GetNameSafe(RoughPick), *GetNameSafe(Best));
								}
							}
						}

						// Dead on: it is marked, or the full check marks a nearer one as near the middle (the tie rule).
						AGrappleAnchor* Blocked = nullptr;
						FString Reason;
						const AGrappleAnchor* Picked = Grapple->SelectBestAnchorFrom(Spot.Centre, true, Lens, Forward, Blocked, Reason);
						const AGrappleAnchor* Truth = FullCheck(Forward);
						if (Picked != Truth)
						{
							++PickerErrors;
						}
						if (Picked == Stat.Anchor)
						{
							++Stat.Picked;
							++PickedPairs;
							continue;
						}
						const FString Why = Picked && Picked == Truth ? FString(TEXT("a nearer anchor as near the middle (the tie rule)"))
							: (Picked ? FString(TEXT("another anchor, not the full check's"))
								: FString(Blocked ? TEXT("nothing marked, one greyed") : TEXT("nothing marked")));
						MissReasons.FindOrAdd(Why)++;
						Stat.Miss = FString::Printf(TEXT("%s (marked %s, greyed %s: %s)"), *Why, *GetNameSafe(Picked), *GetNameSafe(Blocked), *Reason);
					}
				}
			}
			for (const int32 R : StreetRoofsInRange)
			{
				++Roofs[R].StreetSpotsInRange;
			}
		}

		// Neighbours across a street or gap (the closest points of the two footprints see each other over no
		// third building as high as the lower of the two: generate_city.GrappleReach.across), and the roofs
		// that touch (walked or mantled onto).
		auto Top = [&](int32 R) { return Roofs[R].HeightM * 100.f + (Roofs[R].HeightM >= 6.f ? 90.f : 0.f); };
		auto Closest = [](const FVector2D& Pt, const TArray<FVector2D>& Ring, FVector2D& Out)
		{
			float D = BIG_NUMBER;
			for (int32 I = 0; I < Ring.Num(); ++I)
			{
				const FVector2D& S0 = Ring[I];
				const FVector2D AB = Ring[(I + 1) % Ring.Num()] - S0;
				const float L = AB.SizeSquared();
				const float T = L > 0.f ? FMath::Clamp(FVector2D::DotProduct(Pt - S0, AB) / L, 0.f, 1.f) : 0.f;
				const FVector2D C = S0 + AB * T;
				if (FVector2D::Distance(Pt, C) < D)
				{
					D = FVector2D::Distance(Pt, C);
					Out = C;
				}
			}
			return D;
		};
		auto FacesAcross = [&](int32 A, int32 B)
		{
			float Best = BIG_NUMBER;
			FVector2D P(0.f, 0.f), Q(0.f, 0.f);
			for (const FVector2D& Pt : Roofs[B].Ring)
			{
				FVector2D C;
				const float D = Closest(Pt, Roofs[A].Ring, C);
				if (D < Best)
				{
					Best = D;
					P = C;
					Q = Pt;
				}
			}
			for (const FVector2D& Pt : Roofs[A].Ring)
			{
				FVector2D C;
				const float D = Closest(Pt, Roofs[B].Ring, C);
				if (D < Best)
				{
					Best = D;
					P = Pt;
					Q = C;
				}
			}
			const float Low = FMath::Min(Top(A), Top(B)) - 100.f;
			FBox2D Span(ForceInit);
			Span += P;
			Span += Q;
			const FVector2D Dir = Q - P;
			for (int32 K = 0; K < Roofs.Num(); ++K)
			{
				if (K == A || K == B || Top(K) < Low || !Roofs[K].Box.Intersect(Span))
				{
					continue;
				}
				const TArray<FVector2D>& Ring = Roofs[K].Ring;
				for (int32 I = 0; I < Ring.Num(); ++I)
				{
					const FVector2D E0 = Ring[I];
					const FVector2D E = Ring[(I + 1) % Ring.Num()] - E0;
					const float Den = Dir.X * E.Y - Dir.Y * E.X;
					if (FMath::Abs(Den) < 1e-6f)
					{
						continue;
					}
					const float T = ((E0.X - P.X) * E.Y - (E0.Y - P.Y) * E.X) / Den;
					const float U = ((E0.X - P.X) * Dir.Y - (E0.Y - P.Y) * Dir.X) / Den;
					if (T > 0.01f && T < 0.99f && U >= 0.f && U <= 1.f)
					{
						return false;
					}
				}
			}
			return true;
		};
		TArray<TArray<int32>> Neighbours;
		TArray<TArray<int32>> Walkable;
		Neighbours.SetNum(Roofs.Num());
		Walkable.SetNum(Roofs.Num());
		for (int32 A = 0; A < Roofs.Num(); ++A)
		{
			for (int32 B = A + 1; B < Roofs.Num(); ++B)
			{
				FBox2D Grown = Roofs[A].Box.ExpandBy(NeighbourGap);
				if (!Grown.Intersect(Roofs[B].Box))
				{
					continue;
				}
				const float Gap = RingGap(Roofs[A].Ring, Roofs[B].Ring);
				if (Gap <= TouchingGap)
				{
					if (FMath::Abs(Roofs[A].HeightM - Roofs[B].HeightM) * 100.f <= WalkableRise)
					{
						Walkable[A].Add(B);
						Walkable[B].Add(A);
					}
				}
				else if (Gap <= NeighbourGap && FacesAcross(A, B))
				{
					Neighbours[A].Add(B);
					Neighbours[B].Add(A);
				}
			}
		}

		// A roof no 10 m sidewalk spot reaches gets a second look from a spot every 2.5 m: a player stands
		// anywhere on the sidewalk, the 10 m spots are the report's sample.
		auto Anchored = [&](int32 R) { return Roofs[R].HeightM > AnchoredHeightM; };
		TArray<FVector> FineSidewalk;
		TMap<int32, int32> FineFromStreet;
		TMap<int32, TMap<FString, int32>> FineWhy;   // what stopped each 2.5 m spot, for a roof none reaches
		TMap<int32, TArray<FString>> FineExamples;
		auto Kind = [](const AActor* Actor)
		{
			if (!Actor)
			{
				return FString(TEXT("nothing"));
			}
			if (Actor->Tags.Contains(BuildingTag))
			{
				return FString(TEXT("a building"));
			}
			FString Name = Actor->GetClass()->GetName();
			Name.RemoveFromEnd(TEXT("_C"));
			return Name;
		};
		for (int32 R = 0; R < Roofs.Num(); ++R)
		{
			if (!Anchored(R) || Roofs[R].FromStreet.Num() > 0 || Roofs[R].Anchors.Num() == 0)
			{
				continue;
			}
			if (FineSidewalk.Num() == 0)
			{
				SidewalkSpots(FineStreetStep, FineSidewalk);
			}
			for (const int32 Id : Roofs[R].Anchors)
			{
				const AGrappleAnchor* Anchor = Anchors[Id].Anchor;
				const FVector Marker = Anchor->GetMarkerLocation();
				bool bFound = false;
				for (const FVector& C : FineSidewalk)
				{
					const float Distance = FVector::Dist(C, Marker);
					if (Distance > Grapple->Range || Distance < Grapple->MinRange)
					{
						continue;
					}
					FVector Lens, Forward;
					HawkeyeGrappleView::PredictLens(Kate, C, Marker, Lens, Forward);
					const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector::DotProduct(Forward, (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
					if (Angle > Grapple->ConeDegrees || !Grapple->IsOnScreen(Lens, Forward, Marker))
					{
						FineWhy.FindOrAdd(R).FindOrAdd(TEXT("out of the cone at the pitch limit"))++;
						continue;
					}
					if (!Grapple->HasLineOfSight(Anchor, Lens))
					{
						FCollisionQueryParams SightParams(SCENE_QUERY_STAT(GrappleAuditWhy), false, Kate);
						SightParams.AddIgnoredActor(Anchor);
						FHitResult Hit;
						World->LineTraceSingleByChannel(Hit, Lens, Marker, ECC_Visibility, SightParams);
						FineWhy.FindOrAdd(R).FindOrAdd(TEXT("view blocked by ") + (Hit.GetActor() == Roofs[R].Actor
							? FString(TEXT("its own building")) : Kind(Hit.GetActor())))++;
						TArray<FString>& Seen = FineExamples.FindOrAdd(R);
						if (Seen.Num() < 3)
						{
							Seen.Add(FString::Printf(TEXT("%s from %s: lens %s, hit %s at %s"), *Anchor->GetName(), *C.ToCompactString(),
								*Lens.ToCompactString(), *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString()));
						}
						continue;
					}
					AActor* Blocker = nullptr;
					if (!Grapple->IsZipClear(C, Anchor, true, &Blocker))
					{
						FineWhy.FindOrAdd(R).FindOrAdd(TEXT("zip blocked by ") + Kind(Blocker))++;
						continue;
					}
					bFound = true;
					break;
				}
				if (bFound)
				{
					FineFromStreet.FindOrAdd(R)++;
				}
			}
		}

		// Tallies.
		int32 AnchoredRoofs = 0;
		int32 RoofsWithAnchors = 0;
		TArray<FString> NoStreet;
		TArray<FString> NoStreetInRange;
		TArray<FString> NoStreetAtAll;
		TArray<FString> NoRoof;
		int32 NeighbourPairs = 0;
		int32 NeighbourPairsMissing = 0;
		int32 NeighbourPairsOutOfRange = 0;
		TArray<TArray<FVector>> RoofSpotsOf;
		RoofSpotsOf.SetNum(Roofs.Num());
		for (const FSpot& Spot : Spots)
		{
			if (Spot.Roof != INDEX_NONE)
			{
				RoofSpotsOf[Spot.Roof].Add(Spot.Centre);
			}
		}
		// Some spot on From within the grapple's range (less 100 cm) of the top of To's walls.
		auto PairInRange = [&](int32 From, int32 To)
		{
			const float Z = Top(To) - 20.f;
			for (const FVector& C : RoofSpotsOf[From])
			{
				const FVector2D C2(C.X, C.Y);
				FVector2D Unused;
				const float Flat = Inside(C2, Roofs[To].Ring) ? 0.f : Closest(C2, Roofs[To].Ring, Unused);
				if (FMath::Sqrt(Flat * Flat + FMath::Square(Z - C.Z)) <= Grapple->Range - 100.f)
				{
					return true;
				}
			}
			return false;
		};
		TArray<FString> MissingPairs;
		for (int32 R = 0; R < Roofs.Num(); ++R)
		{
			if (!Anchored(R))
			{
				continue;
			}
			++AnchoredRoofs;
			RoofsWithAnchors += Roofs[R].Anchors.Num() > 0 ? 1 : 0;
			const FString Label = FString::Printf(TEXT("%s (%s, %.1f m, %d anchors)"), *Roofs[R].Osm, *GetNameSafe(Roofs[R].Actor),
				Roofs[R].HeightM, Roofs[R].Anchors.Num());
			if (Roofs[R].FromStreet.Num() == 0)
			{
				NoStreet.Add(Label);
				if (Roofs[R].StreetSpotsInRange > 0 || Roofs[R].Anchors.Num() == 0)
				{
					NoStreetInRange.Add(Label);
				}
				if (!FineFromStreet.Contains(R))
				{
					TArray<FString> Why;
					if (const TMap<FString, int32>* Reasons = FineWhy.Find(R))
					{
						for (const TPair<FString, int32>& Reason : *Reasons)
						{
							Why.Add(FString::Printf(TEXT("%s x%d"), *Reason.Key, Reason.Value));
						}
					}
					if (const TArray<FString>* Examples = FineExamples.Find(R))
					{
						Why.Append(*Examples);
					}
					NoStreetAtAll.Add(Label + TEXT(": ") + (Why.Num() ? FString::Join(Why, TEXT(", ")) : FString(TEXT("no sidewalk spot in range"))));
				}
			}
			if (Roofs[R].FromRoofs.Num() == 0)
			{
				NoRoof.Add(Label);
			}
			for (const int32 N : Neighbours[R])
			{
				if (Roofs[R].ReachedFromRoof.Contains(N))
				{
					++NeighbourPairs;
				}
				else if (PairInRange(N, R))
				{
					++NeighbourPairs;
					++NeighbourPairsMissing;
					if (MissingPairs.Num() < 400)
					{
						MissingPairs.Add(FString::Printf(TEXT("%s from %s"), *Roofs[R].Osm, *Roofs[N].Osm));
					}
				}
				else
				{
					// Every spot on the neighbour is farther than the grapple's range from this roof's walls.
					++NeighbourPairsOutOfRange;
				}
			}
		}
		TArray<FString> NeverReach;
		TArray<FString> NeverPicked;
		for (const FAnchorStat& Stat : Anchors)
		{
			if (Stat.Roof == INDEX_NONE)
			{
				continue;
			}
			if (Stat.Reach == 0)
			{
				NeverReach.Add(FString::Printf(TEXT("%s on %s (in range %d, seen %d, clear %d)"), *Stat.Anchor->GetName(),
					*Roofs[Stat.Roof].Osm, Stat.InRange, Stat.Seen, Stat.Clear));
			}
			else if (Stat.Picked == 0)
			{
				NeverPicked.Add(FString::Printf(TEXT("%s on %s (reachable from %d spots): %s"), *Stat.Anchor->GetName(),
					*Roofs[Stat.Roof].Osm, Stat.Reach, *Stat.Miss));
			}
		}

		// The chapter 1 roofs: the objective volumes and the chapter-end arrow carry the roof's osm id.
		TMap<FString, FString> ObjectiveOsm;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			for (const FName& Tag : It->Tags)
			{
				const FString S = Tag.ToString();
				if (S.StartsWith(TEXT("objective:")) && !OsmOf(*It).IsEmpty()
					&& (S.EndsWith(TEXT("reach_roof")) || S.EndsWith(TEXT("cross_block")) || S.EndsWith(TEXT("find_arrow"))))
				{
					ObjectiveOsm.Add(S.Mid(10), OsmOf(*It));
				}
			}
		}
		auto RoofByOsm = [&](const FString& Osm)
		{
			for (int32 R = 0; R < Roofs.Num(); ++R)
			{
				if (Roofs[R].Osm == Osm)
				{
					return R;
				}
			}
			return int32(INDEX_NONE);
		};
		// Roofs reachable from From by grapple hops and walks along touching roofs.
		// The roofs with a fire escape: a way down to the street.
		TSet<int32> EscapeRoofs;
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			const FVector2D L(It->GetActorLocation().X, It->GetActorLocation().Y);
			int32 Best = INDEX_NONE;
			float BestGap = 250.f;
			for (int32 R = 0; R < Roofs.Num(); ++R)
			{
				if (!Roofs[R].Box.ExpandBy(250.f).IsInside(L))
				{
					continue;
				}
				FVector2D Unused;
				const float D = Closest(L, Roofs[R].Ring, Unused);
				if (D < BestGap)
				{
					BestGap = D;
					Best = R;
				}
			}
			if (Best != INDEX_NONE)
			{
				EscapeRoofs.Add(Best);
			}
		}
		TSet<int32> LastReached;
		auto Chain = [&](int32 From, int32 To, int32& OutHops)
		{
			LastReached.Reset();
			TMap<int32, int32> Depth;
			TArray<int32> Queue = { From };
			Depth.Add(From, 0);
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 R = Queue[Head];
				LastReached.Add(R);
				if (R == To)
				{
					OutHops = Depth[R];
					return true;
				}
				TArray<int32> Next = Walkable[R];
				for (int32 B = 0; B < Roofs.Num(); ++B)
				{
					if (Roofs[B].ReachedFromRoof.Contains(R))
					{
						Next.Add(B);
					}
				}
				for (const int32 B : Next)
				{
					if (!Depth.Contains(B))
					{
						Depth.Add(B, Depth[R] + 1);
						Queue.Add(B);
					}
				}
			}
			OutHops = -1;
			return false;
		};
		TArray<FString> ObjectiveLines;
		const TCHAR* Order[] = { TEXT("reach_roof"), TEXT("cross_block"), TEXT("find_arrow") };
		int32 Previous = INDEX_NONE;
		for (const TCHAR* Id : Order)
		{
			const FString* Osm = ObjectiveOsm.Find(Id);
			const int32 R = Osm ? RoofByOsm(*Osm) : INDEX_NONE;
			if (R == INDEX_NONE)
			{
				Test->AddError(FString::Printf(TEXT("Audit: no roof found for %s."), Id));
				continue;
			}
			int32 Hops = -1;
			const bool bByRoofs = Previous == INDEX_NONE || Chain(Previous, R, Hops);
			// find_arrow is across a street wider than the grapple's range from cross_block: down a fire escape on a
			// roof the chain reaches, then up from the sidewalk (cross_block itself is crossed on the roofs).
			bool bEscapeReached = false;
			for (const int32 K : LastReached)
			{
				bEscapeReached |= EscapeRoofs.Contains(K);
			}
			const bool bByStreet = !bByRoofs && FCString::Strcmp(Id, TEXT("find_arrow")) == 0 && Roofs[R].FromStreet.Num() > 0
				&& bEscapeReached;
			const bool bChained = bByRoofs || bByStreet;
			const FString How = bByRoofs ? FString::Printf(TEXT("%d hop(s) on the roofs"), Hops)
				: (bByStreet ? FString(TEXT("down a fire escape and up from the street")) : FString(TEXT("no chain")));
			const FString Line = FString::Printf(TEXT("%s on %s (%s): %d anchors, %d reachable from the street, %d from other roofs%s"), Id,
				*Roofs[R].Osm, *GetNameSafe(Roofs[R].Actor), Roofs[R].Anchors.Num(), Roofs[R].FromStreet.Num(), Roofs[R].FromRoofs.Num(),
				Previous == INDEX_NONE ? TEXT("") : *FString::Printf(TEXT(", from %s: %s"), *Roofs[Previous].Osm, *How));
			ObjectiveLines.Add(Line);
			Test->AddInfo(TEXT("Audit: ") + Line);
			if (Roofs[R].FromStreet.Num() == 0)
			{
				Test->AddError(FString::Printf(TEXT("Audit: the %s roof %s has no anchor reachable from the street."), Id, *Roofs[R].Osm));
			}
			if (!bChained)
			{
				Test->AddError(FString::Printf(TEXT("Audit: the %s roof %s cannot be reached from the %s roof %s by grapples and walks."), Id,
					*Roofs[R].Osm, *Roofs[Previous].Osm, *Roofs[Previous].Osm));
			}
			Previous = R;
		}

		const double Seconds = FPlatformTime::Seconds() - Began;
		const FString Summary = FString::Printf(
			TEXT("%d anchors (%d on no roof), %d roofs over %.0f m (%d with anchors); %d spots (%d street, %d roof); %lld spot-anchor pairs in range, ")
			TEXT("%lld seen, %lld zip-clear, %lld reachable, %lld of those picked (%.1f%%); %d roofs with no anchor reachable from the street ")
			TEXT("(%d with a street spot in range or no anchors; %d reached from none of the 2.5 m spots either), %d with none from another roof; ")
			TEXT("neighbour pairs %d, %d not reachable ")
			TEXT("(%d more out of range); ")
			TEXT("%d anchors reachable from nowhere, %d reachable but never picked; %.1f s"),
			Anchors.Num(), Homeless, AnchoredRoofs, AnchoredHeightM, RoofsWithAnchors, Spots.Num(), StreetSpots, RoofSpots, Pairs, SeenPairs,
			ClearPairs, ReachPairs, PickedPairs, ReachPairs > 0 ? 100.0 * double(PickedPairs) / double(ReachPairs) : 0.0, NoStreet.Num(),
			NoStreetInRange.Num(), NoStreetAtAll.Num(), NoRoof.Num(), NeighbourPairs, NeighbourPairsMissing, NeighbourPairsOutOfRange, NeverReach.Num(),
			NeverPicked.Num(), Seconds);
		Test->AddInfo(TEXT("Audit: ") + Summary);
		UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: %s"), *Summary);
		for (const TPair<FString, int32>& Miss : MissReasons)
		{
			UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: picker missed %d reachable pair(s): %s"), Miss.Value, *Miss.Key);
		}
		const FString Rough = FString::Printf(TEXT("dead on, the picker disagreed with the full check on %lld of %lld views; aimed 6.3 ")
			TEXT("degrees off, on %lld of %lld (%.2f%%)"), PickerErrors, ReachPairs, RoughMismatches, RoughViews,
			RoughViews > 0 ? 100.0 * double(RoughMismatches) / double(RoughViews) : 0.0);
		Test->AddInfo(TEXT("Audit: ") + Rough);
		UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: %s"), *Rough);
		for (const TPair<FString, int32>& Miss : RoughReasons)
		{
			UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: aimed off, %d view(s): %s"), Miss.Value, *Miss.Key);
			MissReasons.Add(TEXT("aimed off: ") + Miss.Key, Miss.Value);
		}

		// The report.
		auto JsonList = [](const TArray<FString>& Items)
		{
			TArray<FString> Quoted;
			for (const FString& Item : Items)
			{
				Quoted.Add(TEXT("\"") + Item.ReplaceCharWithEscapedChar() + TEXT("\""));
			}
			return TEXT("[") + FString::Join(Quoted, TEXT(",\n    ")) + TEXT("]");
		};
		TArray<FString> RoofRows;
		for (int32 R = 0; R < Roofs.Num(); ++R)
		{
			if (Anchored(R))
			{
				RoofRows.Add(FString::Printf(TEXT("{\"osm\": \"%s\", \"actor\": \"%s\", \"height_m\": %.1f, \"anchors\": %d, \"from_street\": %d, ")
					TEXT("\"from_roofs\": %d, \"street_spots_in_range\": %d, \"neighbours\": %d, \"neighbours_reaching\": %d}"),
					*Roofs[R].Osm, *GetNameSafe(Roofs[R].Actor), Roofs[R].HeightM, Roofs[R].Anchors.Num(), Roofs[R].FromStreet.Num(),
					Roofs[R].FromRoofs.Num(), Roofs[R].StreetSpotsInRange, Neighbours[R].Num(),
					Neighbours[R].FilterByPredicate([&](int32 N) { return Roofs[R].ReachedFromRoof.Contains(N); }).Num()));
			}
		}
		TArray<FString> MissRows;
		for (const TPair<FString, int32>& Miss : MissReasons)
		{
			MissRows.Add(FString::Printf(TEXT("%s x%d"), *Miss.Key, Miss.Value));
		}
		const FString Json = FString::Printf(TEXT("{\n  \"summary\": \"%s\",\n  \"objectives\": %s,\n  \"picker_misses\": %s,\n")
			TEXT("  \"roofs_no_street\": %s,\n  \"roofs_no_street_with_spot_in_range\": %s,\n  \"roofs_no_street_at_2_5_m\": %s,\n")
			TEXT("  \"roofs_no_roof\": %s,\n")
			TEXT("  \"neighbour_pairs_missing\": %s,\n  \"anchors_never_reachable\": %s,\n  \"anchors_never_picked\": %s,\n  \"roofs\": [\n    %s\n  ]\n}\n"),
			*Summary.ReplaceCharWithEscapedChar(), *JsonList(ObjectiveLines), *JsonList(MissRows), *JsonList(NoStreet),
			*JsonList(NoStreetInRange), *JsonList(NoStreetAtAll), *JsonList(NoRoof), *JsonList(MissingPairs), *JsonList(NeverReach), *JsonList(NeverPicked),
			*FString::Join(RoofRows, TEXT(",\n    ")));
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("grapple_audit.json"));
		FFileHelper::SaveStringToFile(Json, *Path);
		Test->AddInfo(TEXT("Audit: wrote ") + Path);

		for (const FString& Label : NoStreetAtAll)
		{
			Test->AddInfo(TEXT("Audit: no anchor reachable from the street on ") + Label);
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeGrappleAuditRun, FAutomationTestBase*, Test);

bool FHawkeyeGrappleAuditRun::Update()
{
	UWorld* World = HawkeyeGrappleAudit::FindWorld();
	if (!World)
	{
		Test->AddError(TEXT("Audit: no game world."));
		return true;
	}
	HawkeyeGrappleAudit::Run(Test, World);
	return true;
}

bool FHawkeyeGrappleAudit::RunTest(const FString& Parameters)
{
	if (GIsEditor)
	{
		AddInfo(TEXT("Needs the district as a game world: skipped in the editor. Run it from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(8.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeGrappleAuditRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
