#include "Combat/ArrowDefinition.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/CityLedgeData.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The ways down off a roof (claude-docs/gameplay-semantics.md, traversal): drop to hang from a
 * roof or fire-escape edge, the widened catch window and the ledge a drop lets go of, the
 * controlled drop's landing dip, the fire-escape landing geometry, and the grapple's launch
 * point, hop, start-ignore radius, level and downward lines and the zip cancel.
 */
namespace HawkeyeWaysDownTest
{
	static constexpr float HalfHeight = 88.f;

	static AHawkeyeTestBlocker* SpawnBox(const FHawkeyeTestWorld& TestWorld, const FVector& Min, const FVector& Max)
	{
		AHawkeyeTestBlocker* Box = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), (Min + Max) * 0.5f, FRotator::ZeroRotator));
		if (Box)
		{
			Box->SetExtent((Max - Min) * 0.5f);
		}
		return Box;
	}

	/** Kate standing with her feet at FeetZ on whatever is there, facing Yaw, Walking. */
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& FeetAt, float Yaw = 0.f)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FeetAt + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f)));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	static float Feet(const ACharacter* Kate)
	{
		return Kate->GetActorLocation().Z - HalfHeight;
	}

	/**
	 * A roof whose top is z = 0 ending at x = RoofEnd, a Height parapet ParapetDepth thick on its
	 * edge (none when Height is 0), and the street StreetDrop below.
	 */
	static void SpawnRoofEdge(const FHawkeyeTestWorld& TestWorld, float RoofEnd, float Height, float ParapetDepth, float StreetDrop)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(RoofEnd, 3000.f, 0.f));
		SpawnBox(TestWorld, FVector(RoofEnd, -3000.f, -StreetDrop - 100.f), FVector(3000.f, 3000.f, -StreetDrop));
		if (Height > 0.f)
		{
			SpawnBox(TestWorld, FVector(RoofEnd - ParapetDepth, -300.f, 0.f), FVector(RoofEnd, 300.f, Height));
		}
	}

	/** Runs the parkour component's own tick: timers, the catch, the auto trigger. */
	static void TickParkour(UParkourComponent* Parkour, float DeltaSeconds)
	{
		static_cast<UActorComponent*>(Parkour)->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}

	static void AdvanceMoveFor(UParkourComponent* Parkour, float Seconds)
	{
		for (float T = 0.f; T < Seconds; T += 0.05f)
		{
			Parkour->AdvanceMove(0.05f);
		}
	}

	/** A fire-escape landing record at (0, 0, Z) on a facade along x, +Y out of it. */
	static FCityFireEscapeRecord LandingRecord(float Z, int32 Floor)
	{
		FCityFireEscapeRecord Record;
		Record.Transform = FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, Z));
		Record.Floor = Floor;
		Record.LadderDrop = Floor > 1 ? 330.f : 0.f;
		Record.LadderSide = Floor % 2 == 0 ? 1.f : -1.f;
		Record.OsmId = TEXT("test");
		return Record;
	}

	static AFireEscapeLanding* SpawnLanding(const FHawkeyeTestWorld& TestWorld, const FCityFireEscapeRecord& Record)
	{
		AFireEscapeLanding* Landing = Cast<AFireEscapeLanding>(TestWorld.SpawnActor(
			AFireEscapeLanding::StaticClass(), Record.Transform.GetLocation(), Record.Transform.Rotator()));
		if (Landing)
		{
			Landing->ApplyRecord(Record);
		}
		return Landing;
	}

	/** A building whose street facade is the plane y = 0 (it fills y < 0), the street at z = 0. */
	static void SpawnFacadeAndStreet(const FHawkeyeTestWorld& TestWorld)
	{
		SpawnBox(TestWorld, FVector(-1000.f, -1000.f, 0.f), FVector(1000.f, 0.f, 1200.f));
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDropToHangDetection, "Hawkeye.Parkour.DropToHangDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDropToHangDetection::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	{
		// A 90 cm, 30 cm parapet whose inner face is 46 cm from the capsule, the street 300 below.
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 90.f, 30.f, 300.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
		if (!TestNotNull(TEXT("Kate has a parkour component"), Parkour))
		{
			return false;
		}
		TestEqual(TEXT("The edge must be within 60 cm"), Parkour->DropToHangReach, 60.f);
		TestEqual(TEXT("With more than 150 cm below"), Parkour->DropToHangMinDrop, 150.f);
		FHawkeyeParkourObstacle Edge;
		TestTrue(TEXT("A parapet 46 cm away with 300 cm beyond is an edge to hang from"), Parkour->FindDropEdge(FVector::ForwardVector, Edge));
		TestEqual(TEXT("Over a 90 cm lip"), Edge.Height, 90.f, 3.f);
		TestEqual(TEXT("46 cm from the capsule"), Edge.Distance, 46.f, 5.f);
		TestTrue(TEXT("The hang face points out, away from her"), Edge.WallNormal.Equals(FVector::ForwardVector, 0.01f));
		TestEqual(TEXT("The ledge is the parapet's outer top edge"), static_cast<float>(Edge.LedgePoint.X), 110.f, 1.f);
		TestEqual(TEXT("300 cm down beyond"), Edge.LandingDrop, 300.f, 5.f);
		TestFalse(TEXT("Nothing behind her"), Parkour->FindDropEdge(-FVector::ForwardVector, Edge));
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 140.f, 90.f, 30.f, 300.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		FHawkeyeParkourObstacle Edge;
		TestFalse(TEXT("A parapet 76 cm away is out of reach"), Kate->GetParkourComponent()->FindDropEdge(FVector::ForwardVector, Edge));
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 90.f, 30.f, 140.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		FHawkeyeParkourObstacle Edge;
		TestFalse(TEXT("140 cm beyond is not a drop worth hanging from"), Kate->GetParkourComponent()->FindDropEdge(FVector::ForwardVector, Edge));
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 80.f, 0.f, 0.f, 300.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		FHawkeyeParkourObstacle Edge;
		TestTrue(TEXT("A walkable edge 46 cm away with no lip counts"), Kate->GetParkourComponent()->FindDropEdge(FVector::ForwardVector, Edge));
		TestEqual(TEXT("The ledge is the floor's own edge"), Edge.Height, 0.f, 3.f);
		TestEqual(TEXT("At x = 80"), static_cast<float>(Edge.LedgePoint.X), 80.f, 1.f);
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 200.f, 30.f, 300.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		FHawkeyeParkourObstacle Edge;
		TestFalse(TEXT("A 200 cm wall is not a lip"), Kate->GetParkourComponent()->FindDropEdge(FVector::ForwardVector, Edge));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDropToHangMove, "Hawkeye.Parkour.DropToHangMoveAndTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDropToHangMove::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 90.f, 30.f, 600.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		TestTrue(TEXT("TryDropToHang starts the move"), Parkour->TryDropToHang(FVector::ForwardVector, TEXT("test")));
		TestEqual(TEXT("Drop to hang"), Parkour->GetActiveMove(), EHawkeyeParkourMove::DropToHang);
		TestTrue(TEXT("Input locked"), Parkour->IsLockingInput());
		AdvanceMoveFor(Parkour, Parkour->DropToHangSeconds + 0.05f);
		TestTrue(TEXT("Hanging after 0.5 s"), Parkour->IsHanging());
		TestEqual(TEXT("Feet 145 cm under the parapet top, like a catch"), Feet(Kate), 90.f - 145.f, 1.f);
		TestEqual(TEXT("36 cm out from the outer face"), static_cast<float>(Kate->GetActorLocation().X), 146.f, 1.f);
		TestEqual(TEXT("Turned to face the wall"), FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw))), 180.f, 1.f);
		Kate->Jump();
		TestEqual(TEXT("Jump from that hang climbs back up"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Climb);
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 90.f, 30.f, 600.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector, 180.f);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->Jump();
		TestEqual(TEXT("The jump key with the edge behind her drops to the hang"), Parkour->GetActiveMove(), EHawkeyeParkourMove::DropToHang);
		AdvanceMoveFor(Parkour, 0.6f);
		TestTrue(TEXT("Hanging"), Parkour->IsHanging());
	}
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld, 110.f, 90.f, 30.f, 600.f);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		Kate->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		TestFalse(TEXT("Not in the air"), Kate->GetParkourComponent()->TryDropToHang(FVector::ForwardVector, TEXT("test")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCatchWindow, "Hawkeye.Parkour.CatchWindowWidened",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCatchWindow::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	const UParkourComponent* Defaults = GetDefault<UParkourComponent>();
	TestEqual(TEXT("Catch from 150 cm above the feet"), Defaults->CatchMinHeight, 150.f);
	TestEqual(TEXT("Up to 330 cm, one floor"), Defaults->CatchMaxHeight, 330.f);
	struct FCase
	{
		float BelowTop;
		bool bCatch;
		const TCHAR* What;
	};
	const FCase Cases[] = {
		{ 300.f, true, TEXT("A ledge 300 cm above the feet is caught (the old window stopped at 260)") },
		{ 325.f, true, TEXT("325 cm: caught") },
		{ 345.f, false, TEXT("345 cm: out of reach") },
		{ 140.f, false, TEXT("140 cm: too low to catch") },
	};
	for (const FCase& Case : Cases)
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnBox(TestWorld, FVector(100.f, -150.f, 0.f), FVector(400.f, 150.f, 800.f));
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(30.f, 0.f, 800.f - Case.BelowTop));
		Kate->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Kate->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -300.f);
		TestEqual(Case.What, Kate->GetParkourComponent()->TryCatchLedge(), Case.bCatch);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFireEscapeChain, "Hawkeye.Parkour.FireEscapeHangDropCatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFireEscapeChain::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnFacadeAndStreet(TestWorld);
	AFireEscapeLanding* Lower = SpawnLanding(TestWorld, LandingRecord(330.f, 1));
	AFireEscapeLanding* Upper = SpawnLanding(TestWorld, LandingRecord(660.f, 2));
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 50.f, 662.f), 90.f);
	if (!Lower || !Upper || !Kate)
	{
		AddError(TEXT("Could not spawn the fire escape."));
		return false;
	}
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();

	FHawkeyeParkourObstacle Edge;
	TestTrue(TEXT("Standing on a landing, the rail is an edge to hang from"), Parkour->FindDropEdge(FVector::RightVector, Edge));
	TestEqual(TEXT("A 90 cm rail"), Edge.Height, 90.f, 3.f);
	TestTrue(TEXT("The street is far below beyond it"), Edge.LandingDrop > 600.f);
	TestTrue(TEXT("Crouch toward it (TryDropToHang) goes over"), Parkour->TryDropToHang(FVector::RightVector, TEXT("test")));
	AdvanceMoveFor(Parkour, 0.6f);
	TestTrue(TEXT("Hanging from the rail"), Parkour->IsHanging());
	TestEqual(TEXT("Feet 145 cm under the rail top"), Feet(Kate), 750.f - 145.f, 1.f);

	TestTrue(TEXT("Crouch drops"), Parkour->DropFromHang());
	TestTrue(TEXT("The rail she let go of is ignored"), Parkour->IsIgnoringDroppedLedge());
	// Part way down, the rail she left is in the catch window: not caught again.
	Kate->SetActorLocation(FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, 750.f - 200.f + HalfHeight));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, -500.f);
	TickParkour(Parkour, 0.7f);
	TestFalse(TEXT("Past the regrab time, the same rail is still not caught"), Parkour->IsBusy());
	// One floor down: the lower landing's rail comes into the window and is caught.
	Kate->SetActorLocation(FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, 420.f - 160.f + HalfHeight));
	Movement->Velocity = FVector(0.f, 0.f, -500.f);
	TickParkour(Parkour, 1.f / 60.f);
	TestEqual(TEXT("The next landing's rail is caught"), Parkour->GetActiveMove(), EHawkeyeParkourMove::LedgeGrab);
	AdvanceMoveFor(Parkour, 0.2f);
	TestTrue(TEXT("Hanging from the lower landing"), Parkour->IsHanging());
	TestEqual(TEXT("Feet 145 cm under its rail"), Feet(Kate), 420.f - 145.f, 1.f);
	TestFalse(TEXT("A catch clears the ignored ledge"), Parkour->IsIgnoringDroppedLedge());
	TestTrue(TEXT("From that hang the last drop to the street is under the 400 cm roll height"), Feet(Kate) < 400.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeControlledDropDip, "Hawkeye.Movement.ControlledDropLandingDip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeControlledDropDip::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();

	Movement->Velocity = FVector::ZeroVector;
	Movement->SetMovementMode(MOVE_Falling);
	TestTrue(TEXT("Walking off an edge is a controlled drop"), Kate->IsControlledDrop());
	Kate->TestApplyLanding(250.f);
	TestTrue(TEXT("Landing 250 cm down from it dips"), Kate->IsRecoveringFromLanding());
	TestFalse(TEXT("Landing ends it"), Kate->IsControlledDrop());

	const FHawkeyeTestWorld JumpWorld;
	AHawkeyeAimTestCharacter* Jumper = SpawnKate(JumpWorld, FVector(0.f, 0.f, 1000.f));
	Jumper->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, 420.f);
	Jumper->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("A jump is not a controlled drop"), Jumper->IsControlledDrop());
	Jumper->TestApplyLanding(250.f);
	TestFalse(TEXT("So 250 cm after a jump does not dip (only falls over 400 do)"), Jumper->IsRecoveringFromLanding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFireEscapeGeometry, "Hawkeye.FireEscape.LandingGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFireEscapeGeometry::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	FCityFireEscapeRecord Record;
	Record.Transform = FTransform(FRotator(0.f, 90.f, 0.f), FVector(1000.f, 2000.f, 660.f));
	Record.LadderDrop = 330.f;
	Record.LadderSide = 1.f;
	TestEqual(TEXT("240 x 90 x 8 cm slab"), Record.SlabSize, FVector(240.f, 90.f, 8.f));
	TestEqual(TEXT("5 cm off the facade"), Record.FacadeGap, 5.f);
	TestEqual(TEXT("90 cm rails"), Record.RailHeight, 90.f);

	TArray<FBox> Boxes;
	AFireEscapeLanding::BuildCollisionBoxes(Record, Boxes);
	if (!TestEqual(TEXT("Slab and three rails"), Boxes.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("Slab: 240 along, 5 to 95 out, 8 thick under the landing height"),
		Boxes[0].Equals(FBox(FVector(-120.f, 5.f, -8.f), FVector(120.f, 95.f, 0.f)), 0.01f));
	TestTrue(TEXT("Outer rail along the far edge, 90 cm up"),
		Boxes[1].Equals(FBox(FVector(-120.f, 89.f, 0.f), FVector(120.f, 95.f, 90.f)), 0.01f));
	TestTrue(TEXT("End rails on both short sides"), Boxes[2].Min.X == -120.f && Boxes[3].Max.X == 120.f
		&& Boxes[2].Min.Y == 5.f && Boxes[3].Max.Y == 95.f);

	TArray<FTransform> Cubes, Cylinders;
	AFireEscapeLanding::BuildParts(Record, Cubes, Cylinders);
	TestEqual(TEXT("Slab, six rails, five posts"), Cubes.Num(), 12);
	TestEqual(TEXT("Two ladder rails and ten rungs down 330 cm"), Cylinders.Num(), 12);
	// Yaw 90: local +X is world +Y, local +Y (out of the facade) is world -X.
	TestTrue(TEXT("The slab sits 50 cm out from the facade, 4 cm under the landing height"),
		Cubes[0].GetLocation().Equals(FVector(1000.f - 50.f, 2000.f, 656.f), 0.01f));
	TestTrue(TEXT("Scaled from the 100 cm cube"), Cubes[0].GetScale3D().Equals(FVector(2.4f, 0.9f, 0.08f), 0.001f));
	for (const FTransform& Part : Cylinders)
	{
		const float Z = Part.GetLocation().Z;
		if (Z < 660.f - 330.f - 1.f || Z > 660.f - 8.f + 1.f)
		{
			AddError(FString::Printf(TEXT("A ladder part at z %.1f is outside the floor below"), Z));
		}
		if (Part.GetLocation().Y < 2000.f)
		{
			AddError(TEXT("LadderSide +1 puts the ladder at the landing's +X end (world +Y)"));
		}
	}
	TArray<FTransform> NoLadderCubes, NoLadder;
	Record.LadderDrop = 0.f;
	AFireEscapeLanding::BuildParts(Record, NoLadderCubes, NoLadder);
	TestEqual(TEXT("The lowest landing has no ladder"), NoLadder.Num(), 0);

	// The rail ledge block: Ledge_1 (local y = 0, top) must run along the outer top rail, 2 cm proud.
	const FTransform Ledge = AFireEscapeLanding::ComputeLedgeTransform(Record);
	FVector A, B;
	AFireEscapeLanding::ComputeOuterRailTop(Record, A, B);
	const FVector Out(-2.f, 0.f, 0.f);
	TestTrue(TEXT("Ledge_1 starts at the rail's +X end"), Ledge.TransformPosition(FVector(0.f, 0.f, 100.f)).Equals(A + Out, 0.05f));
	TestTrue(TEXT("And ends at its -X end"), Ledge.TransformPosition(FVector(100.f, 0.f, 100.f)).Equals(B + Out, 0.05f));
	TestTrue(TEXT("Its -Y side faces out of the facade"),
		Ledge.TransformVectorNoScale(FVector(0.f, -1.f, 0.f)).Equals(FVector(-1.f, 0.f, 0.f), 0.001f));

	// The actor: its boxes are where the record says.
	const FHawkeyeTestWorld TestWorld;
	AFireEscapeLanding* Landing = SpawnLanding(TestWorld, Record);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FireEscapeTest), false);
	TestTrue(TEXT("A trace down onto the landing hits the slab top"), TestWorld.Get()->LineTraceSingleByChannel(Hit,
		FVector(950.f, 2000.f, 700.f), FVector(950.f, 2000.f, 600.f), ECC_Visibility, Params) && Hit.GetActor() == Landing);
	TestEqual(TEXT("At the landing height"), static_cast<float>(Hit.ImpactPoint.Z), 660.f, 0.5f);
	TestTrue(TEXT("A trace in from the street hits the outer rail"), TestWorld.Get()->LineTraceSingleByChannel(Hit,
		FVector(800.f, 2000.f, 700.f), FVector(1000.f, 2000.f, 700.f), ECC_Visibility, Params) && Hit.GetActor() == Landing);
	TestEqual(TEXT("95 cm out from the facade"), static_cast<float>(Hit.ImpactPoint.X), 905.f, 0.5f);
	TestEqual(TEXT("The camera is not blocked by it"), static_cast<int32>(Landing->FindComponentByClass<UPrimitiveComponent>()
		->GetCollisionResponseToChannel(ECC_Camera)), static_cast<int32>(ECR_Ignore));
	return true;
}

