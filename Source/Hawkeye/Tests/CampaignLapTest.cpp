#include "Audio/HawkeyeMusicSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Combat/BowComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Crime/CrimeSpot.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Flashback/FlashbackSequencer.h"
#include "Framework/Application/SlateApplication.h"
#include "Flashback/FlashbackWidget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
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
#include "Mission/ObjectiveTriggerVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Phone/PhoneInbox.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
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
#include "World/FireEscapeLanding.h"
#include "Player/ParkourComponent.h"
#include "Tests/GrappleAuditKit.h"
#include "World/GrappleAnchor.h"
#include "World/InteractionComponent.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Lap.Campaign: Chapter 1 end to end from a cold start, in the standalone game, played as a new
 * tester would play it:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap.Campaign; Quit"
 *
 * Its own save slot, deleted first; the settings are put back as they were at the end. The test hook
 * AHawkeyePlayerController::bAutomationPlaysAsPlayer gives it what automation otherwise skips (the boot
 * menu, the difficulty prompt, the chapter opening, the notices, the crime schedule) and hawkeye.Hints 2
 * turns the hints on.
 *
 * The run: the main menu with no save, New Game, Normal at the prompt, the title card, the first text
 * read on the phone (P, then P again), reach_roof and cross_block by grapple (a planner that walks to a
 * spot with a clear line, aims, presses Q and walks the roof; with nothing in reach from a roof, down its
 * fire escape and up again from the street), clear_roof against the RoofPair with the
 * bow, a bola and melee, then Esc > Quit to menu and Continue, compared field by field with what was
 * there before; find_arrow, its roof reached by grapple (down cross_block's fire escape and up from the
 * street to the facade anchor on its street wall; never put on it: an error if it cannot), the examine (E),
 * the close-up, the end card, the slides, the placeholder
 * room walked to its trigger, and the street again, where the save, the map, the music, a challenge, a
 * fast travel and the crime schedule are tried; an ambush lost on purpose and the reload after it; and
 * Quit to menu and Continue once more. Every step checks input, the HUD, the pause, the time dilation,
 * the camera fade, the grey post process and the widgets in the viewport. Everything that had to be
 * forced is listed as such. Writes Saved/Automation/campaign_lap.json and
 * Saved/Screenshots/Campaign/campaign_*.png.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCampaign, "Hawkeye.Lap.Campaign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeCampaignLap
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* SceneMapName = TEXT("L_Scene_Placeholder");
	static const TCHAR* Slot = TEXT("HawkeyeCampaignLap");
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* InteractPath = TEXT("/Game/Input/IA_Interact.IA_Interact");
	static const TCHAR* Slot1Path = TEXT("/Game/Input/IA_Slot1.IA_Slot1");
	static const TCHAR* Slot4Path = TEXT("/Game/Input/IA_Slot4.IA_Slot4");
	static const TCHAR* CrouchPath = TEXT("/Game/Input/IA_Crouch.IA_Crouch");
	static const FName BuildingTag(TEXT("CityBuilding"));
	static const FName RoofPairTag(TEXT("RoofPair"));
	static const FName ArcherPairTag(TEXT("ArcherPair"));
	static const FName FirstSafehouse(TEXT("ch01_east_7th"));
	static const FName SecondSafehouse(TEXT("avenue_b"));
	static constexpr int32 SlotStandard = 1;
	static constexpr int32 SlotBola = 4;

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

	static bool IsSceneWorld(const UWorld* World)
	{
		return World && World->GetMapName().Contains(SceneMapName);
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

	/** The CityBuilding under Point within Depth cm, or null. */
	static AActor* BuildingUnder(UWorld* World, const FVector& Point, const AActor* Ignore, float Depth = 300.f)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignRoof), false, Ignore);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 50.f), Point - FVector(0.f, 0.f, Depth), ECC_Visibility, Params)
			&& Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag))
		{
			return Hit.GetActor();
		}
		return nullptr;
	}

	/** The first thing under XY from FromZ down to ToZ. */
	static bool GroundAt(UWorld* World, const FVector& XY, float FromZ, float ToZ, const AActor* Ignore, FVector& OutGround, AActor** OutActor)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignGround), false, Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, ToZ), ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		*OutActor = Hit.GetActor();
		return true;
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

	static AObjectiveTriggerVolume* FindTrigger(UWorld* World, FName Id)
	{
		for (TActorIterator<AObjectiveTriggerVolume> It(World); It; ++It)
		{
			if (It->ObjectiveId == Id)
			{
				return *It;
			}
		}
		return nullptr;
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
			if (It->ActorHasTag(UCrimeSubsystem::CrimeThugTag))
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
		return Out;
	}
}

// --- The run ------------------------------------------------------------------------------------------

class FHawkeyeCampaignLapRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeCampaignLapRunner(FAutomationTestBase* InTest) : Test(InTest)
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
		Title,
		PhoneWait,
		PhoneOpen,
		PhoneClose,
		ReachRoof,
		CrossBlock,
		ClearRoof,
		Calm,
		ArcherFight,
		QuitMenu,
		Continue,
		AfterContinue,
		FindArrow,
		ChapterEnd,
		Scene,
		SceneWalk,
		Return,
		Systems,
		CrimeApproach,
		Crime,
		CrimeReload,
		FinalQuit,
		FinalContinue,
		Done,
	};

	/** The traversal legs' own steps. */
	enum class ELeg : uint8
	{
		Plan,
		Walk,
		Aim,
		Zip,
		Descend,
		Final,
	};

	FAutomationTestBase* Test;
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
	int32 CheckFailures = 0;
	TWeakObjectPtr<UWorld> LastWorld;
	TArray<FString> Shots;

	// Traversal.
	ELeg Leg = ELeg::Plan;
	FName LegObjective;
	FVector Goal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> GoalBuilding;
	TWeakObjectPtr<AGrappleAnchor> PlanAnchor;
	TArray<FVector> Path;
	int32 PathIndex = 0;
	TSet<TWeakObjectPtr<AGrappleAnchor>> Excluded;
	TSet<TWeakObjectPtr<AActor>> VisitedRoofs;
	int32 Hops = 0;
	int32 Presses = 0;
	double LegStart = 0.0;
	double SubStart = 0.0;
	double PressAt = -1.0;
	bool bMoveInjecting = false;
	bool bSprintHeld = false;
	FVector LastProgressSpot = FVector::ZeroVector;
	double LastProgressAt = 0.0;
	TArray<FString> LegNotes;

	// Down a fire escape (the find_arrow leg: the street to it is wider than the grapple's range).
	int32 DescendPhase = 0;
	FVector DescendStand = FVector::ZeroVector;
	FVector DescendOut = FVector::ForwardVector;
	FString DescendRoute;
	double DescendStart = 0.0;
	bool bDescendPushed = false;
	int32 DescendRetries = 0;
	int32 DescendHangs = 0;
	int32 DescendCatches = 0;
	int32 DescendStops = 0;
	float StreetZ = 0.f;

	/** The top landing of the fire escape on Roof, or null. */
	AFireEscapeLanding* TopLandingOn(UWorld* World, const AActor* Roof) const;

	/** Down the fire escape on the roof she stands on: false when it has none. */
	bool BeginDescend(UWorld* World, AHawkeyeCharacter* Kate);

	// The fight.
	FName FightTag = HawkeyeCampaignLap::RoofPairTag;
	double FightStartedAt = 0.0;
	TArray<TWeakObjectPtr<AThugCharacter>> Pair;
	bool bFightSetUp = false;
	bool bDrawing = false;
	bool bMeleeHeld = false;
	bool bBolaLoosed = false;
	int32 BolaStage = 0;
	int32 BolaTries = 0;
	double DrawStart = 0.0;
	double MeleeHeldSince = 0.0;
	double NextActionAt = 0.0;
	double LastParryTap = -10.0;
	int32 BolaBefore = 0;
	int32 ArrowsLoosed = 0;
	int32 Swings = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 HitsTaken = 0;
	float LastHealth = 0.f;

	// Save and chapter end.
	HawkeyeCampaignLap::FSnapshot BeforeQuit;
	int32 SavesAtQuit = 0;
	TArray<FString> Beats;
	int32 LastSlide = INDEX_NONE;
	TArray<FString> SlideTimes;
	bool bEndCardShot = false;
	bool bSlideShot = false;
	double EndCardSeenAt = -1.0;
	double SlideSeenAt = -1.0;
	TWeakObjectPtr<UWorld> DistrictWorld;
	bool bDied = false;
	int32 DownCount = 0;

	double Clock() const { return FPlatformTime::Seconds() - LapStart; }
	double InPhase() const { return FPlatformTime::Seconds() - PhaseStart; }

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

	double InStep() const { return FPlatformTime::Seconds() - StepAt; }

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

	/** The widgets in this world's viewport other than the HUD and the ones named in Allowed. */
	static TArray<FString> StrayWidgets(UWorld* World, const TArray<UClass*>& Allowed);

	/**
	 * Input free, HUD up, nothing paused, dilated, faded, greyed or left on the screen. What says what the
	 * moment allows (the menu, a card).
	 */
	void CheckClean(const FString& Where, const TArray<UClass*>& Allowed = {}, bool bHudExpected = true, bool bToastAllowed = true);

	void StopMoving(APlayerController* PC);
	bool Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive);
	void SetSprint(APlayerController* PC, bool bSprint);

	/** Starts a traversal leg to Id's goal. False (and an error) when the goal is not on the map. */
	bool BeginLeg(UWorld* World, FName Id);

	/** One frame of a traversal leg. True when the leg is over (the objective done, or given up). */
	bool TickLeg(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate);

	/** Finds an anchor and a stand spot that gets Kate onto GoalBuilding, or nearer it. */
	bool PlanHop(UWorld* World, AHawkeyeCharacter* Kate, FString& OutNote);

	/** One frame of the roof fight. True when both of the pair are down (or it gave up). */
	bool TickFight(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate);

	/**
	 * One frame of the archer duel on the find_arrow roof, played the way the ArcherDuel lap does: crouch
	 * behind a parapet both archers cannot see over, stand only when neither is mid-draw, loose at full
	 * draw on one she can see, crouch again. True when both are down (or she went down and the save
	 * reloaded: DuelLosses counts those).
	 */
	bool TickDuel(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate);
	bool FindCover(UWorld* World, AHawkeyeCharacter* Kate);
	void Crouch(APlayerController* PC, AHawkeyeCharacter* Kate, bool bDown);
	enum class EDuel : uint8 { SetUp, Moving, Hidden, Rising, Drawing };
	EDuel Duel = EDuel::SetUp;
	double DuelSince = 0.0;
	double DuelStart = 0.0;
	double LastCrouchTap = -10.0;
	FVector Cover = FVector::ZeroVector;
	TWeakObjectPtr<UWorld> DuelWorld;
	TWeakObjectPtr<AThugCharacter> DuelTarget;
	int32 DuelLosses = 0;
	int32 DuelArrows = 0;
	int32 DuelHits = 0;
	int32 CoverSearches = 0;
	float DuelAimHeight = 30.f;

	/** Where the chapter end is up to, as a beat name. */
	FString CurrentBeat(UWorld* World) const;

	void NoteBeat(const FString& Beat)
	{
		if (!Beat.IsEmpty() && (Beats.IsEmpty() || Beats.Last() != Beat))
		{
			Beats.Add(Beat);
			Mark(TEXT("beat ") + Beat);
		}
	}

	/** Waits for a freshly loaded world with a player, no load pending and the fade in over. */
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

