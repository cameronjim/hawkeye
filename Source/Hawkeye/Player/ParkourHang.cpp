// The hang vocabulary of UParkourComponent (claude-docs/gameplay-semantics.md, traversal, "Hang"): shimmy along
// the ledge, round its corners, leap sideways or hop back to another ledge, the auto climb, the hands hand over
// hand, and the hang camera. The rest of the component is in ParkourComponent.cpp.

#include "Player/ParkourComponent.h"

#include "CollisionQueryParams.h"
#include "Combat/StrikePose.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Hawkeye.h"

namespace HawkeyeHang
{
	/** The top is looked for this far in from the edge, cm. */
	static constexpr float TopInset = 5.f;
	/** Along the line a top within this of the hang's is the same ledge, cm. */
	static constexpr float TopTolerance = 15.f;
	/** The face is looked at from this far out in front of it, cm. */
	static constexpr float FrontOut = 30.f;
	/** A spline this close to the edge is the edge's (the city's stand 2 cm proud of the facade), cm. */
	static constexpr float SplineReach = 25.f;
	/** Ledge splines are looked for within this of a point on the edge, cm. */
	static constexpr float SplineSearch = 60.f;
	/** Collinear ledge splines whose ends are this close join into one line, cm. */
	static constexpr float JoinGap = 15.f;
	/** At most this many splines are joined each way. */
	static constexpr int32 MaxJoins = 8;
	/** A line with no ledge data is probed this far each way at most, cm. */
	static constexpr float LineScan = 2000.f;
	/** Past the end of the ledge data the edge is followed this far at most, cm. */
	static constexpr float DataExtension = 300.f;
	/** The stick counts from this far pushed. */
	static constexpr float DeadZone = 0.25f;
	/** Shimmy when the stick is at least this much along the wall (within about 66 degrees of it). */
	static constexpr float ShimmyMinSide = 0.4f;
	/** The auto climb wants the stick within about 32 degrees of straight at the wall, at least half pushed. */
	static constexpr float ClimbMinUp = 0.85f;
	static constexpr float ClimbMinPush = 0.5f;
	/** A jump leaps or hops only with the stick this far pushed and this much to the side or back. */
	static constexpr float JumpMinPush = 0.5f;
	static constexpr float JumpMinSide = 0.6f;
	static constexpr float JumpMinBack = 0.6f;
	/** The leap looks this much further than it reaches, to say how far off the nearest ledge was, cm. */
	static constexpr float SearchBeyond = 150.f;
	/** A corner is tried this much past where the end margin stops her, and the hang round it may start this much further
	 * along the new ledge than the margin, cm: slack for a spline end or a sill a few cm off, not a pier to get past. */
	static constexpr float CornerSlack = 15.f;
	/** A leap's target face may stand this far out from her wall (a fire-escape rail is 95 cm out) or in, cm. */
	static constexpr float LeapFaceOut = 150.f;
	static constexpr float LeapFaceIn = 60.f;
	/** The hop back takes a top from this far below hers up to HopTopAbove over it, cm. */
	static constexpr float HopTopBelow = 150.f;
	static constexpr float HopTopAbove = 40.f;
	/** A leap arcs this high, the hop this high, cm. */
	static constexpr float LeapRise = 25.f;
	static constexpr float HopRise = 30.f;
	/** Hand lift at the middle of a step and how far it comes off the edge, cm. */
	static constexpr float StepLift = 6.f;
	static constexpr float StepOut = 4.f;
	/** The body sway at the middle of a hand's step, as a share of the hit lean's degrees. */
	static constexpr float SwayPeak = 0.7f;
	/** The hands' IK blends in and out over this, s. */
	static constexpr float HandsBlendSeconds = 0.15f;
	/** The lens changes shoulder when the camera's right points this far into the wall, and back under the other. */
	static constexpr float SideFlip = 0.35f;
	static constexpr float SideKeep = 0.15f;
	/** The shoulder swap takes this long end to end, s. */
	static constexpr float SideSwapSeconds = 0.5f;

	/** Her right as she hangs facing the wall. */
	static FVector AlongFor(const FVector& Normal)
	{
		return FVector::CrossProduct(FVector::UpVector, -Normal.GetSafeNormal2D()).GetSafeNormal2D();
	}

	/** Signed angle from A to B about up, degrees (counter-clockwise from above, as yaw runs). */
	static float SignedYaw(const FVector& A, const FVector& B)
	{
		const FVector A2 = A.GetSafeNormal2D();
		const FVector B2 = B.GetSafeNormal2D();
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::CrossProduct(A2, B2).Z),
			static_cast<float>(FVector::DotProduct(A2, B2))));
	}

	/**
	 * A point Alpha (0..1, already eased) round an outside corner: the angle about Pivot swings the way she turns
	 * (TurnDegrees' sign) from Start's to End's, through the open side, and the distance from the corner blends.
	 * Blending two rotated offsets instead cut the corner as a chord when she starts well short of it.
	 */
	static FVector ArcPoint(const FVector& Start, const FVector& End, const FVector& Pivot, float TurnDegrees, float Alpha)
	{
		const FVector2D From(Start.X - Pivot.X, Start.Y - Pivot.Y);
		const FVector2D To(End.X - Pivot.X, End.Y - Pivot.Y);
		const float FromAngle = FMath::RadiansToDegrees(FMath::Atan2(From.Y, From.X));
		const float ToAngle = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		float Swing = FRotator::NormalizeAxis(ToAngle - FromAngle);
		if (TurnDegrees < 0.f && Swing > 0.f)
		{
			Swing -= 360.f;
		}
		else if (TurnDegrees > 0.f && Swing < 0.f)
		{
			Swing += 360.f;
		}
		const float Angle = FMath::DegreesToRadians(FromAngle + Swing * Alpha);
		const float Radius = FMath::Lerp(static_cast<float>(From.Size()), static_cast<float>(To.Size()), Alpha);
		return FVector(Pivot.X + Radius * FMath::Cos(Angle), Pivot.Y + Radius * FMath::Sin(Angle), FMath::Lerp(Start.Z, End.Z, Alpha));
	}

	static const TCHAR* SideName(float Sign)
	{
		return Sign > 0.f ? TEXT("right") : TEXT("left");
	}

	static AController* LocalController(const AActor* Owner)
	{
		const APawn* Pawn = Cast<APawn>(Owner);
		AController* Controller = Pawn ? Pawn->GetController() : nullptr;
		return Controller && Controller->IsLocalPlayerController() ? Controller : nullptr;
	}
}

bool UParkourComponent::IsHangTransfer(EHawkeyeParkourMove Move)
{
	return Move == EHawkeyeParkourMove::HangCorner || Move == EHawkeyeParkourMove::HangLeap || Move == EHawkeyeParkourMove::HangHop;
}

// --- Input -------------------------------------------------------------------------------------------

void UParkourComponent::SetHangInput(const FVector& WorldInput)
{
	HangInput = FVector(WorldInput.X, WorldInput.Y, 0.f);
	HangInputFrame = GFrameCounter;
	bHangInputSet = true;
}

FVector UParkourComponent::GetFreshHangInput() const
{
	// Input_Move sends it on every frame the stick is held; a frame without it means the stick is centred.
	return bHangInputSet && GFrameCounter - HangInputFrame <= 1 ? HangInput : FVector::ZeroVector;
}

// --- The ledge line -----------------------------------------------------------------------------------

float UParkourComponent::GetHangAlong() const
{
	const AActor* Owner = GetOwner();
	return Owner ? HangLine.AlongOf(Owner->GetActorLocation()) : 0.f;
}

FVector UParkourComponent::HangLocationAt(float Distance) const
{
	const ACharacter* Character = GetCharacter();
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	return HangLine.PointAt(Distance) + HangLine.Normal * HangBackFromEdge + FVector(0.f, 0.f, HalfHeight - HangBelowLedge);
}

