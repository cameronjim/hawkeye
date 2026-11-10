#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Combat/ArrowEffects/BolaEffect.h"
#include "Combat/ArrowEffects/EmpPulse.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/ArrowEffects/HeldEffect.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/SpotLight.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHotbarWidget.h"
#include "UI/HawkeyeQuiverWheelWidget.h"
#include "UI/QuiverWheelMath.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The trick arrows from claude-docs/gameplay-semantics.md, "trick arrows": the fixed slot order,
 * the wheel's segment arithmetic, and each effect's numbers. Definitions are built with NewObject
 * and effects are driven through AdvanceEffect, so nothing here loads Content or needs a tick.
 */
namespace HawkeyeTrickArrowTest
{
	static constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static UArrowDefinition* MakeArrow(
		UObject* Outer, int32 Slot, int32 Cap, EArrowHitEffect Effect, float Damage, const TCHAR* Name)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = Slot;
		Arrow->Cap = Cap;
		Arrow->OnHitEffect = Effect;
		Arrow->Damage = Damage;
		Arrow->ShortName = FText::FromString(Name);
		Arrow->DisplayName = FText::FromString(Name);
		return Arrow;
	}

	/** The seven arrow types as create_weapon_data.py makes them, in slot order. */
	static TArray<UArrowDefinition*> MakeQuiverTypes(UObject* Outer)
	{
		return {
			MakeArrow(Outer, 1, 30, EArrowHitEffect::None, 40.f, TEXT("Arrow")),
			MakeArrow(Outer, 2, 6, EArrowHitEffect::Grapple, 0.f, TEXT("Grapple")),
			MakeArrow(Outer, 3, 4, EArrowHitEffect::Putty, 10.f, TEXT("Putty")),
			MakeArrow(Outer, 4, 4, EArrowHitEffect::Bola, 10.f, TEXT("Bola")),
			MakeArrow(Outer, 5, 3, EArrowHitEffect::Smoke, 0.f, TEXT("Smoke")),
			MakeArrow(Outer, 6, 3, EArrowHitEffect::EMP, 0.f, TEXT("EMP")),
			MakeArrow(Outer, 7, 2, EArrowHitEffect::Explosive, 80.f, TEXT("Explosive")),
		};
	}

	static FHawkeyeQuiverSlot Grant(UArrowDefinition* Arrow, int32 Count)
	{
		FHawkeyeQuiverSlot Slot;
		Slot.Arrow = Arrow;
		Slot.Count = Count;
		return Slot;
	}

	/** CH01's grant from create_mission_data.py: 30 standard, 6 grapple, 2 putty, 2 bola, 1 smoke, 1 EMP. */
	static TArray<FHawkeyeQuiverSlot> MakeChapterOneGrant(const TArray<UArrowDefinition*>& Types)
	{
		return {Grant(Types[0], 30), Grant(Types[1], 6), Grant(Types[2], 2), Grant(Types[3], 2), Grant(Types[4], 1),
			Grant(Types[5], 1)};
	}

	static AThugAIController* SpawnPossessedThug(
		const FHawkeyeTestWorld& TestWorld, const FVector& Location, AThugCharacter*& OutThug)
	{
		OutThug =
			Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		AThugAIController* Controller = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), Location, FRotator::ZeroRotator));
		if (OutThug && Controller)
		{
			Controller->Possess(OutThug);
		}
		return Controller;
	}

	static FHitResult MakeHit(AActor* Actor, const FVector& Point, const FVector& Normal = FVector::UpVector)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = Point;
		Hit.Location = Point;
		Hit.ImpactNormal = Normal;
		Hit.Normal = Normal;
		if (Actor)
		{
			Hit.HitObjectHandle = FActorInstanceHandle(Actor);
		}
		return Hit;
	}

	/** Spawns EffectClass at Point, gives it Hit, and activates it. */
	template <typename TEffect>
	static TEffect* SpawnEffect(const FHawkeyeTestWorld& TestWorld, const FHitResult& Hit,
		const UArrowDefinition* Arrow = nullptr, AActor* Shooter = nullptr)
	{
		TEffect* Effect =
			Cast<TEffect>(TestWorld.SpawnActor(TEffect::StaticClass(), Hit.ImpactPoint, FRotator::ZeroRotator));
		if (Effect)
		{
			Effect->InitEffect(Arrow, Shooter, Hit);
			Effect->Activate();
		}
		return Effect;
	}
}

