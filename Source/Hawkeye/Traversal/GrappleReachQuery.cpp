#include "Traversal/GrappleReachQuery.h"

void FGrappleReachRoof::UpdateBox()
{
	Box = FBox2D(ForceInit);
	for (const FVector2D& Point : Ring)
	{
		Box += Point;
	}
}

const TCHAR* LexToString(EGrappleReachVerdict Verdict)
{
	switch (Verdict)
	{
	case EGrappleReachVerdict::Reachable:
		return TEXT("reachable");
	case EGrappleReachVerdict::Disabled:
		return TEXT("disabled");
	case EGrappleReachVerdict::SameRoof:
		return TEXT("on her own roof");
	case EGrappleReachVerdict::TooClose:
		return TEXT("too close");
	case EGrappleReachVerdict::TooFar:
		return TEXT("too far");
	case EGrappleReachVerdict::TooSteep:
		return TEXT("too steep (out of the cone at the pitch limit)");
	case EGrappleReachVerdict::SightBlocked:
		return TEXT("view blocked");
	case EGrappleReachVerdict::ZipBlocked:
		return TEXT("zip line blocked");
	}
	return TEXT("unknown");
}

const TCHAR* LexToString(EGrappleRoofReach Reach)
{
	switch (Reach)
	{
	case EGrappleRoofReach::Reached:
		return TEXT("reached from the street");
	case EGrappleRoofReach::BelowAnchorHeight:
		return TEXT("below the anchored height");
	case EGrappleRoofReach::NoAnchors:
		return TEXT("no anchors");
	case EGrappleRoofReach::NoStreetInRange:
		return TEXT("no sidewalk spot in range");
	case EGrappleRoofReach::NoStreetWall:
		return TEXT("no wall on a street (inside its block or behind buildings)");
	case EGrappleRoofReach::Unreached:
		return TEXT("street-facing, no anchor reachable");
	}
	return TEXT("unknown");
}

// --- The view ------------------------------------------------------------------------------------

float FGrappleReachQuery::AngleOffViewDegrees(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point)
{
	// Float arithmetic on purpose: this is the expression UGrappleComponent::IsAnchorValid had before it moved here,
	// kept bit for bit so the game's picks did not change.
	const FVector ToPoint = (Point - ViewLocation).GetSafeNormal();
	const float Cosine = FMath::Clamp(FVector::DotProduct(ViewForward.GetSafeNormal(), ToPoint), -1.f, 1.f);
	return FMath::RadiansToDegrees(FMath::Acos(Cosine));
}

bool FGrappleReachQuery::IsOnScreen(const FGrappleReachRules& Rules, const FVector& ViewLocation, const FVector& ViewForward,
	const FVector& Point)
{
	// The view's own axes with no roll, as the camera has: the marker's angles off the middle, across and up.
	const FRotationMatrix View(ViewForward.GetSafeNormal().Rotation());
	const FVector Local = View.InverseTransformVector(Point - ViewLocation);
	if (Local.X <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float HalfAcross = FMath::DegreesToRadians(FMath::Clamp(Rules.ViewFieldOfView, 10.f, 170.f) * 0.5f);
	const float HalfUp = FMath::Atan(FMath::Tan(HalfAcross) / FMath::Max(Rules.ViewAspectRatio, 0.1f));
	const float Margin = FMath::DegreesToRadians(Rules.ScreenEdgeMarginDegrees);
	return FMath::Abs(FMath::Atan2(Local.Y, Local.X)) <= HalfAcross - Margin
		&& FMath::Abs(FMath::Atan2(Local.Z, Local.X)) <= HalfUp - Margin;
}

bool FGrappleReachQuery::IsInRange(const FGrappleReachRules& Rules, float Distance)
{
	return Distance <= Rules.Range && Distance >= Rules.MinRange;
}

int32 FGrappleReachQuery::ChoosePick(const TArray<FVector2f>& AnglesAndDistances, float TieDegrees)
{
	// Nearest the middle of the screen; of those within TieDegrees of that, the nearest to her.
	int32 Middle = INDEX_NONE;
	for (int32 Index = 0; Index < AnglesAndDistances.Num(); ++Index)
	{
		if (Middle == INDEX_NONE || AnglesAndDistances[Index].X < AnglesAndDistances[Middle].X)
		{
			Middle = Index;
		}
	}
	int32 Best = Middle;
	for (int32 Index = 0; Index < AnglesAndDistances.Num() && Middle != INDEX_NONE; ++Index)
	{
		if (AnglesAndDistances[Index].X <= AnglesAndDistances[Middle].X + TieDegrees
			&& AnglesAndDistances[Index].Y < AnglesAndDistances[Best].Y)
		{
			Best = Index;
		}
	}
	return Best;
}

void FGrappleReachQuery::PredictLens(const FGrappleLensModel& Model, const FVector& Centre, const FVector& Target,
	const TFunction<FVector(const FVector&, const FVector&)>& Probe, FVector& OutLens, FVector& OutForward)
{
	const FVector Base = Centre + Model.PivotOffset;
	FRotator Rot = (Target - Base).Rotation();
	FVector Lens = Base;
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		Rot = FRotator(Model.ClampPitch(static_cast<float>(Rot.Pitch)), Rot.Yaw, 0.f);
		float ArmLength = 0.f;
		FVector SocketOffset = FVector::ZeroVector;
		float PivotLift = 0.f;
		if (Model.Targets)
		{
			Model.Targets(static_cast<float>(Rot.Pitch), ArmLength, SocketOffset, PivotLift);
		}
		const FVector Origin = Base + FVector(0.f, 0.f, PivotLift);
		const FVector Desired = Origin - Rot.Vector() * ArmLength + FRotationMatrix(Rot).TransformVector(SocketOffset);
		Lens = Probe ? Probe(Origin, Desired) : Desired;
		Rot = (Target - Lens).Rotation();
	}
	Rot = FRotator(Model.ClampPitch(static_cast<float>(Rot.Pitch)), Rot.Yaw, 0.f);
	OutLens = Lens;
	OutForward = Rot.Vector();
}