bool UParkourComponent::IsHangLedgeAt(const FVector& EdgePoint, const FVector& Normal, float TopZ, float* OutTopZ,
	AActor** OutActor, FString* OutWhyNot) const
{
	using namespace HawkeyeHang;
	const UWorld* World = GetWorld();
	const FVector N = Normal.GetSafeNormal2D();
	auto Fail = [OutWhyNot](const FString& Why)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = Why;
		}
		return false;
	};
	if (!World || N.IsNearlyZero())
	{
		return Fail(TEXT("no world"));
	}
	// The top, just in from the edge, at about her height.
	const FVector In(EdgePoint.X - N.X * TopInset, EdgePoint.Y - N.Y * TopInset, TopZ);
	FHitResult Top;
	const bool bTop = TraceLine(In + FVector(0.f, 0.f, 40.f), In - FVector(0.f, 0.f, 40.f), Top);
	if (bTop && Top.bStartPenetrating)
	{
		// Something tall stands on the top there (a chimney, a wing): the probe began inside it.
		return Fail(FString::Printf(TEXT("%s on the top"), *GetNameSafe(Top.GetActor())));
	}
	if (!bTop || Top.ImpactNormal.Z < 0.7f || FMath::Abs(Top.ImpactPoint.Z - TopZ) > TopTolerance)
	{
		return Fail(TEXT("the top runs out"));
	}
	const float Z = Top.ImpactPoint.Z;
	// Open air in front of the face: a wall across the ledge (an inside corner, a wing standing out) ends it.
	const FVector Front(EdgePoint.X + N.X * FrontOut, EdgePoint.Y + N.Y * FrontOut, Z - 8.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeHangFront), false, GetOwner());
	if (World->OverlapBlockingTestByChannel(Front, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(4.f), Params))
	{
		return Fail(TEXT("a wall across it"));
	}
	// The face itself, under the edge.
	FHitResult Face;
	if (!TraceLine(Front, Front - N * (FrontOut + 15.f), Face)
		|| FVector::DotProduct(Face.ImpactNormal.GetSafeNormal2D(), N) < 0.8f
		|| FMath::Abs(FVector::DotProduct(Face.ImpactPoint - EdgePoint, N)) > 12.f)
	{
		return Fail(TEXT("the face runs out"));
	}
	// Nothing standing on the top at the edge, where the hands go (a chimney).
	FHitResult Above;
	const float AboveZ = Z + 30.f;
	if (TraceLine(FVector(Front.X, Front.Y, AboveZ), FVector(In.X, In.Y, AboveZ), Above))
	{
		return Fail(FString::Printf(TEXT("%s on the top"), *GetNameSafe(Above.GetActor())));
	}
	if (OutTopZ)
	{
		*OutTopZ = Z;
	}
	if (OutActor)
	{
		*OutActor = Face.GetActor();
	}
	return true;
}

float UParkourComponent::FindLedgeRun(const FVector& EdgePoint, const FVector& Direction, const FVector& Normal, float TopZ,
	float MaxDistance, float Step, FString* OutWhyEnds) const
{
	const FVector D = Direction.GetSafeNormal2D();
	if (MaxDistance <= 0.f || D.IsNearlyZero())
	{
		return 0.f;
	}
	float Good = 0.f;
	float Bad = -1.f;
	for (float Distance = Step; ; Distance += Step)
	{
		const float At = FMath::Min(Distance, MaxDistance);
		if (!IsHangLedgeAt(EdgePoint + D * At, Normal, TopZ, nullptr, nullptr, OutWhyEnds))
		{
			Bad = At;
			break;
		}
		Good = At;
		if (At >= MaxDistance)
		{
			if (OutWhyEnds)
			{
				*OutWhyEnds = FString::Printf(TEXT("followed %.0f cm, no further"), MaxDistance);
			}
			return MaxDistance;
		}
	}
	// Narrowed to about a centimetre.
	for (int32 Pass = 0; Pass < 6 && Bad - Good > 1.f; ++Pass)
	{
		const float Middle = (Good + Bad) * 0.5f;
		if (IsHangLedgeAt(EdgePoint + D * Middle, Normal, TopZ))
		{
			Good = Middle;
		}
		else
		{
			Bad = Middle;
		}
	}
	return Good;
}

bool UParkourComponent::FindLedgeLine(const FHawkeyeParkourObstacle& Ledge, FHawkeyeLedgeLine& OutLine) const
{
	using namespace HawkeyeHang;
	OutLine = FHawkeyeLedgeLine();
	const UWorld* World = GetWorld();
	const FVector N = Ledge.WallNormal.GetSafeNormal2D();
	if (!World || !Ledge.bFound || N.IsNearlyZero())
	{
		return false;
	}
	const FVector T = AlongFor(N);
	OutLine.Origin = Ledge.LedgePoint;
	OutLine.Along = T;
	OutLine.Normal = N;
	const float TopZ = Ledge.LedgePoint.Z;

	// The ledge splines near a point on the edge that run along it: [start, end] along the line.
	struct FSegment
	{
		float A = 0.f;
		float B = 0.f;
		FString Name;
	};
	auto Collect = [&](const FVector& At, TArray<FSegment>& Out)
	{
		Out.Reset();
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeHangLedges), false, GetOwner());
		World->OverlapMultiByChannel(Overlaps, At, FQuat::Identity, ECC_GameTraceChannel1,
			FCollisionShape::MakeSphere(SplineSearch), Params);
		TSet<const AActor*> Seen;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const AActor* Actor = Overlap.GetActor();
			if (!Actor || Seen.Contains(Actor))
			{
				continue;
			}
			Seen.Add(Actor);
			TInlineComponentArray<USplineComponent*> Splines(Actor);
			for (const USplineComponent* Spline : Splines)
			{
				if (!Spline || !Spline->GetName().StartsWith(TEXT("Ledge")) || Spline->GetNumberOfSplinePoints() < 2)
				{
					continue;
				}
				const FVector P0 = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
				const FVector P1 = Spline->GetLocationAtSplinePoint(Spline->GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::World);
				const FVector Run = (P1 - P0).GetSafeNormal2D();
				if (FMath::Abs(FVector::DotProduct(Run, T)) < 0.98f
					|| FMath::Abs(FVector::DotProduct(P0 - OutLine.Origin, N)) > SplineReach
					|| FMath::Abs(FVector::DotProduct(P1 - OutLine.Origin, N)) > SplineReach
					|| FMath::Abs(P0.Z - TopZ) > TopTolerance || FMath::Abs(P1.Z - TopZ) > TopTolerance)
				{
					continue;
				}
				// A spline's up vector is its ledge's outward normal (the sample's convention): the other side's is not ours.
				const FVector Up = Spline->GetUpVectorAtSplinePoint(0, ESplineCoordinateSpace::World).GetSafeNormal2D();
				if (!Up.IsNearlyZero() && FVector::DotProduct(Up, N) < 0.7f)
				{
					continue;
				}
				FSegment Segment;
				Segment.A = FMath::Min(OutLine.AlongOf(P0), OutLine.AlongOf(P1));
				Segment.B = FMath::Max(OutLine.AlongOf(P0), OutLine.AlongOf(P1));
				Segment.Name = FString::Printf(TEXT("%s on %s"), *Spline->GetName(), *Actor->GetActorNameOrLabel());
				Out.Add(Segment);
			}
		}
	};

	TArray<FSegment> Found;
	Collect(Ledge.LedgePoint, Found);
	const FSegment* Here = nullptr;
	for (const FSegment& Segment : Found)
	{
		if (Segment.A <= 5.f && Segment.B >= -5.f && (!Here || Segment.B - Segment.A > Here->B - Here->A))
		{
			Here = &Segment;
		}
	}
	if (Here)
	{
		OutLine.MinAlong = Here->A;
		OutLine.MaxAlong = Here->B;
		OutLine.DataSegments = 1;
		OutLine.Source = Here->Name;
		// Row houses of one height: the next building's ledge carries straight on from this one's end.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 Join = 0; Join < MaxJoins; ++Join)
			{
				const float End = Side == 0 ? OutLine.MaxAlong : OutLine.MinAlong;
				TArray<FSegment> Next;
				Collect(OutLine.PointAt(End), Next);
				bool bJoined = false;
				for (const FSegment& Segment : Next)
				{
					if (Side == 0 && FMath::Abs(Segment.A - End) <= JoinGap && Segment.B > End + 1.f)
					{
						OutLine.MaxAlong = Segment.B;
						bJoined = true;
						break;
					}
					if (Side == 1 && FMath::Abs(Segment.B - End) <= JoinGap && Segment.A < End - 1.f)
					{
						OutLine.MinAlong = Segment.A;
						bJoined = true;
						break;
					}
				}
				if (!bJoined)
				{
					break;
				}
				++OutLine.DataSegments;
			}
		}
		if (OutLine.DataSegments > 1)
		{
			OutLine.Source += FString::Printf(TEXT(" (+%d more)"), OutLine.DataSegments - 1);
		}
		// Past the data the same edge may carry on (a join with no spline at it): follow it a little way.
		const float MorePlus = FindLedgeRun(OutLine.PointAt(OutLine.MaxAlong), T, N, TopZ, DataExtension, 20.f, &OutLine.MaxEndWhy);
		const float MoreMinus = FindLedgeRun(OutLine.PointAt(OutLine.MinAlong), -T, N, TopZ, DataExtension, 20.f, &OutLine.MinEndWhy);
		if (MorePlus < 1.f)
		{
			OutLine.MaxEndWhy = FString::Printf(TEXT("the end of the ledge data (%s)"), *OutLine.MaxEndWhy);
		}
		if (MoreMinus < 1.f)
		{
			OutLine.MinEndWhy = FString::Printf(TEXT("the end of the ledge data (%s)"), *OutLine.MinEndWhy);
		}
		OutLine.MaxAlong += MorePlus;
		OutLine.MinAlong -= MoreMinus;
	}
	else
	{
		OutLine.MaxAlong = FindLedgeRun(OutLine.Origin, T, N, TopZ, LineScan, 20.f, &OutLine.MaxEndWhy);
		OutLine.MinAlong = -FindLedgeRun(OutLine.Origin, -T, N, TopZ, LineScan, 20.f, &OutLine.MinEndWhy);
		OutLine.Source = TEXT("probed along the wall");
	}
	OutLine.bValid = true;
	return true;
}

