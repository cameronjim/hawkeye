#include "Combat/AimAssist.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bow's aim assist (claude-docs/gameplay-semantics.md, "Bow aim assist"): the numbers per setting and
 * difficulty, the cone, the slowdown and pull, the aim press's snap and the release's bend at each level.
 */
namespace HawkeyeAimAssistTest
{
	/** Puts Level and Difficulty in force for as long as it lives (automation worlds have no game instance). */
	struct FScopedAssist
	{
		FHawkeyeSettings Settings;
		FScopedAssist(EHawkeyeAimAssist Level, EHawkeyeDifficulty Difficulty)
		{
			Settings.AimAssist = Level;
			Settings.Difficulty = Difficulty;
			UHawkeyeSettingsSubsystem::SetTestSettingsOverride(&Settings);
		}
		~FScopedAssist() { UHawkeyeSettingsSubsystem::SetTestSettingsOverride(nullptr); }
	};

	/** A candidate standing at Centre (capsule 34 x 88). */
	static FHawkeyeAimAssistCandidate At(const FVector& Centre)
	{
		FHawkeyeAimAssistCandidate Candidate;
		Candidate.Chest = Centre + FVector(0.f, 0.f, UHawkeyeAimAssist::ChestHeight);
		Candidate.Feet = Centre - FVector(0.f, 0.f, 88.f);
		Candidate.Head = Centre + FVector(0.f, 0.f, 88.f);
		Candidate.Radius = 34.f;
		return Candidate;
	}

