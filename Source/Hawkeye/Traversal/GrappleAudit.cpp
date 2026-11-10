#include "Traversal/GrappleAudit.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace HawkeyeGrappleAuditRun
{
	/** Per roof, what the spots found. */
	struct FRoofTally
	{
		TArray<int32> Anchors;
		TSet<int32> FromStreet;
		TSet<int32> FromRoofs;
		/** The roofs some spot of which reaches an anchor here. */
		TSet<int32> ReachedFromRoof;
		int32 StreetSpotsInRange = 0;
		int32 FineSpotsInRange = 0;
		int32 FromFineStreet = 0;
		TMap<FString, int32> FineWhy;
		TArray<FString> FineExamples;
	};

	/** Per anchor, over every spot. */
	struct FAnchorTally
	{
		int32 InRange = 0;
		int32 Seen = 0;
		int32 Clear = 0;
		int32 Reach = 0;
	};

	/** One audit run, split into the test's passes. */
	class FRun
	{
	public:
		explicit FRun(const FGrappleAuditInputs& InInputs)
			: Inputs(InInputs)
			, Scene(*InInputs.Scene)
			, Rules(InInputs.Profile.Rules)
			, Settings(InInputs.Settings)
		{
		}

		FGrappleAuditReport Execute()
		{
			const double Began = FPlatformTime::Seconds();
			IndexAnchors();
			GatherSpots();
			CheckPairs();
			FGrappleReachQuery::FindNeighbours(Scene.Roofs, Settings, Neighbours, Walkable);
			FinePass();
			Tally();
			CheckChain();
			Report.Seconds = FPlatformTime::Seconds() - Began;
			Summarise();
			return MoveTemp(Report);
		}

	private:
		FString Label(int32 R) const
		{
			return FString::Printf(TEXT("%s (%s, %.1f m, %d anchors)"), *Scene.Roofs[R].OsmId, *Scene.Roofs[R].ActorName,
				Scene.Roofs[R].HeightM, Roofs[R].Anchors.Num());
		}

		bool IsAnchored(int32 R) const { return Settings.IsAnchored(Scene.Roofs[R].HeightM); }

		FIntPoint CellOf(const FVector& Location) const
		{
			return FIntPoint(FMath::FloorToInt32(Location.X / Rules.Range), FMath::FloorToInt32(Location.Y / Rules.Range));
		}

		void IndexAnchors()
		{
			Roofs.SetNum(Scene.Roofs.Num());
			Anchors.SetNum(Scene.Anchors.Num());
			for (int32 Id = 0; Id < Scene.Anchors.Num(); ++Id)
			{
				const int32 R = Scene.Anchors[Id].Roof;
				Homeless += Roofs.IsValidIndex(R) ? 0 : 1;
				if (Roofs.IsValidIndex(R))
				{
					Roofs[R].Anchors.Add(Id);
				}
				AnchorCells.FindOrAdd(CellOf(Scene.Anchors[Id].Marker)).Add(Id);
			}
		}

		void GatherSidewalk(float Step, TArray<FVector>& Out) const
		{
			if (!Inputs.SidewalkFloor)
			{
				return;
			}
			FGrappleReachQuery::GatherSidewalkSpots(Scene.Streets, Scene.Bounds, Step, Rules, Settings,
				[this](const FVector2D& Point, FVector& OutFloor) { return Inputs.SidewalkFloor(Point, OutFloor); }, Out);
		}

		void GatherSpots()
		{
			TArray<FVector> Sidewalk;
			GatherSidewalk(Settings.StreetStep, Sidewalk);
			for (const FVector& Centre : Sidewalk)
			{
				Spots.Add({ Centre, INDEX_NONE });
			}
			Report.StreetSpotCount = Sidewalk.Num();
			for (int32 R = 0; R < Scene.Roofs.Num() && Inputs.RoofTop; ++R)
			{
				TArray<FVector2D> Points;
				FGrappleReachQuery::ComputeRoofSpotPoints(Scene.Roofs[R].Ring, Settings, Points);
				for (const FVector2D& Point : Points)
				{
					FVector Top;
					if (!Inputs.RoofTop(Point, R, Top))
					{
						continue;
					}
					Spots.Add({ FGrappleReachQuery::ComputeStandingCentre(Rules, Top), R });
					++Report.RoofSpotCount;
				}
			}
		}

		void CheckPairs()
		{
			for (const FGrappleReachSpot& Spot : Spots)
			{
				TSet<int32> StreetRoofsInRange;
				const FIntPoint Home = CellOf(Spot.Centre);
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						if (const TArray<int32>* Bucket = AnchorCells.Find(Home + FIntPoint(DX, DY)))
						{
							for (const int32 Id : *Bucket)
							{
								CheckPair(Spot, Id, StreetRoofsInRange);
							}
						}
					}
				}
				for (const int32 R : StreetRoofsInRange)
				{
					++Roofs[R].StreetSpotsInRange;
				}
			}
		}

		void CheckPair(const FGrappleReachSpot& Spot, int32 Id, TSet<int32>& StreetRoofsInRange)
		{
			const FGrappleReachAnchor& Anchor = Scene.Anchors[Id];
			if (Anchor.Roof == INDEX_NONE || Anchor.Roof == Spot.Roof || !Anchor.bEnabled)
			{
				return;
			}
			if (!FGrappleReachQuery::IsInRange(Rules, static_cast<float>(FVector::Dist(Spot.Centre, Anchor.Marker))))
			{
				return;
			}
			++Pairs;
			++Anchors[Id].InRange;
			if (Spot.Roof == INDEX_NONE)
			{
				StreetRoofsInRange.Add(Anchor.Roof);
			}
			const FGrappleReachEvaluation Result = FGrappleReachQuery::Evaluate(Rules, Inputs.Profile.Lens, Inputs.Oracle, Spot, Scene.Anchors, Id,
				/*bCheckZipWhenUnseen=*/true);
			Anchors[Id].Seen += Result.bSeen ? 1 : 0;
			Anchors[Id].Clear += Result.bClear ? 1 : 0;
			SeenPairs += Result.bSeen ? 1 : 0;
			ClearPairs += Result.bClear ? 1 : 0;
			if (!Result.bSeen || !Result.bClear)
			{
				return;
			}
			++Anchors[Id].Reach;
			++ReachPairs;
			FRoofTally& Target = Roofs[Anchor.Roof];
			if (Spot.Roof == INDEX_NONE)
			{
				Target.FromStreet.Add(Id);
			}
			else
			{
				Target.FromRoofs.Add(Id);
				Target.ReachedFromRoof.Add(Spot.Roof);
			}
		}

		/** A roof no 10 m sidewalk spot reaches gets a second look from a spot every 2.5 m, with the reasons. */
		void FinePass()
		{
			TArray<FVector> Fine;
			for (int32 R = 0; R < Scene.Roofs.Num(); ++R)
			{
				if (!IsAnchored(R) || Roofs[R].FromStreet.Num() > 0 || Roofs[R].Anchors.Num() == 0)
				{
					continue;
				}
				if (Fine.Num() == 0)
				{
					GatherSidewalk(Settings.FineStreetStep, Fine);
				}
				TSet<int32> SpotsInRange;
				for (const int32 Id : Roofs[R].Anchors)
				{
					if (FineReaches(R, Id, Fine, SpotsInRange))
					{
						++Roofs[R].FromFineStreet;
					}
				}
				Roofs[R].FineSpotsInRange = SpotsInRange.Num();
			}
		}

		bool FineReaches(int32 R, int32 Id, const TArray<FVector>& Fine, TSet<int32>& SpotsInRange)
		{
			FRoofTally& Roof = Roofs[R];
			for (int32 SpotIndex = 0; SpotIndex < Fine.Num(); ++SpotIndex)
			{
				const FGrappleReachSpot Spot{ Fine[SpotIndex], INDEX_NONE };
				if (!FGrappleReachQuery::IsInRange(Rules, static_cast<float>(FVector::Dist(Spot.Centre, Scene.Anchors[Id].Marker))))
				{
					continue;
				}
				SpotsInRange.Add(SpotIndex);
				const FGrappleReachEvaluation Result = FGrappleReachQuery::Evaluate(Rules, Inputs.Profile.Lens, Inputs.Oracle, Spot, Scene.Anchors, Id);
				switch (Result.Verdict)
				{
				case EGrappleReachVerdict::Reachable:
					return true;
				case EGrappleReachVerdict::TooSteep:
					Roof.FineWhy.FindOrAdd(TEXT("out of the cone at the pitch limit"))++;
					break;
				case EGrappleReachVerdict::SightBlocked:
				{
					const bool bOwn = Result.Blocker.Kind == TEXT("a building") && Result.Blocker.OsmId == Scene.Roofs[R].OsmId;
					Roof.FineWhy.FindOrAdd(TEXT("view blocked by ") + (bOwn ? FString(TEXT("its own building")) : Result.Blocker.Kind))++;
					if (Roof.FineExamples.Num() < 3)
					{
						Roof.FineExamples.Add(FString::Printf(TEXT("%s from %s: lens %s, hit %s"), *Scene.Anchors[Id].Name,
							*Spot.Centre.ToCompactString(), *Result.Lens.ToCompactString(), *Result.Blocker.Name));
					}
					break;
				}
				case EGrappleReachVerdict::ZipBlocked:
					Roof.FineWhy.FindOrAdd(TEXT("zip blocked by ") + Result.Blocker.Kind)++;
					break;
				default:
					break;
				}
			}
			return false;
		}

		FString FineWhyText(int32 R) const
		{
			TArray<FString> Why;
			for (const TPair<FString, int32>& Reason : Roofs[R].FineWhy)
			{
				Why.Add(FString::Printf(TEXT("%s x%d"), *Reason.Key, Reason.Value));
			}
			Why.Append(Roofs[R].FineExamples);
			return Why.Num() ? FString::Join(Why, TEXT(", ")) : FString(TEXT("no sidewalk spot in range"));
		}

		void Tally()
		{
			TArray<TArray<FVector>> RoofSpotsOf;
			RoofSpotsOf.SetNum(Scene.Roofs.Num());
			for (const FGrappleReachSpot& Spot : Spots)
			{
				if (Spot.Roof != INDEX_NONE)
				{
					RoofSpotsOf[Spot.Roof].Add(Spot.Centre);
				}
			}
			for (int32 R = 0; R < Scene.Roofs.Num(); ++R)
			{
				if (IsAnchored(R))
				{
					TallyRoof(R, RoofSpotsOf);
				}
			}
			for (int32 Id = 0; Id < Scene.Anchors.Num(); ++Id)
			{
				const int32 R = Scene.Anchors[Id].Roof;
				if (R != INDEX_NONE && Anchors[Id].Reach == 0)
				{
					Report.AnchorsNeverReachable.Add(FString::Printf(TEXT("%s on %s (in range %d, seen %d, clear %d)"), *Scene.Anchors[Id].Name,
						*Scene.Roofs[R].OsmId, Anchors[Id].InRange, Anchors[Id].Seen, Anchors[Id].Clear));
				}
			}
		}

		void TallyRoof(int32 R, const TArray<TArray<FVector>>& RoofSpotsOf)
		{
			++AnchoredRoofs;
			const FRoofTally& Roof = Roofs[R];
			RoofsWithAnchors += Roof.Anchors.Num() > 0 ? 1 : 0;
			FGrappleAuditRoofRow Row;
			Row.Roof = R;
			Row.OsmId = Scene.Roofs[R].OsmId;
			Row.ActorName = Scene.Roofs[R].ActorName;
			Row.HeightM = Scene.Roofs[R].HeightM;
			Row.Anchors = Roof.Anchors.Num();
			Row.FromStreet = Roof.FromStreet.Num();
			Row.FromRoofs = Roof.FromRoofs.Num();
			Row.StreetSpotsInRange = Roof.StreetSpotsInRange;
			Row.FineSpotsInRange = Roof.FineSpotsInRange;
			Row.FromFineStreet = Roof.FromFineStreet;
			Row.Neighbours = Neighbours[R].Num();
			for (const int32 N : Neighbours[R])
			{
				Row.NeighboursReaching += Roof.ReachedFromRoof.Contains(N) ? 1 : 0;
			}

			const FString RoofLabel = Label(R);
			if (Roof.FromStreet.Num() == 0)
			{
				Report.RoofsNoStreet.Add(RoofLabel);
				if (Roof.StreetSpotsInRange > 0 || Roof.Anchors.Num() == 0)
				{
					Report.RoofsNoStreetWithSpotInRange.Add(RoofLabel);
				}
				if (Roof.FromFineStreet == 0)
				{
					Row.Why = FineWhyText(R);
					Report.RoofsNoStreetAtFineStep.Add(RoofLabel + TEXT(": ") + Row.Why);
				}
			}
			if (Roof.FromRoofs.Num() == 0)
			{
				Report.RoofsNoRoof.Add(RoofLabel);
			}
			Row.Reach = ClassifyRow(Row);
			if (Row.Reach == EGrappleRoofReach::Unreached)
			{
				Report.StreetFacingUnreached.Add(Report.Roofs.Num());
			}
			TallyNeighbours(R, RoofSpotsOf);
			Report.Roofs.Add(MoveTemp(Row));
		}

		static EGrappleRoofReach ClassifyRow(const FGrappleAuditRoofRow& Row)
		{
			if (Row.Anchors == 0)
			{
				return EGrappleRoofReach::NoAnchors;
			}
			if (Row.FromStreet > 0 || Row.FromFineStreet > 0)
			{
				return EGrappleRoofReach::Reached;
			}
			return Row.StreetSpotsInRange > 0 || Row.FineSpotsInRange > 0 ? EGrappleRoofReach::Unreached : EGrappleRoofReach::NoStreetInRange;
		}

		void TallyNeighbours(int32 R, const TArray<TArray<FVector>>& RoofSpotsOf)
		{
			for (const int32 N : Neighbours[R])
			{
				if (Roofs[R].ReachedFromRoof.Contains(N))
				{
					++NeighbourPairs;
				}
				else if (FGrappleReachQuery::IsPairInRange(Rules, Settings, RoofSpotsOf[N], Scene.Roofs[R]))
				{
					++NeighbourPairs;
					++NeighbourPairsMissing;
					if (Report.NeighbourPairsMissing.Num() < 400)
					{
						Report.NeighbourPairsMissing.Add(FString::Printf(TEXT("%s from %s"), *Scene.Roofs[R].OsmId, *Scene.Roofs[N].OsmId));
					}
				}
				else
				{
					// Every spot on the neighbour is farther than the grapple's range from this roof's walls.
					++NeighbourPairsOutOfRange;
				}
			}
		}

		/** The roofs with a fire escape: a way down to the street. */
		TSet<int32> FindEscapeRoofs() const
		{
			TSet<int32> Out;
			for (const FCityFireEscapeRecord& Escape : Scene.FireEscapes)
			{
				const FVector2D Location(Escape.Transform.GetLocation().X, Escape.Transform.GetLocation().Y);
				int32 Best = INDEX_NONE;
				double BestGap = Settings.EscapeRoofGap;
				for (int32 R = 0; R < Scene.Roofs.Num(); ++R)
				{
					if (!Scene.Roofs[R].Box.ExpandBy(Settings.EscapeRoofGap).IsInside(Location))
					{
						continue;
					}
					FVector2D Unused;
					const double Gap = FGrappleReachQuery::ClosestOnRing(Location, Scene.Roofs[R].Ring, Unused);
					if (Gap < BestGap)
					{
						BestGap = Gap;
						Best = R;
					}
				}
				if (Best != INDEX_NONE)
				{
					Out.Add(Best);
				}
			}
			return Out;
		}

		void CheckChain()
		{
			if (Inputs.ObjectiveChain.Num() == 0)
			{
				return;
			}
			TArray<TSet<int32>> ReachedFrom;
			for (const FRoofTally& Roof : Roofs)
			{
				ReachedFrom.Add(Roof.ReachedFromRoof);
			}
			const TSet<int32> EscapeRoofs = FindEscapeRoofs();
			int32 Previous = INDEX_NONE;
			for (const FString& Id : Inputs.ObjectiveChain)
			{
				const FString* Osm = Scene.ObjectiveRoofs.Find(Id);
				const int32 R = Osm ? Scene.FindRoofByOsm(*Osm) : INDEX_NONE;
				if (R == INDEX_NONE)
				{
					Report.Errors.Add(FString::Printf(TEXT("no roof found for %s"), *Id));
					Report.bObjectiveChainBroken = true;
					continue;
				}
				CheckChainLink(Id, Previous, R, ReachedFrom, EscapeRoofs);
				Previous = R;
			}
		}

		void CheckChainLink(const FString& Id, int32 Previous, int32 R, const TArray<TSet<int32>>& ReachedFrom, const TSet<int32>& EscapeRoofs)
		{
			TSet<int32> Visited;
			const int32 Hops = Previous == INDEX_NONE ? 0 : FGrappleReachQuery::FindChainHops(Previous, R, Walkable, ReachedFrom, &Visited);
			const bool bByRoofs = Previous == INDEX_NONE || Hops >= 0;
			bool bEscapeReached = false;
			for (const int32 K : Visited)
			{
				bEscapeReached |= EscapeRoofs.Contains(K);
			}
			// Down a fire escape on a roof the chain reaches, then up from the sidewalk (find_arrow's way, generalised).
			const bool bByStreet = !bByRoofs && Roofs[R].FromStreet.Num() > 0 && bEscapeReached;
			const FString How = bByRoofs ? FString::Printf(TEXT("%d hop(s) on the roofs"), Hops)
				: (bByStreet ? FString(TEXT("down a fire escape and up from the street")) : FString(TEXT("no chain")));
			const FString From = Previous == INDEX_NONE ? FString()
				: FString::Printf(TEXT(", from %s: %s"), *Scene.Roofs[Previous].OsmId, *How);
			Report.Objectives.Add(FString::Printf(TEXT("%s on %s (%s): %d anchors, %d reachable from the street, %d from other roofs%s"), *Id,
				*Scene.Roofs[R].OsmId, *Scene.Roofs[R].ActorName, Roofs[R].Anchors.Num(), Roofs[R].FromStreet.Num(), Roofs[R].FromRoofs.Num(), *From));
			if (Roofs[R].FromStreet.Num() == 0)
			{
				Report.Errors.Add(FString::Printf(TEXT("the %s roof %s has no anchor reachable from the street"), *Id, *Scene.Roofs[R].OsmId));
				Report.bObjectiveChainBroken = true;
			}
			if (!bByRoofs && !bByStreet)
			{
				Report.Errors.Add(FString::Printf(TEXT("the %s roof %s cannot be reached from %s by grapples and walks"), *Id, *Scene.Roofs[R].OsmId,
					*Scene.Roofs[Previous].OsmId));
				Report.bObjectiveChainBroken = true;
			}
		}

		void Summarise()
		{
			Report.AnchorCount = Scene.Anchors.Num();
			Report.Summary = FString::Printf(
				TEXT("%d anchors (%d on no roof), %d roofs over %.0f m (%d with anchors); %d spots (%d street, %d roof); %lld spot-anchor pairs in range, ")
				TEXT("%lld seen, %lld zip-clear, %lld reachable; %d roofs with no anchor reachable from the street ")
				TEXT("(%d with a street spot in range or no anchors; %d reached from none of the 2.5 m spots either), %d with none from another roof; ")
				TEXT("neighbour pairs %d, %d not reachable (%d more out of range); %d anchors reachable from nowhere; ")
				TEXT("%d street-facing roofs unreached; picker not checked (offline audit, profile %s); %.1f s"),
				Scene.Anchors.Num(), Homeless, AnchoredRoofs, Settings.AnchoredHeightM, RoofsWithAnchors, Spots.Num(), Report.StreetSpotCount,
				Report.RoofSpotCount, Pairs, SeenPairs, ClearPairs, ReachPairs, Report.RoofsNoStreet.Num(), Report.RoofsNoStreetWithSpotInRange.Num(),
				Report.RoofsNoStreetAtFineStep.Num(), Report.RoofsNoRoof.Num(), NeighbourPairs, NeighbourPairsMissing, NeighbourPairsOutOfRange,
				Report.AnchorsNeverReachable.Num(), Report.StreetFacingUnreached.Num(), *Inputs.Profile.Source, Report.Seconds);
		}

		const FGrappleAuditInputs& Inputs;
		const FGrappleReachScene& Scene;
		const FGrappleReachRules Rules;
		const FGrappleAuditSettings Settings;
		FGrappleAuditReport Report;

		TArray<FRoofTally> Roofs;
		TArray<FAnchorTally> Anchors;
		TMap<FIntPoint, TArray<int32>> AnchorCells;
		TArray<FGrappleReachSpot> Spots;
		TArray<TArray<int32>> Neighbours;
		TArray<TArray<int32>> Walkable;

		int32 Homeless = 0;
		int64 Pairs = 0;
		int64 SeenPairs = 0;
		int64 ClearPairs = 0;
		int64 ReachPairs = 0;
		int32 AnchoredRoofs = 0;
		int32 RoofsWithAnchors = 0;
		int32 NeighbourPairs = 0;
		int32 NeighbourPairsMissing = 0;
		int32 NeighbourPairsOutOfRange = 0;
	};

	static FString JsonList(const TArray<FString>& Items)
	{
		TArray<FString> Quoted;
		for (const FString& Item : Items)
		{
			Quoted.Add(TEXT("\"") + Item.ReplaceCharWithEscapedChar() + TEXT("\""));
		}
		return TEXT("[") + FString::Join(Quoted, TEXT(",\n    ")) + TEXT("]");
	}
}

