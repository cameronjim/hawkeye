#include "Audio/HawkeyeMusicSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Chase/ChaseRouteDefinition.h"
#include "Chase/ChaseRules.h"
#include "Chase/ChaseSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TakedownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Crime/CrimeSpot.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Phone/PhoneInbox.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/LapFightPressure.h"
#include "UI/ChapterTitleWidget.h"
#include "UI/HawkeyeDifficultyPromptWidget.h"
#include "UI/HawkeyeHintSubsystem.h"
#include "UI/HawkeyeHints.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeMainMenuWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/MissionEndCardWidget.h"
#include "UI/PhoneWidget.h"
#include "UObject/UObjectIterator.h"
#include "World/ChapterEndInteractable.h"
#include "World/GrappleAnchor.h"
#include "World/InteractionComponent.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Lap.Campaign: Chapter 1, "Night Shift", end to end from a cold start, in the standalone game, played as a
 * new tester would play it:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap.Campaign; Quit"
 *
 * Its own save slot, deleted first; the settings are put back as they were at the end. The test hook
 * HawkeyeAutomation::SetPlaysAsPlayer gives it what automation otherwise skips (the boot menu, the
 * difficulty prompt, the chapter opening and its chase, the notices, the crime schedule) and hawkeye.Hints 2
 * turns the hints on.
 *
 * The run: the main menu with no save, New Game, Normal at the prompt; the chapter opens mid-chase (Kate on the
 * route's start at a sprint, the thief ahead, the title card over the first seconds) and the scripted player runs
 * the route the chase's data asset lays out (DA_Chase_CH01_NightShift): sprinting over the vent boxes (vaults), up
 * the bulkhead or the step (a mantle), the first grapple at the gap (aimed at the route's anchor, Q), the grapple
 * and its chain mid-zip, the grapple that goes wrong into the dumpster, out of the alley along the navmesh, and the
 * takedown from behind where he is cornered. After the first grapple lands, Esc > Quit to menu > Continue is
 * compared field by field (objective, quiver, thugs, hints, crimes, challenges, phone, safehouses, position,
 * health, and the chase: its state, both progresses, the waypoints reached, the beat). Then the end card, the
 * return to the street with the chapter-complete toast, Clint's texts read on the phone, the save, the map, a
 * challenge, a fast travel, an ambush lost on purpose and its reload, and Quit and Continue again. Every step
 * checks input, HUD, pause, dilation, fade, the grey post process and the widgets left in the viewport.
 *
 * Hawkeye.Lap.ChaseFallBehind: the same start, then the scripted player stands still until the chase is lost,
 * and checks the "[He got away]" toast, the checkpoint reload, and that the chase picks up again from it.
 *
 * Both write Saved/Automation/campaign_lap.json (or campaign_fall_behind.json: the timeline, the chase's lead
 * sampled each second, the checks, the LogHawkeye warnings and what had to be forced, [lap] for a gap in the
 * lap's own driving and [game] for one in the game) and Saved/Screenshots/Campaign/campaign_*.png.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCampaign, "Hawkeye.Lap.Campaign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCampaignFallBehind, "Hawkeye.Lap.ChaseFallBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeCampaignLap
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* Slot = TEXT("HawkeyeCampaignLap");
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	static const FName FirstSafehouse(TEXT("ch01_east_7th"));
	static const FName SecondSafehouse(TEXT("avenue_b"));

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

	static AHawkeyePlayerController* FindController(UWorld* World)
	{
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static AHawkeyeCharacter* FindPlayer(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Campaign") / FileName);
	}

	static UEnhancedInputLocalPlayerSubsystem* InputOf(APlayerController* PC)
	{
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		return Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	}

	static const UInputAction* Action(const TCHAR* Path)
	{
		return LoadObject<UInputAction>(nullptr, Path);
	}

	/** One frame of a press through the real IA_ asset. */
	static void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = Action(Path);
		if (Input && A)
		{
			Input->InjectInputForAction(A, FInputActionValue(true), {}, {});
		}
	}

	static void Hold(APlayerController* PC, const TCHAR* Path, bool bHold)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = Action(Path);
		if (!Input || !A)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(A, FInputActionValue(true), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(A);
		}
	}

	/**
	 * A key the way a keyboard delivers it: through Slate, so a focused menu (the phone, the pause menu) sees it
	 * first and the viewport passes it on to the controller; pressed, then released. Straight to the controller
	 * when Slate is not up.
	 */
	static void PressKey(AHawkeyePlayerController* PC, const FKey& Key)
	{
		if (FSlateApplication::IsInitialized())
		{
			const FKeyEvent Down(Key, FModifierKeysState(), 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyDownEvent(Down);
			const FKeyEvent Up(Key, FModifierKeysState(), 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyUpEvent(Up);
			return;
		}
		if (PC)
		{
			static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
			static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
		}
	}

	static FVector2D MoveTowards(const FVector& WorldDirection, float CameraYaw)
	{
		const float Angle = FMath::DegreesToRadians(WorldDirection.Rotation().Yaw - CameraYaw);
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
	}

	static FVector Feet(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	static void AimAt(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Target - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
	}

	static bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || Thug->GetHealthComponent()->IsDead();
	}

	static AChapterEndInteractable* FindChapterEnd(UWorld* World)
	{
		TActorIterator<AChapterEndInteractable> It(World);
		return It ? *It : nullptr;
	}

	static FName CurrentObjectiveId(UWorld* World)
	{
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionObjective* Objective = Missions ? Missions->GetCurrentObjective() : nullptr;
		return Objective ? Objective->ObjectiveId : NAME_None;
	}

	static bool IsObjectiveDone(UWorld* World, FName Id)
	{
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
		const UMissionObjective* Objective = Tracker ? Tracker->FindObjective(Id) : nullptr;
		return Objective && Objective->IsCompleted();
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	/** Every LogHawkeye warning and error while the lap runs, with the lap's clock. */
	class FLogCapture : public FOutputDevice
	{
	public:
		double Start = 0.0;
		FCriticalSection Lock;
		TArray<FString> Lines;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if ((Level == ELogVerbosity::Warning || Level == ELogVerbosity::Error) && Category == FName(TEXT("LogHawkeye")))
			{
				FScopeLock Scope(&Lock);
				Lines.Add(FString::Printf(TEXT("[%.1f s] %s: %s"), FPlatformTime::Seconds() - Start,
					Level == ELogVerbosity::Error ? TEXT("Error") : TEXT("Warning"), V));
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
	};

	/** What Continue has to bring back. */
	struct FSnapshot
	{
		FName Objective;
		TArray<FName> Completed;
		TArray<FString> Arrows;
		int32 ActiveSlot = 0;
		TMap<FString, FString> Thugs;
		TMap<FString, FVector> ThugSpots;
		TArray<FString> Hints;
		TArray<FString> Crimes;
		TArray<FString> Challenges;
		TArray<FName> Received;
		TArray<FName> Read;
		TArray<FName> Pending;
		float WorldSeconds = 0.f;
		TArray<FName> Safehouses;
		FVector Kate = FVector::ZeroVector;
		float Health = 0.f;
		FString Difficulty;
		/** The chase: its state, both progresses (cm along the route), the waypoints reached, the beat done. */
		FString Chase;
		float ThiefProgress = 0.f;
		float KateProgress = 0.f;
		int32 ChaseReached = 0;
		bool bBeatDone = false;
	};

	static FSnapshot TakeSnapshot(UWorld* World)
	{
		FSnapshot Out;
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
		Out.Objective = CurrentObjectiveId(World);
		Out.Completed = Tracker ? Tracker->GetCompletedObjectiveIds() : TArray<FName>();
		if (const UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr)
		{
			const TArray<FHawkeyeQuiverSlot>& Slots = Inventory->GetArrowSlots();
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				Out.Arrows.Add(FString::Printf(TEXT("%d:%s:%d"), Index + 1, *GetNameSafe(Slots[Index].Arrow), Slots[Index].Count));
			}
			Out.ActiveSlot = Inventory->GetActiveArrowSlot();
		}
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(UCrimeSubsystem::CrimeThugTag) || It->ActorHasTag(UChaseSubsystem::ThiefTag))
			{
				continue;
			}
			Out.Thugs.Add(It->GetName(), IsDown(*It) ? TEXT("down") : TEXT("up"));
			Out.ThugSpots.Add(It->GetName(), It->GetActorLocation());
		}
		if (const UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(World))
		{
			for (const FHawkeyeHintRule& Rule : UHawkeyeHintRules::GetDefaultRules())
			{
				Out.Hints.Add(FString::Printf(TEXT("%s=%d"), *Rule.Id.ToString(), Hints->GetShowCount(Rule.Id)));
			}
		}
		UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
		if (Campaign)
		{
			if (const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World))
			{
				Crimes->MirrorToCampaign();
			}
			if (const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World))
			{
				Challenges->MirrorToCampaign();
			}
			for (int32 Index = 0; Index < Campaign->CrimeTypes.Num(); ++Index)
			{
				Out.Crimes.Add(FString::Printf(TEXT("%s=%d"), *Campaign->CrimeTypes[Index].ToString(),
					Campaign->CrimeCompletions.IsValidIndex(Index) ? Campaign->CrimeCompletions[Index] : -1));
			}
			for (int32 Index = 0; Index < Campaign->ChallengeIds.Num(); ++Index)
			{
				Out.Challenges.Add(FString::Printf(TEXT("%s=%d"), *Campaign->ChallengeIds[Index].ToString(),
					Campaign->ChallengeCompletions.IsValidIndex(Index) ? Campaign->ChallengeCompletions[Index] : -1));
			}
			Out.Safehouses = Campaign->DiscoveredSafehouses;
		}
		if (const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World); Phone && Phone->GetInbox())
		{
			Phone->GetInbox()->Export(Out.Received, Out.Read);
			TArray<float> Seconds;
			Phone->GetInbox()->ExportPending(Out.Pending, Seconds);
		}
		Out.WorldSeconds = World->GetTimeSeconds();
		if (Kate)
		{
			Out.Kate = Kate->GetActorLocation();
			Out.Health = Kate->GetHealthComponent()->GetCurrentHealth();
		}
		if (const UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World))
		{
			Out.Difficulty = UDifficultySubsystem::GetDifficultyName(Settings->GetDifficulty()).ToString();
		}
		if (const UChaseSubsystem* Chase = UChaseSubsystem::Get(World))
		{
			Out.Chase = UEnum::GetValueAsString(Chase->GetState());
			Out.ThiefProgress = Chase->GetThiefProgress();
			Out.KateProgress = Chase->GetKateProgress();
			Out.ChaseReached = Chase->GetReachedWaypoints();
			Out.bBeatDone = Chase->IsBeatDone();
		}
		return Out;
	}

	static FString JoinNames(const TArray<FName>& Names)
	{
		TArray<FString> Out;
		for (const FName Name : Names)
		{
			Out.Add(Name.ToString());
		}
		return FString::Join(Out, TEXT(","));
	}

	/** Every difference between Before and After, as readable lines. Empty when they match. */
	static TArray<FString> Compare(const FSnapshot& Before, const FSnapshot& After)
	{
		TArray<FString> Out;
		if (Before.Objective != After.Objective)
		{
			Out.Add(FString::Printf(TEXT("objective %s, was %s"), *After.Objective.ToString(), *Before.Objective.ToString()));
		}
		if (JoinNames(Before.Completed) != JoinNames(After.Completed))
		{
			Out.Add(FString::Printf(TEXT("completed [%s], was [%s]"), *JoinNames(After.Completed), *JoinNames(Before.Completed)));
		}
		if (FString::Join(Before.Arrows, TEXT(" ")) != FString::Join(After.Arrows, TEXT(" ")) || Before.ActiveSlot != After.ActiveSlot)
		{
			Out.Add(FString::Printf(TEXT("quiver [%s] slot %d, was [%s] slot %d"), *FString::Join(After.Arrows, TEXT(" ")), After.ActiveSlot,
				*FString::Join(Before.Arrows, TEXT(" ")), Before.ActiveSlot));
		}
		for (const TPair<FString, FString>& Thug : Before.Thugs)
		{
			const FString* Now = After.Thugs.Find(Thug.Key);
			if (!Now || *Now != Thug.Value)
			{
				Out.Add(FString::Printf(TEXT("thug %s %s, was %s"), *Thug.Key, Now ? **Now : TEXT("missing"), *Thug.Value));
				continue;
			}
			const FVector* Was = Before.ThugSpots.Find(Thug.Key);
			const FVector* Is = After.ThugSpots.Find(Thug.Key);
			// A patrol walks on once the world is back: allow what he could have walked since the load.
			const float Allowed = 150.f + 450.f * After.WorldSeconds;
			if (Thug.Value == TEXT("up") && Was && Is && FVector::Dist2D(*Was, *Is) > Allowed)
			{
				Out.Add(FString::Printf(TEXT("thug %s moved %.0f cm"), *Thug.Key, FVector::Dist2D(*Was, *Is)));
			}
		}
		const TPair<const TArray<FString>*, const TCHAR*> Lists[] = {
			{ &Before.Hints, TEXT("hints") }, { &Before.Crimes, TEXT("crimes") }, { &Before.Challenges, TEXT("challenges") } };
		const TArray<FString>* AfterLists[] = { &After.Hints, &After.Crimes, &After.Challenges };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FString A = FString::Join(*Lists[Index].Key, TEXT(" "));
			const FString B = FString::Join(*AfterLists[Index], TEXT(" "));
			if (A != B)
			{
				Out.Add(FString::Printf(TEXT("%s [%s], was [%s]"), Lists[Index].Value, *B, *A));
			}
		}
		// A text still on its delay at the quit may have arrived since; nothing else may appear or go.
		TArray<FName> Arrived = After.Received;
		Arrived.RemoveAll([&Before](const FName Id) { return Before.Pending.Contains(Id) && !Before.Received.Contains(Id); });
		if (JoinNames(Before.Received) != JoinNames(Arrived) || JoinNames(Before.Read) != JoinNames(After.Read))
		{
			Out.Add(FString::Printf(TEXT("phone received [%s] read [%s], was [%s] [%s]"), *JoinNames(After.Received), *JoinNames(After.Read),
				*JoinNames(Before.Received), *JoinNames(Before.Read)));
		}
		if (JoinNames(Before.Safehouses) != JoinNames(After.Safehouses))
		{
			Out.Add(FString::Printf(TEXT("safehouses [%s], was [%s]"), *JoinNames(After.Safehouses), *JoinNames(Before.Safehouses)));
		}
		if (FVector::Dist(Before.Kate, After.Kate) > 100.f)
		{
			Out.Add(FString::Printf(TEXT("Kate %.0f cm from where she was"), FVector::Dist(Before.Kate, After.Kate)));
		}
		if (FMath::Abs(Before.Health - After.Health) > 1.f)
		{
			Out.Add(FString::Printf(TEXT("health %.0f, was %.0f"), After.Health, Before.Health));
		}
		if (Before.Difficulty != After.Difficulty)
		{
			Out.Add(FString::Printf(TEXT("difficulty %s, was %s"), *After.Difficulty, *Before.Difficulty));
		}
		if (Before.Chase != After.Chase || Before.ChaseReached != After.ChaseReached || Before.bBeatDone != After.bBeatDone)
		{
			Out.Add(FString::Printf(TEXT("chase %s, %d waypoint(s), beat %s; was %s, %d, %s"), *After.Chase, After.ChaseReached,
				After.bBeatDone ? TEXT("done") : TEXT("to come"), *Before.Chase, Before.ChaseReached, Before.bBeatDone ? TEXT("done") : TEXT("to come")));
		}
		if (FMath::Abs(Before.KateProgress - After.KateProgress) > 100.f)
		{
			Out.Add(FString::Printf(TEXT("Kate's chase progress %.0f, was %.0f"), After.KateProgress, Before.KateProgress));
		}
		// He runs on once the world is back: no further than he could have since the load, never behind.
		const float Ran = 700.f * After.WorldSeconds + 50.f;
		if (After.ThiefProgress < Before.ThiefProgress - 1.f || After.ThiefProgress > Before.ThiefProgress + Ran)
		{
			Out.Add(FString::Printf(TEXT("thief progress %.0f, was %.0f (%.1f s of world since)"), After.ThiefProgress, Before.ThiefProgress,
				After.WorldSeconds));
		}
		return Out;
	}
}

