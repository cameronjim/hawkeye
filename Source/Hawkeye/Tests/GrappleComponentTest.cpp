#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/GrappleAnchor.h"
#include "World/GrappleArrowProjectile.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The grapple arrow rules from claude-docs/gameplay-semantics.md (PLANNED: traversal): anchors
 * within 2500 cm and 30 degrees of the camera forward, in sight; a straight zip to the landing point
 * (its speed is the movement component's: Hawkeye.Movement.Zip.*); chaining from 40% of the line,
 * redirected in the air; grapple arrows have no count (never
 * spent, never refused for want of one); a blocked zip drops the character.
 */
namespace HawkeyeGrappleTest
{
	/** A grapple arrow definition for slot 2, built in code. */
	static UArrowDefinition* MakeGrappleArrow(UObject* Outer, int32 Cap)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = 2;
		Arrow->Cap = Cap;
		Arrow->OnHitEffect = EArrowHitEffect::Grapple;
		Arrow->bRecoverable = true;
		return Arrow;
	}

	/** Kate with the grapple arrow in her quiver (GrappleArrows is ignored: the type has no count). */
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& Location,
		int32 GrappleArrows = 99)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		if (UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr)
		{
			Inventory->AddArrows(MakeGrappleArrow(Inventory, 99), GrappleArrows);
		}
		return Kate;
	}

	static AGrappleAnchor* SpawnAnchor(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AGrappleAnchor>(
			TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), Location, FRotator::ZeroRotator));
	}

	static AHawkeyeTestBlocker* SpawnBlocker(const FHawkeyeTestWorld& TestWorld, const FVector& Centre,
		const FVector& HalfExtent)
	{
		AHawkeyeTestBlocker* Blocker = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), Centre, FRotator::ZeroRotator));
		if (Blocker)
		{
			Blocker->SetExtent(HalfExtent);
		}
		return Blocker;
	}

	/** Ticks her movement in DeltaSeconds steps until the zip ends or MaxSeconds pass. Returns the steps run. */
	static int32 RunZip(AHawkeyeAimTestCharacter* Kate, float DeltaSeconds, float MaxSeconds)
	{
		int32 Steps = 0;
		for (float Elapsed = 0.f; Kate->IsZipping() && Elapsed < MaxSeconds; Elapsed += DeltaSeconds)
		{
			Kate->TestTickMovement(DeltaSeconds);
			++Steps;
		}
		return Steps;
	}

	/** A 400 cm roof with its top at TopZ under an anchor at Location, so a zip landing there has a floor. */
	static AHawkeyeTestBlocker* SpawnRoofUnder(const FHawkeyeTestWorld& TestWorld, const FVector& Location, float TopZ)
	{
		return SpawnBlocker(TestWorld, FVector(Location.X + 200.f, Location.Y, TopZ - 100.f), FVector(200.f, 200.f, 100.f));
	}

	static int32 ModeOf(const ACharacter* Character)
	{
		return static_cast<int32>(Character->GetCharacterMovement()->MovementMode.GetValue());
	}

	static AGrappleArrowProjectile* FindArrowInFlight(UWorld* World)
	{
		for (TActorIterator<AGrappleArrowProjectile> It(World); It; ++It)
		{
			if (!It->HasArrived())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleSelection, "Hawkeye.Grapple.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleSelection::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Kate spawned"), Kate)
		|| !TestNotNull(TEXT("She has a grapple"), Kate->GetGrappleComponent()))
	{
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();

	// Markers sit 20 cm above each anchor, so anchors at z = 0 are level with a view at z = 20.
	const FVector View(0.f, 0.f, 20.f);
	const FVector Forward = FVector::ForwardVector;
	const float Tan29 = FMath::Tan(FMath::DegreesToRadians(29.f));
	const float Tan31 = FMath::Tan(FMath::DegreesToRadians(31.f));

	AGrappleAnchor* AtFeet = SpawnAnchor(TestWorld, FVector(200.f, 0.f, 0.f));           // 0 deg, 200 cm
	AGrappleAnchor* Ahead = SpawnAnchor(TestWorld, FVector(2000.f, 0.f, 0.f));           // 0 deg, 2000 cm
	AGrappleAnchor* NearEdge = SpawnAnchor(TestWorld, FVector(2450.f, 300.f, 0.f));      // 7 deg, 2468 cm
	AGrappleAnchor* Side = SpawnAnchor(TestWorld, FVector(2000.f, 700.f, 0.f));          // 19 deg
	AGrappleAnchor* TooFar = SpawnAnchor(TestWorld, FVector(2600.f, 100.f, 0.f));        // 2 deg, 2602 cm
	AGrappleAnchor* Wide = SpawnAnchor(TestWorld, FVector(1000.f, 1000.f, 0.f));         // 45 deg
	AGrappleAnchor* Cone29 = SpawnAnchor(TestWorld, FVector(1500.f, -1500.f * Tan29, 0.f));
	AGrappleAnchor* Cone31 = SpawnAnchor(TestWorld, FVector(1500.f, -1500.f * Tan31, 0.f));
	if (!AtFeet || !Ahead || !NearEdge || !Side || !TooFar || !Wide || !Cone29 || !Cone31)
	{
		AddError(TEXT("Could not spawn the anchors."));
		return false;
	}

	TestEqual(TEXT("Range is 2500"), Grapple->Range, 2500.f);
	TestEqual(TEXT("Cone is 30 degrees"), Grapple->ConeDegrees, 30.f);
	TestTrue(TEXT("The smallest angle in range wins; one under 300 cm away does not count"),
		Grapple->SelectBestAnchor(View, Forward) == Ahead);

	Ahead->bEnabled = false;
	TestTrue(TEXT("A disabled anchor is skipped; next smallest angle, just inside 2500 cm"),
		Grapple->SelectBestAnchor(View, Forward) == NearEdge);

	NearEdge->bEnabled = false;
	TestTrue(TEXT("Out of range at 2 degrees loses to in range at 19"),
		Grapple->SelectBestAnchor(View, Forward) == Side);

	Side->bEnabled = false;
	TestTrue(TEXT("29 degrees is inside the cone"), Grapple->SelectBestAnchor(View, Forward) == Cone29);

	Cone29->bEnabled = false;
	TestNull(TEXT("31 and 45 degrees are outside it, 2600 cm is out of range"),
		Grapple->SelectBestAnchor(View, Forward));

	// Line of sight: a wall between the camera and the side anchor, clear of the straight-ahead line.
	Side->bEnabled = true;
	SpawnBlocker(TestWorld, FVector(1000.f, 350.f, 20.f), FVector(20.f, 100.f, 100.f));
	TestNull(TEXT("A wall in front of the only candidate leaves no target"), Grapple->SelectBestAnchor(View, Forward));

	Ahead->bEnabled = true;
	TestTrue(TEXT("An anchor the wall does not cover is still found"),
		Grapple->SelectBestAnchor(View, Forward) == Ahead);

	Grapple->UpdateTarget(View, Forward);
	TestTrue(TEXT("UpdateTarget stores the pick"), Grapple->GetTargetAnchor() == Ahead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleZipKinematics, "Hawkeye.Grapple.ZipKinematics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleZipKinematics::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	const FVector Start(0.f, 0.f, 200.f);
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Start);
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1800.f, 0.f, 500.f));
	if (!Kate || !Anchor || !SpawnRoofUnder(TestWorld, Anchor->GetActorLocation(), 500.f))
	{
		AddError(TEXT("Could not spawn Kate, an anchor and its roof."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Grapple->OnGrappleLanded.AddDynamic(Listener, &UHawkeyeTestListener::HandleGrappleLanded);
	const float HealthBefore = Kate->GetHealthComponent()->GetCurrentHealth();

	const FVector End = Grapple->ComputeZipEnd(Anchor);
	TestTrue(TEXT("The zip ends with her feet on the landing point, 60 cm inboard of the anchor"),
		End.Equals(FVector(1860.f, 0.f, 500.f + Kate->GetSimpleCollisionHalfHeight() + 2.f), 0.5f));

	TestTrue(TEXT("StartZip accepts a valid anchor"), Grapple->StartZip(Anchor));
	TestTrue(TEXT("Zipping"), Grapple->IsZipping());
	TestEqual(TEXT("In the zip movement mode"), ModeOf(Kate), static_cast<int32>(MOVE_Custom));
	TestEqual(TEXT("Its zip sub-mode"), static_cast<int32>(Movement->CustomMovementMode),
		static_cast<int32>(EHawkeyeCustomMovement::Zip));

	for (int32 Step = 0; Step < 5; ++Step)
	{
		Kate->TestTickMovement(0.1f);
	}
	const FVector Direction = (End - Start).GetSafeNormal();
	const float Length = FVector::Dist(Start, End);
	const float Along = FVector::DotProduct(Kate->GetActorLocation() - Start, Direction);
	TestTrue(TEXT("After 0.5 s she is part way along the straight line"),
		Along > 0.f && Kate->GetActorLocation().Equals(Start + Direction * Along, 0.5f));
	TestEqual(TEXT("Progress is the distance over the line length"), Grapple->GetZipProgress(), Along / Length, 0.001f);
	TestEqual(TEXT("Still zipping mid-line"), ModeOf(Kate), static_cast<int32>(MOVE_Custom));

	RunZip(Kate, 0.1f, 5.f);
	TestFalse(TEXT("The zip is over"), Grapple->IsZipping());
	TestTrue(TEXT("She is exactly on the end point"), Kate->GetActorLocation().Equals(End, 0.5f));
	TestEqual(TEXT("Landing on the roof sets Walking"), ModeOf(Kate), static_cast<int32>(MOVE_Walking));
	TestEqual(TEXT("Gravity was never touched"), Movement->GravityScale, 1.f);
	TestEqual(TEXT("OnGrappleLanded fired once"), Listener->GrappleLandedCount, 1);
	TestTrue(TEXT("For this anchor"), Listener->LastGrappleAnchor == Anchor);
	TestFalse(TEXT("A zip landing is not a roll: no camera dip, no speed cut"), Kate->IsRecoveringFromLanding());
	TestEqual(TEXT("A zip landing is a 0 cm landing"), Kate->GetLastFallHeight(), 0.f);
	TestEqual(TEXT("And costs no health"), Kate->GetHealthComponent()->GetCurrentHealth(), HealthBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleIgnoresAnchorBuilding, "Hawkeye.Grapple.ZipIgnoresAnchorBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleIgnoresAnchorBuilding::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 30.f, 0.f));
	// Two adjoining buildings with roofs at z = 200, sharing a wall along y = 0; the anchor stands
	// on the first one's front corner, right by the second. The line up from the street clips both
	// roof edges on the way in, which must not count as blocked.
	AHawkeyeTestBlocker* Building = SpawnBlocker(TestWorld, FVector(2200.f, 300.f, 100.f), FVector(200.f, 300.f, 100.f));
	AHawkeyeTestBlocker* Neighbour = SpawnBlocker(TestWorld, FVector(2200.f, -300.f, 100.f), FVector(200.f, 300.f, 100.f));
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(2010.f, 30.f, 200.f));
	if (!Kate || !Building || !Neighbour || !Anchor)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();

	Grapple->StartZip(Anchor);
	RunZip(Kate, 1.f / 60.f, 5.f);
	TestTrue(TEXT("She arrives on the roof"), Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Anchor), 0.5f));
	TestEqual(TEXT("Walking"), ModeOf(Kate), static_cast<int32>(MOVE_Walking));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleChain, "Hawkeye.Grapple.ChainRedirectsMidAirFrom40Percent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleChain::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 200.f));
	AGrappleAnchor* First = SpawnAnchor(TestWorld, FVector(1800.f, 0.f, 500.f));
	AGrappleAnchor* Second = SpawnAnchor(TestWorld, FVector(1800.f, 1500.f, 500.f));
	if (!Kate || !First || !Second || !SpawnRoofUnder(TestWorld, Second->GetActorLocation(), 500.f))
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	TestEqual(TEXT("The chain window opens at 40%"), Grapple->ChainMinProgress, 0.4f);
	TestEqual(TEXT("A chain arrow arrives at once"), Grapple->ChainArrowSpeed, 0.f);
	TestEqual(TEXT("The turn onto the new line takes 0.1 s"), Kate->GetHawkeyeMovement()->RedirectBlendSeconds, 0.1f);

	Grapple->StartZip(First);
	const float Length = Grapple->GetZipLength();
	const FVector FirstEnd = Grapple->ComputeZipEnd(First);
	auto AimAtSecond = [Grapple, Kate, Second]()
	{
		const FVector View = Kate->GetActorLocation();
		Grapple->UpdateTarget(View, (Second->GetMarkerLocation() - View).GetSafeNormal());
	};
	auto RunToProgress = [Kate, Grapple](float Progress)
	{
		for (int32 Step = 0; Step < 600 && Grapple->IsZipping() && Grapple->GetZipProgress() < Progress; ++Step)
		{
			Kate->TestTickMovement(1.f / 120.f);
		}
	};

	RunToProgress(0.35f);
	AimAtSecond();
	TestTrue(TEXT("The next anchor is targeted mid-zip"), Grapple->GetTargetAnchor() == Second);
	TestFalse(TEXT("At 35% a second arrow is refused"), Grapple->TryFire());
	TestTrue(TEXT("Still zipping to the first anchor"), Grapple->GetZipAnchor() == First);

	RunToProgress(0.5f);
	AimAtSecond();
	const FVector OldDirection = Grapple->GetZipDirection();
	const FVector AtPress = Kate->GetActorLocation();
	TestTrue(TEXT("At 50% the chain fires"), Grapple->TryFire());
	TestFalse(TEXT("The chain arrow is already there"), Grapple->IsArrowInFlight());
	TestEqual(TEXT("It stays in the new anchor"), Second->GetStuckArrowCount(), 1);
	TestTrue(TEXT("The zip target changed before landing"), Grapple->IsZipping() && Grapple->GetZipAnchor() == Second);
	TestTrue(TEXT("She is still in the air, nowhere near the first roof"),
		FVector::Dist(Kate->GetActorLocation(), FirstEnd) > 0.4f * Length);
	TestEqual(TEXT("Zipping, not landed"), ModeOf(Kate), static_cast<int32>(MOVE_Custom));
	TestEqual(TEXT("From the start of its new line"), Grapple->GetZipProgress(), 0.f);
	TestTrue(TEXT("From where she was, with no hop"), Grapple->GetZipLaunch().Equals(AtPress, 0.5f));
	TestTrue(TEXT("Turning onto the new line"), Grapple->IsRedirecting());
	TestEqual(TEXT("Counted as a mid-air redirect"), Grapple->GetRedirectCount(), 1);
	TestFalse(TEXT("A new line starts the 40% rule over"), Grapple->TryFire());

	// Half the blend: the direction is between the old line and the new one, not snapped.
	const FVector NewDirection = (Grapple->ComputeZipEnd(Second) - AtPress).GetSafeNormal();
	Kate->TestTickMovement(0.05f);
	const FVector Mid = Grapple->GetZipDirection();
	const float ToOld = FVector::DotProduct(Mid, OldDirection);
	const float ToNew = FVector::DotProduct(Mid, NewDirection);
	TestTrue(TEXT("Half way through the blend it has turned part of the way"),
		ToNew > FVector::DotProduct(OldDirection, NewDirection) + 0.05f && ToNew < 0.999f && ToOld < 0.999f);
	Kate->TestTickMovement(0.06f);
	TestFalse(TEXT("After 0.1 s the blend is done"), Grapple->IsRedirecting());
	TestTrue(TEXT("And she heads straight for the new landing point"),
		FVector::DotProduct(Grapple->GetZipDirection(),
			(Grapple->ComputeZipEnd(Second) - Kate->GetActorLocation()).GetSafeNormal()) > 0.999f);

	RunZip(Kate, 0.05f, 5.f);
	TestTrue(TEXT("She lands on the second anchor's roof"),
		Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Second), 0.5f));
	TestEqual(TEXT("Walking"), ModeOf(Kate), static_cast<int32>(MOVE_Walking));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleArrowCount, "Hawkeye.Grapple.ArrowsAreUnlimited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleArrowCount::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 200.f));
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1500.f, 0.f, 200.f));
	AGrappleAnchor* Beyond = SpawnAnchor(TestWorld, FVector(3000.f, 0.f, 200.f));
	if (!Kate || !Anchor || !Beyond)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector Forward = FVector::ForwardVector;

	TestFalse(TEXT("No target, no shot"), Grapple->TryFire());

	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	Inventory->SetArrowCount(2, 0);
	Grapple->UpdateTarget(Kate->GetActorLocation() + FVector(0.f, 0.f, 20.f), Forward);
	TestTrue(TEXT("The near anchor is the target"), Grapple->GetTargetAnchor() == Anchor);
	TestEqual(TEXT("Ready even after the count was set to none"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
	TestTrue(TEXT("It fires"), Grapple->TryFire());
	TestEqual(TEXT("The hint counts the use"), Grapple->GetUseCount(), 1);
	TestFalse(TEXT("A second press while it flies is refused"), Grapple->TryFire());

	AGrappleArrowProjectile* Arrow = FindArrowInFlight(TestWorld.Get());
	if (!TestNotNull(TEXT("A projectile was spawned"), Arrow))
	{
		return false;
	}
	Arrow->Advance(1.f);
	RunZip(Kate, 0.05f, 5.f);
	TestFalse(TEXT("Landed"), Grapple->IsZipping());

	// Standing by the anchor tidies the stuck arrow away; there is no count for it to go back into.
	TestEqual(TEXT("Standing by the anchor tidies the arrow away"), Grapple->RecoverNearbyArrows(), 1);
	TestEqual(TEXT("Gone from the anchor"), Anchor->GetStuckArrowCount(), 0);
	TestTrue(TEXT("The stuck projectile is removed"), !IsValid(Arrow) || Arrow->IsActorBeingDestroyed());
	TestEqual(TEXT("Nothing more to tidy"), Grapple->RecoverNearbyArrows(), 0);

	Grapple->UpdateTarget(Kate->GetActorLocation(), Forward);
	TestTrue(TEXT("The next anchor along is targeted"), Grapple->GetTargetAnchor() == Beyond);
	TestTrue(TEXT("And fires with no arrow picked up"), Grapple->TryFire());
	TestNotNull(TEXT("A projectile is on its way"), FindArrowInFlight(TestWorld.Get()));
	TestEqual(TEXT("The quiver still shows the infinity"), Inventory->GetSlotCountText(2).ToString(),
		FString(UInventoryComponent::UnlimitedGlyph));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleQSpendsFromQuiver, "Hawkeye.Grapple.QFiresTheQuiversGrappleArrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleQSpendsFromQuiver::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 200.f), 0);
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1500.f, 0.f, 200.f));
	UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
	if (!Inventory || !Anchor)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	Inventory->SetArrowCount(1, 30);
	TestEqual(TEXT("Standard arrows are nocked"), Inventory->GetActiveArrowSlot(), 1);

	Grapple->UpdateTarget(Kate->GetActorLocation() + FVector(0.f, 0.f, 20.f), FVector::ForwardVector);
	TestTrue(TEXT("Q fires at the marked anchor whatever is nocked"), Grapple->TryFire());
	TestEqual(TEXT("Slot 2 has no count to spend"), Inventory->GetSlotCountText(2).ToString(), FString(UInventoryComponent::UnlimitedGlyph));
	TestEqual(TEXT("Standard arrows untouched"), Inventory->GetArrowCount(1), 30);
	TestEqual(TEXT("And still nocked"), Inventory->GetActiveArrowSlot(), 1);
	AGrappleArrowProjectile* Arrow = FindArrowInFlight(TestWorld.Get());
	TestNotNull(TEXT("A grapple projectile left the bow"), Arrow);
	TestTrue(TEXT("Carrying the quiver's grapple definition"),
		Arrow && Arrow->GetArrowDefinition() == Inventory->GetArrowSlot(2).Arrow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleBlocked, "Hawkeye.Grapple.BlockedZipDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleBlocked::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 200.f));
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(2000.f, 0.f, 200.f));
	AHawkeyeTestBlocker* Wall = SpawnBlocker(TestWorld, FVector(1000.f, 0.f, 250.f), FVector(20.f, 200.f, 300.f));
	if (!Kate || !Anchor || !Wall)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Grapple->OnGrappleLanded.AddDynamic(Listener, &UHawkeyeTestListener::HandleGrappleLanded);
	Grapple->OnGrappleCancelled.AddDynamic(Listener, &UHawkeyeTestListener::HandleGrappleCancelled);

	Grapple->StartZip(Anchor);
	RunZip(Kate, 0.05f, 5.f);
	TestFalse(TEXT("The zip stopped"), Grapple->IsZipping());
	TestTrue(TEXT("At the wall, not through it"), Kate->GetActorLocation().X < 1000.f);
	TestEqual(TEXT("And she falls"), ModeOf(Kate), static_cast<int32>(MOVE_Falling));
	TestEqual(TEXT("OnGrappleCancelled fired"), Listener->GrappleCancelledCount, 1);
	TestEqual(TEXT("OnGrappleLanded did not"), Listener->GrappleLandedCount, 0);
	return true;
}

#endif