bool FHawkeyeCampaignLapRunner::Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive)
{
	using namespace HawkeyeCampaignLap;
	const FVector To = (Target - Kate->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
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

bool FHawkeyeCampaignLapRunner::BeginLeg(UWorld* World, FName Id)
{
	using namespace HawkeyeCampaignLap;
	LegObjective = Id;
	Leg = ELeg::Plan;
	Hops = 0;
	Excluded.Reset();
	VisitedRoofs.Reset();
	LegNotes.Reset();
	LegStart = FPlatformTime::Seconds();
	AHawkeyeCharacter* Kate = FindPlayer(World);
	if (Id == TEXT("find_arrow"))
	{
		const AChapterEndInteractable* Arrow = FindChapterEnd(World);
		if (!Arrow)
		{
			Test->AddError(TEXT("Campaign: no chapter-end arrow in the district."));
			return false;
		}
		// Where she stands to examine it: the middle of its zone, on whatever roof is under that.
		const UPrimitiveComponent* Zone = Arrow->FindComponentByClass<UBoxComponent>();
		const FVector Centre = Zone ? Zone->GetComponentLocation() : Arrow->GetActorLocation();
		FVector Ground = Centre;
		AActor* Under = nullptr;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignArrowRoof), false, Kate);
		Params.AddIgnoredActor(Arrow);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Centre + FVector(0.f, 0.f, 100.f), Centre - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params))
		{
			Ground = Hit.ImpactPoint;
			Under = Hit.GetActor();
		}
		// The tower sits on the roof; walk down to the building proper.
		AActor* Roof = Under;
		FVector Probe = Ground;
		for (int32 Guard = 0; Guard < 4 && Roof && !Roof->Tags.Contains(BuildingTag); ++Guard)
		{
			Params.AddIgnoredActor(Roof);
			if (!World->LineTraceSingleByChannel(Hit, Probe + FVector(0.f, 0.f, 10.f), Probe - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params))
			{
				Roof = nullptr;
				break;
			}
			Roof = Hit.GetActor();
			Probe = Hit.ImpactPoint;
		}
		Goal = Ground;
		GoalBuilding = Roof;
		// How a player gets up there at all: every spot (street or roof, the top surface on a 2 m grid) within
		// grapple range of an anchor on that roof with a clear zip, and the fire escapes on it.
		if (Roof && Kate)
		{
			const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
			const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			int32 Anchors = 0;
			int32 Clear = 0;
			TMap<FString, int32> From;
			for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
			{
				if (BuildingUnder(World, It->GetLandingLocation(), Kate) != Roof)
				{
					continue;
				}
				++Anchors;
				const FVector Marker = It->GetMarkerLocation();
				for (float X = -2400.f; X <= 2400.f; X += 200.f)
				{
					for (float Y = -2400.f; Y <= 2400.f; Y += 200.f)
					{
						FVector Spot;
						AActor* Top = nullptr;
						if (!GroundAt(World, Marker + FVector(X, Y, 0.f), Marker.Z + 3000.f, -1000.f, Kate, Spot, &Top) || Top == Roof)
						{
							continue;
						}
						const FVector At = Spot + FVector(0.f, 0.f, HalfHeight + 2.f);
						const float Reach = FVector::Dist(At, Marker);
						if (Reach > Grapple->Range - 150.f || Reach < Grapple->MinRange + 100.f || !Grapple->IsZipClear(At, *It, true))
						{
							continue;
						}
						++Clear;
						From.FindOrAdd(Top && Top->Tags.Contains(BuildingTag) ? Top->GetName() : FString(TEXT("street")))++;
					}
				}
			}
			int32 Escapes = 0;
			const FBox Box = Roof->GetComponentsBoundingBox().ExpandBy(300.f);
			for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
			{
				Escapes += Box.IsInside(It->GetActorLocation()) ? 1 : 0;
			}
			TArray<FString> Lines;
			for (const TPair<FString, int32>& Entry : From)
			{
				Lines.Add(FString::Printf(TEXT("%s %d"), *Entry.Key, Entry.Value));
			}
			Mark(FString::Printf(TEXT("survey of %s: %d anchor(s), %d clear stand spot(s) [%s], %d fire-escape landing(s)"), *Roof->GetName(),
				Anchors, Clear, *FString::Join(Lines, TEXT(", ")), Escapes));
		}
		Mark(FString::Printf(TEXT("leg find_arrow: the arrow's zone at %s on %s (%s), %.0f m away"), *Goal.ToCompactString(),
			*GetNameSafe(Under), *GetNameSafe(Roof), Kate ? FVector::Dist(Kate->GetActorLocation(), Goal) / 100.f : -1.f));
		return true;
	}
	const AObjectiveTriggerVolume* Trigger = FindTrigger(World, Id);
	if (!Trigger)
	{
		Test->AddError(FString::Printf(TEXT("Campaign: no trigger volume for %s."), *Id.ToString()));
		return false;
	}
	Goal = Trigger->GetActorLocation();
	FVector Ground;
	AActor* Under = nullptr;
	if (GroundAt(World, Goal, Goal.Z + 400.f, Goal.Z - 3000.f, Kate, Ground, &Under))
	{
		Goal = Ground;
	}
	GoalBuilding = Under && Under->Tags.Contains(BuildingTag) ? Under : nullptr;
	Mark(FString::Printf(TEXT("leg %s: trigger at %s on %s, %.0f m away"), *Id.ToString(), *Goal.ToCompactString(), *GetNameSafe(Under),
		Kate ? FVector::Dist(Kate->GetActorLocation(), Goal) / 100.f : -1.f));
	return true;
}

