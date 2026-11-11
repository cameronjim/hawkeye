#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Traversal/GrappleAudit.h"
#include "Traversal/GrappleReachQuery.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FGrappleReachQuery's pure rules (claude-docs/tools-traversal-debugger.md): the verdict order, the view, the zip's
 * release point, roof spots, neighbours and chains, sidewalk sampling and the roofs the street cannot reach. The
 * world comes in through a fake FGrappleReachOracle, so none of these needs a map.
 */
namespace HawkeyeGrappleReachQueryTest
{
	/** A square footprint of Side cm with its corner at Corner, counter-clockwise in X east / Y south. */
	static FGrappleReachRoof Square(const FString& Osm, const FVector2D& Corner, double Side, float HeightM)
	{
		FGrappleReachRoof Roof;
		Roof.OsmId = Osm;
		Roof.ActorName = Osm;
		Roof.HeightM = HeightM;
		Roof.Ring = { Corner, Corner + FVector2D(Side, 0.0), Corner + FVector2D(Side, Side), Corner + FVector2D(0.0, Side) };
		Roof.UpdateBox();
		return Roof;
	}

	static FGrappleReachAnchor Anchor(const FVector& Marker, int32 Roof)
	{
		FGrappleReachAnchor Result;
		Result.Origin = Marker - FVector(0.f, 0.f, 20.f);
		Result.Marker = Marker;
		Result.Landing = Marker + FVector(60.f, 0.f, -20.f);
		Result.Roof = Roof;
		Result.Name = FString::Printf(TEXT("Anchor_%d"), Roof);
		return Result;
	}

	/** Sees everything and every zip is clear, except the anchors named in the two lists. */
	static FGrappleReachOracle Oracle(const TSet<FString>& Hidden = {}, const TSet<FString>& Blocked = {})
	{
		FGrappleReachOracle Result;
		Result.HasLineOfSight = [Hidden](const FVector&, const FGrappleReachAnchor& Target, FGrappleReachBlocker* OutBlocker)
		{
			if (Hidden.Contains(Target.Name))
			{
				if (OutBlocker)
				{
					OutBlocker->Name = TEXT("Wall");
					OutBlocker->Kind = TEXT("a building");
				}
				return false;
			}
			return true;
		};
		Result.IsZipClear = [Blocked](const FVector&, const FGrappleReachAnchor& Target, bool, FGrappleReachBlocker* OutBlocker)
		{
			if (Blocked.Contains(Target.Name))
			{
				if (OutBlocker)
				{
					OutBlocker->Name = TEXT("FireEscapeLanding_1");
					OutBlocker->Kind = TEXT("FireEscapeLanding");
				}
				return false;
			}
			return true;
		};
		return Result;
	}