// --- The run ------------------------------------------------------------------------------------------

class FHawkeyeCampaignLapRunner : public IAutomationLatentCommand
{
public:
	FHawkeyeCampaignLapRunner(FAutomationTestBase* InTest, bool bInFallBehind) : Test(InTest), bFallBehind(bInFallBehind)
	{
		LapStart = FPlatformTime::Seconds();
		Log.Start = LapStart;
		GLog->AddOutputDevice(&Log);
	}

	virtual ~FHawkeyeCampaignLapRunner() override
	{
		GLog->RemoveOutputDevice(&Log);
	}

	virtual bool Update() override;

	/** What the lap set up before the map opened; put back at the end. */
	FHawkeyeSettings SettingsBefore;
	bool bHaveSettings = false;
	FString HintsCVarBefore;
	FString CrimeIntervalBefore;
	FString CrimeTypeBefore;

private:
	enum class EPhase : uint8
	{
		Boot,
		NewGame,
		Difficulty,
		Chase,
		QuitMenu,
		Continue,
		AfterContinue,
		ChapterEnd,
		PhoneWait,
		PhoneOpen,
		PhoneClose,
		Systems,
		CrimeApproach,
		Crime,
		CrimeReload,
		FinalQuit,
		FinalContinue,
		FallBehind,
		Reloaded,
		Done,
	};

	/** The chase's own steps, in the order the route teaches them. */
	enum class EStep : uint8
	{
		Run,
		Aim,
		Zip,
		Beat,
		Street,
		Caught,
	};

	FAutomationTestBase* Test;
	bool bFallBehind = false;
	HawkeyeCampaignLap::FLogCapture Log;
	EPhase Phase = EPhase::Boot;
	double LapStart = 0.0;
	double PhaseStart = 0.0;
	double StepAt = 0.0;
	int32 Step = 0;
	TArray<FString> Timeline;
	TArray<FString> Forced;
	TArray<FString> Findings;
	TArray<FString> Checks;
	TArray<FString> LeadSamples;
	int32 CheckFailures = 0;
	TWeakObjectPtr<UWorld> LastWorld;
	TWeakObjectPtr<UWorld> DistrictWorld;
	TArray<FString> Shots;
	bool bMoveInjecting = false;
	bool bSprintHeld = false;

	// The chase.
	EStep ChaseStep = EStep::Run;
	int32 Target = 1;           // the waypoint she is running to
	int32 Grapple = 0;          // the route's grapple she is on
	TArray<int32> StandIndex;   // the waypoint of each grapple's stand
	double SubStart = 0.0;
	double PressAt = -1.0;
	double LastSample = 0.0;
	double LastPathAt = -10.0;
	double JumpAt = -10.0;
	FVector LastProgressSpot = FVector::ZeroVector;
	double LastProgressAt = 0.0;
	bool bChained = false;
	bool bChainTried = false;
	bool bQuitDone = false;
	bool bTitleShot = false;
	bool bTitleChecked = false;
	bool bTitleGone = false;
	bool bThiefShot = false;
	bool bPromptShot = false;
	bool bDumpsterShot = false;
	bool bCorneredShot = false;
	bool bVaulted = false;
	bool bMantled = false;
	int32 Zips = 0;
	int32 BeatPhaseSeen = 0;
	double BeatPhaseAt = 0.0;
	double ChaseStartedAt = 0.0;
	float ChaseSeconds = 0.f;
	/** Unpaused seconds of the chase in the lap's own count (the chase's clock restarts on a load). */
	float ChaseActiveSeconds = 0.f;
	bool bLostToastSeen = false;
	float MinLead = BIG_NUMBER;
	float MaxLead = 0.f;
	TArray<FVector> Path;
	int32 PathIndex = 0;
	FVector Goal = FVector::ZeroVector;

	// Save and chapter end.
	HawkeyeCampaignLap::FSnapshot BeforeQuit;
	int32 SavesAtQuit = 0;
	bool bEndCardShot = false;
	double EndCardSeenAt = -1.0;
	bool bEndCardSeen = false;
	bool bDied = false;
	int32 DownCount = 0;
	double LostAt = -1.0;

	double Clock() const { return FPlatformTime::Seconds() - LapStart; }
	double InPhase() const { return FPlatformTime::Seconds() - PhaseStart; }
	double InStep() const { return FPlatformTime::Seconds() - StepAt; }
	double InSub() const { return FPlatformTime::Seconds() - SubStart; }

	void Mark(const FString& What)
	{
		const FString Line = FString::Printf(TEXT("%7.1f s  %s"), Clock(), *What);
		Timeline.Add(Line);
		Test->AddInfo(TEXT("Campaign: ") + Line);
		UE_LOG(LogTemp, Display, TEXT("[Hawkeye] campaign %s"), *Line);
	}

	void Enter(EPhase NewPhase)
	{
		Phase = NewPhase;
		PhaseStart = FPlatformTime::Seconds();
		Step = 0;
		StepAt = PhaseStart;
	}

	void NextStep()
	{
		++Step;
		StepAt = FPlatformTime::Seconds();
	}

	void Sub(EStep NewStep)
	{
		ChaseStep = NewStep;
		SubStart = FPlatformTime::Seconds();
		PressAt = -1.0;
	}

	/** Something the lap had to do for the game. bLapOnly: a gap in the lap's own driving, not the game's. */
	void Force(const FString& What, bool bLapOnly = false)
	{
		Forced.Add(FString::Printf(TEXT("%.1f s: [%s] %s"), Clock(), bLapOnly ? TEXT("lap") : TEXT("game"), *What));
		Test->AddWarning(TEXT("Campaign FORCED: ") + What);
		Mark(TEXT("FORCED: ") + What);
	}