	/** A direction Degrees to the right of +X, level. */
	static FVector Yawed(float Degrees)
	{
		return FRotator(0.f, Degrees, 0.f).Vector();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAimAssistTuningTest, "Hawkeye.AimAssist.TuningPerLevelAndDifficulty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAimAssistTuningTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Normal is the default"), FHawkeyeSettings().AimAssist, EHawkeyeAimAssist::Normal);

	const FHawkeyeAimAssistTuning Off = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Off, EHawkeyeDifficulty::Normal);
	TestTrue(TEXT("Off is off"), Off.IsOff());
	TestEqual(TEXT("Off: no pull"), Off.PadPullDegreesPerSecond, 0.f);
	TestEqual(TEXT("Off: no slowdown"), Off.PadSlowdown, 1.f);

	const FHawkeyeAimAssistTuning Normal = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Normal, EHawkeyeDifficulty::Normal);
	TestEqual(TEXT("Normal: 4 degree magnetism"), Normal.MagnetismConeDegrees, 4.f);
	TestEqual(TEXT("Normal: 12 degree snap"), Normal.SnapConeDegrees, 12.f);
	TestEqual(TEXT("Normal: 2 degree bend"), Normal.BendMaxDegrees, 2.f);
	TestTrue(TEXT("The pad is helped more than the mouse"),
		Normal.PadPullDegreesPerSecond > Normal.MousePullDegreesPerSecond && Normal.PadSlowdown < Normal.MouseSlowdown);

	const FHawkeyeAimAssistTuning Strong = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Strong, EHawkeyeDifficulty::Normal);
	TestEqual(TEXT("Strong: 7 degree magnetism"), Strong.MagnetismConeDegrees, 7.f);
	TestEqual(TEXT("Strong: 12 degree snap"), Strong.SnapConeDegrees, 12.f);
	TestEqual(TEXT("Strong: 4 degree bend"), Strong.BendMaxDegrees, 4.f);
	TestTrue(TEXT("Strong pulls harder"), Strong.PadPullDegreesPerSecond > Normal.PadPullDegreesPerSecond);

	const FHawkeyeAimAssistTuning Hard = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Normal, EHawkeyeDifficulty::Hard);
	TestEqual(TEXT("Hard halves the cone"), Hard.MagnetismConeDegrees, 2.f);
	TestEqual(TEXT("Hard halves the snap"), Hard.SnapConeDegrees, 6.f);
	TestEqual(TEXT("Hard halves the bend"), Hard.BendMaxDegrees, 1.f);
	TestEqual(TEXT("Hard halves the pull"), Hard.PadPullDegreesPerSecond, Normal.PadPullDegreesPerSecond * 0.5f);
	const FHawkeyeAimAssistTuning HardStrong = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Strong, EHawkeyeDifficulty::Hard);
	TestEqual(TEXT("Hard Strong: 3.5 degree cone"), HardStrong.MagnetismConeDegrees, 3.5f);
	TestEqual(TEXT("Hard Strong: 2 degree bend"), HardStrong.BendMaxDegrees, 2.f);

	const FHawkeyeAimAssistTuning Story = UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist::Normal, EHawkeyeDifficulty::Story);
	TestEqual(TEXT("Story: 5 degree cone"), Story.MagnetismConeDegrees, 5.f);
	TestEqual(TEXT("Story: 2.5 degree bend"), Story.BendMaxDegrees, 2.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAimAssistSetting, "Hawkeye.AimAssist.SettingRoundTripsAndMigrates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAimAssistSetting::RunTest(const FString& Parameters)
{
	static const TCHAR* TestSlot = TEXT("HawkeyeAimAssistAutomationTest");
	auto ClearSlot = []()
	{
		if (UGameplayStatics::DoesSaveGameExist(TestSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
		}
	};
	auto MakeSettings = []()
	{
		UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(NewObject<UGameInstance>(GEngine));
		Settings->SlotNameOverride = TestSlot;
		return Settings;
	};
	ClearSlot();
	UHawkeyeSettingsSubsystem* Writer = MakeSettings();
	Writer->Load();
	TestEqual(TEXT("A fresh slot is Normal"), Writer->GetSettings().AimAssist, EHawkeyeAimAssist::Normal);
	Writer->SetAimAssist(EHawkeyeAimAssist::Strong);
	UHawkeyeSettingsSubsystem* Reader = MakeSettings();
	Reader->Load();
	TestEqual(TEXT("Strong survives the save"), Reader->GetSettings().AimAssist, EHawkeyeAimAssist::Strong);

	// A version 6 save (before the aim assist) keeps what the player set and comes up Normal.
	ClearSlot();
	UHawkeyeSettingsSave* Old = Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!TestNotNull(TEXT("A save object"), Old))
	{
		return false;
	}
	Old->Settings.LookSensitivity = 0.37f;
	Old->Settings.TimeOfDay = EHawkeyeTimeOfDay::Day;
	Old->Settings.AimAssist = EHawkeyeAimAssist::Normal;
	Old->Settings.Version = 6;
	UGameplayStatics::SaveGameToSlot(Old, TestSlot, 0);
	UHawkeyeSettingsSubsystem* Migrated = MakeSettings();
	Migrated->Load();
	TestEqual(TEXT("Version 6 keeps the sensitivity"), Migrated->GetLookSensitivity(), 0.37f);
	TestEqual(TEXT("And the time of day"), Migrated->GetStoredSettings().TimeOfDay, EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("And gets Normal aim assist"), Migrated->GetStoredSettings().AimAssist, EHawkeyeAimAssist::Normal);
	TestEqual(TEXT("Stamped the current version"), Migrated->GetStoredSettings().Version, FHawkeyeSettings::CurrentVersion);
	ClearSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAimAssistCone, "Hawkeye.AimAssist.ConeSlowdownAndPull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAimAssistCone::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAimAssistTest;
	const FVector Eye = FVector(0.f, 0.f, UHawkeyeAimAssist::ChestHeight);
	// Chests 3 and 5 degrees right of +X at 20 m.
	const TArray<FHawkeyeAimAssistCandidate> Candidates = { At(Yawed(5.f) * 2000.f), At(Yawed(3.f) * 2000.f) };
	TestEqual(TEXT("Normal's 4 degree cone finds the one 3 degrees off"),
		UHawkeyeAimAssist::FindBestIndex(Eye, FVector::ForwardVector, Candidates, 4.f, 6000.f), 1);
	TestEqual(TEXT("Strong's 7 takes the nearer of the two by angle"),
		UHawkeyeAimAssist::FindBestIndex(Eye, FVector::ForwardVector, Candidates, 7.f, 6000.f), 1);
	TestEqual(TEXT("Nothing inside 2 degrees"), UHawkeyeAimAssist::FindBestIndex(Eye, FVector::ForwardVector, Candidates, 2.f, 6000.f),
		INDEX_NONE);
	TestEqual(TEXT("Nothing past the range"), UHawkeyeAimAssist::FindBestIndex(Eye, FVector::ForwardVector, Candidates, 7.f, 1000.f),
		INDEX_NONE);
	TestEqual(TEXT("The angle to a chest"), UHawkeyeAimAssist::AngleToPoint(Eye, FVector::ForwardVector, Candidates[1].Chest), 3.f, 0.01f);

	TestEqual(TEXT("Right on him: the slowdown"), UHawkeyeAimAssist::ComputeLookScale(0.f, 4.f, 0.6f), 0.6f);
	TestEqual(TEXT("Half way out: half of it"), UHawkeyeAimAssist::ComputeLookScale(2.f, 4.f, 0.6f), 0.8f, 1e-4f);
	TestEqual(TEXT("At the edge: none"), UHawkeyeAimAssist::ComputeLookScale(4.f, 4.f, 0.6f), 1.f);
	TestEqual(TEXT("Off: none"), UHawkeyeAimAssist::ComputeLookScale(0.f, 0.f, 0.6f), 1.f);

	// The pull: toward his chest, weaker toward the edge, never past it, nothing outside the cone.
	const FVector Chest = Candidates[1].Chest;
	const FRotator View = FRotator::ZeroRotator;
	const FRotator Pulled = UHawkeyeAimAssist::ComputePull(View, Eye, Chest, 4.f, 6.f, 0.1f);
	const float Before = UHawkeyeAimAssist::AngleToPoint(Eye, View.Vector(), Chest);
	const float After = UHawkeyeAimAssist::AngleToPoint(Eye, Pulled.Vector(), Chest);
	TestEqual(TEXT("0.1 s at 6 deg/s a quarter from the edge turns 0.15 deg"), Before - After, 6.f * 0.25f * 0.1f, 0.01f);
	const FRotator Long = UHawkeyeAimAssist::ComputePull(View, Eye, Chest, 4.f, 600.f, 10.f);
	TestEqual(TEXT("Never past his chest"), UHawkeyeAimAssist::AngleToPoint(Eye, Long.Vector(), Chest), 0.f, 0.05f);
	TestTrue(TEXT("Outside the cone: no pull"),
		UHawkeyeAimAssist::ComputePull(View, Eye, Candidates[0].Chest, 4.f, 6.f, 0.1f).Equals(View, 1e-4f));
	const FRotator Near = UHawkeyeAimAssist::ComputePull(FRotator(0.f, 2.f, 0.f), Eye, Chest, 4.f, 6.f, 0.1f);
	TestTrue(TEXT("Stronger nearer the centre"),
		(2.f + 1.f - UHawkeyeAimAssist::AngleToPoint(Eye, Near.Vector(), Chest)) - 2.f > Before - After);

	// On his body (the head too) is on him: the pull leaves it alone. A metre to the side is not.
	const FHawkeyeAimAssistCandidate Thug = At(FVector(2000.f, 0.f, 0.f));
	TestTrue(TEXT("A line at his head is on his body"),
		UHawkeyeAimAssist::IsLineOnBody(Eye, (Thug.Head - FVector(0.f, 0.f, 10.f)) - Eye, Thug));
	TestTrue(TEXT("A line at his chest is"), UHawkeyeAimAssist::IsLineOnBody(Eye, Thug.Chest - Eye, Thug));
	TestFalse(TEXT("A line a metre wide is not"),
		UHawkeyeAimAssist::IsLineOnBody(Eye, Thug.Chest + FVector(0.f, 100.f, 0.f) - Eye, Thug));
	TestFalse(TEXT("A line away from him is not"), UHawkeyeAimAssist::IsLineOnBody(Eye, -FVector::ForwardVector, Thug));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAimAssistSnap, "Hawkeye.AimAssist.SnapOnAimPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAimAssistSnap::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAimAssistTest;
	const FVector Eye(0.f, 0.f, 60.f);
	const FVector Chest(1000.f, 100.f, 90.f);
	const FRotator Snap = UHawkeyeAimAssist::ComputeSnapRotation(Eye, Chest);
	TestTrue(TEXT("The snap looks straight at his chest"), Snap.Vector().Equals((Chest - Eye).GetSafeNormal(), 1e-4f));
	TestEqual(TEXT("No roll"), static_cast<float>(Snap.Roll), 0.f);

	for (const EHawkeyeAimAssist Level : { EHawkeyeAimAssist::Off, EHawkeyeAimAssist::Normal, EHawkeyeAimAssist::Strong })
	{
		const FScopedAssist Assist(Level, EHawkeyeDifficulty::Normal);
		FHawkeyeTestWorld TestWorld;
		AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		APlayerController* PC = Cast<APlayerController>(
			TestWorld.SpawnActor(APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		// 10 degrees off at 15 m: inside the snap cone, outside the magnetism.
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(),
			Yawed(10.f) * 1500.f, FRotator::ZeroRotator));
		if (!Kate || !PC || !Thug || !Kate->GetInventoryComponent())
		{
			AddError(TEXT("Failed to set up Kate, her controller and a thug."));
			return false;
		}
		PC->Possess(Kate);
		PC->SetControlRotation(FRotator::ZeroRotator);
		Kate->GetInventoryComponent()->GiveBow(NewObject<UBowDefinition>(Kate->GetInventoryComponent()));
		Kate->PressAim();
		const FString Name = UHawkeyeAimAssist::GetLevelName(Level).ToString();
		if (Level == EHawkeyeAimAssist::Off)
		{
			TestFalse(TEXT("Off: no snap"), Kate->IsAimSnapping());
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: the aim press snaps"), *Name), Kate->IsAimSnapping());
		TestTrue(FString::Printf(TEXT("%s: onto him"), *Name), Kate->GetAimAssistTarget() == Thug);
		Kate->UpdateAimAssist(0.05f);
		Kate->UpdateAimAssist(0.06f);
		FVector ViewLocation;
		FRotator ViewRotation;
		Kate->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		TestEqual(FString::Printf(TEXT("%s: 0.1 s later the reticle is on his chest"), *Name),
			UHawkeyeAimAssist::AngleToPoint(ViewLocation, PC->GetControlRotation().Vector(),
				Thug->GetActorLocation() + FVector(0.f, 0.f, UHawkeyeAimAssist::ChestHeight)), 0.f, 0.5f);
		TestFalse(FString::Printf(TEXT("%s: and the snap is over"), *Name), Kate->IsAimSnapping());

		// A thug 20 degrees off is outside the 12 degree cone.
		Kate->ReleaseAim();
		PC->SetControlRotation(FRotator(0.f, -10.f, 0.f));
		Kate->PressAim();
		TestFalse(FString::Printf(TEXT("%s: 20 degrees off, no snap"), *Name), Kate->IsAimSnapping());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAimAssistBend, "Hawkeye.AimAssist.ReleaseBendPerLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAimAssistBend::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAimAssistTest;
	TestTrue(TEXT("BendToward caps the turn"),
		FMath::IsNearlyEqual(UHawkeyeAimAssist::AngleToPoint(FVector::ZeroVector,
			UHawkeyeAimAssist::BendToward(FVector::ForwardVector, Yawed(5.f), 2.f), FVector::ForwardVector * 100.f), 2.f, 0.01f));
	TestTrue(TEXT("And goes all the way when it is closer"),
		UHawkeyeAimAssist::BendToward(FVector::ForwardVector, Yawed(1.f), 2.f).Equals(Yawed(1.f), 1e-4f));

	struct FCase
	{
		EHawkeyeAimAssist Level;
		EHawkeyeDifficulty Difficulty;
		float Expected;
	};
	// Aimed 1.8 degrees wide of a thug at 20 m (63 cm, outside his body, inside even Hard's 2 degree cone): the bend
	// turns toward his lead-and-drop point (about 2.4 degrees away with the drop) up to its cap.
	const FCase Cases[] = {
		{ EHawkeyeAimAssist::Off, EHawkeyeDifficulty::Normal, 0.f },
		{ EHawkeyeAimAssist::Normal, EHawkeyeDifficulty::Normal, 2.f },
		{ EHawkeyeAimAssist::Strong, EHawkeyeDifficulty::Normal, -1.f },
		{ EHawkeyeAimAssist::Normal, EHawkeyeDifficulty::Hard, 1.f },
		{ EHawkeyeAimAssist::Strong, EHawkeyeDifficulty::Hard, 2.f },
	};
	for (const FCase& Case : Cases)
	{
		const FScopedAssist Assist(Case.Level, Case.Difficulty);
		FHawkeyeTestWorld TestWorld;
		AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		AThugCharacter* Thug = Cast<AThugCharacter>(
			TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(2000.f, 0.f, 0.f), FRotator::ZeroRotator));
		UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
		if (!Bow || !Thug)
		{
			AddError(TEXT("Failed to spawn Kate and a thug."));
			return false;
		}
		TestTrue(TEXT("Her bow has the assist"), Bow->bAimAssist);
		const FString Label = FString::Printf(TEXT("%s on %s"), *UHawkeyeAimAssist::GetLevelName(Case.Level).ToString(),
			Case.Difficulty == EHawkeyeDifficulty::Hard ? TEXT("Hard") : TEXT("Normal"));
		const FVector Start(0.f, 0.f, 30.f);
		const FVector Chest = Thug->GetActorLocation() + FVector(0.f, 0.f, UHawkeyeAimAssist::ChestHeight);
		const FVector Wide = FRotator(0.f, 1.8f, 0.f).RotateVector((Chest - Start).GetSafeNormal());
		const FVector Bent = Bow->ApplyReleaseBend(Start, Wide, 6000.f);
		const float Turned = UHawkeyeAimAssist::AngleToPoint(FVector::ZeroVector, Wide, Bent * 100.f);
		if (Case.Expected >= 0.f)
		{
			TestEqual(FString::Printf(TEXT("%s: bends %.1f deg"), *Label, Case.Expected), Turned, Case.Expected, 0.05f);
		}
		else
		{
			// Strong's 4 is more than the 2.4 needed: all the way onto the lead-and-drop point.
			TestTrue(FString::Printf(TEXT("%s: all the way (%.2f deg)"), *Label, Turned), Turned > 2.05f && Turned < 3.f);
		}

		// Her own aim on his head is left alone: the headshot is hers.
		const FVector AtHead = (Thug->GetActorLocation() + FVector(0.f, 0.f, 80.f) - Start).GetSafeNormal();
		TestTrue(FString::Printf(TEXT("%s: a line on his head is not bent"), *Label),
			Bow->ApplyReleaseBend(Start, AtHead, 6000.f).Equals(AtHead, 1e-5f));
		// Far outside the cone: nothing.
		const FVector Away = FRotator(0.f, 20.f, 0.f).RotateVector((Chest - Start).GetSafeNormal());
		TestTrue(FString::Printf(TEXT("%s: 20 degrees off is not bent"), *Label), Bow->ApplyReleaseBend(Start, Away, 6000.f).Equals(Away, 1e-5f));
	}
	return true;
}

#endif
