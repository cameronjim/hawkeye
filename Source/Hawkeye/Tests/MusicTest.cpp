#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeMusicRules.h"
#include "Audio/HawkeyeMusicSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "EnhancedActionKeyMapping.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Settings/HawkeyeSettings.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/ThugCharacter.h"
#include "World/TimeOfDaySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The combat music's rules (claude-docs/gameplay-semantics.md, "Audio", music), the Music slider's save, the
 * day street bed, and the partner mark's pad button. Headless: the rules are pure and the world test only
 * reads thugs, so nothing here needs an audio device.
 */
namespace HawkeyeMusicTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static FHawkeyeMusicInputs Calm()
	{
		return FHawkeyeMusicInputs();
	}

	static FHawkeyeMusicInputs Alerted(int32 Count, float NearestCm, bool bBoss = false)
	{
		FHawkeyeMusicInputs In;
		In.AlertedStanding = Count;
		In.NearestAlertedCm = NearestCm;
		In.bBossAlerted = bBoss;
		In.EngagedStanding = Count;
		return In;
	}

	/** Steps Director by Seconds in 0.1 s frames. */
	static void Run(FHawkeyeMusicDirector& Director, const FHawkeyeMusicInputs& In, float Seconds)
	{
		for (int32 Frame = FMath::RoundToInt(Seconds / 0.1f); Frame > 0; --Frame)
		{
			Director.Step(In, 0.1f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicTargets, "Hawkeye.Music.TargetStates", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicTargets::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	TestEqual(TEXT("Nobody alerted: Roam"), HawkeyeMusic::ComputeTarget(Calm()), EHawkeyeMusicState::Roam);
	TestEqual(TEXT("One alerted at 20 m: Alert"), HawkeyeMusic::ComputeTarget(Alerted(1, 2000.f)), EHawkeyeMusicState::Alert);
	TestEqual(TEXT("One alerted at 8.1 m: still Alert"), HawkeyeMusic::ComputeTarget(Alerted(1, 810.f)), EHawkeyeMusicState::Alert);
	TestEqual(TEXT("One alerted at 8 m: Fight"), HawkeyeMusic::ComputeTarget(Alerted(1, 800.f)), EHawkeyeMusicState::Fight);
	TestEqual(TEXT("Two alerted far off: Fight"), HawkeyeMusic::ComputeTarget(Alerted(2, 3000.f)), EHawkeyeMusicState::Fight);
	TestEqual(TEXT("An alerted archer (or boss) far off: Duel"), HawkeyeMusic::ComputeTarget(Alerted(1, 2500.f, true)),
		EHawkeyeMusicState::Duel);

	// The heavy is a duel only with a crowd: on his own (or with one other) he is an ordinary fight.
	FHawkeyeMusicInputs Heavy = Alerted(1, 2500.f);
	Heavy.bHeavyAlerted = true;
	TestEqual(TEXT("The heavy alone, far off: Alert, no motif"), HawkeyeMusic::ComputeTarget(Heavy), EHawkeyeMusicState::Alert);
	Heavy.NearestAlertedCm = 300.f;
	TestEqual(TEXT("The heavy alone, close: Fight, no motif"), HawkeyeMusic::ComputeTarget(Heavy), EHawkeyeMusicState::Fight);
	Heavy.AlertedStanding = 2;
	TestEqual(TEXT("The heavy and one other: Fight"), HawkeyeMusic::ComputeTarget(Heavy), EHawkeyeMusicState::Fight);
	Heavy.AlertedStanding = 3;
	TestEqual(TEXT("The heavy and two others: Duel"), HawkeyeMusic::ComputeTarget(Heavy), EHawkeyeMusicState::Duel);
	TestEqual(TEXT("And its layers carry the motif"), HawkeyeMusic::GetLayersFor(HawkeyeMusic::ComputeTarget(Heavy), Heavy).Motif, 1.f);
	Heavy.AlertedStanding = 1;
	TestEqual(TEXT("And alone its layers do not"), HawkeyeMusic::GetLayersFor(HawkeyeMusic::ComputeTarget(Heavy), Heavy).Motif, 0.f);
	TestEqual(TEXT("A boss in a crowd is still a Duel"), HawkeyeMusic::ComputeTarget(Alerted(3, 300.f, true)), EHawkeyeMusicState::Duel);

	FHawkeyeMusicInputs Crime = Calm();
	Crime.bCrimeNearby = true;
	TestEqual(TEXT("A crime on nearby, nobody alerted yet: Alert"), HawkeyeMusic::ComputeTarget(Crime), EHawkeyeMusicState::Alert);

	FHawkeyeMusicInputs Down = Alerted(3, 200.f, true);
	Down.bPlayerDowned = true;
	TestEqual(TEXT("Downed beats a duel"), HawkeyeMusic::ComputeTarget(Down), EHawkeyeMusicState::Downed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicCooldown, "Hawkeye.Music.RisesAtOnceFallsAfterCooldown", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicCooldown::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	FHawkeyeMusicDirector Director;
	TestEqual(TEXT("Starts in Roam"), Director.GetState(), EHawkeyeMusicState::Roam);
	TestTrue(TEXT("Roam to Alert changes"), Director.Step(Alerted(1, 2000.f), 0.1f));
	TestEqual(TEXT("Alert at once"), Director.GetState(), EHawkeyeMusicState::Alert);
	Director.Step(Alerted(2, 2000.f), 0.1f);
	TestEqual(TEXT("Fight at once"), Director.GetState(), EHawkeyeMusicState::Fight);

	// One of the two loses her: the level wants Alert, but Fight holds for 4 s.
	FHawkeyeMusicInputs One = Alerted(1, 2000.f);
	One.EngagedStanding = 2;
	Run(Director, One, 3.9f);
	TestEqual(TEXT("Still Fight after 3.9 s"), Director.GetState(), EHawkeyeMusicState::Fight);
	Run(Director, One, 0.2f);
	TestEqual(TEXT("Alert after 4 s"), Director.GetState(), EHawkeyeMusicState::Alert);

	// Both lose her: Alert holds 4 s, then Roam.
	FHawkeyeMusicInputs Lost = Calm();
	Lost.EngagedStanding = 2;
	Run(Director, Lost, 2.f);
	// A rise mid-cooldown resets the wait.
	Director.Step(Alerted(1, 2000.f), 0.1f);
	TestEqual(TEXT("Seeing her again holds Alert"), Director.GetState(), EHawkeyeMusicState::Alert);
	TestEqual(TEXT("And resets the cooldown"), Director.GetCooldown(), 0.f);
	Run(Director, Lost, 3.9f);
	TestEqual(TEXT("Alert 3.9 s after losing her again"), Director.GetState(), EHawkeyeMusicState::Alert);
	Run(Director, Lost, 0.2f);
	TestEqual(TEXT("Roam after 4 s"), Director.GetState(), EHawkeyeMusicState::Roam);
	TestEqual(TEXT("No win: nobody went down"), Director.GetPreviousState(), EHawkeyeMusicState::Alert);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicDuelSteps, "Hawkeye.Music.DuelStepsDownOneLevelAtATime", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicDuelSteps::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	FHawkeyeMusicDirector Director;
	Director.Step(Alerted(1, 2500.f, true), 0.1f);
	TestEqual(TEXT("An archer: straight to Duel"), Director.GetState(), EHawkeyeMusicState::Duel);
	FHawkeyeMusicInputs Lost = Calm();
	Lost.EngagedStanding = 1;
	Run(Director, Lost, 4.05f);
	TestEqual(TEXT("4 s later: Fight"), Director.GetState(), EHawkeyeMusicState::Fight);
	Run(Director, Lost, 4.05f);
	TestEqual(TEXT("8 s: Alert"), Director.GetState(), EHawkeyeMusicState::Alert);
	Run(Director, Lost, 4.05f);
	TestEqual(TEXT("12 s: Roam"), Director.GetState(), EHawkeyeMusicState::Roam);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicWin, "Hawkeye.Music.WinStingWhenTheLastOneIsDown", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicWin::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	FHawkeyeMusicDirector Director;
	Director.Step(Alerted(2, 500.f), 0.1f);
	FHawkeyeMusicInputs OneLeft = Alerted(1, 500.f);
	OneLeft.EngagedStanding = 1;
	OneLeft.EngagedDown = 1;
	Director.Step(OneLeft, 0.1f);
	TestEqual(TEXT("One down, one standing: still Fight"), Director.GetState(), EHawkeyeMusicState::Fight);

	FHawkeyeMusicInputs Won = Calm();
	Won.EngagedDown = 2;
	TestTrue(TEXT("The last one down changes the state"), Director.Step(Won, 0.1f));
	TestEqual(TEXT("Win, with no cooldown"), Director.GetState(), EHawkeyeMusicState::Win);
	TestEqual(TEXT("A 2 s sting"), Director.GetWinRemaining(), 2.f, 0.001f);
	Run(Director, Won, 1.8f);
	TestEqual(TEXT("Still the sting at 1.8 s"), Director.GetState(), EHawkeyeMusicState::Win);
	Run(Director, Won, 0.3f);
	TestEqual(TEXT("Roam after 2 s"), Director.GetState(), EHawkeyeMusicState::Roam);

	// A new thug alerted during the sting cuts it short.
	FHawkeyeMusicDirector Again;
	Again.Step(Alerted(1, 300.f), 0.1f);
	FHawkeyeMusicInputs Down = Calm();
	Down.EngagedDown = 1;
	Again.Step(Down, 0.1f);
	TestEqual(TEXT("A fight of one also wins"), Again.GetState(), EHawkeyeMusicState::Win);
	FHawkeyeMusicInputs Fresh = Alerted(1, 300.f);
	Fresh.EngagedDown = 1;
	Again.Step(Fresh, 0.1f);
	TestEqual(TEXT("Another one on her: straight back to Fight"), Again.GetState(), EHawkeyeMusicState::Fight);

	// A takedown in Roam (never alerted, so never engaged) is no win.
	FHawkeyeMusicDirector Quiet;
	Quiet.Step(Calm(), 0.1f);
	TestEqual(TEXT("Nothing engaged, nothing to win"), Quiet.GetState(), EHawkeyeMusicState::Roam);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicDowned, "Hawkeye.Music.DownedDroneAndRevive", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicDowned::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	FHawkeyeMusicDirector Director;
	Director.Step(Alerted(1, 2500.f, true), 0.1f);
	FHawkeyeMusicInputs Down = Alerted(1, 2500.f, true);
	Down.bPlayerDowned = true;
	Director.Step(Down, 0.1f);
	TestEqual(TEXT("Downed at once, from a duel"), Director.GetState(), EHawkeyeMusicState::Downed);
	Run(Director, Down, 5.f);
	TestEqual(TEXT("And held while she is down"), Director.GetState(), EHawkeyeMusicState::Downed);

	// Revived with one thug still on her: straight to the fight, no cooldown from Downed.
	Director.Step(Alerted(1, 2000.f), 0.1f);
	TestEqual(TEXT("Revived: Alert straight away"), Director.GetState(), EHawkeyeMusicState::Alert);

	// Downed with everyone down (the last one's arrow landed as she fell) is not a win until she is up.
	FHawkeyeMusicInputs DownAndWon = Calm();
	DownAndWon.bPlayerDowned = true;
	DownAndWon.EngagedDown = 1;
	Director.Step(DownAndWon, 0.1f);
	TestEqual(TEXT("Downed wins over the win"), Director.GetState(), EHawkeyeMusicState::Downed);

	const FHawkeyeMusicLayers Drone = HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Downed, Calm());
	TestEqual(TEXT("Downed plays the drone"), Drone.Drone, 1.f);
	TestEqual(TEXT("And no pulse"), Drone.Pulse, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicLayersTest, "Hawkeye.Music.LayersAndCrossfades", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicLayersTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	FHawkeyeMusicInputs Night = Calm();
	FHawkeyeMusicInputs Day = Calm();
	Day.bNight = false;
	FHawkeyeMusicInputs DayChallenge = Day;
	DayChallenge.bChallengeRunning = true;
	TestEqual(TEXT("Roam at night: the pad, quietly"), HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Roam, Night).Pad, 0.5f);
	TestEqual(TEXT("Roam by day: silence"), HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Roam, Day).Pad, 0.f);
	TestEqual(TEXT("A challenge by day: the pad"), HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Roam, DayChallenge).Pad, 0.5f);

	const FHawkeyeMusicLayers Alert = HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Alert, Night);
	TestTrue(TEXT("Alert: the pulse only"), Alert.Pulse == 1.f && Alert.Perc == 0.f && Alert.Motif == 0.f && Alert.Pad == 0.f);
	const FHawkeyeMusicLayers Fight = HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Fight, Night);
	TestTrue(TEXT("Fight: pulse and percussion"), Fight.Pulse == 1.f && Fight.Perc == 1.f && Fight.Motif == 0.f);
	const FHawkeyeMusicLayers Duel = HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Duel, Night);
	TestTrue(TEXT("Duel: pulse, percussion and the motif"), Duel.Pulse == 1.f && Duel.Perc == 1.f && Duel.Motif == 1.f);
	const FHawkeyeMusicLayers Win = HawkeyeMusic::GetLayersFor(EHawkeyeMusicState::Win, Night);
	TestTrue(TEXT("Win: every layer out under the sting"),
		Win.Pad == 0.f && Win.Pulse == 0.f && Win.Perc == 0.f && Win.Motif == 0.f && Win.Drone == 0.f);

	// Crossfades: in over 1.5 s, out over 2 s, out over 1 s into Win and Downed. Never a jump.
	FHawkeyeMusicLayers Layers;
	Layers = HawkeyeMusic::AdvanceLayers(Layers, Duel, 0.75f, false);
	TestEqual(TEXT("Half way in after 0.75 s"), Layers.Pulse, 0.5f, 0.001f);
	Layers = HawkeyeMusic::AdvanceLayers(Layers, Duel, 0.75f, false);
	TestEqual(TEXT("Full after 1.5 s"), Layers.Motif, 1.f, 0.001f);
	FHawkeyeMusicLayers Out = HawkeyeMusic::AdvanceLayers(Layers, Alert, 1.f, false);
	TestEqual(TEXT("Half way out after 1 s"), Out.Perc, 0.5f, 0.001f);
	TestEqual(TEXT("The pulse it keeps stays"), Out.Pulse, 1.f, 0.001f);
	FHawkeyeMusicLayers Fast = HawkeyeMusic::AdvanceLayers(Layers, Win, 0.5f, true);
	TestEqual(TEXT("Into Win: half way out after 0.5 s"), Fast.Pulse, 0.5f, 0.001f);
	FHawkeyeMusicLayers Frame = HawkeyeMusic::AdvanceLayers(Layers, Win, 1.f / 60.f, false);
	TestTrue(TEXT("One frame moves a layer by at most 1/90"), 1.f - Frame.Pulse <= 1.f / 90.f + 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicGathers, "Hawkeye.Music.GathersFromTheThugs", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicGathers::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AThugCharacter* Bat = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator));
	AThugCharacter* Archer = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(2500.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!World || !Kate || !Bat || !Archer)
	{
		AddError(TEXT("Could not spawn Kate and the thugs."));
		return false;
	}
	Bat->Weapon = EThugWeapon::Bat;
	Archer->Weapon = EThugWeapon::Bow;
	UHawkeyeMusicSubsystem* Music = World->GetSubsystem<UHawkeyeMusicSubsystem>();
	if (!Music)
	{
		Music = NewObject<UHawkeyeMusicSubsystem>(World);
	}

	FHawkeyeMusicInputs In = Music->GatherInputs(Kate);
	TestEqual(TEXT("Nobody alerted"), In.AlertedStanding, 0);
	TestEqual(TEXT("Nothing engaged"), In.EngagedStanding + In.EngagedDown, 0);

	Bat->SetAlertState(EThugAlertState::Alerted);
	In = Music->GatherInputs(Kate);
	TestEqual(TEXT("The bat thug is alerted"), In.AlertedStanding, 1);
	TestEqual(TEXT("5 m away"), In.NearestAlertedCm, 500.f, 1.f);
	TestFalse(TEXT("A bat is no boss"), In.bBossAlerted);
	TestEqual(TEXT("Within 8 m: Fight"), HawkeyeMusic::ComputeTarget(In), EHawkeyeMusicState::Fight);

	Archer->SetAlertState(EThugAlertState::Alerted);
	In = Music->GatherInputs(Kate);
	TestTrue(TEXT("An alerted archer is the duel"), In.bBossAlerted);
	TestEqual(TEXT("Duel"), HawkeyeMusic::ComputeTarget(In), EHawkeyeMusicState::Duel);

	TestFalse(TEXT("No heavy among them"), In.bHeavyAlerted);

	// The archer loses her: still engaged, still standing, so no win and no duel.
	Archer->SetAlertState(EThugAlertState::Suspicious);
	In = Music->GatherInputs(Kate);
	TestFalse(TEXT("A suspicious archer is no duel"), In.bBossAlerted);
	TestEqual(TEXT("Both engaged and up"), In.EngagedStanding, 2);

	// The heavy seen alongside the bat is a heavy, not a boss: a fight until a third joins.
	Archer->Weapon = EThugWeapon::Shield;
	Archer->SetAlertState(EThugAlertState::Alerted);
	In = Music->GatherInputs(Kate);
	TestTrue(TEXT("An alerted heavy is noted"), In.bHeavyAlerted);
	TestFalse(TEXT("But he is no boss"), In.bBossAlerted);
	TestEqual(TEXT("The heavy and the bat: Fight, not Duel"), HawkeyeMusic::ComputeTarget(In), EHawkeyeMusicState::Fight);

	Bat->GoLimp(Kate);
	Archer->GoLimp(Kate);
	In = Music->GatherInputs(Kate);
	TestEqual(TEXT("Nobody standing alerted"), In.AlertedStanding, 0);
	TestEqual(TEXT("Both engaged thugs down"), In.EngagedDown, 2);
	TestEqual(TEXT("None of them up"), In.EngagedStanding, 0);

	// The subsystem runs the director the same way: a fight, then the sting when they go down.
	FHawkeyeMusicInputs Fight;
	Fight.AlertedStanding = 1;
	Fight.NearestAlertedCm = 300.f;
	Fight.EngagedStanding = 1;
	Music->Advance(Fight, 0.1f);
	TestEqual(TEXT("The subsystem's state: Fight"), Music->GetState(), EHawkeyeMusicState::Fight);
	Music->Advance(In, 0.1f);
	TestEqual(TEXT("And Win"), Music->GetState(), EHawkeyeMusicState::Win);
	TestEqual(TEXT("Two transitions counted"), Music->GetTransitionCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicWinOnce, "Hawkeye.Music.WinOncePerFight", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicWinOnce::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMusicTest;
	// Found by the crime screenshot pass: a fight won, then a crime starting nearby, flipped Win and Alert every frame.
	FHawkeyeMusicDirector Director;
	Director.Step(Alerted(2, 300.f), 0.1f);
	FHawkeyeMusicInputs Won;
	Won.EngagedDown = 2;
	Director.Step(Won, 0.1f);
	TestEqual(TEXT("The fight won: Win"), Director.GetState(), EHawkeyeMusicState::Win);
	FHawkeyeMusicInputs CrimeAfter = Won;
	CrimeAfter.bCrimeNearby = true;
	int32 Changes = 0;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Changes += Director.Step(CrimeAfter, 0.1f) ? 1 : 0;
	}
	TestEqual(TEXT("A crime nearby does not cut the sting, nor flip it"), Changes, 0);
	TestEqual(TEXT("Still the sting"), Director.GetState(), EHawkeyeMusicState::Win);
	Run(Director, CrimeAfter, 2.f);
	TestEqual(TEXT("After it, the crime's pulse"), Director.GetState(), EHawkeyeMusicState::Alert);
	Changes = 0;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Changes += Director.Step(CrimeAfter, 0.1f) ? 1 : 0;
	}
	TestEqual(TEXT("And the won fight does not sting again"), Changes, 0);

	// Someone new seen and put down is a new fight: its own sting.
	FHawkeyeMusicInputs New = Alerted(1, 300.f);
	New.EngagedDown = 2;
	Director.Step(New, 0.1f);
	TestEqual(TEXT("A new thug: Fight"), Director.GetState(), EHawkeyeMusicState::Fight);
	FHawkeyeMusicInputs NewWon;
	NewWon.EngagedDown = 3;
	Director.Step(NewWon, 0.1f);
	TestEqual(TEXT("Down too: Win again"), Director.GetState(), EHawkeyeMusicState::Win);

	// An alerted thug during the sting still cuts it.
	FHawkeyeMusicInputs Cut = Alerted(1, 300.f);
	Cut.EngagedDown = 3;
	Director.Step(Cut, 0.1f);
	TestEqual(TEXT("A thug alerted in the sting: back to the fight"), Director.GetState(), EHawkeyeMusicState::Fight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicPauses, "Hawkeye.Music.PausesWithPauseScreens", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicPauses::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UHawkeyeMusicSubsystem* Music = World ? World->GetSubsystem<UHawkeyeMusicSubsystem>() : nullptr;
	if (!Music)
	{
		AddError(TEXT("No music subsystem in the test world."));
		return false;
	}
	TestTrue(TEXT("It ticks under a pause, to hold the sound"), Music->IsTickableWhenPaused());
	FHawkeyeMusicInputs Fight;
	Fight.AlertedStanding = 2;
	Fight.NearestAlertedCm = 300.f;
	Fight.EngagedStanding = 2;
	Music->Advance(Fight, 0.1f);
	TestEqual(TEXT("A fight on"), Music->GetState(), EHawkeyeMusicState::Fight);
	TestFalse(TEXT("Not held before any pause"), Music->IsScorePaused());

	// A pause screen pauses the world (SetPause names a pauser): the next tick holds the music and does not
	// step the rules, however long the frame.
	APlayerState* Pauser = Cast<APlayerState>(TestWorld.SpawnActor(APlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	TestTrue(TEXT("The world is paused"), World->IsPaused());
	const int32 Before = Music->GetTransitionCount();
	Music->Tick(0.1f);
	TestTrue(TEXT("Held while the screen is open"), Music->IsScorePaused());
	Music->Tick(10.f);
	TestEqual(TEXT("Its state kept"), Music->GetState(), EHawkeyeMusicState::Fight);
	TestEqual(TEXT("No transition under the pause"), Music->GetTransitionCount(), Before);

	// Closing it lets go; the fight carries on where it was.
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Music->Tick(0.1f);
	TestFalse(TEXT("Resumed on close"), Music->IsScorePaused());
	TestEqual(TEXT("Still the fight"), Music->GetState(), EHawkeyeMusicState::Fight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMusicVolumeSetting, "Hawkeye.Music.VolumeSettingRoundTrip", HawkeyeMusicTest::Flags)

bool FHawkeyeMusicVolumeSetting::RunTest(const FString& Parameters)
{
	const FString Slot = TEXT("HawkeyeMusicVolumeTest");
	auto ClearSlot = [&Slot]()
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
		}
	};
	auto MakeSettings = [&Slot]()
	{
		UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(NewObject<UGameInstance>(GEngine));
		Settings->SlotNameOverride = Slot;
		return Settings;
	};
	TestTrue(TEXT("Settings version 9 or later (9 added the Music slider)"), FHawkeyeSettings::CurrentVersion >= 9);
	ClearSlot();
	UHawkeyeSettingsSubsystem* Writer = MakeSettings();
	Writer->Load();
	TestEqual(TEXT("A fresh slot's music is 0.6"), Writer->GetMusicVolume(), 0.6f);
	Writer->SetMusicVolume(0.3f);
	Writer->SetAmbientVolume(0.45f);
	UHawkeyeSettingsSubsystem* Reader = MakeSettings();
	Reader->Load();
	TestEqual(TEXT("0.3 survives the save"), Reader->GetMusicVolume(), 0.3f);
	TestEqual(TEXT("Next to the ambience"), Reader->GetAmbientVolume(), 0.45f);
	Reader->SetMusicVolume(1.7f);
	TestEqual(TEXT("Clamped to 1"), Reader->GetMusicVolume(), 1.f);

	// A version 8 save (before the slider) keeps what the player set and gets the default music.
	ClearSlot();
	UHawkeyeSettingsSave* Old = Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!TestNotNull(TEXT("A save object"), Old))
	{
		return false;
	}
	Old->Settings.LookSensitivity = 0.31f;
	Old->Settings.AmbientVolume = 0.5f;
	Old->Settings.MusicVolume = 0.6f;
	Old->Settings.Version = 8;
	UGameplayStatics::SaveGameToSlot(Old, Slot, 0);
	UHawkeyeSettingsSubsystem* Migrated = MakeSettings();
	Migrated->Load();
	TestEqual(TEXT("Version 8 keeps the sensitivity"), Migrated->GetLookSensitivity(), 0.31f);
	TestEqual(TEXT("And the ambience"), Migrated->GetAmbientVolume(), 0.5f);
	TestEqual(TEXT("And gets the default music"), Migrated->GetMusicVolume(), 0.6f);
	TestEqual(TEXT("Stamped the current version"), Migrated->GetStoredSettings().Version, FHawkeyeSettings::CurrentVersion);
	ClearSlot();

	// The slider's gain, under the master, reaches the controller's SCL_Music override.
	FHawkeyeSettings Values;
	Values.MasterVolume = 0.5f;
	Values.MusicVolume = 0.6f;
	const FHawkeyeClassVolumes Volumes = HawkeyeAudioMath::ComputeClassVolumes(Values);
	TestEqual(TEXT("music = 0.25 x 0.36"), Volumes.Music, 0.09f, 1e-5f);
	Values.MusicVolume = 0.f;
	TestEqual(TEXT("music 0 is silent"), HawkeyeAudioMath::ComputeClassVolumes(Values).Music, 0.f);
	FHawkeyeTestWorld World;
	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(
		World.SpawnActor(AHawkeyePlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("a controller"), PC))
	{
		return false;
	}
	Values.MasterVolume = 1.f;
	Values.MusicVolume = 0.5f;
	PC->ApplyVolumeSettings(Values);
	TestEqual(TEXT("controller music = 0.25"), PC->GetAppliedVolumes().Music, 0.25f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDayBedSelection, "Hawkeye.Audio.DayBedSelection", HawkeyeMusicTest::Flags)

bool FHawkeyeDayBedSelection::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Night's row: all night bed"), UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Night).DayBedWeight, 0.f);
	TestEqual(TEXT("Day's row: all day bed"), UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Day).DayBedWeight, 1.f);

	float NightBed = -1.f;
	float DayBed = -1.f;
	HawkeyeAudioMath::ComputeStreetBeds(0.8f, 0.f, NightBed, DayBed);
	TestEqual(TEXT("At night the night bed carries the street"), NightBed, 0.8f, 1e-5f);
	TestEqual(TEXT("And the day bed is silent"), DayBed, 0.f);
	HawkeyeAudioMath::ComputeStreetBeds(0.8f, 1.f, NightBed, DayBed);
	TestEqual(TEXT("By day the night bed is silent"), NightBed, 0.f);
	TestEqual(TEXT("And the day bed carries it"), DayBed, 0.8f, 1e-5f);
	HawkeyeAudioMath::ComputeStreetBeds(1.f, 0.5f, NightBed, DayBed);
	TestEqual(TEXT("Half way, an even split"), NightBed + DayBed, 1.f, 1e-5f);
	HawkeyeAudioMath::ComputeStreetBeds(1.f, 3.f, NightBed, DayBed);
	TestEqual(TEXT("A weight past 1 clamps"), DayBed, 1.f);
	TestTrue(TEXT("The bed crossfade is 1 to 2 s"), HawkeyeAudioMath::BedCrossfadeSeconds >= 1.f && HawkeyeAudioMath::BedCrossfadeSeconds <= 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePartnerMarkPad, "Hawkeye.Partner.MarkOnPadIsR3", HawkeyeMusicTest::Flags)