	void Shot(const FString& FileName)
	{
		HawkeyeShots::Request(Test, HawkeyeCampaignLap::ShotPath(FileName), /*bShowUI=*/true);
		Shots.Add(FileName);
		Mark(TEXT("shot ") + FileName);
	}

	/** Whether a check held, logged either way. */
	bool Expect(const FString& What, bool bHeld)
	{
		if (!bHeld)
		{
			++CheckFailures;
			Checks.Add(FString::Printf(TEXT("%.1f s FAIL %s"), Clock(), *What));
			Test->AddError(FString::Printf(TEXT("Campaign %.1f s: %s"), Clock(), *What));
		}
		return bHeld;
	}

	static TArray<FString> StrayWidgets(UWorld* World, const TArray<UClass*>& Allowed);
	void CheckClean(const FString& Where, const TArray<UClass*>& Allowed = {}, bool bHudExpected = true, bool bToastAllowed = true);
	void StopMoving(APlayerController* PC);
	bool Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive);
	void SetSprint(APlayerController* PC, bool bSprint);

	/** Sets up the chase's driving from the route: where each grapple is fired from. */
	void PlanChase(const UChaseRouteDefinition* Route);

	/** One frame of the chase. True when it is over (caught, or given up). */
	bool TickChase(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate);
	bool TickRun(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route);
	bool TickAim(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route);
	bool TickZip(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route);
	bool TickBeat(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseSubsystem* Chase);
	bool TickStreet(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseSubsystem* Chase);
	void WatchTitle(AHawkeyePlayerController* PC);
	void SampleLead(const UChaseSubsystem* Chase);

	/** The grapple anchor nearest Point (within 150 cm), or null. */
	static AGrappleAnchor* AnchorNear(UWorld* World, const FVector& Point);

	bool WorldSettled(UWorld* World, const UWorld* NotThis, float MinSeconds) const;
	void Finish(UWorld* World);
};

TArray<FString> FHawkeyeCampaignLapRunner::StrayWidgets(UWorld* World, const TArray<UClass*>& Allowed)
{
	TArray<FString> Out;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!Widget || Widget->GetWorld() != World || !Widget->IsInViewport() || Widget->IsA<UHawkeyeHudWidget>())
		{
			continue;
		}
		if (Widget->GetVisibility() == ESlateVisibility::Collapsed || Widget->GetVisibility() == ESlateVisibility::Hidden)
		{
			continue;
		}
		bool bAllowed = false;
		for (const UClass* Class : Allowed)
		{
			bAllowed |= Class && Widget->IsA(Class);
		}
		if (!bAllowed)
		{
			Out.Add(Widget->GetClass()->GetName());
		}
	}
	return Out;
}

void FHawkeyeCampaignLapRunner::CheckClean(const FString& Where, const TArray<UClass*>& Allowed, bool bHudExpected, bool bToastAllowed)
{
	using namespace HawkeyeCampaignLap;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = FindController(World);
	AHawkeyeCharacter* Kate = FindPlayer(World);
	if (!Expect(Where + TEXT(": a player and a pawn"), PC && Kate))
	{
		return;
	}
	TArray<FString> Bad;
	if (PC->IsMoveInputIgnored() || PC->IsLookInputIgnored())
	{
		Bad.Add(TEXT("move or look input ignored"));
	}
	if (UGameplayStatics::IsGamePaused(World))
	{
		Bad.Add(TEXT("paused"));
	}
	if (PC->bShowMouseCursor)
	{
		Bad.Add(TEXT("cursor shown (UI input mode)"));
	}
	const float Dilation = World->GetWorldSettings() ? World->GetWorldSettings()->TimeDilation : 1.f;
	if (!FMath::IsNearlyEqual(Dilation, 1.f, 0.01f))
	{
		Bad.Add(FString::Printf(TEXT("time dilation %.2f"), Dilation));
	}
	if (PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount > 0.05f)
	{
		Bad.Add(FString::Printf(TEXT("camera fade %.2f"), PC->PlayerCameraManager->FadeAmount));
	}
	if (PC->GetViewTarget() != Kate)
	{
		Bad.Add(FString::Printf(TEXT("view target %s"), *GetNameSafe(PC->GetViewTarget())));
	}
	const FPostProcessSettings& Post = Kate->GetFollowCamera()->PostProcessSettings;
	if (Post.bOverride_ColorSaturation && Post.ColorSaturation.X < 0.9f)
	{
		Bad.Add(FString::Printf(TEXT("grey post process (saturation %.2f, health %.0f)"), Post.ColorSaturation.X,
			Kate->GetHealthComponent()->GetCurrentHealth()));
	}
	if (Kate->IsDowned() || !Kate->GetHealthComponent()->IsAlive())
	{
		Bad.Add(TEXT("Kate down or dead"));
	}
	if (Kate->IsLockedOutByTakedown() || Kate->IsQuiverWheelOpen())
	{
		Bad.Add(TEXT("Kate locked out (takedown or quiver wheel)"));
	}
	if (PC->IsPauseMenuOpen() || PC->IsMainMenuOpen() || PC->IsPhoneOpen() || PC->IsMapOpen() || PC->IsInventoryOpen()
		|| PC->IsSafehouseMenuOpen() || PC->IsChallengeResultsOpen() || PC->IsFlashbackActive() || PC->IsCloseUpActive()
		|| PC->IsDifficultyPromptOpen())
	{
		Bad.Add(TEXT("a menu, card or beat still flagged open on the controller"));
	}
	if (PC->GetMissionFlow() && PC->GetMissionFlow()->IsRunning())
	{
		Bad.Add(FString::Printf(TEXT("mission flow still running (step %d)"), static_cast<int32>(PC->GetMissionFlow()->GetStep())));
	}
	const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
	const bool bHudUp = Hud && Hud->IsInViewport() && Hud->GetVisibility() != ESlateVisibility::Collapsed
		&& Hud->GetVisibility() != ESlateVisibility::Hidden;
	if (bHudExpected != bHudUp)
	{
		Bad.Add(bHudUp ? TEXT("HUD up when it should not be") : TEXT("HUD not up"));
	}
	const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
	if (!bToastAllowed && Toasts && Toasts->IsToastVisible())
	{
		Bad.Add(FString::Printf(TEXT("stale toast \"%s\""), *Toasts->GetToastHeading().ToString()));
	}
	const TArray<FString> Stray = StrayWidgets(World, Allowed);
	if (Stray.Num() > 0)
	{
		Bad.Add(TEXT("widgets left in the viewport: ") + FString::Join(Stray, TEXT(", ")));
	}
	const FString Line = FString::Printf(TEXT("%s: %s (objective %s, health %.0f, dilation %.2f, fade %.2f)"), *Where,
		Bad.Num() == 0 ? TEXT("clean") : *FString::Join(Bad, TEXT("; ")), *CurrentObjectiveId(World).ToString(),
		Kate->GetHealthComponent()->GetCurrentHealth(), Dilation, PC->PlayerCameraManager ? PC->PlayerCameraManager->FadeAmount : 0.f);
	Checks.Add(FString::Printf(TEXT("%.1f s %s"), Clock(), *Line));
	Expect(Line, Bad.Num() == 0);
}

void FHawkeyeCampaignLapRunner::SetSprint(APlayerController* PC, bool bSprint)
{
	if (bSprint != bSprintHeld)
	{
		HawkeyeCampaignLap::Hold(PC, HawkeyeCampaignLap::SprintPath, bSprint);
		bSprintHeld = bSprint;
	}
}

void FHawkeyeCampaignLapRunner::StopMoving(APlayerController* PC)
{
	SetSprint(PC, false);
	if (bMoveInjecting)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeCampaignLap::InputOf(PC))
		{
			Input->StopContinuousInputInjectionForAction(HawkeyeCampaignLap::Action(HawkeyeCampaignLap::MovePath));
		}
		bMoveInjecting = false;
	}
}

bool FHawkeyeCampaignLapRunner::Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& InTarget, bool bSprint, float Arrive)
{
	using namespace HawkeyeCampaignLap;
	const FVector To = (InTarget - Kate->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	if (To.Size() <= Arrive)
	{
		return true;
	}
	const FVector Direction = To.GetSafeNormal();
	const float DeltaSeconds = Kate->GetWorld()->GetDeltaSeconds();
	const FRotator Control = PC->GetControlRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Control.Yaw, Direction.Rotation().Yaw);
	const float MaxTurn = 540.f * DeltaSeconds;
	const float NewYaw = Control.Yaw + FMath::Clamp(DeltaYaw, -MaxTurn, MaxTurn);
	PC->SetControlRotation(FRotator(-10.f, NewYaw, 0.f));
	const FVector2D Value = MoveTowards(Direction, NewYaw);
	if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
	{
		const UInputAction* Move = Action(MovePath);
		if (!bMoveInjecting)
		{
			Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
			bMoveInjecting = true;
		}
		else
		{
			Input->UpdateValueOfContinuousInputInjectionForAction(Move, FInputActionValue(Value));
		}
	}
	SetSprint(PC, bSprint);
	return false;
}

AGrappleAnchor* FHawkeyeCampaignLapRunner::AnchorNear(UWorld* World, const FVector& Point)
{
	AGrappleAnchor* Best = nullptr;
	float BestDistance = 150.f;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Point);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

// --- The chase ----------------------------------------------------------------------------------------