// --- FGrappleAuditReport -------------------------------------------------------------------------

FString FGrappleAuditReport::ToJson() const
{
	using namespace HawkeyeGrappleAuditRun;
	TArray<FString> RoofRows;
	for (const FGrappleAuditRoofRow& Row : Roofs)
	{
		RoofRows.Add(FString::Printf(TEXT("{\"osm\": \"%s\", \"actor\": \"%s\", \"height_m\": %.1f, \"anchors\": %d, \"from_street\": %d, ")
			TEXT("\"from_roofs\": %d, \"street_spots_in_range\": %d, \"neighbours\": %d, \"neighbours_reaching\": %d}"),
			*Row.OsmId, *Row.ActorName, Row.HeightM, Row.Anchors, Row.FromStreet, Row.FromRoofs, Row.StreetSpotsInRange, Row.Neighbours,
			Row.NeighboursReaching));
	}
	TArray<FString> Gate;
	for (const int32 Index : StreetFacingUnreached)
	{
		const FGrappleAuditRoofRow& Row = Roofs[Index];
		Gate.Add(FString::Printf(TEXT("%s (%s, %.1f m, %d anchors): %s"), *Row.OsmId, *Row.ActorName, Row.HeightM, Row.Anchors, *Row.Why));
	}
	return FString::Printf(TEXT("{\n  \"summary\": \"%s\",\n  \"objectives\": %s,\n  \"picker_misses\": %s,\n")
		TEXT("  \"roofs_no_street\": %s,\n  \"roofs_no_street_with_spot_in_range\": %s,\n  \"roofs_no_street_at_2_5_m\": %s,\n")
		TEXT("  \"roofs_no_roof\": %s,\n")
		TEXT("  \"neighbour_pairs_missing\": %s,\n  \"anchors_never_reachable\": %s,\n  \"anchors_never_picked\": %s,\n")
		TEXT("  \"street_facing_unreached\": %s,\n  \"roofs\": [\n    %s\n  ]\n}\n"),
		*Summary.ReplaceCharWithEscapedChar(), *JsonList(Objectives), *JsonList(PickerMisses), *JsonList(RoofsNoStreet),
		*JsonList(RoofsNoStreetWithSpotInRange), *JsonList(RoofsNoStreetAtFineStep), *JsonList(RoofsNoRoof), *JsonList(NeighbourPairsMissing),
		*JsonList(AnchorsNeverReachable), *JsonList(AnchorsNeverPicked), *JsonList(Gate), *FString::Join(RoofRows, TEXT(",\n    ")));
}