bool FHawkeyeCampaignLapRunner::PlanHop(UWorld* World, AHawkeyeCharacter* Kate, FString& OutNote)
{
	using namespace HawkeyeCampaignLap;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector Here = Kate->GetActorLocation();
	const FVector HereFeet = Feet(Kate);
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	AActor* OnBuilding = BuildingUnder(World, HereFeet, Kate);
	if (OnBuilding)
	{
		VisitedRoofs.Add(OnBuilding);
	}
	const float HereToGoal = FVector::Dist2D(Here, Goal);

	// Anchors on the goal's roof first (nearest its point), then any that land nearer the goal.
	TArray<TPair<float, AGrappleAnchor*>> Ranked;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		if (!It->bEnabled || Excluded.Contains(*It))
		{
			continue;
		}
		const FVector Landing = It->GetLandingLocation();
		AActor* LandsOn = BuildingUnder(World, Landing, Kate);
		if (!LandsOn || LandsOn == OnBuilding)
		{
			continue;
		}
		const float ToGoal = FVector::Dist2D(Landing, Goal);
		if (LandsOn == GoalBuilding.Get())
		{
			Ranked.Emplace(ToGoal, *It);
		}
		else if (ToGoal < HereToGoal - 800.f && FVector::Dist(Here, It->GetMarkerLocation()) < 6000.f)
		{
			Ranked.Emplace(100000.f + ToGoal, *It);
		}
		else if (!VisitedRoofs.Contains(LandsOn) && FVector::Dist(Here, It->GetMarkerLocation()) < 3000.f)
		{
			// Sideways, to a roof not stood on yet this leg: the way round when nothing ahead is in reach.
			Ranked.Emplace(200000.f + ToGoal, *It);
		}
	}
	Ranked.Sort([](const TPair<float, AGrappleAnchor*>& A, const TPair<float, AGrappleAnchor*>& B) { return A.Key < B.Key; });

	int32 Tried = 0;
	int32 Spots = 0;
	TMap<FString, int32> Blockers;
	for (const TPair<float, AGrappleAnchor*>& Entry : Ranked)
	{
		AGrappleAnchor* Anchor = Entry.Value;
		const FVector Marker = Anchor->GetMarkerLocation();
		// Only anchors a stand spot could be in range of count against the 25 tried: the far ones nearer the goal
		// used to use the budget up before one in reach was looked at.
		if (FVector::Dist2D(Here, Marker) > Grapple->Range + (OnBuilding ? 2200.f : 9000.f))
		{
			continue;
		}
		if (++Tried > 25)
		{
			break;
		}
		const int32 SpotsBefore = Spots;
		TArray<FVector> Stands;
		Stands.Add(Here);
		for (const float Distance : { 300.f, 600.f, 900.f, 1200.f, 1600.f, 2000.f })
		{
			for (int32 Angle = 0; Angle < 16; ++Angle)
			{
				Stands.Add(Here + FRotator(0.f, 22.5f * Angle, 0.f).Vector() * Distance);
			}
		}
		// A 2 m grid round the anchor, within reach of it in plan.
		for (float X = -2200.f; X <= 2200.f; X += 200.f)
		{
			for (float Y = -2200.f; Y <= 2200.f; Y += 200.f)
			{
				if (X * X + Y * Y <= 2200.f * 2200.f && X * X + Y * Y >= 500.f * 500.f)
				{
					Stands.Add(Marker + FVector(X, Y, 0.f));
				}
			}
		}
		float BestCost = BIG_NUMBER;
		FVector BestStand = FVector::ZeroVector;
		TArray<FVector> BestPath;
		for (int32 Index = 0; Index < Stands.Num(); ++Index)
		{
			FVector Ground = HereFeet;
			AActor* Under = OnBuilding;
			if (Index > 0)
			{
				if (!GroundAt(World, Stands[Index], HereFeet.Z + 250.f, HereFeet.Z - 400.f, Kate, Ground, &Under)
					|| FMath::Abs(Ground.Z - HereFeet.Z) > 150.f)
				{
					continue;
				}
				// The same roof she is on, or (on the street) not a roof at all.
				const bool bRoof = Under && Under->Tags.Contains(BuildingTag);
				if (OnBuilding ? Under != OnBuilding : bRoof)
				{
					continue;
				}
			}
			const FVector From = Ground + FVector(0.f, 0.f, HalfHeight + 2.f);
			const float Reach = FVector::Dist(From, Marker);
			if (Reach > Grapple->Range - 150.f || Reach < Grapple->MinRange + 100.f)
			{
				continue;
			}
			++Spots;
			if (FVector::Dist2D(Here, From) >= BestCost)
			{
				continue;
			}
			// The grapple picks by what the lens sees: the hip camera turned to put the anchor in the middle of the
			// screen (the spring arm's sums, the pitch limit and the probe, as Hawkeye.Grapple.Audit has it), so an
			// awning or a fire escape over her can hide it, and past the pitch limit it can leave the screen.
			{
				FVector Lens, Forward;
				HawkeyeGrappleView::PredictLens(Kate, From, Marker, Lens, Forward);
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector::DotProduct(Forward, (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
				if (Angle > Grapple->ConeDegrees - 3.f || !Grapple->IsOnScreen(Lens, Forward, Marker))
				{
					Blockers.FindOrAdd(TEXT("too steep for the camera"))++;
					continue;
				}
				if (!Grapple->HasLineOfSight(Anchor, Lens))
				{
					FCollisionQueryParams SightParams(SCENE_QUERY_STAT(CampaignLens), false, Kate);
					SightParams.AddIgnoredActor(Anchor);
					FHitResult SightHit;
					World->LineTraceSingleByChannel(SightHit, Lens, Marker, ECC_Visibility, SightParams);
					Blockers.FindOrAdd(FString(TEXT("the lens: ")) + GetNameSafe(SightHit.GetActor()))++;
					continue;
				}
			}
			AActor* Blocker = nullptr;
			if (!Grapple->IsZipClear(From, Anchor, /*bFromGround=*/true, &Blocker))
			{
				Blockers.FindOrAdd(Blocker ? Blocker->GetName() : FString(TEXT("something")))++;
				continue;
			}
			TArray<FVector> Route;
			float Length = FVector::Dist2D(Here, From);
			if (Index > 0)
			{
				bool bRouted = false;
				if (!OnBuilding)
				{
					const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(World, Here, From, Kate);
					if (NavPath && NavPath->IsValid() && !NavPath->IsPartial() && NavPath->PathPoints.Num() > 0)
					{
						Route = NavPath->PathPoints;
						Length = NavPath->GetPathLength();
						bRouted = Length < 9000.f;
					}
				}
				else
				{
					// Across her own roof in a straight line: every metre of it still roof.
					bRouted = true;
					const int32 Samples = FMath::Max(2, FMath::CeilToInt(Length / 100.f));
					for (int32 Sample = 1; Sample < Samples && bRouted; ++Sample)
					{
						const FVector At = FMath::Lerp(HereFeet, Ground, static_cast<float>(Sample) / Samples);
						bRouted = BuildingUnder(World, At, Kate, 200.f) == OnBuilding;
					}
					Route = { From };
				}
				if (!bRouted)
				{
					continue;
				}
			}
			// A steep look up from the foot of a wall rarely targets (the lens sits behind her): prefer standing back.
			const float Rise = FMath::RadiansToDegrees(FMath::Atan2(Marker.Z - From.Z, FVector::Dist2D(Marker, From)));
			Length += Rise > 45.f ? 3000.f : 0.f;
			if (Length < BestCost)
			{
				BestCost = Length;
				BestStand = From;
				BestPath = Route;
			}
		}
		if (BestCost < BIG_NUMBER)
		{
			PlanAnchor = Anchor;
			Path = BestPath;
			PathIndex = 0;
			OutNote = FString::Printf(TEXT("%s (lands on %s, %.0f m from the goal) from %s, %.0f m walk, %d anchors tried"),
				*Anchor->GetName(), *GetNameSafe(BuildingUnder(World, Anchor->GetLandingLocation(), Kate)),
				FVector::Dist2D(Anchor->GetLandingLocation(), Goal) / 100.f, *BestStand.ToCompactString(), BestCost / 100.f, Tried);
			return true;
		}
		if (Spots == SpotsBefore)
		{
			--Tried;
		}
	}
	Blockers.ValueSort([](int32 A, int32 B) { return A > B; });
	TArray<FString> Worst;
	for (const TPair<FString, int32>& Entry : Blockers)
	{
		if (Worst.Num() < 4)
		{
			Worst.Add(FString::Printf(TEXT("%s x%d"), *Entry.Key, Entry.Value));
		}
	}
	OutNote = FString::Printf(TEXT("no clear grapple from %s (%s): %d anchors ranked, %d tried, %d stand spots in range, lines blocked by %s"),
		*Here.ToCompactString(), OnBuilding ? *OnBuilding->GetName() : TEXT("street"), Ranked.Num(), Tried, Spots,
		Worst.Num() ? *FString::Join(Worst, TEXT(", ")) : TEXT("nothing"));
	return false;
}

AFireEscapeLanding* FHawkeyeCampaignLapRunner::TopLandingOn(UWorld* World, const AActor* Roof) const
{
	if (!Roof)
	{
		return nullptr;
	}
	FString Osm;
	for (const FName& Tag : Roof->Tags)
	{
		if (Tag.ToString().StartsWith(TEXT("osm:")))
		{
			Osm = Tag.ToString().Mid(4);
		}
	}
	AFireEscapeLanding* Top = nullptr;
	for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
	{
		if (!Osm.IsEmpty() && It->GetRecord().OsmId == Osm && (!Top || It->GetRecord().Floor > Top->GetRecord().Floor))
		{
			Top = *It;
		}
	}
	return Top;
}

bool FHawkeyeCampaignLapRunner::BeginDescend(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	AActor* Roof = BuildingUnder(World, Feet(Kate), Kate);
	const AFireEscapeLanding* Landing = TopLandingOn(World, Roof);
	if (!Landing)
	{
		return false;
	}
	// On the roof, square behind the top landing's middle: parapet (30), capsule (34), a margin.
	DescendOut = Landing->GetActorRightVector().GetSafeNormal2D();
	const FVector Facade = Landing->GetActorLocation();
	DescendStand = FVector(Facade.X, Facade.Y, Feet(Kate).Z) - DescendOut * 110.f;
	DescendRoute = FString::Printf(TEXT("fire escape of %s (%d landings)"), *GetNameSafe(Roof), Landing->GetRecord().Floor);
	FVector Street;
	AActor* Under = nullptr;
	StreetZ = GroundAt(World, Facade + DescendOut * 300.f, Facade.Z, Facade.Z - 5000.f, Kate, Street, &Under) ? Street.Z : 0.f;
	DescendPhase = 1;
	DescendStart = FPlatformTime::Seconds();
	SubStart = DescendStart;
	DescendRetries = 0;
	DescendHangs = 0;
	DescendCatches = 0;
	DescendStops = 0;
	bDescendPushed = false;
	Leg = ELeg::Descend;
	return true;
}

bool FHawkeyeCampaignLapRunner::TickLeg(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	if (IsObjectiveDone(World, LegObjective))
	{
		StopMoving(PC);
		Mark(FString::Printf(TEXT("%s complete (%.1f s, %d grapple(s)%s)"), *LegObjective.ToString(), FPlatformTime::Seconds() - LegStart, Hops,
			LegNotes.Num() ? *(TEXT(": ") + FString::Join(LegNotes, TEXT("; "))) : TEXT("")));
		return true;
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const double Now = FPlatformTime::Seconds();
	if (Now - LegStart > 150.0)
	{
		StopMoving(PC);
		if (LegObjective == TEXT("find_arrow"))
		{
			Test->AddError(FString::Printf(TEXT("Campaign: find_arrow roof not reached by grapple in 150 s: %s"), *FString::Join(LegNotes, TEXT("; "))));
		}
		Force(FString::Printf(TEXT("%s not reached in 150 s (%s); completing it"), *LegObjective.ToString(), *FString::Join(LegNotes, TEXT("; "))));
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(World))
		{
			Missions->CompleteObjective(LegObjective);
		}
		return true;
	}
	switch (Leg)
	{
	case ELeg::Plan:
	{
		if (!Kate->GetCharacterMovement()->IsMovingOnGround() || Kate->IsTraversing())
		{
			return false;
		}
		const AActor* On = BuildingUnder(World, Feet(Kate), Kate);
		if (GoalBuilding.IsValid() ? On == GoalBuilding.Get() : !On)
		{
			Leg = ELeg::Final;
			SubStart = Now;
			LastProgressSpot = Kate->GetActorLocation();
			LastProgressAt = Now;
			return false;
		}
		FString Note;
		if (Hops < 14 && On && !PlanHop(World, Kate, Note))
		{
			// Nothing in reach from this roof: down its fire escape as a player would, then up again from the street.
			if (BeginDescend(World, Kate))
			{
				LegNotes.Add(Note);
				Mark(FString::Printf(TEXT("%s: %s; down the %s"), *LegObjective.ToString(), *Note, *DescendRoute));
				return false;
			}
			FVector Street = FVector::ZeroVector;
			AActor* StreetActor = nullptr;
			bool bFound = false;
			for (float Radius = 500.f; Radius <= 3000.f && !bFound; Radius += 250.f)
			{
				for (int32 Angle = 0; Angle < 16 && !bFound; ++Angle)
				{
					const FVector Probe = Kate->GetActorLocation() + FRotator(0.f, 22.5f * Angle, 0.f).Vector() * Radius;
					bFound = GroundAt(World, Probe, Feet(Kate).Z - 100.f, -1000.f, Kate, Street, &StreetActor) && StreetActor
						&& !StreetActor->Tags.Contains(BuildingTag) && Street.Z < Feet(Kate).Z - 500.f;
				}
			}
			if (bFound)
			{
				if (LegObjective == TEXT("find_arrow"))
				{
					Test->AddError(FString::Printf(TEXT("Campaign: find_arrow: no fire escape down from %s"), *GetNameSafe(On)));
				}
				Force(FString::Printf(TEXT("%s: %s; no fire escape here, putting Kate on the street %.0f m below"), *LegObjective.ToString(),
					*Note, (Feet(Kate).Z - Street.Z) / 100.f), /*bLapOnly=*/true);
				StopMoving(PC);
				Kate->TeleportTo(Street + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Kate->GetActorRotation());
				return false;
			}
		}
		if (Hops >= 14 || !PlanHop(World, Kate, Note))
		{
			LegNotes.Add(Note);
			StopMoving(PC);
			if (LegObjective == TEXT("find_arrow"))
			{
				// The find_arrow roof must be reached by grapple (generate_city's facade anchor on its street wall).
				Test->AddError(FString::Printf(TEXT("Campaign: find_arrow roof not reached by grapple: %s"), *Note));
			}
			Force(FString::Printf(TEXT("%s: %s; putting Kate on the goal"), *LegObjective.ToString(), *Note));
			Kate->TeleportTo(Goal + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Kate->GetActorRotation());
			Leg = ELeg::Final;
			SubStart = Now;
			return false;
		}
		Mark(FString::Printf(TEXT("%s plan: %s"), *LegObjective.ToString(), *Note));
		Leg = ELeg::Walk;
		SubStart = Now;
		LastProgressSpot = Kate->GetActorLocation();
		LastProgressAt = Now;
		return false;
	}

	case ELeg::Walk:
	{
		if (!Path.IsValidIndex(PathIndex))
		{
			StopMoving(PC);
			Leg = ELeg::Aim;
			SubStart = Now;
			Presses = 0;
			PressAt = -1.0;
			return false;
		}
		const bool bLast = PathIndex == Path.Num() - 1;
		if (Steer(PC, Kate, Path[PathIndex], !bLast, bLast ? 60.f : 120.f))
		{
			++PathIndex;
		}
		if (FVector::Dist2D(Kate->GetActorLocation(), LastProgressSpot) > 100.f)
		{
			LastProgressSpot = Kate->GetActorLocation();
			LastProgressAt = Now;
		}
		else if (Now - LastProgressAt > 4.0)
		{
			LegNotes.Add(FString::Printf(TEXT("stuck walking to the stand spot at %s"), *Kate->GetActorLocation().ToCompactString()));
			Mark(TEXT("stuck on the walk; aiming from here"));
			StopMoving(PC);
			Leg = ELeg::Aim;
			SubStart = Now;
			Presses = 0;
			PressAt = -1.0;
		}
		return false;
	}

	case ELeg::Aim:
	{
		AGrappleAnchor* Anchor = PlanAnchor.Get();
		if (!Anchor)
		{
			Leg = ELeg::Plan;
			return false;
		}
		if (PressAt >= 0.0)
		{
			if (Grapple->IsArrowInFlight() || Grapple->IsZipping())
			{
				++Hops;
				Mark(FString::Printf(TEXT("grapple %d to %s on press %d"), Hops, *Anchor->GetName(), Presses));
				Leg = ELeg::Zip;
				SubStart = Now;
				return false;
			}
			if (Now - PressAt < 0.5)
			{
				return false;
			}
			PressAt = -1.0;
		}
		AimAt(PC, Kate, Anchor->GetMarkerLocation());
		Grapple->RefreshTarget();
		// The grapple's own pick is fine when it lands on the same roof: that is the one a player would take.
		if (AGrappleAnchor* Picked = Grapple->GetTargetAnchor(); Picked && Picked != Anchor && !Excluded.Contains(Picked)
			&& BuildingUnder(World, Picked->GetLandingLocation(), Kate) == BuildingUnder(World, Anchor->GetLandingLocation(), Kate)
			&& Grapple->IsZipClear(Kate->GetActorLocation(), Picked, /*bFromGround=*/true))
		{
			PlanAnchor = Picked;
			Anchor = Picked;
		}
		if (Grapple->GetTargetAnchor() == Anchor && Grapple->CanChain() && !Grapple->IsArrowInFlight() && Presses < 3 && Now - SubStart > 0.25)
		{
			++Presses;
			PressAt = Now;
			Tap(PC, GrapplePath);
			return false;
		}
		if (Now - SubStart > 2.0 || Presses >= 3)
		{
			LegNotes.Add(FString::Printf(TEXT("%s never targeted (%d press(es), target %s, blocked %s: %s)"), *Anchor->GetName(), Presses,
				*GetNameSafe(Grapple->GetTargetAnchor()), *GetNameSafe(Grapple->GetBlockedAnchor()), *Grapple->GetBlockedReason()));
			Excluded.Add(Anchor);
			Leg = ELeg::Plan;
		}
		return false;
	}

	case ELeg::Zip:
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight() || !Kate->GetCharacterMovement()->IsMovingOnGround() || Kate->IsTraversing())
		{
			if (Now - SubStart > 12.0)
			{
				LegNotes.Add(TEXT("zip or fall did not end in 12 s"));
				Leg = ELeg::Plan;
			}
			return false;
		}
		Mark(FString::Printf(TEXT("landed on %s"), *GetNameSafe(BuildingUnder(World, Feet(Kate), Kate))));
		Leg = ELeg::Plan;
		return false;

	case ELeg::Descend:
	{
		// As the EastVillage lap: crouch at the parapet over the top landing (a drop to hang), crouch to drop, and
		// at each landing catch its rail on the way down or stand on it and crouch at its rail, to the street.
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		const float FeetZ = Feet(Kate).Z;
		auto Fail = [&](const FString& Why)
		{
			StopMoving(PC);
			LegNotes.Add(TEXT("descent: ") + Why);
			Mark(FString::Printf(TEXT("%s: the descent stopped: %s"), *LegObjective.ToString(), *Why));
			Leg = ELeg::Plan;
			++Hops;
		};
		if (Now - DescendStart > 45.0)
		{
			Fail(FString::Printf(TEXT("over 45 s (phase %d at %s)"), DescendPhase, *Kate->GetActorLocation().ToCompactString()));
			return false;
		}
		if (DescendPhase == 1)
		{
			if (Steer(PC, Kate, DescendStand, false, 40.f) || Now - SubStart > 10.0)
			{
				StopMoving(PC);
				DescendPhase = 2;
				SubStart = Now;
				bDescendPushed = false;
			}
		}
		else if (DescendPhase == 2)
		{
			Steer(PC, Kate, FVector(DescendStand.X, DescendStand.Y, FeetZ) + DescendOut * 1000.f, false, 1.f);
			if (Parkour->IsBusy())
			{
				StopMoving(PC);
				DescendRetries = 0;
				DescendPhase = 3;
				SubStart = Now;
			}
			else if (Now - SubStart > 0.25 && !bDescendPushed)
			{
				bDescendPushed = true;
				Tap(PC, CrouchPath);
			}
			else if (Now - SubStart > 1.0)
			{
				bDescendPushed = false;
				SubStart = Now;
				if (++DescendRetries >= 3)
				{
					Fail(FString::Printf(TEXT("crouch at the edge found no drop to hang three times at %s"), *Kate->GetActorLocation().ToCompactString()));
				}
			}
		}
		else if (DescendPhase == 3)
		{
			if (Parkour->IsHanging())
			{
				if (Now - SubStart > 0.3)
				{
					++DescendHangs;
					Tap(PC, CrouchPath);
					DescendPhase = 4;
					SubStart = Now;
				}
			}
			else if (!Parkour->IsBusy() && Now - SubStart > 2.0)
			{
				Fail(FString::Printf(TEXT("the move over the edge did not end in a hang (at %s)"), *Kate->GetActorLocation().ToCompactString()));
			}
		}
		else if (DescendPhase == 4 && Now - SubStart > 0.15)
		{
			if (Parkour->IsBusy())
			{
				if (Parkour->IsHanging() || Parkour->GetActiveMove() == EHawkeyeParkourMove::LedgeGrab)
				{
					++DescendCatches;
					DescendPhase = 3;
					SubStart = Now;
				}
			}
			else if (Kate->GetCharacterMovement()->IsMovingOnGround() && Now - SubStart > 0.2)
			{
				if (FeetZ - StreetZ > 200.f)
				{
					++DescendStops;
					DescendStand = FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, FeetZ);
					DescendRetries = 0;
					bDescendPushed = false;
					DescendPhase = 2;
					SubStart = Now;
				}
				else
				{
					StopMoving(PC);
					const FString Done = FString::Printf(TEXT("down the %s: %d hang(s), %d catch(es), %d stop(s) on a landing, %.1f s"),
						*DescendRoute, DescendHangs, DescendCatches, DescendStops, Now - DescendStart);
					LegNotes.Add(Done);
					Mark(FString::Printf(TEXT("%s: on the street, %s"), *LegObjective.ToString(), *Done));
					VisitedRoofs.Reset();
					Excluded.Reset();
					Leg = ELeg::Plan;
				}
			}
			else if (Now - SubStart > 5.0)
			{
				Fail(FString::Printf(TEXT("still in the air 5 s after a drop (at %s)"), *Kate->GetActorLocation().ToCompactString()));
			}
		}
		return false;
	}

	case ELeg::Final:
	{
		// Along the roof (or the street) to the goal; a trigger completes itself, the arrow needs examining.
		const bool bArrow = LegObjective == TEXT("find_arrow");
		if (bArrow)
		{
			AChapterEndInteractable* Arrow = FindChapterEnd(World);
			UInteractionComponent* Interaction = Kate->GetInteractionComponent();
			if (Arrow && Interaction && IInteractable::Execute_CanInteract(Arrow, Kate))
			{
				Interaction->RefreshFocus();
				if (Interaction->GetFocusedActor() == Arrow || FVector::Dist2D(Kate->GetActorLocation(), Goal) < 120.f)
				{
					StopMoving(PC);
					if (Interaction->GetFocusedActor() != Arrow)
					{
						AimAt(PC, Kate, Arrow->GetActorLocation());
						Interaction->RefreshFocus();
					}
					if (Interaction->GetFocusedActor() == Arrow && Now - SubStart > 0.3)
					{
						Mark(TEXT("the arrow offers [Examine]; pressing E"));
						Tap(PC, InteractPath);
						// Its completion is what ends the leg; give it the frame.
						SubStart = Now + 1.0;
					}
					return false;
				}
			}
		}
		if (Steer(PC, Kate, Goal, false, bArrow ? 60.f : 80.f))
		{
			StopMoving(PC);
		}
		if (FVector::Dist2D(Kate->GetActorLocation(), LastProgressSpot) > 60.f)
		{
			LastProgressSpot = Kate->GetActorLocation();
			LastProgressAt = Now;
		}
		else if (Now - LastProgressAt > 6.0)
		{
			StopMoving(PC);
			if (bArrow)
			{
				AChapterEndInteractable* Arrow = FindChapterEnd(World);
				Force(FString::Printf(TEXT("stood %.0f cm from the arrow's zone for 6 s without [Examine]; examining directly"),
					FVector::Dist2D(Kate->GetActorLocation(), Goal)));
				if (Arrow)
				{
					IInteractable::Execute_Interact(Arrow, Kate);
				}
			}
			else
			{
				Force(FString::Printf(TEXT("%s: stuck %.0f cm from the trigger; completing it"), *LegObjective.ToString(),
					FVector::Dist2D(Kate->GetActorLocation(), Goal)));
				if (UMissionSubsystem* Missions = UMissionSubsystem::Get(World))
				{
					Missions->CompleteObjective(LegObjective);
				}
			}
			LastProgressAt = Now + 30.0;
		}
		return false;
	}
	}
	return false;
}