// --- The zip -------------------------------------------------------------------------------------

FVector FGrappleReachQuery::ComputeZipLaunch(const FGrappleReachRules& Rules, const FVector& Centre, bool bFromGround)
{
	return bFromGround ? Centre + FVector(0.f, 0.f, Rules.ZipLaunchHeight) : Centre;
}

FVector FGrappleReachQuery::ComputeZipEnd(const FGrappleReachRules& Rules, const FVector& Landing)
{
	return Landing + FVector(0.f, 0.f, Rules.CapsuleHalfHeight + GrappleReachDefaults::StandClearance);
}

FVector FGrappleReachQuery::ComputeStandingCentre(const FGrappleReachRules& Rules, const FVector& Floor)
{
	return Floor + FVector(0.f, 0.f, Rules.CapsuleHalfHeight + GrappleReachDefaults::StandClearance);
}

FVector FGrappleReachQuery::ComputeStartReleasePoint(const FVector& From, const FVector& Launch, const FVector& End,
	float ReleaseDistance)
{
	// The far root of |Launch + Dir * S - From| = ReleaseDistance. Float arithmetic on purpose: the expression
	// UGrappleComponent::IsZipClear had before it moved here, kept bit for bit so zips clear exactly as they did.
	const float Release = ReleaseDistance;
	const FVector Line = End - Launch;
	const float Length = Line.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return End;
	}
	const FVector Dir = Line / Length;
	const FVector Offset = Launch - From;
	const float B = FVector::DotProduct(Offset, Dir);
	const float C = Offset.SizeSquared() - Release * Release;
	const float Disc = B * B - C;
	const float S = Disc >= 0.f ? -B + FMath::Sqrt(Disc) : 0.f;
	return Launch + Dir * FMath::Clamp(S, 0.f, Length);
}

// --- Anchors from a standing point ---------------------------------------------------------------