bool FGrappleAuditReport::WriteJson(const FString& Path) const
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	return FFileHelper::SaveStringToFile(ToJson(), *Path);
}

TArray<FString> FGrappleAuditReport::FindGateFailures(const TSet<FString>& Allowed) const
{
	TArray<FString> Failures;
	for (const int32 Index : StreetFacingUnreached)
	{
		const FGrappleAuditRoofRow& Row = Roofs[Index];
		if (!Allowed.Contains(Row.OsmId))
		{
			Failures.Add(FString::Printf(TEXT("%s (%s, %.1f m, %d anchors): %s"), *Row.OsmId, *Row.ActorName, Row.HeightM, Row.Anchors, *Row.Why));
		}
	}
	return Failures;
}

FString FGrappleAuditReport::GetDefaultPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("grapple_audit.json"));
}

// --- FGrappleAudit -------------------------------------------------------------------------------

FGrappleAuditReport FGrappleAudit::Run(const FGrappleAuditInputs& Inputs)
{
	if (!Inputs.Scene || !Inputs.Oracle.HasLineOfSight || !Inputs.Oracle.IsZipClear)
	{
		FGrappleAuditReport Empty;
		Empty.Errors.Add(TEXT("the audit needs a scene and an oracle with sight and zip checks"));
		Empty.Summary = Empty.Errors[0];
		return Empty;
	}
	HawkeyeGrappleAuditRun::FRun Worker(Inputs);
	return Worker.Execute();
}

