#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Engine/World.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The death flow (claude-docs/gameplay-semantics.md, "Health and damage"): at 0 health she is down for
 * DownedMaxSeconds at most; the partner's revive has to land by then; with none coming a key ends it
 * early; either way she then dies and the last save loads. Nobody attacks a downed player.
 */
namespace HawkeyeDeathFlowTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static AHawkeyeCharacter* SpawnHawkeye(const FHawkeyeTestWorld& TestWorld, const FVector& At, const TCHAR* Name)
	{
		AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), At, FRotator::ZeroRotator));
		if (Character)
		{
			Character->CharacterName = FText::FromString(Name);
		}
		return Character;
	}

	/** Kate played by a controller, as in the game (the down's key comes through it). */
	static AHawkeyeCharacter* SpawnPlayedKate(const FHawkeyeTestWorld& TestWorld, const FVector& At)
	{
		AHawkeyeCharacter* Kate = SpawnHawkeye(TestWorld, At, TEXT("Kate"));
		AHawkeyePauseTestController* PC = Cast<AHawkeyePauseTestController>(
			TestWorld.SpawnActor(AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Kate && PC)
		{
			PC->Possess(Kate);
		}
		return Kate;
	}

	static AHawkeyePartnerController* SpawnPartner(const FHawkeyeTestWorld& TestWorld, AHawkeyeCharacter* Partner,
		AHawkeyeCharacter* Lead)
	{
		AHawkeyePartnerController* Brain = Cast<AHawkeyePartnerController>(
			TestWorld.SpawnActor(AHawkeyePartnerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Brain)
		{
			Brain->SetTestTimeSeconds(1.0);
			Brain->Possess(Partner);
			Brain->SetLeader(Lead);
		}
		return Brain;
	}

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, EThugWeapon Weapon,
		AThugAIController*& OutBrain)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator::ZeroRotator));
		OutBrain = nullptr;
		if (!Thug)
		{
			return nullptr;
		}
		Thug->Weapon = Weapon;
		OutBrain = Cast<AThugAIController>(TestWorld.SpawnActor(AThugAIController::StaticClass(), At, FRotator::ZeroRotator));
		if (AController* Existing = Thug->GetController())
		{
			Existing->UnPossess();
		}
		if (OutBrain)
		{
			OutBrain->Possess(Thug);
		}
		return Thug;
	}

	/** Alerts Brain on Target the way a gunshot next to him would. */
	static void Alert(AThugAIController* Brain, AActor* Target)
	{
		Brain->SetTarget(Target);
		Brain->ReportStimulus(EStimulusKind::Hearing, Target->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDeathDownedTimesOut, "Hawkeye.Death.DownedTimesOutToDeath", HawkeyeDeathFlowTest::Flags)

bool FHawkeyeDeathDownedTimesOut::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDeathFlowTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnPlayedKate(TestWorld, FVector(0.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}
	UHealthComponent* Health = Kate->GetHealthComponent();
	TestEqual(TEXT("Downed lasts 8 s at most"), Kate->DownedMaxSeconds, 8.f);

	// Alone (no partner): down, no revive coming, not dead of it yet.
	Health->ApplyDamage(1000.f, nullptr);
	TestTrue(TEXT("At 0 health she is down"), Kate->IsDowned());
	TestFalse(TEXT("No partner, so no revive is coming"), Kate->IsReviveExpected());
	TestFalse(TEXT("Not dead of it yet"), Kate->HasDiedFromDown());
	TestEqual(TEXT("The ring starts full"), Kate->GetDownedFractionLeft(), 1.f, 0.001f);

	Kate->AdvanceDowned(4.f);
	TestEqual(TEXT("Half the ring at 4 s"), Kate->GetDownedFractionLeft(), 0.5f, 0.001f);
	Kate->AdvanceDowned(3.9f);
	TestFalse(TEXT("Still only down at 7.9 s"), Kate->HasDiedFromDown());
	Kate->AdvanceDowned(0.2f);
	TestTrue(TEXT("Dead of it at 8 s"), Kate->HasDiedFromDown());
	TestTrue(TEXT("And still on the ground until the reload"), Kate->IsDowned());

	// A revive that turns up after that is too late.
	Kate->ReviveFromDown(0.3f);
	TestTrue(TEXT("A revive after the timer does nothing"), Kate->IsDowned());
	TestFalse(TEXT("Health stays at 0"), Health->IsAlive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDeathReviveInTime, "Hawkeye.Death.ReviveMustLandInTime", HawkeyeDeathFlowTest::Flags)

bool FHawkeyeDeathReviveInTime::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDeathFlowTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnPlayedKate(TestWorld, FVector(0.f, 0.f, 100.f));
	AHawkeyeCharacter* Clint = SpawnHawkeye(TestWorld, FVector(-100.f, 0.f, 100.f), TEXT("Clint"));
	AHawkeyePartnerController* Brain = Kate && Clint ? SpawnPartner(TestWorld, Clint, Kate) : nullptr;
	if (!TestNotNull(TEXT("The pair spawned"), Brain))
	{
		return false;
	}
	// However far he is, the teleport and the revive fit inside the down.
	TestTrue(TEXT("Reach timeout plus revive fits in the 8 s"),
		Brain->ReviveTimeoutSeconds + Brain->ReviveSeconds < Kate->DownedMaxSeconds);

	Kate->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	TestTrue(TEXT("Down"), Kate->IsDowned());
	TestTrue(TEXT("Clint took the revive on"), Kate->IsReviveExpected());
	TestTrue(TEXT("He is the one coming"), Kate->GetReviver() == Clint);
	TestFalse(TEXT("A key does not throw away a revive that is coming"), Kate->GiveUpFromDown());

	// He never gets to her (his modes are not run): the timer wins.
	Kate->AdvanceDowned(8.1f);
	TestTrue(TEXT("No revive by 8 s: she dies"), Kate->HasDiedFromDown());
	TestFalse(TEXT("And no revive is expected any more"), Kate->IsReviveExpected());
	TestNotEqual(TEXT("He stops trying"), Brain->ChooseMode(), EHawkeyePartnerMode::Revive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDeathGiveUp, "Hawkeye.Death.AnyKeyWithoutRevive", HawkeyeDeathFlowTest::Flags)

bool FHawkeyeDeathGiveUp::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDeathFlowTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnPlayedKate(TestWorld, FVector(0.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}
	TestFalse(TEXT("Nothing to give up while standing"), Kate->GiveUpFromDown());
	Kate->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	Kate->AdvanceDowned(0.2f);
	TestFalse(TEXT("A press in the first 0.5 s is ignored (a held attack)"), Kate->GiveUpFromDown());
	TestFalse(TEXT("so she is still only down"), Kate->HasDiedFromDown());
	Kate->AdvanceDowned(0.4f);
	TestTrue(TEXT("After it, a key ends the down"), Kate->GiveUpFromDown());
	TestTrue(TEXT("She died of it"), Kate->HasDiedFromDown());
	TestFalse(TEXT("A second key does nothing more"), Kate->GiveUpFromDown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDeathThugsStandOff, "Hawkeye.Death.ThugsStopOnDownedPlayer", HawkeyeDeathFlowTest::Flags)

bool FHawkeyeDeathThugsStandOff::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDeathFlowTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnPlayedKate(TestWorld, FVector(0.f, 0.f, 100.f));
	AThugAIController* Fists = nullptr;
	AThugAIController* Gunner = nullptr;
	AThugCharacter* Brawler = SpawnThug(TestWorld, FVector(110.f, 0.f, 100.f), EThugWeapon::Fists, Fists);
	AThugCharacter* Shooter = SpawnThug(TestWorld, FVector(900.f, 300.f, 100.f), EThugWeapon::Pistol, Gunner);
	if (!Kate || !Brawler || !Shooter || !Fists || !Gunner)
	{
		AddError(TEXT("Could not spawn Kate and the two thugs."));
		return false;
	}
	UHealthComponent* Health = Kate->GetHealthComponent();
	Alert(Fists, Kate);
	Alert(Gunner, Kate);
	ThinkThug(Fists, 0.25f);
	ThinkThug(Gunner, 0.25f);
	const int32 SwingsBefore = Fists->GetSwingsStarted();
	TestTrue(TEXT("Standing, the brawler swings at her"), SwingsBefore > 0);
	TestFalse(TEXT("Nobody stands off from a standing player"), Fists->IsStandingOff() || Gunner->IsStandingOff());
	TestFalse(TEXT("She is not down"), AThugAIController::IsTargetDown(Kate));

	Health->ApplyDamage(1000.f, Brawler);
	TestTrue(TEXT("Down now"), AThugAIController::IsTargetDown(Kate));
	// The swing already started plays out; after it nothing new starts, from either of them, for 3 s.
	if (UMeleeComponent* Melee = Brawler->GetMeleeComponent())
	{
		Melee->CancelAttack();
	}
	for (float Elapsed = 0.f; Elapsed < 3.f; Elapsed += 0.25f)
	{
		ThinkThug(Fists, 0.25f);
		ThinkThug(Gunner, 0.25f);
	}
	TestTrue(TEXT("The brawler circles instead"), Fists->IsStandingOff());
	TestTrue(TEXT("So does the gunner"), Gunner->IsStandingOff());
	TestEqual(TEXT("No new swing at a downed player"), Fists->GetSwingsStarted(), SwingsBefore);
	TestFalse(TEXT("No burst at a downed player"), Gunner->IsBursting() || Gunner->IsTelegraphing());
	TestEqual(TEXT("And no damage lands on her"), Health->ApplyMeleeDamage(25.f, Brawler), 0.f);

	// Back up: they come again.
	Kate->ReviveFromDown(0.3f);
	TestFalse(TEXT("Revived, she is a target again"), AThugAIController::IsTargetDown(Kate));
	ThinkThug(Fists, 0.25f);
	TestFalse(TEXT("The brawler stops circling"), Fists->IsStandingOff());
	return true;
}

#endif