FGrappleReachEvaluation FGrappleReachQuery::Evaluate(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens,
	const FGrappleReachOracle& Oracle, const FGrappleReachSpot& Spot, const TArray<FGrappleReachAnchor>& Anchors,
	int32 AnchorIndex, bool bCheckZipWhenUnseen)
{
	FGrappleReachEvaluation Result;
	Result.Anchor = AnchorIndex;
	if (!Anchors.IsValidIndex(AnchorIndex))
	{
		return Result;
	}
	const FGrappleReachAnchor& Anchor = Anchors[AnchorIndex];
	Result.Distance = static_cast<float>(FVector::Dist(Spot.Centre, Anchor.Marker));
	if (!Anchor.bEnabled)
	{
		Result.Verdict = EGrappleReachVerdict::Disabled;
		return Result;
	}
	if (Spot.Roof != INDEX_NONE && Anchor.Roof == Spot.Roof)
	{
		Result.Verdict = EGrappleReachVerdict::SameRoof;
		return Result;
	}
	if (Result.Distance > Rules.Range)
	{
		Result.Verdict = EGrappleReachVerdict::TooFar;
		return Result;
	}
	if (Result.Distance < Rules.MinRange)
	{
		Result.Verdict = EGrappleReachVerdict::TooClose;
		return Result;
	}

	PredictLens(Lens, Spot.Centre, Anchor.Marker, Oracle.ProbeLens, Result.Lens, Result.Forward);
	Result.AngleDegrees = AngleOffViewDegrees(Result.Lens, Result.Forward, Anchor.Marker);
	const bool bInView = Result.AngleDegrees <= Rules.ConeDegrees && IsOnScreen(Rules, Result.Lens, Result.Forward, Anchor.Marker);
	FGrappleReachBlocker SightBlocker;
	Result.bSeen = bInView && Oracle.HasLineOfSight && Oracle.HasLineOfSight(Result.Lens, Anchor, &SightBlocker);

	FGrappleReachBlocker ZipBlocker;
	if (Result.bSeen || bCheckZipWhenUnseen)
	{
		Result.bClear = Oracle.IsZipClear && Oracle.IsZipClear(Spot.Centre, Anchor, /*bFromGround=*/true, &ZipBlocker);
	}

	if (!bInView)
	{
		Result.Verdict = EGrappleReachVerdict::TooSteep;
	}
	else if (!Result.bSeen)
	{
		Result.Verdict = EGrappleReachVerdict::SightBlocked;
		Result.Blocker = SightBlocker;
	}
	else if (!Result.bClear)
	{
		Result.Verdict = EGrappleReachVerdict::ZipBlocked;
		Result.Blocker = ZipBlocker;
	}
	else
	{
		Result.Verdict = EGrappleReachVerdict::Reachable;
	}
	return Result;
}

void FGrappleReachQuery::EvaluateAll(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens, const FGrappleReachOracle& Oracle,
	const FGrappleReachSpot& Spot, const TArray<FGrappleReachAnchor>& Anchors, TArray<FGrappleReachEvaluation>& OutEvaluations)
{
	OutEvaluations.Reset(Anchors.Num());
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		OutEvaluations.Add(Evaluate(Rules, Lens, Oracle, Spot, Anchors, Index));
	}
}

int32 FGrappleReachQuery::PickFrom(const FGrappleReachRules& Rules, const TArray<FGrappleReachAnchor>& Anchors,
	const TArray<FGrappleReachEvaluation>& Evaluations, const FVector& ViewLocation, const FVector& ViewForward)
{
	TArray<FVector2f> Passing;
	TArray<int32> PassingAnchors;
	for (const FGrappleReachEvaluation& Evaluation : Evaluations)
	{
		if (Evaluation.Verdict != EGrappleReachVerdict::Reachable || !Anchors.IsValidIndex(Evaluation.Anchor))
		{
			continue;
		}
		const FVector& Marker = Anchors[Evaluation.Anchor].Marker;
		const float Angle = AngleOffViewDegrees(ViewLocation, ViewForward, Marker);
		if (Angle > Rules.ConeDegrees || !IsOnScreen(Rules, ViewLocation, ViewForward, Marker))
		{
			continue;
		}
		Passing.Add(FVector2f(Angle, Evaluation.Distance));
		PassingAnchors.Add(Evaluation.Anchor);
	}
	const int32 Choice = ChoosePick(Passing, Rules.PickTieDegrees);
	return Choice != INDEX_NONE ? PassingAnchors[Choice] : INDEX_NONE;
}

// --- Footprints ----------------------------------------------------------------------------------