// --- The hang's state ----------------------------------------------------------------------------------

FHawkeyeParkourObstacle UParkourComponent::ProbeHangLedge(const FVector& EdgePoint, const FVector& Normal, AActor* Actor) const
{
	FHawkeyeParkourObstacle Out;
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return Out;
	}
	const FVector Location = Character->GetActorLocation();
	const FVector Feet(Location.X, Location.Y, GetFeetZ());
	Out.bFound = true;
	Out.WallNormal = Normal.GetSafeNormal2D();
	Out.LedgePoint = EdgePoint;
	Out.WallPoint = EdgePoint - FVector(0.f, 0.f, 10.f);
	Out.Height = EdgePoint.Z - Feet.Z;
	Out.Distance = FMath::Max(0.f, HangBackFromEdge - Character->GetCapsuleComponent()->GetScaledCapsuleRadius());
	Out.Actor = Actor;
	ProbeBeyond(Feet, Out);
	ProbeStanding(Out);
	return Out;
}

void UParkourComponent::BeginHangState()
{
	using namespace HawkeyeHang;
	HangInput = FVector::ZeroVector;
	bHangInputSet = false;
	HangUpHeld = 0.f;
	bShimmying = false;
	bShimmyStopLogged = false;
	bAutoClimbRefusalLogged = false;
	LastHangRefusal.Reset();
	SteppingHand = INDEX_NONE;
	StepElapsed = 0.f;
	SwayAlpha = 0.f;
	bHandRestKnown = false;

	if (!FindLedgeLine(HangObstacle, HangLine))
	{
		HangLine = FHawkeyeLedgeLine();
		HangLine.bValid = true;
		HangLine.Origin = HangObstacle.LedgePoint;
		HangLine.Normal = HangObstacle.WallNormal.GetSafeNormal2D();
		HangLine.Along = AlongFor(HangLine.Normal);
		HangLine.Source = TEXT("no edge found here: climb or drop only");
	}
	HangStartAlong = GetHangAlong();
	HangProbedAlong = HangStartAlong;
	// The hands start at their places on the new line (their rest is read again once the IK has let go).
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const float Rest = HandRest[Hand].IsNearlyZero() ? (Hand == 0 ? -HangHandSpread : HangHandSpread) : HandRest[Hand].Y;
		HandAlong[Hand] = HangStartAlong + Rest;
	}
	HangStartStand = HangObstacle.StandPoint;
	bHangStartStand = HangObstacle.bStandingSurface;

	// The far side of the top, for the roof-edge guard; the climb keeps what the grab or the drop measured.
	const FHawkeyeParkourObstacle Probe = ProbeHangLedge(HangObstacle.LedgePoint, HangLine.Normal, HangObstacle.Actor);
	HangFarSideDrop = Probe.Depth > 0.f ? Probe.FarSideDrop - Probe.Height : 0.f;
	bHangTopGuarded = Probe.Depth > 0.f && HangFarSideDrop > RoofEdgeGuardDrop;
	UE_LOG(LogHawkeye, Log,
		TEXT("%s: hang line: %s, %.0f cm long, %.0f cm from its left end and %.0f from its right; top guarded %d (depth %.0f, %.0f cm below it beyond)"),
		*GetNameSafe(GetOwner()), *HangLine.Source, HangLine.Length(), HangStartAlong - HangLine.MinAlong,
		HangLine.MaxAlong - HangStartAlong, bHangTopGuarded ? 1 : 0, Probe.Depth, HangFarSideDrop);
}

void UParkourComponent::EndHangState()
{
	if (bShimmying)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: hang: shimmy ends at %.0f cm along"), *GetNameSafe(GetOwner()), GetHangAlong());
	}
	bShimmying = false;
	HangUpHeld = 0.f;
	SteppingHand = INDEX_NONE;
	SwayAlpha = 0.f;
	bHangInputSet = false;
}

void UParkourComponent::RefreshHangLedge()
{
	if (!bHanging)
	{
		return;
	}
	const float Along = GetHangAlong();
	if (FMath::Abs(Along - HangProbedAlong) < 5.f)
	{
		return;
	}
	HangProbedAlong = Along;
	const FVector Edge = HangLine.PointAt(Along);
	FHawkeyeParkourObstacle Probe = ProbeHangLedge(Edge, HangLine.Normal, HangObstacle.Actor);
	if (!Probe.bStandingSurface && bHangStartStand)
	{
		// Where she caught the edge the top was somewhere to stand: the same roof, as far along as she has come.
		const ACharacter* Character = GetCharacter();
		const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		const FVector Shifted = HangStartStand + HangLine.Along * (Along - HangStartAlong);
		FHitResult Floor;
		if (TraceLine(Shifted + FVector(0.f, 0.f, 40.f), Shifted - FVector(0.f, 0.f, 60.f), Floor) && Floor.ImpactNormal.Z >= 0.7f
			&& CapsuleFits(Floor.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 2.f)))
		{
			Probe.bStandingSurface = true;
			Probe.StandPoint = Floor.ImpactPoint;
		}
	}
	HangObstacle.LedgePoint = Edge;
	HangObstacle.Height = Probe.Height;
	HangObstacle.Depth = Probe.Depth;
	HangObstacle.bClearBeyond = Probe.bClearBeyond;
	HangObstacle.LandingPoint = Probe.LandingPoint;
	HangObstacle.LandingDrop = Probe.LandingDrop;
	HangObstacle.FarSideDrop = Probe.FarSideDrop;
	HangObstacle.bStandingSurface = Probe.bStandingSurface;
	HangObstacle.StandPoint = Probe.StandPoint;
	HangObstacle.BeyondWhyNot = Probe.BeyondWhyNot;
	HangObstacle.StandWhyNot = Probe.StandWhyNot;
	HangFarSideDrop = Probe.Depth > 0.f ? Probe.FarSideDrop - Probe.Height : 0.f;
	bHangTopGuarded = Probe.Depth > 0.f && HangFarSideDrop > RoofEdgeGuardDrop;
}

