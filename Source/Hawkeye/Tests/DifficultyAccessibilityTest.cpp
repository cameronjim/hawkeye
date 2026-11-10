#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Combat/WeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Flashback/FlashbackDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeAccessibility.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/FlashbackReplayWidget.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/HawkeyeThugOverheadWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The difficulty table and each place it is used (thug damage and health, archer draws, the parry
 * window, Kate's regeneration, fall damage, trick arrow caps), and the accessibility rules (hold versus
 * toggle, palettes, subtitle sizes, shake and flash scales, the HUD scale, the glyph outline, the
 * flashback replay list). claude-docs/gameplay-semantics.md, "Difficulty" and "Accessibility".
 */
namespace HawkeyeDifficultyTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static const TCHAR* TestSlot = TEXT("HawkeyeDifficultyAutomationTest");

	static FHawkeyeSettings On(EHawkeyeDifficulty Difficulty)
	{
		FHawkeyeSettings Settings;
		Settings.Difficulty = Difficulty;
		return Settings;
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& At = FVector::ZeroVector)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), At, FRotator::ZeroRotator));
	}

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw, EThugWeapon Weapon)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
		if (Thug)
		{
			Thug->Weapon = Weapon;
		}
		return Thug;
	}

	static AThugAIController* Possess(const FHawkeyeTestWorld& TestWorld, AThugCharacter* Thug)
	{
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Brain && Thug)
		{
			if (AController* Existing = Thug->GetController())
			{
				Existing->UnPossess();
			}
			Brain->Possess(Thug);
		}
		return Brain;
	}

	static float Distance(const FLinearColor& A, const FLinearColor& B)
	{
		return FMath::Sqrt(FMath::Square(A.R - B.R) + FMath::Square(A.G - B.G) + FMath::Square(A.B - B.B));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDifficultyTable, "Hawkeye.Difficulty.Table", HawkeyeDifficultyTest::Flags)

