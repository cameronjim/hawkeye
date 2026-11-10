#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Mission/ObjectiveTriggerVolume.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/ObjectiveMarkerMath.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The objective marker's rules (claude-docs/gameplay-semantics.md, "Objective markers and
 * compass"): where the point comes from, that it follows the current objective, and the pure
 * projection, edge clamp and compass arithmetic the HUD draws from.
 */
namespace HawkeyeObjectiveMarkerTest
{
	static UMissionDefinition* MakeMission(UObject* Outer, std::initializer_list<const TCHAR*> Ids)
	{
		UMissionDefinition* Mission = NewObject<UMissionDefinition>(Outer);
		for (const TCHAR* Id : Ids)
		{
			UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
			Objective->ObjectiveId = FName(Id);
			Objective->Title = FText::FromString(Id);
			Mission->Objectives.Add(Objective);
		}
		return Mission;
	}

	static const FVector2D Viewport(1280.f, 720.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveVolumeRegistersLocation, "Hawkeye.Objective.VolumeRegistersLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveVolumeRegistersLocation::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UMissionSubsystem* Missions = TestWorld.Get() ? TestWorld.Get()->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!Missions)
	{
		AddError(TEXT("The test world has no UMissionSubsystem."));
		return false;
	}
	Missions->StartMission(HawkeyeObjectiveMarkerTest::MakeMission(Missions, { TEXT("reach_roof") }));

	FVector Location;
	TestFalse(TEXT("No point before a volume exists"), Missions->GetCurrentObjectiveLocation(Location));

	const FVector Centre(1200.f, -3400.f, 2150.f);
	const FTransform Transform(Centre);
	AObjectiveTriggerVolume* Volume = TestWorld.Get()->SpawnActorDeferred<AObjectiveTriggerVolume>(
		AObjectiveTriggerVolume::StaticClass(), Transform);
	if (!Volume)
	{
		AddError(TEXT("Failed to spawn the volume."));
		return false;
	}
	Volume->ObjectiveId = TEXT("reach_roof");
	Volume->FinishSpawning(Transform);

	TestTrue(TEXT("BeginPlay registered the volume"), Missions->GetCurrentObjectiveLocation(Location));
	TestEqual(TEXT("at its centre"), Location, Centre);
	FVector Registered;
	TestTrue(TEXT("The tracker holds it by id"),
		Missions->GetTracker()->GetRegisteredObjectiveLocation(TEXT("reach_roof"), Registered));
	TestEqual(TEXT("the same point"), Registered, Centre);

	Volume->Destroy();
	TestFalse(TEXT("EndPlay unregisters it"), Missions->GetCurrentObjectiveLocation(Location));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveLocationFollowsCurrent, "Hawkeye.Objective.LocationFollowsCurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveLocationFollowsCurrent::RunTest(const FString& Parameters)
{
	UMissionTracker* Tracker = NewObject<UMissionTracker>();
	const FVector RoofA(100.f, 0.f, 1900.f);
	const FVector RoofB(5000.f, 200.f, 1300.f);

	// Registered before the mission starts, as volumes that BeginPlay first do.
	Tracker->RegisterObjectiveLocation(TEXT("reach_roof"), RoofA);
	Tracker->RegisterObjectiveLocation(TEXT("cross_block"), RoofB);

	UMissionDefinition* Mission = HawkeyeObjectiveMarkerTest::MakeMission(Tracker,
		{ TEXT("reach_roof"), TEXT("cross_block"), TEXT("find_arrow") });
	UMissionObjective* FindArrow = Mission->Objectives[2];
	FindArrow->bHasWorldLocation = true;
	FindArrow->WorldLocation = FVector(-800.f, 900.f, 1280.f);
	Tracker->StartMission(Mission);

	FVector Location;
	TestTrue(TEXT("reach_roof has a point"), Tracker->GetCurrentObjectiveLocation(Location));
	TestEqual(TEXT("reach_roof's volume"), Location, RoofA);

	Tracker->CompleteObjective(TEXT("reach_roof"));
	TestTrue(TEXT("cross_block has a point"), Tracker->GetCurrentObjectiveLocation(Location));
	TestEqual(TEXT("the marker moves to cross_block"), Location, RoofB);

	// An explicit WorldLocation wins over a registration for the same id.
	Tracker->RegisterObjectiveLocation(TEXT("find_arrow"), RoofA);
	Tracker->CompleteObjective(TEXT("cross_block"));
	TestTrue(TEXT("find_arrow has a point"), Tracker->GetCurrentObjectiveLocation(Location));
	TestEqual(TEXT("its own WorldLocation"), Location, FindArrow->WorldLocation);

	Tracker->CompleteObjective(TEXT("find_arrow"));
	TestFalse(TEXT("Nothing current, no point"), Tracker->GetCurrentObjectiveLocation(Location));

	// A restart keeps the registrations: the volumes are still in the level.
	Tracker->StartMission(Mission);
	TestTrue(TEXT("Restarted mission still finds reach_roof"), Tracker->GetCurrentObjectiveLocation(Location));
	TestEqual(TEXT("at the same point"), Location, RoofA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveMarkerProjection, "Hawkeye.Objective.MarkerProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveMarkerProjection::RunTest(const FString& Parameters)
{
	using HawkeyeObjectiveMarkerTest::Viewport;
	// Camera at the origin looking along +X with a 90 degree horizontal FOV.
	const FMatrix ViewProjection = UObjectiveMarkerMath::MakeViewProjection(
		FVector::ZeroVector, FRotator::ZeroRotator, 90.f, Viewport);

	FVector2D Screen;
	TestTrue(TEXT("Straight ahead is in front"),
		UObjectiveMarkerMath::ProjectWorldToScreen(FVector(1000.f, 0.f, 0.f), ViewProjection, Viewport, Screen));
	TestTrue(TEXT("and lands on the centre"), Screen.Equals(FVector2D(640.f, 360.f), 0.5f));

	// 45 degrees right is the edge of a 90 degree FOV.
	UObjectiveMarkerMath::ProjectWorldToScreen(FVector(1000.f, 1000.f, 0.f), ViewProjection, Viewport, Screen);
	TestTrue(TEXT("45 degrees right is the right edge"), Screen.Equals(FVector2D(1280.f, 360.f), 0.5f));

	// Above the axis is up the screen (smaller y); vertical scale follows the aspect ratio.
	UObjectiveMarkerMath::ProjectWorldToScreen(FVector(1000.f, 0.f, 562.5f), ViewProjection, Viewport, Screen);
	TestTrue(TEXT("Up is toward the top"), Screen.Equals(FVector2D(640.f, 0.f), 0.5f));

	TestFalse(TEXT("Behind is not in front"),
		UObjectiveMarkerMath::ProjectWorldToScreen(FVector(-1000.f, 100.f, 0.f), ViewProjection, Viewport, Screen));

	const FObjectiveMarkerPlacement Ahead = UObjectiveMarkerMath::PlaceMarker(
		FVector(2000.f, 200.f, 100.f), ViewProjection, Viewport, 48.f);
	TestTrue(TEXT("A point just right of centre is on screen"), Ahead.bOnScreen && Ahead.bInFront);
	TestTrue(TEXT("and right of centre"), Ahead.Position.X > 640.f && Ahead.Position.Y < 360.f);

	// A turned camera: yaw 90 looks along +Y, so a point on +Y is centred.
	const FMatrix Turned = UObjectiveMarkerMath::MakeViewProjection(
		FVector(0.f, 0.f, 0.f), FRotator(0.f, 90.f, 0.f), 90.f, Viewport);
	UObjectiveMarkerMath::ProjectWorldToScreen(FVector(0.f, 1000.f, 0.f), Turned, Viewport, Screen);
	TestTrue(TEXT("Turned camera centres its own forward"), Screen.Equals(FVector2D(640.f, 360.f), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveMarkerEdgeClamp, "Hawkeye.Objective.MarkerEdgeClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveMarkerEdgeClamp::RunTest(const FString& Parameters)
{
	using HawkeyeObjectiveMarkerTest::Viewport;
	const float Margin = 48.f;

	TestTrue(TEXT("Right clamps to the right margin"), UObjectiveMarkerMath::ClampToScreenEdge(
		FVector2D(10.f, 0.f), Viewport, Margin).Equals(FVector2D(1232.f, 360.f), 0.01f));
	TestTrue(TEXT("Up clamps to the top margin"), UObjectiveMarkerMath::ClampToScreenEdge(
		FVector2D(0.f, -3.f), Viewport, Margin).Equals(FVector2D(640.f, 48.f), 0.01f));
	TestTrue(TEXT("A zero direction goes to the bottom"), UObjectiveMarkerMath::ClampToScreenEdge(
		FVector2D::ZeroVector, Viewport, Margin).Equals(FVector2D(640.f, 672.f), 0.01f));
	// Diagonal on a wide screen meets the top or bottom first: half height 312 against half width 592.
	TestTrue(TEXT("A diagonal meets the nearer edge"), UObjectiveMarkerMath::ClampToScreenEdge(
		FVector2D(1.f, 1.f), Viewport, Margin).Equals(FVector2D(952.f, 672.f), 0.01f));

	const FMatrix ViewProjection = UObjectiveMarkerMath::MakeViewProjection(
		FVector::ZeroVector, FRotator::ZeroRotator, 90.f, Viewport);

	const FObjectiveMarkerPlacement FarRight = UObjectiveMarkerMath::PlaceMarker(
		FVector(1000.f, 3000.f, 0.f), ViewProjection, Viewport, Margin);
	TestTrue(TEXT("Far right is in front but off screen"), FarRight.bInFront && !FarRight.bOnScreen);
	TestTrue(TEXT("held on the right edge"), FarRight.Position.Equals(FVector2D(1232.f, 360.f), 0.5f));
	TestTrue(TEXT("arrow pointing right"), FMath::IsNearlyEqual(FarRight.ArrowAngleDegrees, 0.f, 0.5f));

	const FObjectiveMarkerPlacement BehindLeft = UObjectiveMarkerMath::PlaceMarker(
		FVector(-1000.f, -500.f, 0.f), ViewProjection, Viewport, Margin);
	TestFalse(TEXT("Behind is off screen"), BehindLeft.bOnScreen || BehindLeft.bInFront);
	TestTrue(TEXT("behind and to the left points left, not right"), BehindLeft.Position.X < 640.f);
	TestTrue(TEXT("and sits on the left edge"), FMath::IsNearlyEqual(BehindLeft.Position.X, Margin, 0.5f));

	const FObjectiveMarkerPlacement BehindHigh = UObjectiveMarkerMath::PlaceMarker(
		FVector(-1000.f, 0.f, 2000.f), ViewProjection, Viewport, Margin);
	TestTrue(TEXT("A roof behind and above points up"), FMath::IsNearlyEqual(BehindHigh.Position.Y, Margin, 0.5f));

	const FObjectiveMarkerPlacement DeadBehind = UObjectiveMarkerMath::PlaceMarker(
		FVector(-1000.f, 0.f, 0.f), ViewProjection, Viewport, Margin);
	TestTrue(TEXT("Dead behind drops to the bottom edge"), DeadBehind.Position.Equals(FVector2D(640.f, 672.f), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveCompassBearing, "Hawkeye.Objective.CompassBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveCompassBearing::RunTest(const FString& Parameters)
{
	// The district's north is -Y.
	const float North = -90.f;
	TestEqual(TEXT("Yaw -90 is north"), UObjectiveMarkerMath::YawToBearing(-90.f, North), 0.f);
	TestEqual(TEXT("Yaw 0 (+X) is east"), UObjectiveMarkerMath::YawToBearing(0.f, North), 90.f);
	TestEqual(TEXT("Yaw 90 (+Y) is south"), UObjectiveMarkerMath::YawToBearing(90.f, North), 180.f);
	TestEqual(TEXT("Yaw 180 is west"), UObjectiveMarkerMath::YawToBearing(180.f, North), 270.f);
	TestEqual(TEXT("Yaw -450 wraps to north"), UObjectiveMarkerMath::YawToBearing(-450.f, North), 0.f);

	const FVector Here(500.f, 500.f, 0.f);
	TestTrue(TEXT("A point up -Y bears 0"), FMath::IsNearlyEqual(
		UObjectiveMarkerMath::BearingBetween(Here, FVector(500.f, -1500.f, 900.f), North), 0.f, 0.01f));
	TestTrue(TEXT("A point north-east bears 45"), FMath::IsNearlyEqual(
		UObjectiveMarkerMath::BearingBetween(Here, FVector(1500.f, -500.f, 0.f), North), 45.f, 0.01f));
	TestTrue(TEXT("A point west bears 270"), FMath::IsNearlyEqual(
		UObjectiveMarkerMath::BearingBetween(Here, FVector(-500.f, 500.f, 0.f), North), 270.f, 0.01f));

	TestEqual(TEXT("10 from 350 is 20 to the right"), UObjectiveMarkerMath::BearingDelta(10.f, 350.f), 20.f);
	TestEqual(TEXT("350 from 10 is 20 to the left"), UObjectiveMarkerMath::BearingDelta(350.f, 10.f), -20.f);
	TestEqual(TEXT("Straight behind is +180"), UObjectiveMarkerMath::BearingDelta(180.f, 0.f), 180.f);

	bool bClamped = false;
	TestEqual(TEXT("45 degrees right on a 400 px, 180 degree strip is +100 px"),
		UObjectiveMarkerMath::CompassOffset(45.f, 0.f, 400.f, 180.f, bClamped), 100.f);
	TestFalse(TEXT("not clamped"), bClamped);
	TestEqual(TEXT("West while facing north is the left end"),
		UObjectiveMarkerMath::CompassOffset(270.f, 0.f, 400.f, 180.f, bClamped), -200.f);
	TestEqual(TEXT("Behind is held at an end"),
		FMath::Abs(UObjectiveMarkerMath::CompassOffset(170.f, 0.f, 400.f, 180.f, bClamped)), 200.f);
	TestTrue(TEXT("and says so"), bClamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveToastsAndSizes, "Hawkeye.Objective.ToastsAndSizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveToastsAndSizes::RunTest(const FString& Parameters)
{
	UHawkeyeObjectiveWidget* Widget = NewObject<UHawkeyeObjectiveWidget>();
	TestEqual(TEXT("The diamond is 16 px"), Widget->GetMarkerSize(), 16.f);
	TestEqual(TEXT("The compass is 400 px"), Widget->GetCompassWidth(), 400.f);
	TestEqual(TEXT("Distance reads in whole metres"),
		UObjectiveMarkerMath::FormatDistance(4210.f).ToString(), FString(TEXT("42 m")));

	TestFalse(TEXT("No toast at first"), Widget->IsToastVisible());
	Widget->PushToast(FText::FromString(TEXT("Objective complete")), FText::FromString(TEXT("Get to a rooftop")));
	Widget->PushToast(FText::FromString(TEXT("New objective")), FText::FromString(TEXT("Cross the block")));
	TestEqual(TEXT("The completion shows first"), Widget->GetToastTitle().ToString(), FString(TEXT("Get to a rooftop")));

	Widget->AdvanceToasts(1.9f);
	TestEqual(TEXT("still up at 1.9 s"), Widget->GetToastTitle().ToString(), FString(TEXT("Get to a rooftop")));
	Widget->AdvanceToasts(0.2f);
	TestEqual(TEXT("then the new objective, for its own 2 s"),
		Widget->GetToastHeading().ToString(), FString(TEXT("New objective")));
	Widget->AdvanceToasts(1.7f);
	TestTrue(TEXT("still up"), Widget->IsToastVisible());
	Widget->AdvanceToasts(0.3f);
	TestFalse(TEXT("then gone"), Widget->IsToastVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveToastsWaitForTheTitle, "Hawkeye.Objective.ToastsWaitForTheTitle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveToastsWaitForTheTitle::RunTest(const FString& Parameters)
{
	UHawkeyeObjectiveWidget* Widget = NewObject<UHawkeyeObjectiveWidget>();
	// The title card is up when the chapter's first objective is announced.
	Widget->SetToastsHeld(true);
	Widget->PushToast(FText::FromString(TEXT("New objective")), FText::FromString(TEXT("Get to a rooftop")));
	TestTrue(TEXT("queued"), Widget->IsToastQueued());
	TestFalse(TEXT("but not shown under the card"), Widget->IsToastVisible());
	Widget->AdvanceToasts(5.f);
	TestTrue(TEXT("and its clock does not run while held"), Widget->IsToastQueued());

	Widget->SetToastsHeld(false);
	TestTrue(TEXT("the card gone, it shows"), Widget->IsToastVisible());
	TestEqual(TEXT("the same toast"), Widget->GetToastTitle().ToString(), FString(TEXT("Get to a rooftop")));
	Widget->AdvanceToasts(1.9f);
	TestTrue(TEXT("for its full 2 s"), Widget->IsToastVisible());
	Widget->AdvanceToasts(0.2f);
	TestFalse(TEXT("then gone"), Widget->IsToastVisible());

	// Held partway through, one starts over when it comes back.
	Widget->PushToast(FText::FromString(TEXT("[Safehouse nearby]")), FText::FromString(TEXT("Safehouse 1")));
	Widget->AdvanceToasts(1.5f);
	Widget->SetToastsHeld(true);
	Widget->SetToastsHeld(false);
	Widget->AdvanceToasts(1.5f);
	TestTrue(TEXT("a toast cut off by a hold gets its 2 s again"), Widget->IsToastVisible());
	return true;
}

#endif