// --- Each tick -------------------------------------------------------------------------------------------

void UParkourComponent::AdvanceHang(float DeltaSeconds)
{
	using namespace HawkeyeHang;
	ACharacter* Character = GetCharacter();
	if (!bHanging || !Character)
	{
		return;
	}
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->Velocity = FVector::ZeroVector;
	}
	const FVector Input = GetFreshHangInput();
	const float Push = FMath::Min(static_cast<float>(Input.Size2D()), 1.f);
	const FVector Direction = Input.GetSafeNormal2D();
	const float Side = Push >= DeadZone ? static_cast<float>(FVector::DotProduct(Direction, HangLine.Along)) : 0.f;
	const float Up = Push >= DeadZone ? static_cast<float>(FVector::DotProduct(Direction, -HangLine.Normal)) : 0.f;

	float Moved = 0.f;
	float Sign = 0.f;
	if (FMath::Abs(Side) >= ShimmyMinSide)
	{
		HangUpHeld = 0.f;
		bAutoClimbRefusalLogged = false;
		Sign = Side > 0.f ? 1.f : -1.f;
		Moved = Shimmy(Sign, ShimmySpeed * Push * DeltaSeconds);
		if (!bHanging)
		{
			return;   // round a corner: that is a move now
		}
	}
	else
	{
		if (bShimmying)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: shimmy stops at %.0f cm along (the stick let go)"), *GetNameSafe(Character),
				GetHangAlong());
		}
		bShimmying = false;
		bShimmyStopLogged = false;
		if (Push >= ClimbMinPush && Up >= ClimbMinUp)
		{
			HangUpHeld += DeltaSeconds;
			if (HangUpHeld >= AutoClimbHoldSeconds)
			{
				TryAutoClimb();
				if (!bHanging)
				{
					return;
				}
			}
		}
		else
		{
			HangUpHeld = 0.f;
			bAutoClimbRefusalLogged = false;
		}
	}
	AdvanceHangHands(DeltaSeconds, Moved, Sign);
	KeepHangCameraOffTheWall(DeltaSeconds);
}

bool UParkourComponent::SweepHang(const FVector& From, const FVector& To, FHitResult& OutHit) const
{
	const ACharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeHangSweep), false, Character);
	if (!World->SweepSingleByChannel(OutHit, From, To, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() - 2.f, Capsule->GetScaledCapsuleHalfHeight() - 2.f), Params))
	{
		return false;
	}
	if (OutHit.bStartPenetrating)
	{
		// Already touching something where she hangs: only where she is going matters.
		if (CapsuleFits(To))
		{
			return false;
		}
		OutHit.Time = 0.f;
	}
	return true;
}

float UParkourComponent::Shimmy(float Sign, float Step)
{
	using namespace HawkeyeHang;
	ACharacter* Character = GetCharacter();
	if (!Character || Step <= 0.f)
	{
		return 0.f;
	}
	const FVector D = HangLine.Along * Sign;
	const float TopZ = HangLine.Origin.Z;
	const float Start = GetHangAlong();
	const float Low = HangLine.MinAlong + ShimmyEndMargin;
	const float High = HangLine.MaxAlong - ShimmyEndMargin;
	float Target = Start + Sign * Step;
	FString Why;
	bool bAcross = false;
	FVector BlockPoint = FVector::ZeroVector;

	// The ledge's ends: both hands stay on it.
	if (Sign > 0.f && Target > High)
	{
		Target = FMath::Max(Start, High);
		Why = FString::Printf(TEXT("the ledge's end: %s"), *HangLine.MaxEndWhy);
	}
	else if (Sign < 0.f && Target < Low)
	{
		Target = FMath::Min(Start, Low);
		Why = FString::Printf(TEXT("the ledge's end: %s"), *HangLine.MinEndWhy);
	}
	// The leading hand on the top: something standing on it (a chimney) or the edge giving out stops her.
	const float Lead = FMath::Max(ShimmyEndMargin - 3.f, 0.f);
	if (FMath::Abs(Target - Start) > KINDA_SMALL_NUMBER)
	{
		FString LeadWhy;
		if (!IsHangLedgeAt(HangLine.PointAt(Target + Sign * Lead), HangLine.Normal, TopZ, nullptr, nullptr, &LeadWhy))
		{
			float Good = Start;
			float Bad = Target;
			if (IsHangLedgeAt(HangLine.PointAt(Start + Sign * Lead), HangLine.Normal, TopZ))
			{
				for (int32 Pass = 0; Pass < 5; ++Pass)
				{
					const float Middle = (Good + Bad) * 0.5f;
					if (IsHangLedgeAt(HangLine.PointAt(Middle + Sign * Lead), HangLine.Normal, TopZ))
					{
						Good = Middle;
					}
					else
					{
						Bad = Middle;
					}
				}
			}
			Target = Good;
			Why = FString::Printf(TEXT("the leading hand: %s"), *LeadWhy);
		}
	}
	// The body's way along the wall: a fire-escape rail, a ladder, a wall across the ledge.
	if (FMath::Abs(Target - Start) > KINDA_SMALL_NUMBER)
	{
		FHitResult Hit;
		if (SweepHang(Character->GetActorLocation(), HangLocationAt(Target), Hit))
		{
			const float Free = FMath::Max(0.f, Hit.Time * FMath::Abs(Target - Start) - 0.5f);
			Target = Start + Sign * Free;
			Why = FString::Printf(TEXT("%s in the way"), *GetNameSafe(Hit.GetActor()));
			if (FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), -D) > 0.7f)
			{
				bAcross = true;
				BlockPoint = Hit.ImpactPoint;
			}
		}
	}

	const float Moved = FMath::Abs(Target - Start);
	if (Moved > 0.01f)
	{
		Character->SetActorLocation(HangLocationAt(Target));
		HangObstacle.LedgePoint = HangLine.PointAt(Target);
		HangObstacle.WallPoint = FVector(HangObstacle.LedgePoint.X, HangObstacle.LedgePoint.Y, HangObstacle.WallPoint.Z);
		if (!bShimmying)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: shimmy %s (up to %.0f cm/s) from %.0f cm along (the line runs %.0f to %.0f, %s)"),
				*GetNameSafe(Character), SideName(Sign), ShimmySpeed, Start, HangLine.MinAlong, HangLine.MaxAlong, *HangLine.Source);
		}
		bShimmying = true;
		bShimmyStopLogged = false;
		LastHangRefusal.Reset();
	}
	else
	{
		bShimmying = false;
	}
	if (Moved < Step * 0.5f)
	{
		// Pushing into the end, or into a wall across the ledge: round the corner if there is one.
		if (TryTurnCorner(Sign, bAcross, BlockPoint))
		{
			return Moved;
		}
		if (!bShimmyStopLogged)
		{
			bShimmyStopLogged = true;
			LastHangRefusal = Why.IsEmpty() ? TEXT("stopped") : Why;
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: shimmy %s stops at %.0f cm along: %s"), *GetNameSafe(Character), SideName(Sign),
				Target, *LastHangRefusal);
		}
	}
	return Moved;
}