bool FHawkeyeCampaignLapRunner::TickFight(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	const double Now = FPlatformTime::Seconds();
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	if (!bFightSetUp)
	{
		bFightSetUp = true;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(FightTag))
			{
				Pair.Add(*It);
			}
		}
		BolaBefore = Inventory->GetArrowCount(SlotBola);
		LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
		Mark(FString::Printf(TEXT("fight: %d of the %s, %d bola(s), health %.0f"), Pair.Num(), *FightTag.ToString(), BolaBefore, LastHealth));
		bBolaLoosed = FightTag != RoofPairTag;
	}
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++HitsTaken;
	}
	LastHealth = Health;
	bool bAllDown = Pair.Num() > 0;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Pair)
	{
		bAllDown &= IsDown(Thug.Get());
	}
	if (FightTag == RoofPairTag ? IsObjectiveDone(World, TEXT("clear_roof")) : bAllDown)
	{
		StopMoving(PC);
		Hold(PC, FirePath, false);
		Hold(PC, MeleePath, false);
		bDrawing = false;
		bMeleeHeld = false;
		Mark(FString::Printf(TEXT("%s down (%.1f s): %d arrow(s), bola used %s, %d swing(s), %d parr(ies), %d finisher(s), %d hit(s) taken, health %.0f"),
			*FightTag.ToString(), InPhase() - FightStartedAt, ArrowsLoosed, Inventory->GetArrowCount(SlotBola) < BolaBefore ? TEXT("yes") : TEXT("no"), Swings, Parries, Finishers,
			HitsTaken, Health));
		return true;
	}
	if (Kate->IsDowned())
	{
		StopMoving(PC);
		return false;
	}
	if (InPhase() - FightStartedAt > 120.0)
	{
		StopMoving(PC);
		Force(FString::Printf(TEXT("the %s fight ran 120 s; downing them"), *FightTag.ToString()));
		for (const TWeakObjectPtr<AThugCharacter>& Thug : Pair)
		{
			if (!IsDown(Thug.Get()))
			{
				Thug->GetHealthComponent()->ApplyDamage(100000.f, Kate);
			}
		}
		return false;
	}
	AThugCharacter* Target = nullptr;
	float Distance = BIG_NUMBER;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Pair)
	{
		if (!IsDown(Thug.Get()))
		{
			const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
			if (D < Distance)
			{
				Distance = D;
				Target = Thug.Get();
			}
		}
	}
	if (!Target)
	{
		// Both down but the objective waits on a limp body: give it a moment.
		StopMoving(PC);
		return false;
	}
	const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	AimAt(PC, Kate, Chest);

	// The bola: slot 4, a full draw at whoever is nearest while there is room, then back to slot 1.
	if (!bBolaLoosed && BolaBefore > 0)
	{
		switch (BolaStage)
		{
		case 0:
			if (Distance > 600.f || InPhase() - FightStartedAt > 4.0)
			{
				StopMoving(PC);
				Tap(PC, Slot4Path);
				BolaStage = 1;
				NextActionAt = Now + 0.25;
			}
			else if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
			{
				// Too close for a draw: back off a few steps first, facing him.
				const FVector Away = (Kate->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
				const FVector2D Value = MoveTowards(Away, PC->GetControlRotation().Yaw);
				if (!bMoveInjecting)
				{
					Input->StartContinuousInputInjectionForAction(Action(MovePath), FInputActionValue(Value), {}, {});
					bMoveInjecting = true;
				}
				else
				{
					Input->UpdateValueOfContinuousInputInjectionForAction(Action(MovePath), FInputActionValue(Value));
				}
			}
			return false;
		case 1:
			// Feet down and free: a draw is refused mid-landing, mid-traversal or staggered.
			if (Now < NextActionAt || !Kate->GetCharacterMovement()->IsMovingOnGround() || Kate->IsTraversing()
				|| Kate->IsRecoveringFromLanding() || Kate->IsStaggered() || Kate->IsMeleeAttacking())
			{
				return false;
			}
			if (Inventory->GetActiveArrowSlot() != SlotBola)
			{
				Mark(TEXT("IA_Slot4 did not select the bola; selecting it on the inventory"));
				Inventory->SelectArrowSlot(SlotBola);
			}
			Hold(PC, FirePath, true);
			DrawStart = Now;
			BolaStage = 2;
			return false;
		case 2:
			if (Now - DrawStart > 0.25 && !Kate->IsDrawingBow())
			{
				// The press did not start a draw (a hit, a stagger): let go and press again.
				Hold(PC, FirePath, false);
				BolaStage = 1;
				NextActionAt = Now + 0.2;
				if (++BolaTries >= 6)
				{
					bBolaLoosed = true;
					Tap(PC, Slot1Path);
					Mark(TEXT("the bola never drew in six presses"));
				}
				return false;
			}
			if (Now - DrawStart < (Distance < 450.f ? 0.45 : 0.85))
			{
				return false;
			}
			Hold(PC, FirePath, false);
			++ArrowsLoosed;
			BolaStage = 3;
			NextActionAt = Now + 0.3;
			return false;
		default:
			if (Now < NextActionAt)
			{
				return false;
			}
			Tap(PC, Slot1Path);
			++BolaTries;
			bBolaLoosed = Inventory->GetArrowCount(SlotBola) < BolaBefore || BolaTries >= 3;
			BolaStage = bBolaLoosed ? BolaStage : 0;
			Mark(FString::Printf(TEXT("bola try %d at %s, %.0f cm; %d left%s"), BolaTries, *Target->GetName(), Distance,
				Inventory->GetArrowCount(SlotBola), bBolaLoosed ? TEXT("") : TEXT(" (not loosed: again)")));
			NextActionAt = Now + 0.25;
			return false;
		}
	}

	const bool bBusy = Kate->IsMeleeAttacking() || Kate->IsDodging() || Kate->IsStaggered() || Kate->IsLockedOutByTakedown();
	if (bMeleeHeld)
	{
		if (Now - MeleeHeldSince >= 0.45)
		{
			Hold(PC, MeleePath, false);
			bMeleeHeld = false;
			NextActionAt = Now + 0.1;
		}
		return false;
	}
	if (bDrawing)
	{
		if (Now - DrawStart >= 0.82 || Distance < 260.f)
		{
			Hold(PC, FirePath, false);
			bDrawing = false;
			++ArrowsLoosed;
			NextActionAt = Now + 0.15;
		}
		return false;
	}
	// A wind-up in front of her: tap melee to parry it.
	const FVector Forward = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
	if (Now - LastParryTap > 0.3 && !bBusy && !Kate->IsDrawingBow())
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const EHawkeyeParryKind Kind = UHawkeyeMeleeRules::ClassifyParry(*It);
			const UMeleeComponent* Melee = It->GetMeleeComponent();
			if (Kind != EHawkeyeParryKind::None && Melee
				&& UHawkeyeMeleeRules::IsInFrontWithin(Kate->GetActorLocation(), Forward, It->GetActorLocation(), 240.f, 65.f)
				&& Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining() >= 0.2f)
			{
				StopMoving(PC);
				Tap(PC, MeleePath);
				LastParryTap = Now;
				++Parries;
				NextActionAt = Now + 0.25;
				return false;
			}
		}
	}
	if (Now < NextActionAt || bBusy)
	{
		return false;
	}
	if (Kate->GetFinisherComponent() && Kate->GetFinisherComponent()->FindTarget())
	{
		StopMoving(PC);
		Tap(PC, TakedownPath);
		++Finishers;
		NextActionAt = Now + 0.3;
		return false;
	}
	if (Distance <= 190.f)
	{
		StopMoving(PC);
		const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
		if ((ThugMelee && ThugMelee->IsWindingUp()) || Target->IsKnockedDown() || Target->IsStaggered())
		{
			Tap(PC, MeleePath);
			NextActionAt = Now + 0.32;
		}
		else
		{
			Hold(PC, MeleePath, true);
			bMeleeHeld = true;
			MeleeHeldSince = Now;
		}
		++Swings;
		return false;
	}
	if (Distance <= 450.f || Target->GetAlertState() != EThugAlertState::Alerted)
	{
		// Close in (an unalerted thug has to be walked up to; he will turn).
		const FVector To = (Target->GetActorLocation() - Kate->GetActorLocation()).GetSafeNormal2D();
		if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
		{
			const FVector2D Value = MoveTowards(To, PC->GetControlRotation().Yaw);
			if (!bMoveInjecting)
			{
				Input->StartContinuousInputInjectionForAction(Action(MovePath), FInputActionValue(Value), {}, {});
				bMoveInjecting = true;
			}
			else
			{
				Input->UpdateValueOfContinuousInputInjectionForAction(Action(MovePath), FInputActionValue(Value));
			}
		}
		return false;
	}
	StopMoving(PC);
	Hold(PC, FirePath, true);
	bDrawing = true;
	DrawStart = Now;
	return false;
}