bool FHawkeyeDifficultyTable::RunTest(const FString& Parameters)
{
	using S = EDifficultyStat;
	const EHawkeyeDifficulty Levels[] = { EHawkeyeDifficulty::Story, EHawkeyeDifficulty::Normal, EHawkeyeDifficulty::Hard };
	struct FRow
	{
		S Stat;
		float Values[3];
		const TCHAR* Label;
	};
	const FRow Rows[] = {
		{ S::ThugDamage, { 0.6f, 1.0f, 1.4f }, TEXT("thug damage") },
		{ S::ThugHealth, { 0.8f, 1.0f, 1.2f }, TEXT("thug health") },
		{ S::ArcherDrawSeconds, { 1.5f, 1.2f, 1.0f }, TEXT("archer draw") },
		{ S::ParryWindowSeconds, { 0.15f, 0.f, -0.1f }, TEXT("parry window") },
		{ S::RegenDelaySeconds, { 3.f, 5.f, 8.f }, TEXT("regen delay") },
		{ S::FallDamage, { 0.5f, 1.0f, 1.0f }, TEXT("fall damage") },
		{ S::TrickArrowCapBonus, { 2.f, 0.f, 0.f }, TEXT("trick arrow caps") },
	};
	for (const FRow& Row : Rows)
	{
		for (int32 Level = 0; Level < 3; ++Level)
		{
			TestEqual(FString::Printf(TEXT("%s on %s"), Row.Label, *UDifficultySubsystem::GetDifficultyName(Levels[Level]).ToString()),
				UDifficultySubsystem::GetTableValue(Levels[Level], Row.Stat), Row.Values[Level], 0.0001f);
		}
	}

	EHawkeyeDifficulty Parsed = EHawkeyeDifficulty::Normal;
	TestTrue(TEXT("\"story\" parses"), UDifficultySubsystem::ParseDifficulty(TEXT("story"), Parsed));
	TestEqual(TEXT("To Story"), Parsed, EHawkeyeDifficulty::Story);
	TestTrue(TEXT("\"HARD\" parses"), UDifficultySubsystem::ParseDifficulty(TEXT("HARD"), Parsed));
	TestEqual(TEXT("To Hard"), Parsed, EHawkeyeDifficulty::Hard);
	TestFalse(TEXT("\"easy\" does not"), UDifficultySubsystem::ParseDifficulty(TEXT("easy"), Parsed));
	TestEqual(TEXT("The default is Normal"), FHawkeyeSettings().Difficulty, EHawkeyeDifficulty::Normal);

	// Without a game instance the scalar comes from the settings in force: the defaults, or a test's.
	TestEqual(TEXT("No game: Normal's thug damage"), UDifficultySubsystem::GetScalarFor(nullptr, S::ThugDamage), 1.f);
	{
		FHawkeyeScopedSettingsOverride Hard(HawkeyeDifficultyTest::On(EHawkeyeDifficulty::Hard));
		TestEqual(TEXT("Hard in force: 1.4"), UDifficultySubsystem::GetScalarFor(nullptr, S::ThugDamage), 1.4f);
	}
	TestEqual(TEXT("And back to Normal after"), UDifficultySubsystem::GetScalarFor(nullptr, S::ThugDamage), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDifficultyPersists, "Hawkeye.Difficulty.PersistsInSettings", HawkeyeDifficultyTest::Flags)

bool FHawkeyeDifficultyPersists::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	if (UGameplayStatics::DoesSaveGameExist(TestSlot, 0))
	{
		UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
	}
	UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
	UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(Outer);
	Settings->SlotNameOverride = TestSlot;
	Settings->Load();
	TestTrue(TEXT("Version 8 or later"), FHawkeyeSettings::CurrentVersion >= 8);

	Settings->SetDifficulty(EHawkeyeDifficulty::Hard);
	Settings->SetSubtitleSize(EHawkeyeSubtitleSize::Large);
	Settings->SetSubtitleBackgroundOpacity(1.7f);
	Settings->SetToggleAim(true);
	Settings->SetToggleCrouch(false);
	Settings->SetColorPalette(EHawkeyeColorPalette::Tritanopia);
	Settings->SetReduceCameraShake(true);
	Settings->SetReduceFlashing(true);
	Settings->SetHudScale(3.f);
	Settings->MarkFlashbackSeen(FSoftObjectPath(TEXT("/Game/Flashbacks/DA_FB00_Placeholder.DA_FB00_Placeholder")));
	Settings->MarkFlashbackSeen(FSoftObjectPath(TEXT("/Game/Flashbacks/DA_FB00_Placeholder.DA_FB00_Placeholder")));
	TestEqual(TEXT("The opacity clamps to 1"), Settings->GetSettings().SubtitleBackgroundOpacity, 1.f);
	TestEqual(TEXT("The HUD scale clamps to 1.4"), Settings->GetSettings().HudScale, 1.4f);
	Settings->SetHudScale(0.1f);
	TestEqual(TEXT("And up to 0.8"), Settings->GetSettings().HudScale, 0.8f);

	UHawkeyeSettingsSubsystem* Reloaded = NewObject<UHawkeyeSettingsSubsystem>(Outer);
	Reloaded->SlotNameOverride = TestSlot;
	Reloaded->Load();
	const FHawkeyeSettings Back = Reloaded->GetStoredSettings();
	TestEqual(TEXT("Difficulty survives the save"), Back.Difficulty, EHawkeyeDifficulty::Hard);
	TestEqual(TEXT("Subtitle size"), Back.SubtitleSize, EHawkeyeSubtitleSize::Large);
	TestEqual(TEXT("Subtitle backing"), Back.SubtitleBackgroundOpacity, 1.f);
	TestTrue(TEXT("Toggle aim"), Back.bToggleAim);
	TestFalse(TEXT("Hold crouch"), Back.bToggleCrouch);
	TestEqual(TEXT("Palette"), Back.ColorPalette, EHawkeyeColorPalette::Tritanopia);
	TestTrue(TEXT("Reduce shake"), Back.bReduceCameraShake);
	TestTrue(TEXT("Reduce flashing"), Back.bReduceFlashing);
	TestEqual(TEXT("HUD scale"), Back.HudScale, 0.8f);
	TestEqual(TEXT("One seen flashback, marked once"), Back.SeenFlashbacks.Num(), 1);

	// The defaults, which an older (version 4) save falls back to: Normal, toggle crouch, held aim, scale 1.
	const FHawkeyeSettings Defaults;
	TestEqual(TEXT("Default difficulty"), Defaults.Difficulty, EHawkeyeDifficulty::Normal);
	TestTrue(TEXT("Default crouch toggles, aim is held"), Defaults.bToggleCrouch && !Defaults.bToggleAim);
	TestEqual(TEXT("Default HUD scale"), Defaults.HudScale, 1.f);
	TestEqual(TEXT("Default subtitles medium"), Defaults.SubtitleSize, EHawkeyeSubtitleSize::Medium);
	UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDifficultyThugs, "Hawkeye.Difficulty.ThugDamageHealthAndDraw", HawkeyeDifficultyTest::Flags)

