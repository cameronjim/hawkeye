#include "Misc/AutomationTest.h"
#include "Traversal/TraversalPoseRules.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The traversal body pose's rules (claude-docs/gameplay-semantics.md, "Traversal body pose"): the spring, the lean
 * angle, the tilt clamp, the phases, the arrival and landing classes, and the shape of a pull. Pure maths, no world.
 */
namespace HawkeyeTraversalPoseTest
{
	static constexpr float Frame = 1.f / 60.f;

	/** A pull along +X and up at ElevationDegrees, at Speed, Frames frames long, from state State. The zip is Length long
	 * and Progress of the way along it (it only matters for the brace and the arrival class); bForward is the stick. */
	static FTraversalPoseOutput RunPull(FTraversalPoseState& State, const FHawkeyeTraversalPoseSettings& S, float Speed,
		float ElevationDegrees, int32 Frames, float YawDegrees = 0.f, float Length = 2000.f, float Progress = 0.1f,
		bool bForward = false)
	{
		FTraversalPoseOutput Out;
		const float El = FMath::DegreesToRadians(ElevationDegrees);
		const FQuat Facing = FRotator(0.f, YawDegrees, 0.f).Quaternion();
		const FVector Rope = Facing.RotateVector(FVector(FMath::Cos(El), 0.f, FMath::Sin(El)));
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			FTraversalPoseInput In;
			In.DeltaSeconds = Frame;
			In.bZipping = true;
			In.ZipLineSpeed = Speed;
			In.ZipLength = Length;
			In.ZipProgress = Progress;
			In.bForwardHeld = bForward;
			In.Velocity = Rope * Speed;
			In.RopeDirection = Rope;
			In.ActorRotation = Facing;
			Out = State.Step(In, S);
		}
		return Out;
	}

	/** Frames of free fall or standing after the zip, with the given state of the ground. */
	static FTraversalPoseOutput RunAfter(FTraversalPoseState& State, const FHawkeyeTraversalPoseSettings& S,
		bool bFalling, bool bGrounded, int32 Frames, const FVector& Velocity = FVector::ZeroVector)
	{
		FTraversalPoseOutput Out;
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			FTraversalPoseInput In;
			In.DeltaSeconds = Frame;
			In.bFalling = bFalling;
			In.bGrounded = bGrounded;
			In.Velocity = Velocity;
			Out = State.Step(In, S);
		}
		return Out;
	}

	/** The angle between two directions, degrees. */
	static float AngleBetween(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(static_cast<float>(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal())), -1.f, 1.f)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseSpring, "Hawkeye.Traversal.Pose.Spring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseSpring::RunTest(const FString& Parameters)
{
	// Exact: one big step and many small ones land on the same value (the answer never depends on the frame rate).
	float OneX = 1.f, OneV = 0.f;
	FTraversalPoseRules::SpringStep(OneX, OneV, 0.f, 0.1f, 0.1f);
	float ManyX = 1.f, ManyV = 0.f;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FTraversalPoseRules::SpringStep(ManyX, ManyV, 0.f, 0.1f, 0.01f);
	}
	TestEqual(TEXT("one 0.1 s step equals ten 0.01 s steps"), OneX, ManyX, 1e-4f);
	TestEqual(TEXT("the velocity agrees as well"), OneV, ManyV, 1e-3f);

	// From rest toward a goal it never overshoots, and it gets there.
	float X = 0.f, V = 0.f;
	float Highest = 0.f;
	for (int32 Index = 0; Index < 300; ++Index)
	{
		FTraversalPoseRules::SpringStep(X, V, 10.f, 0.1f, 1.f / 60.f);
		Highest = FMath::Max(Highest, X);
	}
	TestTrue(TEXT("never past the goal"), Highest <= 10.f + 1e-3f);
	TestEqual(TEXT("settled on the goal after 5 s"), X, 10.f, 1e-3f);

	// A shorter half-life closes faster.
	float FastX = 0.f, FastV = 0.f, SlowX = 0.f, SlowV = 0.f;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FTraversalPoseRules::SpringStep(FastX, FastV, 1.f, 0.05f, 1.f / 60.f);
		FTraversalPoseRules::SpringStep(SlowX, SlowV, 1.f, 0.2f, 1.f / 60.f);
	}
	TestTrue(TEXT("half-life 0.05 s is further along than 0.2 s after 0.1 s"), FastX > SlowX);

	// A push on the velocity swings out and settles back: the launch's jerk.
	float KickX = 0.f, KickV = 500.f, Peak = 0.f;
	for (int32 Index = 0; Index < 240; ++Index)
	{
		FTraversalPoseRules::SpringStep(KickX, KickV, 0.f, 0.1f, 1.f / 60.f);
		Peak = FMath::Max(Peak, KickX);
	}
	TestTrue(TEXT("a velocity kick of 500 deg/s swings the tilt out to about 13 degrees (v / (y e))"),
		Peak > 11.f && Peak < 15.f);
	TestEqual(TEXT("and it settles back to the goal"), KickX, 0.f, 1e-3f);

	// The engine's own critically damped smoothing (smoothing time = half-life / ln 2) is the same spring.
	float EngineX = 1.f, EngineV = 0.f, OurX = 1.f, OurV = 0.f;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		FMath::CriticallyDampedSmoothing(EngineX, EngineV, 0.f, 0.f, 1.f / 60.f, 0.15f / UE_LN2);
		FTraversalPoseRules::SpringStep(OurX, OurV, 0.f, 0.15f, 1.f / 60.f);
	}
	TestEqual(TEXT("agrees with FMath::CriticallyDampedSmoothing"), OurX, EngineX, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseLean, "Hawkeye.Traversal.Pose.Lean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseLean::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	TestEqual(TEXT("1 g sideways at strength 0.5 leans 22.5 degrees"),
		FTraversalPoseRules::LeanDegrees(980.f, 980.f, 0.5f), 22.5f, 0.01f);
	TestEqual(TEXT("no acceleration, no lean"), FTraversalPoseRules::LeanDegrees(0.f, 980.f, 0.5f), 0.f, 0.001f);
	TestEqual(TEXT("the zip's own 6000 cm/s^2 at 0.5 is about 40 degrees"),
		FTraversalPoseRules::LeanDegrees(6000.f, 980.f, 0.5f), 40.36f, 0.1f);
	TestTrue(TEXT("a harder pull leans further, never past atan's 90 times the scale"),
		FTraversalPoseRules::LeanDegrees(60000.f, 980.f, 0.5f) < 45.f &&
			FTraversalPoseRules::LeanDegrees(60000.f, 980.f, 0.5f) >
				FTraversalPoseRules::LeanDegrees(6000.f, 980.f, 0.5f));

	const FVector2D Forward = FTraversalPoseRules::AccelerationTilt(FVector(980.f, 0.f, 500.f), 980.f, 0.5f);
	TestEqual(TEXT("forward acceleration leans the head forward"), static_cast<float>(Forward.X), 22.5f, 0.01f);
	TestEqual(
		TEXT("and not sideways; vertical acceleration does not lean"), static_cast<float>(Forward.Y), 0.f, 0.001f);
	const FVector2D Side = FTraversalPoseRules::AccelerationTilt(FVector(0.f, -980.f, 0.f), 980.f, 0.5f);
	TestEqual(TEXT("acceleration to the left leans the head left"), static_cast<float>(Side.Y), -22.5f, 0.01f);

	const FVector Up45(FMath::Cos(PI / 4.f), 0.f, FMath::Sin(PI / 4.f));
	const FVector2D Rope = FTraversalPoseRules::RopeTilt(Up45, 0.5f);
	TestEqual(TEXT("a rope 45 up and ahead tips the body 0.5 x 45 = 22.5 toward it"), static_cast<float>(Rope.X), 22.5f,
		0.01f);
	TestEqual(TEXT("a rope straight up tips it nowhere"),
		static_cast<float>(FTraversalPoseRules::RopeTilt(FVector::UpVector, 0.5f).Size()), 0.f, 0.001f);
	const FVector2D Horizontal = FTraversalPoseRules::RopeTilt(FVector(0.f, 1.f, 0.f), 0.5f);
	TestEqual(TEXT("a level rope to the right tips it 45 to the right"), static_cast<float>(Horizontal.Y), 45.f, 0.01f);

	const FVector2D Big = FTraversalPoseRules::ClampTilt(FVector2D(60.f, 80.f), 55.f);
	TestEqual(TEXT("the tilt is clamped to 55 degrees long"), static_cast<float>(Big.Size()), 55.f, 0.01f);
	TestEqual(TEXT("without turning it"), static_cast<float>(Big.Y / Big.X), 80.f / 60.f, 0.001f);
	const FVector2D Small = FTraversalPoseRules::ClampTilt(FVector2D(10.f, 5.f), 55.f);
	TestEqual(TEXT("a tilt inside the limit is untouched"), static_cast<float>(Small.X), 10.f, 0.001f);

	// What the clamp does to a whole pull: an extreme acceleration and a steep rope still stay inside 55.
	FHawkeyeTraversalPoseSettings S;
	FTraversalPoseState State;
	FTraversalPoseInput In;
	In.DeltaSeconds = Frame;
	In.bZipping = true;
	In.ZipLineSpeed = 1800.f;
	In.RopeDirection = FVector(1.f, 0.f, 0.f);
	float MostTilt = 0.f;
	for (int32 Index = 0; Index < 120; ++Index)
	{
		In.Velocity = FVector(600.f + 6000.f * Index * Frame * 6.f, 0.f, 0.f);
		MostTilt = FMath::Max(MostTilt, static_cast<float>(State.Step(In, S).Tilt.Size()));
	}
	TestTrue(TEXT("the body never tips past MaxTiltDegrees"), MostTilt <= S.MaxTiltDegrees + 0.5f);
	TestTrue(TEXT("a hard enough pull does reach it"), MostTilt > S.MaxTiltDegrees * 0.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseQuats, "Hawkeye.Traversal.Pose.Quats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseQuats::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FVector LeanedUp = FTraversalPoseRules::QuatFromTilt(30.f, 0.f).RotateVector(FVector::UpVector);
	TestEqual(TEXT("a forward tilt of 30 leans up 30 degrees toward +X"), AngleBetween(LeanedUp, FVector::UpVector),
		30.f, 0.01f);
	TestTrue(TEXT("toward +X"), LeanedUp.X > 0.49 && FMath::Abs(LeanedUp.Y) < 1e-4);
	const FVector Right = FTraversalPoseRules::QuatFromTilt(0.f, 20.f).RotateVector(FVector::UpVector);
	TestTrue(TEXT("a tilt to the right leans toward +Y"), Right.Y > 0.33 && FMath::Abs(Right.X) < 1e-4);
	TestTrue(TEXT("no tilt, no turn"), FTraversalPoseRules::QuatFromTilt(0.f, 0.f).Equals(FQuat::Identity));
	const FVector Diagonal = FTraversalPoseRules::QuatFromTilt(30.f, 30.f).RotateVector(FVector::UpVector);
	TestEqual(TEXT("a diagonal tilt leans by its length"), AngleBetween(Diagonal, FVector::UpVector), 42.43f, 0.05f);

	// A leg swung back by 30 degrees about the right axis carries the foot behind her.
	const FQuat Hip = FTraversalPoseRules::ScaledTurn(FVector::RightVector, 30.f, 1.f);
	const FVector Foot = Hip.RotateVector(FVector(0.f, 0.f, -1.f));
	TestTrue(TEXT("a positive hip swing puts the foot behind (-X) and still below"), Foot.X < -0.49 && Foot.Z < -0.8);
	TestTrue(TEXT("a weight of 0 is no turn"),
		FTraversalPoseRules::ScaledTurn(FVector::RightVector, 30.f, 0.f).Equals(FQuat::Identity));
	TestTrue(TEXT("half the weight is half the angle"),
		FMath::Abs(FTraversalPoseRules::ScaledTurn(FVector::RightVector, 30.f, 0.5f).GetAngle() -
			FMath::DegreesToRadians(15.f)) < 1e-4);

	// Into the component's axes: with the mesh turned -90 about up from the actor (the mannequin's), the same turn of
	// the world is the same turn in world space, whatever axes it is written in.
	const FQuat Actor = FRotator(0.f, 40.f, 0.f).Quaternion();
	const FQuat Component = Actor * FRotator(0.f, -90.f, 0.f).Quaternion();
	const FQuat Local = FTraversalPoseRules::QuatFromTilt(25.f, 10.f);
	const FQuat InComponent = FTraversalPoseRules::ActorToComponent(Local, Actor, Component);
	const FVector UpInComponent = FVector::UpVector;
	const FVector WorldA = Component.RotateVector(InComponent.RotateVector(UpInComponent));
	const FVector WorldB = Actor.RotateVector(Local.RotateVector(FVector::UpVector));
	TestTrue(TEXT("the actor's turn and its component-space form tip up the same way in the world"),
		WorldA.Equals(WorldB, 1e-4));
	TestTrue(TEXT("with the component on the actor's own axes it is unchanged"),
		FTraversalPoseRules::ActorToComponent(Local, Actor, Actor).Equals(Local, 1e-5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPosePhases, "Hawkeye.Traversal.Pose.Phases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPosePhases::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FHawkeyeTraversalPoseSettings S;
	auto Is = [this](const TCHAR* What, ETraversalPhase Got, ETraversalPhase Want)
	{ TestEqual(What, static_cast<int32>(Got), static_cast<int32>(Want)); };

	// A ground launch: hop, line, redirect, line, arrival.
	FTraversalPoseState State;
	FTraversalPoseInput In;
	In.DeltaSeconds = Frame;
	Is(TEXT("nothing happening: none"), State.Step(In, S).Phase, ETraversalPhase::None);

	In.bZipping = true;
	In.bHopping = true;
	In.HopFraction = 0.3f;
	Is(TEXT("zipping and hopping: launch"), State.Step(In, S).Phase, ETraversalPhase::Launch);
	In.bHopping = false;
	In.ZipLineSpeed = 600.f;
	In.ZipLength = 2000.f;
	In.ZipProgress = 0.2f;
	In.Velocity = FVector(600.f, 0.f, 0.f);
	Is(TEXT("on the line: pull"), State.Step(In, S).Phase, ETraversalPhase::Pull);
	In.bRedirecting = true;
	Is(TEXT("a chain's turn: redirect"), State.Step(In, S).Phase, ETraversalPhase::Redirect);
	In.bRedirecting = false;
	Is(TEXT("the turn done: pull again"), State.Step(In, S).Phase, ETraversalPhase::Pull);
	// The last 0.3 s: 400 cm left at 1800 cm/s is 0.22 s, so it is in the brace, and 1000 cm left (0.56 s) is not.
	In.ZipLineSpeed = 1800.f;
	In.Velocity = FVector(1800.f, 0.f, 0.f);
	In.ZipProgress = 0.8f;
	Is(TEXT("0.22 s from arrival: the brace"), State.Step(In, S).Phase, ETraversalPhase::Brace);
	In.ZipProgress = 0.5f;
	Is(TEXT("a pull that is not near the end"), State.Step(In, S).Phase, ETraversalPhase::Pull);
	In.ZipProgress = 0.95f;
	Is(TEXT("100 cm from the end: brace"), State.Step(In, S).Phase, ETraversalPhase::Brace);

	// Arriving on the roof: grounded, not falling.
	In.bZipping = false;
	In.bGrounded = true;
	In.ZipLineSpeed = 0.f;
	In.Velocity = FVector(600.f, 0.f, 0.f);
	Is(TEXT("the zip ends on the floor: arrive"), State.Step(In, S).Phase, ETraversalPhase::Arrive);
	TestEqual(TEXT("a long line to a roof, forward not held: the braking two-step"),
		static_cast<int32>(State.GetArrival()), static_cast<int32>(EArrivalKind::Brake));
	for (int32 Index = 0; Index < 50; ++Index)
	{
		State.Step(In, S);
	}
	Is(TEXT("after the arrival's own time: none"), State.GetPhase(), ETraversalPhase::None);

	// Letting go: release, then fall, then the ground.
	FTraversalPoseState Let;
	FTraversalPoseInput Line;
	Line.DeltaSeconds = Frame;
	Line.bZipping = true;
	Line.ZipLineSpeed = 1800.f;
	Line.ZipLength = 2000.f;
	Line.ZipProgress = 0.4f;
	Line.Velocity = FVector(1800.f, 0.f, 600.f);
	Let.Step(Line, S);
	Let.Step(Line, S);
	FTraversalPoseInput Drop;
	Drop.DeltaSeconds = Frame;
	Drop.bFalling = true;
	Drop.Velocity = Line.Velocity;
	Is(TEXT("the zip ends in the air: release"), Let.Step(Drop, S).Phase, ETraversalPhase::Release);
	for (int32 Index = 0; Index < FMath::CeilToInt(S.ReleaseSeconds / Frame) + 2; ++Index)
	{
		Let.Step(Drop, S);
	}
	Is(TEXT("after ReleaseSeconds: fall"), Let.GetPhase(), ETraversalPhase::Fall);
	Drop.bFalling = false;
	Drop.bGrounded = true;
	Is(TEXT("on the ground: the phase ends"), Let.Step(Drop, S).Phase, ETraversalPhase::None);

	// A hang caught out of a fall (neither falling nor grounded) also ends it.
	FTraversalPoseState Caught;
	Caught.Step(Line, S);
	Drop.bFalling = true;
	Drop.bGrounded = false;
	Caught.Step(Drop, S);
	Drop.bFalling = false;
	Is(TEXT("caught on a ledge: none"), Caught.Step(Drop, S).Phase, ETraversalPhase::None);

	// A plain jump, never on a zip, is the sample's.
	FTraversalPoseState Plain;
	FTraversalPoseInput Jump;
	Jump.DeltaSeconds = Frame;
	Jump.bFalling = true;
	Jump.Velocity = FVector(0.f, 0.f, -800.f);
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Plain.Step(Jump, S);
	}
	Is(TEXT("a fall that never was a zip is left to the sample"), Plain.GetPhase(), ETraversalPhase::None);
	TestFalse(TEXT("and draws nothing"), Plain.Step(Jump, S).IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseClasses, "Hawkeye.Traversal.Pose.Classes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseClasses::RunTest(const FString& Parameters)
{
	const FHawkeyeTraversalPoseSettings S;
	// By the zip's length, the slope of the line and the stick, never by line speed (it is the top speed by the end).
	auto Arrival = [this, &S](const TCHAR* What, float Length, float Elevation, bool bForward, EArrivalKind Want)
	{
		TestEqual(What, static_cast<int32>(FTraversalPoseRules::ClassifyArrival(Length, Elevation, bForward, S)),
			static_cast<int32>(Want));
	};
	Arrival(TEXT("a 2 m hop: a step-down"), 200.f, 5.f, true, EArrivalKind::StepDown);
	Arrival(TEXT("249 cm: still a step-down, whatever the slope and the stick"), 249.f, 30.f, false,
		EArrivalKind::StepDown);
	Arrival(TEXT("250 cm and over: the two-step"), 250.f, 30.f, false, EArrivalKind::Brake);
	Arrival(TEXT("a long level line without forward held: the two-step"), 1500.f, 3.f, false, EArrivalKind::Brake);
	Arrival(TEXT("a long level line with forward held: a slide out"), 1500.f, 3.f, true, EArrivalKind::SlideOut);
	Arrival(TEXT("a falling line, level enough: a slide out"), 1500.f, -10.f, true, EArrivalKind::SlideOut);
	Arrival(TEXT("a climb up to a roof's edge with forward held: the two-step"), 1500.f, 35.f, true,
		EArrivalKind::Brake);
	TestEqual(TEXT("time to arrival: 400 cm left at 1800 cm/s"), FTraversalPoseRules::TimeToArrival(0.8f, 2000.f, 1800.f),
		0.222f, 0.001f);
	TestTrue(TEXT("no speed, no arrival"), FTraversalPoseRules::TimeToArrival(0.5f, 2000.f, 0.f) > 100.f);
	TestTrue(TEXT("a slide takes longer than a step"),
		FTraversalPoseRules::ArrivalSeconds(EArrivalKind::SlideOut, S) >
			FTraversalPoseRules::ArrivalSeconds(EArrivalKind::StepDown, S));
	TestEqual(TEXT("no arrival, no time"), FTraversalPoseRules::ArrivalSeconds(EArrivalKind::None, S), 0.f);

	auto Land = [this, &S](const TCHAR* What, float Impact, float Ground, ELandingClip Want, bool bRoll = false,
					bool bStumble = false)
	{
		TestEqual(What, static_cast<int32>(FTraversalPoseRules::ClassifyLanding(Impact, Ground, bRoll, bStumble, S)),
			static_cast<int32>(Want));
	};
	Land(TEXT("a hop (200 cm/s) is not worth a landing"), 200.f, 500.f, ELandingClip::None);
	Land(TEXT("350 cm/s standing: a light stand landing"), 350.f, 0.f, ELandingClip::StandLight);
	Land(TEXT("350 walking at 150: light walk"), 350.f, 150.f, ELandingClip::WalkLight);
	Land(TEXT("500 running at 450: light run"), 500.f, 450.f, ELandingClip::RunLight);
	Land(TEXT("500 sprinting at 700: light sprint"), 500.f, 700.f, ELandingClip::SprintLight);
	Land(TEXT("700 cm/s is heavy"), 700.f, 450.f, ELandingClip::RunHeavy);
	Land(TEXT("900 standing: heavy stand"), 900.f, 0.f, ELandingClip::StandHeavy);
	Land(TEXT("900 sprinting: heavy sprint"), 900.f, 700.f, ELandingClip::SprintHeavy);
	// What the landing rules chose from the fall height is kept whatever the speed says.
	Land(TEXT("a roll is a roll"), 900.f, 450.f, ELandingClip::Roll, /*bRoll=*/true);
	Land(TEXT("a roll that came in slowly is still a roll"), 100.f, 450.f, ELandingClip::Roll, true);
	Land(TEXT("a stumble is a stumble"), 900.f, 0.f, ELandingClip::Stumble, false, /*bStumble=*/true);
	// A heavy landing's clip plays quicker (its crouch held ~0.6 s); a light one, a roll and a stumble play as made.
	TestEqual(TEXT("heavy stand rate"), FTraversalPoseRules::LandingClipRate(ELandingClip::StandHeavy, S), S.HeavyLandingRate);
	TestEqual(TEXT("heavy sprint rate"), FTraversalPoseRules::LandingClipRate(ELandingClip::SprintHeavy, S), S.HeavyLandingRate);
	TestEqual(TEXT("light run rate"), FTraversalPoseRules::LandingClipRate(ELandingClip::RunLight, S), 1.f);
	TestEqual(TEXT("roll rate"), FTraversalPoseRules::LandingClipRate(ELandingClip::Roll, S), 1.f);
	TestTrue(TEXT("a heavy landing is quicker than its clip"), S.HeavyLandingRate > 1.f);

	// Today's thresholds in speed: the roll height (400 cm) is a fall of 885 cm/s, so every roll is above the heavy
	// band.
	const float RollSpeed = FTraversalPoseRules::ImpactSpeedFromHeight(400.f, S.Gravity);
	TestEqual(TEXT("a 400 cm fall hits at 885 cm/s"), RollSpeed, 885.4f, 0.5f);
	TestTrue(TEXT("a fall too short to roll (300 cm) is a heavy landing, not a roll"),
		FTraversalPoseRules::ImpactSpeedFromHeight(300.f, S.Gravity) >= S.HeavyImpactSpeed);
	TestTrue(TEXT("a 100 cm drop is a light landing"),
		FTraversalPoseRules::ImpactSpeedFromHeight(100.f, S.Gravity) >= S.LightImpactSpeed &&
			FTraversalPoseRules::ImpactSpeedFromHeight(100.f, S.Gravity) < S.HeavyImpactSpeed);
	TestTrue(
		TEXT("a 30 cm step is none"), FTraversalPoseRules::ImpactSpeedFromHeight(30.f, S.Gravity) < S.LightImpactSpeed);

	TestEqual(TEXT("the dip envelope peaks at 1 at the attack time"), FTraversalPoseRules::DipEnvelope(0.1f, 0.1f), 1.f,
		1e-4f);
	TestEqual(TEXT("it is 0 before it starts"), FTraversalPoseRules::DipEnvelope(0.f, 0.1f), 0.f, 1e-6f);
	TestTrue(TEXT("and mostly gone half a second on"), FTraversalPoseRules::DipEnvelope(0.5f, 0.1f) < 0.1f);
	TestEqual(TEXT("a pulse is 1 at its centre"), FTraversalPoseRules::Pulse(0.3f, 0.3f, 0.08f), 1.f, 1e-5f);
	TestTrue(TEXT("and small a width away"), FTraversalPoseRules::Pulse(0.46f, 0.3f, 0.08f) < 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPosePull, "Hawkeye.Traversal.Pose.Pull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPosePull::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FHawkeyeTraversalPoseSettings S;

	// Steady at 1800 cm/s on a rope 30 degrees up: tipped toward the rope, legs trailing, both hands on, head up.
	FTraversalPoseState State;
	const FTraversalPoseOutput Out = RunPull(State, S, 1800.f, 30.f, 180);
	TestEqual(TEXT("fully in"), Out.Weight, 1.f, 0.01f);
	TestEqual(TEXT("tipped forward by 0.5 x (90 - 30) = 30 degrees"), static_cast<float>(Out.Tilt.X), 30.f, 0.5f);
	TestEqual(TEXT("not sideways"), static_cast<float>(Out.Tilt.Y), 0.f, 0.1f);
	TestTrue(TEXT("both arms on"), Out.LeftHandAlpha > 0.95f && Out.RightHandAlpha > 0.95f);

	// The rope hand is toward the anchor from the shoulder: ahead and up.
	TestTrue(TEXT("the rope hand is above and ahead of the pelvis"), Out.LeftHand.Z > 40.f && Out.LeftHand.X > 10.f);
	TestTrue(TEXT("and the free hand is back of it, out on the right"),
		Out.RightHand.X < Out.LeftHand.X - 30.f && Out.RightHand.Y > 20.f);
	TestTrue(TEXT("the rope hand is on the left"), Out.LeftHand.Y < 0.f);

	// The legs trail: the foot ends up behind her.
	const FVector FootR = Out.ThighR.RotateVector(FVector(0.f, 0.f, -1.f));
	const FVector FootL = Out.ThighL.RotateVector(FVector(0.f, 0.f, -1.f));
	TestTrue(TEXT("the trailing leg's foot is behind"), FootR.X < -0.3);
	TestTrue(TEXT("the leading leg trails less"), FootL.X > FootR.X);
	TestTrue(TEXT("both knees bent"),
		Out.CalfL.GetAngle() > FMath::DegreesToRadians(5.f) && Out.CalfR.GetAngle() > FMath::DegreesToRadians(15.f));

	// Total of the pelvis and spine shares is the whole tip.
	float Total = static_cast<float>(Out.Pelvis.GetAngle()) + static_cast<float>(Out.Spine[0].GetAngle()) +
		static_cast<float>(Out.Spine[1].GetAngle()) + static_cast<float>(Out.Spine[2].GetAngle());
	TestEqual(
		TEXT("pelvis and the three spine shares add to the whole tip"), FMath::RadiansToDegrees(Total), 30.f, 0.6f);
	TestTrue(TEXT("the lower spine takes more than the upper"),
		Out.Spine[0].GetAngle() > Out.Spine[1].GetAngle() && Out.Spine[1].GetAngle() > Out.Spine[2].GetAngle());
	TestTrue(TEXT("the neck takes most of the tip back (head level)"), Out.Neck.GetAngle() > 0.f);

	// The pose is on the actor's frame: turning the actor changes nothing in it.
	FTraversalPoseState Turned;
	const FTraversalPoseOutput TurnedOut = RunPull(Turned, S, 1800.f, 30.f, 180, 70.f);
	TestEqual(TEXT("facing another way gives the same tilt in the actor's frame"), static_cast<float>(TurnedOut.Tilt.X),
		static_cast<float>(Out.Tilt.X), 0.1f);

	// Slower is a gentler pose: less trail.
	FTraversalPoseState Slow;
	const FTraversalPoseOutput SlowOut = RunPull(Slow, S, 600.f, 30.f, 180);
	TestTrue(TEXT("at a third of the speed the legs trail less"), SlowOut.ThighR.GetAngle() < Out.ThighR.GetAngle());

	// Level and downward ropes tip less than steep ones? A level rope tips more (it is further from vertical).
	FTraversalPoseState Level;
	const FTraversalPoseOutput LevelOut = RunPull(Level, S, 1800.f, 0.f, 180);
	TestTrue(TEXT("a level rope tips the body further than a steep one"), LevelOut.Tilt.X > Out.Tilt.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseMotion, "Hawkeye.Traversal.Pose.Motion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseMotion::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FHawkeyeTraversalPoseSettings S;

	// Launch: the crouch peaks inside the hop and lets go into the line.
	FTraversalPoseState State;
	FTraversalPoseInput In;
	In.DeltaSeconds = Frame;
	In.bZipping = true;
	In.bHopping = true;
	In.RopeDirection = FVector(0.6f, 0.f, 0.8f);
	float MostSink = 0.f, SinkAtEnd = 0.f;
	const int32 HopFrames = 9; // 0.15 s
	for (int32 Index = 0; Index < HopFrames; ++Index)
	{
		In.HopFraction = static_cast<float>(Index + 1) / HopFrames;
		In.Velocity = FVector(0.f, 0.f, 600.f * (1.f - In.HopFraction));
		const FTraversalPoseOutput Out = State.Step(In, S);
		MostSink = FMath::Max(MostSink, static_cast<float>(-Out.PelvisOffset.Z));
		SinkAtEnd = -static_cast<float>(Out.PelvisOffset.Z);
		TestEqual(TEXT("the hop is the launch phase"), static_cast<int32>(Out.Phase),
			static_cast<int32>(ETraversalPhase::Launch));
	}
	TestTrue(TEXT("the pelvis dips in the hop (crouch-load)"), MostSink > 2.f);
	TestTrue(TEXT("the reach is on at the end of the hop"), State.GetChannel(FTraversalPoseState::LeftArm) > 0.4f);

	// The hand-over to the line kicks the tilt forward, which then settles to where the pull wants it.
	In.bHopping = false;
	In.ZipLineSpeed = 600.f;
	float PeakTilt = 0.f;
	float EndTilt = 0.f;
	for (int32 Index = 0; Index < 120; ++Index)
	{
		In.ZipLineSpeed = FMath::Min(1800.f, 600.f + 6000.f * Index * Frame);
		In.Velocity = In.RopeDirection * In.ZipLineSpeed;
		const FTraversalPoseOutput Out = State.Step(In, S);
		PeakTilt = FMath::Max(PeakTilt, static_cast<float>(Out.Tilt.X));
		EndTilt = static_cast<float>(Out.Tilt.X);
	}
	TestTrue(TEXT("the tilt jerks past where it ends"), PeakTilt > EndTilt + 3.f);
	TestEqual(TEXT("and settles on the rope's tip (0.5 x 36.9 degrees from vertical)"), EndTilt, 0.5f * 36.87f, 1.f);

	// A chain: the direction swings 90 degrees over 0.1 s; the body swings through (a side tilt that comes and goes).
	FTraversalPoseState Chain;
	FTraversalPoseOutput Out = RunPull(Chain, S, 1800.f, 20.f, 60);
	const float BeforeSide = static_cast<float>(Out.Tilt.Y);
	float MostSide = 0.f;
	FTraversalPoseInput Turn;
	Turn.DeltaSeconds = Frame;
	Turn.bZipping = true;
	Turn.bRedirecting = true;
	Turn.ZipLineSpeed = 1800.f;
	Turn.RopeDirection = FVector(0.f, 1.f, 0.35f).GetSafeNormal();
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const float Alpha = static_cast<float>(Index + 1) / 6.f;
		const FVector Direction =
			FMath::Lerp(FVector(1.f, 0.f, 0.36f), FVector(0.f, 1.f, 0.35f), FMath::Min(1.f, Alpha)).GetSafeNormal();
		Turn.Velocity = Direction * 1800.f;
		Turn.bRedirecting = Index < 6;
		Out = Chain.Step(Turn, S);
		MostSide = FMath::Max(MostSide, FMath::Abs(static_cast<float>(Out.Tilt.Y)));
	}
	TestEqual(TEXT("before the chain there is no side tilt"), BeforeSide, 0.f, 0.5f);
	TestTrue(TEXT("through the chain the body swings out sideways (> 8 degrees)"), MostSide > 8.f);
	TestTrue(TEXT("and the redirect phase was entered"),
		Chain.GetPhase() == ETraversalPhase::Pull || Chain.GetPhase() == ETraversalPhase::Redirect);

	// The pose fades out after the pull is over, and a teleport clears it at once.
	FTraversalPoseState Fade;
	RunPull(Fade, S, 1800.f, 30.f, 120);
	FTraversalPoseOutput After =
		RunAfter(Fade, S, /*bFalling=*/false, /*bGrounded=*/true, 150, FVector(600.f, 0.f, 0.f));
	TestFalse(TEXT("two and a half seconds after an arrival nothing is left"), After.IsActive());
	TestTrue(
		TEXT("and every channel is at rest"), FMath::Abs(Fade.GetChannel(FTraversalPoseState::TiltForward)) < 1e-3f);

	FTraversalPoseState Reset;
	RunPull(Reset, S, 1800.f, 30.f, 60);
	FTraversalPoseInput Teleport;
	Teleport.DeltaSeconds = Frame;
	Teleport.bTeleported = true;
	Teleport.bGrounded = true;
	TestFalse(TEXT("a teleport drops the pose on the spot"), Reset.Step(Teleport, S).IsActive());
	TestEqual(
		TEXT("with no zip in memory"), static_cast<int32>(Reset.GetPhase()), static_cast<int32>(ETraversalPhase::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseArrive, "Hawkeye.Traversal.Pose.Arrive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseArrive::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FHawkeyeTraversalPoseSettings S;
	// The deepest the pelvis goes in an arrival of each kind, and the time it takes to be gone. The pull runs up to the
	// brace, and the last frames are in it.
	auto Arrive = [&S](float Length, float Elevation, bool bForward, float& OutSink, float& OutSeconds,
					  EArrivalKind& OutKind)
	{
		FTraversalPoseState State;
		RunPull(State, S, 1800.f, Elevation, 60, 0.f, Length, 0.1f, bForward);
		RunPull(State, S, 1800.f, Elevation, 12, 0.f, Length, 0.97f, bForward);
		OutSink = 0.f;
		OutSeconds = 0.f;
		for (int32 Index = 0; Index < 240; ++Index)
		{
			FTraversalPoseInput In;
			In.DeltaSeconds = Frame;
			In.bGrounded = true;
			In.Velocity = FVector(600.f, 0.f, 0.f);
			const FTraversalPoseOutput Out = State.Step(In, S);
			if (Index == 0)
			{
				OutKind = State.GetArrival();
			}
			OutSink = FMath::Max(OutSink, static_cast<float>(-Out.PelvisOffset.Z));
			if (Out.IsActive())
			{
				OutSeconds = (Index + 1) * Frame;
			}
		}
	};
	float StepSink, StepSeconds, BrakeSink, BrakeSeconds, SlideSink, SlideSeconds;
	EArrivalKind StepKind = EArrivalKind::None, BrakeKind = EArrivalKind::None, SlideKind = EArrivalKind::None;
	Arrive(200.f, 10.f, false, StepSink, StepSeconds, StepKind);
	Arrive(1500.f, 40.f, false, BrakeSink, BrakeSeconds, BrakeKind);
	Arrive(1500.f, 4.f, true, SlideSink, SlideSeconds, SlideKind);
	TestEqual(TEXT("a 2 m zip arrives as a step-down"), static_cast<int32>(StepKind),
		static_cast<int32>(EArrivalKind::StepDown));
	TestEqual(TEXT("a long climb arrives braking"), static_cast<int32>(BrakeKind), static_cast<int32>(EArrivalKind::Brake));
	TestEqual(TEXT("a long level line with forward held arrives sliding"), static_cast<int32>(SlideKind),
		static_cast<int32>(EArrivalKind::SlideOut));
	TestTrue(TEXT("the pelvis goes lower the harder the arrival"), StepSink < BrakeSink && BrakeSink < SlideSink);
	TestTrue(TEXT("a step-down is a few centimetres (3 to 9)"), StepSink > 3.f && StepSink < 9.f);
	TestTrue(TEXT("a slide out is deep (over 15 cm)"), SlideSink > 15.f);
	TestTrue(TEXT("and the poses last longer the harder the arrival"),
		StepSeconds < BrakeSeconds && BrakeSeconds < SlideSeconds);
	TestTrue(TEXT("every arrival is over inside two seconds"), SlideSeconds < 2.f);

	// A landing from a fall: heavier hits sink further; a clip leaves the pose to the clip.
	FTraversalPoseState Light, Heavy, Clip;
	Light.BeginLanding(ELandingClip::RunLight, 450.f, false, S);
	Heavy.BeginLanding(ELandingClip::RunHeavy, 1400.f, false, S);
	Clip.BeginLanding(ELandingClip::RunHeavy, 1400.f, true, S);
	float LightSink = 0.f, HeavySink = 0.f, ClipSink = 0.f;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		FTraversalPoseInput In;
		In.DeltaSeconds = Frame;
		In.bGrounded = true;
		LightSink = FMath::Max(LightSink, static_cast<float>(-Light.Step(In, S).PelvisOffset.Z));
		HeavySink = FMath::Max(HeavySink, static_cast<float>(-Heavy.Step(In, S).PelvisOffset.Z));
		ClipSink = FMath::Max(ClipSink, static_cast<float>(-Clip.Step(In, S).PelvisOffset.Z));
	}
	TestTrue(TEXT("a heavy landing sinks further than a light one"), HeavySink > LightSink);
	TestTrue(TEXT("a light landing still sinks"), LightSink > 2.f);
	TestEqual(TEXT("with a clip playing the pose adds nothing"), ClipSink, 0.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseLoadBrace, "Hawkeye.Traversal.Pose.LoadAndBrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseLoadBrace::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseTest;
	const FHawkeyeTraversalPoseSettings S;

	// The crouch-load: the arrow in the air with her on the ground. It builds over LoadSeconds, with no movement of hers.
	FTraversalPoseState State;
	FTraversalPoseInput Fly;
	Fly.DeltaSeconds = Frame;
	Fly.bGrounded = true;
	Fly.bArrowInFlight = true;
	FTraversalPoseOutput Out = State.Step(Fly, S);
	TestEqual(TEXT("the arrow in the air on the ground: the load phase"), static_cast<int32>(Out.Phase),
		static_cast<int32>(ETraversalPhase::Load));
	for (int32 Index = 0; Index < 9; ++Index) // 0.15 s
	{
		Out = State.Step(Fly, S);
	}
	const float SinkEarly = -static_cast<float>(Out.PelvisOffset.Z);
	for (int32 Index = 0; Index < 27; ++Index) // 0.6 s in
	{
		Out = State.Step(Fly, S);
	}
	const float SinkFull = -static_cast<float>(Out.PelvisOffset.Z);
	TestTrue(TEXT("half way through the load the crouch is partly in"), SinkEarly > 1.f && SinkEarly < SinkFull);
	TestEqual(TEXT("after LoadSeconds it is the whole crouch"), SinkFull, S.LaunchSinkCm, 1.5f);
	TestTrue(TEXT("with the knees bent"), State.GetChannel(FTraversalPoseState::CalfL) > S.LaunchKnee * 0.8f);
	TestEqual(TEXT("and the rope hand not yet reaching"), Out.LeftHandAlpha, 0.f, 0.01f);

	// An arrow that never lands lets the crouch go.
	Fly.bArrowInFlight = false;
	for (int32 Index = 0; Index < 90; ++Index)
	{
		Out = State.Step(Fly, S);
	}
	TestFalse(TEXT("the arrow gone, the crouch is gone"), Out.IsActive());

	// The load carries into the hop: it lets go over the hop where a cold hop would be making its own crouch.
	FTraversalPoseState Loaded, Cold;
	Fly.bArrowInFlight = true;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		Loaded.Step(Fly, S);
	}
	FTraversalPoseInput Hop;
	Hop.DeltaSeconds = Frame;
	Hop.bZipping = true;
	Hop.bHopping = true;
	Hop.RopeDirection = FVector(0.6f, 0.f, 0.8f);
	float LoadedStart = 0.f, ColdStart = 0.f, LoadedEnd = 0.f;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Hop.HopFraction = static_cast<float>(Index + 1) / 9.f;
		const FTraversalPoseOutput A = Loaded.Step(Hop, S);
		const FTraversalPoseOutput B = Cold.Step(Hop, S);
		if (Index == 0)
		{
			LoadedStart = -static_cast<float>(A.PelvisOffset.Z);
			ColdStart = -static_cast<float>(B.PelvisOffset.Z);
		}
		LoadedEnd = -static_cast<float>(A.PelvisOffset.Z);
	}
	TestTrue(TEXT("a loaded hop starts down in the crouch"), LoadedStart > ColdStart + 4.f);
	TestTrue(TEXT("and comes up out of it by the end of the hop"), LoadedEnd < LoadedStart * 0.75f);

	// In the air a fired arrow is no load (a chain, a fall).
	FTraversalPoseState Air;
	FTraversalPoseInput Falling = Fly;
	Falling.bGrounded = false;
	Falling.bFalling = true;
	TestEqual(TEXT("an arrow fired in the air is not a load"), static_cast<int32>(Air.Step(Falling, S).Phase),
		static_cast<int32>(ETraversalPhase::None));

	// The brace: the legs go forward and up, the chest upright, the free hand in front; short zips have none.
	FTraversalPoseState Pull;
	RunPull(Pull, S, 1800.f, 25.f, 90, 0.f, 2000.f, 0.3f);
	const float TiltInPull = Pull.GetChannel(FTraversalPoseState::TiltForward);
	TestTrue(TEXT("in the pull the legs trail (hip back)"), Pull.GetChannel(FTraversalPoseState::ThighR) > 5.f);
	const FTraversalPoseOutput Braced = RunPull(Pull, S, 1800.f, 25.f, 24, 0.f, 2000.f, 0.97f); // 0.4 s of brace
	TestEqual(TEXT("the brace phase"), static_cast<int32>(Braced.Phase), static_cast<int32>(ETraversalPhase::Brace));
	TestTrue(TEXT("the legs have swung forward (hips flexed past the body)"),
		Pull.GetChannel(FTraversalPoseState::ThighL) < -S.BraceHipFlexLead * 0.8f &&
			Pull.GetChannel(FTraversalPoseState::ThighR) < -S.BraceHipFlexTrail * 0.8f);
	TestTrue(TEXT("with the knees bent for it"), Pull.GetChannel(FTraversalPoseState::CalfL) > S.BraceKnee * 0.7f);
	TestTrue(TEXT("the chest has come up (the tip under a third of the pull's)"),
		Pull.GetChannel(FTraversalPoseState::TiltForward) < TiltInPull * 0.34f);
	TestTrue(TEXT("the free hand reached forward of the pelvis"), Braced.RightHand.X > 30.f);
	TestTrue(TEXT("the brace is fully in"), Braced.Braced > 0.9f);
	FTraversalPoseState Short;
	RunPull(Short, S, 1800.f, 25.f, 10, 0.f, 200.f, 0.8f);
	TestEqual(TEXT("a zip shorter than 2.5 m has no brace"), static_cast<int32>(Short.GetPhase()),
		static_cast<int32>(ETraversalPhase::Pull));

	// Forward held in the brace makes the arrival a slide out when the line is level, and not when it climbs.
	FTraversalPoseInput Down;
	Down.DeltaSeconds = Frame;
	Down.bGrounded = true;
	Down.Velocity = FVector(600.f, 0.f, 0.f);
	FTraversalPoseState Slide;
	RunPull(Slide, S, 1800.f, 3.f, 20, 0.f, 1500.f, 0.97f, /*bForward=*/true);
	Slide.Step(Down, S);
	TestEqual(TEXT("forward held in the brace on a level line: a slide out"), static_cast<int32>(Slide.GetArrival()),
		static_cast<int32>(EArrivalKind::SlideOut));
	FTraversalPoseState Climb;
	RunPull(Climb, S, 1800.f, 35.f, 20, 0.f, 1500.f, 0.97f, true);
	Climb.Step(Down, S);
	TestEqual(TEXT("a climb to a roof's edge with forward held: the two-step"), static_cast<int32>(Climb.GetArrival()),
		static_cast<int32>(EArrivalKind::Brake));
	return true;
}

#endif