void FHawkeyeCampaignLapRunner::Crouch(APlayerController* PC, AHawkeyeCharacter* Kate, bool bDown)
{
	const double Now = FPlatformTime::Seconds();
	if (Kate->bIsCrouched == bDown || Now - LastCrouchTap < 0.25)
	{
		return;
	}
	LastCrouchTap = Now;
	HawkeyeCampaignLap::Tap(PC, HawkeyeCampaignLap::CrouchPath);
}

bool FHawkeyeCampaignLapRunner::FindCover(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	++CoverSearches;
	const FVector Here = Feet(Kate);
	AActor* Roof = BuildingUnder(World, Here, Kate);
	const float Crouched = Kate->GetCharacterMovement()->GetCrouchedHalfHeight();
	const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	auto Sees = [World, Kate](const AThugCharacter* Archer, const FVector& Point)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignDuelSight), false, Archer);
		Params.AddIgnoredActor(Kate);
		return !World->LineTraceTestByChannel(Archer->GetActorLocation() + FVector(0.f, 0.f, 60.f), Point, ECC_Visibility, Params);
	};
	float Best = BIG_NUMBER;
	int32 Spots = 0;
	for (float X = -1500.f; X <= 1500.f; X += 50.f)
	{
		for (float Y = -1500.f; Y <= 1500.f; Y += 50.f)
		{
			FVector Ground;
			AActor* Under = nullptr;
			if (!GroundAt(World, Here + FVector(X, Y, 0.f), Here.Z + 300.f, Here.Z - 300.f, Kate, Ground, &Under) || Under != Roof
				|| FMath::Abs(Ground.Z - Here.Z) > 40.f)
			{
				continue;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignDuelRoom), false, Kate);
			if (World->OverlapBlockingTestByChannel(Ground + FVector(0.f, 0.f, HalfHeight + 5.f), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Radius + 5.f, HalfHeight), Params))
			{
				continue;
			}
			bool bHidden = true;
			int32 Seen = 0;
			int32 Living = 0;
			for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
			{
				if (IsDown(Archer.Get()))
				{
					continue;
				}
				++Living;
				const FVector Low = Ground + FVector(0.f, 0.f, Crouched);
				bHidden &= !Sees(Archer.Get(), Low) && !Sees(Archer.Get(), Low + FVector(0.f, 0.f, 15.f));
				FCollisionQueryParams ArrowParams(SCENE_QUERY_STAT(CampaignDuelLine), false, Kate);
				ArrowParams.AddIgnoredActor(Archer.Get());
				Seen += World->LineTraceTestByChannel(Ground + FVector(0.f, 0.f, 120.f), Archer->GetActorLocation() + FVector(0.f, 0.f, 55.f),
					ECC_Visibility, ArrowParams) ? 0 : 1;
			}
			if (!bHidden || Seen == 0)
			{
				continue;
			}
			++Spots;
			const float Score = FVector::Dist2D(Ground, Here) + (Seen == Living ? 0.f : 600.f);
			if (Score < Best)
			{
				Best = Score;
				Cover = Ground;
			}
		}
	}
	Mark(FString::Printf(TEXT("duel: %d cover spot(s) on %s; the nearest %.0f cm away"), Spots, *GetNameSafe(Roof),
		Spots ? FVector::Dist2D(Cover, Here) : -1.f));
	return Spots > 0;
}