bool UParkourComponent::TryTurnCorner(float Sign, bool bBlockedAcross, const FVector& BlockPoint)
{
	using namespace HawkeyeHang;
	ACharacter* Character = GetCharacter();
	if (!Character || !bHanging)
	{
		return false;
	}
	const FVector N = HangLine.Normal;
	const FVector D = HangLine.Along * Sign;
	const float TopZ = HangLine.Origin.Z;
	const float Here = GetHangAlong();
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Reach = FMath::Max(ShimmyEndMargin, Radius) + 10.f;
	// The line's end this way, and where a wall across the ledge stands if one stopped her before it.
	const float LineEnd = Sign > 0.f ? HangLine.MaxAlong : HangLine.MinAlong;
	float AcrossAlong = LineEnd;
	if (bBlockedAcross)
	{
		const float BlockAlong = HangLine.AlongOf(BlockPoint);
		if (Sign * (BlockAlong - AcrossAlong) < 0.f)
		{
			AcrossAlong = BlockAlong;
		}
	}
	// An outside corner is tried at the line's end: the end margin stops her 25 cm short of it (the district's parapets
	// are flush with both facades at every corner, so nothing stands proud there to stop her sooner).
	const bool bNearEnd = Sign * (LineEnd - Here) <= Reach + CornerSlack;
	// An inside corner only right at the wall across (a chimney or a rail short of the end is not a corner).
	const bool bNearAcross = Sign * (AcrossAlong - Here) <= Reach;
	if (!bNearEnd && !bNearAcross)
	{
		return false;
	}
	FString WhyNot;
	/** Every candidate's reason, for the log. */
	TArray<FString> Reasons;
	auto Note = [&Reasons, &WhyNot]()
	{
		if (!WhyNot.IsEmpty() && !Reasons.Contains(WhyNot))
		{
			Reasons.Add(WhyNot);
		}
	};
	const float HandReach = FMath::Max(ShimmyEndMargin - 3.f, 0.f);

	auto Turn = [&](const FVector& CornerEnd, const FVector& AwayFromCorner, float FirstOffset, const FVector& NewNormal, float NewTopZ,
		AActor* Actor, bool bOutside, const FVector& Pivot, const FVector& End)
	{
		// The hang on the new ledge: FirstOffset in from its corner end, or a few cm further if the capsule does not fit
		// right there.
		for (float Extra = 0.f; Extra <= CornerSlack; Extra += 5.f)
		{
			const FVector Edge = CornerEnd + AwayFromCorner * (FirstOffset + Extra);
			if (!IsHangLedgeAt(Edge + AwayFromCorner * HandReach, NewNormal, NewTopZ, nullptr, nullptr, &WhyNot))
			{
				WhyNot = FString::Printf(TEXT("the ledge round the %s corner is too short for her hands: %s"),
					bOutside ? TEXT("outside") : TEXT("inside"), *WhyNot);
				return false;
			}
			FHawkeyeParkourObstacle Target = ProbeHangLedge(FVector(Edge.X, Edge.Y, NewTopZ), NewNormal, Actor);
			// Measured from where she will hang, not from here.
			Target.Height = HangBelowLedge;
			const FVector Hang = HangLocationFor(Target);
			if (!CapsuleFits(Hang))
			{
				WhyNot = FString::Printf(TEXT("no room to hang round the %s corner (%s)"), bOutside ? TEXT("outside") : TEXT("inside"),
					*DescribeBlocker(Hang));
				continue;
			}
			const float Degrees = SignedYaw(N, NewNormal);
			if (bOutside)
			{
				// The swing itself, a quarter, a half and three quarters round.
				for (const float Alpha : { 0.25f, 0.5f, 0.75f })
				{
					const FVector Point = ArcPoint(Character->GetActorLocation(), Hang, Pivot, Degrees, Alpha);
					if (!CapsuleFits(Point))
					{
						WhyNot = FString::Printf(TEXT("no room to swing round the outside corner (%s)"), *DescribeBlocker(Point));
						return false;
					}
				}
			}
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: round the %s corner at %s onto %s: %.1f s, turning %+.0f degrees, to %.0f cm along it"),
				*GetNameSafe(Character), bOutside ? TEXT("outside") : TEXT("inside"), *End.ToCompactString(), *GetNameSafe(Actor),
				CornerSeconds, Degrees, FirstOffset + Extra);
			return BeginHangTransfer(EHawkeyeParkourMove::HangCorner, Target, CornerSeconds, 0.f, Degrees, bOutside, Pivot);
		}
		return false;
	};

	if (bNearEnd)
	{
		// Outside: the next face turns away from her wall at the end, facing along her way.
		const FVector End = HangLine.PointAt(LineEnd);
		for (const float In : { 15.f, 35.f, 55.f, 75.f })
		{
			// From past the line's end too: a line probed along the wall can end a little short of or past the corner.
			const FVector From = End - N * In + D * (CornerReach + 30.f) - FVector(0.f, 0.f, 8.f);
			FHitResult Face;
			if (!TraceLine(From, From - D * (CornerReach + 90.f), Face))
			{
				WhyNot = FString::Printf(TEXT("no face round the corner %.0f cm in"), In);
				Note();
				continue;
			}
			const FVector NewNormal = Face.ImpactNormal.GetSafeNormal2D();
			if (FVector::DotProduct(NewNormal, D) < 0.8f)
			{
				WhyNot = FString::Printf(TEXT("%s %.0f cm in faces %.2f her way (%s)"), *GetNameSafe(Face.GetActor()), In,
					FVector::DotProduct(NewNormal, D), Face.bStartPenetrating ? TEXT("the probe began inside it") : TEXT("met it"));
				Note();
				continue;
			}
			const FVector Found(Face.ImpactPoint.X, Face.ImpactPoint.Y, TopZ);
			float NewTopZ = TopZ;
			AActor* Actor = nullptr;
			if (!IsHangLedgeAt(Found, NewNormal, TopZ, &NewTopZ, &Actor, &WhyNot))
			{
				WhyNot = FString::Printf(TEXT("the face round the corner %.0f cm in: %s"), In, *WhyNot);
				Note();
				continue;
			}
			// Its end nearest the corner, back toward her wall's side.
			const FVector CornerEnd = Found + N * FindLedgeRun(Found, N, NewNormal, NewTopZ, In + CornerReach, 5.f);
			if (FVector::Dist2D(End, CornerEnd) > CornerReach)
			{
				WhyNot = FString::Printf(TEXT("the ledge round the corner starts %.0f cm off"), FVector::Dist2D(End, CornerEnd));
				Note();
				continue;
			}
			// It swings round where the two faces meet.
			const FVector Pivot = HangLine.PointAt(HangLine.AlongOf(CornerEnd));
			if (Turn(CornerEnd, -N, ShimmyEndMargin, NewNormal, NewTopZ, Actor, true, Pivot, End))
			{
				return true;
			}
			Note();
		}
	}
	if (bNearAcross)
	{
		// Inside: a wall across the ledge at its end, facing back at her, with the same top.
		const FVector End = HangLine.PointAt(AcrossAlong);
		for (const float Out : { 20.f, 45.f })
		{
			const FVector From = End + N * Out - D * 30.f - FVector(0.f, 0.f, 8.f);
			FHitResult Face;
			if (!TraceLine(From, From + D * (CornerReach + 40.f), Face))
			{
				continue;
			}
			const FVector NewNormal = Face.ImpactNormal.GetSafeNormal2D();
			if (FVector::DotProduct(NewNormal, -D) < 0.8f || FVector::DotProduct(Face.ImpactPoint - End, D) > CornerReach)
			{
				continue;
			}
			const FVector Found(Face.ImpactPoint.X, Face.ImpactPoint.Y, TopZ);
			float NewTopZ = TopZ;
			AActor* Actor = nullptr;
			if (!IsHangLedgeAt(Found, NewNormal, TopZ, &NewTopZ, &Actor, &WhyNot))
			{
				continue;
			}
			// Its end in the corner, toward her wall.
			const FVector CornerEnd = Found - N * FindLedgeRun(Found, -N, NewNormal, NewTopZ, Out + CornerReach, 5.f);
			if (FVector::Dist2D(End, CornerEnd) > CornerReach)
			{
				WhyNot = FString::Printf(TEXT("the ledge across starts %.0f cm off"), FVector::Dist2D(End, CornerEnd));
				continue;
			}
			// Out along it far enough that the capsule clears her old wall.
			if (Turn(CornerEnd, N, FMath::Max(ShimmyEndMargin, HangBackFromEdge + 2.f), NewNormal, NewTopZ, Actor, false, CornerEnd, End))
			{
				return true;
			}
			Note();
		}
	}
	Note();
	if (!Reasons.IsEmpty() && !bShimmyStopLogged)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: hang: no corner at the %s end: %s"), *GetNameSafe(Character), SideName(Sign),
			*FString::Join(Reasons, TEXT("; ")));
	}
	return false;
}

