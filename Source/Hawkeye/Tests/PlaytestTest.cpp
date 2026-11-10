#include "Misc/AutomationTest.h"
#include "Playtest/PlaytestPhotoMode.h"
#include "Playtest/PlaytestTypes.h"
#include "Playtest/PhotoModeWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyePlaytestTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FPlaytestState MakeState()
	{
		FPlaytestState S;
		S.Movement = TEXT("Walking");
		S.Speed = 512.4f;
		S.bSprinting = true;
		S.Parkour = TEXT("None");
		S.bAiming = true;
		S.Health = 87.6f;
		S.AlertedThugs = 2;
		S.Crime = TEXT("[Mugging] 1");
		S.Map = TEXT("L_District_EastVillage");
		S.TimeOfDay = TEXT("Night");
		S.Difficulty = TEXT("Normal");
		return S;
	}

	FPlaytestNote MakeNote()
	{
		FPlaytestNote Note;
		Note.Index = 3;
		Note.SessionSeconds = 734.24f;
		Note.WallClock = TEXT("2026-09-29 14:03:11");
		Note.Position = FVector(1234.4, -567.6, 89.f);
		Note.Yaw = 45.2f;
		Note.State = MakeState();
		Note.StateText = HawkeyePlaytest::DescribeState(Note.State);
		Note.Screenshot = TEXT("note_3.png");
		Note.ScreenshotPath = TEXT("C:/Saved/Playtest/2026-09-29_14-00-00/note_3.png");
		Note.RecentLog = { TEXT("[14:03:10.001] line one"), TEXT("[14:03:11.002] Warning: \"quoted\" line two") };
		return Note;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePlaytestStateString, "Hawkeye.Playtest.StateString", HawkeyePlaytestTest::Flags)