bool FHawkeyeCampaignLapRunner::TickDuel(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCampaignLap;
	const double Now = FPlatformTime::Seconds();
	auto DrawOf = [](const AThugCharacter* Archer)
	{
		const UBowComponent* Bow = Archer ? Archer->GetBowComponent() : nullptr;
		return Bow && Bow->IsDrawing() ? Bow->GetDrawElapsed() : -1.f;
	};
	if (Duel == EDuel::SetUp)
	{
		Pair.Reset();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(ArcherPairTag) && !IsDown(*It))
			{
				Pair.Add(*It);
			}
		}
		DuelWorld = World;
		DuelStart = Now;
		LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
		Kate->GetInventoryComponent()->SelectArrowSlot(SlotStandard);
		StopMoving(PC);
		Mark(FString::Printf(TEXT("duel with the ArcherPair (%d up), health %.0f"), Pair.Num(), LastHealth));
		if (!FindCover(World, Kate))
		{
			Force(TEXT("no cover from both archers on the arrow's roof; downing them"));
			for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
			{
				if (!IsDown(Archer.Get()))
				{
					Archer->GetHealthComponent()->ApplyDamage(100000.f, Kate);
				}
			}
			return true;
		}
		Duel = EDuel::Moving;
		DuelSince = Now;
		return false;
	}
	if (World != DuelWorld.Get())
	{
		// She went down and the last save loaded: the duel starts again from wherever that put her.
		++DuelLosses;
		StopMoving(PC);
		Mark(FString::Printf(TEXT("duel lost (%d); the save reloaded"), DuelLosses));
		bDrawing = false;
		Duel = EDuel::SetUp;
		return true;
	}
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++DuelHits;
	}
	LastHealth = Health;
	int32 Living = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
	{
		Living += IsDown(Archer.Get()) ? 0 : 1;
	}
	if (Living == 0)
	{
		Hold(PC, FirePath, false);
		Crouch(PC, Kate, false);
		Mark(FString::Printf(TEXT("ArcherPair down (%.1f s, %d arrow(s), %d hit(s) taken, %d loss(es)), health %.0f"), Now - DuelStart,
			DuelArrows, DuelHits, DuelLosses, Health));
		Duel = EDuel::SetUp;
		return true;
	}
	if (Now - DuelStart > 120.0 || DuelLosses >= 2)
	{
		Hold(PC, FirePath, false);
		Force(FString::Printf(TEXT("the archer duel ran %.0f s with %d loss(es); downing them"), Now - DuelStart, DuelLosses));
		for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
		{
			if (!IsDown(Archer.Get()))
			{
				Archer->GetHealthComponent()->ApplyDamage(100000.f, Kate);
			}
		}
		return false;
	}
	if (Kate->IsDowned())
	{
		Hold(PC, FirePath, false);
		StopMoving(PC);
		return false;
	}
	// The worst draw of an archer who can see her; a draw held after losing her is waited out by the clock.
	float Danger = -1.f;
	bool bAnyHold = false;
	for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
	{
		if (IsDown(Archer.Get()))
		{
			continue;
		}
		const AThugAIController* Brain = Cast<AThugAIController>(Archer->GetController());
		if (Brain && Brain->IsHoldingDraw())
		{
			bAnyHold = true;
		}
		else
		{
			Danger = FMath::Max(Danger, DrawOf(Archer.Get()));
		}
	}
	switch (Duel)
	{
	case EDuel::Moving:
		Crouch(PC, Kate, false);
		if (Steer(PC, Kate, Cover, false, 40.f) || Now - DuelSince > 8.0)
		{
			StopMoving(PC);
			Crouch(PC, Kate, true);
			Duel = EDuel::Hidden;
			DuelSince = Now;
		}
		return false;
	case EDuel::Hidden:
		Crouch(PC, Kate, true);
		if (Now - DuelSince > 0.5 && Kate->bIsCrouched && Danger < 0.25f && (!bAnyHold || Now - DuelSince >= 1.5))
		{
			Crouch(PC, Kate, false);
			Duel = EDuel::Rising;
			DuelSince = Now;
		}
		else if (Now - DuelSince > 6.0 && CoverSearches < 6)
		{
			// Waited out a long hold, or an archer moved: look for cover again.
			if (FindCover(World, Kate))
			{
				Duel = EDuel::Moving;
			}
			DuelSince = Now;
		}
		return false;
	case EDuel::Rising:
	{
		if (Now - DuelSince < 0.25)
		{
			return false;
		}
		// The weakest she can see from her bow hand: his chest if it shows over his parapet, else his head.
		const FVector Hand = Feet(Kate) + FVector(0.f, 0.f, 120.f);
		AThugCharacter* Pick = nullptr;
		for (const TWeakObjectPtr<AThugCharacter>& Archer : Pair)
		{
			if (IsDown(Archer.Get()))
			{
				continue;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampaignDuelShot), false, Kate);
			Params.AddIgnoredActor(Archer.Get());
			for (const float Try : { 30.f, 55.f, 70.f })
			{
				if (!World->LineTraceTestByChannel(Hand, Archer->GetActorLocation() + FVector(0.f, 0.f, Try), ECC_Visibility, Params))
				{
					if (!Pick || Archer->GetHealthComponent()->GetCurrentHealth() < Pick->GetHealthComponent()->GetCurrentHealth())
					{
						Pick = Archer.Get();
						DuelAimHeight = Try;
					}
					break;
				}
			}
		}
		if (!Pick)
		{
			Crouch(PC, Kate, true);
			Duel = EDuel::Hidden;
			DuelSince = Now - 4.0;
			return false;
		}
		DuelTarget = Pick;
		AimAt(PC, Kate, Pick->GetActorLocation() + FVector(0.f, 0.f, 55.f));
		Hold(PC, FirePath, true);
		DrawStart = Now;
		Duel = EDuel::Drawing;
		return false;
	}
	case EDuel::Drawing:
	{
		AThugCharacter* Target = DuelTarget.Get();
		UBowComponent* Bow = Kate->GetBowComponent();
		if (Target && !IsDown(Target) && Bow)
		{
			// Where the arrow meets him, dropping over the flight; the reticle override is the ArcherDuel lap's own
			// (the camera sits behind and above her, so its trace would meet her parapet first).
			const FVector Aim = AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(),
				Target->GetActorLocation() + FVector(0.f, 0.f, DuelAimHeight), Target->GetVelocity(), 6000.f, World->GetGravityZ());
			Bow->SetAimOverride(Aim);
			AimAt(PC, Kate, Aim);
		}
		const bool bFull = Now - DrawStart >= 0.85;
		const bool bThreat = Danger >= 0.9f && Now - DrawStart < 0.82;
		if (bFull || bThreat || !Target || IsDown(Target))
		{
			Hold(PC, FirePath, false);
			if (Bow)
			{
				Bow->ClearAimOverride();
			}
			DuelArrows += bFull ? 1 : 0;
			Crouch(PC, Kate, true);
			Duel = EDuel::Hidden;
			DuelSince = Now;
		}
		return false;
	}
	default:
		return false;
	}
}