void UParkourComponent::TryAutoClimb()
{
	RefreshHangLedge();
	if (!HangObstacle.bStandingSurface)
	{
		if (!bAutoClimbRefusalLogged)
		{
			bAutoClimbRefusalLogged = true;
			LastHangRefusal = TEXT("the stick held up: nowhere to stand on the top");
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: %s"), *GetNameSafe(GetOwner()), *LastHangRefusal);
		}
		return;
	}
	if (bHangTopGuarded)
	{
		if (!bAutoClimbRefusalLogged)
		{
			bAutoClimbRefusalLogged = true;
			LastHangRefusal = FString::Printf(
				TEXT("roof-edge guard: the top is a parapet with %.0f cm below its far side, so the stick does not climb it (the jump key does)"),
				HangFarSideDrop);
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: %s"), *GetNameSafe(GetOwner()), *LastHangRefusal);
		}
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: hang: the stick held up %.1f s: climbs"), *GetNameSafe(GetOwner()), HangUpHeld);
	HangUpHeld = 0.f;
	ClimbFromHang();
}

// --- Hang jumps ----------------------------------------------------------------------------------------

bool UParkourComponent::FindLeapTarget(float Sign, FHawkeyeParkourObstacle& OutTarget, float& OutDistance, FString& OutWhyNot) const
{
	using namespace HawkeyeHang;
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	const FVector N = HangLine.Normal;
	const FVector D = HangLine.Along * Sign;
	const float TopZ = HangLine.Origin.Z;
	const float Here = GetHangAlong();
	const FVector Location = Character->GetActorLocation();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// Past where a shimmy would take her: the next ledge, over a gap.
	const float Reach = FMath::Max(0.f, Sign > 0.f ? HangLine.MaxAlong - Here : Here - HangLine.MinAlong);
	OutWhyNot.Reset();
	for (float Offset = Reach + 10.f; Offset <= HangLeapReach + SearchBeyond; Offset += 10.f)
	{
		const FVector Edge = HangLine.PointAt(Here + Sign * Offset);
		for (const float Below : { 8.f, 40.f })
		{
			const FVector From = Edge + N * LeapFaceOut - FVector(0.f, 0.f, Below);
			FHitResult Face;
			if (!TraceLine(From, From - N * (LeapFaceOut + LeapFaceIn), Face)
				|| FVector::DotProduct(Face.ImpactNormal.GetSafeNormal2D(), N) < 0.8f)
			{
				continue;
			}
			const FVector NewNormal = Face.ImpactNormal.GetSafeNormal2D();
			const FVector In = Face.ImpactPoint - NewNormal * TopInset;
			FHitResult Top;
			if (!TraceLine(FVector(In.X, In.Y, TopZ + HangLeapHeightTolerance + 20.f),
					FVector(In.X, In.Y, TopZ - HangLeapHeightTolerance - 20.f), Top)
				|| Top.ImpactNormal.Z < 0.7f || FMath::Abs(Top.ImpactPoint.Z - TopZ) > HangLeapHeightTolerance)
			{
				continue;
			}
			const float NewTopZ = Top.ImpactPoint.Z;
			const FVector Found(Face.ImpactPoint.X, Face.ImpactPoint.Y, NewTopZ);
			AActor* Actor = nullptr;
			if (!IsHangLedgeAt(Found, NewNormal, NewTopZ, nullptr, &Actor))
			{
				continue;
			}
			// Both hands on it: the hang's middle a margin in from the end she arrives at.
			const FVector NewAlong = HawkeyeHang::AlongFor(NewNormal) * (FVector::DotProduct(HawkeyeHang::AlongFor(NewNormal), D) >= 0.f ? 1.f : -1.f);
			const FVector Edge2 = Found + NewAlong * ShimmyEndMargin;
			if (!IsHangLedgeAt(Edge2 + NewAlong * FMath::Max(ShimmyEndMargin - 3.f, 0.f), NewNormal, NewTopZ))
			{
				continue;
			}
			const FVector Hang = Edge2 + NewNormal * HangBackFromEdge + FVector(0.f, 0.f, HalfHeight - HangBelowLedge);
			const float Distance = FVector::Dist2D(Location, Hang);
			if (Distance > HangLeapReach)
			{
				OutWhyNot = FString::Printf(TEXT("the nearest ledge to the %s is %.0f cm off (a leap reaches %.0f)"), SideName(Sign),
					Distance, HangLeapReach);
				return false;
			}
			if (!CapsuleFits(Hang))
			{
				OutWhyNot = FString::Printf(TEXT("no room to hang on the ledge %.0f cm to the %s (%s)"), Distance, SideName(Sign),
					*DescribeBlocker(Hang));
				return false;
			}
			FHitResult Block;
			if (SweepHang(Location + N * 15.f, Hang + NewNormal * 15.f, Block))
			{
				OutWhyNot = FString::Printf(TEXT("%s is in the way of the ledge %.0f cm to the %s"), *GetNameSafe(Block.GetActor()),
					Distance, SideName(Sign));
				return false;
			}
			OutTarget = ProbeHangLedge(Edge2, NewNormal, Actor);
			OutTarget.Height = HangBelowLedge;
			OutTarget.LedgePoint = FVector(Edge2.X, Edge2.Y, NewTopZ);
			OutDistance = Distance;
			return true;
		}
	}
	OutWhyNot = FString::Printf(TEXT("no ledge at her height within %.0f cm to the %s (this one runs %.0f cm further that way)"),
		HangLeapReach, SideName(Sign), Reach);
	return false;
}

bool UParkourComponent::FindHopTarget(FHawkeyeParkourObstacle& OutTarget, FString& OutWhyNot) const
{
	using namespace HawkeyeHang;
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	const FVector N = HangLine.Normal;
	const float TopZ = HangLine.Origin.Z;
	const FVector Location = Character->GetActorLocation();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	OutWhyNot = FString::Printf(TEXT("nothing to grab within %.0f cm behind her"), HangHopReach);
	for (const float Below : { 8.f, 60.f, 120.f })
	{
		const FVector From(Location.X, Location.Y, TopZ - Below);
		FHitResult Face;
		if (!TraceLine(From, From + N * HangHopReach, Face))
		{
			continue;
		}
		const FVector NewNormal = Face.ImpactNormal.GetSafeNormal2D();
		if (FVector::DotProduct(NewNormal, -N) < 0.8f)
		{
			OutWhyNot = FString::Printf(TEXT("%s behind her is not a wall facing her"), *GetNameSafe(Face.GetActor()));
			continue;
		}
		const FVector In = Face.ImpactPoint - NewNormal * TopInset;
		FHitResult Top;
		if (!TraceLine(FVector(In.X, In.Y, TopZ + HopTopAbove + 20.f), FVector(In.X, In.Y, TopZ - HopTopBelow - 20.f), Top)
			|| Top.ImpactNormal.Z < 0.7f || Top.ImpactPoint.Z > TopZ + HopTopAbove || Top.ImpactPoint.Z < TopZ - HopTopBelow)
		{
			OutWhyNot = FString::Printf(TEXT("the wall %.0f cm behind her has no top in reach"), Face.Distance);
			continue;
		}
		const float NewTopZ = Top.ImpactPoint.Z;
		const FVector Found(Face.ImpactPoint.X, Face.ImpactPoint.Y, NewTopZ);
		AActor* Actor = nullptr;
		FString Why;
		if (!IsHangLedgeAt(Found, NewNormal, NewTopZ, nullptr, &Actor, &Why))
		{
			OutWhyNot = FString::Printf(TEXT("the wall %.0f cm behind her: %s"), Face.Distance, *Why);
			continue;
		}
		const FVector Hang = Found + NewNormal * HangBackFromEdge + FVector(0.f, 0.f, HalfHeight - HangBelowLedge);
		if (!CapsuleFits(Hang))
		{
			OutWhyNot = FString::Printf(TEXT("no room to hang on the wall %.0f cm behind her"), Face.Distance);
			continue;
		}
		OutTarget = ProbeHangLedge(Found, NewNormal, Actor);
		OutTarget.Height = HangBelowLedge;
		return true;
	}
	return false;
}

bool UParkourComponent::JumpFromHang()
{
	using namespace HawkeyeHang;
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	if (!bHanging || !Character || !Movement)
	{
		return false;
	}
	const FVector Input = GetFreshHangInput();
	const float Push = FMath::Min(static_cast<float>(Input.Size2D()), 1.f);
	const FVector Direction = Input.GetSafeNormal2D();
	const float Side = Push >= JumpMinPush ? static_cast<float>(FVector::DotProduct(Direction, HangLine.Along)) : 0.f;
	const float Back = Push >= JumpMinPush ? static_cast<float>(FVector::DotProduct(Direction, HangLine.Normal)) : 0.f;

	if (FMath::Abs(Side) >= JumpMinSide)
	{
		const float Sign = Side > 0.f ? 1.f : -1.f;
		FHawkeyeParkourObstacle Target;
		float Distance = 0.f;
		FString WhyNot;
		if (!FindLeapTarget(Sign, Target, Distance, WhyNot))
		{
			LastHangRefusal = WhyNot;
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: jump %s: no leap: %s; she stays on the ledge"), *GetNameSafe(Character),
				SideName(Sign), *WhyNot);
			return false;
		}
		const float Seconds = FMath::Lerp(0.3f, HangLeapSeconds, FMath::Clamp(Distance / FMath::Max(HangLeapReach, 1.f), 0.f, 1.f));
		UE_LOG(LogHawkeye, Log, TEXT("%s: hang: jump %s: leaps %.0f cm to a ledge on %s (top %+.0f cm), %.2f s"),
			*GetNameSafe(Character), SideName(Sign), Distance, *GetNameSafe(Target.Actor), Target.LedgePoint.Z - HangLine.Origin.Z,
			Seconds);
		LastHangRefusal.Reset();
		return BeginHangTransfer(EHawkeyeParkourMove::HangLeap, Target, Seconds, LeapRise, 0.f, false, FVector::ZeroVector);
	}
	if (Back >= JumpMinBack)
	{
		FHawkeyeParkourObstacle Target;
		FString WhyNot;
		// Round over the shoulder the stick leans to (the right when it is straight back).
		const float Turn = Side < -0.1f ? -180.f : 180.f;
		if (FindHopTarget(Target, WhyNot))
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: hang: jump back: turns round and hops %.0f cm to a ledge on %s (top %+.0f cm), %.2f s"),
				*GetNameSafe(Character), FVector::Dist2D(Character->GetActorLocation(), HangLocationFor(Target)),
				*GetNameSafe(Target.Actor), Target.LedgePoint.Z - HangLine.Origin.Z, HangHopSeconds);
			LastHangRefusal.Reset();
			return BeginHangTransfer(EHawkeyeParkourMove::HangHop, Target, HangHopSeconds, HopRise, Turn, false, FVector::ZeroVector);
		}
		// Nothing behind: she turns round and pushes off, a controlled drop that still catches what it passes.
		LastHangRefusal = WhyNot;
		UE_LOG(LogHawkeye, Log, TEXT("%s: hang: jump back: %s: turns round and drops off (a controlled drop)"), *GetNameSafe(Character),
			*WhyNot);
		const FVector N = HangLine.Normal;
		bHanging = false;
		EndHangState();
		StopClip(0.2f);
		RestoreRootMotionMode();
		Character->SetActorLocation(Character->GetActorLocation() + N * 15.f);
		Character->SetActorRotation(FRotator(0.f, N.Rotation().Yaw, 0.f));
		// Falling from still, so it counts as a controlled drop; then the push off the wall.
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = N * HangHopDropVelocity.X + FVector(0.f, 0.f, HangHopDropVelocity.Y);
		// No regrab cooldown: she faces away from her own ledge now (and ignores it until she lands), and the
		// late catch takes anything that comes into reach on the way down.
		DroppedLedgePoint = HangObstacle.LedgePoint;
		bIgnoreDroppedLedge = true;
		OnParkourFinished.Broadcast(EHawkeyeParkourMove::LedgeGrab);
		ArmLateCatch();
		return true;
	}
	return ClimbFromHang();
}