namespace HawkeyeWaysDownTest
{
	static AHawkeyeAimTestCharacter* SpawnGrappler(const FHawkeyeTestWorld& TestWorld, const FVector& FeetAt)
	{
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FeetAt);
		if (UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr)
		{
			UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Inventory);
			Arrow->Slot = 2;
			Arrow->Cap = 99;
			Arrow->OnHitEffect = EArrowHitEffect::Grapple;
			Inventory->AddArrows(Arrow, 10);
		}
		return Kate;
	}

	static AGrappleAnchor* SpawnAnchor(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AGrappleAnchor>(TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), Location, FRotator::ZeroRotator));
	}

	static void RunZip(UGrappleComponent* Grapple, float MaxSeconds)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(Grapple->GetOwner());
		for (float T = 0.f; Kate && Grapple->IsZipping() && T < MaxSeconds; T += 1.f / 60.f)
		{
			Kate->TestTickMovement(1.f / 60.f);
		}
	}

	/**
	 * Her roof (top z = 0, to x = 200) with an Obstacle-tall post at x = PostX, and another roof
	 * from x = 1400 at z = OtherRoof with an anchor on its edge.
	 */
	static AGrappleAnchor* SpawnRoofToRoof(const FHawkeyeTestWorld& TestWorld, float PostX, float PostHeight, float OtherRoof)
	{
		SpawnBox(TestWorld, FVector(-1000.f, -500.f, -1000.f), FVector(200.f, 500.f, 0.f));
		if (PostHeight > 0.f)
		{
			SpawnBox(TestWorld, FVector(PostX, -40.f, 0.f), FVector(PostX + 20.f, 40.f, PostHeight));
		}
		SpawnBox(TestWorld, FVector(1400.f, -500.f, OtherRoof - 1000.f), FVector(2600.f, 500.f, OtherRoof));
		return SpawnAnchor(TestWorld, FVector(1415.f, 0.f, OtherRoof));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleLaunch, "Hawkeye.Grapple.LaunchPointAndHop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleLaunch::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	const FHawkeyeTestWorld TestWorld;
	AGrappleAnchor* Anchor = SpawnRoofToRoof(TestWorld, 0.f, 0.f, 0.f);
	AHawkeyeAimTestCharacter* Kate = SpawnGrappler(TestWorld, FVector(0.f, 0.f, 2.f));
	if (!Anchor || !Kate)
	{
		AddError(TEXT("Could not spawn the roofs."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	TestEqual(TEXT("The line starts 120 cm up"), Grapple->ZipLaunchHeight, 120.f);
	TestEqual(TEXT("After a 0.15 s hop"), Kate->GetHawkeyeMovement()->ZipHopSeconds, 0.15f);
	TestEqual(TEXT("Geometry within 250 cm of the start is hers"), Grapple->ZipStartIgnoreRadius, 250.f);
	TestEqual(TEXT("And within 150 cm of the anchor its building's"), Grapple->SupportRadius, 150.f);

	const FVector Start = Kate->GetActorLocation();
	TestTrue(TEXT("From the ground the launch point is 120 cm up"), Grapple->ComputeZipLaunch(Start, true).Equals(Start + FVector(0.f, 0.f, 120.f)));
	TestTrue(TEXT("Mid-air there is no offset"), Grapple->ComputeZipLaunch(Start, false).Equals(Start));
	TestTrue(TEXT("A level roof-to-roof zip starts"), Grapple->StartZip(Anchor));
	TestTrue(TEXT("With the hop"), Grapple->IsHopping());
	TestTrue(TEXT("The line starts at the launch point"), Grapple->GetZipLaunch().Equals(Start + FVector(0.f, 0.f, 120.f)));
	TestEqual(TEXT("Its length is from the launch point"), Grapple->GetZipLength(),
		static_cast<float>(FVector::Dist(Start + FVector(0.f, 0.f, 120.f), Grapple->ComputeZipEnd(Anchor))), 0.5f);
	Kate->TestTickMovement(0.075f);
	TestTrue(TEXT("Half way through the hop she is on her way up, not along"),
		Kate->GetActorLocation().Z > Start.Z + 60.f && FMath::IsNearlyEqual(Kate->GetActorLocation().X, Start.X, 0.5f));
	Kate->TestTickMovement(0.075f);
	TestFalse(TEXT("The hop is over after 0.15 s"), Grapple->IsHopping());
	TestTrue(TEXT("At the launch point"), Kate->GetActorLocation().Equals(Start + FVector(0.f, 0.f, 120.f), 0.5f));
	TestEqual(TEXT("No progress along the line during the hop"), Grapple->GetZipProgress(), 0.f);
	RunZip(Grapple, 3.f);
	TestFalse(TEXT("A level zip arrives"), Grapple->IsZipping());
	TestTrue(TEXT("On the other roof"), Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Anchor), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleStartIgnore, "Hawkeye.Grapple.StartIgnoreRadiusAndLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleStartIgnore::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	{
		// A 150 cm post 150 cm ahead, on her own roof: inside the start radius, ignored.
		const FHawkeyeTestWorld TestWorld;
		AGrappleAnchor* Anchor = SpawnRoofToRoof(TestWorld, 150.f, 150.f, 0.f);
		AHawkeyeAimTestCharacter* Kate = SpawnGrappler(TestWorld, FVector(0.f, 0.f, 2.f));
		UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		TestTrue(TEXT("Offline: clear, the post is within 250 cm of the start"), Grapple->IsZipClear(Kate->GetActorLocation(), Anchor, true));
		Grapple->StartZip(Anchor);
		TestTrue(TEXT("The start supports are ignored at first"), Grapple->IsIgnoringStartSupports());
		RunZip(Grapple, 3.f);
		TestTrue(TEXT("She flies through her own parapet and arrives"), Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Anchor), 0.5f));
	}
	{
		// The same post 600 cm out, in the gap: not hers, so it blocks.
		const FHawkeyeTestWorld TestWorld;
		SpawnBox(TestWorld, FVector(-1000.f, -500.f, -1000.f), FVector(200.f, 500.f, 0.f));
		SpawnBox(TestWorld, FVector(600.f, -40.f, -1000.f), FVector(620.f, 40.f, 150.f));
		SpawnBox(TestWorld, FVector(1400.f, -500.f, -1000.f), FVector(2600.f, 500.f, 0.f));
		AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1415.f, 0.f, 0.f));
		AHawkeyeAimTestCharacter* Kate = SpawnGrappler(TestWorld, FVector(0.f, 0.f, 2.f));
		UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		AActor* Blocker = nullptr;
		TestFalse(TEXT("Offline: blocked by a post 600 cm out"), Grapple->IsZipClear(Kate->GetActorLocation(), Anchor, true, &Blocker));
		TestNotNull(TEXT("And it names the post"), Blocker);
		UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
		Grapple->OnGrappleCancelled.AddDynamic(Listener, &UHawkeyeTestListener::HandleGrappleCancelled);
		Grapple->StartZip(Anchor);
		RunZip(Grapple, 3.f);
		TestEqual(TEXT("The real zip is cancelled too"), Listener->GrappleCancelledCount, 1);
		TestFalse(TEXT("Past the start radius the start supports count again"), Grapple->IsIgnoringStartSupports());
	}
	{
		// A roof 600 cm down: a downward line is allowed and arrives.
		const FHawkeyeTestWorld TestWorld;
		AGrappleAnchor* Anchor = SpawnRoofToRoof(TestWorld, 0.f, 0.f, -600.f);
		AHawkeyeAimTestCharacter* Kate = SpawnGrappler(TestWorld, FVector(0.f, 0.f, 2.f));
		UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		TestTrue(TEXT("Offline: a downward zip is clear"), Grapple->IsZipClear(Kate->GetActorLocation(), Anchor, true));
		Grapple->StartZip(Anchor);
		RunZip(Grapple, 3.f);
		TestTrue(TEXT("And it arrives 600 cm down"), Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Anchor), 0.5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleCancel, "Hawkeye.Grapple.CancelMidZip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleCancel::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWaysDownTest;
	const FHawkeyeTestWorld TestWorld;
	AGrappleAnchor* Anchor = SpawnRoofToRoof(TestWorld, 0.f, 0.f, 0.f);
	// A ledge off to one side of the line, for the catch after letting go.
	SpawnBox(TestWorld, FVector(900.f, 300.f, -2000.f), FVector(1000.f, 600.f, -400.f));
	AHawkeyeAimTestCharacter* Kate = SpawnGrappler(TestWorld, FVector(0.f, 0.f, 2.f));
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Grapple->OnGrappleCancelled.AddDynamic(Listener, &UHawkeyeTestListener::HandleGrappleCancelled);

	Grapple->StartZip(Anchor);
	for (int32 Step = 0; Step < 24; ++Step)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	TestTrue(TEXT("Mid-zip"), Grapple->IsZipping() && Grapple->GetZipProgress() > 0.1f);
	Kate->Jump();
	TestFalse(TEXT("The jump key lets go of the line"), Grapple->IsZipping());
	TestEqual(TEXT("Falling"), static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue()), static_cast<int32>(MOVE_Falling));
	TestEqual(TEXT("Gravity back on"), Kate->GetCharacterMovement()->GravityScale, 1.f);
	TestEqual(TEXT("OnGrappleCancelled once"), Listener->GrappleCancelledCount, 1);
	TestTrue(TEXT("Letting go of a zip is a controlled drop"), Kate->IsControlledDrop());

	// The catch rule is live: a ledge in front, 200 cm above her feet as she falls past it.
	Kate->SetActorRotation(FRotator(0.f, 90.f, 0.f));
	Kate->SetActorLocation(FVector(950.f, 240.f, -400.f - 200.f + HalfHeight));
	Kate->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -400.f);
	TestTrue(TEXT("Falling after the cancel, she catches a ledge"), Kate->GetParkourComponent()->TryCatchLedge());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
