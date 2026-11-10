#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundBase.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Audio.Smoke: plays every MetaSound under /Game/Audio/SFX, Ambient and Music (the MS_
 * sounds create_audio.py builds) one after another through UGameplayStatics::SpawnSound2D, and asserts
 * each one starts (IsPlaying), that a one-shot finishes on its own after more than 0.05 s and within
 * 6 s, and that a loop is still going after half a second. It needs a real audio device, so it only
 * runs in the standalone game, and it fails there when there is none:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Audio.Smoke; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioSmoke, "Hawkeye.Audio.Smoke",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeAudioSmoke
{
	/** create_audio.py builds 45; fewer means a recipe failed or the content step was skipped. */
	static constexpr int32 ExpectedSounds = 45;
	static constexpr double StartCheckSeconds = 0.03;
	static constexpr double MinOneShotSeconds = 0.05;
	static constexpr double MaxOneShotSeconds = 6.0;
	static constexpr double LoopHoldSeconds = 0.5;

	static UWorld* FindGameWorld()
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

	static TArray<FSoftObjectPath> FindSounds()
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		const TArray<FString> Paths = { TEXT("/Game/Audio/SFX"), TEXT("/Game/Audio/Ambient"), TEXT("/Game/Audio/Music") };
		Registry.ScanPathsSynchronous(Paths, /*bForceRescan=*/false);
		TArray<FAssetData> Assets;
		for (const FString& Path : Paths)
		{
			Registry.GetAssetsByPath(FName(*Path), Assets, /*bRecursive=*/true);
		}
		TArray<FSoftObjectPath> Sounds;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().StartsWith(TEXT("MS_")))
			{
				Sounds.AddUnique(Asset.GetSoftObjectPath());
			}
		}
		Sounds.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
		return Sounds;
	}
}

/** Walks the sounds one at a time: start, check it plays, wait for the end (or hold a loop), next. */
class FHawkeyeAudioSmokeRun : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeAudioSmokeRun(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeAudioSmoke;
		if (!bStarted)
		{
			return Begin();
		}
		if (Index >= Sounds.Num())
		{
			Test->AddInfo(FString::Printf(TEXT("Audio smoke: %d sound(s) played, %d one-shot, %d loop."), Sounds.Num(),
				OneShots, Loops));
			return true;
		}
		if (!Component.IsValid() && !bWaitingForNext)
		{
			StartCurrent();
			return false;
		}
		CheckCurrent();
		return false;
	}

private:
	bool Begin()
	{
		using namespace HawkeyeAudioSmoke;
		bStarted = true;
		World = FindGameWorld();
		if (!World.IsValid())
		{
			Test->AddError(TEXT("No game world to play the sounds in."));
			return true;
		}
		FAudioDeviceHandle Device = World->GetAudioDevice();
		if (!Device.IsValid() || !FApp::CanEverRenderAudio())
		{
			Test->AddError(TEXT("No audio device: run this test in -game without -nosound."));
			return true;
		}
		Sounds = FindSounds();
		if (Sounds.Num() < ExpectedSounds)
		{
			Test->AddError(FString::Printf(
				TEXT("Found %d MS_ sounds under /Game/Audio, expected %d; run Tools\\create-content.ps1."),
				Sounds.Num(), ExpectedSounds));
		}
		return Sounds.Num() == 0;
	}

	void StartCurrent()
	{
		using namespace HawkeyeAudioSmoke;
		const FSoftObjectPath& Path = Sounds[Index];
		Sound = Cast<USoundBase>(Path.TryLoad());
		if (!Sound.IsValid())
		{
			Test->AddError(FString::Printf(TEXT("%s did not load as a sound."), *Path.ToString()));
			++Index;
			return;
		}
		const float Declared = Sound->GetDuration();
		if (!FMath::IsFinite(Declared) || Declared <= MinOneShotSeconds)
		{
			Test->AddError(FString::Printf(TEXT("%s: duration %f is not finite and over 0.05 s."), *Path.ToString(),
				Declared));
		}
		UAudioComponent* Played = UGameplayStatics::SpawnSound2D(World.Get(), Sound.Get(), 1.f, 1.f, 0.f, nullptr,
			/*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
		if (!Played || !Played->IsPlaying())
		{
			Test->AddError(FString::Printf(TEXT("%s: SpawnSound2D gave no playing component."), *Path.ToString()));
			++Index;
			return;
		}
		Component = Played;
		StartSeconds = FPlatformTime::Seconds();
		bLoop = Sound->IsLooping();
		bCheckedStart = false;
	}

	void CheckCurrent()
	{
		using namespace HawkeyeAudioSmoke;
		const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
		UAudioComponent* Played = Component.Get();
		const bool bPlaying = Played && Played->IsPlaying();
		const FString Name = Sounds[Index].GetAssetName();
		if (!bCheckedStart && Elapsed >= StartCheckSeconds)
		{
			bCheckedStart = true;
			if (!bPlaying && bLoop)
			{
				Test->AddError(FString::Printf(TEXT("%s: stopped within %.2f s of starting."), *Name, Elapsed));
				Finish(Played);
				return;
			}
		}
		if (bLoop)
		{
			if (Elapsed >= LoopHoldSeconds)
			{
				if (!bPlaying)
				{
					Test->AddError(FString::Printf(TEXT("%s: a loop that stopped by %.2f s."), *Name, Elapsed));
				}
				Test->AddInfo(FString::Printf(TEXT("Audio smoke: %s loop, still playing at %.2f s."), *Name, Elapsed));
				++Loops;
				Finish(Played);
			}
			return;
		}
		if (bPlaying && Elapsed < MaxOneShotSeconds)
		{
			return;
		}
		if (bPlaying)
		{
			Test->AddError(FString::Printf(TEXT("%s: a one-shot still playing after %.1f s (OnFinished unwired?)."),
				*Name, Elapsed));
		}
		else if (!FMath::IsFinite(Elapsed) || Elapsed <= MinOneShotSeconds)
		{
			Test->AddError(FString::Printf(TEXT("%s: finished after %.3f s, too short."), *Name, Elapsed));
		}
		Test->AddInfo(FString::Printf(TEXT("Audio smoke: %s one-shot, %.3f s."), *Name, Elapsed));
		++OneShots;
		Finish(Played);
	}

	void Finish(UAudioComponent* Played)
	{
		if (Played)
		{
			Played->Stop();
			Played->DestroyComponent();
		}
		Component.Reset();
		Sound.Reset();
		++Index;
	}

	FAutomationTestBase* Test = nullptr;
	TWeakObjectPtr<UWorld> World;
	TArray<FSoftObjectPath> Sounds;
	TWeakObjectPtr<USoundBase> Sound;
	TWeakObjectPtr<UAudioComponent> Component;
	double StartSeconds = 0.0;
	int32 Index = 0;
	int32 OneShots = 0;
	int32 Loops = 0;
	bool bStarted = false;
	bool bLoop = false;
	bool bCheckedStart = false;
	bool bWaitingForNext = false;
};

bool FHawkeyeAudioSmoke::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAudioSmokeRun(this));
	return true;
}

#endif