bool FHawkeyeDifficultyThugs::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	FHawkeyeTestWorld TestWorld;

	// Health: scaled at spawn from whatever the class gave him.
	AThugCharacter* NormalThug = SpawnThug(TestWorld, FVector(0.f, 500.f, 0.f), 0.f, EThugWeapon::Bat);
	TestEqual(TEXT("Normal: 100"), NormalThug->GetHealthComponent()->GetMaxHealth(), 100.f);
	{
		FHawkeyeScopedSettingsOverride Story(On(EHawkeyeDifficulty::Story));
		AThugCharacter* StoryThug = SpawnThug(TestWorld, FVector(0.f, 700.f, 0.f), 0.f, EThugWeapon::Bat);
		TestEqual(TEXT("Story: 80"), StoryThug->GetHealthComponent()->GetMaxHealth(), 80.f);
		TestEqual(TEXT("At full"), StoryThug->GetHealthComponent()->GetCurrentHealth(), 80.f);
		AThugAIController* Brain = Possess(TestWorld, StoryThug);
		TestEqual(TEXT("Story: a bat swing does 15"), Brain->ScaleAttackForDifficulty(StoryThug->GetMeleeAttack()).Damage, 15.f, 0.01f);
	}
	{
		FHawkeyeScopedSettingsOverride Hard(On(EHawkeyeDifficulty::Hard));
		AThugCharacter* HardThug = SpawnThug(TestWorld, FVector(0.f, 900.f, 0.f), 0.f, EThugWeapon::Fists);
		TestEqual(TEXT("Hard: 120"), HardThug->GetHealthComponent()->GetMaxHealth(), 120.f);
		AThugAIController* Brain = Possess(TestWorld, HardThug);
		TestEqual(TEXT("Hard: fists do 21"), Brain->ScaleAttackForDifficulty(HardThug->GetMeleeAttack()).Damage, 21.f, 0.01f);

		// A gunner's shot on Hard: 12 * 1.4, and the pistol keeps its own 12.
		AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(600.f, 0.f, 0.f));
		AThugCharacter* Gunner = SpawnThug(TestWorld, FVector::ZeroVector, 0.f, EThugWeapon::Pistol);
		AThugAIController* GunnerBrain = Possess(TestWorld, Gunner);
		if (!Kate || !GunnerBrain)
		{
			AddError(TEXT("Failed to set up the gunner."));
			return false;
		}
		GunnerBrain->AimSpreadDegrees = 0.f;
		GunnerBrain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 3.f);
		GunnerBrain->SetTarget(Kate);
		const int32 Ammo = Gunner->GetWeaponComponent()->CurrentAmmo;
		UHealthComponent* KateHealth = Kate->GetHealthComponent();
		for (int32 Step = 0; Step < 20 && Gunner->GetWeaponComponent()->CurrentAmmo == Ammo; ++Step)
		{
			ThinkThug(GunnerBrain, 0.1f);
		}
		TestTrue(TEXT("He fired"), Gunner->GetWeaponComponent()->CurrentAmmo < Ammo);
		TestEqual(TEXT("The pistol still reads 12"), Gunner->GetWeaponComponent()->Damage, 12.f);
		const float Taken = KateHealth->GetMaxHealth() - KateHealth->GetCurrentHealth();
		if (Taken > 0.f)
		{
			TestEqual(TEXT("Kate took 16.8 a shot"), FMath::Fmod(Taken, 16.8f), 0.f, 0.05f);
		}
		else
		{
			AddWarning(TEXT("The gunner's shot missed Kate; only the restored pistol damage was checked."));
		}
	}

	// Archers: the draw rate that makes a 1.2 s bow take 1.5 or 1.0 s.
	TestEqual(TEXT("Story: 0.8x"), AThugAIController::ComputeArcherDrawRate(1.2f, 1.5f), 0.8f, 0.0001f);
	TestEqual(TEXT("Normal: 1x"), AThugAIController::ComputeArcherDrawRate(1.2f, 1.2f), 1.f, 0.0001f);
	TestEqual(TEXT("Hard: 1.2x"), AThugAIController::ComputeArcherDrawRate(1.2f, 1.0f), 1.2f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDifficultyParryWindow, "Hawkeye.Difficulty.ParryWindow", HawkeyeDifficultyTest::Flags)