FGrappleAuditInputs FGrappleAudit::MakeWorldInputs(UWorld* World, const FGrappleReachScene& Scene, const FGrappleReachProfile& Profile)
{
	FGrappleAuditInputs Inputs;
	Inputs.Scene = &Scene;
	Inputs.Profile = Profile;
	Inputs.Oracle = FGrappleReachWorld::MakeOracle(World, Profile, &Scene);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const float MaxFloorZ = Inputs.Settings.StreetMaxFloorZ;
	Inputs.SidewalkFloor = [WeakWorld, MaxFloorZ](const FVector2D& Point, FVector& OutFloor)
	{
		return FGrappleReachWorld::FindSidewalkFloor(WeakWorld.Get(), Point, MaxFloorZ, OutFloor);
	};
	const FGrappleReachScene* ScenePtr = &Scene;
	Inputs.RoofTop = [WeakWorld, ScenePtr](const FVector2D& Point, int32 Roof, FVector& OutTop)
	{
		if (!ScenePtr->Roofs.IsValidIndex(Roof) || !FGrappleReachQuery::IsInsideRing(Point, ScenePtr->Roofs[Roof].Ring))
		{
			return false;
		}
		// The audit's probe: from 6 m over the roof's height down 8 m, the first building hit must be this one.
		const FVector Over(Point.X, Point.Y, ScenePtr->Roofs[Roof].HeightM * 100.f);
		const AActor* Under = FGrappleReachWorld::FindBuildingUnder(WeakWorld.Get(), Over, 600.f, 800.f, &OutTop);
		return Under && ScenePtr->RoofActors.IsValidIndex(Roof) && Under == ScenePtr->RoofActors[Roof].Get();
	};
	return Inputs;
}