void FHawkeyeCampaignLapRunner::PlanChase(const UChaseRouteDefinition* Route)
{
	StandIndex.Reset();
	for (const FChaseGrapple& Line : Route->Grapples)
	{
		int32 Best = 0;
		float BestDistance = BIG_NUMBER;
		for (int32 Index = 0; Index < Route->Waypoints.Num(); ++Index)
		{
			const float Distance = FVector::Dist(Route->Waypoints[Index].Location, Line.Stand);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
		StandIndex.Add(Best);
	}
	Mark(FString::Printf(TEXT("chase plan: %d waypoints, grapples fired from waypoints %s, %d roof(s): %s"), Route->Waypoints.Num(),
		*FString::JoinBy(StandIndex, TEXT(", "), [](int32 I) { return FString::FromInt(I); }), Route->RoofIds.Num(),
		*FString::JoinBy(Route->RoofIds, TEXT(" > "), [](FName N) { return N.ToString(); })));
}

void FHawkeyeCampaignLapRunner::SampleLead(const UChaseSubsystem* Chase)
{
	if (!Chase || !Chase->IsChaseRunning() || FPlatformTime::Seconds() - LastSample < 1.0)
	{
		return;
	}
	LastSample = FPlatformTime::Seconds();
	if (Chase->GetState() == EChaseState::Running)
	{
		MinLead = FMath::Min(MinLead, Chase->GetLead());
		MaxLead = FMath::Max(MaxLead, Chase->GetLead());
	}
	LeadSamples.Add(FString::Printf(TEXT("%.1f s: %s lead %.0f cm (thief %.0f, Kate %.0f of %.0f, speed %.0f, lose clock %.1f%s)"),
		ChaseActiveSeconds, *UEnum::GetValueAsString(Chase->GetState()), Chase->GetLead(), Chase->GetThiefProgress(),
		Chase->GetKateProgress(), Chase->GetRouteLength(), Chase->GetThiefSpeed(), Chase->GetLoseClock(),
		Chase->IsKateOnRoute() ? TEXT("") : TEXT(", Kate off the route")));
}

void FHawkeyeCampaignLapRunner::WatchTitle(AHawkeyePlayerController* PC)
{
	if (bFallBehind)
	{
		// The main lap checks the card; a second run in one process has already seen it (a campaign of its own).
		return;
	}
	if (!bTitleShot && InPhase() >= 1.0 && PC->IsChapterTitleShowing())
	{
		bTitleShot = true;
		Shot(TEXT("campaign_open.png"));
	}
	if (!bTitleChecked && InPhase() >= 1.4)
	{
		bTitleChecked = true;
		const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
		const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
		Expect(TEXT("the title card is up over the running chase"), PC->IsChapterTitleShowing());
		Expect(TEXT("the first objective's toast waits under the title card"), Toasts && Toasts->AreToastsHeld() && !Toasts->IsToastVisible());
	}
	if (bTitleChecked && !bTitleGone && !PC->IsChapterTitleShowing())
	{
		bTitleGone = true;
		Mark(FString::Printf(TEXT("title card gone after %.1f s of the run"), InPhase()));
	}
	if (!bTitleGone && InPhase() > 10.0)
	{
		bTitleGone = true;
		Expect(TEXT("the title card goes by itself inside 10 s"), false);
	}
}

bool FHawkeyeCampaignLapRunner::TickRun(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route)
{
	using namespace HawkeyeCampaignLap;
	const int32 Stop = StandIndex.IsValidIndex(Grapple) ? StandIndex[Grapple] : Route->Waypoints.Num() - 1;
	if (Target > Stop)
	{
		Target = Stop;
	}
	const FChaseWaypoint& Point = Route->Waypoints[Target];
	// A step up ahead and her stopped against it: the jump key mantles what the sprint did not.
	const float Speed = Kate->GetVelocity().Size2D();
	const bool bHigher = Point.Location.Z > Feet(Kate).Z + 60.f;
	if (bHigher && Speed < 120.f && Kate->GetCharacterMovement()->IsMovingOnGround() && FPlatformTime::Seconds() - JumpAt > 0.8
		&& FVector::Dist2D(Point.Location, Kate->GetActorLocation()) < 260.f)
	{
		JumpAt = FPlatformTime::Seconds();
		Tap(PC, JumpPath);
		Mark(FString::Printf(TEXT("jump: up onto waypoint %d (%.0f cm above her feet)"), Target, Point.Location.Z - Feet(Kate).Z));
	}
	const UParkourComponent* Parkour = Kate->GetParkourComponent();
	const EHawkeyeParkourMove Move = Parkour ? Parkour->GetActiveMove() : EHawkeyeParkourMove::None;
	if (Move == EHawkeyeParkourMove::Vault && !bVaulted)
	{
		bVaulted = true;
		Mark(TEXT("vault (the sprint took the vent box)"));
	}
	if ((Move == EHawkeyeParkourMove::Mantle || Move == EHawkeyeParkourMove::Climb || Move == EHawkeyeParkourMove::LedgeGrab) && !bMantled)
	{
		bMantled = true;
		Mark(FString::Printf(TEXT("mantle (%s)"), *UEnum::GetValueAsString(Move)));
	}
	const bool bThere = Steer(PC, Kate, Point.Location + FVector(0.f, 0.f, 90.f), true, Target == Stop ? 60.f : 110.f);
	if (FVector::Dist2D(Kate->GetActorLocation(), LastProgressSpot) > 60.f)
	{
		LastProgressSpot = Kate->GetActorLocation();
		LastProgressAt = FPlatformTime::Seconds();
	}
	else if (FPlatformTime::Seconds() - LastProgressAt > 5.0)
	{
		Force(FString::Printf(TEXT("stuck 5 s short of waypoint %d (%s); putting Kate on it"), Target, *UEnum::GetValueAsString(Point.Leg)), true);
		Kate->TeleportTo(Point.Location + FVector(0.f, 0.f, 95.f), Kate->GetActorRotation());
		LastProgressAt = FPlatformTime::Seconds();
		return false;
	}
	if (!bThere)
	{
		return false;
	}
	if (Target < Stop)
	{
		++Target;
		return false;
	}
	if (!StandIndex.IsValidIndex(Grapple))
	{
		return true;
	}
	StopMoving(PC);
	Sub(EStep::Aim);
	return false;
}

bool FHawkeyeCampaignLapRunner::TickAim(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route)
{
	using namespace HawkeyeCampaignLap;
	const FChaseGrapple& Line = Route->Grapples[Grapple];
	if (InSub() < 0.4 || !Kate->GetCharacterMovement()->IsMovingOnGround() || Kate->GetVelocity().Size2D() > 60.f)
	{
		return false;
	}
	AimAt(PC, Kate, Line.Anchor);
	UGrappleComponent* Hook = Kate->GetGrappleComponent();
	AGrappleAnchor* Wanted = AnchorNear(World, Line.Anchor);
	if (InSub() < 0.8)
	{
		return false;
	}
	const AGrappleAnchor* Marked = Hook ? Hook->GetTargetAnchor() : nullptr;
	const bool bReady = Hook && Hook->GetTargetState() == EGrappleTargetState::Ready && Marked;
	if (Grapple == 0 && !bPromptShot && bReady)
	{
		bPromptShot = true;
		Shot(TEXT("campaign_grapple_prompt.png"));
		CheckClean(TEXT("at the first grapple's stand"));
		Expect(TEXT("the first grapple's stand completed chase_thief; cross_gap is current"), CurrentObjectiveId(World) == TEXT("cross_gap"));
		return false;
	}
	if (bReady && (Marked == Wanted || !Wanted || FVector::Dist(Marked->GetActorLocation(), Line.Anchor) < 600.f))
	{
		if (Marked != Wanted)
		{
			Findings.Add(FString::Printf(TEXT("grapple %d: the picker marked %s, not the route's %s (%.0f cm apart); fired at the marked one"),
				Grapple, *GetNameSafe(Marked), *GetNameSafe(Wanted), FVector::Dist(Marked->GetActorLocation(), Line.Anchor)));
		}
		Mark(FString::Printf(TEXT("Q: grapple %d at %s%s"), Grapple, *GetNameSafe(Marked), Line.bBeat ? TEXT(" (the beat's gap)") : TEXT("")));
		Tap(PC, GrapplePath);
		++Zips;
		bChained = bChainTried = false;
		Sub(EStep::Zip);
		return false;
	}
	if (InSub() > 4.0)
	{
		Force(FString::Printf(TEXT("grapple %d: no Ready marker on the route's anchor %s in 4 s (marked %s, state %d); zipping to it"),
			Grapple, *GetNameSafe(Wanted), *GetNameSafe(Marked), Hook ? static_cast<int32>(Hook->GetTargetState()) : -1));
		if (Hook && Wanted)
		{
			Hook->StartZip(Wanted);
		}
		++Zips;
		Sub(EStep::Zip);
	}
	return false;
}

bool FHawkeyeCampaignLapRunner::TickZip(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseRouteDefinition* Route)
{
	using namespace HawkeyeCampaignLap;
	const FChaseGrapple& Line = Route->Grapples[Grapple];
	UGrappleComponent* Hook = Kate->GetGrappleComponent();
	const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
	if (Line.bBeat && Chase && Chase->GetState() == EChaseState::Beat)
	{
		Mark(TEXT("the beat: the grapple goes wrong"));
		Sub(EStep::Beat);
		return false;
	}
	const bool bZipping = Hook && (Hook->IsZipping() || Hook->IsArrowInFlight() || Hook->IsHopping());
	if (Line.IsChain() && Hook && Hook->IsZipping() && !bChained && Hook->GetZipProgress() >= FMath::Max(Line.ChainAt - 0.08f, 0.42f))
	{
		bChainTried = true;
		AimAt(PC, Kate, Line.ChainAnchor);
		const AGrappleAnchor* Marked = Hook->GetTargetAnchor();
		if (Hook->GetTargetState() == EGrappleTargetState::Ready && Marked && FVector::Dist(Marked->GetActorLocation(), Line.ChainAnchor) < 600.f)
		{
			bChained = true;
			Mark(FString::Printf(TEXT("Q: chain at %.0f%% of the line to %s"), Hook->GetZipProgress() * 100.f, *GetNameSafe(Marked)));
			Tap(PC, GrapplePath);
			return false;
		}
	}
	if (bZipping || InSub() < 0.5)
	{
		if (InSub() > 12.0)
		{
			Force(FString::Printf(TEXT("grapple %d: still zipping after 12 s; putting Kate on the next waypoint"), Grapple), true);
			Kate->TeleportTo(Route->Waypoints[FMath::Min(StandIndex[Grapple] + 1, Route->Waypoints.Num() - 1)].Location + FVector(0.f, 0.f, 95.f),
				Kate->GetActorRotation());
		}
		return false;
	}
	if (Line.bBeat)
	{
		if (InSub() > 2.0)
		{
			Expect(TEXT("the beat's grapple starts the dumpster beat"), false);
			Sub(EStep::Street);
		}
		return false;
	}
	if (!Kate->GetCharacterMovement()->IsMovingOnGround() || InSub() < 0.8)
	{
		return false;
	}
	if (Line.IsChain())
	{
		Expect(TEXT("the second grapple chains mid-zip"), bChained);
	}
	// On, from the waypoint nearest where she landed.
	int32 Nearest = Target;
	float NearestDistance = BIG_NUMBER;
	for (int32 Index = StandIndex[Grapple] + 1; Index < Route->Waypoints.Num(); ++Index)
	{
		const float Distance = FVector::Dist(Route->Waypoints[Index].Location, Feet(Kate));
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Index;
		}
	}
	Mark(FString::Printf(TEXT("landed grapple %d (%s), %.0f cm from waypoint %d; objective %s"), Grapple,
		Line.IsChain() ? (bChained ? TEXT("chained") : TEXT("no chain")) : TEXT("plain"), NearestDistance, Nearest,
		*CurrentObjectiveId(World).ToString()));
	if (Grapple == 0)
	{
		Expect(TEXT("landing across completes cross_gap; catch_thief is current"), IsObjectiveDone(World, TEXT("cross_gap"))
			&& CurrentObjectiveId(World) == TEXT("catch_thief"));
	}
	Target = FMath::Min(Nearest + 1, Route->Waypoints.Num() - 1);
	++Grapple;
	Sub(EStep::Run);
	LastProgressAt = FPlatformTime::Seconds();
	return false;
}