bool FHawkeyeDifficultyParryWindow::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	TestFalse(TEXT("Not telegraphing: never"), UHawkeyeMeleeRules::IsInParryWindow(-1.f, 0.15f));
	TestTrue(TEXT("Normal: from the first instant"), UHawkeyeMeleeRules::IsInParryWindow(0.f, 0.f));
	TestFalse(TEXT("Hard: not 0.05 s in"), UHawkeyeMeleeRules::IsInParryWindow(0.05f, -0.1f));
	TestTrue(TEXT("Hard: from 0.1 s"), UHawkeyeMeleeRules::IsInParryWindow(0.1f, -0.1f));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
	if (!Kate || !Melee)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}

	// Hard: the start of the telegraph does not count.
	Kate->ApplySettings(On(EHawkeyeDifficulty::Hard));
	TestEqual(TEXT("Hard's delta"), Kate->GetParryWindowDelta(), -0.1f);
	Melee->StartAttack(Thug->GetMeleeAttack());
	Melee->AdvanceAttack(0.05f);
	TestEqual(TEXT("0.05 s into his telegraph"), UHawkeyeMeleeRules::GetTelegraphElapsed(Thug), 0.05f, 0.001f);
	TestFalse(TEXT("Hard: too early to parry"), Kate->TryParry());
	Melee->AdvanceAttack(0.1f);
	TestTrue(TEXT("Hard: 0.15 s in, parried"), Kate->TryParry());
	Melee->AdvanceAttack(2.f);

	// Story: a tap just before the telegraph is held and spent when it starts.
	AThugCharacter* Second = SpawnThug(TestWorld, FVector(140.f, 20.f, 0.f), 180.f, EThugWeapon::Bat);
	Thug->Destroy();
	Kate->ApplySettings(On(EHawkeyeDifficulty::Story));
	TestEqual(TEXT("Story's delta"), Kate->GetParryWindowDelta(), 0.15f);
	Kate->GetMeleeComponent()->CancelAttack();
	const int32 Parries = Kate->GetParryCount();
	TestTrue(TEXT("A tap with nothing to parry strikes"), Kate->StartLightAttack());
	TestEqual(TEXT("And waits 0.15 s for a telegraph"), Kate->GetParryBufferRemaining(), 0.15f, 0.001f);
	Kate->AdvanceMeleeFlow(0.05f);
	Second->GetMeleeComponent()->StartAttack(Second->GetMeleeAttack());
	Kate->AdvanceMeleeFlow(0.05f);
	TestEqual(TEXT("His telegraph, 0.1 s after the tap, is parried"), Kate->GetParryCount(), Parries + 1);
	TestTrue(TEXT("He is parry-staggered"), Second->IsParryStaggered());
	TestEqual(TEXT("The tap is spent"), Kate->GetParryBufferRemaining(), 0.f);

	// Normal: no buffer at all.
	Kate->ApplySettings(On(EHawkeyeDifficulty::Normal));
	Kate->GetMeleeComponent()->CancelAttack();
	Second->GetMeleeComponent()->CancelAttack();
	Kate->StartLightAttack();
	TestEqual(TEXT("Normal holds no early tap"), Kate->GetParryBufferRemaining(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDifficultyKate, "Hawkeye.Difficulty.RegenFallAndCaps", HawkeyeDifficultyTest::Flags)

bool FHawkeyeDifficultyKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;

	// Regeneration on a bare component: after the delay, at the rate, up to max, never while dead.
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->bRegenerates = true;
	Health->RegenDelay = 3.f;
	Health->RegenPerSecond = 10.f;
	Health->ApplyDamage(50.f);
	Health->AdvanceRegen(2.9f);
	TestEqual(TEXT("Nothing before 3 s"), Health->GetCurrentHealth(), 50.f);
	Health->AdvanceRegen(0.6f);
	TestEqual(TEXT("0.5 s past the delay: +5"), Health->GetCurrentHealth(), 55.f, 0.01f);
	Health->ApplyDamage(5.f);
	Health->AdvanceRegen(2.f);
	TestEqual(TEXT("A hit starts the delay over"), Health->GetCurrentHealth(), 50.f, 0.01f);
	Health->AdvanceRegen(20.f);
	TestEqual(TEXT("Up to max and no further"), Health->GetCurrentHealth(), 100.f);
	UHealthComponent* Thug = NewObject<UHealthComponent>();
	Thug->ApplyDamage(50.f);
	Thug->AdvanceRegen(30.f);
	TestEqual(TEXT("Thugs never regenerate"), Thug->GetCurrentHealth(), 50.f);
	Health->ApplyDamage(200.f);
	Health->AdvanceRegen(30.f);
	TestEqual(TEXT("Dead: nothing"), Health->GetCurrentHealth(), 0.f);

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	TestTrue(TEXT("Kate regenerates"), Kate->GetHealthComponent()->bRegenerates);
	TestEqual(TEXT("After 5 s on Normal"), Kate->GetHealthComponent()->RegenDelay, 5.f);
	Kate->ApplySettings(On(EHawkeyeDifficulty::Hard));
	TestEqual(TEXT("8 s on Hard"), Kate->GetHealthComponent()->RegenDelay, 8.f);
	TestEqual(TEXT("Full fall damage on Hard"), Kate->GetFallDamageScale(), 1.f);
	Kate->ApplySettings(On(EHawkeyeDifficulty::Story));
	TestEqual(TEXT("3 s on Story"), Kate->GetHealthComponent()->RegenDelay, 3.f);
	TestEqual(TEXT("Half fall damage on Story"), Kate->GetFallDamageScale(), 0.5f);

	// Trick arrow caps: Story's +2 on the trick arrows, never on standard or grapple.
	UInventoryComponent* Inventory = Kate->FindComponentByClass<UInventoryComponent>();
	UArrowDefinition* Bola = NewObject<UArrowDefinition>(Kate);
	Bola->Slot = 4;
	Bola->Cap = 4;
	Bola->OnHitEffect = EArrowHitEffect::Bola;
	UArrowDefinition* Grapple = NewObject<UArrowDefinition>(Kate);
	Grapple->Slot = 2;
	Grapple->Cap = 6;
	Grapple->OnHitEffect = EArrowHitEffect::Grapple;
	TestTrue(TEXT("Bola is a trick arrow"), UInventoryComponent::IsTrickArrow(Bola));
	TestFalse(TEXT("The grapple is not"), UInventoryComponent::IsTrickArrow(Grapple));
	TestEqual(TEXT("Story's bonus reached the quiver"), Inventory->GetTrickArrowCapBonus(), 2);
	TestEqual(TEXT("Bola cap 6 on Story"), Inventory->GetCap(Bola), 6);
	TestEqual(TEXT("Grapple cap unchanged"), Inventory->GetCap(Grapple), 6);
	TestTrue(TEXT("And the grapple has no count for a cap to matter to"), UInventoryComponent::IsUnlimitedArrow(Grapple));
	TestFalse(TEXT("Bola does"), UInventoryComponent::IsUnlimitedArrow(Bola));
	TestEqual(TEXT("Six bola fit"), Inventory->AddArrows(Bola, 10), 6);
	Kate->ApplySettings(On(EHawkeyeDifficulty::Normal));
	TestEqual(TEXT("Back on Normal the cap is 4"), Inventory->GetCap(Bola), 4);
	TestEqual(TEXT("And the count comes down to it"), Inventory->GetArrowCount(4), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAccessibilityHoldToggle, "Hawkeye.Accessibility.HoldOrToggle", HawkeyeDifficultyTest::Flags)

bool FHawkeyeAccessibilityHoldToggle::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	TestTrue(TEXT("Hold: a press turns it on"), UHawkeyeAccessibility::ResolvePress(false, false));
	TestFalse(TEXT("Hold: a release turns it off"), UHawkeyeAccessibility::ResolveRelease(false, true));
	TestTrue(TEXT("Toggle: a press turns it on"), UHawkeyeAccessibility::ResolvePress(true, false));
	TestFalse(TEXT("Toggle: the next press turns it off"), UHawkeyeAccessibility::ResolvePress(true, true));
	TestTrue(TEXT("Toggle: a release changes nothing"), UHawkeyeAccessibility::ResolveRelease(true, true));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	FHawkeyeSettings Settings;
	TestFalse(TEXT("Aim is held by default"), Kate->IsAimToggle());
	TestTrue(TEXT("Crouch toggles by default"), Kate->IsCrouchToggle());

	// Aim, held.
	Kate->PressAim();
	TestTrue(TEXT("Held aim: aiming while held"), Kate->IsAiming());
	Kate->ReleaseAim();
	TestFalse(TEXT("Held aim: lowered on release"), Kate->IsAiming());

	// Aim, toggled.
	Settings.bToggleAim = true;
	Kate->ApplySettings(Settings);
	Kate->PressAim();
	Kate->ReleaseAim();
	TestTrue(TEXT("Toggled aim: still aiming after the release"), Kate->IsAiming());
	Kate->PressAim();
	TestFalse(TEXT("Toggled aim: the second press lowers it"), Kate->IsAiming());
	Kate->ReleaseAim();
	TestFalse(TEXT("And the release leaves it down"), Kate->IsAiming());

	// Crouch, toggled (the default).
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Kate->PressCrouch();
	Kate->ReleaseCrouch();
	TestTrue(TEXT("Toggled crouch: still crouching after the release"), Kate->IsCrouchWanted());
	Kate->PressCrouch();
	TestFalse(TEXT("Toggled crouch: the second press stands"), Kate->IsCrouchWanted());
	Kate->ReleaseCrouch();

	// Crouch, held.
	Settings.bToggleCrouch = false;
	Kate->ApplySettings(Settings);
	Kate->PressCrouch();
	TestTrue(TEXT("Held crouch: crouching while held"), Kate->IsCrouchWanted());
	Kate->PressCrouch();
	TestTrue(TEXT("Held crouch: a second press does not stand her"), Kate->IsCrouchWanted());
	Kate->ReleaseCrouch();
	TestFalse(TEXT("Held crouch: she stands on the release"), Kate->IsCrouchWanted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAccessibilityPalettes, "Hawkeye.Accessibility.Palettes", HawkeyeDifficultyTest::Flags)

bool FHawkeyeAccessibilityPalettes::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	const FHawkeyePalette Default = UHawkeyeAccessibility::GetPalette(EHawkeyeColorPalette::Default);
	TestTrue(TEXT("Default green is the grapple marker's"), Default.Green.Equals(FLinearColor(0.22f, 1.f, 0.08f, 1.f), 0.001f));
	TestTrue(TEXT("Default purple is the reticle's"), Default.Purple.Equals(FLinearColor(0.78f, 0.55f, 1.f, 1.f), 0.001f));
	for (EHawkeyeColorPalette Palette : { EHawkeyeColorPalette::Deuteranopia, EHawkeyeColorPalette::Protanopia,
			 EHawkeyeColorPalette::Tritanopia })
	{
		const FHawkeyePalette P = UHawkeyeAccessibility::GetPalette(Palette);
		const FString Name = UHawkeyeAccessibility::GetPaletteName(Palette).ToString();
		TestTrue(FString::Printf(TEXT("%s moves the green"), *Name), Distance(P.Green, Default.Green) > 0.3f);
		const FLinearColor Colours[] = { P.Green, P.Purple, P.Cream, P.HealthBar, P.Danger };
		for (int32 A = 0; A < 5; ++A)
		{
			for (int32 B = A + 1; B < 5; ++B)
			{
				TestTrue(FString::Printf(TEXT("%s: accents %d and %d stay apart"), *Name, A, B), Distance(Colours[A], Colours[B]) > 0.3f);
			}
		}
	}

	// The HUD takes the palette: its reticle and grapple marker, and the markers' and glyphs' colours.
	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();
	const FLinearColor Resting = Hud->GetReticleColor();
	FHawkeyeSettings Settings;
	Settings.ColorPalette = EHawkeyeColorPalette::Deuteranopia;
	Hud->ApplySettings(Settings);
	const FHawkeyePalette Deut = UHawkeyeAccessibility::GetPalette(EHawkeyeColorPalette::Deuteranopia);
	TestEqual(TEXT("The HUD shows deuteranopia"), Hud->GetPaletteShown(), EHawkeyeColorPalette::Deuteranopia);
	TestTrue(TEXT("Its reticle is the palette's purple"), Hud->GetReticleColor().Equals(Deut.Purple, 0.001f));
	TestTrue(TEXT("Its grapple marker the palette's green"), Hud->GetGrappleMarkerColor().Equals(Deut.Green, 0.001f));
	Settings.ColorPalette = EHawkeyeColorPalette::Default;
	Hud->ApplySettings(Settings);
	TestTrue(TEXT("Default puts the designer's reticle back"), Hud->GetReticleColor().Equals(Resting, 0.001f));

	UHawkeyeObjectiveWidget* Marker = NewObject<UHawkeyeObjectiveWidget>();
	const FLinearColor Cream = Marker->GetMarkerColor();
	Marker->SetAccentColors(Deut.Cream, Deut.Purple, false);
	TestTrue(TEXT("The objective marker takes the palette's cream"), Marker->GetMarkerColor().Equals(Deut.Cream, 0.001f));
	Marker->SetAccentColors(Deut.Cream, Deut.Purple, true);
	TestTrue(TEXT("And gives it back"), Marker->GetMarkerColor().Equals(Cream, 0.001f));

	UHawkeyeThugOverheadWidget* Overhead = NewObject<UHawkeyeThugOverheadWidget>();
	TestEqual(TEXT("Alert glyphs have a 1 px outline"), Overhead->GetGlyphFont().OutlineSettings.OutlineSize, 1);
	Overhead->SetAccentColors(Deut.Cream, Deut.HealthBar, false);
	TestTrue(TEXT("The health bar takes the palette's colour"), Overhead->BarColor.Equals(Deut.HealthBar, 0.001f));
	const FLinearColor Telegraph = Overhead->TelegraphColor;
	TestTrue(TEXT("The telegraph glyph is red-orange by design"), Telegraph.Equals(Default.Danger, 0.001f));
	Overhead->SetThreatColors(Deut.Danger, Deut.Purple, false);
	TestTrue(TEXT("The telegraph glyph takes the palette's danger colour"), Overhead->TelegraphColor.Equals(Deut.Danger, 0.001f));
	TestTrue(TEXT("The target ring takes its purple"), Overhead->MarkerColor.Equals(Deut.Purple, 0.001f));
	Overhead->SetThreatColors(Deut.Danger, Deut.Purple, true);
	TestTrue(TEXT("And gives the design's back"), Overhead->TelegraphColor.Equals(Telegraph, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAccessibilityShakeFlash, "Hawkeye.Accessibility.ShakeAndFlash", HawkeyeDifficultyTest::Flags)

bool FHawkeyeAccessibilityShakeFlash::RunTest(const FString& Parameters)
{
	using namespace HawkeyeDifficultyTest;
	TestEqual(TEXT("Shake at 30%"), UHawkeyeAccessibility::GetCameraShakeScale(true), 0.3f);
	TestEqual(TEXT("Flash at 30%"), UHawkeyeAccessibility::GetFlashScale(true), 0.3f);
	TestEqual(TEXT("Both whole when off"), UHawkeyeAccessibility::GetCameraShakeScale(false) * UHawkeyeAccessibility::GetFlashScale(false), 1.f);

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	Kate->PlayImpactShake(0.5f, 18.f);
	TestEqual(TEXT("A blast's shake at full"), Kate->GetActiveShakeAmplitude(), 18.f);

	FHawkeyeSettings Reduced;
	Reduced.bReduceCameraShake = true;
	Reduced.bReduceFlashing = true;
	Kate = SpawnKate(TestWorld, FVector(0.f, 400.f, 0.f));
	Kate->ApplySettings(Reduced);
	Kate->PlayImpactShake(0.5f, 18.f);
	TestEqual(TEXT("Reduced: 30% of the throw"), Kate->GetActiveShakeAmplitude(), 5.4f, 0.001f);
	Kate->PlayScreenPulse(0.45f, 1.f);
	TestEqual(TEXT("The EMP's split at 30%"), Kate->GetScreenPulseStrength(), 0.3f, 0.01f);
	TestEqual(TEXT("The parry ring drawn at 30%"), Kate->GetFlashScale(), 0.3f);

	// The explosion's flash light reads the setting when it goes off.
	{
		FHawkeyeScopedSettingsOverride Scope(Reduced);
		AExplosiveBlast* Blast = Cast<AExplosiveBlast>(TestWorld.SpawnActor(AExplosiveBlast::StaticClass(), FVector(0.f, 3000.f, 0.f),
			FRotator::ZeroRotator));
		FHitResult Hit;
		Hit.ImpactPoint = FVector(0.f, 3000.f, 0.f);
		Hit.Location = Hit.ImpactPoint;
		Blast->InitEffect(nullptr, nullptr, Hit);
		Blast->Activate();
		TestEqual(TEXT("The blast's flash at 30%"), Blast->GetFlashScale(), 0.3f);
		TestEqual(TEXT("Its light 30% of the placeholder's 2500 cd"), Blast->GetFlashIntensity(), 750.f, 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAccessibilityHudScale, "Hawkeye.Accessibility.HudScaleAndSubtitles", HawkeyeDifficultyTest::Flags)

bool FHawkeyeAccessibilityHudScale::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Small subtitles 20 px"), UHawkeyeAccessibility::GetSubtitleFontSize(EHawkeyeSubtitleSize::Small), 20);
	TestEqual(TEXT("Medium 26 px"), UHawkeyeAccessibility::GetSubtitleFontSize(EHawkeyeSubtitleSize::Medium), 26);
	TestEqual(TEXT("Large 34 px"), UHawkeyeAccessibility::GetSubtitleFontSize(EHawkeyeSubtitleSize::Large), 34);

	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();
	FHawkeyeSettings Settings;
	Settings.SubtitleSize = EHawkeyeSubtitleSize::Large;
	Settings.SubtitleBackgroundOpacity = 0.6f;
	Settings.HudScale = 1.4f;
	Hud->ApplySettings(Settings);
	TestEqual(TEXT("The HUD's subtitles at 34 px"), Hud->GetSubtitleFontSize(), 34);
	TestEqual(TEXT("With a 60% backing"), Hud->GetSubtitleBackgroundOpacity(), 0.6f);
	TestEqual(TEXT("The HUD at 1.4"), Hud->GetHudScale(), 1.4f);
	Settings.HudScale = 3.f;
	Hud->ApplySettings(Settings);
	TestEqual(TEXT("Clamped to 1.4"), Hud->GetHudScale(), 1.4f);

	UHawkeyeObjectiveWidget* Marker = NewObject<UHawkeyeObjectiveWidget>();
	const float Compass = Marker->GetCompassWidth();
	const float Diamond = Marker->GetMarkerSize();
	Marker->SetHudScale(1.4f);
	TestEqual(TEXT("The compass grows with it"), Marker->GetCompassWidth(), Compass * 1.4f, 0.01f);
	TestEqual(TEXT("And the marker"), Marker->GetMarkerSize(), Diamond * 1.4f, 0.01f);
	Marker->SetHudScale(0.8f);
	TestEqual(TEXT("And shrinks to 0.8"), Marker->GetCompassWidth(), Compass * 0.8f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackReplayList, "Hawkeye.Accessibility.ReplayFlashbacksList", HawkeyeDifficultyTest::Flags)

bool FHawkeyeFlashbackReplayList::RunTest(const FString& Parameters)
{
	UFlashbackDefinition* Titled = NewObject<UFlashbackDefinition>(GetTransientPackage(), TEXT("DA_FB_TestTitled"));
	Titled->Title = FText::FromString(TEXT("[FB00 title]"));
	UFlashbackDefinition* Untitled = NewObject<UFlashbackDefinition>(GetTransientPackage(), TEXT("DA_FB_TestUntitled"));
	TestEqual(TEXT("Listed by its title"), UFlashbackReplayWidget::MakeEntryLabel(Titled).ToString(), FString(TEXT("[FB00 title]")));
	TestEqual(TEXT("Or its asset name"), UFlashbackReplayWidget::MakeEntryLabel(Untitled).ToString(), FString(TEXT("DA_FB_TestUntitled")));

	UFlashbackReplayWidget* List = NewObject<UFlashbackReplayWidget>();
	List->SetFlashbacks({ FSoftObjectPath(Titled), FSoftObjectPath(Untitled) });
	TestEqual(TEXT("Both listed"), List->GetEntryCount(), 2);
	TestEqual(TEXT("Oldest first"), List->GetEntryLabel(0).ToString(), FString(TEXT("[FB00 title]")));
	List->SetFlashbacks({});
	TestEqual(TEXT("Nothing seen: an empty list"), List->GetEntryCount(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