	/** No arm: the lens is the capsule centre, the view turns straight at the target within the pitch limits. */
	static FGrappleLensModel Eye(float PitchMax = 75.f)
	{
		FGrappleLensModel Model;
		Model.PitchMin = -70.f;
		Model.PitchMax = PitchMax;
		return Model;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachSharedRulesTest, "Hawkeye.Grapple.Reach.SharesTheComponentsRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachSharedRulesTest::RunTest(const FString& Parameters)
{
	// The component's defaults and the query's come from one place; the component hands its own back unchanged.
	const UGrappleComponent* Component = GetDefault<UGrappleComponent>();
	const FGrappleReachRules FromComponent = Component->MakeReachRules();
	const FGrappleReachRules Defaults;
	TestEqual(TEXT("Range"), FromComponent.Range, Defaults.Range);
	TestEqual(TEXT("MinRange"), FromComponent.MinRange, Defaults.MinRange);
	TestEqual(TEXT("ConeDegrees"), FromComponent.ConeDegrees, Defaults.ConeDegrees);
	TestEqual(TEXT("SightTolerance"), FromComponent.SightTolerance, Defaults.SightTolerance);
	TestEqual(TEXT("ZipLaunchHeight"), FromComponent.ZipLaunchHeight, Defaults.ZipLaunchHeight);
	TestEqual(TEXT("ZipStartIgnoreRadius"), FromComponent.ZipStartIgnoreRadius, Defaults.ZipStartIgnoreRadius);
	TestEqual(TEXT("SupportRadius"), FromComponent.SupportRadius, Defaults.SupportRadius);

	// The component's picker rule is the query's.
	const TArray<FVector2f> Candidates = { FVector2f(4.f, 900.f), FVector2f(1.f, 2000.f), FVector2f(1.3f, 800.f), FVector2f(1.6f, 300.f) };
	TestEqual(TEXT("Component and query pick the same"), UGrappleComponent::ChoosePick(Candidates, 0.5f),
		FGrappleReachQuery::ChoosePick(Candidates, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachPickTest, "Hawkeye.Grapple.Reach.PickNearestMiddleThenNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachPickTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Nothing to pick"), FGrappleReachQuery::ChoosePick({}, 0.5f), int32(INDEX_NONE));
	// 1.0 degree is the middle-most; 1.3 is within the 0.5 tie and nearer to her; 1.6 is outside the tie.
	const TArray<FVector2f> Candidates = { FVector2f(4.f, 900.f), FVector2f(1.f, 2000.f), FVector2f(1.3f, 800.f), FVector2f(1.6f, 300.f) };
	TestEqual(TEXT("Within the tie the nearer wins"), FGrappleReachQuery::ChoosePick(Candidates, 0.5f), 2);
	TestEqual(TEXT("No tie window: the middle-most wins"), FGrappleReachQuery::ChoosePick(Candidates, 0.f), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachScreenTest, "Hawkeye.Grapple.Reach.OnScreenInsideTheMargin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachScreenTest::RunTest(const FString& Parameters)
{
	const FGrappleReachRules Rules;   // 90 degrees across, 16:9, 2 degrees of margin
	const FVector View = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	TestTrue(TEXT("Dead ahead"), FGrappleReachQuery::IsOnScreen(Rules, View, Forward, FVector(1000.f, 0.f, 0.f)));
	TestFalse(TEXT("Behind"), FGrappleReachQuery::IsOnScreen(Rules, View, Forward, FVector(-1000.f, 0.f, 0.f)));
	// 44 degrees across is inside the 45 degree half width but not its 2 degree margin.
	const FVector Edge = FRotator(0.f, 44.f, 0.f).Vector() * 1000.f;
	TestFalse(TEXT("Inside the edge, outside the margin"), FGrappleReachQuery::IsOnScreen(Rules, View, Forward, Edge));
	TestTrue(TEXT("40 degrees across is on"), FGrappleReachQuery::IsOnScreen(Rules, View, Forward, FRotator(0.f, 40.f, 0.f).Vector() * 1000.f));
	// The half height of a 16:9 screen with 90 across is about 29.4 degrees: 30 up is off it.
	TestFalse(TEXT("30 degrees up is off a 16:9 screen"), FGrappleReachQuery::IsOnScreen(Rules, View, Forward,
		FRotator(30.f, 0.f, 0.f).Vector() * 1000.f));
	TestEqual(TEXT("Angle off the view"), FGrappleReachQuery::AngleOffViewDegrees(View, Forward, FVector(1000.f, 1000.f, 0.f)), 45.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachReleaseTest, "Hawkeye.Grapple.Reach.StartReleasePoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachReleaseTest::RunTest(const FString& Parameters)
{
	FGrappleReachRules Rules;
	const FVector From(0.f, 0.f, 90.f);
	const FVector Launch = FGrappleReachQuery::ComputeZipLaunch(Rules, From, true);
	TestEqual(TEXT("From the ground the line starts ZipLaunchHeight up"), Launch, From + FVector(0.f, 0.f, Rules.ZipLaunchHeight));
	TestEqual(TEXT("Mid-air it starts where she is"), FGrappleReachQuery::ComputeZipLaunch(Rules, From, false), From);

	const FVector End = Launch + FVector(2000.f, 0.f, 0.f);
	const float Release = Rules.GetStartReleaseDistance();
	const FVector Point = FGrappleReachQuery::ComputeStartReleasePoint(From, Launch, End, Release);
	TestEqual(TEXT("The release point is the release distance from where she stood"), static_cast<float>(FVector::Dist(Point, From)),
		Release, 0.1f);
	TestTrue(TEXT("It is on the line, past the launch"), Point.X > Launch.X && FMath::IsNearlyEqual(Point.Z, Launch.Z));

	const FVector ShortEnd = Launch + FVector(100.f, 0.f, 0.f);
	TestEqual(TEXT("A line shorter than the release distance releases at its end"),
		FGrappleReachQuery::ComputeStartReleasePoint(From, Launch, ShortEnd, Release), ShortEnd);
	TestEqual(TEXT("The zip ends with the capsule standing on the landing"), FGrappleReachQuery::ComputeZipEnd(Rules, FVector::ZeroVector),
		FVector(0.f, 0.f, Rules.CapsuleHalfHeight + GrappleReachDefaults::StandClearance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachVerdictTest, "Hawkeye.Grapple.Reach.VerdictOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachVerdictTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	const FGrappleReachRules Rules;
	const FGrappleReachSpot Street{ FVector(0.f, 0.f, 90.f), INDEX_NONE };
	TArray<FGrappleReachAnchor> Anchors;
	Anchors.Add(Anchor(FVector(1500.f, 0.f, 900.f), 0));    // 0 reachable
	Anchors.Add(Anchor(FVector(3000.f, 0.f, 900.f), 1));    // 1 too far
	Anchors.Add(Anchor(FVector(200.f, 0.f, 90.f), 2));      // 2 too close
	Anchors.Add(Anchor(FVector(1500.f, 500.f, 900.f), 3));  // 3 hidden
	Anchors.Add(Anchor(FVector(1500.f, -500.f, 900.f), 4)); // 4 zip blocked
	Anchors.Add(Anchor(FVector(1500.f, 0.f, 900.f), 5));    // 5 disabled
	Anchors[5].bEnabled = false;
	Anchors.Add(Anchor(FVector(300.f, 0.f, 2000.f), 6));    // 6 nearly straight up
	const FGrappleReachOracle Fake = Oracle({ TEXT("Anchor_3") }, { TEXT("Anchor_4") });

	const FGrappleLensModel Free = Eye(75.f);
	auto Verdict = [&](int32 Index, const FGrappleLensModel& Model, const FGrappleReachSpot& Spot)
	{
		return LexToString(FGrappleReachQuery::Evaluate(Rules, Model, Fake, Spot, Anchors, Index).Verdict);
	};
	TestEqual(TEXT("In range, in view, seen and clear"), Verdict(0, Free, Street), LexToString(EGrappleReachVerdict::Reachable));
	TestEqual(TEXT("Past Range"), Verdict(1, Free, Street), LexToString(EGrappleReachVerdict::TooFar));
	TestEqual(TEXT("Inside MinRange"), Verdict(2, Free, Street), LexToString(EGrappleReachVerdict::TooClose));
	TestEqual(TEXT("The sight line is blocked"), Verdict(3, Free, Street), LexToString(EGrappleReachVerdict::SightBlocked));
	TestEqual(TEXT("The zip line is blocked"), Verdict(4, Free, Street), LexToString(EGrappleReachVerdict::ZipBlocked));
	TestEqual(TEXT("Disabled comes before everything"), Verdict(5, Free, Street), LexToString(EGrappleReachVerdict::Disabled));
	TestEqual(TEXT("Steep but within the pitch limit"), Verdict(6, Free, Street), LexToString(EGrappleReachVerdict::Reachable));
	TestEqual(TEXT("Past the pitch limit it is out of the cone"), Verdict(6, Eye(40.f), Street),
		LexToString(EGrappleReachVerdict::TooSteep));
	const FGrappleReachSpot OnRoofZero{ FVector(1400.f, 0.f, 990.f), 0 };
	TestEqual(TEXT("Her own roof is not counted"), Verdict(0, Free, OnRoofZero), LexToString(EGrappleReachVerdict::SameRoof));

	const FGrappleReachEvaluation Hidden = FGrappleReachQuery::Evaluate(Rules, Free, Fake, Street, Anchors, 3);
	TestEqual(TEXT("The blocker is reported"), Hidden.Blocker.Name, FString(TEXT("Wall")));
	TestFalse(TEXT("Unseen: the zip is not swept unless asked"), Hidden.bClear);
	TestTrue(TEXT("Asked: the zip is swept anyway"),
		FGrappleReachQuery::Evaluate(Rules, Free, Fake, Street, Anchors, 3, /*bCheckZipWhenUnseen=*/true).bClear);

	TArray<FGrappleReachEvaluation> All;
	FGrappleReachQuery::EvaluateAll(Rules, Free, Fake, Street, Anchors, All);
	TestEqual(TEXT("One evaluation per anchor"), All.Num(), Anchors.Num());
	// Looking straight at anchor 0, the picker marks it over anchor 6 (far off the middle).
	const FVector Forward = (Anchors[0].Marker - Street.Centre).GetSafeNormal();
	TestEqual(TEXT("The picker marks the one in the middle"), FGrappleReachQuery::PickFrom(Rules, Anchors, All, Street.Centre, Forward), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachLensTest, "Hawkeye.Grapple.Reach.PredictLens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachLensTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	FGrappleLensModel Model = Eye(75.f);
	Model.Targets = [](float, float& OutArm, FVector& OutSocket, float& OutLift)
	{
		OutArm = 350.f;
		OutSocket = FVector(0.f, 70.f, 60.f);
		OutLift = 0.f;
	};
	FVector Lens, Forward;
	const FVector Centre(0.f, 0.f, 90.f);
	const FVector FarAhead = Centre + FVector(100000.f, 0.f, 0.f);
	FGrappleReachQuery::PredictLens(Model, Centre, FarAhead, nullptr, Lens, Forward);
	TestEqual(TEXT("The arm puts the lens behind her"), static_cast<float>(Lens.X), -350.f, 1.f);
	TestEqual(TEXT("and out over the right shoulder"), static_cast<float>(Lens.Y), 70.f, 1.f);
	TestEqual(TEXT("and up by the socket"), static_cast<float>(Lens.Z), 150.f, 1.f);
	TestTrue(TEXT("Looking at a far target the view is level"), Forward.Equals(FVector::ForwardVector, 0.01f));

	// The probe pulls the lens in: here it never lets it leave the pivot.
	const TFunction<FVector(const FVector&, const FVector&)> Probe = [](const FVector& Origin, const FVector&) { return Origin; };
	FGrappleReachQuery::PredictLens(Model, Centre, FarAhead, Probe, Lens, Forward);
	TestEqual(TEXT("A blocked arm keeps the lens at the pivot"), Lens, Centre);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachRoofSpotsTest, "Hawkeye.Grapple.Reach.RoofSpots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachRoofSpotsTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	const FGrappleAuditSettings Settings;
	TArray<FVector2D> Points;
	FGrappleReachQuery::ComputeRoofSpotPoints(Square(TEXT("a"), FVector2D(0.0, 0.0), 1000.0, 12.f).Ring, Settings, Points);
	TestEqual(TEXT("A 10 m square: its centre and the middle of each parapet"), Points.Num(), 5);
	if (Points.Num() == 5)
	{
		TestTrue(TEXT("The centre first"), Points[0].Equals(FVector2D(500.0, 500.0), 0.01));
		TestTrue(TEXT("Each parapet's middle, a metre in"), Points[1].Equals(FVector2D(500.0, 100.0), 0.01));
	}
	// Wound the other way, the inside is still found.
	TArray<FVector2D> Reversed = Square(TEXT("b"), FVector2D(0.0, 0.0), 1000.0, 12.f).Ring;
	Algo::Reverse(Reversed);
	FGrappleReachQuery::ComputeRoofSpotPoints(Reversed, Settings, Points);
	TestEqual(TEXT("Reversed winding too"), Points.Num(), 5);
	// Edges under RoofSpotEdge get no spot of their own.
	FGrappleReachQuery::ComputeRoofSpotPoints(Square(TEXT("c"), FVector2D(0.0, 0.0), 250.0, 12.f).Ring, Settings, Points);
	TestEqual(TEXT("A 2.5 m square: only its centre"), Points.Num(), 1);
	TestTrue(TEXT("Inside"), FGrappleReachQuery::IsInsideRing(FVector2D(10.0, 10.0), Reversed));
	TestFalse(TEXT("Outside"), FGrappleReachQuery::IsInsideRing(FVector2D(-10.0, 10.0), Reversed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachNeighboursTest, "Hawkeye.Grapple.Reach.NeighboursAndChains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachNeighboursTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	const FGrappleAuditSettings Settings;
	TArray<FGrappleReachRoof> Roofs;
	Roofs.Add(Square(TEXT("a"), FVector2D(0.0, 0.0), 1000.0, 12.f));        // 0
	Roofs.Add(Square(TEXT("b"), FVector2D(2000.0, 0.0), 1000.0, 12.f));     // 1: 10 m across a street from a
	Roofs.Add(Square(TEXT("c"), FVector2D(0.0, 1050.0), 1000.0, 13.f));     // 2: touching a, a metre up: walkable
	Roofs.Add(Square(TEXT("d"), FVector2D(0.0, 2100.0), 1000.0, 20.f));     // 3: touching c, 7 m up: not walkable
	Roofs.Add(Square(TEXT("e"), FVector2D(-2400.0, 0.0), 1000.0, 12.f));    // 4: 14 m west of a ...
	Roofs.Add(Square(TEXT("f"), FVector2D(-1250.0, -50.0), 100.0, 30.f));   // 5: ... with a tall stack on the line between
	TArray<TArray<int32>> Neighbours;
	TArray<TArray<int32>> Walkable;
	FGrappleReachQuery::FindNeighbours(Roofs, Settings, Neighbours, Walkable);
	TestTrue(TEXT("a and b face each other across the street"), Neighbours[0].Contains(1) && Neighbours[1].Contains(0));
	TestTrue(TEXT("a and c touch and are a step apart"), Walkable[0].Contains(2));
	TestFalse(TEXT("c and d touch but d is two storeys up"), Walkable[2].Contains(3));
	TestFalse(TEXT("A taller building between a and e hides one from the other"), Neighbours[0].Contains(4));

	// b reaches a by grapple; a walks to c. From b: a in 1 hop, c in 2; d never.
	TArray<TSet<int32>> ReachedFrom;
	ReachedFrom.SetNum(Roofs.Num());
	ReachedFrom[0].Add(1);
	TSet<int32> Visited;
	TestEqual(TEXT("b to a by one grapple"), FGrappleReachQuery::FindChainHops(1, 0, Walkable, ReachedFrom), 1);
	TestEqual(TEXT("b to c by a grapple and a walk"), FGrappleReachQuery::FindChainHops(1, 2, Walkable, ReachedFrom, &Visited), 2);
	TestEqual(TEXT("b to d: no chain"), FGrappleReachQuery::FindChainHops(1, 3, Walkable, ReachedFrom, &Visited), -1);
	TestTrue(TEXT("The roofs reached on the way are reported"), Visited.Contains(0) && Visited.Contains(2) && !Visited.Contains(3));

	const FGrappleReachRules Rules;
	TestTrue(TEXT("A spot on a is within range of b's walls"),
		FGrappleReachQuery::IsPairInRange(Rules, Settings, { FVector(500.f, 500.f, 1290.f) }, Roofs[1]));
	TestFalse(TEXT("A spot 40 m off is not"),
		FGrappleReachQuery::IsPairInRange(Rules, Settings, { FVector(-2000.f, 500.f, 1290.f) }, Roofs[1]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachSidewalkTest, "Hawkeye.Grapple.Reach.SidewalkSpots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachSidewalkTest::RunTest(const FString& Parameters)
{
	const FGrappleReachRules Rules;
	const FGrappleAuditSettings Settings;
	FGrappleReachStreet Street;
	Street.Points = { FVector2D(0.0, 0.0), FVector2D(3000.0, 0.0) };
	Street.WidthM = 12.f;
	const TArray<FGrappleReachStreet> Streets = { Street };
	const FBox2D Bounds(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0));

	TArray<FVector> Spots;
	FGrappleReachQuery::GatherSidewalkSpots(Streets, Bounds, 1000.f, Rules, Settings,
		[](const FVector2D& Point, FVector& OutFloor)
		{
			OutFloor = FVector(Point.X, Point.Y, 15.f);
			return FGrappleReachQuery::EStand::Stand;
		},
		Spots);
	TestEqual(TEXT("Every 10 m along 30 m, both sides"), Spots.Num(), 8);
	if (Spots.Num() > 0)
	{
		// 6 m of half carriageway plus 2 m into the sidewalk.
		TestEqual(TEXT("At the sidewalk's middle"), static_cast<float>(FMath::Abs(Spots[0].Y)), 800.f, 0.1f);
		TestEqual(TEXT("Standing on the floor"), static_cast<float>(Spots[0].Z),
			15.f + Rules.CapsuleHalfHeight + GrappleReachDefaults::StandClearance, 0.1f);
	}

	// A building over the sidewalk's middle: the spot backs off towards the centre line until it is off it.
	Spots.Reset();
	FGrappleReachQuery::GatherSidewalkSpots(Streets, Bounds, 1000.f, Rules, Settings,
		[](const FVector2D& Point, FVector& OutFloor)
		{
			OutFloor = FVector(Point.X, Point.Y, 15.f);
			return FMath::Abs(Point.Y) > 650.0 ? FGrappleReachQuery::EStand::Blocked : FGrappleReachQuery::EStand::Stand;
		},
		Spots);
	TestEqual(TEXT("Still one per sample"), Spots.Num(), 8);
	if (Spots.Num() > 0)
	{
		TestEqual(TEXT("Backed off 2 m"), static_cast<float>(FMath::Abs(Spots[0].Y)), 600.f, 0.1f);
	}

	// Off the map: nothing to stand on, no spot.
	Spots.Reset();
	FGrappleReachQuery::GatherSidewalkSpots(Streets, Bounds, 1000.f, Rules, Settings,
		[](const FVector2D&, FVector&) { return FGrappleReachQuery::EStand::NoGround; }, Spots);
	TestEqual(TEXT("No ground, no spots"), Spots.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachRoofsFromStreetTest, "Hawkeye.Grapple.Reach.RoofsTheStreetCannotReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachRoofsFromStreetTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	const FGrappleReachRules Rules;
	const FGrappleAuditSettings Settings;
	TArray<FGrappleReachRoof> Roofs;
	Roofs.Add(Square(TEXT("low"), FVector2D(0.0, 1000.0), 1000.0, 6.f));          // 0 under the anchored height
	Roofs.Add(Square(TEXT("bare"), FVector2D(1200.0, 1000.0), 1000.0, 12.f));     // 1 tall, no anchors
	Roofs.Add(Square(TEXT("far"), FVector2D(50000.0, 1000.0), 1000.0, 12.f));     // 2 nobody stands near it
	Roofs.Add(Square(TEXT("seen"), FVector2D(2400.0, 1000.0), 1000.0, 12.f));     // 3 reached
	Roofs.Add(Square(TEXT("hidden"), FVector2D(3600.0, 1000.0), 1000.0, 12.f));   // 4 in range, never reached
	TArray<FGrappleReachAnchor> Anchors;
	Anchors.Add(Anchor(FVector(50500.f, 1000.f, 1220.f), 2));
	Anchors.Add(Anchor(FVector(2900.f, 1000.f, 1220.f), 3));
	Anchors.Add(Anchor(FVector(4100.f, 1000.f, 1220.f), 4));
	const TArray<FVector> StreetSpots = { FVector(2000.f, 0.f, 105.f), FVector(4000.f, 0.f, 105.f) };

	TArray<EGrappleRoofReach> Reach;
	FGrappleReachQuery::ClassifyRoofsFromStreet(Rules, Eye(), Oracle({}, { TEXT("Anchor_4") }), Settings, Roofs, Anchors, StreetSpots,
		Reach);
	TestEqual(TEXT("One answer per roof"), Reach.Num(), Roofs.Num());
	if (Reach.Num() == Roofs.Num())
	{
		TestEqual(TEXT("Low roof"), LexToString(Reach[0]), LexToString(EGrappleRoofReach::BelowAnchorHeight));
		TestEqual(TEXT("Bare roof"), LexToString(Reach[1]), LexToString(EGrappleRoofReach::NoAnchors));
		TestEqual(TEXT("Far roof"), LexToString(Reach[2]), LexToString(EGrappleRoofReach::NoStreetInRange));
		TestEqual(TEXT("Seen roof"), LexToString(Reach[3]), LexToString(EGrappleRoofReach::Reached));
		TestEqual(TEXT("Street-facing but blocked"), LexToString(Reach[4]), LexToString(EGrappleRoofReach::Unreached));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachAuditReportTest, "Hawkeye.Grapple.Reach.AuditReportAndGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachAuditReportTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	// Two roofs across a gap from each other on one street; the second's only anchor has its zip blocked from everywhere.
	FGrappleReachScene Scene;
	Scene.Roofs.Add(Square(TEXT("seen"), FVector2D(500.0, 1000.0), 1000.0, 12.f));
	Scene.Roofs.Add(Square(TEXT("hidden"), FVector2D(2500.0, 1000.0), 1000.0, 12.f));
	Scene.RoofByOsm.Add(TEXT("seen"), 0);
	Scene.RoofByOsm.Add(TEXT("hidden"), 1);
	Scene.Anchors.Add(Anchor(FVector(1000.f, 1000.f, 1220.f), 0));
	Scene.Anchors.Add(Anchor(FVector(3000.f, 1000.f, 1220.f), 1));
	FGrappleReachStreet Street;
	Street.Points = { FVector2D(0.0, 0.0), FVector2D(4000.0, 0.0) };
	Scene.Streets.Add(Street);
	Scene.Bounds = FBox2D(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0));

	FGrappleAuditInputs Inputs;
	Inputs.Scene = &Scene;
	Inputs.Profile.Lens = Eye();
	Inputs.Oracle = Oracle({}, { TEXT("Anchor_1") });
	Inputs.SidewalkFloor = [](const FVector2D& Point, FVector& OutFloor)
	{
		OutFloor = FVector(Point.X, Point.Y, 15.f);
		return FGrappleReachQuery::EStand::Stand;
	};
	Inputs.RoofTop = [&Scene](const FVector2D& Point, int32 Roof, FVector& OutTop)
	{
		OutTop = FVector(Point.X, Point.Y, Scene.Roofs[Roof].HeightM * 100.f);
		return FGrappleReachQuery::IsInsideRing(Point, Scene.Roofs[Roof].Ring);
	};
	const FGrappleAuditReport Report = FGrappleAudit::Run(Inputs);

	TestEqual(TEXT("Both roofs are anchored rows"), Report.Roofs.Num(), 2);
	TestEqual(TEXT("One street-facing roof is unreached"), Report.StreetFacingUnreached.Num(), 1);
	if (Report.Roofs.Num() == 2 && Report.StreetFacingUnreached.Num() == 1)
	{
		TestEqual(TEXT("It is the blocked one"), Report.Roofs[Report.StreetFacingUnreached[0]].OsmId, FString(TEXT("hidden")));
		TestTrue(TEXT("The first is reached from the street"), Report.Roofs[0].FromStreet > 0);
		TestEqual(TEXT("and from its neighbour's roof"), Report.Roofs[0].NeighboursReaching, 1);
		TestTrue(TEXT("The reason is the zip"), Report.Roofs[1].Why.Contains(TEXT("zip blocked by FireEscapeLanding")));
	}
	TestEqual(TEXT("Allowed roofs pass the gate"), Report.FindGateFailures({ TEXT("hidden") }).Num(), 0);
	TestEqual(TEXT("Others fail it"), Report.FindGateFailures({}).Num(), 1);

	const FString Json = Report.ToJson();
	for (const TCHAR* Key : { TEXT("\"summary\""), TEXT("\"objectives\""), TEXT("\"picker_misses\""), TEXT("\"roofs_no_street\""),
			 TEXT("\"roofs_no_street_with_spot_in_range\""), TEXT("\"roofs_no_street_at_2_5_m\""), TEXT("\"roofs_no_roof\""),
			 TEXT("\"neighbour_pairs_missing\""), TEXT("\"anchors_never_reachable\""), TEXT("\"anchors_never_picked\""),
			 TEXT("\"street_facing_unreached\""), TEXT("\"roofs\"") })
	{
		TestTrue(FString::Printf(TEXT("The report has %s"), Key), Json.Contains(Key));
	}
	TestFalse(TEXT("Without a picker the picker is not checked"), Report.bPickerChecked);
	TestTrue(TEXT("and the summary says so"), Report.Summary.Contains(TEXT("picker not checked")));
	TestEqual(TEXT("One anchors row per anchor"), Report.AnchorRows.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachStreetWallsTest, "Hawkeye.Grapple.Reach.StreetWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachStreetWallsTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	// generate_city's _reach._street_edges, which the audit now shares: a 12 m street along Y = 0.
	const FGrappleAuditSettings Settings;
	FGrappleReachStreet Street;
	Street.Points = { FVector2D(-2000.0, 0.0), FVector2D(9000.0, 0.0) };
	const TArray<FGrappleReachStreet> Streets = { Street };
	TArray<FGrappleReachRoof> Roofs;
	Roofs.Add(Square(TEXT("front"), FVector2D(0.0, 1000.0), 1000.0, 12.f));        // 0 its south wall faces the street
	Roofs.Add(Square(TEXT("court"), FVector2D(0.0, 2100.0), 1000.0, 12.f));        // 1 behind front: covered and 21 m off
	Roofs.Add(Square(TEXT("shed"), FVector2D(2000.0, 700.0), 1000.0, 3.f));        // 2 low, in front of the next
	Roofs.Add(Square(TEXT("over_shed"), FVector2D(2000.0, 1750.0), 1000.0, 12.f)); // 3 9 m over a shed: still faces it
	Roofs.Add(Square(TEXT("tall"), FVector2D(4000.0, 700.0), 1000.0, 20.f));       // 4 a taller one in front of the next
	Roofs.Add(Square(TEXT("hidden"), FVector2D(4000.0, 1750.0), 1000.0, 12.f));    // 5 covered by it
	Roofs.Add(Square(TEXT("far"), FVector2D(6000.0, 2000.0), 1000.0, 12.f));       // 6 nothing in front, 14 m past the kerb

	TestEqual(TEXT("A wall on the street"), FGrappleReachQuery::CountStreetWalls(Roofs, 0, Streets, Settings), 1);
	TestEqual(TEXT("Behind a building as tall: none"), FGrappleReachQuery::CountStreetWalls(Roofs, 1, Streets, Settings), 0);
	TestEqual(TEXT("A low shed in front does not hide it"), FGrappleReachQuery::CountStreetWalls(Roofs, 3, Streets, Settings), 1);
	TestEqual(TEXT("A taller one in front does"), FGrappleReachQuery::CountStreetWalls(Roofs, 5, Streets, Settings), 0);
	TestEqual(TEXT("Too far past the kerb: none"), FGrappleReachQuery::CountStreetWalls(Roofs, 6, Streets, Settings), 0);
	TestEqual(TEXT("No such roof"), FGrappleReachQuery::CountStreetWalls(Roofs, 99, Streets, Settings), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachAuditOffTheStreetTest, "Hawkeye.Grapple.Reach.AuditRoofsOffTheStreet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachAuditOffTheStreetTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	// A roof inside its block, right behind a street-front roof of its height, with a sidewalk spot in range of its anchor
	// and nothing seeing it: not street-facing, so listed with how it is reached instead (walked onto), never failed.
	FGrappleReachScene Scene;
	Scene.Roofs.Add(Square(TEXT("front"), FVector2D(500.0, 1000.0), 1000.0, 12.f));
	Scene.Roofs.Add(Square(TEXT("court"), FVector2D(500.0, 2100.0), 1000.0, 12.f));
	Scene.RoofByOsm.Add(TEXT("front"), 0);
	Scene.RoofByOsm.Add(TEXT("court"), 1);
	Scene.Anchors.Add(Anchor(FVector(1000.f, 1000.f, 1220.f), 0));
	Scene.Anchors.Add(Anchor(FVector(1000.f, 2150.f, 1220.f), 1));
	FGrappleReachStreet Street;
	Street.Points = { FVector2D(-1000.0, 0.0), FVector2D(3000.0, 0.0) };
	Scene.Streets.Add(Street);
	Scene.Bounds = FBox2D(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0));

	FGrappleAuditInputs Inputs;
	Inputs.Scene = &Scene;
	Inputs.Profile.Lens = Eye();
	Inputs.Oracle = Oracle({ TEXT("Anchor_1") });
	Inputs.SidewalkFloor = [](const FVector2D& Point, FVector& OutFloor)
	{
		OutFloor = FVector(Point.X, Point.Y, 15.f);
		return FGrappleReachQuery::EStand::Stand;
	};
	Inputs.RoofTop = [&Scene](const FVector2D& Point, int32 Roof, FVector& OutTop)
	{
		OutTop = FVector(Point.X, Point.Y, Scene.Roofs[Roof].HeightM * 100.f);
		return FGrappleReachQuery::IsInsideRing(Point, Scene.Roofs[Roof].Ring);
	};
	const FGrappleAuditReport Report = FGrappleAudit::Run(Inputs);

	if (TestEqual(TEXT("Both roofs are anchored rows"), Report.Roofs.Num(), 2))
	{
		TestEqual(TEXT("The front one is reached"), LexToString(Report.Roofs[0].Reach), LexToString(EGrappleRoofReach::Reached));
		TestTrue(TEXT("The court one has a sidewalk spot in range"), Report.Roofs[1].StreetSpotsInRange + Report.Roofs[1].FineSpotsInRange > 0);
		TestEqual(TEXT("but no wall on a street"), Report.Roofs[1].StreetWalls, 0);
		TestEqual(TEXT("so it is classed so"), LexToString(Report.Roofs[1].Reach), LexToString(EGrappleRoofReach::NoStreetWall));
	}
	TestEqual(TEXT("Nothing street-facing is unreached"), Report.StreetFacingUnreached.Num(), 0);
	TestEqual(TEXT("The gate passes with no allow list"), Report.FindGateFailures({}).Num(), 0);
	if (TestEqual(TEXT("It is listed off the street"), Report.RoofsOffTheStreet.Num(), 1))
	{
		TestTrue(TEXT("with its class"), Report.RoofsOffTheStreet[0].Contains(TEXT("no wall on a street")));
		TestTrue(TEXT("and how it is reached instead"), Report.RoofsOffTheStreet[0].Contains(TEXT("walked onto from 1 roofs")));
	}
	TestTrue(TEXT("The JSON lists it"), Report.ToJson().Contains(TEXT("\"roofs_off_the_street\"")));
	TestTrue(TEXT("The summary counts it"), Report.Summary.Contains(TEXT("1 with no wall on a street")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleReachAuditChainTest, "Hawkeye.Grapple.Reach.AuditPickerAndStreetLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleReachAuditChainTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleReachQueryTest;
	// Two roofs 35 m apart on one street, too far for a grapple between them; the first has a fire escape.
	FGrappleReachScene Scene;
	Scene.Roofs.Add(Square(TEXT("near"), FVector2D(500.0, 1000.0), 1000.0, 12.f));
	Scene.Roofs.Add(Square(TEXT("far"), FVector2D(5000.0, 1000.0), 1000.0, 12.f));
	Scene.RoofByOsm.Add(TEXT("near"), 0);
	Scene.RoofByOsm.Add(TEXT("far"), 1);
	Scene.Anchors.Add(Anchor(FVector(1000.f, 1000.f, 1220.f), 0));
	Scene.Anchors.Add(Anchor(FVector(5500.f, 1000.f, 1220.f), 1));
	for (int32 Index = 0; Index < Scene.Anchors.Num(); ++Index)
	{
		Scene.Anchors[Index].Index = Index;
	}
	FGrappleReachStreet Street;
	Street.Points = { FVector2D(-1000.0, 0.0), FVector2D(7000.0, 0.0) };
	Scene.Streets.Add(Street);
	Scene.Bounds = FBox2D(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0));
	FCityFireEscapeRecord Escape;
	Escape.Transform.SetLocation(FVector(1500.0, 1500.0, 400.0));
	Scene.FireEscapes.Add(Escape);
	Scene.ObjectiveRoofs.Add(TEXT("first"), TEXT("near"));
	Scene.ObjectiveRoofs.Add(TEXT("second"), TEXT("far"));

	FGrappleAuditInputs Inputs;
	Inputs.Scene = &Scene;
	Inputs.Profile.Lens = Eye();
	Inputs.Oracle = Oracle();
	Inputs.SidewalkFloor = [](const FVector2D& Point, FVector& OutFloor)
	{
		OutFloor = FVector(Point.X, Point.Y, 15.f);
		return FGrappleReachQuery::EStand::Stand;
	};
	Inputs.RoofTop = [&Scene](const FVector2D& Point, int32 Roof, FVector& OutTop)
	{
		OutTop = FVector(Point.X, Point.Y, Scene.Roofs[Roof].HeightM * 100.f);
		return FGrappleReachQuery::IsInsideRing(Point, Scene.Roofs[Roof].Ring);
	};
	Inputs.ObjectiveChain = { TEXT("first"), TEXT("second") };

	// The second roof is reached from the street but not from the first: the chain breaks unless the data lets that
	// link go down the fire escape and back up from the street.
	const FGrappleAuditReport Strict = FGrappleAudit::Run(Inputs);
	TestTrue(TEXT("Both roofs are reached from the street"), Strict.Roofs.Num() == 2 && Strict.Roofs[0].FromStreet > 0 && Strict.Roofs[1].FromStreet > 0);
	TestTrue(TEXT("Without a street link the chain is broken"), Strict.bObjectiveChainBroken);
	TestTrue(TEXT("and says which link"), Strict.Errors.Num() == 1 && Strict.Errors[0].Contains(TEXT("cannot be reached from near")));

	Inputs.StreetLinkObjectives = { TEXT("second") };
	const FGrappleAuditReport Linked = FGrappleAudit::Run(Inputs);
	TestFalse(TEXT("With the street link the chain holds"), Linked.bObjectiveChainBroken);
	TestTrue(TEXT("by the fire escape"), Linked.Objectives.Num() == 2 && Linked.Objectives[1].Contains(TEXT("down a fire escape")));

	// A picker that never marks anything: every reachable pair is a miss, and every view disagrees with the full check.
	int32 PickerCalls = 0;
	Inputs.Picker = [&PickerCalls](const FGrappleReachSpot&, const FVector&, const FVector&, int32& OutGreyed, FString& OutReason)
	{
		++PickerCalls;
		OutGreyed = INDEX_NONE;
		OutReason = TEXT("blind");
		return int32(INDEX_NONE);
	};
	const FGrappleAuditReport Blind = FGrappleAudit::Run(Inputs);
	TestTrue(TEXT("The picker is checked"), Blind.bPickerChecked);
	TestTrue(TEXT("Some pairs are reachable"), Blind.ReachablePairs > 0);
	TestEqual(TEXT("Asked once dead on and four times aimed off per reachable pair"), int64(PickerCalls), Blind.ReachablePairs * 5);
	TestEqual(TEXT("Nothing is picked"), Blind.PickedPairs, int64(0));
	TestEqual(TEXT("Dead on, it disagrees with the full check every time"), Blind.PickerDisagreements, Blind.ReachablePairs);
	TestEqual(TEXT("Both anchors are reachable but never picked"), Blind.AnchorsNeverPicked.Num(), 2);
	TestTrue(TEXT("The reason is listed"), Blind.PickerMisses.Num() > 0 && Blind.PickerMisses[0].StartsWith(TEXT("nothing marked x")));
	TestTrue(TEXT("The summary counts the picks"), Blind.Summary.Contains(TEXT("0 of those picked")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
