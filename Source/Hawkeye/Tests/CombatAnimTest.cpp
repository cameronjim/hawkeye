#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "Combat/AnimNotifyState_ComboWindow.h"
#include "Combat/AnimNotifyState_HitWindow.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Combat clips (claude-docs/gameplay-semantics.md, "Combat animation clips"): the anim set's roles and
 * its one fallback, the swing timed by ANS_HitWindow and ANS_ComboWindow, the procedural swing, dash,
 * reactions and bow kept exactly when no clip plays, and the bow IK anchored on a clip's grip. A test
 * world has no anim instance, so no montage ever plays here: the notify path is driven through the
 * notify classes themselves, which is what a playing montage would call.
 */
namespace HawkeyeCombatAnimTest
{
	static UAnimMontage* MakeMontage(FName Slot = HawkeyeCombatAnim::FullBodySlot)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SlotAnimTracks.AddDefaulted();
		Montage->SlotAnimTracks[0].SlotName = Slot;
		return Montage;
	}

	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	}

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At,
			FRotator(0.f, 180.f, 0.f)));
		if (Thug)
		{
			Thug->Weapon = EThugWeapon::Fists;
		}
		return Thug;
	}

	/** A set with every strike, dodge, reaction and bow role filled by montages that can never play here. */
	static UCombatAnimSet* MakeFullSet()
	{
		UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
		for (TSoftObjectPtr<UAnimMontage>* Slot : { &Set->Light1, &Set->Light2, &Set->Light3, &Set->Heavy, &Set->Parry,
				&Set->DodgeForward, &Set->DodgeBack, &Set->DodgeLeft, &Set->DodgeRight, &Set->HitFront, &Set->HitBack,
				&Set->HitLeft, &Set->HitRight, &Set->Knockdown, &Set->GetUp, &Set->FinisherAttacker, &Set->FinisherVictim })
		{
			*Slot = MakeMontage();
		}
		for (TSoftObjectPtr<UAnimMontage>* Slot : { &Set->BowDraw, &Set->BowAimIdle, &Set->BowFire, &Set->BowNock })
		{
			*Slot = MakeMontage(HawkeyeCombatAnim::UpperBodySlot);
		}
		return Set;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCombatAnimSetResolution, "Hawkeye.CombatAnim.SetResolutionAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCombatAnimSetResolution::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatAnimTest;

	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	TestEqual(TEXT("An empty set names nothing"), Set->CountAssigned(), 0);
	TestNull(TEXT("An empty role resolves to nothing"), Set->ResolveMontage(ECombatAnimRole::Light1));
	TestNull(TEXT("No set resolves to nothing"), UCombatAnimSet::Resolve(nullptr, ECombatAnimRole::Light1));
	TestNull(TEXT("None is never a clip"), Set->ResolveMontage(ECombatAnimRole::None));

	UAnimMontage* Jab = MakeMontage();
	UAnimMontage* Kick = MakeMontage();
	Set->Light1 = Jab;
	Set->Kick = Kick;
	TestTrue(TEXT("Light1 resolves to its montage"), Set->ResolveMontage(ECombatAnimRole::Light1) == Jab);
	TestNull(TEXT("Light2 is still empty: no borrowing from Light1"), Set->ResolveMontage(ECombatAnimRole::Light2));
	TestTrue(TEXT("Light3 with no clip of its own uses Kick"), Set->ResolveMontage(ECombatAnimRole::Light3) == Kick);
	TestTrue(TEXT("HasMontage sees the fallback"), Set->HasMontage(ECombatAnimRole::Light3));
	TestEqual(TEXT("Two slots assigned (the fallback is not a slot)"), Set->CountAssigned(), 2);
	UAnimMontage* Ender = MakeMontage();
	Set->Light3 = Ender;
	TestTrue(TEXT("Its own Light3 wins over Kick"), Set->ResolveMontage(ECombatAnimRole::Light3) == Ender);
	Set->Preload();
	TestTrue(TEXT("Resolve through the static helper"), UCombatAnimSet::Resolve(Set, ECombatAnimRole::Light1) == Jab);

	TestTrue(TEXT("A blow from behind is HitBack"), UCombatAnimSet::HitRoleFor(EHawkeyeHitDirection::Back) == ECombatAnimRole::HitBack);
	TestTrue(TEXT("From the left is HitLeft"), UCombatAnimSet::HitRoleFor(EHawkeyeHitDirection::Left) == ECombatAnimRole::HitLeft);
	TestTrue(TEXT("From the front is HitFront"), UCombatAnimSet::HitRoleFor(EHawkeyeHitDirection::Front) == ECombatAnimRole::HitFront);
	TestTrue(TEXT("A dash to the right is DodgeRight"), UCombatAnimSet::DodgeRoleFor(EHawkeyeHitDirection::Right) == ECombatAnimRole::DodgeRight);
	TestTrue(TEXT("A dash back is DodgeBack"), UCombatAnimSet::DodgeRoleFor(EHawkeyeHitDirection::Back) == ECombatAnimRole::DodgeBack);

	TestTrue(TEXT("An UpperBody slot is an upper-body montage"),
		HawkeyeCombatAnim::IsUpperBodyMontage(MakeMontage(HawkeyeCombatAnim::UpperBodySlot)));
	TestFalse(TEXT("DefaultSlot is full body"), HawkeyeCombatAnim::IsUpperBodyMontage(Jab));
	TestNull(TEXT("A mesh with no anim instance has nowhere to play"), HawkeyeCombatAnim::PickInstance(
		NewObject<USkeletalMeshComponent>(GetTransientPackage()), Jab));
	TestEqual(TEXT("A 1.2 s clip fitted to 0.4 s plays at 3x"), HawkeyeCombatAnim::FitRate(1.2f, 0.4f), 3.f, 0.001f);
	TestEqual(TEXT("Fitting clamps at 4x"), HawkeyeCombatAnim::FitRate(10.f, 0.1f), 4.f, 0.001f);
	TestEqual(TEXT("Nothing to fit plays at 1x"), HawkeyeCombatAnim::FitRate(0.f, 0.4f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCombatAnimHitFit, "Hawkeye.CombatAnim.ThugClipHitLandsOnTheTelegraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCombatAnimHitFit::RunTest(const FString& Parameters)
{
	using HawkeyeCombatAnim::FHitFit;
	auto Lands = [](const FHitFit& Fit, float Hit) { return Fit.HoldSeconds + (Hit - Fit.StartAtSeconds) / Fit.Rate; };

	// Inside the band: just the rate.
	const FHitFit Inside = HawkeyeCombatAnim::FitHitToWindup(0.66f, 0.6f, 0.8f, 1.3f);
	TestEqual(TEXT("A 0.66 s wind-up on a 0.6 s telegraph plays at 1.1x"), Inside.Rate, 1.1f, 0.001f);
	TestEqual(TEXT("from the start"), Inside.StartAtSeconds, 0.f);
	TestEqual(TEXT("with no hold"), Inside.HoldSeconds, 0.f);

	// Too long a wind-up: the fastest rate, from later in it.
	const FHitFit Long = HawkeyeCombatAnim::FitHitToWindup(0.9f, 0.6f, 0.8f, 1.3f);
	TestEqual(TEXT("A 0.9 s wind-up on 0.6 s plays at the 1.3x ceiling"), Long.Rate, 1.3f, 0.001f);
	TestEqual(TEXT("from 0.12 s in"), Long.StartAtSeconds, 0.12f, 0.001f);
	TestEqual(TEXT("and lands at 0.6 s"), Lands(Long, 0.9f), 0.6f, 0.001f);

	// Too short: the slowest rate, after holding the first frame.
	const FHitFit Short = HawkeyeCombatAnim::FitHitToWindup(0.24f, 0.6f, 0.8f, 1.3f);
	TestEqual(TEXT("A 0.24 s wind-up on 0.6 s plays at the 0.8x floor"), Short.Rate, 0.8f, 0.001f);
	TestEqual(TEXT("after a 0.3 s hold"), Short.HoldSeconds, 0.3f, 0.001f);
	TestEqual(TEXT("and lands at 0.6 s"), Lands(Short, 0.24f), 0.6f, 0.001f);

	// One clip for the bat, the bash and the slow swing: every one lands on its own telegraph, in the band.
	for (const float Windup : { 0.6f, 0.8f, 1.f })
	{
		const FHitFit Fit = HawkeyeCombatAnim::FitHitToWindup(0.85f, Windup, 0.8f, 1.3f);
		TestEqual(FString::Printf(TEXT("A %.1f s telegraph lands on its end"), Windup), Lands(Fit, 0.85f), Windup, 0.001f);
		TestTrue(FString::Printf(TEXT("at a rate in the band (%.2fx)"), Fit.Rate), Fit.Rate >= 0.8f - 1e-3f && Fit.Rate <= 1.3f + 1e-3f);
	}

	const FHitFit None = HawkeyeCombatAnim::FitHitToWindup(0.f, 0.6f, 0.8f, 1.3f);
	TestTrue(TEXT("No hit window plays at 1x from the start"), None.Rate == 1.f && None.StartAtSeconds == 0.f && None.HoldSeconds == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCombatAnimNotifyTiming, "Hawkeye.CombatAnim.NotifyWindowsTimeTheSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCombatAnimNotifyTiming::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatAnimTest;

	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(500.f, true);
	USkeletalMeshComponent* Body = Kate->GetMesh();
	UAnimNotifyState_HitWindow* HitWindow = NewObject<UAnimNotifyState_HitWindow>(GetTransientPackage());
	UAnimNotifyState_ComboWindow* ComboWindow = NewObject<UAnimNotifyState_ComboWindow>(GetTransientPackage());
	const FAnimNotifyEventReference Event;

	// The hit waits for the window, however long the wind-up timer says.
	Melee->ForceNotifyTimingForTest();
	TestTrue(TEXT("The first light starts"), Kate->StartLightAttack());
	TestTrue(TEXT("Its hit is timed by the clip"), Melee->IsHitFromNotify());
	Melee->AdvanceAttack(1.f);
	TestTrue(TEXT("A second past its 0.1 s wind-up it is still winding up"), Melee->IsWindingUp());
	TestEqual(TEXT("And nothing has landed"), Health->GetCurrentHealth(), 500.f, 0.01f);
	HitWindow->NotifyBegin(Body, nullptr, 0.1f, Event);
	TestEqual(TEXT("ANS_HitWindow opening lands the 15"), Health->GetCurrentHealth(), 485.f, 0.01f);
	TestEqual(TEXT("And the swing is recovering"), Melee->GetPhase(), EMeleePhase::Recover);
	HitWindow->NotifyEnd(Body, nullptr, Event);

	// The chain holds open until the combo window closes; a press before it opens waits for it.
	TestTrue(TEXT("The chain is open"), Kate->GetCombo().IsChainOpen());
	TestTrue(TEXT("Pressed before the combo window, the next light is held"), Kate->StartLightAttack());
	TestFalse(TEXT("It has not started"), Melee->CanStartAttack());
	Kate->AdvanceMeleeFlow(1.f);
	TestTrue(TEXT("A second later the chain is still open (no 0.35 s timer)"), Kate->GetCombo().IsChainOpen());
	TestEqual(TEXT("Still the first light"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	ComboWindow->NotifyBegin(Body, nullptr, 0.3f, Event);
	TestTrue(TEXT("ANS_ComboWindow lets a swing start"), Melee->CanStartAttack());
	Melee->ForceNotifyTimingForTest();
	Kate->AdvanceMeleeFlow(0.01f);
	TestEqual(TEXT("The held press goes the moment the window opens: the chain's second"),
		Melee->GetCurrentAttack().Name, FName(TEXT("light2")));
	HitWindow->NotifyBegin(Body, nullptr, 0.1f, Event);
	TestEqual(TEXT("It lands its 15"), Health->GetCurrentHealth(), 470.f, 0.01f);
	HitWindow->NotifyEnd(Body, nullptr, Event);

	// Left alone, the window closing ends the chain.
	ComboWindow->NotifyBegin(Body, nullptr, 0.3f, Event);
	TestTrue(TEXT("The chain is open for the third"), Kate->GetCombo().IsChainOpen());
	ComboWindow->NotifyEnd(Body, nullptr, Event);
	TestFalse(TEXT("ANS_ComboWindow closing shuts the chain"), Kate->GetCombo().IsChainOpen());
	TestEqual(TEXT("The next light is the first again"), Kate->GetCombo().GetNextStep(), 0);
	Melee->NotifyMontageEnded(false);
	TestFalse(TEXT("The clip ending ends the swing"), Melee->IsAttacking());

	// A window that finds nobody is a miss when it closes, not when it opens; sweeping goes on meanwhile.
	Thug->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	Melee->ForceNotifyTimingForTest();
	TestTrue(TEXT("A light at nobody"), Kate->StartLightAttack());
	HitWindow->NotifyBegin(Body, nullptr, 0.2f, Event);
	TestTrue(TEXT("The window is open"), Melee->IsHitWindowOpen());
	Thug->SetActorLocation(FVector(100.f, 0.f, 0.f));
	Melee->AdvanceAttack(0.02f);
	TestEqual(TEXT("He stepped into the open window and took it"), Health->GetCurrentHealth(), 455.f, 0.01f);
	HitWindow->NotifyEnd(Body, nullptr, Event);
	Melee->NotifyMontageEnded(false);

	Thug->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	Melee->ForceNotifyTimingForTest();
	TestTrue(TEXT("Another light at nobody"), Kate->StartLightAttack());
	HitWindow->NotifyBegin(Body, nullptr, 0.2f, Event);
	HitWindow->NotifyEnd(Body, nullptr, Event);
	TestFalse(TEXT("The window closed on nobody"), Melee->IsHitWindowOpen());
	Thug->SetActorLocation(FVector(100.f, 0.f, 0.f));
	Melee->AdvanceAttack(0.02f);
	TestEqual(TEXT("After it closes nothing more is swept: he takes nothing"), Health->GetCurrentHealth(), 455.f, 0.01f);
	TestEqual(TEXT("It was a miss, and the chain carries on to the second"), Kate->GetCombo().GetNextStep(), 1);
	TestTrue(TEXT("Held open for the clip's combo window"), Kate->GetCombo().IsChainHeld());
	Melee->NotifyMontageEnded(true);
	TestFalse(TEXT("Idle again"), Melee->IsAttacking());
	TestEqual(TEXT("The clip ending lets the chain go"), Kate->GetCombo().GetNextStep(), 0);

	// Punching the air with clips: the combo window governs, so three lights play first, second, third.
	Thug->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	const int32 CountBefore = Kate->GetComboCount();
	const FName Expected[] = { FName(TEXT("light")), FName(TEXT("light2")), FName(TEXT("light3")) };
	for (int32 Press = 0; Press < 3; ++Press)
	{
		Melee->ForceNotifyTimingForTest();
		if (Press == 0)
		{
			TestTrue(TEXT("An air light starts"), Kate->StartLightAttack());
		}
		else
		{
			// The last press was held through the miss; it goes when the combo window opens.
			ComboWindow->NotifyBegin(Body, nullptr, 0.3f, Event);
			Kate->AdvanceMeleeFlow(0.01f);
		}
		TestEqual(FString::Printf(TEXT("Air press %d plays the chain's step %d"), Press + 1, Press + 1),
			Melee->GetCurrentAttack().Name, Expected[Press]);
		HitWindow->NotifyBegin(Body, nullptr, 0.1f, Event);
		if (Press < 2)
		{
			TestTrue(TEXT("A press while the hit window is open is held"), Kate->StartLightAttack());
		}
		HitWindow->NotifyEnd(Body, nullptr, Event);
		Kate->AdvanceMeleeFlow(1.f);
		if (Press < 2)
		{
			TestTrue(TEXT("The miss keeps the chain open past 0.35 s (the clip's window governs)"),
				Kate->GetCombo().IsChainOpen());
		}
	}
	TestEqual(TEXT("After the third the chain starts over"), Kate->GetCombo().GetNextStep(), 0);
	TestTrue(TEXT("Misses never count"), Kate->GetComboCount() <= CountBefore);
	Melee->NotifyMontageEnded(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCombatAnimVariants, "Hawkeye.CombatAnim.StrikeVariantsCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCombatAnimVariants::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatAnimTest;

	// The picker on its own: in order, round again, never the same twice running.
	int32 Cursor = 0;
	TArray<int32> Picks;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		Picks.Add(UCombatAnimSet::PickNextVariant(Cursor, 3));
	}
	TestTrue(TEXT("Three variants go 0, 1, 2, 0, 1, 2, 0"), Picks == TArray<int32>({ 0, 1, 2, 0, 1, 2, 0 }));
	int32 One = 0;
	TestEqual(TEXT("One variant is always 0"), UCombatAnimSet::PickNextVariant(One, 1), 0);
	TestEqual(TEXT("and again"), UCombatAnimSet::PickNextVariant(One, 1), 0);
	int32 None = 5;
	TestEqual(TEXT("None is 0"), UCombatAnimSet::PickNextVariant(None, 0), 0);

	// The set: a role's slot is variant 0, MoreVariants the rest; the Light3 fallback takes Kick's list.
	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	UAnimMontage* Uppercut = MakeMontage();
	UAnimMontage* Roundhouse = MakeMontage();
	UAnimMontage* Jab = MakeMontage();
	UAnimMontage* FrontKick = MakeMontage();
	UAnimMontage* SideKick = MakeMontage();
	Set->Heavy = Uppercut;
	Set->Light1 = Jab;
	Set->Kick = FrontKick;
	FCombatAnimVariants& HeavyMore = Set->MoreVariants.AddDefaulted_GetRef();
	HeavyMore.Role = ECombatAnimRole::Heavy;
	HeavyMore.Montages.Add(Roundhouse);
	FCombatAnimVariants& KickMore = Set->MoreVariants.AddDefaulted_GetRef();
	KickMore.Role = ECombatAnimRole::Kick;
	KickMore.Montages.Add(SideKick);
	FCombatAnimVariants& Orphan = Set->MoreVariants.AddDefaulted_GetRef();
	Orphan.Role = ECombatAnimRole::Light2;
	Orphan.Montages.Add(MakeMontage());
	TestEqual(TEXT("Heavy has two"), Set->GetVariantCount(ECombatAnimRole::Heavy), 2);
	TestEqual(TEXT("Light1 has its one"), Set->GetVariantCount(ECombatAnimRole::Light1), 1);
	TestEqual(TEXT("A role with an empty slot has none, whatever MoreVariants says"),
		Set->GetVariantCount(ECombatAnimRole::Light2), 0);
	TestEqual(TEXT("Light3 with no slot takes Kick's two"), Set->GetVariantCount(ECombatAnimRole::Light3), 2);
	TestTrue(TEXT("Variant 0 is the slot"), Set->ResolveVariant(ECombatAnimRole::Heavy, 0) == Uppercut);
	TestTrue(TEXT("Variant 1 is the next"), Set->ResolveVariant(ECombatAnimRole::Heavy, 1) == Roundhouse);
	TestNull(TEXT("Past the end is nothing"), Set->ResolveVariant(ECombatAnimRole::Heavy, 2));
	TestTrue(TEXT("Light3's second is the side kick"), Set->ResolveVariant(ECombatAnimRole::Light3, 1) == SideKick);
	TestTrue(TEXT("GetMontage is still the slot"), Set->ResolveMontage(ECombatAnimRole::Heavy) == Uppercut);
	TestEqual(TEXT("CountAssigned counts slots only"), Set->CountAssigned(), 3);

	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	Kate->SetCombatAnimSet(Set);
	Kate->ApplyCombatAnimSet();
	UMeleeComponent* Melee = Kate->GetMeleeComponent();

	// Heavy: the uppercut, the roundhouse, the uppercut, the roundhouse; never the same twice running.
	const UAnimMontage* HeavyOrder[] = { Uppercut, Roundhouse, Uppercut, Roundhouse };
	const UAnimMontage* Last = nullptr;
	for (int32 Swing = 0; Swing < 4; ++Swing)
	{
		TestTrue(FString::Printf(TEXT("Heavy %d starts"), Swing + 1), Kate->StartHeavyAttack());
		TestTrue(FString::Printf(TEXT("Heavy %d picks variant %d"), Swing + 1, Swing % 2),
			Melee->GetPickedMontage() == HeavyOrder[Swing]);
		TestEqual(TEXT("and says which"), Melee->GetPickedVariantIndex(), Swing % 2);
		TestTrue(TEXT("Not the one before it"), Melee->GetPickedMontage() != Last);
		Last = Melee->GetPickedMontage();
		TestNull(TEXT("It cannot play here, so the swing is procedural"), Melee->GetCurrentMontage());
		Melee->AdvanceAttack(2.f);
		Kate->AdvanceMeleeFlow(2.f);
	}

	// Light1 has one clip: every first light takes it, as before variants.
	for (int32 Swing = 0; Swing < 3; ++Swing)
	{
		TestTrue(TEXT("A first light starts"), Kate->StartLightAttack());
		TestEqual(TEXT("It is the chain's first"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
		TestTrue(TEXT("Its one clip, every time"), Melee->GetPickedMontage() == Jab);
		TestEqual(TEXT("Variant 0"), Melee->GetPickedVariantIndex(), 0);
		Melee->AdvanceAttack(2.f);
		Kate->AdvanceMeleeFlow(1.f);
	}

	// Light2 has none: nothing picked, the procedural swing.
	TestTrue(TEXT("A first light"), Kate->StartLightAttack());
	Melee->AdvanceAttack(0.11f);
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.05f);
	TestTrue(TEXT("The second, on the first's miss"), Kate->StartLightAttack());
	TestEqual(TEXT("It is light2"), Melee->GetCurrentAttack().Name, FName(TEXT("light2")));
	TestNull(TEXT("With no clip picked"), Melee->GetPickedMontage());
	TestEqual(TEXT("Index -1"), Melee->GetPickedVariantIndex(), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCombatAnimAbsentKeepsProcedural, "Hawkeye.CombatAnim.NoClipKeepsProceduralBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCombatAnimAbsentKeepsProcedural::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatAnimTest;

	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	// Every role filled, but no anim instance to play on: exactly the game before clips.
	UCombatAnimSet* Set = MakeFullSet();
	Kate->SetCombatAnimSet(Set);
	Kate->ApplyCombatAnimSet();
	Thug->SetCombatAnimSet(Set);
	Thug->ApplyCombatAnimSet();
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(500.f, true);
	TestTrue(TEXT("The melee component has the set"), Melee->GetAnimSet() == Set);
	TestTrue(TEXT("So does the bow"), Kate->GetBowComponent()->GetAnimSet() == Set);

	TestTrue(TEXT("A light starts"), Kate->StartLightAttack());
	TestNull(TEXT("Its clip did not play"), Melee->GetCurrentMontage());
	TestFalse(TEXT("So the timers time it"), Melee->IsHitFromNotify() || Melee->IsComboFromNotify());
	TestTrue(TEXT("And the strike pose is the arms"), Kate->GetStrikePose().IsActive());
	Melee->AdvanceAttack(0.09f);
	TestEqual(TEXT("Nothing at 0.09 s"), Health->GetCurrentHealth(), 500.f, 0.01f);
	Melee->AdvanceAttack(0.02f);
	TestEqual(TEXT("15 at 0.1 s, as before clips"), Health->GetCurrentHealth(), 485.f, 0.01f);
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.2f);
	TestFalse(TEXT("Over at 0.3 s"), Melee->IsAttacking());
	Kate->AdvanceMeleeFlow(0.2f);
	TestFalse(TEXT("The 0.35 s chain timer still runs"), Kate->GetCombo().IsChainOpen());

	TestTrue(TEXT("A heavy starts"), Kate->StartHeavyAttack());
	Melee->AdvanceAttack(0.61f);
	TestTrue(TEXT("It knocks him down"), Thug->IsKnockedDown());
	TestFalse(TEXT("Not through a clip"), Thug->IsKnockdownFromClip());
	Thug->UpdateKnockdown(Thug->KnockdownSeconds + 0.1f);
	TestFalse(TEXT("Up after KnockdownSeconds"), Thug->IsKnockedDown());

	Melee->AdvanceAttack(1.f);
	Kate->TestTickDodge(2.f);
	TestTrue(TEXT("A dodge starts with every dodge role filled"), Kate->TryDodge(FVector(0.f, 1.f, 0.f)));
	TestTrue(TEXT("The dash runs"), Kate->IsDodging());
	TestTrue(TEXT("Invulnerable as before"), Kate->GetHealthComponent()->IsInvulnerable());
	Kate->TestTickDodge(2.f);

	Thug->StaggerFor(Kate, 0.5f);
	TestTrue(TEXT("A thug's hit reaction still staggers him"), Thug->IsStaggered());

	UBowComponent* Bow = Kate->GetBowComponent();
	Kate->GetInventoryComponent()->GiveBow(NewObject<UBowDefinition>(Kate));
	Bow->SetTestTimeSeconds(10.0);
	TestTrue(TEXT("A draw starts"), Bow->StartDraw());
	TestFalse(TEXT("With no bow clip playing"), Bow->IsBowClipPlaying());
	Bow->CancelDraw();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowIKClipAnchor, "Hawkeye.BowIK.ClipHoldsBowArmStringOnGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowIKClipAnchor::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatAnimTest;

	const FBowHandIKSettings Settings;
	const FVector Grip(120.f, -30.f, 150.f);
	const FRotator Aim(10.f, 90.f, 0.f);
	const FBowHandIKTargets OnGrip = UHawkeyeBowIKAnimInstance::ComputeHandTargetsOnGrip(Grip, Aim, 1.f, Settings);
	TestTrue(TEXT("The bow hand is exactly where the clip put the bow"), OnGrip.BowHand.Equals(Grip, 0.01));
	TestTrue(TEXT("The string hand keeps its full-draw place relative to the grip, along the aim"),
		(OnGrip.StringHand - OnGrip.BowHand).Equals(Aim.RotateVector(Settings.StringHandFullOffset - Settings.GripOffset), 0.01));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	if (!TestNotNull(TEXT("Kate with a bow component"), Bow))
	{
		return false;
	}
	UBowDefinition* Definition = NewObject<UBowDefinition>(Kate);
	Kate->GetInventoryComponent()->GiveBow(Definition);
	Bow->SetTestTimeSeconds(10.0);
	TestTrue(TEXT("Draws"), Bow->StartDraw());
	Bow->SetTestTimeSeconds(10.0 + Definition->FullDrawSeconds);

	UHawkeyeBowIKAnimInstance* Procedural = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	UHawkeyeBowIKAnimInstance* Clip = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Procedural->UpdateFromBowWithClip(Bow, 0.075f, false);
		Clip->UpdateFromBowWithClip(Bow, 0.075f, true);
	}
	TestEqual(TEXT("Without a clip the bow hand is on the IK"), Procedural->LeftArmAlpha, 1.f, 0.001f);
	TestEqual(TEXT("No clip, no clip alpha"), Procedural->GetClipAlpha(), 0.f);
	TestEqual(TEXT("A clip takes the bow arm over in 0.15 s"), Clip->GetClipAlpha(), 1.f, 0.001f);
	TestEqual(TEXT("So the bow hand's IK is off"), Clip->LeftArmAlpha, 0.f, 0.001f);
	TestEqual(TEXT("Without a clip the string hand is on the IK"), Procedural->RightArmAlpha, 1.f, 0.001f);
	// 2026-09-28: under a clip the string hand is the clip's; the IK only corrects it off the arrow line,
	// and this mesh has no hand for it to be off with.
	TestEqual(TEXT("Under a clip the string hand is the clip's"), Clip->RightArmAlpha, 0.f, 0.001f);
	TestEqual(TEXT("No aim offset asset, no aim offset"), Clip->AimOffsetAlpha, 0.f);
	TestTrue(TEXT("The procedural spine turns side-on"), Procedural->SpineTwist.Yaw > Settings.SideOnDegrees - 0.1f);
	TestEqual(TEXT("Under a clip only the turn toward the aim is added (none here)"), Clip->SpineTwist.Yaw, 0.0, 0.01);
	TestEqual(TEXT("And the neck is not turned back"), Clip->NeckTwist.Yaw, 0.0, 0.01);
	Bow->CancelDraw();
	return true;
}

#endif