FString FHawkeyeCampaignLapRunner::CurrentBeat(UWorld* World) const
{
	using namespace HawkeyeCampaignLap;
	if (IsSceneWorld(World))
	{
		return TEXT("Scene");
	}
	const AHawkeyePlayerController* PC = FindController(World);
	if (!PC)
	{
		return FString();
	}
	if (PC->IsCloseUpActive())
	{
		return TEXT("CloseUp");
	}
	const UMissionFlowController* Flow = PC->GetMissionFlow();
	if (Flow && Flow->GetStep() == EMissionFlowStep::EndCard)
	{
		return TEXT("EndCard");
	}
	if (PC->IsFlashbackActive())
	{
		return TEXT("Flashback");
	}
	if (Flow && Flow->GetStep() == EMissionFlowStep::PlayableScene)
	{
		return TEXT("ToScene");
	}
	return FString();
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
	AHawkeyePlayerController::bAutomationPlaysAsPlayer = false;
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
	const FString Json = FString::Printf(TEXT("{\n  \"test\": \"Hawkeye.Lap.Campaign\",\n  \"seconds\": %.1f,\n  \"check_failures\": %d,\n"
		"  \"timeline\": [\n%s\n  ],\n  \"forced\": [\n%s\n  ],\n  \"findings\": [\n%s\n  ],\n  \"hawkeye_warnings\": [\n%s\n  ],\n"
		"  \"checks\": [\n%s\n  ],\n  \"shots\": [\n%s\n  ]\n}\n"),
		Clock(), CheckFailures, *Quote(Timeline), *Quote(Forced), *Quote(Findings), *Quote(Warnings), *Quote(Checks), *Quote(Shots));
	WriteText(TEXT("campaign_lap.json"), Json);
	Test->AddInfo(TEXT("campaign_lap.json:\n") + Json);
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
			Mark(TEXT("main menu up over the district"));
			Expect(TEXT("no save in the lap's slot"), Save && !Save->HasSave() && Save->GetSlotName() == Slot);
			Expect(TEXT("Continue greyed with no save"), Menu && !Menu->IsContinueEnabled());
			Expect(TEXT("the world is paused under the menu"), UGameplayStatics::IsGamePaused(World));
			Expect(TEXT("the cursor is up for the menu"), PC->bShowMouseCursor);
			Expect(TEXT("no HUD under the menu"), !PC->GetHawkeyeHud() || !PC->GetHawkeyeHud()->IsVisible());
			Expect(TEXT("no title card before New Game"), !PC->IsChapterTitleShowing());
			Shot(TEXT("campaign_menu.png"));
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
		if (InStep() < 0.3)
		{
			return false;
		}
		{
			const UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World);
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			Expect(TEXT("Normal is the difficulty"), Settings && Settings->GetDifficulty() == EHawkeyeDifficulty::Normal);
			Expect(TEXT("the menu and the prompt are gone"), !PC->IsMainMenuOpen() && !PC->IsDifficultyPromptOpen());
			Expect(TEXT("New Game wrote the first save"), Save && Save->HasSave());
			Expect(TEXT("the title card is up"), PC->IsChapterTitleShowing());
			Expect(TEXT("the current objective is reach_roof"), CurrentObjectiveId(World) == TEXT("reach_roof"));
			Mark(TEXT("playing: title card up"));
			Enter(EPhase::Title);
		}
		return false;

	case EPhase::Title:
		if (Step == 0 && InPhase() >= 1.4)
		{
			Shot(TEXT("campaign_title.png"));
			CheckClean(TEXT("under the title card"), { UChapterTitleWidget::StaticClass() });
			const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
			const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
			Expect(TEXT("the first objective's toast waits under the title card"),
				Toasts && Toasts->AreToastsHeld() && Toasts->IsToastQueued() && !Toasts->IsToastVisible());
			NextStep();
			return false;
		}
		if (PC->IsChapterTitleShowing())
		{
			if (InPhase() > 10.0)
			{
				Expect(TEXT("the title card goes by itself inside 10 s"), false);
				Enter(EPhase::PhoneWait);
			}
			return false;
		}
		if (Step <= 1)
		{
			Mark(FString::Printf(TEXT("title card gone after %.1f s"), InPhase()));
			NextStep();
			return false;
		}
		// The HUD releases the held toast on its next tick; read it a moment later, while it is up.
		if (InStep() < 0.4)
		{
			return false;
		}
		{
			const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
			const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
			const bool bToastAfter = Toasts && Toasts->IsToastVisible();
			Expect(TEXT("the held toast plays once the card is gone"), bToastAfter);
			if (bToastAfter)
			{
				Mark(FString::Printf(TEXT("toast after the card: %s %s"), *Toasts->GetToastHeading().ToString(),
					*Toasts->GetToastTitle().ToString()));
				Shot(TEXT("campaign_title_toast.png"));
			}
		}
		Enter(EPhase::PhoneWait);
		return false;

	case EPhase::PhoneWait:
	{
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
		const int32 Arrived = Phone && Phone->GetInbox() ? Phone->GetInbox()->GetArrivedCount() : 0;
		if (Arrived == 0)
		{
			if (InPhase() > 20.0)
			{
				Expect(TEXT("the first text arrives within 20 s of the title"), false);
				Enter(EPhase::ReachRoof);
				BeginLeg(World, TEXT("reach_roof"));
			}
			return false;
		}
		if (Step == 0)
		{
			const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
			Mark(FString::Printf(TEXT("first text arrived (%d unread, badge %d)"), Phone->GetUnreadCount(), Hud ? Hud->GetPhoneBadgeCount() : -1));
			NextStep();
			return false;
		}
		if (InStep() < 1.5)
		{
			return false;
		}
		CheckClean(TEXT("the text arrived"));
		Mark(TEXT("P: opening the phone"));
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
			Mark(TEXT("P did nothing; TogglePhone"));
			PC->TogglePhone();
			PhaseStart = FPlatformTime::Seconds();
			return false;
		}
		{
			const UPhoneWidget* Phone = PC->GetPhoneWidget();
			Expect(TEXT("the phone shows a thread"), Phone && Phone->GetThreadLength() > 0);
			Expect(TEXT("the game is paused under the phone"), UGameplayStatics::IsGamePaused(World));
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
		{
			const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
			Expect(TEXT("the text is read"), Phone && Phone->GetUnreadCount() == 0);
			CheckClean(TEXT("phone closed"));
			Enter(EPhase::ReachRoof);
			BeginLeg(World, TEXT("reach_roof"));
		}
		return false;

	case EPhase::ReachRoof:
	case EPhase::CrossBlock:
		if (!Kate)
		{
			return false;
		}
		if (TickLeg(World, PC, Kate))
		{
			if (Phase == EPhase::ReachRoof)
			{
				Enter(EPhase::CrossBlock);
				BeginLeg(World, TEXT("cross_block"));
			}
			else
			{
				Enter(EPhase::ClearRoof);
			}
		}
		return false;

	case EPhase::ClearRoof:
		if (Kate && TickFight(World, PC, Kate))
		{
			Enter(EPhase::Calm);
		}
		return false;

	case EPhase::ArcherFight:
		if (Kate && TickDuel(World, PC, Kate))
		{
			// Back to the arrow from wherever the fight left her.
			Enter(EPhase::FindArrow);
			const double Started = LegStart;
			BeginLeg(World, TEXT("find_arrow"));
			LegStart = Started;
		}
		return false;

	case EPhase::Calm:
		// The fight over and her health back: then the quit.
		if (InPhase() < 3.0 || (Kate && Kate->GetHealthComponent()->GetHealthPercent() < 0.999f && InPhase() < 20.0))
		{
			return false;
		}
		CheckClean(TEXT("after the roof fight"));
		Mark(TEXT("Esc: pause"));
		PressKey(PC, EKeys::Escape);
		Enter(EPhase::QuitMenu);
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
			Mark(TEXT("Quit to menu"));
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
		if (!WorldSettled(World, DistrictWorld.Get(), 1.f))
		{
			if (InPhase() > 60.0)
			{
				Expect(TEXT("Continue loads the district inside 60 s"), false);
				Finish(World);
				return true;
			}
			return false;
		}
		Mark(FString::Printf(TEXT("Continue: district back after %.1f s"), InPhase()));
		Enter(EPhase::AfterContinue);
		return false;

	case EPhase::AfterContinue:
		if (InPhase() < 2.0)
		{
			return false;
		}
		{
			const HawkeyeCampaignLap::FSnapshot After = TakeSnapshot(World);
			const TArray<FString> Diff = Compare(BeforeQuit, After);
			Mark(FString::Printf(TEXT("after Continue: objective %s, %d difference(s)%s"), *After.Objective.ToString(), Diff.Num(),
				Diff.Num() ? *(TEXT(": ") + FString::Join(Diff, TEXT("; "))) : TEXT("")));
			for (const FString& Line : Diff)
			{
				Expect(TEXT("Continue brings back what was there at the quit: ") + Line, false);
			}
			Expect(TEXT("no title card after Continue"), !PC->IsChapterTitleShowing());
			CheckClean(TEXT("after Continue"), {}, true, true);
			Enter(EPhase::FindArrow);
			BeginLeg(World, TEXT("find_arrow"));
		}
		return false;

	case EPhase::FindArrow:
		if (!Kate)
		{
			return false;
		}
		// The ArcherPair guards the arrow: a player within 35 m of either (or seen by one) takes them on first.
		if (Leg == ELeg::Final && Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			for (TActorIterator<AThugCharacter> It(World); It; ++It)
			{
				if (It->ActorHasTag(ArcherPairTag) && !IsDown(*It) && (It->IsAlerted()
					|| FVector::Dist(It->GetActorLocation(), Kate->GetActorLocation()) < 3500.f))
				{
					StopMoving(PC);
					Mark(FString::Printf(TEXT("the ArcherPair: %s %s, %.0f m; fighting them"), *It->GetName(),
						It->IsAlerted() ? TEXT("alerted") : TEXT("calm"), FVector::Dist(It->GetActorLocation(), Kate->GetActorLocation()) / 100.f));
					FightTag = ArcherPairTag;
					FightStartedAt = 0.0;
					bFightSetUp = false;
					Pair.Reset();
					Duel = EDuel::SetUp;
					Enter(EPhase::ArcherFight);
					return false;
				}
			}
		}
		if (TickLeg(World, PC, Kate))
		{
			DistrictWorld = World;
			Beats.Reset();
			NoteBeat(TEXT("Interact"));
			Enter(EPhase::ChapterEnd);
		}
		return false;

	case EPhase::ChapterEnd:
	{
		NoteBeat(CurrentBeat(World));
		const FString Beat = CurrentBeat(World);
		if (Beat == TEXT("EndCard"))
		{
			if (EndCardSeenAt < 0.0)
			{
				EndCardSeenAt = FPlatformTime::Seconds();
			}
			if (!bEndCardShot && FPlatformTime::Seconds() - EndCardSeenAt > 1.2)
			{
				bEndCardShot = true;
				Shot(TEXT("campaign_endcard.png"));
			}
		}
		for (TObjectIterator<UFlashbackWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsPlaying() && It->GetSequencer())
			{
				const int32 Slide = It->GetSequencer()->GetCurrentSlideIndex();
				if (Slide != LastSlide)
				{
					LastSlide = Slide;
					SlideSeenAt = FPlatformTime::Seconds();
					SlideTimes.Add(FString::Printf(TEXT("slide %d at %.1f s"), Slide, Clock()));
					Mark(FString::Printf(TEXT("flashback slide %d"), Slide));
				}
				if (!bSlideShot && Slide == 1 && FPlatformTime::Seconds() - SlideSeenAt > 0.8)
				{
					bSlideShot = true;
					Shot(TEXT("campaign_flashback.png"));
				}
			}
		}
		if (IsSceneWorld(World) && Kate)
		{
			Enter(EPhase::Scene);
			return false;
		}
		if (InPhase() > 90.0)
		{
			Expect(TEXT("the chapter end reaches the playable scene inside 90 s"), false);
			Finish(World);
			return true;
		}
		return false;
	}

	case EPhase::Scene:
		if (!WorldSettled(World, DistrictWorld.Get(), 1.f) || InPhase() < 1.5)
		{
			if (InPhase() > 30.0)
			{
				Expect(TEXT("the scene settles inside 30 s"), false);
				Enter(EPhase::SceneWalk);
			}
			return false;
		}
		if (Step == 0)
		{
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
			Expect(TEXT("in the scene, saves are refused"), Save && Save->IsInPlayableScene());
			Expect(TEXT("the scene runs its own mission"), Missions && Missions->GetCurrentMission()
				&& Missions->GetCurrentMission()->GetName() == TEXT("DA_Scene_Placeholder"));
			Mark(FString::Printf(TEXT("in the scene (beats %s; slides: %s)"), *FString::Join(Beats, TEXT(" -> ")), *FString::Join(SlideTimes, TEXT(", "))));
			CheckClean(TEXT("in the scene room"));
			PC->SetControlRotation(FRotator(-6.f, Kate->GetActorRotation().Yaw, 0.f));
			NextStep();
			return false;
		}
		if (InStep() < 1.0)
		{
			return false;
		}
		Shot(TEXT("campaign_scene.png"));
		Enter(EPhase::SceneWalk);
		return false;

	case EPhase::SceneWalk:
	{
		if (InPhase() < 0.5)
		{
			return false;
		}
		if (!IsSceneWorld(World))
		{
			StopMoving(PC);
			Mark(TEXT("left the scene"));
			Enter(EPhase::Return);
			return false;
		}
		TActorIterator<AObjectiveTriggerVolume> Volume(World);
		if (!Kate || !Volume)
		{
			return false;
		}
		Steer(PC, Kate, Volume->GetActorLocation(), false, 20.f);
		if (InPhase() > 20.0 && Step == 0)
		{
			NextStep();
			StopMoving(PC);
			Force(TEXT("did not walk into the scene's trigger in 20 s; putting Kate in it"));
			Kate->TeleportTo(Volume->GetActorLocation(), Kate->GetActorRotation());
		}
		return false;
	}

	case EPhase::Return:
		if (!WorldSettled(World, DistrictWorld.Get(), 0.5f) || IsSceneWorld(World))
		{
			if (InPhase() > 60.0)
			{
				Expect(TEXT("the district comes back from the scene inside 60 s"), false);
				Finish(World);
				return true;
			}
			return false;
		}
		if (Step == 0)
		{
			const UHawkeyeObjectiveWidget* Toasts = PC->GetHawkeyeHud() ? PC->GetHawkeyeHud()->GetObjectiveMarker() : nullptr;
			const bool bToast = Toasts && Toasts->IsToastVisible() && Toasts->GetToastHeading().ToString() == TEXT("[Chapter complete]");
			if (bToast)
			{
				NoteBeat(TEXT("Toast"));
			}
			Expect(TEXT("the chapter end runs Interact, close-up, end card, flashback, scene, toast"),
				FString::Join(Beats, TEXT(" -> ")) == TEXT("Interact -> CloseUp -> EndCard -> Flashback -> ToScene -> Scene -> Toast"));
			Expect(TEXT("every objective is complete in the district"), CurrentObjectiveId(World).IsNone());
			Mark(FString::Printf(TEXT("back on the street (%s)"), *FString::Join(Beats, TEXT(" -> "))));
			NextStep();
			return false;
		}
		if (Step == 1)
		{
			if (InStep() < 1.0)
			{
				return false;
			}
			Shot(TEXT("campaign_street.png"));
			NextStep();
			return false;
		}
		if (InStep() < 4.0)
		{
			return false;
		}
		CheckClean(TEXT("back from the scene"), {}, true, false);
		Enter(EPhase::Systems);
		return false;

	case EPhase::Systems:
		switch (Step)
		{
		case 0:
		{
			UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			Expect(TEXT("the scene is over (no scene flag)"), Save && !Save->IsInPlayableScene() && !Save->GetSceneReturn().bInScene);
			Expect(TEXT("a save works after the return"), Save && Save->SaveCampaign(TEXT("campaign lap: after the return")));
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
			Expect(TEXT("the map opens after the return"), PC->IsMapOpen());
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
			Expect(TEXT("a challenge starts after the return"), bStarted && Challenges->IsRunning());
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
			UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
			if (Challenges)
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
				Expect(TEXT("fast travel starts after the return"), PC->FastTravelTo(FirstSafehouse));
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
		// Walk in and stand there; when down, a key gives up (with Clint coming, wait him out and go down again).
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
			Mark(FString::Printf(TEXT("down (%s)"), Kate->IsReviveExpected() ? TEXT("Clint is coming") : TEXT("no revive coming")));
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
			// Revived by Clint before the key: back to the crime to go down again.
			if (Kate && World == DistrictWorld.Get() && !Kate->IsDowned() && Kate->GetHealthComponent()->IsAlive() && InPhase() > 3.0)
			{
				Mark(TEXT("revived; going down again"));
				Enter(EPhase::Crime);
				Step = 2;
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
			const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
			const ASafehouse* Door = Safehouses ? Safehouses->FindSafehouse(FirstSafehouse) : nullptr;
			int32 CrimeThugs = 0;
			for (TActorIterator<AThugCharacter> It(World); It; ++It)
			{
				CrimeThugs += It->ActorHasTag(UCrimeSubsystem::CrimeThugTag) ? 1 : 0;
			}
			Mark(FString::Printf(TEXT("reloaded after %.1f s: health %.0f, %.0f m from [Safehouse 1], %d crime thug(s), crime %s"), InPhase(),
				Kate->GetHealthComponent()->GetCurrentHealth(), Door ? FVector::Dist(Kate->GetActorLocation(), Door->GetActorLocation()) / 100.f : -1.f,
				CrimeThugs, Crimes && Crimes->IsCrimeActive() ? TEXT("on") : TEXT("off")));
			Expect(TEXT("no crime running after the reload"), Crimes && !Crimes->IsCrimeActive() && CrimeThugs == 0);
			Expect(TEXT("full health after the reload"), Kate->GetHealthComponent()->GetHealthPercent() > 0.99f);
			Expect(TEXT("the chapter is still complete after the reload"), CurrentObjectiveId(World).IsNone());
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
			Mark(TEXT("Esc > Quit to menu after the chapter end"));
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
			Expect(TEXT("the chapter-end arrow does not offer itself again"), !FindChapterEnd(World)
				|| !IInteractable::Execute_CanInteract(FindChapterEnd(World), Kate));
			Expect(TEXT("no title card, end card or flashback after the second Continue"), !PC->IsChapterTitleShowing()
				&& !PC->IsFlashbackActive() && !(PC->GetMissionFlow() && PC->GetMissionFlow()->IsRunning()));
			CheckClean(TEXT("after the second Continue"), {}, true, false);
			Mark(TEXT("done"));
			Finish(World);
			Enter(EPhase::Done);
		}
		return false;

	case EPhase::Done:
		return InPhase() > 1.0;
	}
	return false;
}

bool FHawkeyeLapCampaign::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCampaignLap;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the campaign lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	UWorld* World = FindWorld();
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World);
	if (!Save || !Settings)
	{
		AddError(TEXT("Campaign: no save or settings subsystem (run it in the standalone game)."));
		return true;
	}
	const TSharedRef<FHawkeyeCampaignLapRunner> Runner = MakeShared<FHawkeyeCampaignLapRunner>(this);
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
	AHawkeyePlayerController::bAutomationPlaysAsPlayer = true;
	FAutomationTestFramework::Get().EnqueueLatentCommand(Runner);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