bool FGrappleReachQuery::IsInsideRing(const FVector2D& Point, const TArray<FVector2D>& Ring)
{
	bool bInside = false;
	for (int32 I = 0, J = Ring.Num() - 1; I < Ring.Num(); J = I++)
	{
		if ((Ring[I].Y > Point.Y) != (Ring[J].Y > Point.Y)
			&& Point.X < (Ring[J].X - Ring[I].X) * (Point.Y - Ring[I].Y) / (Ring[J].Y - Ring[I].Y) + Ring[I].X)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

double FGrappleReachQuery::SegmentDistance(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const double LengthSquared = AB.SizeSquared();
	const double T = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
	return FVector2D::Distance(Point, A + AB * T);
}

double FGrappleReachQuery::ClosestOnRing(const FVector2D& Point, const TArray<FVector2D>& Ring, FVector2D& OutClosest)
{
	double Best = BIG_NUMBER;
	OutClosest = Point;
	for (int32 I = 0; I < Ring.Num(); ++I)
	{
		const FVector2D& S0 = Ring[I];
		const FVector2D AB = Ring[(I + 1) % Ring.Num()] - S0;
		const double LengthSquared = AB.SizeSquared();
		const double T = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Point - S0, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		const FVector2D C = S0 + AB * T;
		const double D = FVector2D::Distance(Point, C);
		if (D < Best)
		{
			Best = D;
			OutClosest = C;
		}
	}
	return Best;
}

double FGrappleReachQuery::RingGap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
{
	double Best = BIG_NUMBER;
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

bool FGrappleReachQuery::RingCentroid(const TArray<FVector2D>& Ring, FVector2D& OutCentroid, float& OutWinding)
{
	double Area = 0.0;
	FVector2D C(0.0, 0.0);
	for (int32 I = 0; I < Ring.Num(); ++I)
	{
		const FVector2D& P = Ring[I];
		const FVector2D& Q = Ring[(I + 1) % Ring.Num()];
		const double Cross = P.X * Q.Y - Q.X * P.Y;
		Area += Cross;
		C += (P + Q) * Cross;
	}
	if (FMath::Abs(Area) < 1.0)
	{
		return false;
	}
	OutCentroid = C / (3.0 * Area);
	OutWinding = Area > 0.0 ? 1.f : -1.f;
	return true;
}

void FGrappleReachQuery::ComputeRoofSpotPoints(const TArray<FVector2D>& Ring, const FGrappleAuditSettings& Settings,
	TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();
	FVector2D Centroid;
	float Winding = 1.f;
	if (!RingCentroid(Ring, Centroid, Winding))
	{
		return;
	}
	TArray<FVector2D> Points = { Centroid };
	for (int32 E = 0; E < Ring.Num(); ++E)
	{
		const FVector2D& P = Ring[E];
		const FVector2D& Q = Ring[(E + 1) % Ring.Num()];
		if (FVector2D::Distance(P, Q) < Settings.RoofSpotEdge)
		{
			continue;
		}
		const FVector2D Dir = (Q - P).GetSafeNormal();
		// Counter-clockwise in X east / Y south: the inside is to the left of the edge.
		Points.Add((P + Q) * 0.5 + FVector2D(-Dir.Y, Dir.X) * (Winding * Settings.RoofSpotInset));
	}
	for (const FVector2D& Point : Points)
	{
		if (IsInsideRing(Point, Ring))
		{
			OutPoints.Add(Point);
		}
	}
}

// --- Neighbours and chains -----------------------------------------------------------------------

bool FGrappleReachQuery::FacesAcross(const TArray<FGrappleReachRoof>& Roofs, int32 A, int32 B, const FGrappleAuditSettings& Settings)
{
	double Best = BIG_NUMBER;
	FVector2D P(0.0, 0.0);
	FVector2D Q(0.0, 0.0);
	for (const FVector2D& Point : Roofs[B].Ring)
	{
		FVector2D C;
		const double D = ClosestOnRing(Point, Roofs[A].Ring, C);
		if (D < Best)
		{
			Best = D;
			P = C;
			Q = Point;
		}
	}
	for (const FVector2D& Point : Roofs[A].Ring)
	{
		FVector2D C;
		const double D = ClosestOnRing(Point, Roofs[B].Ring, C);
		if (D < Best)
		{
			Best = D;
			P = Point;
			Q = C;
		}
	}
	const float Low = FMath::Min(Settings.RoofTopZ(Roofs[A].HeightM), Settings.RoofTopZ(Roofs[B].HeightM)) - 100.f;
	FBox2D Span(ForceInit);
	Span += P;
	Span += Q;
	const FVector2D Dir = Q - P;
	for (int32 K = 0; K < Roofs.Num(); ++K)
	{
		if (K == A || K == B || Settings.RoofTopZ(Roofs[K].HeightM) < Low || !Roofs[K].Box.Intersect(Span))
		{
			continue;
		}
		const TArray<FVector2D>& Ring = Roofs[K].Ring;
		for (int32 I = 0; I < Ring.Num(); ++I)
		{
			const FVector2D E0 = Ring[I];
			const FVector2D E = Ring[(I + 1) % Ring.Num()] - E0;
			const double Den = Dir.X * E.Y - Dir.Y * E.X;
			if (FMath::Abs(Den) < 1e-6)
			{
				continue;
			}
			const double T = ((E0.X - P.X) * E.Y - (E0.Y - P.Y) * E.X) / Den;
			const double U = ((E0.X - P.X) * Dir.Y - (E0.Y - P.Y) * Dir.X) / Den;
			if (T > 0.01 && T < 0.99 && U >= 0.0 && U <= 1.0)
			{
				return false;
			}
		}
	}
	return true;
}

void FGrappleReachQuery::FindNeighbours(const TArray<FGrappleReachRoof>& Roofs, const FGrappleAuditSettings& Settings,
	TArray<TArray<int32>>& OutNeighbours, TArray<TArray<int32>>& OutWalkable)
{
	OutNeighbours.Reset();
	OutWalkable.Reset();
	OutNeighbours.SetNum(Roofs.Num());
	OutWalkable.SetNum(Roofs.Num());
	for (int32 A = 0; A < Roofs.Num(); ++A)
	{
		const FBox2D Grown = Roofs[A].Box.ExpandBy(Settings.NeighbourGap);
		for (int32 B = A + 1; B < Roofs.Num(); ++B)
		{
			if (!Grown.Intersect(Roofs[B].Box))
			{
				continue;
			}
			const double Gap = RingGap(Roofs[A].Ring, Roofs[B].Ring);
			if (Gap <= Settings.TouchingGap)
			{
				if (FMath::Abs(Roofs[A].HeightM - Roofs[B].HeightM) * 100.f <= Settings.WalkableRise)
				{
					OutWalkable[A].Add(B);
					OutWalkable[B].Add(A);
				}
			}
			else if (Gap <= Settings.NeighbourGap && FacesAcross(Roofs, A, B, Settings))
			{
				OutNeighbours[A].Add(B);
				OutNeighbours[B].Add(A);
			}
		}
	}
}

bool FGrappleReachQuery::IsPairInRange(const FGrappleReachRules& Rules, const FGrappleAuditSettings& Settings,
	const TArray<FVector>& FromSpots, const FGrappleReachRoof& To)
{
	const double Z = Settings.RoofTopZ(To.HeightM) - 20.0;
	for (const FVector& Spot : FromSpots)
	{
		const FVector2D Flat2D(Spot.X, Spot.Y);
		FVector2D Unused;
		const double Flat = IsInsideRing(Flat2D, To.Ring) ? 0.0 : ClosestOnRing(Flat2D, To.Ring, Unused);
		if (FMath::Sqrt(Flat * Flat + FMath::Square(Z - Spot.Z)) <= double(Rules.Range) - 100.0)
		{
			return true;
		}
	}
	return false;
}

int32 FGrappleReachQuery::FindChainHops(int32 From, int32 To, const TArray<TArray<int32>>& Walkable,
	const TArray<TSet<int32>>& ReachedFrom, TSet<int32>* OutVisited)
{
	if (OutVisited)
	{
		OutVisited->Reset();
	}
	if (!Walkable.IsValidIndex(From) || !Walkable.IsValidIndex(To))
	{
		return -1;
	}
	TMap<int32, int32> Depth;
	TArray<int32> Queue = { From };
	Depth.Add(From, 0);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 R = Queue[Head];
		if (OutVisited)
		{
			OutVisited->Add(R);
		}
		if (R == To)
		{
			return Depth[R];
		}
		TArray<int32> Next = Walkable[R];
		for (int32 B = 0; B < ReachedFrom.Num(); ++B)
		{
			if (ReachedFrom[B].Contains(R))
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
	return -1;
}

// --- The street ----------------------------------------------------------------------------------

void FGrappleReachQuery::GatherSidewalkSpots(const TArray<FGrappleReachStreet>& Streets, const FBox2D& Bounds, float Step,
	const FGrappleReachRules& Rules, const FGrappleAuditSettings& Settings,
	TFunctionRef<EStand(const FVector2D& Point, FVector& OutFloor)> FloorAt, TArray<FVector>& OutCentres)
{
	if (Step <= 1.f)
	{
		return;
	}
	TSet<FIntPoint> Taken;
	const double Cell = Step * 0.5;
	for (const FGrappleReachStreet& Street : Streets)
	{
		const double Middle = Street.WidthM * 50.0 + Settings.SidewalkMiddle;
		double Carry = 0.0;
		for (int32 I = 0; I + 1 < Street.Points.Num(); ++I)
		{
			const FVector2D A = Street.Points[I];
			const FVector2D B = Street.Points[I + 1];
			const double Length = FVector2D::Distance(A, B);
			if (Length < 1.0)
			{
				continue;
			}
			const FVector2D Dir = (B - A) / Length;
			const FVector2D Normal(-Dir.Y, Dir.X);
			double T = Carry;
			for (; T <= Length; T += Step)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					for (double Back = 0.0; Back <= Settings.SidewalkBackOff; Back += Settings.SidewalkBackOffStep)
					{
						const FVector2D P = A + Dir * T + Normal * ((Middle - Back) * Side);
						FVector Floor;
						const EStand Stand = FloorAt(P, Floor);
						if (Stand == EStand::NoGround)
						{
							break;
						}
						if (Stand == EStand::Blocked)
						{
							continue;
						}
						const FIntPoint Key(FMath::FloorToInt32(P.X / Cell), FMath::FloorToInt32(P.Y / Cell));
						if (!Taken.Contains(Key) && Bounds.IsInside(P))
						{
							Taken.Add(Key);
							OutCentres.Add(ComputeStandingCentre(Rules, Floor));
						}
						break;
					}
				}
			}
			Carry = T - Length;
		}
	}
}

int32 FGrappleReachQuery::CountStreetWalls(const TArray<FGrappleReachRoof>& Roofs, int32 R, const TArray<FGrappleReachStreet>& Streets,
	const FGrappleAuditSettings& Settings)
{
	if (!Roofs.IsValidIndex(R))
	{
		return 0;
	}
	const FGrappleReachRoof& Roof = Roofs[R];
	FVector2D Centroid;
	float Winding = 1.f;
	if (!RingCentroid(Roof.Ring, Centroid, Winding))
	{
		return 0;
	}
	const float Top = Settings.RoofTopZ(Roof.HeightM);
	auto IsCovered = [&](const FVector2D& Point)
	{
		for (const FGrappleReachRoof& Other : Roofs)
		{
			if (Other.Box.IsInside(Point) && Settings.RoofTopZ(Other.HeightM) >= Top - Settings.StreetWallCoverDrop
				&& IsInsideRing(Point, Other.Ring))
			{
				return true;
			}
		}
		return false;
	};
	int32 Walls = 0;
	for (int32 E = 0; E < Roof.Ring.Num(); ++E)
	{
		const FVector2D& A = Roof.Ring[E];
		const FVector2D& B = Roof.Ring[(E + 1) % Roof.Ring.Num()];
		const double Length = FVector2D::Distance(A, B);
		if (Length < Settings.StreetWallMinEdge)
		{
			continue;
		}
		const FVector2D Along = (B - A) / Length;
		// Counter-clockwise in X east / Y south the inside is to the left: out is to the right.
		const FVector2D Out = FVector2D(Along.Y, -Along.X) * Winding;
		int32 Covered = 0;
		for (const double T : { 0.25, 0.5, 0.75 })
		{
			Covered += IsCovered(A + Along * (Length * T) + Out * Settings.StreetWallProbeOut) ? 1 : 0;
		}
		if (Covered >= 2)
		{
			continue;
		}
		const FVector2D Middle = (A + B) * 0.5;
		bool bFacesStreet = false;
		for (const FGrappleReachStreet& Street : Streets)
		{
			for (int32 I = 0; I + 1 < Street.Points.Num() && !bFacesStreet; ++I)
			{
				const FVector2D& P = Street.Points[I];
				const FVector2D PQ = Street.Points[I + 1] - P;
				const double LengthSquared = PQ.SizeSquared();
				const double T = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Middle - P, PQ) / LengthSquared, 0.0, 1.0) : 0.0;
				const FVector2D ToRoad = P + PQ * T - Middle;
				const double Distance = ToRoad.Size();
				bFacesStreet = Distance >= 1.0 && Distance <= Street.WidthM * 50.0 + Settings.StreetWallRoadReach
					&& FVector2D::DotProduct(ToRoad, Out) / Distance >= Settings.StreetWallFacing;
			}
			if (bFacesStreet)
			{
				break;
			}
		}
		Walls += bFacesStreet ? 1 : 0;
	}
	return Walls;
}

