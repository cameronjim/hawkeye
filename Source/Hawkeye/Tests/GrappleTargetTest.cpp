// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/ArrowDefinition.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/GrappleAnchor.h"
#include "World/GrappleArrowProjectile.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The grapple marker playtest regression (2026-09-28: "there are green diamonds but it no longer
 * tells you what button to press"). The key hint used to vanish after the fifth arrow, the diamond
 * stayed green with an empty quiver, and anchors whose zip line was blocked were marked, so a press
 * dropped her into a fire escape and left the arrow in an anchor she never reached. Now a green
 * diamond is always a zip that works with the key under it; everything else is grey.
 */
namespace HawkeyeGrappleTargetTest
{
	static UArrowDefinition* MakeGrappleArrow(UObject* Outer)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = 2;
		Arrow->Cap = 99;
		Arrow->OnHitEffect = EArrowHitEffect::Grapple;
		Arrow->bRecoverable = true;
		return Arrow;
	}

	static AHawkeyeAimTestCharacter* SpawnArcher(const FHawkeyeTestWorld& TestWorld, const FVector& Location, int32 Arrows)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		if (UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr)
		{
			Inventory->AddArrows(MakeGrappleArrow(Inventory), Arrows);
		}
		return Kate;
	}

	static AGrappleAnchor* SpawnAnchor(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AGrappleAnchor>(TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), Location, FRotator::ZeroRotator));
	}

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

	static void RunZip(UGrappleComponent* Grapple, float MaxSeconds)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(Grapple->GetOwner());
		for (float T = 0.f; Kate && Grapple->IsZipping() && T < MaxSeconds; T += 0.05f)
		{
			Kate->TestTickMovement(0.05f);
		}
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

	/** Fires at the current target and flies the arrow and the zip to the end. */
	static bool FireAndZip(UGrappleComponent* Grapple, UWorld* World)
	{
		if (!Grapple->TryFire())
		{
			return false;
		}
		if (AGrappleArrowProjectile* Arrow = FindArrowInFlight(World))
		{
			Arrow->Advance(5.f);
		}
		RunZip(Grapple, 5.f);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleMarkerLookTest, "Hawkeye.Grapple.MarkerLookByState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleMarkerLookTest::RunTest(const FString& Parameters)
{
	const FHawkeyeGrappleMarkerLook Ready = UHawkeyeHudWidget::GetGrappleMarkerLook(EGrappleTargetState::Ready);
	TestTrue(TEXT("Ready: drawn, green, with the key"), Ready.bVisible && Ready.bReady && Ready.bHint);
	const FHawkeyeGrappleMarkerLook Flying = UHawkeyeHudWidget::GetGrappleMarkerLook(EGrappleTargetState::ArrowInFlight);
	TestTrue(TEXT("Arrow in flight: grey, no key"), Flying.bVisible && !Flying.bReady && !Flying.bHint);
	TestFalse(TEXT("Too early to chain: not drawn"),
		UHawkeyeHudWidget::GetGrappleMarkerLook(EGrappleTargetState::TooEarlyToChain).bVisible);
	TestFalse(TEXT("No target: not drawn"), UHawkeyeHudWidget::GetGrappleMarkerLook(EGrappleTargetState::None).bVisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleHintEveryUse, "Hawkeye.Grapple.KeyHintAfterManyZips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleHintEveryUse::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTargetTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 200.f), 3);
	AGrappleAnchor* East = SpawnAnchor(TestWorld, FVector(1500.f, 0.f, 200.f));
	AGrappleAnchor* West = SpawnAnchor(TestWorld, FVector(-100.f, 0.f, 200.f));
	if (!Kate || !East || !West)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	// Eight zips back and forth: grapple arrows have no count, so every one is ready.
	for (int32 Zip = 0; Zip < 8; ++Zip)
	{
		const AGrappleAnchor* Next = Zip % 2 == 0 ? East : West;
		const FVector View = Kate->GetActorLocation() + FVector(0.f, 0.f, 20.f);
		Grapple->UpdateTarget(View, (Next->GetMarkerLocation() - View).GetSafeNormal());
		TestEqual(FString::Printf(TEXT("Zip %d: the marked anchor is ready"), Zip + 1), Grapple->GetTargetState(),
			EGrappleTargetState::Ready);
		TestTrue(FString::Printf(TEXT("Zip %d: so the key shows"), Zip + 1),
			UHawkeyeHudWidget::GetGrappleMarkerLook(Grapple->GetTargetState()).bHint);
		TestTrue(FString::Printf(TEXT("Zip %d fires and lands"), Zip + 1), FireAndZip(Grapple, TestWorld.Get()));
	}
	TestEqual(TEXT("Eight uses"), Grapple->GetUseCount(), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleInFlightGrey, "Hawkeye.Grapple.ArrowInFlightGreysTheMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleInFlightGrey::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTargetTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 200.f), 1);
	AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1500.f, 0.f, 200.f));
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector View = Kate->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestEqual(TEXT("Ready"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
	TestTrue(TEXT("It fires"), Grapple->TryFire());
	TestEqual(TEXT("In flight: the marker waits"), Grapple->GetTargetState(), EGrappleTargetState::ArrowInFlight);
	TestFalse(TEXT("Grey while it flies"), UHawkeyeHudWidget::GetGrappleMarkerLook(Grapple->GetTargetState()).bReady);
	TestFalse(TEXT("With no key under it"), UHawkeyeHudWidget::GetGrappleMarkerLook(Grapple->GetTargetState()).bHint);
	TestFalse(TEXT("And a second press does nothing"), Grapple->TryFire());

	AGrappleArrowProjectile* Arrow = FindArrowInFlight(TestWorld.Get());
	if (Arrow)
	{
		Arrow->Destroy();
	}
	Kate->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestTrue(TEXT("The anchor is still marked"), Grapple->GetTargetAnchor() == Anchor);
	TestEqual(TEXT("Once it is gone, ready again: there is no count to run out"), Grapple->GetTargetState(),
		EGrappleTargetState::Ready);
	TestTrue(TEXT("So the diamond is green"), UHawkeyeHudWidget::GetGrappleMarkerLook(Grapple->GetTargetState()).bReady);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleBlockedGrey, "Hawkeye.Grapple.BlockedLineIsGreyNotMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleBlockedGrey::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTargetTest;
	const FHawkeyeTestWorld TestWorld;
	// Street at z = 0, a 10 m building from x = 2000 with an anchor on its front parapet, and a
	// fire-escape landing in the air half way that the zip line runs into but the camera, up and
	// to the side, sees past.
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(2000.f, -1500.f, 0.f), FVector(2600.f, 1500.f, 1000.f));
	AHawkeyeTestBlocker* Landing = SpawnBox(TestWorld, FVector(900.f, -120.f, 560.f), FVector(1100.f, 120.f, 700.f));
	AGrappleAnchor* Straight = SpawnAnchor(TestWorld, FVector(2015.f, 0.f, 1000.f));
	AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 90.f), 5);
	if (!Kate || !Straight || !Landing)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector View(0.f, -500.f, 1200.f);
	const FVector Forward = (Straight->GetMarkerLocation() - View).GetSafeNormal();

	AActor* Blocker = nullptr;
	TestFalse(TEXT("The zip line to it is blocked"), Grapple->IsZipClear(Kate->GetActorLocation(), Straight, true, &Blocker));
	TestTrue(TEXT("By the landing"), Blocker == Landing);

	Grapple->UpdateTarget(View, Forward);
	TestNull(TEXT("A blocked anchor is never the target"), Grapple->GetTargetAnchor());
	TestEqual(TEXT("So nothing is marked green"), Grapple->GetTargetState(), EGrappleTargetState::None);
	TestTrue(TEXT("It is the grey one instead"), Grapple->GetBlockedAnchor() == Straight);
	TestTrue(TEXT("With the reason for the debug line"), Grapple->GetBlockedReason().Contains(Landing->GetName()));
	TestFalse(TEXT("A press fires nothing"), Grapple->TryFire());
	TestNull(TEXT("No arrow leaves"), FindArrowInFlight(TestWorld.Get()));

	// A second anchor a few degrees off with a clear line: that one is green, the blocked one stays grey.
	AGrappleAnchor* Clear = SpawnAnchor(TestWorld, FVector(2015.f, 700.f, 1000.f));
	Grapple->RebuildAnchorGrid();
	Grapple->UpdateTarget(View, Forward);
	TestTrue(TEXT("The clear anchor is the target"), Grapple->GetTargetAnchor() == Clear);
	TestEqual(TEXT("Ready"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
	TestTrue(TEXT("The nearer-the-middle blocked one is grey"), Grapple->GetBlockedAnchor() == Straight);
	TestTrue(TEXT("And the press zips to the green one"), FireAndZip(Grapple, TestWorld.Get()));
	TestTrue(TEXT("She lands on its roof"), Kate->GetActorLocation().Equals(Grapple->ComputeZipEnd(Clear), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleArrowsCome, "Hawkeye.Grapple.ArrowsLeaveAnchorsAfterBlocksAndChains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleArrowsCome::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleTargetTest;
	{
		// A zip that is blocked on the way (the clearance check off, as for a wall that moved in) reels its arrow out of the anchor.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 200.f), 5);
		AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(2000.f, 0.f, 200.f));
		SpawnBox(TestWorld, FVector(980.f, -200.f, -50.f), FVector(1020.f, 200.f, 550.f));
		UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		Grapple->bRequireClearZip = false;
		Grapple->UpdateTarget(Kate->GetActorLocation() + FVector(0.f, 0.f, 1000.f),
			(Anchor->GetMarkerLocation() - (Kate->GetActorLocation() + FVector(0.f, 0.f, 1000.f))).GetSafeNormal());
		TestTrue(TEXT("Marked with the check off"), Grapple->GetTargetAnchor() == Anchor);
		TestTrue(TEXT("Fired and flown"), FireAndZip(Grapple, TestWorld.Get()));
		TestTrue(TEXT("Stopped at the wall"), Kate->GetActorLocation().X < 1000.f);
		TestEqual(TEXT("Not left in the anchor"), Anchor->GetStuckArrowCount(), 0);
	}
	{
		// A chain: the arrow in the anchor she chained away from is reeled out; the new one stays until she lands.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 200.f), 5);
		AGrappleAnchor* First = SpawnAnchor(TestWorld, FVector(1800.f, 0.f, 500.f));
		AGrappleAnchor* Second = SpawnAnchor(TestWorld, FVector(1800.f, 1500.f, 500.f));
		UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		const FVector View = Kate->GetActorLocation();
		Grapple->UpdateTarget(View, (First->GetMarkerLocation() - View).GetSafeNormal());
		TestTrue(TEXT("First arrow fired"), Grapple->TryFire());
		if (AGrappleArrowProjectile* Arrow = FindArrowInFlight(TestWorld.Get()))
		{
			Arrow->Advance(5.f);
		}
		TestEqual(TEXT("It is in the first anchor"), First->GetStuckArrowCountFor(Kate), 1);
		for (int32 Step = 0; Step < 600 && Grapple->IsZipping() && Grapple->GetZipProgress() < 0.6f; ++Step)
		{
			Kate->TestTickMovement(1.f / 120.f);
		}
		const FVector Here = Kate->GetActorLocation();
		Grapple->UpdateTarget(Here, (Second->GetMarkerLocation() - Here).GetSafeNormal());
		TestEqual(TEXT("The chain target is ready"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
		TestTrue(TEXT("The chain fires"), Grapple->TryFire());
		TestEqual(TEXT("The first anchor's arrow came back"), First->GetStuckArrowCount(), 0);
		TestEqual(TEXT("One arrow out: in the second anchor"), Second->GetStuckArrowCountFor(Kate), 1);
		RunZip(Grapple, 5.f);
		Grapple->RecoverNearbyArrows();
		TestEqual(TEXT("Landed: tidied away"), Second->GetStuckArrowCount(), 0);
	}
	{
		// The partner walking past Kate's anchor does not pocket her arrow.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnArcher(TestWorld, FVector(0.f, 0.f, 200.f), 0);
		AHawkeyeAimTestCharacter* Clint = SpawnArcher(TestWorld, FVector(1560.f, 0.f, 290.f), 3);
		AGrappleAnchor* Anchor = SpawnAnchor(TestWorld, FVector(1500.f, 0.f, 200.f));
		Anchor->AddStuckArrow(nullptr, Kate);
		TestEqual(TEXT("Clint takes none of Kate's"), Clint->GetGrappleComponent()->RecoverNearbyArrows(), 0);
		TestEqual(TEXT("It is still there"), Anchor->GetStuckArrowCount(), 1);
		Kate->SetActorLocation(FVector(1560.f, 0.f, 290.f));
		TestEqual(TEXT("Kate takes hers"), Kate->GetGrappleComponent()->RecoverNearbyArrows(), 1);
		TestEqual(TEXT("Gone from the anchor"), Anchor->GetStuckArrowCount(), 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