// --- Moves from one hang to another ----------------------------------------------------------------------

bool UParkourComponent::BeginHangTransfer(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Target, float Seconds, float Rise,
	float TurnDegrees, bool bArc, const FVector& Pivot)
{
	ACharacter* Character = GetCharacter();
	if (!Character || !bHanging)
	{
		return false;
	}
	const float StartYaw = Character->GetActorRotation().Yaw;
	bHanging = false;
	EndHangState();
	HangObstacle = Target;
	TransferTarget = Target;
	LastObstacle = Target;
	// The corner keeps the hang pose (the paused clip); a leap or a hop reaches for the edge with the catch.
	static const FHawkeyeParkourClip NoClip;
	if (!BeginMove(Move, HangLocationFor(Target), Rise, Seconds, Move == EHawkeyeParkourMove::HangCorner ? NoClip : CatchClip))
	{
		return false;
	}
	// BeginMove faced her to the new wall at once; a turning move turns over its length instead.
	bMoveTurns = !FMath::IsNearlyZero(TurnDegrees);
	MoveYawStart = StartYaw;
	MoveYawDelta = TurnDegrees;
	MoveYawApplied = 0.f;
	if (bMoveTurns)
	{
		Character->SetActorRotation(FRotator(0.f, StartYaw, 0.f));
	}
	bMoveArc = bArc;
	MovePivot = Pivot;
	return true;
}

FVector UParkourComponent::ComputeHangTransferLocation(float Alpha) const
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	const float Smooth = FMath::SmoothStep(0.f, 1.f, A);
	if (bMoveArc)
	{
		// Round the corner about it, through the open side.
		return HawkeyeHang::ArcPoint(MoveStart, MoveEnd, MovePivot, MoveYawDelta, Smooth);
	}
	FVector Location = FMath::Lerp(MoveStart, MoveEnd, Smooth);
	Location.Z += MoveRise * FMath::Sin(PI * A);
	return Location;
}

void UParkourComponent::ApplyMoveTurn(float Alpha)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	const float Turned = MoveYawDelta * FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Alpha, 0.f, 1.f));
	Character->SetActorRotation(FRotator(0.f, MoveYawStart + Turned, 0.f));
	// The camera comes round with her, so the stick still means along the ledge and the wall stays to one side.
	const float Step = Turned - MoveYawApplied;
	MoveYawApplied = Turned;
	if (AController* Controller = HawkeyeHang::LocalController(GetOwner()))
	{
		FRotator Control = Controller->GetControlRotation();
		Control.Yaw += Step;
		Controller->SetControlRotation(Control);
	}
}

// --- The hands ----------------------------------------------------------------------------------------------

