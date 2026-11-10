#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/TakedownComponent.h"
#include "Dialogue/BanterComponent.h"
#include "Engine/World.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHudWidget.h"
#include "UObject/Package.h"
#include "World/GrappleAnchor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyePartnerTest
{
	static AHawkeyeCharacter* SpawnHawkeye(const FHawkeyeTestWorld& TestWorld, const FVector& Location, const TCHAR* Name)
	{
		AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		if (Character)
		{
			Character->CharacterName = FText::FromString(Name);
		}
		return Character;
	}

	static AHawkeyePartnerController* SpawnPartner(const FHawkeyeTestWorld& TestWorld, AHawkeyeCharacter* Partner,
		AHawkeyeCharacter* Lead)
	{
		AHawkeyePartnerController* Brain = Cast<AHawkeyePartnerController>(
			TestWorld.SpawnActor(AHawkeyePartnerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Brain)
		{
			Brain->SetTestTimeSeconds(0.0);
			Brain->Possess(Partner);
			Brain->SetLeader(Lead);
		}
		return Brain;
	}

	/** Kate and Clint, Kate played by PC, Clint driven by the returned partner. */
	struct FPair
	{
		AHawkeyePauseTestController* PC = nullptr;
		AHawkeyeCharacter* Kate = nullptr;
		AHawkeyeCharacter* Clint = nullptr;
		AHawkeyePartnerController* Brain = nullptr;

		bool IsValid() const { return PC && Kate && Clint && Brain; }
	};

	static FPair SpawnPair(const FHawkeyeTestWorld& TestWorld)
	{
		FPair Pair;
		Pair.PC = Cast<AHawkeyePauseTestController>(TestWorld.SpawnActor(
			AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		Pair.Kate = SpawnHawkeye(TestWorld, FVector(0.f, 0.f, 100.f), TEXT("Kate"));
		Pair.Clint = SpawnHawkeye(TestWorld, FVector(-500.f, 0.f, 100.f), TEXT("Clint"));
		if (Pair.PC && Pair.Kate)
		{
			Pair.PC->Possess(Pair.Kate);
			Pair.PC->bAllowSwitchingOverride = true;
		}
		Pair.Brain = Pair.Clint ? SpawnPartner(TestWorld, Pair.Clint, Pair.Kate) : nullptr;
		return Pair;
	}

	/** An actor with a health component, so SelectAttackTarget can tell the living from the dead. */
	static AActor* SpawnTarget(const FHawkeyeTestWorld& TestWorld, const FVector& Location, UHealthComponent** OutHealth = nullptr)
	{
		// The solid one: it has a root component, so it has a location to be near or far.
		AActor* Target = TestWorld.SpawnActor(AHawkeyeTestSolidTakedownTarget::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Target)
		{
			return nullptr;
		}
		UHealthComponent* Health = NewObject<UHealthComponent>(Target, TEXT("Health"));
		Target->AddInstanceComponent(Health);
		Health->RegisterComponent();
		if (OutHealth)
		{
			*OutHealth = Health;
		}
		return Target;
	}

	static UArrowDefinition* MakeArrow(int32 Slot, int32 Cap, EArrowHitEffect Effect, const TCHAR* Name)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UArrowDefinition::StaticClass(), FName(Name)));
		Arrow->Slot = Slot;
		Arrow->Cap = Cap;
		Arrow->OnHitEffect = Effect;
		return Arrow;
	}

	static FHawkeyeArrowGrant Grant(UArrowDefinition* Arrow, int32 Count)
	{
		FHawkeyeArrowGrant Result;
		Result.Arrow = Arrow;
		Result.Count = Count;
		return Result;
	}

	static FHawkeyeDialogueLine Line(const TCHAR* Speaker, EHawkeyeBanterSituation Situation, const TCHAR* Text)
	{
		FHawkeyeDialogueLine Row;
		Row.Speaker = FName(Speaker);
		Row.Situation = Situation;
		Row.Text = FText::FromString(Text);
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerFollowBand, "Hawkeye.Partner.FollowBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerFollowBand::RunTest(const FString& Parameters)
{
	const FVector Lead(1000.f, 2000.f, 100.f);
	FVector Goal;

	for (const float Distance : { 400.f, 500.f, 800.f })
	{
		const bool bMove = AHawkeyePartnerController::ComputeFollowGoal(Lead, Lead + FVector(Distance, 0.f, 0.f), 400.f,
			800.f, 600.f, Goal);
		TestFalse(FString::Printf(TEXT("%.0f cm is inside the 400-800 band: he holds"), Distance), bMove);
	}

	TestTrue(TEXT("900 cm out: he follows"),
		AHawkeyePartnerController::ComputeFollowGoal(Lead, Lead + FVector(0.f, 900.f, 0.f), 400.f, 800.f, 600.f, Goal));
	TestEqual(TEXT("to 600 cm from her"), static_cast<float>(FVector::Dist2D(Goal, Lead)), 600.f, 1.f);
	TestTrue(TEXT("on the side he came from"), Goal.Y > Lead.Y);

	TestTrue(TEXT("200 cm: too close, he steps back"),
		AHawkeyePartnerController::ComputeFollowGoal(Lead, Lead - FVector(200.f, 0.f, 0.f), 400.f, 800.f, 600.f, Goal));
	TestEqual(TEXT("out to 600 cm"), static_cast<float>(FVector::Dist2D(Goal, Lead)), 600.f, 1.f);
	TestTrue(TEXT("behind her, where he was"), Goal.X < Lead.X);

	TestTrue(TEXT("Height does not count: 500 cm across is in the band from a roof"),
		!AHawkeyePartnerController::ComputeFollowGoal(Lead, Lead + FVector(500.f, 0.f, -1500.f), 400.f, 800.f, 600.f, Goal));

	TestFalse(TEXT("Catch-up waits for the 5 s"), AHawkeyePartnerController::ShouldGrappleCatchUp(900.f, 4.9f, 300.f, 5.f));
	TestTrue(TEXT("and then zips"), AHawkeyePartnerController::ShouldGrappleCatchUp(900.f, 5.f, 300.f, 5.f));
	TestFalse(TEXT("but never for a small height gap"), AHawkeyePartnerController::ShouldGrappleCatchUp(250.f, 60.f, 300.f, 5.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerAttackTargetSelection, "Hawkeye.Partner.AttackTargetRecentlyHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerAttackTargetSelection::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	const FHawkeyeTestWorld TestWorld;
	FPair Pair = SpawnPair(TestWorld);
	UHealthComponent* HealthB = nullptr;
	AActor* ThugA = SpawnTarget(TestWorld, FVector(1500.f, 0.f, 100.f));
	AActor* ThugB = SpawnTarget(TestWorld, FVector(1500.f, 800.f, 100.f), &HealthB);
	AActor* Untouched = SpawnTarget(TestWorld, FVector(300.f, 0.f, 100.f));
	if (!Pair.IsValid() || !ThugA || !ThugB || !Untouched || !HealthB)
	{
		AddError(TEXT("Could not spawn the pair and the thugs."));
		return false;
	}

	AHawkeyePartnerController* Brain = Pair.Brain;
	TestNull(TEXT("Nothing hit yet: no target, even with a thug beside him"), Brain->GetAttackTarget());
	TestEqual(TEXT("so he follows"), Brain->ChooseMode(), EHawkeyePartnerMode::Follow);

	Brain->SetTestTimeSeconds(0.0);
	Brain->NotifyLeaderHit(ThugA);
	Brain->SetTestTimeSeconds(2.0);
	Brain->NotifyLeaderHit(ThugB);

	Brain->SetTestTimeSeconds(2.5);
	TestEqual(TEXT("The one she hit last"), Brain->GetAttackTarget(), ThugB);
	TestEqual(TEXT("and he attacks"), Brain->ChooseMode(), EHawkeyePartnerMode::Attack);

	HealthB->ApplyDamage(1000.f, Pair.Kate);
	TestEqual(TEXT("B is dead: A, hit 2.5 s ago, is still inside 3 s"), Brain->GetAttackTarget(), ThugA);

	Brain->SetTestTimeSeconds(3.1);
	TestNull(TEXT("A's hit is 3.1 s old: out of the window"), Brain->GetAttackTarget());
	TestEqual(TEXT("back to following"), Brain->ChooseMode(), EHawkeyePartnerMode::Follow);

	// Pure: ties go to the nearest.
	TArray<FHawkeyeLeaderHit> Hits;
	for (AActor* Hit : { ThugA, Untouched })
	{
		FHawkeyeLeaderHit& Entry = Hits.AddDefaulted_GetRef();
		Entry.Target = Hit;
		Entry.Seconds = 5.0;
	}
	TestEqual(TEXT("Hit together: the nearer one"),
		AHawkeyePartnerController::SelectAttackTarget(Hits, 5.5, 3.f, FVector::ZeroVector), Untouched);
	TestNull(TEXT("A hit from the future is ignored"),
		AHawkeyePartnerController::SelectAttackTarget(Hits, 4.0, 3.f, FVector::ZeroVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerReviveOncePerFight, "Hawkeye.Partner.ReviveOncePerFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerReviveOncePerFight::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	const FHawkeyeTestWorld TestWorld;
	FPair Pair = SpawnPair(TestWorld);
	AActor* Thug = SpawnTarget(TestWorld, FVector(300.f, 0.f, 100.f));
	if (!Pair.IsValid() || !Thug)
	{
		AddError(TEXT("Could not spawn the pair."));
		return false;
	}
	AHawkeyePartnerController* Brain = Pair.Brain;
	UHealthComponent* KateHealth = Pair.Kate->GetHealthComponent();
	Pair.Clint->SetActorLocation(FVector(-100.f, 0.f, 100.f));

	Brain->SetTestTimeSeconds(1.0);
	KateHealth->ApplyDamage(1000.f, Thug);
	TestTrue(TEXT("At 0 health she is down, not dead and restarting"), Pair.Kate->IsDowned());
	TestEqual(TEXT("He goes to revive"), Brain->ChooseMode(), EHawkeyePartnerMode::Revive);

	Brain->RunMode(EHawkeyePartnerMode::Revive, 1.5f);
	TestTrue(TEXT("Still down at 1.5 s"), Pair.Kate->IsDowned());
	Brain->RunMode(EHawkeyePartnerMode::Revive, 1.6f);
	TestFalse(TEXT("Up after 3 s beside her"), Pair.Kate->IsDowned());
	TestEqual(TEXT("at 30% health"), KateHealth->GetCurrentHealth(), KateHealth->GetMaxHealth() * 0.3f, 0.01f);

	Brain->SetTestTimeSeconds(5.0);
	KateHealth->ApplyDamage(1000.f, Thug);
	// Down again in the same fight: no second revive. She is down on the 8 s clock with nobody coming.
	TestTrue(TEXT("Down again in the same fight"), Pair.Kate->IsDowned());
	TestFalse(TEXT("but no second revive"), Pair.Kate->IsReviveExpected());
	TestFalse(TEXT("she is dead"), KateHealth->IsAlive());

	// The clock itself: contact keeps a fight going for 10 s, the revive comes back with the next fight.
	FHawkeyeFightClock Clock;
	TestFalse(TEXT("No fight before any contact"), Clock.IsInFight(0.0));
	Clock.NotifyContact(0.0);
	TestTrue(TEXT("First revive of the fight"), Clock.ConsumeRevive(1.0));
	TestFalse(TEXT("Second one refused"), Clock.ConsumeRevive(2.0));
	Clock.NotifyContact(4.0);
	TestTrue(TEXT("13.9 s: 9.9 s after the last contact, still the same fight"), Clock.IsInFight(13.9));
	TestFalse(TEXT("so still no revive"), Clock.CanRevive(13.9));
	TestFalse(TEXT("14.1 s: the fight is over"), Clock.IsInFight(14.1));
	TestTrue(TEXT("and the revive is back"), Clock.CanRevive(14.1));
	Clock.NotifyContact(20.0);
	TestTrue(TEXT("A new fight has its own revive"), Clock.ConsumeRevive(21.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerSwitchRefused, "Hawkeye.Partner.SwitchRefusedMidZipAndTakedown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerSwitchRefused::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	const FHawkeyeTestWorld TestWorld;
	FPair Pair = SpawnPair(TestWorld);
	AGrappleAnchor* Anchor = Cast<AGrappleAnchor>(
		TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(1500.f, 0.f, 800.f), FRotator::ZeroRotator));
	if (!Pair.IsValid() || !Anchor)
	{
		AddError(TEXT("Could not spawn the pair and an anchor."));
		return false;
	}

	UGrappleComponent* Grapple = Pair.Kate->GetGrappleComponent();
	TestTrue(TEXT("The zip starts"), Grapple->StartZip(Anchor));
	TestTrue(TEXT("Mid-zip the switch is refused"), Pair.PC->GetSwitchRefusal().Contains(TEXT("mid-zip")));
	TestFalse(TEXT("and does nothing"), Pair.PC->SwitchCharacter());
	TestTrue(TEXT("still Kate"), Pair.PC->GetPawn() == Pair.Kate);
	Grapple->ReleaseZip(/*bJump=*/false);

	// A thug with his back to her, a metre in front.
	AHawkeyeTestSolidTakedownTarget* Thug = Cast<AHawkeyeTestSolidTakedownTarget>(TestWorld.SpawnActor(
		AHawkeyeTestSolidTakedownTarget::StaticClass(), Pair.Kate->GetActorLocation() + FVector(100.f, 0.f, 0.f),
		FRotator::ZeroRotator));
	Pair.Kate->SetActorRotation(FRotator::ZeroRotator);
	UTakedownComponent* Takedown = Pair.Kate->GetTakedownComponent();
	TestTrue(TEXT("The takedown starts"), Thug && Takedown->TryTakedown());
	TestTrue(TEXT("Mid-takedown the switch is refused"), Pair.PC->GetSwitchRefusal().Contains(TEXT("mid-takedown")));
	TestFalse(TEXT("and does nothing"), Pair.PC->SwitchCharacter());
	Takedown->EndTakedown();

	Pair.PC->bAllowSwitchingOverride = false;
	TestTrue(TEXT("With no chapter allowing it, refused"), Pair.PC->GetSwitchRefusal().Contains(TEXT("chapter")));
	Pair.PC->bAllowSwitchingOverride = true;
	TestTrue(TEXT("Free again: nothing refuses it"), Pair.PC->GetSwitchRefusal().IsEmpty());

	Pair.Kate->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	TestTrue(TEXT("Down or dead, refused"), Pair.PC->GetSwitchRefusal().Contains(TEXT("down")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerSwitchSwaps, "Hawkeye.Partner.SwitchSwapsPossessionAndHud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerSwitchSwaps::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	const FHawkeyeTestWorld TestWorld;
	FPair Pair = SpawnPair(TestWorld);
	if (!Pair.IsValid())
	{
		AddError(TEXT("Could not spawn the pair."));
		return false;
	}
	TestEqual(TEXT("The HUD starts on Kate"), Pair.PC->GetHudCharacterName().ToString(), FString(TEXT("Kate")));
	TestTrue(TEXT("Clint cannot die while he is the partner"), Pair.Clint->GetHealthComponent()->CannotDie());

	TestTrue(TEXT("The switch goes ahead"), Pair.PC->SwitchCharacter());
	TestTrue(TEXT("The player now has Clint"), Pair.PC->GetPawn() == Pair.Clint);
	TestTrue(TEXT("the partner controller has Kate"), Pair.Brain->GetPawn() == Pair.Kate);
	TestEqual(TEXT("and follows Clint"), Pair.Brain->GetLeader(), Pair.Clint);
	TestEqual(TEXT("The HUD says Clint"), Pair.PC->GetHudCharacterName().ToString(), FString(TEXT("Clint")));
	TestFalse(TEXT("Clint, played, can die again"), Pair.Clint->GetHealthComponent()->CannotDie());
	TestTrue(TEXT("Kate, the partner now, cannot"), Pair.Kate->GetHealthComponent()->CannotDie());

	TestTrue(TEXT("And back"), Pair.PC->SwitchCharacter());
	TestTrue(TEXT("Kate again"), Pair.PC->GetPawn() == Pair.Kate);
	TestEqual(TEXT("HUD says Kate"), Pair.PC->GetHudCharacterName().ToString(), FString(TEXT("Kate")));
	TestEqual(TEXT("Clint follows her"), Pair.Brain->GetLeader(), Pair.Kate);

	TestEqual(TEXT("The HUD's partner line"),
		UHawkeyeHudWidget::FormatPartnerStatus(FText::FromString(TEXT("Clint")), Pair.Brain->GetStatusText()).ToString(),
		FString(TEXT("Clint: following")));
	TestFalse(TEXT("No name tag at 3 m"), UHawkeyeHudWidget::ShouldShowPartnerTag(300.f, 400.f));
	TestTrue(TEXT("A name tag at 5 m"), UHawkeyeHudWidget::ShouldShowPartnerTag(500.f, 400.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerBanter, "Hawkeye.Partner.BanterBySituationAlternates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerBanter::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	using ESit = EHawkeyeBanterSituation;
	UBanterComponent* Banter = NewObject<UBanterComponent>();
	Banter->SetRandomSeed(7);
	Banter->SetLines({
		Line(TEXT("Kate"), ESit::IdleRoam, TEXT("K idle 1")),
		Line(TEXT("Kate"), ESit::IdleRoam, TEXT("K idle 2")),
		Line(TEXT("Clint"), ESit::IdleRoam, TEXT("C idle 1")),
		Line(TEXT("Kate"), ESit::AfterFight, TEXT("K after")),
		Line(TEXT("Clint"), ESit::AfterFight, TEXT("C after")),
		Line(TEXT("Clint"), ESit::LowHealth, TEXT("C low")),
	});

	TestEqual(TEXT("Kate speaks first"), Banter->GetNextSpeaker(), FName(TEXT("Kate")));
	TestTrue(TEXT("An after-fight line"), Banter->PlaySituation(ESit::AfterFight));
	TestEqual(TEXT("Kate's"), Banter->GetLastText().ToString(), FString(TEXT("K after")));
	TestEqual(TEXT("Clint's turn next"), Banter->GetNextSpeaker(), FName(TEXT("Clint")));
	TestTrue(TEXT("Another"), Banter->PlaySituation(ESit::AfterFight));
	TestEqual(TEXT("Clint's after-fight line, not an idle one"), Banter->GetLastText().ToString(), FString(TEXT("C after")));

	TestTrue(TEXT("Idle"), Banter->PlaySituation(ESit::IdleRoam));
	TestEqual(TEXT("back to Kate"), Banter->GetLastSpeaker(), FName(TEXT("Kate")));
	TestTrue(TEXT("one of her idle lines"), Banter->GetLastText().ToString().StartsWith(TEXT("K idle")));

	TestTrue(TEXT("Low health, Clint's turn"), Banter->PlaySituation(ESit::LowHealth));
	TestEqual(TEXT("Clint"), Banter->GetLastSpeaker(), FName(TEXT("Clint")));
	TestTrue(TEXT("Low health again, Kate's turn but she has none"), Banter->PlaySituation(ESit::LowHealth));
	TestEqual(TEXT("so Clint says it"), Banter->GetLastSpeaker(), FName(TEXT("Clint")));

	TestFalse(TEXT("Nobody has an objective line"), Banter->PlaySituation(ESit::ObjectiveNear));

	FRandomStream Stream(3);
	int32 Last = INDEX_NONE;
	for (int32 Pick = 0; Pick < 8; ++Pick)
	{
		const int32 Index = UBanterComponent::PickLineIndex(Banter->GetLines(), ESit::IdleRoam, FName(TEXT("Kate")), Last, Stream);
		TestNotEqual(TEXT("Never the same idle line twice running"), Index, Last);
		TestEqual(TEXT("Always Kate's idle lines"), Banter->GetLines()[Index].Situation, ESit::IdleRoam);
		Last = Index;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerClintQuiver, "Hawkeye.Partner.ClintQuiverGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePartnerClintQuiver::RunTest(const FString& Parameters)
{
	using namespace HawkeyePartnerTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Clint = SpawnHawkeye(TestWorld, FVector::ZeroVector, TEXT("Clint"));
	UInventoryComponent* Inventory = Clint ? Clint->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		AddError(TEXT("Could not spawn Clint."));
		return false;
	}

	UBowDefinition* ClintBow = NewObject<UBowDefinition>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBowDefinition::StaticClass(), TEXT("TestBowClint")));
	ClintBow->FullDrawSeconds = 1.f;
	UBowDefinition* KateBow = NewObject<UBowDefinition>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBowDefinition::StaticClass(), TEXT("TestBowKate")));
	UArrowDefinition* Standard = MakeArrow(1, 30, EArrowHitEffect::None, TEXT("TestStandard"));
	UArrowDefinition* Grapple = MakeArrow(2, 6, EArrowHitEffect::Grapple, TEXT("TestGrapple"));

	Inventory->bUseOwnStartingQuiver = true;
	Inventory->OwnStartingBow = ClintBow;
	Inventory->OwnStartingArrows = { Grant(Standard, 30), Grant(Grapple, 4) };
	Inventory->ApplyOwnStartingQuiver();

	TestEqual(TEXT("Clint's bow"), Inventory->GetBow(), ClintBow);
	TestEqual(TEXT("with the 1.0 s draw"), Inventory->GetBow()->FullDrawSeconds, 1.f);
	TestEqual(TEXT("30 standard"), Inventory->GetArrowCount(1), 30);
	TestTrue(TEXT("and his grapple (no count)"), Inventory->GetArrowSlot(2).Arrow == Grapple);

	UMissionDefinition* Chapter = NewObject<UMissionDefinition>();
	Chapter->StartingBow = KateBow;
	Chapter->StartingArrows = { Grant(Standard, 30), Grant(Grapple, 6) };
	Inventory->ApplyMissionStart(Chapter);
	TestEqual(TEXT("The chapter's grant is Kate's: he keeps his bow"), Inventory->GetBow(), ClintBow);
	TestTrue(TEXT("and his grapple"), Inventory->GetArrowSlot(2).Arrow == Grapple);

	Inventory->SetArrowCount(1, 3);
	Inventory->Clear();
	TestEqual(TEXT("A restart puts his own 30 back"), Inventory->GetArrowCount(1), 30);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
