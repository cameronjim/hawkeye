#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reference shots of the hero block's December dressing (Tools/Editor/_dressing_build.py), written to
 * Saved/Screenshots/Dressing/<name>.png. Look-at-them tools, not assertions.
 *
 * The cameras are data: generate_city places a CameraActor per shot (City_DressShot_<name>, tagged DressShot and
 * shot:<name>) where _dressing_kit.shot_cameras puts it (street level under the strings, both ends, across the street
 * from the wreaths, the tree stand, a rooftop, an overview), so reframing a shot is a Python change. Kate is hidden.
 * Needs a real RHI, so it is a no-op under -nullrhi. From the standalone game:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720
 *       -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Dressing; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotDressing, "Hawkeye.Screenshot.Dressing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeDressingShots
{
	static const FName ShotTag(TEXT("DressShot"));
	static const FString ShotPrefix(TEXT("shot:"));

	static UWorld* FindWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	static FString ShotName(const AActor* Camera)
	{
		for (const FName& Tag : Camera->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(ShotPrefix))
			{
				return Text.RightChop(ShotPrefix.Len());
			}
		}
		return FString();
	}

	/** Every tagged shot camera, by shot name. */
	static TArray<TPair<FString, ACameraActor*>> FindCameras(UWorld* World)
	{
		TArray<TPair<FString, ACameraActor*>> Out;
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			const FString Name = It->Tags.Contains(ShotTag) ? ShotName(*It) : FString();
			if (!Name.IsEmpty())
			{
				Out.Emplace(Name, *It);
			}
		}
		Out.Sort([](const TPair<FString, ACameraActor*>& A, const TPair<FString, ACameraActor*>& B) { return A.Key < B.Key; });
		return Out;
	}
}

/** Looks through each shot camera in turn, waits for the view to settle, and asks for its screenshot. */
class FHawkeyeDressingShots : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeDressingShots(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeDressingShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!World || !PC)
		{
			Test->AddError(TEXT("No game world or player controller."));
			return true;
		}
		if (!bFound)
		{
			bFound = true;
			Cameras = FindCameras(World);
			if (Cameras.Num() == 0)
			{
				Test->AddError(TEXT("No DressShot cameras in the map; run Tools\\generate-city.ps1."));
				return true;
			}
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorHiddenInGame(true);
			}
		}
		const double Now = FPlatformTime::Seconds();
		// One capture at a time: the view only moves once the last shot is on disk, or it would capture the next view.
		if (HawkeyeShots::GetOutstanding(Test) > 0)
		{
			return false;
		}
		if (Index >= Cameras.Num())
		{
			return true;
		}
		if (StepStart < 0.0)
		{
			StepStart = Now;
			ACameraActor* Camera = Cameras[Index].Value;
			PC->SetViewTarget(Camera);
			Test->AddInfo(FString::Printf(TEXT("%s: camera at %s looking %s."), *Cameras[Index].Key,
				*Camera->GetActorLocation().ToCompactString(), *Camera->GetActorRotation().ToCompactString()));
			return false;
		}
		// Lumen and the exposure settle on the new view.
		if (Now - StepStart < SettleSeconds)
		{
			return false;
		}
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Dressing") / (Cameras[Index].Key + TEXT(".png")));
		HawkeyeShots::Request(Test, Path, /*bShowUI=*/false);
		++Index;
		StepStart = -1.0;
		return false;
	}

private:
	static constexpr double SettleSeconds = 2.5;
	FAutomationTestBase* Test = nullptr;
	TArray<TPair<FString, ACameraActor*>> Cameras;
	bool bFound = false;
	int32 Index = 0;
	double StepStart = -1.0;
};

bool FHawkeyeScreenshotDressing::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the dressing screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	// Map load, the spawner's instances and Lumen settling.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeDressingShots(this));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