// --- Slots and the wheel ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowSlotOrder, "Hawkeye.TrickArrow.SlotOrder", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowSlotOrder::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	TestEqual(TEXT("Seven quiver slots"), HawkeyeQuiverSlotCount, 7);
	TestEqual(TEXT("Six of them on number keys"), HawkeyeQuiverKeyedSlotCount, 6);

	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	const TArray<UArrowDefinition*> Types = MakeQuiverTypes(Inventory);
	Inventory->ApplyStartingQuiver(NewObject<UBowDefinition>(Inventory), MakeChapterOneGrant(Types));

	const EArrowHitEffect Order[] = {EArrowHitEffect::None, EArrowHitEffect::Grapple, EArrowHitEffect::Putty,
		EArrowHitEffect::Bola, EArrowHitEffect::Smoke, EArrowHitEffect::EMP};
	for (int32 Index = 1; Index < UE_ARRAY_COUNT(Order); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Effect %d is in slot %d"), static_cast<int32>(Order[Index]), Index + 1),
			Inventory->FindArrowSlotByEffect(Order[Index]), Index + 1);
	}

	// CH01's grant.
	const int32 Expected[] = {30, 6, 2, 2, 1, 1};
	for (int32 Slot = 1; Slot <= 6; ++Slot)
	{
		TestEqual(*FString::Printf(TEXT("CH01 grants %d in slot %d"), Expected[Slot - 1], Slot),
			Inventory->GetArrowCount(Slot), Expected[Slot - 1]);
	}
	TestTrue(TEXT("CH01 grants no explosive: slot 7 is empty"), Inventory->IsArrowSlotEmpty(7));
	TestFalse(TEXT("An empty explosive slot cannot be picked"), Inventory->SelectArrowSlot(7));

	TestEqual(TEXT("Putty tops out at its cap of 4"), Inventory->AddArrows(Types[2], 10), 2);
	TestEqual(TEXT("Explosive caps at 2"), Inventory->AddArrows(Types[6], 5), 2);
	TestTrue(TEXT("With explosives carried, slot 7 can be picked"), Inventory->SelectArrowSlot(7));
	TestTrue(TEXT("The wheel steps from 7 round to 1"),
		Inventory->SelectNextArrowSlot() && Inventory->GetActiveArrowSlot() == 1);
	TestTrue(
		TEXT("And back from 1 to 7"), Inventory->SelectPreviousArrowSlot() && Inventory->GetActiveArrowSlot() == 7);

	UHawkeyeHotbarWidget* Hotbar = NewObject<UHawkeyeHotbarWidget>();
	Hotbar->BindToInventory(Inventory);
	TestEqual(TEXT("Slot 6 is key 6"), Hotbar->GetSlotKeyText(6).ToString(), FString(TEXT("6")));
	TestEqual(
		TEXT("Slot 7 has no number key; the hint is Tab"), Hotbar->GetSlotKeyText(7).ToString(), FString(TEXT("Tab")));
	TestEqual(TEXT("Explosive shows count and cap"), Hotbar->GetSlotCountText(7).ToString(), FString(TEXT("2/2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowWheelSegmentMath, "Hawkeye.TrickArrow.WheelSegmentMath", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowWheelSegmentMath::RunTest(const FString& Parameters)
{
	const int32 N = HawkeyeQuiverSlotCount;
	TestEqual(
		TEXT("Straight up is segment 1 (standard)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(0.f, 1.f), N), 1);
	TestEqual(
		TEXT("A little left of up is still 1"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(-0.3f, 1.f), N), 1);
	TestEqual(TEXT("Further left of up is 7 (explosive)"),
		UQuiverWheelMath::ComputeWheelSegment(FVector2D(-0.6f, 1.f), N), 7);
	TestEqual(TEXT("Up and right is 2 (grapple)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(0.8f, 1.f), N), 2);
	TestEqual(TEXT("Right is 3 (putty)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(1.f, 0.f), N), 3);
	TestEqual(
		TEXT("Down and a bit right is 4 (bola)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(0.3f, -1.f), N), 4);
	TestEqual(
		TEXT("Down and a bit left is 5 (smoke)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(-0.3f, -1.f), N), 5);
	TestEqual(TEXT("Left is 6 (EMP)"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(-1.f, 0.f), N), 6);
	TestEqual(TEXT("Inside the dead zone is nothing"),
		UQuiverWheelMath::ComputeWheelSegment(FVector2D(10.f, 10.f), N, 30.f), 0);
	TestEqual(TEXT("Just past it counts"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(0.f, 31.f), N, 30.f), 1);
	TestEqual(TEXT("A zero vector is nothing"), UQuiverWheelMath::ComputeWheelSegment(FVector2D::ZeroVector, N), 0);
	TestEqual(TEXT("No segments, nothing"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(0.f, 1.f), 0), 0);

	// A boundary belongs to the segment clockwise of it: 4 segments, exactly up-right is 45 degrees.
	TestEqual(TEXT("Boundary goes clockwise"), UQuiverWheelMath::ComputeWheelSegment(FVector2D(1.f, 1.f), 4), 2);

	TestTrue(TEXT("Segment 3's middle is 3/7 of the way round"),
		FMath::IsNearlyEqual(UQuiverWheelMath::GetSegmentCentreDegrees(3, N), 360.f / 7.f * 2.f, 0.01f));
	const FVector2D Top = UQuiverWheelMath::GetSegmentLabelOffset(1, N, 100.f);
	TestTrue(TEXT("Segment 1's label is straight up the screen (y down)"), Top.Equals(FVector2D(0.f, -100.f), 0.01f));
	const FVector2D Two = UQuiverWheelMath::GetSegmentLabelOffset(2, N, 100.f);
	TestTrue(TEXT("Segment 2's label is up and to the right"), Two.X > 0.f && Two.Y < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowWheelSelects, "Hawkeye.TrickArrow.WheelSelects", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowWheelSelects::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
	AWorldSettings* Settings = TestWorld.Get()->GetWorldSettings();
	if (!Inventory || !Settings)
	{
		AddError(TEXT("No Kate with an inventory, or no world settings."));
		return false;
	}
	const TArray<UArrowDefinition*> Types = MakeQuiverTypes(Inventory);
	Inventory->ApplyStartingQuiver(NewObject<UBowDefinition>(Inventory), MakeChapterOneGrant(Types));

	Kate->OpenQuiverWheel();
	TestTrue(TEXT("The wheel opens"), Kate->IsQuiverWheelOpen());
	TestTrue(TEXT("Time slows to 0.2 while it is open"), FMath::IsNearlyEqual(Settings->TimeDilation, 0.2f, 0.001f));
	TestEqual(TEXT("A centred cursor highlights nothing"), Kate->GetQuiverWheelHighlight(), 0);

	Kate->SetQuiverWheelCursor(FVector2D(500.f, 0.f));
	TestTrue(TEXT("The cursor is held to the rim"),
		FMath::IsNearlyEqual(Kate->GetQuiverWheelCursor().Size(), Kate->QuiverWheelCursorRadius, 0.01f));
	TestEqual(TEXT("Pushed right, it highlights putty"), Kate->GetQuiverWheelHighlight(), 3);

	Kate->CloseQuiverWheel(true);
	TestFalse(TEXT("Release closes it"), Kate->IsQuiverWheelOpen());
	TestEqual(TEXT("And nocks the highlighted slot"), Inventory->GetActiveArrowSlot(), 3);
	TestTrue(TEXT("Time is back to normal"), FMath::IsNearlyEqual(Settings->TimeDilation, 1.f, 0.001f));

	// Onto the empty explosive slot: the release keeps what was nocked.
	Kate->OpenQuiverWheel();
	Kate->SetQuiverWheelCursor(FVector2D(-60.f, 100.f));
	TestEqual(TEXT("Up and left is explosive"), Kate->GetQuiverWheelHighlight(), 7);
	Kate->CloseQuiverWheel(true);
	TestEqual(TEXT("An empty segment nocks nothing"), Inventory->GetActiveArrowSlot(), 3);

	UHawkeyeQuiverWheelWidget* Wheel = NewObject<UHawkeyeQuiverWheelWidget>();
	Wheel->BindToInventory(Inventory);
	Wheel->SetWheelState(true, 4);
	TestEqual(TEXT("The wheel names each segment"), Wheel->GetSegmentNameText(4).ToString(), FString(TEXT("Bola")));
	TestEqual(TEXT("With its count and cap"), Wheel->GetSegmentCountText(4).ToString(), FString(TEXT("2/4")));
	TestEqual(TEXT("And the middle says what a release would nock"), Wheel->GetCentreText().ToString(),
		FString(TEXT("Bola")));
	TestTrue(TEXT("An empty segment shows no count"), Wheel->GetSegmentCountText(7).IsEmpty());
	TestNotEqual(TEXT("The highlighted segment stands out"), Wheel->GetSegmentColor(4), Wheel->GetSegmentColor(5));
	return true;
}

// --- Putty ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowPuttyHoldsAndReleases, "Hawkeye.TrickArrow.PuttyHoldsAndReleases", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowPuttyHoldsAndReleases::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Brain = SpawnPossessedThug(TestWorld, FVector::ZeroVector, Thug);
	if (!Thug || !Brain)
	{
		AddError(TEXT("Failed to spawn a possessed thug."));
		return false;
	}

	AHeldEffect* Putty = SpawnEffect<AHeldEffect>(TestWorld, MakeHit(Thug, Thug->GetActorLocation()));
	if (!Putty)
	{
		AddError(TEXT("Failed to spawn the putty."));
		return false;
	}
	TestEqual(TEXT("A direct hit targets the thug"), Putty->GetTarget(), Thug);
	TestEqual(TEXT("First he reels"), Putty->GetPhase(), EHeldPhase::Stagger);
	TestFalse(TEXT("Not held yet"), Brain->IsHeld());

	Putty->AdvanceEffect(Thug->StaggerSeconds);
	TestEqual(TEXT("After his stagger the putty sets"), Putty->GetPhase(), EHeldPhase::Held);
	TestTrue(TEXT("His brain is paused"), Brain->IsHeld());
	TestEqual(TEXT("He cannot move"), static_cast<int32>(Thug->GetCharacterMovement()->MovementMode),
		static_cast<int32>(MOVE_None));
	TestTrue(TEXT("For 4 s"), FMath::IsNearlyEqual(Putty->GetPhaseRemaining(), 4.f, 0.001f));

	// Held means nothing: a sighting that would alert him does not.
	Brain->ReportStimulus(EStimulusKind::Sight, FVector(500.f, 0.f, 0.f), true);
	ThinkThug(Brain, 1.f);
	TestNotEqual(TEXT("A held thug does not confirm a sighting"), Thug->GetAlertState(), EThugAlertState::Alerted);

	Putty->AdvanceEffect(3.9f);
	TestTrue(TEXT("Still held at 3.9 s"), Brain->IsHeld());
	Putty->AdvanceEffect(0.2f);
	TestFalse(TEXT("Released after 4 s"), Brain->IsHeld());
	TestEqual(TEXT("And walking again"), static_cast<int32>(Thug->GetCharacterMovement()->MovementMode),
		static_cast<int32>(MOVE_Walking));
	TestFalse(TEXT("The putty is gone"), IsValid(Putty));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowPuttySurface, "Hawkeye.TrickArrow.PuttySurface", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowPuttySurface::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Brain = SpawnPossessedThug(TestWorld, FVector::ZeroVector, Thug);
	AActor* Wall = TestWorld.SpawnActor(AActor::StaticClass(), FVector(0.f, 400.f, 0.f), FRotator::ZeroRotator);
	if (!Thug || !Brain)
	{
		AddError(TEXT("Failed to spawn a possessed thug."));
		return false;
	}

	// On the wall 150 cm from him: within 200, so he is held all the same.
	AHeldEffect* Near =
		SpawnEffect<AHeldEffect>(TestWorld, MakeHit(Wall, FVector(0.f, 150.f, 0.f), -FVector::RightVector));
	TestEqual(TEXT("Putty 150 cm away finds him"), Near ? Near->GetTarget() : nullptr, Thug);
	if (Near)
	{
		Near->AdvanceEffect(Thug->StaggerSeconds);
	}
	TestTrue(TEXT("And holds him"), Brain->IsHeld());

	// A second putty on a held thug restarts his hold rather than adding another.
	if (Near)
	{
		Near->AdvanceEffect(3.f);
	}
	AHeldEffect* Again = SpawnEffect<AHeldEffect>(TestWorld, MakeHit(Thug, Thug->GetActorLocation()));
	TestFalse(TEXT("The second putty folds into the first"), IsValid(Again));
	TestTrue(TEXT("Which has 4 s again"), Near && FMath::IsNearlyEqual(Near->GetPhaseRemaining(), 4.f, 0.001f));
	if (Near)
	{
		Near->AdvanceEffect(4.1f);
	}
	TestFalse(TEXT("Released"), Brain->IsHeld());

	// 300 cm away: nobody to hold, a blob on the wall for 10 s.
	AHeldEffect* Far =
		SpawnEffect<AHeldEffect>(TestWorld, MakeHit(Wall, FVector(0.f, 300.f, 0.f), -FVector::RightVector));
	if (!Far)
	{
		AddError(TEXT("Failed to spawn the far putty."));
		return false;
	}
	TestEqual(TEXT("Putty 300 cm away is a blob on the wall"), Far->GetPhase(), EHeldPhase::SurfaceBlob);
	TestNull(TEXT("Holding nobody"), Far->GetTarget());
	TestFalse(TEXT("And he is free"), Brain->IsHeld());
	Far->AdvanceEffect(9.9f);
	TestTrue(TEXT("Still there at 9.9 s"), IsValid(Far));
	Far->AdvanceEffect(0.2f);
	TestFalse(TEXT("Gone after 10 s"), IsValid(Far));
	return true;
}

// --- Bola ----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowBolaTrips, "Hawkeye.TrickArrow.BolaTrips", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowBolaTrips::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the thug."));
		return false;
	}

	ABolaEffect* Bola = SpawnEffect<ABolaEffect>(TestWorld, MakeHit(Thug, Thug->GetActorLocation()));
	TestTrue(TEXT("The bola trips him"), Bola && Bola->DidTrip());
	TestTrue(TEXT("He is down"), Thug->IsKnockedDown());
	TestTrue(TEXT("For 2.5 s"), FMath::IsNearlyEqual(Thug->GetKnockdownRemaining(), 2.5f, 0.001f));
	Thug->UpdateKnockdown(2.4f);
	TestTrue(TEXT("Still down at 2.4 s"), Thug->IsKnockedDown());
	Thug->UpdateKnockdown(0.2f);
	TestFalse(TEXT("Up after 2.5 s"), Thug->IsKnockedDown());

	ABolaEffect* Miss = SpawnEffect<ABolaEffect>(TestWorld, MakeHit(nullptr, FVector(500.f, 0.f, 0.f)));
	TestTrue(TEXT("A miss trips nobody"), Miss && !Miss->DidTrip());
	return true;
}

// --- Smoke ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowSmokeBlinds, "Hawkeye.TrickArrow.SmokeBlindsByGeometry", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowSmokeBlinds::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	using FSub = UArrowEffectsSubsystem;
	TestTrue(TEXT("A line through the middle is blocked"),
		FSub::SegmentIntersectsSphere(
			FVector(-1000.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector::ZeroVector, 500.f));
	TestTrue(TEXT("A line grazing inside the edge is blocked"),
		FSub::SegmentIntersectsSphere(
			FVector(-1000.f, 490.f, 0.f), FVector(1000.f, 490.f, 0.f), FVector::ZeroVector, 500.f));
	TestFalse(TEXT("A line passing outside is clear"),
		FSub::SegmentIntersectsSphere(
			FVector(-1000.f, 510.f, 0.f), FVector(1000.f, 510.f, 0.f), FVector::ZeroVector, 500.f));
	TestFalse(TEXT("A line that stops short is clear"),
		FSub::SegmentIntersectsSphere(
			FVector(-2000.f, 0.f, 0.f), FVector(-600.f, 0.f, 0.f), FVector::ZeroVector, 500.f));
	TestTrue(TEXT("A line starting inside is blocked"),
		FSub::SegmentIntersectsSphere(
			FVector(100.f, 0.f, 0.f), FVector(3000.f, 3000.f, 0.f), FVector::ZeroVector, 500.f));

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Brain = SpawnPossessedThug(TestWorld, FVector::ZeroVector, Thug);
	if (!Thug || !Brain)
	{
		AddError(TEXT("Failed to spawn a possessed thug."));
		return false;
	}

	// The cloud halfway between him and Kate, 10 m and 20 m out.
	ASmokeCloud* Cloud = SpawnEffect<ASmokeCloud>(TestWorld, MakeHit(nullptr, FVector(1000.f, 0.f, -100.f)));
	if (!Cloud)
	{
		AddError(TEXT("Failed to spawn the smoke."));
		return false;
	}
	Cloud->AdvanceEffect(1.f);
	TestTrue(
		TEXT("Full 500 cm once it has billowed out"), FMath::IsNearlyEqual(Cloud->GetCurrentRadius(), 500.f, 0.1f));

	const FVector Kate(2000.f, 0.f, 0.f);
	Brain->ReportStimulus(EStimulusKind::Sight, Kate, true);
	TestTrue(TEXT("Seeing her through the cloud blinds him"), Brain->IsBlinded());
	ThinkThug(Brain, 1.f);
	TestEqual(TEXT("A blinded thug confirms nothing"), Thug->GetAlertState(), EThugAlertState::Calm);

	// Crouched inside it she is gone to eyes and ears; standing, only the geometry counts.
	ACharacter* Crouched = Cast<ACharacter>(
		TestWorld.SpawnActor(ACharacter::StaticClass(), FVector(1000.f, 100.f, 0.f), FRotator::ZeroRotator));
	if (Crouched)
	{
		Crouched->bIsCrouched = true;
		TestTrue(TEXT("Crouched in the smoke is hidden"), AThugAIController::IsHiddenInSmoke(Crouched));
		Crouched->bIsCrouched = false;
		TestFalse(TEXT("Standing in it is not hidden by crouching"), AThugAIController::IsHiddenInSmoke(Crouched));
		Crouched->SetActorLocation(FVector(2500.f, 0.f, 0.f));
		Crouched->bIsCrouched = true;
		TestFalse(TEXT("Crouched outside it is not hidden"), AThugAIController::IsHiddenInSmoke(Crouched));
	}

	// The cloud lasts 8 s; once it clears he sees her and a sustained look alerts him.
	Cloud->AdvanceEffect(6.9f);
	TestTrue(TEXT("Still blocking at 7.9 s"), Cloud->IsBlocking());
	Cloud->AdvanceEffect(0.2f);
	TestFalse(TEXT("Gone after 8 s"), IsValid(Cloud));
	ThinkThug(Brain, 0.7f);
	TestFalse(TEXT("With the smoke gone he can see"), Brain->IsBlinded());
	TestEqual(TEXT("And the sighting alerts him"), Thug->GetAlertState(), EThugAlertState::Alerted);
	return true;
}

// --- EMP -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowEmpLightsAndJam, "Hawkeye.TrickArrow.EmpLightsAndJam", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowEmpLightsAndJam::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	FHawkeyeTestWorld TestWorld;
	UArrowEffectsSubsystem* Effects = TestWorld.Get()->GetSubsystem<UArrowEffectsSubsystem>();
	auto SpawnLamp = [&TestWorld](const FVector& Location)
	{
		ASpotLight* Lamp =
			Cast<ASpotLight>(TestWorld.SpawnActor(ASpotLight::StaticClass(), Location, FRotator::ZeroRotator));
		if (Lamp)
		{
			Lamp->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			Lamp->Tags.Add(UArrowEffectsSubsystem::LampTag);
		}
		return Lamp;
	};
	// Lamp heads are 7 m up: the radius is measured across the ground.
	ASpotLight* Near = SpawnLamp(FVector(400.f, 300.f, 670.f));
	ASpotLight* Far = SpawnLamp(FVector(900.f, 0.f, 670.f));
	AThugCharacter* Gunner = nullptr;
	AThugAIController* GunnerBrain = SpawnPossessedThug(TestWorld, FVector(200.f, 0.f, 0.f), Gunner);
	AThugCharacter* Batter = nullptr;
	AThugAIController* BatterBrain = SpawnPossessedThug(TestWorld, FVector(0.f, 200.f, 0.f), Batter);
	if (!Effects || !Near || !Far || !GunnerBrain || !BatterBrain)
	{
		AddError(TEXT("Failed to set up the EMP scene."));
		return false;
	}
	Gunner->Weapon = EThugWeapon::Pistol;
	Batter->Weapon = EThugWeapon::Bat;

	AEmpPulse* Pulse = SpawnEffect<AEmpPulse>(TestWorld, MakeHit(nullptr, FVector::ZeroVector));
	TestTrue(TEXT("The pulse ran"), Pulse != nullptr);
	TestFalse(TEXT("The lamp 500 cm away goes out"), Near->GetLightComponent()->IsVisible());
	TestTrue(TEXT("The lamp 900 cm away stays on"), Far->GetLightComponent()->IsVisible());
	TestTrue(TEXT("The subsystem knows it is out"), Effects->IsLampDisabled(Near));
	TestTrue(TEXT("The gunner's pistol jams"), GunnerBrain->IsJammed());
	TestTrue(TEXT("For 6 s"), FMath::IsNearlyEqual(GunnerBrain->GetJamRemaining(), 6.f, 0.001f));
	TestFalse(TEXT("A bat has nothing to jam"), BatterBrain->IsJammed());

	// A jammed gunner, alerted with Kate in range, does not fire.
	AActor* Kate = TestWorld.SpawnActor(ACharacter::StaticClass(), FVector(800.f, 0.f, 0.f), FRotator::ZeroRotator);
	GunnerBrain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 3.f);
	GunnerBrain->SetTarget(Kate);
	const int32 Ammo = Gunner->GetWeaponComponent()->CurrentAmmo;
	ThinkThug(GunnerBrain, 1.f);
	ThinkThug(GunnerBrain, 1.f);
	TestEqual(TEXT("Jammed: no shot"), Gunner->GetWeaponComponent()->CurrentAmmo, Ammo);
	TestFalse(TEXT("Jammed: no telegraph either"), GunnerBrain->IsTelegraphing());
	ThinkThug(GunnerBrain, 3.9f);
	TestTrue(TEXT("Still jammed at 5.9 s"), GunnerBrain->IsJammed());
	ThinkThug(GunnerBrain, 0.2f);
	TestFalse(TEXT("Clear after 6 s"), GunnerBrain->IsJammed());
	// Six seconds with nothing seen or heard lost him the target; one more shot from Kate brings him back.
	GunnerBrain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 3.f);
	ThinkThug(GunnerBrain, 1.f);
	TestTrue(TEXT("Unjammed, he telegraphs a burst"), GunnerBrain->IsTelegraphing());
	ThinkThug(GunnerBrain, 1.f);
	TestEqual(TEXT("And he shoots again"), Gunner->GetWeaponComponent()->CurrentAmmo, Ammo - 1);

	Effects->Tick(19.9f);
	TestFalse(TEXT("Still dark at 19.9 s"), Near->GetLightComponent()->IsVisible());
	Effects->Tick(0.2f);
	TestTrue(TEXT("Back on after 20 s"), Near->GetLightComponent()->IsVisible());
	TestFalse(TEXT("And no longer tracked"), Effects->IsLampDisabled(Near));
	return true;
}

// --- Explosive -----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTrickArrowExplosiveFalloff, "Hawkeye.TrickArrow.ExplosiveFalloff", HawkeyeTrickArrowTest::Flags)

bool FHawkeyeTrickArrowExplosiveFalloff::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickArrowTest;
	TestEqual(TEXT("80 at the centre"), AExplosiveBlast::ComputeFalloffDamage(80.f, 0.f, 400.f), 80.f);
	TestEqual(TEXT("40 halfway out"), AExplosiveBlast::ComputeFalloffDamage(80.f, 200.f, 400.f), 40.f);
	TestEqual(TEXT("20 at 300 cm"), AExplosiveBlast::ComputeFalloffDamage(80.f, 300.f, 400.f), 20.f);
	TestEqual(TEXT("0 at the edge"), AExplosiveBlast::ComputeFalloffDamage(80.f, 400.f, 400.f), 0.f);
	TestEqual(TEXT("0 outside"), AExplosiveBlast::ComputeFalloffDamage(80.f, 600.f, 400.f), 0.f);

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Close = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator));
	AThugCharacter* Outside = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(-450.f, 0.f, 0.f), FRotator::ZeroRotator));
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 200.f, 0.f), FRotator::ZeroRotator));
	if (!Close || !Outside || !Kate)
	{
		AddError(TEXT("Failed to set up the blast scene."));
		return false;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();
	const float KateBefore = KateHealth->GetCurrentHealth();

	UArrowDefinition* Arrow = MakeArrow(Kate, 7, 2, EArrowHitEffect::Explosive, 80.f, TEXT("Explosive"));
	AExplosiveBlast* Blast =
		SpawnEffect<AExplosiveBlast>(TestWorld, MakeHit(nullptr, FVector::ZeroVector), Arrow, Kate);
	TestTrue(TEXT("The blast ran"), Blast != nullptr);
	TestEqual(TEXT("A thug 100 cm out takes 60"), Close->GetHealthComponent()->GetCurrentHealth(), 40.f);
	TestTrue(TEXT("And is knocked down"), Close->IsKnockedDown());
	TestTrue(TEXT("For 2 s"), FMath::IsNearlyEqual(Close->GetKnockdownRemaining(), 2.f, 0.001f));
	TestEqual(TEXT("A thug 450 cm out is untouched"), Outside->GetHealthComponent()->GetCurrentHealth(), 100.f);
	TestFalse(TEXT("And stays up"), Outside->IsKnockedDown());
	TestEqual(TEXT("Kate 200 cm from her own blast takes 40"), KateHealth->GetCurrentHealth(), KateBefore - 40.f);
	TestTrue(TEXT("And her camera shakes"), Kate->GetHitShakeAlpha() > 0.f);
	TestEqual(TEXT("Two victims"), Blast ? Blast->GetVictimCount() : 0, 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
