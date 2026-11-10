#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Engine/World.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeInventoryWidget.h"
#include "UI/HawkeyeMapMath.h"
#include "UI/HawkeyeMapWidget.h"
#include "World/CityMapData.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/** The world map (claude-docs/gameplay-semantics.md, "World map"): its data, its projection, its icons and its mark. */
namespace HawkeyeWorldMapTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** 140 East 7th Street, the first safehouse's building. */
	static const TCHAR* SafehouseBuilding = TEXT("W248142338");

	static FHawkeyeMapPlace Place(const TCHAR* Id, const FVector& At, bool bDiscovered, EChallengeMedal Medal = EChallengeMedal::None,
		bool bMarked = false)
	{
		FHawkeyeMapPlace Out;
		Out.Id = Id;
		Out.Name = FText::FromString(Id);
		Out.Location = At;
		Out.bDiscovered = bDiscovered;
		Out.Medal = Medal;
		Out.bMarked = bMarked;
		return Out;
	}

	static int32 Count(const TArray<FHawkeyeMapIcon>& Icons, EHawkeyeMapIconKind Kind)
	{
		return Icons.FilterByPredicate([Kind](const FHawkeyeMapIcon& Icon) { return Icon.Kind == Kind; }).Num();
	}

	static const FHawkeyeMapIcon* Find(const TArray<FHawkeyeMapIcon>& Icons, FName Id)
	{
		return Icons.FindByPredicate([Id](const FHawkeyeMapIcon& Icon) { return Icon.Id == Id; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWorldMapData, "Hawkeye.Map.DataRoundTrip", HawkeyeWorldMapTest::Flags)

bool FHawkeyeWorldMapData::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWorldMapTest;
	const UCityMapData* Data = LoadObject<UCityMapData>(nullptr, UCityMapData::DefaultPath);
	if (!TestNotNull(TEXT("DA_EastVillage_Map exists (Tools\\generate-city.ps1 writes it)"), Data))
	{
		return false;
	}
	TestTrue(TEXT("It is usable"), Data->IsUsable());
	TestFalse(TEXT("It carries the generator's hash"), Data->SourceHash.IsEmpty());

	// The ground rectangle is 667 x 376 m plus a 22 m margin, turned 29 degrees: its box is about 800 x 700 m.
	const FVector2D Size = (Data->BoundsMax - Data->BoundsMin) / 100.0;
	AddInfo(FString::Printf(TEXT("Bounds %.0f x %.0f m, %d footprints, %d parks, %d street pieces."), Size.X, Size.Y,
		Data->Footprints.Num(), Data->Parks.Num(), Data->Streets.Num()));
	TestTrue(TEXT("Bounds are 700 to 900 m east to west"), Size.X > 700.0 && Size.X < 900.0);
	TestTrue(TEXT("and 600 to 800 m north to south"), Size.Y > 600.0 && Size.Y < 800.0);
	TestTrue(TEXT("The origin (the district's middle) is inside"), Data->BoundsMin.X < 0.0 && Data->BoundsMax.X > 0.0
		&& Data->BoundsMin.Y < 0.0 && Data->BoundsMax.Y > 0.0);

	// 526 buildings in the OSM records; a degenerate one or two may be dropped, as the meshes drop them.
	TestTrue(TEXT("Every building has a footprint"), Data->Footprints.Num() >= 500 && Data->Footprints.Num() <= 526);
	TestEqual(TEXT("One park: Tompkins Square"), Data->Parks.Num(), 1);
	TestTrue(TEXT("The streets are there"), Data->Streets.Num() >= 10);

	int32 MaxPoints = 0;
	int32 Outside = 0;
	for (const FCityMapPolygon& Footprint : Data->Footprints)
	{
		MaxPoints = FMath::Max(MaxPoints, Footprint.Points.Num());
		for (const FVector2D& Point : Footprint.Points)
		{
			Outside += (Point.X < Data->BoundsMin.X - 5000.0 || Point.X > Data->BoundsMax.X + 5000.0
				|| Point.Y < Data->BoundsMin.Y - 5000.0 || Point.Y > Data->BoundsMax.Y + 5000.0) ? 1 : 0;
		}
	}
	TestEqual(TEXT("No footprint point is more than 50 m outside the bounds"), Outside, 0);
	TestTrue(TEXT("Footprints are simplified (no more than 40 points)"), MaxPoints <= 40);

	const FCityMapPolygon* Known = Data->FindFootprint(SafehouseBuilding);
	if (TestNotNull(TEXT("The first safehouse's building (W248142338) has a footprint"), Known))
	{
		TestTrue(TEXT("It is a few points"), Known->Points.Num() >= 3 && Known->Points.Num() <= 12);
		TestTrue(TEXT("It is 15.9 m tall, as its OSM record says"), FMath::IsNearlyEqual(Known->HeightM, 15.9f, 0.05f));
	}
	for (const FCityMapStreet& Street : Data->Streets)
	{
		if (Street.Points.Num() < 2 || Street.WidthM <= 0.f)
		{
			AddError(FString::Printf(TEXT("Street piece %s has %d points and %.1f m width."), *Street.Name, Street.Points.Num(), Street.WidthM));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWorldMapProjection, "Hawkeye.Map.Projection", HawkeyeWorldMapTest::Flags)

bool FHawkeyeWorldMapProjection::RunTest(const FString& Parameters)
{
	// An 800 x 400 m district in a 1000 x 600 px area at (16, 16): the width limits the fit.
	FHawkeyeMapView View;
	View.SetFrame(FVector2D(-40000.0, -20000.0), FVector2D(40000.0, 20000.0), FVector2D(16.0, 16.0), FVector2D(1000.0, 600.0));
	TestEqual(TEXT("Fit scale: 1000 px over 80000 cm"), View.GetFitScale(), 1000.0 / 80000.0);
	TestTrue(TEXT("The centre is the area's centre"), View.WorldToMap(FVector2D::ZeroVector).Equals(FVector2D(516.0, 316.0), 0.01));
	TestTrue(TEXT("The west edge is the gutter"), FMath::IsNearlyEqual(View.WorldToMap(FVector2D(-40000.0, 0.0)).X, 16.0, 0.01));
	TestTrue(TEXT("The east edge is the area's right"), FMath::IsNearlyEqual(View.WorldToMap(FVector2D(40000.0, 0.0)).X, 1016.0, 0.01));
	// North up: north is world -Y (East 11th has the smaller Y), so it is the smaller screen Y.
	TestTrue(TEXT("North is up"), View.WorldToMap(FVector2D(0.0, -20000.0)).Y < View.WorldToMap(FVector2D(0.0, 20000.0)).Y);
	TestTrue(TEXT("East is right"), View.WorldToMap(FVector2D(10000.0, 0.0)).X > View.WorldToMap(FVector2D::ZeroVector).X);
	const FVector2D Probe(12345.0, -6789.0);
	TestTrue(TEXT("Map to world undoes world to map"), View.MapToWorld(View.WorldToMap(Probe)).Equals(Probe, 0.01));
	TestTrue(TEXT("A yaw of -90 (north) points up the screen"), FMath::Sin(FHawkeyeMapView::YawToMapAngle(-90.f)) < -0.99f);

	// Zoom about a point keeps that world point under it; the zoom is held to 1..3.
	const FVector2D Cursor(300.0, 200.0);
	const FVector2D Under = View.MapToWorld(Cursor);
	View.ZoomAbout(Cursor, 2.f);
	TestEqual(TEXT("Zoomed to 2x"), View.Zoom, 2.f);
	TestTrue(TEXT("The point under the cursor stays there"), View.MapToWorld(Cursor).Equals(Under, 1.0));
	View.ZoomAbout(Cursor, 10.f);
	TestEqual(TEXT("No closer than 3x"), View.Zoom, FHawkeyeMapView::ZoomMax);
	View.ZoomAbout(Cursor, 0.2f);
	TestEqual(TEXT("No further than 1x"), View.Zoom, FHawkeyeMapView::ZoomMin);
	TestTrue(TEXT("At 1x the district is centred again"), View.Centre.Equals(FVector2D::ZeroVector, 0.01));

	// Panning follows the mouse and stops at the district's edge.
	View.ZoomAbout(FVector2D(516.0, 316.0), 3.f);
	View.PanPixels(FVector2D(100.0, 0.0));
	TestTrue(TEXT("Dragging right moves the view west"), View.Centre.X < 0.0);
	View.PanPixels(FVector2D(100000.0, 100000.0));
	TestTrue(TEXT("The west edge stays on the area's left"), FMath::IsNearlyEqual(View.WorldToMap(FVector2D(-40000.0, 0.0)).X, 16.0, 0.01));
	TestTrue(TEXT("The north edge stays on the area's top"), FMath::IsNearlyEqual(View.WorldToMap(FVector2D(0.0, -20000.0)).Y, 16.0, 0.01));
	View.Reset();
	View.PanPixels(FVector2D(0.0, 300.0));
	TestTrue(TEXT("At 1x the view does not pan"), View.Centre.Equals(FVector2D::ZeroVector, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWorldMapIconSet, "Hawkeye.Map.IconSet", HawkeyeWorldMapTest::Flags)

bool FHawkeyeWorldMapIconSet::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWorldMapTest;
	FHawkeyeMapState State;
	State.bHasPlayer = true;
	State.PlayerLocation = FVector(0.f, 0.f, 100.f);
	State.PlayerYaw = -90.f;
	State.PlayerName = FText::FromString(TEXT("Kate"));
	State.bHasPartner = true;
	State.PartnerLocation = FVector(-500.f, 0.f, 100.f);
	State.bHasObjective = true;
	State.ObjectiveLocation = FVector(3000.f, 0.f, 1500.f);
	State.Safehouses = { Place(TEXT("found"), FVector(5000.f, 0.f, 0.f), true, EChallengeMedal::None, true),
		Place(TEXT("unfound"), FVector(-9000.f, 0.f, 0.f), false) };
	State.Challenges = { Place(TEXT("gold"), FVector(0.f, 8000.f, 2000.f), true, EChallengeMedal::Gold),
		Place(TEXT("none"), FVector(0.f, -8000.f, 2000.f), true) };
	State.Doors = { Place(TEXT("door"), FVector(2000.f, 2000.f, 0.f), false) };
	// The marked safehouse's own marker (250 cm over its door) and a challenge target elsewhere.
	State.SecondaryMarkers = { FVector(5000.f, 0.f, 250.f), FVector(12000.f, 12000.f, 900.f) };

	const TArray<FHawkeyeMapIcon> Icons = FHawkeyeMapIcons::Build(State);
	TestEqual(TEXT("Both safehouses, found or not"), Count(Icons, EHawkeyeMapIconKind::Safehouse), 2);
	TestEqual(TEXT("Both pedestals"), Count(Icons, EHawkeyeMapIconKind::Challenge), 2);
	TestEqual(TEXT("The door"), Count(Icons, EHawkeyeMapIconKind::InteriorDoor), 1);
	TestEqual(TEXT("The objective"), Count(Icons, EHawkeyeMapIconKind::Objective), 1);
	TestEqual(TEXT("No crime is on"), Count(Icons, EHawkeyeMapIconKind::Crime), 0);
	TestEqual(TEXT("The marked safehouse's marker is its house, not a second icon"), Count(Icons, EHawkeyeMapIconKind::Secondary), 1);
	TestTrue(TEXT("The player is drawn last, on top"), Icons.Num() > 0 && Icons.Last().Kind == EHawkeyeMapIconKind::Player);
	TestEqual(TEXT("with her heading"), Icons.Last().Yaw, -90.f);

	const FHawkeyeMapIcon* Found = Find(Icons, TEXT("found"));
	const FHawkeyeMapIcon* Unfound = Find(Icons, TEXT("unfound"));
	TestTrue(TEXT("A found safehouse is filled"), Found && Found->bDiscovered);
	TestTrue(TEXT("and it is the marked one"), Found && Found->bMarked);
	TestTrue(TEXT("An unfound one is hollow"), Unfound && !Unfound->bDiscovered);
	TestTrue(TEXT("Safehouses can be marked"), Found && Found->IsMarkable());

	const FLinearColor Unearned(0.78f, 0.55f, 1.f, 1.f);
	const FHawkeyeMapIcon* Gold = Find(Icons, TEXT("gold"));
	const FHawkeyeMapIcon* None = Find(Icons, TEXT("none"));
	TestTrue(TEXT("The gold pedestal carries its medal"), Gold && Gold->Medal == EChallengeMedal::Gold);
	TestTrue(TEXT("Gold is drawn gold, not purple"), Gold && !FHawkeyeMapIcons::GetMedalColor(Gold->Medal, Unearned).Equals(Unearned));
	TestTrue(TEXT("No medal yet is the palette's purple"), None && FHawkeyeMapIcons::GetMedalColor(None->Medal, Unearned).Equals(Unearned));
	TestFalse(TEXT("Silver and bronze differ"), FHawkeyeMapIcons::GetMedalColor(EChallengeMedal::Silver, Unearned)
		.Equals(FHawkeyeMapIcons::GetMedalColor(EChallengeMedal::Bronze, Unearned)));

	// A crime on: its marker is the crime's icon.
	State.bCrimeActive = true;
	State.CrimeLocation = FVector(12000.f, 12000.f, 0.f);
	const TArray<FHawkeyeMapIcon> WithCrime = FHawkeyeMapIcons::Build(State);
	TestEqual(TEXT("The crime has its icon"), Count(WithCrime, EHawkeyeMapIconKind::Crime), 1);
	TestEqual(TEXT("and its marker is not drawn twice"), Count(WithCrime, EHawkeyeMapIconKind::Secondary), 0);

	// The cursor picks the place over the player standing next to it.
	FHawkeyeMapView View;
	View.SetFrame(FVector2D(-20000.0), FVector2D(20000.0), FVector2D::ZeroVector, FVector2D(1000.0));
	const FVector2D OnFound = View.WorldToMap(FVector2D(5000.0, 0.0));
	const int32 Hit = FHawkeyeMapIcons::FindAt(Icons, View, OnFound + FVector2D(3.0, 0.0), 16.f);
	TestTrue(TEXT("Hovering the found safehouse finds it"), Icons.IsValidIndex(Hit) && Icons[Hit].Id == FName(TEXT("found")));
	TestEqual(TEXT("Nothing far from any icon"), FHawkeyeMapIcons::FindAt(Icons, View, FVector2D(990.0, 10.0), 16.f), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWorldMapMark, "Hawkeye.Map.MarkFromMap", HawkeyeWorldMapTest::Flags)

bool FHawkeyeWorldMapMark::RunTest(const FString& Parameters)
{
	using namespace HawkeyeWorldMapTest;
	const FHawkeyeTestWorld TestWorld;
	ASafehouse* Safehouse = Cast<ASafehouse>(TestWorld.SpawnActor(ASafehouse::StaticClass(), FVector(4000.f, 0.f, 0.f), FRotator::ZeroRotator));
	AChallengeStart* Start = Cast<AChallengeStart>(TestWorld.SpawnActor(AChallengeStart::StaticClass(), FVector(-6000.f, 0.f, 0.f),
		FRotator::ZeroRotator));
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(TestWorld.Get());
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(TestWorld.Get());
	UMissionSubsystem* Missions = UMissionSubsystem::Get(TestWorld.Get());
	if (!Safehouse || !Start || !Safehouses || !Challenges || !Missions)
	{
		AddError(TEXT("Could not set up the safehouse, the pedestal or the subsystems."));
		return false;
	}
	Safehouse->SafehouseId = TEXT("map_house");
	Safehouse->DisplayName = FText::FromString(TEXT("[Map house]"));
	UChallengeDefinition* Definition = NewObject<UChallengeDefinition>(Start);
	Definition->Id = TEXT("map_range");
	Start->Definition = Definition;

	// This world has no save: the safehouse is not found yet, and the map says so.
	TArray<FHawkeyeMapIcon> Icons = FHawkeyeMapIcons::Build(UHawkeyeMapWidget::GatherState(TestWorld.Get(), nullptr));
	const FHawkeyeMapIcon* House = Find(Icons, TEXT("map_house"));
	const FHawkeyeMapIcon* Range = Find(Icons, TEXT("map_range"));
	if (!TestNotNull(TEXT("The safehouse is on the map"), House) || !TestNotNull(TEXT("The pedestal is on the map"), Range))
	{
		return false;
	}
	TestFalse(TEXT("Not found yet: hollow"), House->bDiscovered);
	TestEqual(TEXT("and it is the unknown safehouse"), House->Name.ToString(), FString(TEXT("[Unknown safehouse]")));
	TestEqual(TEXT("No medal yet"), Range->Medal, EChallengeMedal::None);
	const FHawkeyeMapIcon HouseIcon = *House;
	const FHawkeyeMapIcon RangeIcon = *Range;

	TestTrue(TEXT("Marking the safehouse from the map"), UHawkeyeMapWidget::MarkIcon(TestWorld.Get(), HouseIcon));
	TestEqual(TEXT("marks it, as Mark nearest does"), Safehouses->GetMarkedSafehouse(), Safehouse);
	TestEqual(TEXT("with its world marker"), Missions->GetSecondaryMarkers().Num(), 1);
	Icons = FHawkeyeMapIcons::Build(UHawkeyeMapWidget::GatherState(TestWorld.Get(), nullptr));
	TestTrue(TEXT("The map rings it"), Find(Icons, TEXT("map_house")) && Find(Icons, TEXT("map_house"))->bMarked);
	TestEqual(TEXT("and its marker is not a second icon"), Count(Icons, EHawkeyeMapIconKind::Secondary), 0);

	TestTrue(TEXT("Marking the pedestal"), UHawkeyeMapWidget::MarkIcon(TestWorld.Get(), RangeIcon));
	TestEqual(TEXT("marks it"), Challenges->GetMarkedStart(), Start);
	TestNull(TEXT("and takes the safehouse's marker down: one marked place"), Safehouses->GetMarkedSafehouse());
	TestEqual(TEXT("One marker"), Missions->GetSecondaryMarkers().Num(), 1);

	TestTrue(TEXT("Marking the marked pedestal again"), UHawkeyeMapWidget::MarkIcon(TestWorld.Get(), RangeIcon));
	TestNull(TEXT("clears it"), Challenges->GetMarkedStart());
	TestEqual(TEXT("No markers"), Missions->GetSecondaryMarkers().Num(), 0);

	FHawkeyeMapIcon Door;
	Door.Kind = EHawkeyeMapIconKind::InteriorDoor;
	TestFalse(TEXT("A door cannot be marked"), UHawkeyeMapWidget::MarkIcon(TestWorld.Get(), Door));
	FHawkeyeMapIcon Gone = HouseIcon;
	Gone.Id = TEXT("no_such_house");
	TestFalse(TEXT("Nor a safehouse that is not there"), UHawkeyeMapWidget::MarkIcon(TestWorld.Get(), Gone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWorldMapOpens, "Hawkeye.Map.OpensPausesAndCloses", HawkeyeWorldMapTest::Flags)

bool FHawkeyeWorldMapOpens::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = Cast<AHawkeyePauseTestController>(
		TestWorld.SpawnActor(AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("A player controller"), PC))
	{
		return false;
	}
	PC->InventoryWidgetClass = UHawkeyeInventoryWidget::StaticClass();
	PC->ToggleMap();
	TestTrue(TEXT("M opens the map"), PC->IsMapOpen());
	TestTrue(TEXT("and pauses the game under it"), PC->IsPaused());
	PC->SetInventoryOpen(true);
	TestFalse(TEXT("The inventory does not open over the map"), PC->IsInventoryOpen());
	PC->TestInputPause();
	TestFalse(TEXT("Escape closes the map"), PC->IsMapOpen());
	TestFalse(TEXT("without opening the pause menu"), PC->IsPauseMenuOpen());
	TestFalse(TEXT("and the game runs again"), PC->IsPaused());

	PC->SetInventoryOpen(true);
	PC->ToggleMap();
	TestFalse(TEXT("The map does not open over the inventory"), PC->IsMapOpen());
	PC->SetInventoryOpen(false);

	// The pad: a 0.4 s hold of D-pad up opens it; a tap does not.
	PC->HandleDPadUpPressed(10.0);
	PC->HandleDPadUpReleased(10.1);
	TestFalse(TEXT("A tap of D-pad up is the standard arrows, not the map"), PC->IsMapOpen());
	PC->HandleDPadUpPressed(20.0);
	PC->TickDPadUp(20.2);
	TestFalse(TEXT("Not yet at 0.2 s"), PC->IsMapOpen());
	PC->TickDPadUp(20.45);
	TestTrue(TEXT("A 0.4 s hold opens the map"), PC->IsMapOpen());
	PC->HandleDPadUpReleased(20.6);
	TestTrue(TEXT("and letting go leaves it open"), PC->IsMapOpen());
	PC->SetMapOpen(false);
	TestFalse(TEXT("Closed"), PC->IsMapOpen());
	return true;
}

#endif