// --- Roofs the street cannot reach ---------------------------------------------------------------

void FGrappleReachQuery::ClassifyRoofsFromStreet(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens,
	const FGrappleReachOracle& Oracle, const FGrappleAuditSettings& Settings, const TArray<FGrappleReachRoof>& Roofs,
	const TArray<FGrappleReachAnchor>& Anchors, const TArray<FVector>& StreetSpots, TArray<EGrappleRoofReach>& OutReach)
{
	OutReach.Init(EGrappleRoofReach::NoAnchors, Roofs.Num());
	TArray<TArray<int32>> AnchorsOf;
	AnchorsOf.SetNum(Roofs.Num());
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		if (AnchorsOf.IsValidIndex(Anchors[Index].Roof))
		{
			AnchorsOf[Anchors[Index].Roof].Add(Index);
		}
	}
	// Street spots bucketed by the grapple's range: a roof only asks the spots round its anchors.
	TMap<FIntPoint, TArray<int32>> SpotCells;
	const double Cell = FMath::Max(Rules.Range, 100.f);
	for (int32 Index = 0; Index < StreetSpots.Num(); ++Index)
	{
		SpotCells.FindOrAdd(FIntPoint(FMath::FloorToInt32(StreetSpots[Index].X / Cell), FMath::FloorToInt32(StreetSpots[Index].Y / Cell))).Add(Index);
	}

	for (int32 R = 0; R < Roofs.Num(); ++R)
	{
		if (!Settings.IsAnchored(Roofs[R].HeightM))
		{
			OutReach[R] = EGrappleRoofReach::BelowAnchorHeight;
			continue;
		}
		if (AnchorsOf[R].Num() == 0)
		{
			OutReach[R] = EGrappleRoofReach::NoAnchors;
			continue;
		}
		bool bInRange = false;
		bool bReached = false;
		for (int32 AnchorIndex : AnchorsOf[R])
		{
			const FVector& Marker = Anchors[AnchorIndex].Marker;
			const FIntPoint Home(FMath::FloorToInt32(Marker.X / Cell), FMath::FloorToInt32(Marker.Y / Cell));
			for (int32 DX = -1; DX <= 1 && !bReached; ++DX)
			{
				for (int32 DY = -1; DY <= 1 && !bReached; ++DY)
				{
					const TArray<int32>* Bucket = SpotCells.Find(Home + FIntPoint(DX, DY));
					if (!Bucket)
					{
						continue;
					}
					for (int32 SpotIndex : *Bucket)
					{
						const FGrappleReachSpot Spot{ StreetSpots[SpotIndex], INDEX_NONE };
						if (!IsInRange(Rules, static_cast<float>(FVector::Dist(Spot.Centre, Marker))))
						{
							continue;
						}
						bInRange = true;
						if (Evaluate(Rules, Lens, Oracle, Spot, Anchors, AnchorIndex).Verdict == EGrappleReachVerdict::Reachable)
						{
							bReached = true;
							break;
						}
					}
				}
			}
			if (bReached)
			{
				break;
			}
		}
		OutReach[R] = bReached ? EGrappleRoofReach::Reached : (bInRange ? EGrappleRoofReach::Unreached : EGrappleRoofReach::NoStreetInRange);
	}
}

// --- FGrappleAnchorGrid --------------------------------------------------------------------------

void FGrappleAnchorGrid::Build(const TArray<FGrappleReachAnchor>& Anchors, float InCellSize)
{
	CellSize = FMath::Max(InCellSize, 100.f);
	Cells.Reset();
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		Cells.FindOrAdd(CellOf(Anchors[Index].Marker)).Add(Index);
	}
}

void FGrappleAnchorGrid::Gather(const FVector& Location, float Radius, TArray<int32>& OutIndices) const
{
	const FIntPoint Centre = CellOf(Location);
	const int32 Reach = FMath::Max(1, FMath::CeilToInt(Radius / CellSize));
	for (int32 DX = -Reach; DX <= Reach; ++DX)
	{
		for (int32 DY = -Reach; DY <= Reach; ++DY)
		{
			if (const TArray<int32>* Cell = Cells.Find(Centre + FIntPoint(DX, DY)))
			{
				OutIndices.Append(*Cell);
			}
		}
	}
}

FIntPoint FGrappleAnchorGrid::CellOf(const FVector& Location) const
{
	return FIntPoint(FMath::FloorToInt32(Location.X / CellSize), FMath::FloorToInt32(Location.Y / CellSize));
}