void UParkourComponent::AdvanceHangHands(float DeltaSeconds, float Moved, float Sign)
{
	using namespace HawkeyeHang;
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	const float Here = GetHangAlong();
	if (!bHandRestKnown && HandsAlpha <= 0.f)
	{
		// The pose's own hands, read before the IK first takes them, so switching it on moves nothing.
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Fallback(HangBackFromEdge + 2.f, HangHandSpread, HangBelowLedge - HalfHeight + 4.f);
		HandRest[0] = FVector(Fallback.X, -Fallback.Y, Fallback.Z);
		HandRest[1] = Fallback;
		const USkeletalMeshComponent* Body = Character->GetMesh();
		if (Body && Body->GetSkeletalMeshAsset() && Body->GetAnimInstance() && Body->DoesSocketExist(TEXT("hand_l"))
			&& Body->DoesSocketExist(TEXT("hand_r")))
		{
			const FTransform Frame(Character->GetActorRotation(), Character->GetActorLocation());
			const FVector Left = Frame.InverseTransformPosition(Body->GetSocketLocation(TEXT("hand_l")));
			const FVector Right = Frame.InverseTransformPosition(Body->GetSocketLocation(TEXT("hand_r")));
			// Only a pose that has the hands up at the edge, apart, left on the left.
			if (Left.Z > 0.f && Right.Z > 0.f && Left.Y < Right.Y - 10.f)
			{
				HandRest[0] = Left;
				HandRest[1] = Right;
			}
		}
		HandAlong[0] = Here + HandRest[0].Y;
		HandAlong[1] = Here + HandRest[1].Y;
		bHandRestKnown = true;
	}
	// The IK takes the hands from her first shimmy in a hang and keeps them until she lets go.
	if (Moved > 0.f || HandsAlpha > 0.f)
	{
		HandsAlpha = FMath::Min(1.f, HandsAlpha + DeltaSeconds / HandsBlendSeconds);
	}
	const float Speed = DeltaSeconds > 0.f ? Moved / DeltaSeconds : 0.f;
	// Each step covers what the body travels in two, so the hands alternate: the planted one lags, the other reaches.
	const float Ahead = FMath::Min(HandStride, Speed * HandStepSeconds * 0.5f);
	if (SteppingHand != INDEX_NONE)
	{
		StepElapsed += DeltaSeconds;
		const float A = FMath::Clamp(StepElapsed / HandStepSeconds, 0.f, 1.f);
		HandAlong[SteppingHand] = FMath::Lerp(StepFrom, StepTo, FMath::SmoothStep(0.f, 1.f, A));
		SwayAlpha = SwayPeak * FMath::Sin(PI * A);
		if (A >= 1.f)
		{
			SteppingHand = INDEX_NONE;
		}
	}
	else
	{
		SwayAlpha = FMath::FInterpTo(SwayAlpha, 0.f, DeltaSeconds, 10.f);
		// The hand furthest from its place steps: moving, once it lags a step's worth (the leading hand first);
		// stopped, the hands settle back to the pose.
		const bool bMoving = Moved > 0.f && Sign != 0.f;
		int32 Worst = INDEX_NONE;
		float WorstLag = bMoving ? FMath::Max(Ahead, 2.f) : 2.f;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			// The leading hand is looked at first, so it wins a tie.
			const int32 Hand = bMoving && Sign > 0.f ? 1 - Pass : Pass;
			const float Lag = FMath::Abs(HandAlong[Hand] - (Here + HandRest[Hand].Y));
			if (Lag > WorstLag + 0.01f)
			{
				Worst = Hand;
				WorstLag = Lag;
			}
		}
		if (Worst != INDEX_NONE)
		{
			SteppingHand = Worst;
			StepElapsed = 0.f;
			StepFrom = HandAlong[Worst];
			// Where the body will be when the step lands, and a little past it.
			StepTo = Here + HandRest[Worst].Y + (bMoving ? Sign * (Speed * HandStepSeconds + Ahead) : 0.f);
			SwayDirection = HangLine.Along * (StepTo >= StepFrom ? 1.f : -1.f);
		}
	}
	// Never off the ledge's ends.
	for (float& Along : HandAlong)
	{
		Along = FMath::Clamp(Along, HangLine.MinAlong + 3.f, FMath::Max(HangLine.MinAlong + 3.f, HangLine.MaxAlong - 3.f));
	}
}

FVector2D UParkourComponent::GetHangHandOffsets() const
{
	const float Here = GetHangAlong();
	return FVector2D(HandAlong[0] - Here, HandAlong[1] - Here);
}

bool UParkourComponent::GetHangArmPose(FHawkeyeStrikePoseSample& InOutPose, FVector& InOutLeanDirection, float& InOutLeanAlpha) const
{
	using namespace HawkeyeHang;
	if (HandsAlpha <= 0.f || !bHandRestKnown)
	{
		return false;
	}
	const float Here = GetHangAlong();
	FVector Hands[2];
	FVector Elbows[2];
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		FVector Point = HandRest[Hand];
		Point.Y = HandAlong[Hand] - Here;
		if (Hand == SteppingHand)
		{
			const float Lift = FMath::Sin(PI * FMath::Clamp(StepElapsed / HandStepSeconds, 0.f, 1.f));
			Point.Z += StepLift * Lift;
			Point.X -= StepOut * Lift;
		}
		Hands[Hand] = Point;
		// The elbows out to the sides and back, under the hands.
		const float Out = Hand == 0 ? -1.f : 1.f;
		Elbows[Hand] = FVector(Point.X - 30.f, Point.Y + Out * 25.f, Point.Z - 30.f);
	}
	InOutPose.LeftAlpha = HandsAlpha;
	InOutPose.LeftHand = Hands[0];
	InOutPose.LeftElbow = Elbows[0];
	InOutPose.RightAlpha = HandsAlpha;
	InOutPose.RightHand = Hands[1];
	InOutPose.RightElbow = Elbows[1];
	if (bHanging && SwayAlpha > InOutLeanAlpha)
	{
		InOutLeanDirection = SwayDirection;
		InOutLeanAlpha = SwayAlpha;
	}
	return true;
}

// --- The camera ------------------------------------------------------------------------------------------------

float UParkourComponent::ComputeHangCameraYaw(float ControlYaw, const FVector& WallNormal, float MaxDegrees)
{
	const FVector N = WallNormal.GetSafeNormal2D();
	if (N.IsNearlyZero())
	{
		return ControlYaw;
	}
	// Looking straight at the wall is yaw of -N; the arm (behind the lens) then points out along N, into open air.
	const float WallYaw = (-N).Rotation().Yaw;
	const float Off = FRotator::NormalizeAxis(ControlYaw - WallYaw);
	return FRotator::NormalizeAxis(WallYaw + FMath::Clamp(Off, -MaxDegrees, MaxDegrees));
}

float UParkourComponent::ComputeHangSocketSide(float ControlYaw, const FVector& WallNormal, float CurrentSide)
{
	using namespace HawkeyeHang;
	const FVector Right = FRotationMatrix(FRotator(0.f, ControlYaw, 0.f)).GetUnitAxis(EAxis::Y);
	const float IntoWall = static_cast<float>(FVector::DotProduct(Right, -WallNormal.GetSafeNormal2D()));
	if (IntoWall > SideFlip)
	{
		return -1.f;
	}
	if (IntoWall < SideKeep)
	{
		return 1.f;
	}
	return CurrentSide < 0.f ? -1.f : 1.f;
}

void UParkourComponent::KeepHangCameraOffTheWall(float DeltaSeconds)
{
	AController* Controller = HawkeyeHang::LocalController(GetOwner());
	if (!Controller || !bHanging)
	{
		return;
	}
	FRotator Control = Controller->GetControlRotation();
	const float Want = ComputeHangCameraYaw(Control.Yaw, HangLine.Normal, HangCameraMaxYaw);
	const float Off = FRotator::NormalizeAxis(Want - Control.Yaw);
	if (FMath::Abs(Off) < 0.01f)
	{
		return;
	}
	// Eased, so a camera that was looking away when she caught the edge comes round rather than snapping.
	Control.Yaw += Off * FMath::Min(1.f, DeltaSeconds * HangCameraYawSpeed);
	Controller->SetControlRotation(Control);
}

void UParkourComponent::ApplyHangCamera(FHawkeyeCameraTargets& InOutTargets, float DeltaSeconds)
{
	using namespace HawkeyeHang;
	const bool bOn = bHanging || IsHangTransfer(ActiveMove);
	float Want = 1.f;
	if (bOn)
	{
		const AController* Controller = LocalController(GetOwner());
		const float Yaw = Controller ? Controller->GetControlRotation().Yaw : (GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.f);
		const FVector Wall = bHanging ? HangLine.Normal : TransferTarget.WallNormal;
		HangCameraSideTarget = ComputeHangSocketSide(Yaw, Wall, HangCameraSideTarget);
		Want = HangCameraSideTarget;
	}
	else
	{
		HangCameraSideTarget = 1.f;
	}
	HangCameraSide = FMath::FInterpConstantTo(HangCameraSide, Want, DeltaSeconds, 2.f / SideSwapSeconds);
	if (HangCameraSide < 1.f)
	{
		InOutTargets.SocketOffset.Y *= HangCameraSide;
	}
}