bool FHawkeyePlaytestStateString::RunTest(const FString& Parameters)
{
	using namespace HawkeyePlaytestTest;
	const FPlaytestState S = MakeState();
	TestEqual(TEXT("Every field, in order, spaces made underscores, empties a dash"), HawkeyePlaytest::DescribeState(S),
		FString(TEXT("move=Walking speed=512 sprint=yes crouch=no parkour=None zip=no aim=yes downed=no health=88 fight=2 ")
			TEXT("crime=[Mugging]_1 challenge=- interior=no map=L_District_EastVillage tod=Night difficulty=Normal paused=no")));

	const FPlaytestState Empty;
	TestTrue(TEXT("A default state still names every field"),
		HawkeyePlaytest::DescribeState(Empty).StartsWith(TEXT("move=None speed=0 sprint=no")));

	TestEqual(TEXT("The NOTE line"), HawkeyePlaytest::FormatNoteLine(MakeNote()),
		FString(TEXT("NOTE #3 at 734.2s: pos=(1234,-568,89) yaw=45 state=")) + HawkeyePlaytest::DescribeState(S));

	// The movement key: first match wins.
	FPlaytestState K;
	TestEqual(TEXT("Standing still"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Idle")));
	K.Speed = 150.f;
	TestEqual(TEXT("Slow"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Walk")));
	K.Speed = 500.f;
	TestEqual(TEXT("Fast"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Run")));
	K.bSprinting = true;
	TestEqual(TEXT("Sprint over run"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Sprint")));
	K.bCrouching = true;
	TestEqual(TEXT("Crouch (a slide) over sprint"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Crouch")));
	K.Movement = TEXT("Falling");
	TestEqual(TEXT("Air over the ground states"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Air")));
	K.Parkour = TEXT("Mantle");
	TestEqual(TEXT("The parkour move over air"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Mantle")));
	K.bZipping = true;
	TestEqual(TEXT("Zip over parkour"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Zip")));
	K.bDowned = true;
	TestEqual(TEXT("Downed over zip"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Downed")));
	K.bPaused = true;
	TestEqual(TEXT("Paused over everything"), HawkeyePlaytest::MovementKey(K), FString(TEXT("Paused")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePlaytestNoteJson, "Hawkeye.Playtest.NoteJsonRoundTrip", HawkeyePlaytestTest::Flags)

bool FHawkeyePlaytestNoteJson::RunTest(const FString& Parameters)
{
	using namespace HawkeyePlaytestTest;
	FPlaytestNotesFile File;
	File.Session = TEXT("2026-09-29_14-00-00");
	File.Notes.Add(MakeNote());
	FPlaytestNote Second = MakeNote();
	Second.Index = 4;
	Second.State.bZipping = true;
	Second.State.Parkour = TEXT("Hanging");
	Second.RecentLog.Reset();
	File.Notes.Add(Second);

	const FString Json = HawkeyePlaytest::NotesToJson(File);
	TestTrue(TEXT("The report reads these keys"), Json.Contains(TEXT("\"notes\"")) && Json.Contains(TEXT("\"stateText\""))
		&& Json.Contains(TEXT("\"recentLog\"")) && Json.Contains(TEXT("\"screenshot\"")) && Json.Contains(TEXT("\"alertedThugs\"")));

	FPlaytestNotesFile Back;
	if (!TestTrue(TEXT("Parses back"), HawkeyePlaytest::NotesFromJson(Json, Back)))
	{
		return false;
	}
	TestEqual(TEXT("Session"), Back.Session, File.Session);
	if (!TestEqual(TEXT("Both notes"), Back.Notes.Num(), 2))
	{
		return false;
	}
	const FPlaytestNote& A = File.Notes[0];
	const FPlaytestNote& B = Back.Notes[0];
	TestEqual(TEXT("Index"), B.Index, A.Index);
	TestEqual(TEXT("Seconds"), B.SessionSeconds, A.SessionSeconds);
	TestEqual(TEXT("Wall clock"), B.WallClock, A.WallClock);
	TestTrue(TEXT("Position"), B.Position.Equals(A.Position, 0.01));
	TestEqual(TEXT("Yaw"), B.Yaw, A.Yaw);
	TestEqual(TEXT("State text"), B.StateText, A.StateText);
	TestEqual(TEXT("State re-described"), HawkeyePlaytest::DescribeState(B.State), A.StateText);
	TestEqual(TEXT("Crime"), B.State.Crime, A.State.Crime);
	TestEqual(TEXT("Alerted"), B.State.AlertedThugs, 2);
	TestTrue(TEXT("Flags"), B.State.bAiming && B.State.bSprinting && !B.State.bZipping);
	TestEqual(TEXT("Screenshot"), B.Screenshot, A.Screenshot);
	TestEqual(TEXT("Screenshot path"), B.ScreenshotPath, A.ScreenshotPath);
	TestEqual(TEXT("Log lines, quotes and all"), B.RecentLog, A.RecentLog);
	TestTrue(TEXT("Second note's zip and hang"), Back.Notes[1].State.bZipping && Back.Notes[1].State.Parkour == TEXT("Hanging"));
	TestEqual(TEXT("Second note has no log"), Back.Notes[1].RecentLog.Num(), 0);

	FPlaytestNotesFile Junk;
	TestFalse(TEXT("Junk does not parse"), HawkeyePlaytest::NotesFromJson(TEXT("{ not json"), Junk));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePlaytestSummaryCounters, "Hawkeye.Playtest.SummaryCounters", HawkeyePlaytestTest::Flags)

bool FHawkeyePlaytestSummaryCounters::RunTest(const FString& Parameters)
{
	FPlaytestSummary S;
	S.Session = TEXT("s");

	TestTrue(TEXT("A step counts"), S.AddStep(FVector::ZeroVector, FVector(300.f, 400.f, 0.f)));
	TestFalse(TEXT("A teleport does not"), S.AddStep(FVector::ZeroVector, FVector(FPlaytestSummary::MaxStepCm + 1.f, 0.f, 0.f)));
	TestEqual(TEXT("5 m"), S.DistanceMetres, 5.f);

	S.AddMovement(TEXT("Run"), 1.5f);
	S.AddMovement(TEXT("Run"), 0.5f);
	S.AddMovement(TEXT("Zip"), 0.25f);
	S.AddMovement(TEXT("Idle"), 0.f);
	TestEqual(TEXT("Run adds up"), S.MovementSeconds.FindRef(TEXT("Run")), 2.f);
	TestEqual(TEXT("Zip"), S.MovementSeconds.FindRef(TEXT("Zip")), 0.25f);
	TestFalse(TEXT("No zero entries"), S.MovementSeconds.Contains(TEXT("Idle")));

	S.AddChallengeEnd(0);
	S.AddChallengeEnd(1);
	S.AddChallengeEnd(3);
	S.AddChallengeEnd(3);
	TestEqual(TEXT("Challenges run"), S.ChallengesRun, 4);
	TestTrue(TEXT("Medals"), S.BronzeMedals == 1 && S.SilverMedals == 0 && S.GoldMedals == 2);

	S.AddCrimeEnd(true);
	S.AddCrimeEnd(false);
	TestEqual(TEXT("Crimes stopped"), S.CrimesStopped, 1);

	S.AddMap(TEXT("L_District_EastVillage"));
	S.AddMap(TEXT("L_Int_Sample"));
	S.AddMap(TEXT("L_District_EastVillage"));
	S.AddMap(FString());
	TestEqual(TEXT("Maps once each, in order"), S.Maps, TArray<FString>{ TEXT("L_District_EastVillage"), TEXT("L_Int_Sample") });

	// The fight rule.
	TestTrue(TEXT("All down, standing: won"), HawkeyePlaytest::IsFightWon(3, 0, true));
	TestFalse(TEXT("One got away"), HawkeyePlaytest::IsFightWon(3, 1, true));
	TestFalse(TEXT("Went down"), HawkeyePlaytest::IsFightWon(3, 0, false));
	TestFalse(TEXT("No thugs, no fight"), HawkeyePlaytest::IsFightWon(0, 0, true));

	FPlaytestFightWatch Watch;
	using EEnd = FPlaytestFightWatch::EEnd;
	TestTrue(TEXT("Quiet is nothing"), Watch.Update(0, 0, 0, true, 0.0) == EEnd::None);
	TestTrue(TEXT("Alerted starts it"), Watch.Update(2, 2, 2, true, 1.0) == EEnd::None && Watch.bInFight);
	TestTrue(TEXT("A short lull is still the fight"), Watch.Update(0, 2, 0, true, 1.5) == EEnd::None && Watch.bInFight);
	TestTrue(TEXT("Re-alerted"), Watch.Update(1, 2, 1, true, 1.8) == EEnd::None);
	TestTrue(TEXT("Quiet for a second, all down: won"), Watch.Update(0, 2, 0, true, 2.9) == EEnd::Won && !Watch.bInFight);
	TestTrue(TEXT("Over is over"), Watch.Update(0, 2, 0, true, 5.0) == EEnd::None);
	Watch.Update(1, 1, 1, true, 10.0);
	TestTrue(TEXT("They lost her: not won"), Watch.Update(0, 1, 1, true, 11.5) == EEnd::Other);

	S.AddFightEnd(true);
	S.AddFightEnd(false);
	TestTrue(TEXT("Fights"), S.Fights == 2 && S.FightsWon == 1);

	FPlaytestEdge Edge;
	int32 Rises = 0;
	for (const bool b : { false, true, true, false, true, false, false })
	{
		Rises += Edge.Rise(b) ? 1 : 0;
	}
	TestEqual(TEXT("Two zips, two deaths: rising edges only"), Rises, 2);

	S.Deaths = 2;
	S.Grapples = 7;
	S.FastTravels = 1;
	S.Notes = 3;
	S.Photos = 2;
	FPlaytestSummary Back;
	TestTrue(TEXT("Summary parses back"), HawkeyePlaytest::SummaryFromJson(HawkeyePlaytest::SummaryToJson(S), Back));
	TestTrue(TEXT("Counters survive"), Back.Deaths == 2 && Back.Grapples == 7 && Back.FastTravels == 1 && Back.Notes == 3
		&& Back.Photos == 2 && Back.GoldMedals == 2 && Back.CrimesStopped == 1 && Back.FightsWon == 1);
	TestEqual(TEXT("Movement map survives"), Back.MovementSeconds.FindRef(TEXT("Run")), 2.f);
	TestEqual(TEXT("Distance survives"), Back.DistanceMetres, 5.f);
	TestEqual(TEXT("Maps survive"), Back.Maps.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePlaytestLogRing, "Hawkeye.Playtest.LogRingAndSession", HawkeyePlaytestTest::Flags)

bool FHawkeyePlaytestLogRing::RunTest(const FString& Parameters)
{
	FPlaytestLogRing Ring(HawkeyePlaytest::RecentLogLines);
	for (int32 i = 1; i <= 25; ++i)
	{
		Ring.Add(FString::Printf(TEXT("line %d"), i));
	}
	const TArray<FString> Lines = Ring.Snapshot();
	TestEqual(TEXT("Keeps the last 20"), Lines.Num(), 20);
	TestEqual(TEXT("Oldest first"), Lines[0], FString(TEXT("line 6")));
	TestEqual(TEXT("Newest last"), Lines.Last(), FString(TEXT("line 25")));

	TestEqual(TEXT("Session name from the launch time"),
		HawkeyePlaytest::MakeSessionName(FDateTime(2026, 9, 29, 14, 3, 11)), FString(TEXT("2026-09-29_14-03-11")));
	TestEqual(TEXT("A passed name keeps only safe characters"),
		HawkeyePlaytest::SanitizeSessionName(TEXT("..\\2026-09-29 14:03/x_y")), FString(TEXT("2026-09-291403x_y")));
	TestEqual(TEXT("Nothing safe is empty"), HawkeyePlaytest::SanitizeSessionName(TEXT("../\\")), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePlaytestPhotoCamera, "Hawkeye.Playtest.PhotoCamera", HawkeyePlaytestTest::Flags)

bool FHawkeyePlaytestPhotoCamera::RunTest(const FString& Parameters)
{
	using P = UPlaytestPhotoMode;
	TestEqual(TEXT("FOV floor"), P::StepFov(45.f, -20.f), 40.f);
	TestEqual(TEXT("FOV ceiling"), P::StepFov(100.f, 30.f), 110.f);
	TestEqual(TEXT("FOV step"), P::StepFov(90.f, -5.f), 85.f);

	const FRotator Up = P::StepRotation(FRotator(80.f, 170.f, 0.f), 20.f, 30.f);
	TestEqual(TEXT("Pitch stops short of straight up"), float(Up.Pitch), 89.f);
	TestEqual(TEXT("Yaw wraps"), float(Up.Yaw), -170.f, 0.01f);
	TestEqual(TEXT("No roll"), float(Up.Roll), 0.f);

	const FVector Anchor(1000.f, 0.f, 500.f);
	const FVector Forward = P::StepLocation(Anchor, FRotator(0.f, 90.f, 0.f), FVector(1.f, 0.f, 0.f), 500.f, 0.5f, Anchor, 5000.f);
	TestTrue(TEXT("Forward along a 90 degree yaw is +Y, 250 cm in half a second"), Forward.Equals(Anchor + FVector(0.f, 250.f, 0.f), 0.01));
	const FVector Right = P::StepLocation(Anchor, FRotator(-45.f, 0.f, 0.f), FVector(0.f, 1.f, 1.f), 100.f, 1.f, Anchor, 5000.f);
	TestTrue(TEXT("Right stays level under a pitch, up is world up"), Right.Equals(Anchor + FVector(0.f, 100.f, 100.f), 0.01));
	const FVector Far = P::StepLocation(Anchor + FVector(4990.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(1.f, 0.f, 0.f), 2000.f, 1.f,
		Anchor, 5000.f);
	TestTrue(TEXT("Held inside the radius"), Far.Equals(Anchor + FVector(5000.f, 0.f, 0.f), 0.01));
	const FVector Clamped = P::StepLocation(Anchor, FRotator::ZeroRotator, FVector(3.f, 0.f, 0.f), 100.f, 1.f, Anchor, 5000.f);
	TestTrue(TEXT("Axes clamp to 1"), Clamped.Equals(Anchor + FVector(100.f, 0.f, 0.f), 0.01));

	TestTrue(TEXT("Keyboard card names the keys"), UPhotoModeWidget::GetControlsText(false).Contains(TEXT("F12 photo")));
	TestTrue(TEXT("Pad card names the buttons"), UPhotoModeWidget::GetControlsText(true).Contains(TEXT("A photo, B back")));
	return true;
}

#endif