bool FHawkeyeCampaignLapRunner::TickBeat(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseSubsystem* Chase)
{
	using namespace HawkeyeCampaignLap;
	const int32 Phase4 = Chase->GetBeatPhase();
	if (Phase4 != BeatPhaseSeen)
	{
		BeatPhaseSeen = Phase4;
		BeatPhaseAt = FPlatformTime::Seconds();
		Mark(FString::Printf(TEXT("beat phase %d at %.2f s into it"), Phase4, Chase->GetBeatSeconds()));
	}
	if (Phase4 == 3 && !bDumpsterShot && FPlatformTime::Seconds() - BeatPhaseAt > 0.25)
	{
		bDumpsterShot = true;
		Shot(TEXT("campaign_dumpster.png"));
		Expect(TEXT("in the beat, move input is the beat's"), PC->IsMoveInputIgnored());
		Expect(TEXT("in the beat, look input stays hers"), !PC->IsLookInputIgnored());
	}
	if (Chase->GetState() == EChaseState::Beat)
	{
		if (InSub() > 8.0)
		{
			Expect(TEXT("the beat ends inside 8 s"), false);
			Sub(EStep::Street);
		}
		return false;
	}
	Mark(FString::Printf(TEXT("beat over after %.1f s (%s); Kate %.0f cm off the alley floor, lead %.0f cm"), InSub(),
		Chase->DidBeatPlay() ? TEXT("played through") : TEXT("cut short"), Feet(Kate).Z, Chase->GetLead()));
	Expect(TEXT("the beat played through in under 4 s of its moves"), Chase->DidBeatPlay() && InSub() < 4.5);
	Expect(TEXT("move input is hers again after the beat"), !PC->IsMoveInputIgnored());
	Expect(TEXT("the beat leaves her walking on the ground"), Kate->GetCharacterMovement()->IsMovingOnGround());
	Sub(EStep::Street);
	LastPathAt = -10.0;
	return false;
}

bool FHawkeyeCampaignLapRunner::TickStreet(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const UChaseSubsystem* Chase)
{
	using namespace HawkeyeCampaignLap;
	AThugCharacter* Thief = Chase->GetThief();
	if (!Thief)
	{
		Expect(TEXT("a thief to catch"), false);
		return true;
	}
	const float Distance = FVector::Dist2D(Thief->GetActorLocation(), Kate->GetActorLocation());
	if (Chase->GetState() == EChaseState::Cornered && !bCorneredShot && Distance < 1500.f)
	{
		bCorneredShot = true;
		StopMoving(PC);
		SubStart = FPlatformTime::Seconds();
		return false;
	}
	if (bCorneredShot && InSub() < 0.9)
	{
		// Held on him a moment, then the shot, so it is not the camera's swing.
		AimAt(PC, Kate, Thief->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		if (InSub() > 0.7 && (Shots.Num() == 0 || Shots.Last() != TEXT("campaign_cornered.png")))
		{
			Shot(TEXT("campaign_cornered.png"));
		}
		return false;
	}
	if (Distance < 700.f)
	{
		// Up behind him: his back is to her when he is cornered, and he runs away from her when he is not.
		const FVector Behind = Thief->GetActorLocation() - Thief->GetActorForwardVector() * 90.f;
		Steer(PC, Kate, Behind, Distance > 250.f, 40.f);
		UTakedownComponent* Takedown = Kate->GetTakedownComponent();
		if (Takedown && Distance < 170.f && Takedown->IsValidTakedownTarget(Thief, Kate->GetActorLocation()) && PressAt < 0.0)
		{
			StopMoving(PC);
			PressAt = FPlatformTime::Seconds();
			Mark(FString::Printf(TEXT("F: takedown on the thief (%s, %.0f cm)"), *UEnum::GetValueAsString(Chase->GetState()), Distance));
			Tap(PC, TakedownPath);
		}
		if (PressAt > 0.0 && FPlatformTime::Seconds() - PressAt > 1.5 && Chase->GetState() != EChaseState::Caught && Chase->IsChaseRunning())
		{
			PressAt = -1.0;
			Findings.Add(TEXT("a takedown press did not take the thief; tried again"));
		}
		return false;
	}
	if (FPlatformTime::Seconds() - LastPathAt > 1.5)
	{
		LastPathAt = FPlatformTime::Seconds();
		const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(World, Kate->GetActorLocation(),
			Thief->GetActorLocation(), Kate);
		Path = NavPath && NavPath->IsValid() ? NavPath->PathPoints : TArray<FVector>{ Thief->GetActorLocation() };
		PathIndex = Path.Num() > 1 ? 1 : 0;
	}
	if (Path.IsValidIndex(PathIndex) && Steer(PC, Kate, Path[PathIndex], true, 120.f))
	{
		++PathIndex;
	}
	if (InSub() > 90.0)
	{
		Force(TEXT("not at the thief 90 s after the beat; putting Kate behind him"), true);
		Kate->TeleportTo(Thief->GetActorLocation() - Thief->GetActorForwardVector() * 120.f, Thief->GetActorRotation());
		SubStart = FPlatformTime::Seconds();
	}
	return false;
}

bool FHawkeyeCampaignLapRunner::TickChase(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
	const UChaseRouteDefinition* Route = Chase ? Chase->GetRoute() : nullptr;
	if (!Chase || !Route)
	{
		Expect(TEXT("a chase with a route"), false);
		return true;
	}
	SampleLead(Chase);
	if (Chase->GetState() == EChaseState::Lost)
	{
		Expect(TEXT("the scripted player keeps the thief"), false);
		return true;
	}
	if (ChaseStep == EStep::Caught || Chase->GetState() == EChaseState::Caught)
	{
		if (ChaseStep != EStep::Caught)
		{
			ChaseSeconds = ChaseActiveSeconds;
			Mark(FString::Printf(TEXT("caught after %.1f s of chase; lead %.0f to %.0f cm"), ChaseSeconds, MinLead, MaxLead));
			StopMoving(PC);
			Sub(EStep::Caught);
		}
		return !Chase->IsChaseRunning();
	}
	if (!bThiefShot && Grapple == 1 && ChaseStep == EStep::Run && InSub() > 0.3 && Chase->GetThief())
	{
		// Turn to him and hold it a moment, so the shot is not the camera's swing.
		StopMoving(PC);
		AimAt(PC, Kate, Chase->GetThief()->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		if (InSub() < 0.9)
		{
			return false;
		}
		bThiefShot = true;
		Shot(TEXT("campaign_thief_roof.png"));
		Mark(FString::Printf(TEXT("the thief %.0f cm ahead along the route, on %s"), Chase->GetLead(),
			Chase->GetThief()->GetActorLocation().Z > 800.f ? TEXT("a roof") : TEXT("the street")));
		return false;
	}
	if (bThiefShot && !bQuitDone && Grapple == 1 && Phase == EPhase::Chase)
	{
		// Held still for the quit that follows (Phase Chase does it), so the snapshot is of a standing Kate.
		return false;
	}
	switch (ChaseStep)
	{
	case EStep::Run:
		return TickRun(World, PC, Kate, Route);
	case EStep::Aim:
		return TickAim(World, PC, Kate, Route);
	case EStep::Zip:
		return TickZip(World, PC, Kate, Route);
	case EStep::Beat:
		return TickBeat(World, PC, Kate, Chase);
	case EStep::Street:
		return TickStreet(World, PC, Kate, Chase);
	default:
		return false;
	}
}

bool FHawkeyeCampaignLapRunner::WorldSettled(UWorld* World, const UWorld* NotThis, float MinSeconds) const
{
	using namespace HawkeyeCampaignLap;
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	const AHawkeyePlayerController* PC = FindController(World);
	return World && World != NotThis && Save && !Save->IsLoading() && !Save->GetSceneReturn().bReturnPending && FindPlayer(World)
		&& PC && (!PC->PlayerCameraManager || PC->PlayerCameraManager->FadeAmount < 0.02f) && World->GetTimeSeconds() > MinSeconds;
}

void FHawkeyeCampaignLapRunner::Finish(UWorld* World)
{
	using namespace HawkeyeCampaignLap;
	if (AHawkeyePlayerController* PC = FindController(World))
	{
		StopMoving(PC);
	}
	HawkeyeAutomation::SetPlaysAsPlayer(false);
	auto SetCVar = [](const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(*Value, ECVF_SetByCode);
		}
	};
	SetCVar(TEXT("hawkeye.Hints"), HintsCVarBefore);
	SetCVar(TEXT("hawkeye.CrimeInterval"), CrimeIntervalBefore);
	SetCVar(TEXT("hawkeye.CrimeType"), CrimeTypeBefore);
	if (UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World); Settings && bHaveSettings)
	{
		Settings->SetSettings(SettingsBefore);
	}
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World))
	{
		IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
		Save->SlotNameOverride.Reset();
	}

	TArray<FString> Warnings;
	{
		FScopeLock Scope(&Log.Lock);
		Warnings = Log.Lines;
	}
	auto Quote = [](const TArray<FString>& Lines)
	{
		TArray<FString> Out;
		for (const FString& Line : Lines)
		{
			Out.Add(FString::Printf(TEXT("    \"%s\""), *Line.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""))));
		}
		return FString::Join(Out, TEXT(",\n"));
	};
	const FString Name = bFallBehind ? TEXT("Hawkeye.Lap.ChaseFallBehind") : TEXT("Hawkeye.Lap.Campaign");
	const FString Json = FString::Printf(TEXT("{\n  \"test\": \"%s\",\n  \"seconds\": %.1f,\n  \"check_failures\": %d,\n"
		"  \"chase_seconds\": %.1f,\n  \"lead_min_cm\": %.0f,\n  \"lead_max_cm\": %.0f,\n"
		"  \"timeline\": [\n%s\n  ],\n  \"lead\": [\n%s\n  ],\n  \"forced\": [\n%s\n  ],\n  \"findings\": [\n%s\n  ],\n"
		"  \"hawkeye_warnings\": [\n%s\n  ],\n  \"checks\": [\n%s\n  ],\n  \"shots\": [\n%s\n  ]\n}\n"),
		*Name, Clock(), CheckFailures, ChaseSeconds, MinLead < BIG_NUMBER ? MinLead : 0.f, MaxLead, *Quote(Timeline), *Quote(LeadSamples),
		*Quote(Forced), *Quote(Findings), *Quote(Warnings), *Quote(Checks), *Quote(Shots));
	WriteText(bFallBehind ? TEXT("campaign_fall_behind.json") : TEXT("campaign_lap.json"),
		HawkeyeLapPressure::Finish(Test, bFallBehind ? TEXT("ChaseFallBehind") : TEXT("Campaign"), Json));
	Test->AddInfo(TEXT("campaign lap json:\n") + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] campaign lap:\n%s"), *Json);
	for (const FString& Warning : Warnings)
	{
		Test->AddWarning(TEXT("Campaign LogHawkeye: ") + Warning);
	}
}

