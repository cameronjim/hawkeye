#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HawkeyePlayerController.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Playtest/PhotoModeWidget.h"
#include "Playtest/PlaytestPhotoMode.h"
#include "Playtest/PlaytestSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/HawkeyeCapture.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyePlaytestShots
{
	AHawkeyePlayerController* FindController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void Key(AHawkeyePlayerController* PC, const FKey& Key, EInputEvent Event, float Amount = 1.f)
	{
		// Through the engine's public entry, as the viewport delivers it.
		static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Amount));
	}

	bool FileGood(const FString& Path)
	{
		return IFileManager::Get().FileExists(*Path) && IFileManager::Get().FileSize(*Path) > 0;
	}
}

/**
 * The playtest kit from the keys in: F12 once (a note, its screenshot with the HUD, notes.json), F11 into
 * photo mode, the camera flown forward and turned, the view narrowed a notch, F12 for a clean photo, Esc
 * back to the game exactly where it was; then a pad's Menu tapped (the pause menu, on release) and held
 * (a second note). Then the files are checked on disk and summary.json written.
 */
class FHawkeyePlaytestRun : public IAutomationLatentCommand
{
public:
	explicit FHawkeyePlaytestRun(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyePlaytestShots;
		const double Now = FPlatformTime::Seconds();
		if (StageStart < 0.0)
		{
			StageStart = Now;
		}
		const double InStage = Now - StageStart;
		AHawkeyePlayerController* PC = FindController();
		UPlaytestSubsystem* Playtest = PC ? UPlaytestSubsystem::Get(PC) : nullptr;
		if (!PC || !Playtest)
		{
			Test->AddError(TEXT("No Hawkeye player controller or playtest subsystem in the game world."));
			return true;
		}
		if (InStage > 30.0)
		{
			Test->AddError(FString::Printf(TEXT("Playtest run stuck in stage %d."), Stage));
			PC->ExitPhotoMode();
			return true;
		}
		const auto Next = [this, Now]() { ++Stage; StageStart = Now; };

		switch (Stage)
		{
		case 0: // The note.
			NoteIndex = Playtest->GetNotes().Num() + 1;
			Key(PC, EKeys::F12, IE_Pressed);
			Key(PC, EKeys::F12, IE_Released);
			if (!Test->TestEqual(TEXT("F12 took a note"), Playtest->GetNotes().Num(), NoteIndex))
			{
				return true;
			}
			Test->AddInfo(FString::Printf(TEXT("Note line: %s"), *HawkeyePlaytest::FormatNoteLine(Playtest->GetNotes().Last())));
			Next();
			return false;
		case 1: // Its screenshot is written (the toast goes up after, so it is not in the shot).
			if (HawkeyeCapture::GetOutstanding(Playtest) > 0)
			{
				return false;
			}
			if (InStage < 0.5)
			{
				return false;
			}
			KateLocation = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
			Key(PC, EKeys::F11, IE_Pressed);
			Key(PC, EKeys::F11, IE_Released);
			if (!Test->TestTrue(FString::Printf(TEXT("F11 opened photo mode (refusal: '%s')"), *PC->GetPhotoModeRefusal()),
				PC->IsPhotoModeActive()))
			{
				return true;
			}
			Test->TestTrue(TEXT("Photo mode pauses the world"), PC->IsPaused());
			Test->TestTrue(TEXT("The HUD is hidden"), !PC->GetHawkeyeHud() || !PC->GetHawkeyeHud()->IsVisible());
			Test->TestFalse(TEXT("Pause cannot open over it"), PC->CanTogglePause());
			StartCamera = PC->GetPhotoMode()->GetCameraLocation();
			// Fly forward and turn right a little, and narrow the view one notch.
			Key(PC, EKeys::W, IE_Pressed);
			Key(PC, EKeys::MouseWheelAxis, IE_Axis, 1.f);
			Next();
			return false;
		case 2:
			Key(PC, EKeys::MouseX, IE_Axis, 3.f);
			if (InStage < 1.2)
			{
				return false;
			}
			Key(PC, EKeys::W, IE_Released);
			{
				const UPlaytestPhotoMode* Photo = PC->GetPhotoMode();
				const double Moved = FVector::Dist(Photo->GetCameraLocation(), StartCamera);
				Test->AddInfo(FString::Printf(TEXT("Photo camera moved %.0f cm, yaw %.0f, FOV %.0f; card: %s"), Moved,
					Photo->GetCameraRotation().Yaw, Photo->GetFov(),
					Photo->GetOverlay() ? *Photo->GetOverlay()->GetShownText().Replace(TEXT("\n"), TEXT(" | ")) : TEXT("none")));
				Test->TestTrue(TEXT("The camera flew forward"), Moved > 300.0);
				Test->TestEqual(TEXT("One wheel notch narrows the view 5 degrees"), Photo->GetFov(), 85.f, 0.5f);
				Test->TestTrue(TEXT("The corner card is up"), Photo->GetOverlay() && Photo->GetOverlay()->IsInViewport());
			}
			PhotoIndex = Playtest->GetSummary().Photos + 1;
			Key(PC, EKeys::F12, IE_Pressed);
			Key(PC, EKeys::F12, IE_Released);
			Test->TestEqual(TEXT("F12 in photo mode is a photo, not a note"), Playtest->GetNotes().Num(), NoteIndex);
			Next();
			return false;
		case 3:
			if (HawkeyeCapture::GetOutstanding(Playtest) > 0 || InStage < 0.3)
			{
				return false;
			}
			Key(PC, EKeys::Escape, IE_Pressed);
			Key(PC, EKeys::Escape, IE_Released);
			Test->TestFalse(TEXT("Esc left photo mode"), PC->IsPhotoModeActive());
			Test->TestFalse(TEXT("The world runs again"), PC->IsPaused());
			Test->TestFalse(TEXT("No pause menu"), PC->IsPauseMenuOpen());
			Test->TestTrue(TEXT("The view is Kate's again"), PC->GetViewTarget() == PC->GetPawn());
			Test->TestTrue(TEXT("The HUD is back"), !PC->GetHawkeyeHud() || PC->GetHawkeyeHud()->IsVisible());
			Test->TestTrue(TEXT("Kate did not move"), !PC->GetPawn() || PC->GetPawn()->GetActorLocation().Equals(KateLocation, 1.0));
			Next();
			return false;
		case 4: // A pad's Menu tapped: the pause menu, on the release.
			if (InStage < 0.3)
			{
				return false;
			}
			Key(PC, EKeys::Gamepad_Special_Right, IE_Pressed);
			Test->TestFalse(TEXT("Menu's press alone does not pause (a hold may be a note)"), PC->IsPauseMenuOpen());
			Key(PC, EKeys::Gamepad_Special_Right, IE_Released);
			Test->TestTrue(TEXT("Menu tapped: the pause menu"), PC->IsPauseMenuOpen());
			PC->SetPauseMenuOpen(false);
			Next();
			return false;
		case 5: // Menu held: a note, and no pause menu.
			if (InStage < 0.3)
			{
				return false;
			}
			if (!bMenuDown)
			{
				Key(PC, EKeys::Gamepad_Special_Right, IE_Pressed);
				bMenuDown = true;
				return false;
			}
			if (InStage < 0.3 + PC->NoteHoldSeconds + 0.2)
			{
				return false;
			}
			Key(PC, EKeys::Gamepad_Special_Right, IE_Released);
			Test->TestEqual(TEXT("Menu held 0.6 s took a note"), Playtest->GetNotes().Num(), NoteIndex + 1);
			Test->TestFalse(TEXT("and did not pause"), PC->IsPauseMenuOpen());
			Next();
			return false;
		case 6:
			if (HawkeyeCapture::GetOutstanding(Playtest) > 0)
			{
				return false;
			}
			Next();
			return false;
		default:
		{
			const FString Folder = Playtest->GetSessionFolder();
			const FString NotePng = Folder + FString::Printf(TEXT("note_%d.png"), NoteIndex);
			const FString PhotoPng = Folder + FString::Printf(TEXT("photo_%d.png"), PhotoIndex);
			Test->TestTrue(FString::Printf(TEXT("%s written"), *NotePng), FileGood(NotePng));
			Test->TestTrue(FString::Printf(TEXT("%s written"), *PhotoPng), FileGood(PhotoPng));
			FString Json;
			FPlaytestNotesFile Notes;
			Test->TestTrue(TEXT("notes.json reads back"), FFileHelper::LoadFileToString(Json, *(Folder + TEXT("notes.json")))
				&& HawkeyePlaytest::NotesFromJson(Json, Notes));
			if (Notes.Notes.IsValidIndex(NoteIndex - 1))
			{
				const FPlaytestNote& Note = Notes.Notes[NoteIndex - 1];
				Test->TestEqual(TEXT("The record names its screenshot"), Note.Screenshot, FString::Printf(TEXT("note_%d.png"), NoteIndex));
				Test->TestTrue(TEXT("The record carries recent log lines"), Note.RecentLog.Num() > 0);
				Test->TestTrue(TEXT("The record has the map"), Note.State.Map.Contains(TEXT("EastVillage")));
			}
			else
			{
				Test->AddError(TEXT("notes.json does not have the note."));
			}
			Test->TestTrue(TEXT("summary.json written"), Playtest->WriteSummary(TEXT("test"), /*bForce=*/true));
			FPlaytestSummary Summary;
			Test->TestTrue(TEXT("summary.json reads back"), FFileHelper::LoadFileToString(Json, *(Folder + TEXT("summary.json")))
				&& HawkeyePlaytest::SummaryFromJson(Json, Summary) && Summary.Notes >= 1 && Summary.Photos >= 1);
			Test->AddInfo(FString::Printf(TEXT("Playtest session folder: %s"), *Folder));
			return true;
		}
		}
	}

private:
	FAutomationTestBase* Test = nullptr;
	int32 Stage = 0;
	double StageStart = -1.0;
	int32 NoteIndex = 0;
	int32 PhotoIndex = 0;
	FVector KateLocation = FVector::ZeroVector;
	FVector StartCamera = FVector::ZeroVector;
	bool bMenuDown = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotPlaytest, "Hawkeye.Screenshot.Playtest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotPlaytest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the playtest capture pass."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyePlaytestRun(this));
	return true;
}

#endif
