#include "Boss/BossDefinition.h"
#include "Boss/BossRules.h"
#include "Demo/DemoStart.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The owner's demo clip starts (claude-docs/gameplay-semantics.md, "Demo starts"; Demo/DemoStart.h): the switch parses, the
 * geometry that picks a stand is sound, and in a world with ground each plan returns a stand on walkable ground, the stated
 * distance from what it faces, looking at it. Written to run headless; not part of a lap.
 */
namespace HawkeyeDemoStartTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static bool IsFinite(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	/** A box that blocks everything with its top face at TopZ, centred on XY. */
	static AHawkeyeTestBlocker* AddSlab(const FHawkeyeTestWorld& TestWorld, const FVector2D& XY, float TopZ, const FVector& HalfExtent)
	{
		AHawkeyeTestBlocker* Slab = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(),
			FVector(XY.X, XY.Y, TopZ - HalfExtent.Z), FRotator::ZeroRotator));
		if (Slab)
		{
			Slab->SetExtent(HalfExtent);
		}
		return Slab;
	}

	/** Ground at z 0 for 300 m round the origin. */
	static AHawkeyeTestBlocker* AddGround(const FHawkeyeTestWorld& TestWorld)
	{
		return AddSlab(TestWorld, FVector2D::ZeroVector, 0.f, FVector(30000.f, 30000.f, 50.f));
	}

	/** A downward trace from above the stand's feet finds ground within a centimetre of them. */
	static bool StandsOnGround(UWorld* World, const FVector& Feet)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoStartTestGround), false);
		return World->LineTraceSingleByChannel(Hit, Feet + FVector(0.f, 0.f, 200.f), Feet - FVector(0.f, 0.f, 200.f), ECC_Visibility, Params)
			&& FMath::Abs(Hit.ImpactPoint.Z - Feet.Z) < 1.f;
	}

	static float FacingDot(const FHawkeyeDemoStand& Stand, const FVector& Target)
	{
		return FVector::DotProduct(FRotator(0.f, Stand.Yaw, 0.f).Vector(), (Target - Stand.Feet).GetSafeNormal2D());
	}

	static AThugCharacter* AddThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, EThugWeapon Weapon, bool bStreetGroup)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator::ZeroRotator));
		if (Thug)
		{
			Thug->Weapon = Weapon;
			if (bStreetGroup)
			{
				Thug->Tags.Add(FHawkeyeDemoStart::StreetGroupTag);
			}
		}
		return Thug;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartParsesSwitch, "Hawkeye.DemoStart.ParsesTheSwitch", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartParsesSwitch::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("street_fight"), FHawkeyeDemoStart::ParseClip(TEXT("street_fight")), EHawkeyeDemoClip::StreetFight);
	TestEqual(TEXT("stealth_takedown"), FHawkeyeDemoStart::ParseClip(TEXT("stealth_takedown")), EHawkeyeDemoClip::StealthTakedown);
	TestEqual(TEXT("tomas_phase"), FHawkeyeDemoStart::ParseClip(TEXT("tomas_phase")), EHawkeyeDemoClip::TomasPhase);
	TestEqual(TEXT("any case, a dash for the underscore"), FHawkeyeDemoStart::ParseClip(TEXT(" Street-Fight ")), EHawkeyeDemoClip::StreetFight);
	TestEqual(TEXT("a name that is no clip"), FHawkeyeDemoStart::ParseClip(TEXT("grotto")), EHawkeyeDemoClip::None);
	TestEqual(TEXT("empty"), FHawkeyeDemoStart::ParseClip(TEXT("")), EHawkeyeDemoClip::None);
	for (const EHawkeyeDemoClip Clip : { EHawkeyeDemoClip::StreetFight, EHawkeyeDemoClip::StealthTakedown, EHawkeyeDemoClip::TomasPhase })
	{
		TestEqual(TEXT("A clip's name parses back to it"), FHawkeyeDemoStart::ParseClip(FHawkeyeDemoStart::ClipName(Clip)), Clip);
	}

	FString Asked;
	TestEqual(TEXT("On a command line, among others"),
		FHawkeyeDemoStart::ClipFromCommandLine(TEXT("Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -NoMainMenu -HawkeyeDemoStart=tomas_phase -log"), &Asked),
		EHawkeyeDemoClip::TomasPhase);
	TestEqual(TEXT("and the raw text it read"), Asked, FString(TEXT("tomas_phase")));
	TestEqual(TEXT("At the end of the line"), FHawkeyeDemoStart::ClipFromCommandLine(TEXT("-game -HawkeyeDemoStart=street_fight")),
		EHawkeyeDemoClip::StreetFight);
	TestEqual(TEXT("No switch: no clip"), FHawkeyeDemoStart::ClipFromCommandLine(TEXT("-game -HawkeyeChapter=DA_CH03_Grotto"), &Asked),
		EHawkeyeDemoClip::None);
	TestTrue(TEXT("and nothing asked for (the game mode does nothing)"), Asked.IsEmpty());
	TestEqual(TEXT("A name that is no clip: none, but it was asked for"),
		FHawkeyeDemoStart::ClipFromCommandLine(TEXT("-HawkeyeDemoStart=grotto"), &Asked), EHawkeyeDemoClip::None);
	TestEqual(TEXT("...and says what"), Asked, FString(TEXT("grotto")));
	TestEqual(TEXT("A null command line"), FHawkeyeDemoStart::ClipFromCommandLine(nullptr), EHawkeyeDemoClip::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartGeometry, "Hawkeye.DemoStart.Geometry", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartGeometry::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDemoStartTest;
	TestTrue(TEXT("Facing +Y is 90 degrees"), FMath::IsNearlyEqual(FHawkeyeDemoStart::YawFacing(FVector::ZeroVector, FVector(0.f, 500.f, 90.f)), 90.f, 0.01f));
	TestTrue(TEXT("Facing -X is 180 degrees"), FMath::IsNearlyEqual(FMath::Abs(FHawkeyeDemoStart::YawFacing(FVector(10.f, 10.f, 0.f), FVector(-90.f, 10.f, 0.f))), 180.f, 0.01f));

	// The street fight's spot: 9 m from the pair's middle toward the heavy, at the pair's height.
	const FVector Pair(100.f, 200.f, 50.f);
	const FVector Heavy(1100.f, 1200.f, 0.f);
	const FVector Spot = FHawkeyeDemoStart::StreetFightSpot(Pair, Heavy);
	TestTrue(TEXT("Nine metres from the pair"), FMath::IsNearlyEqual(FVector::Dist2D(Spot, Pair), FHawkeyeDemoStart::StreetFightDistance, 0.5f));
	TestTrue(TEXT("on the heavy's side"), FVector::DotProduct((Spot - Pair).GetSafeNormal2D(), (Heavy - Pair).GetSafeNormal2D()) > 0.999f);
	TestEqual(TEXT("at the pair's height"), Spot.Z, Pair.Z);
	TestTrue(TEXT("Finite"), IsFinite(Spot));
	TestTrue(TEXT("A heavy on top of the pair does not make NaN"), IsFinite(FHawkeyeDemoStart::StreetFightSpot(Pair, Pair)));

	TestTrue(TEXT("Inside a box"), FHawkeyeDemoStart::IsInsideBox2D(FVector(10.f, 10.f, 999.f), FVector::ZeroVector, FVector(100.f, 100.f, 1.f), 0.f));
	TestFalse(TEXT("outside it"), FHawkeyeDemoStart::IsInsideBox2D(FVector(150.f, 0.f, 0.f), FVector::ZeroVector, FVector(100.f, 100.f, 1.f), 0.f));
	TestTrue(TEXT("a margin grows it"), FHawkeyeDemoStart::IsInsideBox2D(FVector(150.f, 0.f, 0.f), FVector::ZeroVector, FVector(100.f, 100.f, 1.f), 60.f));

	// A door facing +X with the grotto's arrival volume 4.5 m out and 6.5 m each way: the stand is outside the volume.
	const FVector Door(1000.f, 2000.f, 15.f);
	const FVector VolumeCentre = Door + FVector(450.f, 0.f, 150.f);
	const FVector VolumeHalf(650.f, 650.f, 300.f);
	const TArray<FVector> Spots = FHawkeyeDemoStart::DoorApproachCandidates(Door, 0.f, VolumeCentre, VolumeHalf);
	TestTrue(TEXT("There are candidates"), Spots.Num() > 0);
	float LastDistance = 0.f;
	bool bAllOutside = true;
	bool bFinite = true;
	bool bInRange = true;
	bool bOrdered = true;
	for (const FVector& Candidate : Spots)
	{
		const float Distance = FVector::Dist2D(Candidate, Door);
		bAllOutside &= !FHawkeyeDemoStart::IsInsideBox2D(Candidate, VolumeCentre, VolumeHalf, FHawkeyeDemoStart::VolumeMargin);
		bFinite &= IsFinite(Candidate) && FMath::IsNearlyEqual(Candidate.Z, Door.Z);
		bInRange &= Distance >= FHawkeyeDemoStart::DoorApproachMin - 1.f && Distance <= FHawkeyeDemoStart::DoorApproachMax + 1.f;
		bOrdered &= Distance >= LastDistance - 1.f;
		LastDistance = Distance;
	}
	TestTrue(TEXT("every candidate is outside the arrival volume, so walking forward enters it"), bAllOutside);
	TestTrue(TEXT("finite, at the door's height"), bFinite);
	TestTrue(TEXT("between the nearest and furthest distance"), bInRange);
	TestTrue(TEXT("nearest first"), bOrdered);
	TestTrue(TEXT("the nearest candidate is no further than straight out past the volume (13 m)"), FVector::Dist2D(Spots[0], Door) <= 1300.f + 1.f);
	TestTrue(TEXT("and past the volume's own reach: nothing within 8 m of the door is offered"), FVector::Dist2D(Spots[0], Door) >= 800.f);

	// With no volume the nearest is the 6 m the clip is described with.
	const TArray<FVector> Near = FHawkeyeDemoStart::DoorApproachCandidates(Door, 90.f, Door, FVector::ZeroVector);
	TestTrue(TEXT("Without a volume: 6 m out"), Near.Num() > 0 && FMath::IsNearlyEqual(FVector::Dist2D(Near[0], Door), 600.f, 0.5f));
	TestTrue(TEXT("along the door's facing"), Near.Num() > 0 && Near[0].Equals(FVector(Door.X, Door.Y + 600.f, Door.Z), 0.5f));

	// The roof entry: from the anchor toward the centre, 2.5 m to 9 m in.
	const FVector Anchor(-2000.f, 100.f, 1100.f);
	const FVector Centre(0.f, 100.f, 1000.f);
	const TArray<FVector> Roof = FHawkeyeDemoStart::RoofEntryCandidates(Anchor, Centre);
	TestEqual(TEXT("14 spots from 2.5 to 9 m"), Roof.Num(), 14);
	TestTrue(TEXT("the first is 2.5 m in"), Roof.Num() > 0 && Roof[0].Equals(FVector(-1750.f, 100.f, 1100.f), 0.5f));
	TestTrue(TEXT("the last is 9 m in"), Roof.Num() > 0 && Roof.Last().Equals(FVector(-1100.f, 100.f, 1100.f), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartStreetFightStand, "Hawkeye.DemoStart.StreetFightStand", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartStreetFightStand::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDemoStartTest;
	{
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld)))
		{
			return false;
		}
		// The bat and the gunner 2 m apart, the heavy 40 m down the avenue; a bystander of another group stands near and counts for nothing.
		AddThug(TestWorld, FVector(0.f, 0.f, 100.f), EThugWeapon::Fists, true);
		AddThug(TestWorld, FVector(200.f, 0.f, 100.f), EThugWeapon::Pistol, true);
		AddThug(TestWorld, FVector(4000.f, 100.f, 100.f), EThugWeapon::Shield, true);
		AddThug(TestWorld, FVector(-300.f, 0.f, 100.f), EThugWeapon::Fists, false);
		const FVector PairMid(100.f, 0.f, 100.f);
		const FVector HeavyAt(4000.f, 100.f, 100.f);

		FHawkeyeDemoStand Stand;
		FString Why;
		const bool bPlanned = FHawkeyeDemoStart::PlanStreetFight(World, nullptr, Stand, Why);
		if (!TestTrue(FString::Printf(TEXT("The stand is planned (%s)"), *Why), bPlanned))
		{
			return false;
		}
		TestTrue(TEXT("Her feet are finite"), IsFinite(Stand.Feet));
		TestTrue(TEXT("on ground a downward trace finds"), StandsOnGround(World, Stand.Feet));
		TestTrue(TEXT("9 m from the pair"), FMath::IsNearlyEqual(FVector::Dist2D(Stand.Feet, PairMid), FHawkeyeDemoStart::StreetFightDistance, 1.f));
		TestTrue(TEXT("facing the pair"), FacingDot(Stand, PairMid) > 0.999f);
		TestTrue(TEXT("on the heavy's side"), FVector::DotProduct(Stand.Feet - PairMid, HeavyAt - PairMid) > 0.f);
		TestTrue(TEXT("with the heavy further off than the pair"), FVector::Dist2D(Stand.Feet, HeavyAt) > FVector::Dist2D(Stand.Feet, PairMid));
		TestTrue(TEXT("The log clause says what is in front of her"), Stand.Where.Contains(TEXT("pair")));
	}
	{
		// A group short of its heavy is no street fight.
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld)))
		{
			return false;
		}
		AddThug(TestWorld, FVector(0.f, 0.f, 100.f), EThugWeapon::Fists, true);
		AddThug(TestWorld, FVector(200.f, 0.f, 100.f), EThugWeapon::Pistol, true);
		FHawkeyeDemoStand Stand;
		FString Why;
		TestFalse(TEXT("Two thugs, no heavy: refused"), FHawkeyeDemoStart::PlanStreetFight(World, nullptr, Stand, Why));
		TestFalse(TEXT("and it says why"), Why.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartDoorStand, "Hawkeye.DemoStart.DoorStand", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartDoorStand::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDemoStartTest;
	FChapterSiteInteriorDoor Door;
	Door.Location = FVector(0.f, 0.f, 0.f);
	Door.Yaw = 0.f;
	Door.DisplayName = FText::FromString(TEXT("Santa's Grotto"));
	const FVector VolumeCentre(450.f, 0.f, 150.f);
	const FVector VolumeHalf(650.f, 650.f, 300.f);

	{
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld)))
		{
			return false;
		}
		FHawkeyeDemoStand Stand;
		FString Why;
		const bool bPlanned = FHawkeyeDemoStart::PlanDoorStand(World, nullptr, Door, VolumeCentre, VolumeHalf, Stand, Why);
		if (!TestTrue(FString::Printf(TEXT("The stand is planned (%s)"), *Why), bPlanned))
		{
			return false;
		}
		TestTrue(TEXT("Her feet are finite"), IsFinite(Stand.Feet));
		TestTrue(TEXT("on ground a downward trace finds"), StandsOnGround(World, Stand.Feet));
		TestFalse(TEXT("outside the arrival volume (a step forward enters it)"),
			FHawkeyeDemoStart::IsInsideBox2D(Stand.Feet, VolumeCentre, VolumeHalf, FHawkeyeDemoStart::VolumeMargin));
		const float Distance = FVector::Dist2D(Stand.Feet, Door.Location);
		TestTrue(TEXT("on the street between 6 and 25 m from the door"),
			Distance >= FHawkeyeDemoStart::DoorApproachMin - 1.f && Distance <= FHawkeyeDemoStart::DoorApproachMax + 1.f);
		TestTrue(TEXT("facing the door"), FacingDot(Stand, Door.Location) > 0.999f);
		TestTrue(TEXT("on the street side of it"), Stand.Feet.X > Door.Location.X);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoStartTestSight), false);
		TestFalse(TEXT("with nothing between her eyes and the door"),
			World->LineTraceSingleByChannel(Hit, Stand.Feet + FVector(0.f, 0.f, 150.f), Door.Location + FVector(60.f, 0.f, 120.f), ECC_Visibility, Params));
		TestTrue(TEXT("The log clause says what is in front of her"), Stand.Where.Contains(TEXT("Santa's Grotto")));

		// With no volume (an objective that has none) the nearest spot is the 6 m the clip is described with.
		FHawkeyeDemoStand Close;
		const bool bClose = FHawkeyeDemoStart::PlanDoorStand(World, nullptr, Door, Door.Location, FVector::ZeroVector, Close, Why);
		TestTrue(TEXT("Without a volume a stand is planned"), bClose);
		TestTrue(TEXT("6 m out, straight out of the door"), bClose && Close.Feet.Equals(FVector(600.f, 0.f, 0.f), 1.f));
	}
	{
		// A wall between every street spot and the door: nowhere to stand with the door in view, so it refuses rather than
		// stand her behind a wall.
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld))
			|| !TestNotNull(TEXT("a wall"), AddSlab(TestWorld, FVector2D(300.f, 0.f), 450.f, FVector(20.f, 4000.f, 300.f))))
		{
			return false;
		}
		FHawkeyeDemoStand Stand;
		FString Why;
		TestFalse(TEXT("A door behind a wall: refused"), FHawkeyeDemoStart::PlanDoorStand(World, nullptr, Door, VolumeCentre, VolumeHalf, Stand, Why));
		TestFalse(TEXT("and it says why"), Why.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartRoofStand, "Hawkeye.DemoStart.RoofStand", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartRoofStand::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDemoStartTest;
	FBossArena Arena;
	Arena.Outline = { FVector2D(-2000.f, -1500.f), FVector2D(2000.f, -1500.f), FVector2D(2000.f, 1500.f), FVector2D(-2000.f, 1500.f) };
	Arena.RoofZ = 1000.f;
	Arena.Centre = FVector(0.f, 0.f, 1000.f);
	Arena.ApproachAnchor = FVector(-2000.f, 0.f, 1100.f);
	Arena.BossSpot = FVector(800.f, 0.f, 1000.f);
	Arena.PlayerStart = FVector(-2600.f, 0.f, 0.f);
	const TArray<FVector> Crew = { FVector(500.f, 300.f, 1000.f), FVector(500.f, -300.f, 1000.f), FVector(700.f, 0.f, 1000.f), FVector(300.f, 0.f, 1000.f) };

	{
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld))
			|| !TestNotNull(TEXT("roof"), AddSlab(TestWorld, FVector2D::ZeroVector, Arena.RoofZ, FVector(2000.f, 1500.f, 50.f))))
		{
			return false;
		}
		// A crate on the first spot, 1 m tall: the entry goes round it.
		TestNotNull(TEXT("a crate"), AddSlab(TestWorld, FVector2D(-1750.f, 0.f), Arena.RoofZ + 100.f, FVector(80.f, 80.f, 50.f)));

		FHawkeyeDemoStand Stand;
		FString Why;
		bool bOnRoof = false;
		const bool bPlanned = FHawkeyeDemoStart::PlanRoofEntry(World, nullptr, Arena, Crew, Stand, Why, bOnRoof);
		if (!TestTrue(FString::Printf(TEXT("A roof entry is planned (%s)"), *Why), bPlanned))
		{
			return false;
		}
		TestTrue(TEXT("It is on the roof"), bOnRoof);
		TestTrue(TEXT("Her feet are finite"), IsFinite(Stand.Feet));
		TestTrue(TEXT("on the roof floor a downward trace finds"), StandsOnGround(World, Stand.Feet) && FMath::Abs(Stand.Feet.Z - Arena.RoofZ) < 1.f);
		TestTrue(TEXT("where the fight's watch counts her on the roof (it completes meet_tomas)"), UBossRules::IsOnRoof(Arena.Outline, Arena.RoofZ, Stand.Feet));
		TestTrue(TEXT("round the crate, in from the parapet"), Stand.Feet.X > -1670.f && FVector::Dist2D(Stand.Feet, Arena.ApproachAnchor) <= FHawkeyeDemoStart::RoofEntryMax + 1.f);
		bool bClear = true;
		for (const FVector& Member : Crew)
		{
			bClear &= FVector::Dist2D(Stand.Feet, Member) >= FHawkeyeDemoStart::RoofCrewClearance - 1.f;
		}
		TestTrue(TEXT("4 m or more from every one of the crew"), bClear);
		TestTrue(TEXT("facing the crew"), FacingDot(Stand, FVector(375.f, 0.f, 1000.f)) > 0.999f);
	}
	{
		// No roof under the outline (the world has only the street): the street stand below the anchor, looking up at it, and it says so.
		FHawkeyeTestWorld TestWorld;
		UWorld* World = TestWorld.Get();
		if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("ground"), AddGround(TestWorld)))
		{
			return false;
		}
		FHawkeyeDemoStand Stand;
		FString Why;
		bool bOnRoof = true;
		if (!TestTrue(TEXT("The street stand is planned"), FHawkeyeDemoStart::PlanRoofEntry(World, nullptr, Arena, Crew, Stand, Why, bOnRoof)))
		{
			return false;
		}
		TestFalse(TEXT("It is not on the roof"), bOnRoof);
		TestTrue(TEXT("on the street"), StandsOnGround(World, Stand.Feet) && FMath::Abs(Stand.Feet.Z) < 1.f);
		TestTrue(TEXT("Her feet are finite"), IsFinite(Stand.Feet));
		TestTrue(TEXT("facing the anchor"), FacingDot(Stand, Arena.ApproachAnchor) > 0.999f);
		TestTrue(TEXT("with the camera tilted up at it, within the opening's limits"), Stand.Pitch >= -10.f && Stand.Pitch <= 40.f);
		TestTrue(TEXT("and the log says it is the street stand"), Stand.Where.Contains(TEXT("street stand")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoStartNeedsAPlayer, "Hawkeye.DemoStart.NeedsAPlayer", HawkeyeDemoStartTest::Flags)

bool FHawkeyeDemoStartNeedsAPlayer::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	FString What;
	TestFalse(TEXT("No pawn to place: refused"), FHawkeyeDemoStart::Apply(TestWorld.Get(), EHawkeyeDemoClip::StreetFight, What));
	TestFalse(TEXT("and it says so"), What.IsEmpty());
	return true;
}

#endif