bool FHawkeyeCampaignLapRunner::Update()
{
	using namespace HawkeyeCampaignLap;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = FindController(World);
	AHawkeyeCharacter* Kate = FindPlayer(World);
	if (World != LastWorld.Get())
	{
		LastWorld = World;
		if (World)
		{
			Mark(FString::Printf(TEXT("world %s"), *World->GetMapName()));
		}
	}
	if (Phase != EPhase::Done && Clock() > 1500.0)
	{
		Test->AddError(FString::Printf(TEXT("Campaign: timed out in phase %d step %d."), static_cast<int32>(Phase), Step));
		Finish(World);
		return true;
	}

	switch (Phase)
	{
	case EPhase::Boot:
		if (Step == 0)
		{
			// A cold start: the district opened fresh (AutomationOpenMap keeps a map that is already up).
			DistrictWorld = World;
			if (World)
			{
				UGameplayStatics::OpenLevel(World, FName(MapPath));
			}
			NextStep();
			return false;
		}
		if (World == DistrictWorld.Get())
		{
			return false;
		}
		if (!PC || !PC->IsMainMenuOpen())
		{
			if (InPhase() > 90.0)
			{
				Test->AddError(TEXT("Campaign: the main menu never came up on the fresh district."));
				Finish(World);
				return true;
			}
			return false;
		}
		if (InPhase() < 3.0 || InStep() < 2.0)
		{
			return false;
		}
		{
			const UHawkeyeMainMenuWidget* Menu = PC->GetMainMenuWidget();
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
			Mark(TEXT("main menu up over the district"));
			Expect(TEXT("no save in the lap's slot"), Save && !Save->HasSave() && Save->GetSlotName() == Slot);
			Expect(TEXT("Continue greyed with no save"), Menu && !Menu->IsContinueEnabled());
			Expect(TEXT("the world is paused under the menu"), UGameplayStatics::IsGamePaused(World));
			Expect(TEXT("the cursor is up for the menu"), PC->bShowMouseCursor);
			Expect(TEXT("no HUD under the menu"), !PC->GetHawkeyeHud() || !PC->GetHawkeyeHud()->IsVisible());
			Expect(TEXT("no title card before New Game"), !PC->IsChapterTitleShowing());
			Expect(TEXT("no chase before New Game"), Chase && !Chase->IsChaseRunning());
			if (!bFallBehind)
			{
				Shot(TEXT("campaign_menu.png"));
			}
			Enter(EPhase::NewGame);
		}
		return false;

	case EPhase::NewGame:
		if (InPhase() < 1.0)
		{
			return false;
		}
		Mark(TEXT("New Game"));
		PC->MainMenuNewGame();
		Enter(EPhase::Difficulty);
		return false;

	case EPhase::Difficulty:
		if (Step == 0)
		{
			if (!Expect(TEXT("New Game asks for the difficulty"), PC->IsDifficultyPromptOpen() && PC->GetDifficultyPromptWidget()))
			{
				Finish(World);
				return true;
			}
			Mark(TEXT("difficulty prompt up; picking Normal"));
			NextStep();
			return false;
		}
		if (Step == 1)
		{
			if (InStep() < 1.0)
			{
				return false;
			}
			PC->GetDifficultyPromptWidget()->Choose(EHawkeyeDifficulty::Normal);
			NextStep();
			return false;
		}
		{
			const UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World);
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
			const UChaseRouteDefinition* Route = Chase ? Chase->GetRoute() : nullptr;
			Expect(TEXT("Normal is the difficulty"), Settings && Settings->GetDifficulty() == EHawkeyeDifficulty::Normal);
			Expect(TEXT("the menu and the prompt are gone"), !PC->IsMainMenuOpen() && !PC->IsDifficultyPromptOpen());
			Expect(TEXT("New Game wrote the first save"), Save && Save->HasSave());
			Expect(TEXT("the current objective is chase_thief"), CurrentObjectiveId(World) == TEXT("chase_thief"));
			Expect(TEXT("the chase is running from the first frame"), Chase && Chase->GetState() == EChaseState::Running && Route);
			Expect(TEXT("no partner on screen: no HUD partner line"), !PC->FindPartnerController());
			Expect(TEXT("the switch is refused"), !PC->IsSwitchingAllowed());
			if (Route && Kate)
			{
				const float FromStart = FVector::Dist(Feet(Kate), Route->PlayerStart);
				Expect(FString::Printf(TEXT("Kate is on the route's start (%.0f cm off)"), FromStart), FromStart < 400.f);
				Expect(FString::Printf(TEXT("Kate opens at a run (%.0f cm/s)"), Kate->GetVelocity().Size2D()), Kate->GetVelocity().Size2D() > 400.f);
				Mark(FString::Printf(TEXT("playing: mid-chase on %s, the thief %.0f cm ahead"), *Route->GetName(), Chase->GetLead()));
				PlanChase(Route);
			}
			ChaseStartedAt = FPlatformTime::Seconds();
			LastProgressAt = ChaseStartedAt;
			Enter(bFallBehind ? EPhase::FallBehind : EPhase::Chase);
			Sub(EStep::Run);
		}
		return false;

	case EPhase::Chase:
		if (!Kate || !PC)
		{
			return false;
		}
		ChaseActiveSeconds += UGameplayStatics::IsGamePaused(World) ? 0.f : World->GetDeltaSeconds();
		WatchTitle(PC);
		// The quit in the middle: once she has landed the first grapple, the thief's shot taken.
		if (!bQuitDone && Grapple == 1 && ChaseStep == EStep::Run && bThiefShot && InSub() > 1.4)
		{
			bQuitDone = true;
			StopMoving(PC);
			Mark(TEXT("Esc: pause mid-chase"));
			PressKey(PC, EKeys::Escape);
			Enter(EPhase::QuitMenu);
			return false;
		}
		if (TickChase(World, PC, Kate))
		{
			StopMoving(PC);
			Enter(EPhase::ChapterEnd);
		}
		return false;

	case EPhase::QuitMenu:
		if (Step == 0)
		{
			if (InPhase() < 0.6)
			{
				return false;
			}
			if (!PC->IsPauseMenuOpen())
			{
				Findings.Add(TEXT("Esc (simulated key) did not open the pause menu; opened through the controller"));
				PC->TogglePause();
			}
			Expect(TEXT("the pause menu is open"), PC->IsPauseMenuOpen());
			BeforeQuit = TakeSnapshot(World);
			SavesAtQuit = UHawkeyeSaveSubsystem::Get(World) ? UHawkeyeSaveSubsystem::Get(World)->GetSaveCount() : 0;
			Mark(FString::Printf(TEXT("Quit to menu mid-chase (thief %.0f, Kate %.0f along)"), BeforeQuit.ThiefProgress, BeforeQuit.KateProgress));
			PC->QuitToMenu();
			NextStep();
			return false;
		}
		if (InStep() < 1.5)
		{
			return false;
		}
		{
			const UHawkeyeMainMenuWidget* Menu = PC->GetMainMenuWidget();
			Expect(TEXT("the main menu is up after Quit to menu"), PC->IsMainMenuOpen() && !PC->IsPauseMenuOpen());
			Expect(TEXT("Continue is on"), Menu && Menu->IsContinueEnabled());
			Expect(TEXT("the world is paused under the menu"), UGameplayStatics::IsGamePaused(World));
			Mark(FString::Printf(TEXT("menu up (saves this session %d at the quit, %d now); Continue"), SavesAtQuit,
				UHawkeyeSaveSubsystem::Get(World)->GetSaveCount()));
			DistrictWorld = World;
			PC->MainMenuContinue();
			Enter(EPhase::Continue);
		}
		return false;

	case EPhase::Continue:
		if (!WorldSettled(World, DistrictWorld.Get(), 0.5f))
		{
			if (InPhase() > 60.0)
			{
				Expect(TEXT("Continue loads the district inside 60 s"), false);
				Finish(World);
				return true;
			}
			return false;
		}
		{
			const HawkeyeCampaignLap::FSnapshot After = TakeSnapshot(World);
			const TArray<FString> Diff = Compare(BeforeQuit, After);
			Mark(FString::Printf(TEXT("Continue: district back after %.1f s; objective %s, %d difference(s)%s"), InPhase(),
				*After.Objective.ToString(), Diff.Num(), Diff.Num() ? *(TEXT(": ") + FString::Join(Diff, TEXT("; "))) : TEXT("")));
			for (const FString& Line : Diff)
			{
				Expect(TEXT("Continue brings back what was there at the quit: ") + Line, false);
			}
			Expect(TEXT("no title card after Continue"), !PC->IsChapterTitleShowing());
			Expect(TEXT("still no partner after Continue"), !PC->FindPartnerController());
		}
		Enter(EPhase::AfterContinue);
		return false;

	case EPhase::AfterContinue:
		if (InPhase() < 0.5)
		{
			return false;
		}
		CheckClean(TEXT("after Continue mid-chase"), {}, true, true);
		Enter(EPhase::Chase);
		bTitleGone = bTitleChecked = bTitleShot = true;
		Sub(EStep::Run);
		LastProgressAt = FPlatformTime::Seconds();
		return false;

	case EPhase::ChapterEnd:
	{
		const UMissionFlowController* Flow = PC ? PC->GetMissionFlow() : nullptr;
		const bool bCard = Flow && Flow->GetStep() == EMissionFlowStep::EndCard;
		if (bCard)
		{
			if (EndCardSeenAt < 0.0)
			{
				EndCardSeenAt = FPlatformTime::Seconds();
				bEndCardSeen = true;
				Mark(TEXT("end card"));
			}
			if (!bEndCardShot && FPlatformTime::Seconds() - EndCardSeenAt > 1.2)
			{
				bEndCardShot = true;
				Shot(TEXT("campaign_endcard.png"));
			}
			return false;
		}
		if (!bEndCardSeen)
		{
			if (InPhase() > 20.0)
			{
				Expect(TEXT("the catch ends the chapter on the end card inside 20 s"), false);
				Enter(EPhase::PhoneWait);
			}
			return false;
		}
		if (Flow && Flow->IsRunning())
		{
			return false;
		}
		if (Step == 0)
		{
			const UHawkeyeObjectiveWidget* Toasts = PC->GetHawkeyeHud() ? PC->GetHawkeyeHud()->GetObjectiveMarker() : nullptr;
			Expect(TEXT("back on the street with the chapter-complete toast"), Toasts && Toasts->IsToastVisible()
				&& Toasts->GetToastHeading().ToString() == TEXT("[Chapter complete]"));
			Expect(TEXT("every objective is complete"), CurrentObjectiveId(World).IsNone());
			Expect(TEXT("no flashback, no scene: the street"), !PC->IsFlashbackActive() && UHawkeyeSaveSubsystem::Get(World)
				&& !UHawkeyeSaveSubsystem::Get(World)->IsInPlayableScene());
			Mark(TEXT("back on the street after the end card"));
			NextStep();
			return false;
		}
		if (InStep() < 1.0)
		{
			return false;
		}
		Shot(TEXT("campaign_street.png"));
		Enter(EPhase::PhoneWait);
		return false;
	}

	case EPhase::PhoneWait:
	{
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
		const int32 Unread = Phone ? Phone->GetUnreadCount() : 0;
		if (Unread < 3)
		{
			if (InPhase() > 45.0)
			{
				Expect(FString::Printf(TEXT("Clint's and the landlord's texts arrive within 45 s of the street (%d unread)"), Unread), false);
				Enter(EPhase::Systems);
			}
			return false;
		}
		if (InPhase() < 3.0)
		{
			return false;
		}
		CheckClean(TEXT("the texts arrived"), {}, true, true);
		Mark(FString::Printf(TEXT("%d unread texts; P: opening the phone"), Unread));
		PressKey(PC, EKeys::P);
		Enter(EPhase::PhoneOpen);
		return false;
	}

	case EPhase::PhoneOpen:
		if (InPhase() < 0.9)
		{
			return false;
		}
		if (!PC->IsPhoneOpen())
		{
			Findings.Add(TEXT("P did not open the phone from a simulated key; opened through the controller"));
			PC->TogglePhone();
			PhaseStart = FPlatformTime::Seconds();
			return false;
		}
		{
			const UPhoneWidget* Phone = PC->GetPhoneWidget();
			Expect(TEXT("the phone shows a thread"), Phone && Phone->GetThreadLength() > 0);
			Expect(TEXT("the game is paused under the phone"), UGameplayStatics::IsGamePaused(World));
			const UPhoneSubsystem* Phones = UPhoneSubsystem::Get(World);
			const TArray<FString> Contacts = Phones && Phones->GetInbox() ? Phones->GetInbox()->GetContacts() : TArray<FString>();
			Mark(FString::Printf(TEXT("phone contacts: %s"), *FString::Join(Contacts, TEXT(", "))));
			Expect(TEXT("Kate's replies sit in the threads they answer, not under her own name"), !Contacts.Contains(TEXT("Kate")));
			Shot(TEXT("campaign_phone.png"));
			Enter(EPhase::PhoneClose);
		}
		return false;

	case EPhase::PhoneClose:
		if (Step == 0)
		{
			if (InPhase() < 1.2)
			{
				return false;
			}
			Mark(TEXT("P: closing the phone"));
			PressKey(PC, EKeys::P);
			NextStep();
			return false;
		}
		if (InStep() < 1.0)
		{
			return false;
		}
		if (PC->IsPhoneOpen())
		{
			Findings.Add(TEXT("P did not close the phone from a simulated key while paused; closed through the controller"));
			PC->TogglePhone();
			StepAt = FPlatformTime::Seconds();
			return false;
		}
		CheckClean(TEXT("phone closed"));
		Enter(EPhase::Systems);
		return false;

	case EPhase::Systems:
		switch (Step)
		{
		case 0:
		{
			UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			Expect(TEXT("a save works on the street after the chapter"), Save && Save->SaveCampaign(TEXT("campaign lap: after the chapter")));
			const UHawkeyeMusicSubsystem* Music = UHawkeyeMusicSubsystem::Get(World);
			Expect(TEXT("the score is on and roaming"), Music && !Music->IsScorePaused() && Music->GetState() == EHawkeyeMusicState::Roam);
			Expect(TEXT("hints may run"), UHawkeyeHintSubsystem::IsRunAllowed());
			Mark(FString::Printf(TEXT("systems: save ok, music %s, M for the map"), Music ? *UEnum::GetValueAsString(Music->GetState()) : TEXT("-")));
			PressKey(PC, EKeys::M);
			NextStep();
			return false;
		}
		case 1:
			if (InStep() < 1.0)
			{
				return false;
			}
			if (!PC->IsMapOpen())
			{
				Findings.Add(TEXT("M (simulated key) did not open the map; opened through the controller"));
				PC->SetMapOpen(true);
			}
			Expect(TEXT("the map opens after the chapter"), PC->IsMapOpen());
			PC->SetMapOpen(false);
			NextStep();
			return false;
		case 2:
		{
			if (InStep() < 1.0)
			{
				return false;
			}
			CheckClean(TEXT("map closed"), {}, true);
			UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
			AChallengeStart* Start = Challenges ? Challenges->FindNearestStart(Kate->GetActorLocation()) : nullptr;
			const bool bStarted = Start && Challenges->StartChallenge(Start, Kate);
			Expect(TEXT("a challenge starts after the chapter"), bStarted && Challenges->IsRunning());
			Mark(FString::Printf(TEXT("challenge %s started (test shortcut: started from here, not walked to)"), *GetNameSafe(Start)));
			NextStep();
			return false;
		}
		case 3:
		{
			if (InStep() < 1.5)
			{
				return false;
			}
			if (UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World))
			{
				Challenges->AbortChallenge();
			}
			NextStep();
			return false;
		}
		case 4:
			if (InStep() < 1.0)
			{
				return false;
			}
			if (PC->IsChallengeResultsOpen())
			{
				PC->CloseChallengeResults();
				return false;
			}
			Expect(TEXT("the challenge is over"), !UChallengeSubsystem::Get(World)->IsRunning());
			{
				UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
				Save->DiscoverSafehouse(FirstSafehouse);
				Save->DiscoverSafehouse(SecondSafehouse);
				Mark(TEXT("fast travel to [Safehouse 1] (test shortcut: both marked found)"));
				Expect(TEXT("fast travel starts after the chapter"), PC->FastTravelTo(FirstSafehouse));
			}
			NextStep();
			return false;
		case 5:
		{
			const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
			if (Safehouses && Safehouses->IsTravelling())
			{
				if (InStep() > 10.0)
				{
					Expect(TEXT("the fast travel ends inside 10 s"), false);
					NextStep();
				}
				return false;
			}
			if (InStep() < 1.5)
			{
				return false;
			}
			const ASafehouse* Door = Safehouses ? Safehouses->FindSafehouse(FirstSafehouse) : nullptr;
			const float Off = Door ? FVector::Dist(Kate->GetActorLocation(), Door->GetActorLocation()) : -1.f;
			Expect(FString::Printf(TEXT("fast travel put Kate at [Safehouse 1] (%.0f cm)"), Off), Off >= 0.f && Off < 400.f);
			CheckClean(TEXT("after the fast travel"), {}, true);
			Mark(FString::Printf(TEXT("travelled; %.0f cm from the door"), Off));
			NextStep();
			return false;
		}
		default:
			Enter(EPhase::CrimeApproach);
			return false;
		}

	case EPhase::CrimeApproach:
	{
		// A crime comes 25 to 40 m out and never within 40 m of a safehouse: walk towards the nearest ambush spot first.
		UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
		if (!Crimes || !Kate)
		{
			return false;
		}
		if (Step == 0)
		{
			ACrimeSpot* Best = nullptr;
			for (ACrimeSpot* Spot : Crimes->GetSpots())
			{
				if (Spot && Spot->FindCrime(ECrimeType::Ambush) && (!Best || FVector::Dist2D(Spot->GetActorLocation(), Kate->GetActorLocation())
					< FVector::Dist2D(Best->GetActorLocation(), Kate->GetActorLocation())))
				{
					Best = Spot;
				}
			}
			const UNavigationPath* NavPath = Best ? UNavigationSystemV1::FindPathToLocationSynchronously(World, Kate->GetActorLocation(),
				Best->GetActorLocation(), Kate) : nullptr;
			if (!Best || !NavPath || !NavPath->IsValid())
			{
				Expect(TEXT("a crime spot with an ambush to walk to"), false);
				Enter(EPhase::FinalQuit);
				return false;
			}
			Goal = Best->GetActorLocation();
			Path = NavPath->PathPoints;
			PathIndex = 0;
			Mark(FString::Printf(TEXT("walking towards %s (%.0f m) for a crime"), *Best->GetName(), FVector::Dist2D(Goal, Kate->GetActorLocation()) / 100.f));
			NextStep();
			return false;
		}
		const bool bNear = FVector::Dist2D(Goal, Kate->GetActorLocation()) < 3200.f;
		if (!bNear && Path.IsValidIndex(PathIndex) && InStep() < 90.0)
		{
			if (Steer(PC, Kate, Path[PathIndex], true, 150.f))
			{
				++PathIndex;
			}
			return false;
		}
		StopMoving(PC);
		Mark(FString::Printf(TEXT("stopped %.0f m from the spot"), FVector::Dist2D(Goal, Kate->GetActorLocation()) / 100.f));
		Enter(EPhase::Crime);
		return false;
	}

	case EPhase::Crime:
	{
		UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
		if (!Crimes || !Kate)
		{
			return false;
		}
		if (Step == 0)
		{
			// The schedule's own start, told to come soon and to be an ambush (four thugs alerted from the start).
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeInterval")))
			{
				Var->Set(TEXT("1"), ECVF_SetByCode);
			}
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeType")))
			{
				Var->Set(TEXT("Ambush"), ECVF_SetByCode);
			}
			Mark(TEXT("crime: interval 1 s, type Ambush; waiting for the schedule"));
			NextStep();
			return false;
		}
		if (Step == 1)
		{
			if (!Crimes->IsCrimeActive())
			{
				if (InStep() > 30.0)
				{
					Expect(TEXT("a crime starts inside 30 s with the interval at 1"), false);
					Enter(EPhase::FinalQuit);
				}
				return false;
			}
			const FVector SpotAt = Crimes->GetActiveSpot() ? Crimes->GetActiveSpot()->GetActorLocation() : Kate->GetActorLocation();
			Mark(FString::Printf(TEXT("crime on at %s, %.0f m away; walking in to lose it"), *GetNameSafe(Crimes->GetActiveSpot()),
				FVector::Dist(Kate->GetActorLocation(), SpotAt) / 100.f));
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeInterval")))
			{
				Var->Set(*CrimeIntervalBefore, ECVF_SetByCode);
			}
			if (const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(World, Kate->GetActorLocation(),
				SpotAt, Kate))
			{
				Path = NavPath->PathPoints;
			}
			PathIndex = 0;
			DistrictWorld = World;
			NextStep();
			return false;
		}
		// Walk in and stand there; when down, a key gives up (no partner in Act 1: no revive is coming).
		if (!Kate->IsDowned() && Kate->GetHealthComponent()->IsAlive())
		{
			if (Path.IsValidIndex(PathIndex) && Steer(PC, Kate, Path[PathIndex], false, 150.f))
			{
				++PathIndex;
			}
			else if (!Path.IsValidIndex(PathIndex))
			{
				StopMoving(PC);
			}
			if (InStep() > 90.0)
			{
				StopMoving(PC);
				Force(TEXT("the ambush had not downed Kate in 90 s; applying damage"));
				Kate->GetHealthComponent()->ApplyDamage(10000.f, nullptr);
				StepAt = FPlatformTime::Seconds();
			}
			return false;
		}
		StopMoving(PC);
		if (Kate->IsDowned() && Step == 2)
		{
			++DownCount;
			Mark(FString::Printf(TEXT("down (%s)"), Kate->IsReviveExpected() ? TEXT("a revive is coming") : TEXT("no revive coming")));
			Expect(TEXT("no partner revive in Act 1"), !Kate->IsReviveExpected());
			NextStep();
			return false;
		}
		if (Kate->IsDowned() && !Kate->IsReviveExpected() && InStep() > 0.8 && Step == 3)
		{
			Mark(TEXT("any key: giving up"));
			PressKey(PC, EKeys::SpaceBar);
			NextStep();
			return false;
		}
		if (Step >= 4 || Kate->HasDiedFromDown())
		{
			bDied = true;
			Mark(TEXT("dead; waiting for the reload"));
			Enter(EPhase::CrimeReload);
		}
		return false;
	}

	case EPhase::CrimeReload:
		if (!WorldSettled(World, DistrictWorld.Get(), 1.f))
		{
			if (InPhase() > 45.0)
			{
				Expect(TEXT("dying reloads the last save inside 45 s"), false);
				Enter(EPhase::FinalQuit);
			}
			return false;
		}
		if (InPhase() < 3.0)
		{
			return false;
		}
		{
			const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
			int32 CrimeThugs = 0;
			for (TActorIterator<AThugCharacter> It(World); It; ++It)
			{
				CrimeThugs += It->ActorHasTag(UCrimeSubsystem::CrimeThugTag) ? 1 : 0;
			}
			Mark(FString::Printf(TEXT("reloaded after %.1f s: health %.0f, %d crime thug(s), crime %s"), InPhase(),
				Kate->GetHealthComponent()->GetCurrentHealth(), CrimeThugs, Crimes && Crimes->IsCrimeActive() ? TEXT("on") : TEXT("off")));
			Expect(TEXT("no crime running after the reload"), Crimes && !Crimes->IsCrimeActive() && CrimeThugs == 0);
			Expect(TEXT("full health after the reload"), Kate->GetHealthComponent()->GetHealthPercent() > 0.99f);
			Expect(TEXT("the chapter is still complete after the reload"), CurrentObjectiveId(World).IsNone());
			Expect(TEXT("no chase after the chapter's reload"), Chase && !Chase->IsChaseRunning());
			Expect(TEXT("not in a scene after the reload"), Save && !Save->IsInPlayableScene());
			CheckClean(TEXT("after the death reload"), {}, true, false);
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeType")))
			{
				Var->Set(*CrimeTypeBefore, ECVF_SetByCode);
			}
			Enter(EPhase::FinalQuit);
		}
		return false;

	case EPhase::FinalQuit:
		if (Step == 0)
		{
			if (InPhase() < 1.0)
			{
				return false;
			}
			PressKey(PC, EKeys::Escape);
			NextStep();
			return false;
		}
		if (Step == 1)
		{
			if (InStep() < 0.6)
			{
				return false;
			}
			if (!PC->IsPauseMenuOpen())
			{
				PC->TogglePause();
			}
			BeforeQuit = TakeSnapshot(World);
			Mark(TEXT("Esc > Quit to menu after the chapter"));
			PC->QuitToMenu();
			NextStep();
			return false;
		}
		if (InStep() < 1.5)
		{
			return false;
		}
		Expect(TEXT("the main menu is up"), PC->IsMainMenuOpen());
		Expect(TEXT("Continue is on"), PC->GetMainMenuWidget() && PC->GetMainMenuWidget()->IsContinueEnabled());
		Mark(TEXT("Continue"));
		DistrictWorld = World;
		PC->MainMenuContinue();
		Enter(EPhase::FinalContinue);
		return false;

	case EPhase::FinalContinue:
		if (!WorldSettled(World, DistrictWorld.Get(), 1.f))
		{
			if (InPhase() > 60.0)
			{
				Expect(TEXT("the second Continue loads inside 60 s"), false);
				Finish(World);
				return true;
			}
			return false;
		}
		if (InPhase() < 3.0)
		{
			return false;
		}
		{
			const HawkeyeCampaignLap::FSnapshot After = TakeSnapshot(World);
			const TArray<FString> Diff = Compare(BeforeQuit, After);
			Mark(FString::Printf(TEXT("second Continue: %d difference(s)%s"), Diff.Num(), Diff.Num() ? *(TEXT(": ") + FString::Join(Diff, TEXT("; "))) : TEXT("")));
			for (const FString& Line : Diff)
			{
				Expect(TEXT("the second Continue brings back what was there: ") + Line, false);
			}
			Expect(TEXT("the chapter is still complete"), CurrentObjectiveId(World).IsNone());
			Expect(TEXT("the old chapter's arrow does not offer itself"), !FindChapterEnd(World)
				|| !IInteractable::Execute_CanInteract(FindChapterEnd(World), Kate));
			Expect(TEXT("no title card, end card or flashback after the second Continue"), !PC->IsChapterTitleShowing()
				&& !PC->IsFlashbackActive() && !(PC->GetMissionFlow() && PC->GetMissionFlow()->IsRunning()));
			CheckClean(TEXT("after the second Continue"), {}, true, false);
			Mark(TEXT("done"));
			Finish(World);
			Enter(EPhase::Done);
		}
		return false;

	case EPhase::FallBehind:
	{
		// The new tester who stops to look at the view: no input at all until the chase is lost.
		UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
		if (!Chase || !PC)
		{
			return false;
		}
		ChaseActiveSeconds += UGameplayStatics::IsGamePaused(World) ? 0.f : World->GetDeltaSeconds();
		WatchTitle(PC);
		SampleLead(Chase);
		if (LostAt < 0.0 && Chase->GetState() == EChaseState::Lost)
		{
			LostAt = FPlatformTime::Seconds();
			ChaseSeconds = ChaseActiveSeconds;
			const UHawkeyeObjectiveWidget* Toasts = PC->GetHawkeyeHud() ? PC->GetHawkeyeHud()->GetObjectiveMarker() : nullptr;
			const FString Heading = Toasts && Toasts->IsToastQueued() ? Toasts->GetToastHeading().ToString() : FString();
			bLostToastSeen = Heading == TEXT("[He got away]");
			Mark(FString::Printf(TEXT("lost after %.1f s of chase standing still (lead %.0f to %.0f cm); toast \"%s\""), ChaseSeconds, MinLead,
				MaxLead, *Heading));
			Expect(TEXT("the thief goes with the loss"), !Chase->GetThief());
			DistrictWorld = World;
			Shot(TEXT("campaign_lost.png"));
			Enter(EPhase::Reloaded);
			return false;
		}
		if (InPhase() > 40.0)
		{
			Expect(TEXT("standing still loses the chase inside 40 s"), false);
			Finish(World);
			return true;
		}
		return false;
	}

	case EPhase::Reloaded:
		if (World == DistrictWorld.Get() && PC && !bLostToastSeen)
		{
			// Another notice may be in front for a moment: the loss's toast is looked for until the reload.
			const UHawkeyeObjectiveWidget* Toasts = PC->GetHawkeyeHud() ? PC->GetHawkeyeHud()->GetObjectiveMarker() : nullptr;
			bLostToastSeen = Toasts && Toasts->IsToastQueued() && Toasts->GetToastHeading().ToString() == TEXT("[He got away]");
		}
		if (!WorldSettled(World, DistrictWorld.Get(), 0.5f))
		{
			if (InPhase() > 60.0)
			{
				Expect(TEXT("the checkpoint reloads inside 60 s of the loss"), false);
				Finish(World);
				return true;
			}
			return false;
		}
		if (Step == 0)
		{
			const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
			const UChaseRouteDefinition* Route = Chase ? Chase->GetRoute() : nullptr;
			Mark(FString::Printf(TEXT("checkpoint loaded %.1f s after the loss"), FPlatformTime::Seconds() - LostAt));
			Expect(TEXT("losing him says so before the reload: \"[He got away]\""), bLostToastSeen);
			Expect(TEXT("the chase picks up again from the checkpoint"), Chase && Chase->GetState() == EChaseState::Running && Chase->GetThief());
			Expect(TEXT("the objective is chase_thief again"), CurrentObjectiveId(World) == TEXT("chase_thief"));
			if (Chase && Route && Kate)
			{
				const float FromStart = FVector::Dist(Feet(Kate), Route->PlayerStart);
				Mark(FString::Printf(TEXT("after the reload: Kate %.0f cm from the start, thief %.0f along (Kate %.0f), lose clock %.1f"), FromStart,
					Chase->GetThiefProgress(), Chase->GetKateProgress(), Chase->GetLoseClock()));
				Expect(TEXT("Kate is back where the new game's save had her (the chase's start)"), FromStart < 400.f);
				Expect(TEXT("the thief is no further ahead than a load allows"),
					Chase->GetThiefProgress() <= Chase->GetKateProgress() + Route->Tuning.RestoreMaxLead + 1.f);
				Expect(TEXT("the lose clock is back to 0"), Chase->GetLoseClock() < 0.5f);
				Goal = FVector(Chase->GetThiefProgress(), 0.f, 0.f);
			}
			CheckClean(TEXT("after the chase's checkpoint reload"), {}, true, true);
			Expect(TEXT("no partner after the reload"), !PC->FindPartnerController());
			Shot(TEXT("campaign_lost_reload.png"));
			NextStep();
			return false;
		}
		if (InStep() < 3.0)
		{
			return false;
		}
		{
			const UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
			Expect(TEXT("the thief runs on after the reload"), Chase && Chase->GetThiefProgress() > Goal.X + 300.f);
			Mark(FString::Printf(TEXT("3 s on: thief %.0f along"), Chase ? Chase->GetThiefProgress() : -1.f));
			Finish(World);
			Enter(EPhase::Done);
		}
		return false;

	case EPhase::Done:
		return InPhase() > 1.0;
	}
	return false;
}