bool FHawkeyePartnerMarkPad::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Imc = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IMC_Default (run create_input_assets)"), Imc))
	{
		return false;
	}
	int32 OnR3 = 0;
	bool bMarkOnT = false;
	bool bMarkOnR3 = false;
	for (const FEnhancedActionKeyMapping& Mapping : Imc->GetMappings())
	{
		const FString Action = GetNameSafe(Mapping.Action);
		if (Mapping.Key == EKeys::Gamepad_RightThumbstick)
		{
			++OnR3;
			bMarkOnR3 |= Action == TEXT("IA_PartnerMark");
		}
		bMarkOnT |= Action == TEXT("IA_PartnerMark") && Mapping.Key == EKeys::T;
	}
	TestTrue(TEXT("R3 sends the partner"), bMarkOnR3);
	TestEqual(TEXT("And does nothing else"), OnR3, 1);
	TestTrue(TEXT("T still does on keys"), bMarkOnT);
	TestEqual(TEXT("The keys hint"), UHawkeyeHudWidget::FormatPartnerMarkHint(false).ToString(), FString(TEXT("[T] send")));
	TestEqual(TEXT("The pad hint"), UHawkeyeHudWidget::FormatPartnerMarkHint(true).ToString(), FString(TEXT("[R3] send")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
