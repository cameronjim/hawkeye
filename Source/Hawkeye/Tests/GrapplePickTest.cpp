#include "Combat/ArrowDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/GrappleAnchor.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The picker's rule (gameplay-semantics.md, "Grapple marker and arrows", revised 2026-09-29 after "many street-side
 * anchors are never targeted"): the marked anchor is the one nearest the middle of the screen that passes range,
 * cone, the screen's edges, sight and a clear zip; of those within PickTieDegrees of it the nearest to her wins. Every
 * candidate the budget allows is looked at: a visible anchor behind eight hidden ones nearer the middle used to go
 * unmarked. A marked anchor is always on the screen, so its diamond is always drawn.
 */
namespace HawkeyeGrapplePickTest
{
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator));
		if (UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr)
		{
			UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Inventory);
			Arrow->Slot = 2;
			Arrow->Cap = 99;
			Arrow->OnHitEffect = EArrowHitEffect::Grapple;
			Inventory->AddArrows(Arrow, 5);
		}
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	static AGrappleAnchor* SpawnAnchor(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AGrappleAnchor>(TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), Location, FRotator::ZeroRotator));
	}

	static void SpawnBox(const FHawkeyeTestWorld& TestWorld, const FVector& Min, const FVector& Max)
	{
		if (AHawkeyeTestBlocker* Box = Cast<AHawkeyeTestBlocker>(
				TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), (Min + Max) * 0.5f, FRotator::ZeroRotator)))
		{
			Box->SetExtent((Max - Min) * 0.5f);
		}
	}

	/** The anchor's origin for a marker Distance cm along the view from View, Degrees off it across (+Y). */
	static FVector Across(const FVector& View, float Distance, float Degrees, float MarkerHeight)
	{
		return View + FRotator(0.f, Degrees, 0.f).Vector() * Distance - FVector(0.f, 0.f, MarkerHeight);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrapplePickRuleTest, "Hawkeye.Grapple.PickRule.NearestMiddleTiesByDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrapplePickRuleTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrapplePickTest;
	// The rule on its own: the nearest the middle is 0.2 degrees; of those within 0.5 of it the nearest is at 0.6.
	const TArray<FVector2f> Options = { FVector2f(1.0f, 500.f), FVector2f(0.2f, 900.f), FVector2f(0.6f, 700.f), FVector2f(0.8f, 400.f) };
	TestEqual(TEXT("Nearest the middle, then the nearest within the tie"), UGrappleComponent::ChoosePick(Options, 0.5f), 2);
	TestEqual(TEXT("With no tie allowance, nearest the middle"), UGrappleComponent::ChoosePick(Options, 0.f), 1);
	TestEqual(TEXT("Nothing to choose from"), UGrappleComponent::ChoosePick({}, 0.5f), static_cast<int32>(INDEX_NONE));

	// In a world: one anchor dead ahead 15 m out, one 0.3 degrees off at 10 m, one 2 degrees off at 6 m.
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn Kate."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector View(0.f, 0.f, 160.f);
	const float Lift = GetDefault<AGrappleAnchor>()->MarkerHeight;
	AGrappleAnchor* Ahead = SpawnAnchor(TestWorld, Across(View, 1500.f, 0.f, Lift));
	AGrappleAnchor* Near = SpawnAnchor(TestWorld, Across(View, 1000.f, 0.3f, Lift));
	AGrappleAnchor* Aside = SpawnAnchor(TestWorld, Across(View, 600.f, 2.f, Lift));
	Grapple->RebuildAnchorGrid();
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestTrue(TEXT("The nearer anchor as near the middle is marked"), Grapple->GetTargetAnchor() == Near);
	TestFalse(TEXT("Not the one 2 degrees off, however near"), Grapple->GetTargetAnchor() == Aside);
	Grapple->PickTieDegrees = 0.f;
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestTrue(TEXT("With no tie allowance the one dead ahead"), Grapple->GetTargetAnchor() == Ahead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleOnScreenTest, "Hawkeye.Grapple.PickRule.MarkedAnchorIsOnScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleOnScreenTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrapplePickTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn Kate."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector View(0.f, 0.f, 160.f);
	// 90 degrees across at 16:9 is 29.4 degrees up: 28 degrees up is inside the 30 degree cone but not 2 degrees
	// inside the top of the screen; 28 degrees across is well inside the side.
	const FVector Up = View + FRotator(28.f, 0.f, 0.f).Vector() * 1200.f;
	const FVector Side = View + FRotator(0.f, 28.f, 0.f).Vector() * 1200.f;
	TestFalse(TEXT("28 degrees up is off the top of a 16:9 screen (with the margin)"), Grapple->IsOnScreen(View, FVector::ForwardVector, Up));
	TestTrue(TEXT("28 degrees across is on it"), Grapple->IsOnScreen(View, FVector::ForwardVector, Side));
	TestFalse(TEXT("Behind the lens is not"), Grapple->IsOnScreen(View, FVector::ForwardVector, View - FVector(500.f, 0.f, 0.f)));

	const float Lift = GetDefault<AGrappleAnchor>()->MarkerHeight;
	AGrappleAnchor* High = SpawnAnchor(TestWorld, Up - FVector(0.f, 0.f, Lift));
	Grapple->RebuildAnchorGrid();
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestNull(TEXT("An anchor in the cone but off the screen is not marked (its diamond could not be drawn)"), Grapple->GetTargetAnchor());
	Grapple->ViewAspectRatio = 4.f / 3.f;
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestTrue(TEXT("On a 4:3 screen, 36.9 degrees up, it is"), Grapple->GetTargetAnchor() == High);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleBehindHiddenTest, "Hawkeye.Grapple.PickRule.VisibleBehindHiddenIsMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleBehindHiddenTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrapplePickTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn Kate."));
		return false;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector View(0.f, 0.f, 160.f);
	const float Lift = GetDefault<AGrappleAnchor>()->MarkerHeight;
	// Ten anchors within 7 degrees of the middle behind a wall, and one 18 degrees across that the wall does not hide.
	SpawnBox(TestWorld, FVector(780.f, -100.f, 0.f), FVector(820.f, 200.f, 400.f));
	for (int32 K = 0; K < 10; ++K)
	{
		SpawnAnchor(TestWorld, FVector(1500.f, 20.f * K, View.Z - Lift));
	}
	AGrappleAnchor* Open = SpawnAnchor(TestWorld, FVector(1500.f, 500.f, View.Z - Lift));
	Grapple->RebuildAnchorGrid();
	Grapple->UpdateTarget(View, FVector::ForwardVector);
	TestTrue(TEXT("The one anchor in view is marked, whatever is hidden nearer the middle"), Grapple->GetTargetAnchor() == Open);
	TestEqual(TEXT("Ready"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