static bool HawkeyeStartCampaignLap(FAutomationTestBase* Test, bool bFallBehind)
{
	using namespace HawkeyeCampaignLap;
	if (!FApp::CanEverRender())
	{
		Test->AddInfo(TEXT("No RHI: skipping the campaign lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	UWorld* World = FindWorld();
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World);
	if (!Save || !Settings)
	{
		Test->AddError(TEXT("Campaign: no save or settings subsystem (run it in the standalone game)."));
		return true;
	}
	const TSharedRef<FHawkeyeCampaignLapRunner> Runner = MakeShared<FHawkeyeCampaignLapRunner>(Test, bFallBehind);
	Runner->SettingsBefore = Settings->GetSettings();
	Runner->bHaveSettings = true;
	auto ReadCVar = [](const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetString() : FString();
	};
	Runner->HintsCVarBefore = ReadCVar(TEXT("hawkeye.Hints"));
	Runner->CrimeIntervalBefore = ReadCVar(TEXT("hawkeye.CrimeInterval"));
	Runner->CrimeTypeBefore = ReadCVar(TEXT("hawkeye.CrimeType"));
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.Hints")))
	{
		Var->Set(TEXT("2"), ECVF_SetByCode);
	}

	// A cold start: no save in the lap's slot, the menu armed for the next world, played as a player.
	Save->SlotNameOverride = Slot;
	IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
	Save->RearmBootMenu();
	HawkeyeAutomation::SetPlaysAsPlayer(true);
	FAutomationTestFramework::Get().EnqueueLatentCommand(Runner);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(Test));
	return true;
}

bool FHawkeyeLapCampaign::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCampaignLap(this, /*bFallBehind=*/false);
}

bool FHawkeyeLapCampaignFallBehind::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCampaignLap(this, /*bFallBehind=*/true);
}

#endif
