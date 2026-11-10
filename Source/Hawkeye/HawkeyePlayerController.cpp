#include "HawkeyePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Vfx/SnowfallComponent.h"
#include "Audio/HawkeyeVolumeSubsystem.h"
#include "HawkeyeGameMode.h"
#include "Combat/HealthComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Flashback/FlashbackDefinition.h"
#include "Flashback/FlashbackWidget.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionSubsystem.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeInventoryWidget.h"
#include "UI/HawkeyePauseWidget.h"
#include "UI/HawkeyeSettingsWidget.h"
#include "UI/MissionEndCardWidget.h"
#include "Dialogue/BanterComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UI/HawkeyeMainMenuWidget.h"
#include "UI/HawkeyeDifficultyPromptWidget.h"
#include "Settings/DifficultySubsystem.h"
#include "UI/FlashbackReplayWidget.h"
#include "Flashback/FlashbackDefinition.h"
#include "UI/ChallengeResultsWidget.h"
#include "UI/HawkeyeSafehouseWidget.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "World/ChapterEndInteractable.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Misc/CommandLine.h"
#include "Save/HawkeyeCampaignState.h"
#include "TimerManager.h"
#include "UI/ChapterTitleWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/PhoneWidget.h"
#include "UI/HawkeyeMapWidget.h"
#include "Playtest/PlaytestPhotoMode.h"
#include "Playtest/PlaytestSubsystem.h"

bool AHawkeyePlayerController::bAutomationPlaysAsPlayer = false;

AHawkeyePlayerController::AHawkeyePlayerController()
{
	Banter = CreateDefaultSubobject<UBanterComponent>(TEXT("Banter"));
	Snowfall = CreateDefaultSubobject<USnowfallComponent>(TEXT("Snowfall"));
	MusicSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/Audio/Classes/SCL_Music.SCL_Music")));
	MainMenuWidgetClass = UHawkeyeMainMenuWidget::StaticClass();
	DifficultyPromptWidgetClass = UHawkeyeDifficultyPromptWidget::StaticClass();
	FlashbackReplayWidgetClass = UFlashbackReplayWidget::StaticClass();
	SafehouseWidgetClass = UHawkeyeSafehouseWidget::StaticClass();
	ChallengeResultsWidgetClass = UChallengeResultsWidget::StaticClass();
	ChapterTitleWidgetClass = UChapterTitleWidget::StaticClass();
	PhoneWidgetClass = UPhoneWidget::StaticClass();
	MapWidgetClass = UHawkeyeMapWidget::StaticClass();
}

void AHawkeyePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (FinalCardPrompt.IsEmpty())
	{
		FinalCardPrompt = NSLOCTEXT("Hawkeye", "EndCardPressAnyKey", "Press any key");
	}

	MissionFlow = NewObject<UMissionFlowController>(this, TEXT("MissionFlow"));

	if (UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this))
	{
		MissionSubsystem->OnMissionComplete.AddDynamic(this, &AHawkeyePlayerController::HandleMissionComplete);
		MissionSubsystem->OnFlashbackRequested.AddDynamic(this, &AHawkeyePlayerController::HandleFlashbackRequested);
		MissionSubsystem->OnMissionStarted.AddDynamic(this, &AHawkeyePlayerController::HandleMissionStartedForOpening);
	}

	AddPauseMappingContext();
	CreateHud();

	if (IsLocalController())
	{
		if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
		{
			SettingsSubsystem->OnSettingsChanged.AddDynamic(this, &AHawkeyePlayerController::HandleSettingsChanged);
			ApplyVolumeSettings(SettingsSubsystem->GetSettings());
		}
	}

	// First boot: the district is already loaded behind the menu, so there is no separate map.
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this); Save && IsLocalController() && Save->ConsumeBootMenu())
	{
		ShowMainMenu();
		bMainMenuOverFreshBoot = true;
	}

	// The game mode may have started the chapter before this controller began play.
	if (const UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && Missions->GetCurrentMission())
	{
		HandleMissionStartedForOpening(Missions->GetCurrentMission());
	}
}

void AHawkeyePlayerController::AddPauseMappingContext()
{
	if (!PauseMappingContext || !IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(PauseMappingContext, PauseMappingPriority);
	}
}

void AHawkeyePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!PauseAction && !SwitchCharacterAction && !PartnerMarkAction && !PhoneAction && !MapAction)
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogHawkeye, Error,
			TEXT("%s: expected an EnhancedInputComponent; the pause key will not work."), *GetName());
		return;
	}

	if (PauseAction)
	{
		EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &AHawkeyePlayerController::Input_Pause);
	}
	// On the controller, like pause: switching moves between pawns, so neither pawn can own it.
	if (SwitchCharacterAction)
	{
		EnhancedInput->BindAction(SwitchCharacterAction, ETriggerEvent::Started, this,
			&AHawkeyePlayerController::Input_SwitchCharacter);
	}
	if (PartnerMarkAction)
	{
		EnhancedInput->BindAction(PartnerMarkAction, ETriggerEvent::Started, this, &AHawkeyePlayerController::Input_PartnerMark);
	}
	if (PhoneAction)
	{
		EnhancedInput->BindAction(PhoneAction, ETriggerEvent::Started, this, &AHawkeyePlayerController::Input_Phone);
	}
	if (MapAction)
	{
		EnhancedInput->BindAction(MapAction, ETriggerEvent::Started, this, &AHawkeyePlayerController::Input_Map);
	}
}

bool AHawkeyePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Every key reaches here regardless of what (if anything) it is bound to, so this is the one
	// place that reliably knows which device was touched last.
	bUsingGamepad = Params.Key.IsGamepadKey();

	// Photo mode takes every key and axis, so nothing reaches the frozen game under it.
	if (PhotoMode && PhotoMode->IsActive())
	{
		return PhotoMode->HandleKey(Params);
	}
	if (HandlePlaytestKey(Params))
	{
		return true;
	}

	if (Params.Event == IE_Pressed)
	{
		// Input is allowed under the title card; the first press after its lockout also fades it.
		if (ChapterTitleWidget && ChapterTitleWidget->IsPlaying())
		{
			ChapterTitleWidget->Skip();
		}
		if (Params.Key == EKeys::Gamepad_DPad_Down)
		{
			HandleDPadDownPressed(FPlatformTime::Seconds());
		}
		if (Params.Key == EKeys::Gamepad_DPad_Up)
		{
			HandleDPadUpPressed(FPlatformTime::Seconds());
		}
		// Down with no revive coming: any key but the pause keys ends it (the character ignores it otherwise).
		AHawkeyeCharacter* Downed = Cast<AHawkeyeCharacter>(GetPawn());
		if (Downed && Downed->IsDowned() && !IsPaused() && Params.Key != EKeys::Escape
			&& Params.Key != EKeys::Gamepad_Special_Right && !Params.Key.IsAxis1D() && !Params.Key.IsAxis2D())
		{
			Downed->GiveUpFromDown();
		}
	}
	else if (Params.Event == IE_Released && Params.Key == EKeys::Gamepad_DPad_Down)
	{
		HandleDPadDownReleased(FPlatformTime::Seconds());
	}
	else if (Params.Event == IE_Released && Params.Key == EKeys::Gamepad_DPad_Up)
	{
		HandleDPadUpReleased(FPlatformTime::Seconds());
	}

	return Super::InputKey(Params);
}

void AHawkeyePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Real time, like the quiver wheel: the hold is the thumb, not game time.
	TickDPadDown(FPlatformTime::Seconds());
	TickDPadUp(FPlatformTime::Seconds());
	TickPlaytestKeys(FPlatformTime::Seconds());

	const double Now = FPlatformTime::Seconds();
	// Automation drives its own notices (UpdatePlaceNotices directly), so no toast or marker lands in a shot;
	// a -game run started to run tests counts from its first frame, before GIsAutomationTesting is set.
	static const bool bRunTests = FCString::Stristr(FCommandLine::Get(), TEXT("RunTests")) != nullptr;
	if (Now >= NextPlaceNoticeSeconds && !IsPaused() && ((!GIsAutomationTesting && !bRunTests) || bAutomationPlaysAsPlayer))
	{
		NextPlaceNoticeSeconds = Now + 0.5;
		UpdatePlaceNotices();
	}
}

int32 AHawkeyePlayerController::UpdatePlaceNotices()
{
	const APawn* ControlledPawn = GetPawn();
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!ControlledPawn || !Save)
	{
		return 0;
	}
	const FVector At = ControlledPawn->GetActorLocation();
	int32 Pushed = 0;
	if (USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this))
	{
		ASafehouse* Near = Safehouses->FindUnnoticed(At, Safehouses->NoticeRadius, Save->GetDiscoveredSafehouses(),
			Save->GetNoticedPlaces());
		if (Near && Save->NotePlace(Near->SafehouseId))
		{
			Safehouses->MarkSafehouse(Near);
			PushHudToast(NSLOCTEXT("Hawkeye", "SafehouseNearby", "[Safehouse nearby]"), Safehouses->GetMarkerNameNow(Near));
			++Pushed;
		}
	}
	if (UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this))
	{
		AChallengeStart* Near = Challenges->FindUnnoticedStart(At, Challenges->NoticeRadius, Save->GetNoticedPlaces());
		if (Near && !Challenges->IsRunning() && Save->NotePlace(Near->Definition->Id))
		{
			PushHudToast(NSLOCTEXT("Hawkeye", "ChallengeNearby", "[Challenge nearby]"), Near->Definition->GetDisplayName());
			++Pushed;
		}
		// Marked and reached: the pedestal is right there, the marker has done its job.
		const AChallengeStart* Marked = Challenges->GetMarkedStart();
		if (Marked && FVector::Dist2D(Marked->GetActorLocation(), At) < 300.f)
		{
			Challenges->ClearChallengeMarker();
		}
	}
	return Pushed;
}

void AHawkeyePlayerController::HandleDPadDownPressed(double NowSeconds)
{
	DPadDown.HoldSeconds = PhoneHoldSeconds;
	DPadDown.Press(NowSeconds);
}

void AHawkeyePlayerController::TickDPadDown(double NowSeconds)
{
	if (DPadDown.Tick(NowSeconds) == EHawkeyeTapHold::Hold)
	{
		TogglePhone();
	}
}

void AHawkeyePlayerController::HandleDPadDownReleased(double NowSeconds)
{
	switch (DPadDown.Release(NowSeconds))
	{
	case EHawkeyeTapHold::Hold:
		TogglePhone();
		break;
	case EHawkeyeTapHold::Tap:
	{
		// Quiver slot 2 (grapple), as IA_Slot2 does from the keyboard; not under a menu or the phone.
		const bool bScreenTaken = bPauseMenuOpen || bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen || bInventoryOpen
			|| bPhoneOpen || bMapOpen || bCloseUpActive;
		const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetPawn());
		if (!bScreenTaken && Hawkeye && Hawkeye->GetInventoryComponent())
		{
			Hawkeye->GetInventoryComponent()->SelectArrowSlot(2);
		}
		break;
	}
	default:
		break;
	}
}

void AHawkeyePlayerController::Input_Phone(const FInputActionValue& /*Value*/)
{
	TogglePhone();
}

void AHawkeyePlayerController::TogglePhone()
{
	SetPhoneOpen(!bPhoneOpen);
}

void AHawkeyePlayerController::SetPhoneOpen(bool bOpen)
{
	if (bPhoneOpen == bOpen)
	{
		return;
	}
	const bool bScreenTaken = bPauseMenuOpen || bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen || bInventoryOpen
		|| bMapOpen || bCloseUpActive || (MissionFlow && MissionFlow->IsRunning());
	if (bOpen && (bScreenTaken || !IsLocalController()))
	{
		return;
	}
	if (bOpen)
	{
		if (!PhoneWidget)
		{
			PhoneWidget = CreateWidget<UPhoneWidget>(this, PhoneWidgetClass ? PhoneWidgetClass.Get() : UPhoneWidget::StaticClass());
		}
		if (!PhoneWidget)
		{
			return;
		}
		if (!PhoneWidget->IsInViewport())
		{
			PhoneWidget->AddToViewport(12);
		}
		PhoneWidget->Open();
	}
	else if (PhoneWidget)
	{
		// It slides out and takes itself off the screen.
		PhoneWidget->Close();
	}
	bPhoneOpen = bOpen;
	SetPause(bOpen);
	ApplyPauseInputMode(bOpen);
	UE_LOG(LogHawkeye, Log, TEXT("%s: phone %s."), *GetName(), bOpen ? TEXT("open") : TEXT("closed"));
}

void AHawkeyePlayerController::HandleDPadUpPressed(double NowSeconds)
{
	DPadUp.HoldSeconds = MapHoldSeconds;
	DPadUp.Press(NowSeconds);
}

void AHawkeyePlayerController::TickDPadUp(double NowSeconds)
{
	if (DPadUp.Tick(NowSeconds) == EHawkeyeTapHold::Hold && !bMapOpen)
	{
		SetMapOpen(true);
	}
}

void AHawkeyePlayerController::HandleDPadUpReleased(double NowSeconds)
{
	switch (DPadUp.Release(NowSeconds))
	{
	case EHawkeyeTapHold::Hold:
		if (!bMapOpen)
		{
			SetMapOpen(true);
		}
		break;
	case EHawkeyeTapHold::Tap:
	{
		// Quiver slot 1 (standard arrows), as IA_Slot1 does from the keyboard; not under a menu.
		const bool bScreenTaken = bPauseMenuOpen || bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen
			|| bInventoryOpen || bPhoneOpen || bMapOpen || bCloseUpActive;
		const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetPawn());
		if (!bScreenTaken && Hawkeye && Hawkeye->GetInventoryComponent())
		{
			Hawkeye->GetInventoryComponent()->SelectArrowSlot(1);
		}
		break;
	}
	default:
		break;
	}
}

void AHawkeyePlayerController::Input_Map(const FInputActionValue& /*Value*/)
{
	ToggleMap();
}

void AHawkeyePlayerController::ToggleMap()
{
	SetMapOpen(!bMapOpen);
}

void AHawkeyePlayerController::SetMapOpen(bool bOpen)
{
	if (bMapOpen == bOpen)
	{
		return;
	}
	const bool bScreenTaken = bPauseMenuOpen || bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen
		|| bInventoryOpen || bPhoneOpen || bCloseUpActive || (MissionFlow && MissionFlow->IsRunning());
	if (bOpen && bScreenTaken)
	{
		return;
	}
	// The flag flips with or without a widget, like the inventory: a headless test with no local
	// player still needs bMapOpen and the pause it drives.
	bMapOpen = bOpen;
	if (bOpen && IsLocalController())
	{
		if (!MapWidget)
		{
			MapWidget = CreateWidget<UHawkeyeMapWidget>(this, MapWidgetClass ? MapWidgetClass.Get() : UHawkeyeMapWidget::StaticClass());
		}
		if (MapWidget)
		{
			if (!MapWidget->IsInViewport())
			{
				MapWidget->AddToViewport(11);
			}
			MapWidget->Open();
		}
	}
	else if (!bOpen && MapWidget)
	{
		MapWidget->Close();
	}
	SetPause(bOpen);
	ApplyPauseInputMode(bOpen);
	UE_LOG(LogHawkeye, Log, TEXT("%s: map %s."), *GetName(), bOpen ? TEXT("open") : TEXT("closed"));
}

void AHawkeyePlayerController::Input_Pause(const FInputActionValue& /*Value*/)
{
	// Escape inside Settings is Back, not unpause: the player came from the pause menu and
	// that is where one press should put them.
	if (bSettingsOpen)
	{
		CloseSettings();
		return;
	}

	if (bFlashbackReplayOpen)
	{
		CloseFlashbackReplay();
		return;
	}

	// Escape on the difficulty prompt goes back to the main menu under it.
	if (bDifficultyPromptOpen)
	{
		HideDifficultyPrompt();
		return;
	}

	if (bPhoneOpen)
	{
		SetPhoneOpen(false);
		return;
	}

	if (bMapOpen)
	{
		SetMapOpen(false);
		return;
	}

	// Escape closes the inventory rather than opening a second menu over it.
	if (bInventoryOpen)
	{
		SetInventoryOpen(false);
		return;
	}

	if (bSafehouseMenuOpen)
	{
		CloseSafehouseMenu();
		return;
	}

	if (bChallengeResultsOpen)
	{
		CloseChallengeResults();
		return;
	}

	// The main menu has no "back": the player picks one of its buttons.
	if (bMainMenuOpen)
	{
		return;
	}

	TogglePause();
}

void AHawkeyePlayerController::ToggleInventory()
{
	SetInventoryOpen(!bInventoryOpen);
}

void AHawkeyePlayerController::SetInventoryOpen(bool bOpen)
{
	if (bInventoryOpen == bOpen)
	{
		return;
	}

	// One thing owns the pause at a time; the slideshow and every menu outrank Tab.
	if (bOpen && (bPauseMenuOpen || bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen || bPhoneOpen || bMapOpen))
	{
		return;
	}

	// The flag flips regardless of whether a widget can be shown for it, the same as the pause
	// menu: a bot or a headless test with no local player still needs bInventoryOpen (and the
	// pause it drives) to be accurate, even with nothing drawn.
	bInventoryOpen = bOpen;

	if (bOpen)
	{
		if (!InventoryWidgetClass || !IsLocalController())
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s has no InventoryWidgetClass set."), *GetName());
		}
		else
		{
			if (!InventoryWidget)
			{
				InventoryWidget = CreateWidget<UHawkeyeInventoryWidget>(this, InventoryWidgetClass);
				if (!InventoryWidget)
				{
					UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the inventory widget."), *GetName());
				}
			}

			if (InventoryWidget)
			{
				if (!InventoryWidget->IsInViewport())
				{
					InventoryWidget->AddToViewport(10);
				}
				InventoryWidget->SetVisibility(ESlateVisibility::Visible);
				InventoryWidget->BindToOwningPawn();
				InventoryWidget->RefreshRows();
			}
		}
	}
	else if (InventoryWidget)
	{
		InventoryWidget->RemoveFromParent();
	}

	SetPause(bOpen);
	ApplyPauseInputMode(bOpen);
}

bool AHawkeyePlayerController::CanTogglePause() const
{
	// The slideshow pauses the game itself and restores the previous state on finish; letting
	// Escape unpause underneath it would leave the flashback running over live gameplay.
	if (bFlashbackActive || bMainMenuOpen || bSafehouseMenuOpen || bChallengeResultsOpen || bPhoneOpen || bMapOpen
		|| IsPhotoModeActive())
	{
		return false;
	}

	// Dead: the mission is already reloading behind the death screen, and a pause menu whose
	// Resume button gives you a corpse back is worse than no menu.
	const APawn* ControlledPawn = GetPawn();
	const UHealthComponent* Health =
		ControlledPawn ? ControlledPawn->FindComponentByClass<UHealthComponent>() : nullptr;

	return !Health || Health->IsAlive();
}

void AHawkeyePlayerController::TogglePause()
{
	if (!CanTogglePause())
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: pause ignored (a flashback is playing, or the player is dead)."), *GetName());
		return;
	}

	SetPauseMenuOpen(!bPauseMenuOpen);
}

void AHawkeyePlayerController::SetPauseMenuOpen(bool bOpen)
{
	if (bPauseMenuOpen == bOpen)
	{
		return;
	}

	bPauseMenuOpen = bOpen;

	if (bOpen)
	{
		ShowPauseWidget();
	}
	else
	{
		// Leaving the pause state at all takes the settings screen and the replay list with it.
		bSettingsOpen = false;
		HideSettingsWidget();
		bFlashbackReplayOpen = false;
		if (FlashbackReplayWidget)
		{
			FlashbackReplayWidget->RemoveFromParent();
		}
		HidePauseWidget();
	}

	SetPause(bOpen);
	ApplyPauseInputMode(bOpen);
}

void AHawkeyePlayerController::OpenSettings()
{
	if ((!bPauseMenuOpen && !bMainMenuOpen) || bSettingsOpen)
	{
		return;
	}

	if (!ShowSettingsWidget())
	{
		return;
	}

	// One menu at a time; the game stays paused underneath both.
	HidePauseWidget();
	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
	}
	bSettingsOpen = true;
	ApplyPauseInputMode(true);
}

void AHawkeyePlayerController::CloseSettings()
{
	if (!bSettingsOpen)
	{
		return;
	}

	bSettingsOpen = false;
	HideSettingsWidget();
	if (bMainMenuOpen && MainMenuWidget)
	{
		MainMenuWidget->AddToViewport(20);
	}
	else
	{
		ShowPauseWidget();
	}
	ApplyPauseInputMode(true);
}

UHawkeyePauseWidget* AHawkeyePlayerController::ShowPauseWidget()
{
	if (!PauseWidgetClass || !IsLocalController())
	{
		// Pausing still works; there is simply no menu drawn over it.
		UE_LOG(LogHawkeye, Warning, TEXT("%s has no PauseWidgetClass set."), *GetName());
		return nullptr;
	}

	if (!PauseWidget)
	{
		PauseWidget = CreateWidget<UHawkeyePauseWidget>(this, PauseWidgetClass);
		if (!PauseWidget)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the pause widget."), *GetName());
			return nullptr;
		}

		PauseWidget->OnResumeClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseResumeClicked);
		PauseWidget->OnSettingsClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseSettingsClicked);
		PauseWidget->OnReplayFlashbacksClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseReplayFlashbacksClicked);
		PauseWidget->OnMarkSafehouseClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseMarkSafehouseClicked);
		PauseWidget->OnMarkChallengeClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseMarkChallengeClicked);
		PauseWidget->OnPhotoModeClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePausePhotoModeClicked);
		PauseWidget->OnRestartMissionClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseRestartClicked);
		PauseWidget->OnQuitToDesktopClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseQuitClicked);
		PauseWidget->OnQuitToMenuClicked.AddDynamic(this, &AHawkeyePlayerController::HandlePauseQuitToMenuClicked);
	}

	if (!PauseWidget->IsInViewport())
	{
		PauseWidget->AddToViewport(10);
	}
	PauseWidget->SetVisibility(ESlateVisibility::Visible);

	return PauseWidget;
}

void AHawkeyePlayerController::HidePauseWidget()
{
	if (PauseWidget)
	{
		PauseWidget->RemoveFromParent();
	}
}

UHawkeyeSettingsWidget* AHawkeyePlayerController::ShowSettingsWidget()
{
	if (!SettingsWidgetClass || !IsLocalController())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s has no SettingsWidgetClass set."), *GetName());
		return nullptr;
	}

	if (!SettingsWidget)
	{
		SettingsWidget = CreateWidget<UHawkeyeSettingsWidget>(this, SettingsWidgetClass);
		if (!SettingsWidget)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the settings widget."), *GetName());
			return nullptr;
		}

		SettingsWidget->OnBackRequested.AddDynamic(this, &AHawkeyePlayerController::HandleSettingsBackRequested);
	}

	if (!SettingsWidget->IsInViewport())
	{
		SettingsWidget->AddToViewport(11);
	}
	SettingsWidget->SetVisibility(ESlateVisibility::Visible);
	SettingsWidget->RefreshFromSettings();

	return SettingsWidget;
}

void AHawkeyePlayerController::HideSettingsWidget()
{
	if (SettingsWidget)
	{
		SettingsWidget->RemoveFromParent();
	}
}

TSharedPtr<SWidget> AHawkeyePlayerController::GetFocusedMenuWidget() const
{
	if (bDifficultyPromptOpen && DifficultyPromptWidget)
	{
		return DifficultyPromptWidget->TakeWidget();
	}
	if (bFlashbackReplayOpen && FlashbackReplayWidget)
	{
		return FlashbackReplayWidget->TakeWidget();
	}
	if (bPhoneOpen && PhoneWidget)
	{
		return PhoneWidget->TakeWidget();
	}
	if (bMapOpen && MapWidget)
	{
		return MapWidget->TakeWidget();
	}
	if (bInventoryOpen && InventoryWidget)
	{
		return InventoryWidget->TakeWidget();
	}
	if (bSettingsOpen && SettingsWidget)
	{
		return SettingsWidget->TakeWidget();
	}
	if (bSafehouseMenuOpen && SafehouseWidget)
	{
		return SafehouseWidget->TakeWidget();
	}
	if (bChallengeResultsOpen && ChallengeResultsWidget)
	{
		return ChallengeResultsWidget->TakeWidget();
	}
	if (bMainMenuOpen && MainMenuWidget)
	{
		return MainMenuWidget->TakeWidget();
	}
	if (PauseWidget)
	{
		return PauseWidget->TakeWidget();
	}
	return nullptr;
}

void AHawkeyePlayerController::ApplyPauseInputMode(bool bPaused)
{
	// Every menu comes through here: a toggled sprint does not carry on under it (or after it).
	if (bPaused)
	{
		if (AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(GetPawn()))
		{
			Kate->StopSprintToggle(EHawkeyeSprintStop::Menu);
		}
	}
	if (!IsLocalController())
	{
		return;
	}

	if (bPaused)
	{
		// UI and Game so the buttons take clicks but the world is still shown behind them.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		// Game and UI, so the Escape mapping still reaches this controller while a slider has
		// keyboard focus.
		if (const TSharedPtr<SWidget> Focus = GetFocusedMenuWidget())
		{
			Mode.SetWidgetToFocus(Focus);
		}
		SetInputMode(Mode);
		bShowMouseCursor = true;
		return;
	}

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void AHawkeyePlayerController::HandlePauseResumeClicked()
{
	SetPauseMenuOpen(false);
}

void AHawkeyePlayerController::HandlePauseSettingsClicked()
{
	OpenSettings();
}

void AHawkeyePlayerController::HandleSettingsBackRequested()
{
	CloseSettings();
}

void AHawkeyePlayerController::HandlePauseReplayFlashbacksClicked()
{
	OpenFlashbackReplay();
}

void AHawkeyePlayerController::OpenFlashbackReplay()
{
	if (!bPauseMenuOpen || bSettingsOpen || bFlashbackReplayOpen || !FlashbackReplayWidgetClass || !IsLocalController())
	{
		return;
	}
	if (!FlashbackReplayWidget)
	{
		FlashbackReplayWidget = CreateWidget<UFlashbackReplayWidget>(this, FlashbackReplayWidgetClass);
		if (!FlashbackReplayWidget)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the flashback replay widget."), *GetName());
			return;
		}
		FlashbackReplayWidget->OnFlashbackPicked.AddDynamic(this, &AHawkeyePlayerController::HandleFlashbackReplayPicked);
		FlashbackReplayWidget->OnBackRequested.AddDynamic(this, &AHawkeyePlayerController::HandleFlashbackReplayBack);
	}
	FlashbackReplayWidget->SetFlashbacks(UHawkeyeSettingsSubsystem::GetCurrentSettings(this).SeenFlashbacks);
	if (!FlashbackReplayWidget->IsInViewport())
	{
		FlashbackReplayWidget->AddToViewport(11);
	}
	HidePauseWidget();
	bFlashbackReplayOpen = true;
	ApplyPauseInputMode(true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: replay flashbacks (%d seen)."), *GetName(), FlashbackReplayWidget->GetEntryCount());
}

void AHawkeyePlayerController::CloseFlashbackReplay()
{
	if (!bFlashbackReplayOpen)
	{
		return;
	}
	bFlashbackReplayOpen = false;
	if (FlashbackReplayWidget)
	{
		FlashbackReplayWidget->RemoveFromParent();
	}
	ShowPauseWidget();
	ApplyPauseInputMode(true);
}

void AHawkeyePlayerController::HandleFlashbackReplayBack()
{
	CloseFlashbackReplay();
}

void AHawkeyePlayerController::HandleFlashbackReplayPicked(UFlashbackDefinition* Flashback)
{
	if (!Flashback)
	{
		return;
	}
	CloseFlashbackReplay();
	// Only the slides: a replay never reopens the playable scene or moves the story on.
	UE_LOG(LogHawkeye, Log, TEXT("%s: replaying %s."), *GetName(), *Flashback->GetName());
	PlayFlashback(Flashback);
}

void AHawkeyePlayerController::HandlePauseRestartClicked()
{
	RestartMissionFromPause();
}

void AHawkeyePlayerController::HandlePauseQuitClicked()
{
	QuitToDesktop();
}

void AHawkeyePlayerController::RestartMissionFromPause()
{
	// Unpause first: the fade and the load it ends in run on a live world.
	SetPauseMenuOpen(false);

	// Back to the last save, as dying does, with a shorter fade: the player chose this.
	if (AHawkeyeGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AHawkeyeGameMode>() : nullptr)
	{
		GameMode->ReloadLastCheckpoint(0.5f);
	}
}

void AHawkeyePlayerController::HandlePauseQuitToMenuClicked()
{
	QuitToMenu();
}

void AHawkeyePlayerController::QuitToMenu()
{
	// Continue has to bring back this moment, not the last autosave: patrols, the quiver, health and
	// hints since then would otherwise be lost. Refused (and logged) while down, in a scene or mid-load.
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->SaveCampaign(TEXT("quit to menu"));
	}
	SetPauseMenuOpen(false);
	ShowMainMenu();
	bMainMenuOverFreshBoot = false;
}

// --- Main menu ----------------------------------------------------------------------------------------

UHawkeyeMainMenuWidget* AHawkeyePlayerController::EnsureMainMenuWidget()
{
	if (MainMenuWidget || !MainMenuWidgetClass || !IsLocalController())
	{
		return MainMenuWidget;
	}
	MainMenuWidget = CreateWidget<UHawkeyeMainMenuWidget>(this, MainMenuWidgetClass);
	if (!MainMenuWidget)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the main menu widget."), *GetName());
		return nullptr;
	}
	MainMenuWidget->OnContinueClicked.AddDynamic(this, &AHawkeyePlayerController::MainMenuContinue);
	MainMenuWidget->OnNewGameClicked.AddDynamic(this, &AHawkeyePlayerController::MainMenuNewGame);
	MainMenuWidget->OnSettingsClicked.AddDynamic(this, &AHawkeyePlayerController::HandleMainMenuSettingsClicked);
	MainMenuWidget->OnQuitClicked.AddDynamic(this, &AHawkeyePlayerController::HandleMainMenuQuitClicked);
	return MainMenuWidget;
}

void AHawkeyePlayerController::ShowMainMenu()
{
	UHawkeyeMainMenuWidget* Menu = EnsureMainMenuWidget();
	if (!Menu)
	{
		return;
	}
	SetInventoryOpen(false);
	SetPhoneOpen(false);
	CloseSafehouseMenu();
	Menu->RefreshFromSave(UHawkeyeSaveSubsystem::Get(this));
	if (!Menu->IsInViewport())
	{
		Menu->AddToViewport(20);
	}
	bMainMenuOpen = true;
	SetHudVisible(false);
	SetPause(true);
	ApplyPauseInputMode(true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: main menu (continue %s)."), *GetName(),
		Menu->IsContinueEnabled() ? TEXT("on") : TEXT("off"));
}

void AHawkeyePlayerController::HideMainMenu()
{
	if (!bMainMenuOpen)
	{
		return;
	}
	bSettingsOpen = false;
	HideSettingsWidget();
	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
	}
	bMainMenuOpen = false;
	bMainMenuOverFreshBoot = false;
	SetHudVisible(true);
	SetPause(false);
	ApplyPauseInputMode(false);

	if (UMissionDefinition* Opening = PendingOpening)
	{
		PendingOpening = nullptr;
		BeginChapterOpening(Opening);
	}
}

void AHawkeyePlayerController::MainMenuContinue()
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!Save || !Save->HasSave())
	{
		return;
	}
	// The menu stays up over the paused world until the load swaps the world out.
	Save->LoadCampaign();
}

void AHawkeyePlayerController::MainMenuNewGame()
{
	// Asked once, here; automation runs keep whatever the settings (or -Difficulty=) say.
	if (!IsAutomationRun() && DifficultyPromptWidgetClass)
	{
		ShowDifficultyPrompt();
		if (bDifficultyPromptOpen)
		{
			return;
		}
	}
	StartNewGame();
}

void AHawkeyePlayerController::ShowDifficultyPrompt()
{
	if (!IsLocalController() || !DifficultyPromptWidgetClass)
	{
		return;
	}
	if (!DifficultyPromptWidget)
	{
		DifficultyPromptWidget = CreateWidget<UHawkeyeDifficultyPromptWidget>(this, DifficultyPromptWidgetClass);
		if (!DifficultyPromptWidget)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the difficulty prompt."), *GetName());
			return;
		}
		DifficultyPromptWidget->OnDifficultyChosen.AddDynamic(this, &AHawkeyePlayerController::HandleNewGameDifficultyChosen);
	}
	if (!DifficultyPromptWidget->IsInViewport())
	{
		DifficultyPromptWidget->AddToViewport(25);
	}
	bDifficultyPromptOpen = true;
	ApplyPauseInputMode(true);
}

void AHawkeyePlayerController::HideDifficultyPrompt()
{
	if (!bDifficultyPromptOpen)
	{
		return;
	}
	bDifficultyPromptOpen = false;
	if (DifficultyPromptWidget)
	{
		DifficultyPromptWidget->RemoveFromParent();
	}
	ApplyPauseInputMode(bMainMenuOpen || bPauseMenuOpen);
}

void AHawkeyePlayerController::HandleNewGameDifficultyChosen(EHawkeyeDifficulty Difficulty)
{
	if (UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(this))
	{
		Settings->SetDifficulty(Difficulty);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: new game on %s."), *GetName(), *UDifficultySubsystem::GetDifficultyName(Difficulty).ToString());
	HideDifficultyPrompt();
	StartNewGame();
}

void AHawkeyePlayerController::StartNewGame()
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!bMainMenuOverFreshBoot && Save)
	{
		Save->StartNewGame();
		return;
	}
	// Nothing has happened in this world yet: it is the new game.
	HideMainMenu();
	if (Save)
	{
		Save->SaveCampaign(TEXT("new game"));
	}
}

void AHawkeyePlayerController::HandleMainMenuSettingsClicked()
{
	OpenSettings();
}

void AHawkeyePlayerController::HandleMainMenuQuitClicked()
{
	HideMainMenu();
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

// --- Safehouse ----------------------------------------------------------------------------------------

UHawkeyeSafehouseWidget* AHawkeyePlayerController::EnsureSafehouseWidget()
{
	if (SafehouseWidget || !SafehouseWidgetClass || !IsLocalController())
	{
		return SafehouseWidget;
	}
	SafehouseWidget = CreateWidget<UHawkeyeSafehouseWidget>(this, SafehouseWidgetClass);
	if (!SafehouseWidget)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the safehouse widget."), *GetName());
		return nullptr;
	}
	SafehouseWidget->OnRefillClicked.AddDynamic(this, &AHawkeyePlayerController::SafehouseRefill);
	SafehouseWidget->OnSaveClicked.AddDynamic(this, &AHawkeyePlayerController::SafehouseSave);
	SafehouseWidget->OnFastTravelClicked.AddDynamic(this, &AHawkeyePlayerController::SafehouseFastTravel);
	SafehouseWidget->OnChapterSelectClicked.AddDynamic(this, &AHawkeyePlayerController::SafehouseChapterSelect);
	SafehouseWidget->OnLeaveClicked.AddDynamic(this, &AHawkeyePlayerController::CloseSafehouseMenu);
	SafehouseWidget->OnListRowPicked.AddDynamic(this, &AHawkeyePlayerController::HandleSafehouseListPicked);
	SafehouseWidget->OnListBackClicked.AddDynamic(this, &AHawkeyePlayerController::HandleSafehouseListBack);
	return SafehouseWidget;
}

void AHawkeyePlayerController::OpenSafehouseMenu(ASafehouse* Safehouse)
{
	if (bSafehouseMenuOpen || bChallengeResultsOpen || bMainMenuOpen || bPauseMenuOpen || bFlashbackActive || !Safehouse)
	{
		return;
	}
	UHawkeyeSafehouseWidget* Menu = EnsureSafehouseWidget();
	if (!Menu)
	{
		return;
	}
	SetInventoryOpen(false);
	ActiveSafehouse = Safehouse;
	Menu->SetSafehouseName(Safehouse->GetDisplayName());
	Menu->SetSubtitle(Safehouse->Address);
	Menu->ShowMain();
	SafehouseListKind = 0;
	Menu->SetStatus(NSLOCTEXT("Hawkeye", "SafehouseWelcome", "Healed to full. Progress saved."));
	if (!Menu->IsInViewport())
	{
		Menu->AddToViewport(10);
	}
	bSafehouseMenuOpen = true;
	SetPause(true);
	ApplyPauseInputMode(true);
}

void AHawkeyePlayerController::CloseSafehouseMenu()
{
	if (!bSafehouseMenuOpen)
	{
		return;
	}
	if (SafehouseWidget)
	{
		SafehouseWidget->RemoveFromParent();
	}
	bSafehouseMenuOpen = false;
	ActiveSafehouse = nullptr;
	SafehouseListKind = 0;
	TravelRowIds.Reset();
	SetPause(false);
	ApplyPauseInputMode(false);
}

void AHawkeyePlayerController::OpenChallengeResults(const FChallengeResult& Result)
{
	if (!IsLocalController() || !ChallengeResultsWidgetClass || bMainMenuOpen || bPauseMenuOpen || bFlashbackActive
		|| bSafehouseMenuOpen)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s results not shown (another screen is up)."), *GetName(), *Result.ChallengeId.ToString());
		return;
	}
	if (!ChallengeResultsWidget)
	{
		ChallengeResultsWidget = CreateWidget<UChallengeResultsWidget>(this, ChallengeResultsWidgetClass);
		if (!ChallengeResultsWidget)
		{
			return;
		}
		ChallengeResultsWidget->OnRetryClicked.AddDynamic(this, &AHawkeyePlayerController::ChallengeRetry);
		ChallengeResultsWidget->OnLeaveClicked.AddDynamic(this, &AHawkeyePlayerController::CloseChallengeResults);
	}
	SetInventoryOpen(false);
	SetPhoneOpen(false);
	ChallengeResultsWidget->SetResult(Result);
	if (!ChallengeResultsWidget->IsInViewport())
	{
		ChallengeResultsWidget->AddToViewport(10);
	}
	bChallengeResultsOpen = true;
	SetPause(true);
	ApplyPauseInputMode(true);
}

void AHawkeyePlayerController::CloseChallengeResults()
{
	if (!bChallengeResultsOpen)
	{
		return;
	}
	if (ChallengeResultsWidget)
	{
		ChallengeResultsWidget->RemoveFromParent();
	}
	bChallengeResultsOpen = false;
	SetPause(false);
	ApplyPauseInputMode(false);
}

void AHawkeyePlayerController::ChallengeRetry()
{
	CloseChallengeResults();
	if (UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this))
	{
		Challenges->RetryLastChallenge();
	}
}

void AHawkeyePlayerController::SetSafehouseStatus(const FText& Status)
{
	if (SafehouseWidget)
	{
		SafehouseWidget->SetStatus(Status);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: safehouse: %s"), *GetName(), *Status.ToString());
}

void AHawkeyePlayerController::SafehouseRefill()
{
	const APawn* ControlledPawn = GetPawn();
	UInventoryComponent* Inventory = ControlledPawn ? ControlledPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const int32 Added = ASafehouse::RefillArrows(Inventory);
	SetSafehouseStatus(Added > 0
		? FText::Format(NSLOCTEXT("Hawkeye", "SafehouseRefilled", "Arrows refilled (+{0})."), Added)
		: NSLOCTEXT("Hawkeye", "SafehouseQuiverFull", "The quiver is already full."));
}

void AHawkeyePlayerController::SafehouseSave()
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const bool bSaved = Save && Save->SaveCampaign(TEXT("safehouse"));
	SetSafehouseStatus(bSaved ? NSLOCTEXT("Hawkeye", "SafehouseSaved", "Saved.")
		: NSLOCTEXT("Hawkeye", "SafehouseSaveFailed", "Could not save right now."));
}

void AHawkeyePlayerController::PushHudToast(const FText& Heading, const FText& Title)
{
	if (UHawkeyeObjectiveWidget* Toasts = HudWidget ? HudWidget->GetObjectiveMarker() : nullptr)
	{
		Toasts->PushToast(Heading, Title);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: toast: %s %s"), *GetName(), *Heading.ToString(), *Title.ToString());
}

void AHawkeyePlayerController::SafehouseFastTravel()
{
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!Safehouses || !SafehouseWidget)
	{
		return;
	}
	const FText Refusal = Safehouses->GetTravelRefusalNow();
	if (!Refusal.IsEmpty())
	{
		SetSafehouseStatus(Refusal);
		PushHudToast(FText::GetEmpty(), Refusal);
		return;
	}
	const TArray<FName> Discovered = Save ? Save->GetDiscoveredSafehouses() : TArray<FName>();
	const TArray<FHawkeyeSafehouseEntry> List = USafehouseSubsystem::BuildTravelList(Safehouses->MakeEntries(Discovered),
		ActiveSafehouse ? ActiveSafehouse->SafehouseId : NAME_None, Discovered);
	TArray<FHawkeyeMenuListRow> Rows;
	TravelRowIds.Reset();
	for (const FHawkeyeSafehouseEntry& Entry : List)
	{
		FHawkeyeMenuListRow& Row = Rows.AddDefaulted_GetRef();
		Row.Label = Entry.bDiscovered ? Entry.DisplayName : NSLOCTEXT("Hawkeye", "SafehouseUndiscovered", "[Undiscovered]");
		Row.bEnabled = Entry.bDiscovered;
		TravelRowIds.Add(Entry.SafehouseId);
	}
	SafehouseListKind = 1;
	SafehouseWidget->ShowList(NSLOCTEXT("Hawkeye", "FastTravelHeading", "Fast travel"), Rows);
	SetSafehouseStatus(Rows.Num() == 0 ? NSLOCTEXT("Hawkeye", "SafehouseNoOther", "No other safehouse yet.")
		: NSLOCTEXT("Hawkeye", "FastTravelPick", "Pick a safehouse."));
}

void AHawkeyePlayerController::SafehouseChapterSelect()
{
	if (!SafehouseWidget)
	{
		return;
	}
	// TODO(stage4): real chapters once more than CH01 exists; until then a stub with placeholder rows.
	TArray<FHawkeyeMenuListRow> Rows;
	Rows.Add({ NSLOCTEXT("Hawkeye", "ChapterSelectCH01", "[CH01]"), true });
	for (int32 Slot = 1; Slot < ChapterSelectSlots; ++Slot)
	{
		Rows.Add({ NSLOCTEXT("Hawkeye", "ChapterSelectLocked", "[Locked]"), false });
	}
	SafehouseListKind = 2;
	SafehouseWidget->ShowList(NSLOCTEXT("Hawkeye", "ChapterSelectHeading", "Chapter select"), Rows);
	SetSafehouseStatus(NSLOCTEXT("Hawkeye", "SafehouseChapters", "[Chapter select is a stub]"));
}

void AHawkeyePlayerController::HandleSafehouseListPicked(int32 Index)
{
	if (SafehouseListKind == 1 && TravelRowIds.IsValidIndex(Index))
	{
		FastTravelTo(TravelRowIds[Index]);
		return;
	}
	if (SafehouseListKind == 2)
	{
		SetSafehouseStatus(NSLOCTEXT("Hawkeye", "ChapterSelectCurrent", "[CH01] is the chapter in progress."));
	}
}

void AHawkeyePlayerController::HandleSafehouseListBack()
{
	SafehouseListKind = 0;
	TravelRowIds.Reset();
	SetSafehouseStatus(FText::GetEmpty());
}

bool AHawkeyePlayerController::FastTravelTo(FName SafehouseId)
{
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	ASafehouse* Destination = Safehouses ? Safehouses->FindSafehouse(SafehouseId) : nullptr;
	if (!Safehouses || !Destination)
	{
		return false;
	}
	const FText Refusal = Safehouses->GetTravelRefusalNow();
	if (!Refusal.IsEmpty())
	{
		SetSafehouseStatus(Refusal);
		PushHudToast(FText::GetEmpty(), Refusal);
		return false;
	}
	CloseSafehouseMenu();
	return Safehouses->BeginFastTravel(this, Destination);
}

bool AHawkeyePlayerController::MarkNearestSafehouse()
{
	SetPauseMenuOpen(false);
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	const ASafehouse* Marked = Safehouses && GetPawn() ? Safehouses->MarkNearestSafehouse(GetPawn()->GetActorLocation()) : nullptr;
	PushHudToast(Marked ? NSLOCTEXT("Hawkeye", "SafehouseMarked", "[Safehouse marked]")
		: NSLOCTEXT("Hawkeye", "SafehouseNoneToMark", "[No safehouse in the district]"),
		Marked ? Safehouses->GetMarkerNameNow(Marked) : FText::GetEmpty());
	return Marked != nullptr;
}

void AHawkeyePlayerController::HandlePauseMarkSafehouseClicked()
{
	MarkNearestSafehouse();
}

bool AHawkeyePlayerController::MarkNearestChallenge()
{
	SetPauseMenuOpen(false);
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	const AChallengeStart* Marked = Challenges && GetPawn() ? Challenges->MarkNearestChallenge(GetPawn()->GetActorLocation()) : nullptr;
	PushHudToast(Marked ? NSLOCTEXT("Hawkeye", "ChallengeMarked", "[Challenge marked]")
		: NSLOCTEXT("Hawkeye", "ChallengeNoneToMark", "[No challenge in the district]"),
		Marked ? Marked->Definition->GetDisplayName() : FText::GetEmpty());
	return Marked != nullptr;
}

void AHawkeyePlayerController::HandlePauseMarkChallengeClicked()
{
	MarkNearestChallenge();
}

void AHawkeyePlayerController::QuitToDesktop()
{
	SetPauseMenuOpen(false);
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void AHawkeyePlayerController::CreateHud()
{
	if (HudWidget || !HudWidgetClass || !IsLocalController())
	{
		return;
	}

	HudWidget = CreateWidget<UHawkeyeHudWidget>(this, HudWidgetClass);
	if (!HudWidget)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the HUD widget."), *GetName());
		return;
	}

	HudWidget->AddToViewport(0);
}

void AHawkeyePlayerController::SetHudVisible(bool bVisible)
{
	if (HudWidget)
	{
		HudWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

UHawkeyeHudWidget* AHawkeyePlayerController::GetHawkeyeHudFor(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	return PC ? PC->GetHawkeyeHud() : nullptr;
}

void AHawkeyePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitPhotoMode();
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->OnSettingsChanged.RemoveDynamic(this, &AHawkeyePlayerController::HandleSettingsChanged);
	}
	PopVolumeMix();

	if (UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this))
	{
		MissionSubsystem->OnMissionComplete.RemoveDynamic(this, &AHawkeyePlayerController::HandleMissionComplete);
		MissionSubsystem->OnFlashbackRequested.RemoveDynamic(this, &AHawkeyePlayerController::HandleFlashbackRequested);
		MissionSubsystem->OnMissionStarted.RemoveDynamic(this, &AHawkeyePlayerController::HandleMissionStartedForOpening);
	}
	GetWorldTimerManager().ClearTimer(CloseUpTimer);
	if (PhoneWidget)
	{
		PhoneWidget->RemoveFromParent();
		PhoneWidget = nullptr;
	}
	bPhoneOpen = false;
	if (MapWidget)
	{
		MapWidget->RemoveFromParent();
		MapWidget = nullptr;
	}
	bMapOpen = false;
	if (ChapterTitleWidget)
	{
		ChapterTitleWidget->RemoveFromParent();
		ChapterTitleWidget = nullptr;
	}

	if (EndCardWidget)
	{
		EndCardWidget->OnEndCardFinished.RemoveDynamic(this, &AHawkeyePlayerController::HandleEndCardFinished);
		EndCardWidget->RemoveFromParent();
		EndCardWidget = nullptr;
	}

	if (ActiveFlashbackWidget)
	{
		ActiveFlashbackWidget->OnFlashbackFinished.RemoveDynamic(this, &AHawkeyePlayerController::HandleFlashbackFinished);
		ActiveFlashbackWidget = nullptr;
	}

	if (SettingsWidget)
	{
		SettingsWidget->OnBackRequested.RemoveDynamic(this, &AHawkeyePlayerController::HandleSettingsBackRequested);
		SettingsWidget->RemoveFromParent();
		SettingsWidget = nullptr;
	}
	bSettingsOpen = false;

	if (InventoryWidget)
	{
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}
	bInventoryOpen = false;

	if (MainMenuWidget)
	{
		MainMenuWidget->OnContinueClicked.RemoveDynamic(this, &AHawkeyePlayerController::MainMenuContinue);
		MainMenuWidget->OnNewGameClicked.RemoveDynamic(this, &AHawkeyePlayerController::MainMenuNewGame);
		MainMenuWidget->OnSettingsClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandleMainMenuSettingsClicked);
		MainMenuWidget->OnQuitClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandleMainMenuQuitClicked);
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}
	bMainMenuOpen = false;

	if (SafehouseWidget)
	{
		SafehouseWidget->OnRefillClicked.RemoveDynamic(this, &AHawkeyePlayerController::SafehouseRefill);
		SafehouseWidget->OnSaveClicked.RemoveDynamic(this, &AHawkeyePlayerController::SafehouseSave);
		SafehouseWidget->OnFastTravelClicked.RemoveDynamic(this, &AHawkeyePlayerController::SafehouseFastTravel);
		SafehouseWidget->OnChapterSelectClicked.RemoveDynamic(this, &AHawkeyePlayerController::SafehouseChapterSelect);
		SafehouseWidget->OnLeaveClicked.RemoveDynamic(this, &AHawkeyePlayerController::CloseSafehouseMenu);
		SafehouseWidget->OnListRowPicked.RemoveDynamic(this, &AHawkeyePlayerController::HandleSafehouseListPicked);
		SafehouseWidget->OnListBackClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandleSafehouseListBack);
		SafehouseWidget->RemoveFromParent();
		SafehouseWidget = nullptr;
	}
	bSafehouseMenuOpen = false;

	if (ChallengeResultsWidget)
	{
		ChallengeResultsWidget->OnRetryClicked.RemoveDynamic(this, &AHawkeyePlayerController::ChallengeRetry);
		ChallengeResultsWidget->OnLeaveClicked.RemoveDynamic(this, &AHawkeyePlayerController::CloseChallengeResults);
		ChallengeResultsWidget->RemoveFromParent();
		ChallengeResultsWidget = nullptr;
	}
	bChallengeResultsOpen = false;

	if (PauseWidget)
	{
		PauseWidget->OnQuitToMenuClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandlePauseQuitToMenuClicked);
		PauseWidget->OnResumeClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandlePauseResumeClicked);
		PauseWidget->OnSettingsClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandlePauseSettingsClicked);
		PauseWidget->OnRestartMissionClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandlePauseRestartClicked);
		PauseWidget->OnQuitToDesktopClicked.RemoveDynamic(this, &AHawkeyePlayerController::HandlePauseQuitClicked);
		PauseWidget->RemoveFromParent();
		PauseWidget = nullptr;
	}
	bPauseMenuOpen = false;

	if (HudWidget)
	{
		HudWidget->RemoveFromParent();
		HudWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AHawkeyePlayerController::HandleFlashbackRequested(UFlashbackDefinition* Flashback)
{
	// This arrives in the same frame as OnMissionComplete, while the end card is still up, so
	// the definition is only cached here; the flow plays it when it reaches the Flashback beat.
	PendingFlashback = Flashback;
}

UFlashbackWidget* AHawkeyePlayerController::PlayFlashback(UFlashbackDefinition* Flashback)
{
	if (!Flashback)
	{
		return nullptr;
	}

	// Escape belongs to the slideshow from here until OnFlashbackFinished.
	bFlashbackActive = true;

	// Seen once is enough for the pause menu's replay list, whatever save it was in.
	if (UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(this))
	{
		Settings->MarkFlashbackSeen(FSoftObjectPath(Flashback));
	}

	if (!FlashbackWidgetClass)
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s has no FlashbackWidgetClass set; skipping the flashback."), *GetName());
		HandleFlashbackFinished(Flashback);
		return nullptr;
	}

	if (ActiveFlashbackWidget && ActiveFlashbackWidget->IsPlaying())
	{
		return ActiveFlashbackWidget;
	}

	// A flashback and the pause menu cannot share the screen; the slideshow wins.
	SetPauseMenuOpen(false);

	ActiveFlashbackWidget = CreateWidget<UFlashbackWidget>(this, FlashbackWidgetClass);
	if (!ActiveFlashbackWidget)
	{
		HandleFlashbackFinished(Flashback);
		return nullptr;
	}

	// The slideshow owns the screen while it plays.
	SetHudVisible(false);

	ActiveFlashbackWidget->OnFlashbackFinished.AddDynamic(this, &AHawkeyePlayerController::HandleFlashbackFinished);
	ActiveFlashbackWidget->Play(Flashback);

	return ActiveFlashbackWidget;
}

void AHawkeyePlayerController::HandleFlashbackFinished(UFlashbackDefinition* /*Flashback*/)
{
	if (ActiveFlashbackWidget)
	{
		ActiveFlashbackWidget->OnFlashbackFinished.RemoveDynamic(this, &AHawkeyePlayerController::HandleFlashbackFinished);
		ActiveFlashbackWidget = nullptr;
	}

	bFlashbackActive = false;
	PendingFlashback = nullptr;

	SetHudVisible(true);

	// A flashback played outside a mission end (a debug key, a scripted beat) just ends.
	if (MissionFlow && MissionFlow->GetStep() == EMissionFlowStep::Flashback)
	{
		MissionFlow->Advance();
		PerformCurrentFlowStep();
		return;
	}

	if (bOpenNextLevelAfterFlashback)
	{
		TryOpenNextLevel();
	}
}

void AHawkeyePlayerController::HandleMissionComplete(UMissionDefinition* Mission)
{
	if (!MissionFlow)
	{
		return;
	}

	// A flashback's playable scene: its mission done, the district comes back instead of an end card.
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this); Save && Save->IsInPlayableScene())
	{
		SetHudVisible(false);
		if (PlayerCameraManager)
		{
			PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.5f, FLinearColor::Black, false, true);
		}
		Save->ReturnFromPlayableScene();
		return;
	}

	if (MissionFlow->IsRunning())
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s: mission completed while an end sequence was already running; ignoring."), *GetName());
		return;
	}

	CompletedMission = Mission;

	// The end card owns the screen from here; the HUD comes back only if we stay in the level.
	SetHudVisible(false);
	SetPauseMenuOpen(false);

	SetPhoneOpen(false);
	MissionFlow->BeginRoute(MakeFlowRoute(Mission, bCloseUpActive));
	PerformCurrentFlowStep();
}

FMissionFlowRoute AHawkeyePlayerController::MakeFlowRoute(const UMissionDefinition* Mission, bool bCloseUp)
{
	FMissionFlowRoute Route;
	Route.bHasCloseUp = bCloseUp;
	if (!Mission)
	{
		return Route;
	}
	Route.bHasFlashback = !Mission->FlashbackToPlay.IsNull();
	const UFlashbackDefinition* Flashback = Route.bHasFlashback ? Mission->FlashbackToPlay.LoadSynchronous() : nullptr;
	Route.bHasPlayableScene = Flashback && Flashback->HasPlayableScene();
	Route.bHasNextLevel = !Mission->NextLevel.IsNull();
	Route.bReturnToRoaming = Mission->bReturnToRoamingAtEnd;
	return Route;
}

void AHawkeyePlayerController::PlayCloseUp(AActor* ViewTarget, float Seconds, float BlendSeconds)
{
	if (!ViewTarget || bCloseUpActive)
	{
		return;
	}
	bCloseUpActive = true;
	CloseUpTarget = ViewTarget;
	CloseUpBlendSeconds = FMath::Max(BlendSeconds, 0.f);
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	SetViewTargetWithBlend(ViewTarget, CloseUpBlendSeconds, VTBlend_EaseInOut, 2.f);
	if (AChapterEndInteractable* Examined = Cast<AChapterEndInteractable>(ViewTarget))
	{
		Examined->BeginPush();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: close-up on %s for %.1f s."), *GetName(), *ViewTarget->GetName(), Seconds);
	GetWorldTimerManager().SetTimer(CloseUpTimer, this, &AHawkeyePlayerController::EndCloseUpView, FMath::Max(Seconds, 0.1f), false);
}

void AHawkeyePlayerController::EndCloseUpView()
{
	if (APawn* ControlledPawn = GetPawn())
	{
		SetViewTargetWithBlend(ControlledPawn, CloseUpBlendSeconds, VTBlend_EaseInOut, 2.f);
	}
	if (CloseUpBlendSeconds <= 0.f)
	{
		FinishCloseUp();
		return;
	}
	GetWorldTimerManager().SetTimer(CloseUpTimer, this, &AHawkeyePlayerController::FinishCloseUp, CloseUpBlendSeconds, false);
}

void AHawkeyePlayerController::FinishCloseUp()
{
	if (!bCloseUpActive)
	{
		return;
	}
	bCloseUpActive = false;
	if (AChapterEndInteractable* Examined = Cast<AChapterEndInteractable>(CloseUpTarget))
	{
		Examined->EndPush();
	}
	CloseUpTarget = nullptr;
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
	if (MissionFlow && MissionFlow->GetStep() == EMissionFlowStep::CloseUp)
	{
		MissionFlow->Advance();
		PerformCurrentFlowStep();
	}
}

void AHawkeyePlayerController::ReturnToRoaming()
{
	HideEndCard();
	SetHudVisible(true);
	ApplyPauseInputMode(false);
	const UMissionDefinition* Mission = CompletedMission;
	const FText Toast = Mission && !Mission->ChapterCompleteToast.IsEmpty()
		? Mission->ChapterCompleteToast : NSLOCTEXT("Hawkeye", "ChapterCompleteToast", "[Chapter complete]");
	if (UHawkeyeObjectiveWidget* Toasts = HudWidget ? HudWidget->GetObjectiveMarker() : nullptr)
	{
		// A load back from a scene queues a stale "New objective"; the chapter's end outranks it.
		Toasts->ClearToasts();
		Toasts->PushToast(Toast, Mission ? Mission->MissionName : FText::GetEmpty(), EHawkeyeUISound::ObjectiveComplete);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: chapter complete; back to roaming."), *GetName());
}

void AHawkeyePlayerController::HandleReturnedFromScene()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	UMissionDefinition* Mission = Missions ? Missions->GetCurrentMission() : nullptr;
	if (!MissionFlow || !Mission)
	{
		return;
	}
	CompletedMission = Mission;
	FMissionFlowRoute Route = MakeFlowRoute(Mission, false);
	Route.bHasPlayableScene = true;
	MissionFlow->ResumeAt(Route, EMissionFlowStep::PlayableScene);
	MissionFlow->Advance();
	PerformCurrentFlowStep();
}

bool AHawkeyePlayerController::IsAutomationRun()
{
	if (bAutomationPlaysAsPlayer)
	{
		return false;
	}
	const TCHAR* CommandLine = FCommandLine::Get();
	return GIsAutomationTesting || FCString::Stristr(CommandLine, TEXT("RunTests")) != nullptr
		|| FParse::Param(CommandLine, TEXT("NoChapterOpening"));
}

void AHawkeyePlayerController::HandleMissionStartedForOpening(UMissionDefinition* Mission)
{
	if (!Mission || !IsLocalController() || IsAutomationRun())
	{
		return;
	}
	// A loaded game is past its opening (a save only exists after it), and so is a return from a scene.
	if (const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this); Save && (Save->IsLoading() || Save->GetSceneReturn().bReturnPending))
	{
		return;
	}
	if (bMainMenuOpen)
	{
		PendingOpening = Mission;
		return;
	}
	BeginChapterOpening(Mission);
}

void AHawkeyePlayerController::BeginChapterOpening(UMissionDefinition* Mission)
{
	if (!Mission)
	{
		return;
	}
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	const bool bSeen = Campaign && Campaign->SeenChapterTitles.Contains(Mission->GetFName());
	if (!bSeen && !Mission->OpeningTitle.IsEmpty())
	{
		ShowChapterTitle(Mission);
		if (Campaign)
		{
			Campaign->SeenChapterTitles.AddUnique(Mission->GetFName());
		}
	}
	if (UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this); Dialogue && !Mission->OpeningDialogueSequence.IsNone())
	{
		Dialogue->PlaySequence(Mission->OpeningDialogueSequence);
	}
}

UChapterTitleWidget* AHawkeyePlayerController::ShowChapterTitle(UMissionDefinition* Mission)
{
	if (!Mission || !IsLocalController())
	{
		return nullptr;
	}
	if (!ChapterTitleWidget)
	{
		ChapterTitleWidget = CreateWidget<UChapterTitleWidget>(
			this, ChapterTitleWidgetClass ? ChapterTitleWidgetClass.Get() : UChapterTitleWidget::StaticClass());
	}
	if (!ChapterTitleWidget)
	{
		return nullptr;
	}
	if (!ChapterTitleWidget->IsInViewport())
	{
		ChapterTitleWidget->AddToViewport(15);
	}
	ChapterTitleWidget->Play(Mission);
	return ChapterTitleWidget;
}

bool AHawkeyePlayerController::IsChapterTitleShowing() const
{
	return ChapterTitleWidget && ChapterTitleWidget->IsPlaying();
}

void AHawkeyePlayerController::PerformCurrentFlowStep()
{
	if (!MissionFlow)
	{
		return;
	}

	switch (MissionFlow->GetStep())
	{
	case EMissionFlowStep::CloseUp:
		// The close-up is already running; FinishCloseUp moves the flow on. If it ended in the
		// same frame, there is nothing to wait for.
		if (!bCloseUpActive)
		{
			MissionFlow->Advance();
			PerformCurrentFlowStep();
		}
		break;

	case EMissionFlowStep::EndCard:
		ShowEndCard(CompletedMission, /*bWaitForInput=*/false);
		break;

	case EMissionFlowStep::PlayableScene:
	{
		HideEndCard();
		UFlashbackDefinition* Flashback = CompletedMission ? CompletedMission->FlashbackToPlay.LoadSynchronous() : nullptr;
		UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
		if (!Save || !Save->EnterPlayableScene(Flashback))
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: the flashback's playable scene could not be opened."), *GetName());
			MissionFlow->Advance();
			PerformCurrentFlowStep();
		}
		break;
	}

	case EMissionFlowStep::ReturnToRoaming:
		ReturnToRoaming();
		MissionFlow->Advance();
		break;

	case EMissionFlowStep::Flashback:
	{
		HideEndCard();

		// PendingFlashback is what OnFlashbackRequested handed us; fall back to a synchronous
		// load so a mission that completes without the delegate still shows its slideshow.
		UFlashbackDefinition* Flashback = PendingFlashback;
		if (!Flashback && CompletedMission)
		{
			Flashback = CompletedMission->FlashbackToPlay.LoadSynchronous();
		}

		if (!Flashback)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: the mission's flashback could not be loaded."), *GetName());
			MissionFlow->Advance();
			PerformCurrentFlowStep();
			return;
		}

		PlayFlashback(Flashback);
		break;
	}

	case EMissionFlowStep::FinalCard:
		// No next level: the campaign is over, so hold the card until the player says go.
		ShowEndCard(CompletedMission, /*bWaitForInput=*/true);
		break;

	case EMissionFlowStep::OpenNextLevel:
		HideEndCard();
		if (!TryOpenNextLevel())
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: no next level to travel to after all."), *GetName());
		}
		MissionFlow->Advance();
		break;

	case EMissionFlowStep::OpenMenuLevel:
		HideEndCard();
		OpenMenuLevel();
		MissionFlow->Advance();
		break;

	default:
		break;
	}
}

UMissionEndCardWidget* AHawkeyePlayerController::ShowEndCard(UMissionDefinition* Mission, bool bWaitForInput)
{
	if (!EndCardWidgetClass || !IsLocalController())
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s has no EndCardWidgetClass set; skipping the end card."), *GetName());
		HandleEndCardFinished(Mission);
		return nullptr;
	}

	if (!EndCardWidget)
	{
		EndCardWidget = CreateWidget<UMissionEndCardWidget>(this, EndCardWidgetClass);
		if (!EndCardWidget)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: could not create the end card widget."), *GetName());
			HandleEndCardFinished(Mission);
			return nullptr;
		}

		EndCardWidget->OnEndCardFinished.AddDynamic(this, &AHawkeyePlayerController::HandleEndCardFinished);
	}

	if (!EndCardWidget->IsInViewport())
	{
		EndCardWidget->AddToViewport(5);
	}
	EndCardWidget->SetVisibility(ESlateVisibility::Visible);

	if (bWaitForInput)
	{
		EndCardWidget->PlayAndWaitForInput(Mission, FinalCardPrompt);
	}
	else
	{
		EndCardWidget->Play(Mission);
	}

	return EndCardWidget;
}

void AHawkeyePlayerController::HideEndCard()
{
	if (EndCardWidget)
	{
		EndCardWidget->RemoveFromParent();
	}
}

void AHawkeyePlayerController::HandleEndCardFinished(UMissionDefinition* /*Mission*/)
{
	if (!MissionFlow)
	{
		return;
	}

	MissionFlow->Advance();
	PerformCurrentFlowStep();
}

void AHawkeyePlayerController::OpenMenuLevel()
{
	// L_MainMenu is a stage-5 asset; until it exists, land somewhere that loads rather than
	// throwing the player at a map name the engine cannot resolve.
	FName Target = MenuLevelName;
	if (Target.IsNone() || !FPackageName::DoesPackageExist(Target.ToString()))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s does not exist; falling back to %s."),
			*GetName(), *Target.ToString(), *FallbackMenuLevelName.ToString());
		Target = FallbackMenuLevelName;
	}

	UE_LOG(LogHawkeye, Log, TEXT("Campaign over; returning to %s."), *Target.ToString());
	UGameplayStatics::OpenLevel(this, Target);
}

bool AHawkeyePlayerController::TryOpenNextLevel()
{
	const UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this);
	const UMissionDefinition* Mission = MissionSubsystem ? MissionSubsystem->GetCurrentMission() : nullptr;

	if (!Mission || Mission->NextLevel.IsNull())
	{
		return false;
	}

	UE_LOG(LogHawkeye, Log, TEXT("Travelling to next level: %s"), *Mission->NextLevel.ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, Mission->NextLevel);
	return true;
}

// --- Partner and switching ----------------------------------------------------------------------------

void AHawkeyePlayerController::Input_SwitchCharacter(const FInputActionValue& /*Value*/)
{
	SwitchCharacter();
}

void AHawkeyePlayerController::Input_PartnerMark(const FInputActionValue& /*Value*/)
{
	MarkPoint();
}

bool AHawkeyePlayerController::IsSwitchingAllowed() const
{
	if (bAllowSwitchingOverride)
	{
		return true;
	}
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionDefinition* Mission = Missions ? Missions->GetCurrentMission() : nullptr;
	return Mission && Mission->bAllowSwitching;
}

AHawkeyePartnerController* AHawkeyePlayerController::FindPartnerController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AHawkeyePartnerController* Any = nullptr;
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		if (!It->GetPartner())
		{
			continue;
		}
		if (It->GetLeader() == GetPawn())
		{
			return *It;
		}
		Any = Any ? Any : *It;
	}
	return Any;
}

FString AHawkeyePlayerController::GetSwitchRefusal() const
{
	if (!IsSwitchingAllowed())
	{
		return TEXT("this chapter does not allow switching");
	}
	const AHawkeyeCharacter* Current = Cast<AHawkeyeCharacter>(GetPawn());
	if (!Current)
	{
		return TEXT("no Hawkeye to switch from");
	}
	const AHawkeyePartnerController* Partner = FindPartnerController();
	const AHawkeyeCharacter* Other = Partner ? Partner->GetPartner() : nullptr;
	if (!Other)
	{
		return TEXT("no partner to switch to");
	}
	const FString Blocker = Current->GetSwitchBlocker();
	if (!Blocker.IsEmpty())
	{
		return FString::Printf(TEXT("%s is %s"), *Current->GetCharacterName().ToString(), *Blocker);
	}
	const FString OtherBlocker = Other->GetSwitchBlocker();
	if (!OtherBlocker.IsEmpty())
	{
		return FString::Printf(TEXT("%s is %s"), *Other->GetCharacterName().ToString(), *OtherBlocker);
	}
	return FString();
}

bool AHawkeyePlayerController::SwitchCharacter()
{
	const FString Refusal = GetSwitchRefusal();
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: switch refused: %s."), *GetName(), *Refusal);
		return false;
	}

	AHawkeyeCharacter* Previous = Cast<AHawkeyeCharacter>(GetPawn());
	AHawkeyePartnerController* Partner = FindPartnerController();
	AHawkeyeCharacter* Next = Partner->GetPartner();
	const FRotator View = GetControlRotation();

	// Whatever the thumbs were holding on the old body is let go: no sprint or draw carries over.
	Previous->ReleaseHeldInputs();
	Partner->UnPossess();
	Possess(Next);
	SetControlRotation(FRotator(View.Pitch, View.Yaw, 0.f));
	Partner->Possess(Previous);
	Partner->SetLeader(Next);

	// Possess snapped the view to the new pawn; start from the old one's camera and blend across.
	if (SwitchBlendSeconds > 0.f && IsLocalController())
	{
		SetViewTarget(Previous);
		SetViewTargetWithBlend(Next, SwitchBlendSeconds, VTBlend_Cubic);
	}

	ActiveCharacterName = Next->GetCharacterName();
	if (HudWidget)
	{
		HudWidget->RebindToPawn();
		HudWidget->SetCharacterName(ActiveCharacterName);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: switched from %s to %s; %s now follows."), *GetName(),
		*Previous->GetCharacterName().ToString(), *ActiveCharacterName.ToString(), *Previous->GetCharacterName().ToString());
	return true;
}

FText AHawkeyePlayerController::GetHudCharacterName() const
{
	if (!ActiveCharacterName.IsEmpty())
	{
		return ActiveCharacterName;
	}
	const AHawkeyeCharacter* Current = Cast<AHawkeyeCharacter>(GetPawn());
	return Current ? Current->GetCharacterName() : FText::GetEmpty();
}

bool AHawkeyePlayerController::MarkPoint()
{
	AHawkeyePartnerController* Partner = FindPartnerController();
	UWorld* World = GetWorld();
	if (!Partner || !World)
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeMarkPoint), false, GetPawn());
	Params.AddIgnoredActor(Partner->GetPawn());
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * MarkTraceDistance,
			ECC_Visibility, Params))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: nothing under the view to mark."), *GetName());
		return false;
	}
	FVector Point = Hit.ImpactPoint;
	FNavLocation OnNav;
	if (const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
		Nav && Nav->ProjectPointToNavigation(Point, OnNav, FVector(200.f, 200.f, 300.f)))
	{
		Point = OnNav.Location;
	}
	Partner->CommandMoveTo(Point);
	return true;
}

// --- Audio ---------------------------------------------------------------------------------------

void AHawkeyePlayerController::PlayUISound(EHawkeyeUISound Sound)
{
	const TSoftObjectPtr<USoundBase>* Chosen = nullptr;
	const TCHAR* Event = TEXT("ui");
	switch (Sound)
	{
	case EHawkeyeUISound::Hover:
		Chosen = &UIHoverSound;
		Event = TEXT("ui hover");
		break;
	case EHawkeyeUISound::Click:
		Chosen = &UIClickSound;
		Event = TEXT("ui click");
		break;
	case EHawkeyeUISound::ObjectiveComplete:
		Chosen = &ObjectiveCompleteSound;
		Event = TEXT("objective complete");
		break;
	case EHawkeyeUISound::NewObjective:
		Chosen = &NewObjectiveSound;
		Event = TEXT("new objective");
		break;
	default:
		Chosen = &ToastSound;
		Event = TEXT("toast");
		break;
	}
	UHawkeyeAudioSubsystem::Play2D(this, *Chosen, Event);
}

void AHawkeyePlayerController::PopVolumeMix()
{
	if (!PushedMix)
	{
		return;
	}
	for (USoundClass* Class : OverriddenClasses)
	{
		UGameplayStatics::ClearSoundMixClassOverride(this, PushedMix, Class, 0.f);
	}
	UGameplayStatics::PopSoundMixModifier(this, PushedMix);
	PushedMix = nullptr;
	OverriddenClasses.Reset();
}

void AHawkeyePlayerController::HandleSettingsChanged(FHawkeyeSettings Settings)
{
	ApplyVolumeSettings(Settings);
}

void AHawkeyePlayerController::ApplyVolumeSettings(const FHawkeyeSettings& Settings)
{
	AppliedVolumes = HawkeyeAudioMath::ComputeClassVolumes(Settings);
	USoundMix* Mix = PushedMix ? PushedMix.Get() : VolumeMix.LoadSynchronous();
	UHawkeyeVolumeSubsystem* Keeper = UHawkeyeVolumeSubsystem::Get(this);
	if (!Mix || !Keeper)
	{
		// No mix set (tests), or no game instance to keep it alive under the audio device.
		return;
	}
	Keeper->Hold(Mix);
	// Each class gets its full product, not applied to children: the class tree must not multiply
	// the master in a second time.
	const TPair<const TSoftObjectPtr<USoundClass>*, float> Classes[] = {
		{ &MasterSoundClass, AppliedVolumes.Master },
		{ &SfxSoundClass, AppliedVolumes.Sfx },
		{ &AmbientSoundClass, AppliedVolumes.Ambient },
		{ &UISoundClass, AppliedVolumes.UI },
		{ &MusicSoundClass, AppliedVolumes.Music },
	};
	for (const TPair<const TSoftObjectPtr<USoundClass>*, float>& Entry : Classes)
	{
		if (USoundClass* Class = Entry.Key->LoadSynchronous())
		{
			Keeper->Hold(Class);
			OverriddenClasses.AddUnique(Class);
			UGameplayStatics::SetSoundMixClassOverride(this, Mix, Class, Entry.Value, 1.f, 0.f, false);
		}
	}
	if (!PushedMix)
	{
		PushedMix = Mix;
		UGameplayStatics::PushSoundMixModifier(this, Mix);
	}
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: volumes master %.2f, sfx %.2f, ambient %.2f, ui %.2f, music %.2f."), *GetName(),
		AppliedVolumes.Master, AppliedVolumes.Sfx, AppliedVolumes.Ambient, AppliedVolumes.UI, AppliedVolumes.Music);
}

// --- Playtest capture ----------------------------------------------------------------------------

bool AHawkeyePlayerController::HandlePlaytestKey(const FInputKeyEventArgs& Params)
{
	const double Now = FPlatformTime::Seconds();
	if (Params.Key == EKeys::F12)
	{
		if (Params.Event == IE_Pressed)
		{
			TakePlaytestNote();
			NoteKeyDownSeconds = Now;
			bVerboseBumpStarted = false;
		}
		else if (Params.Event == IE_Released)
		{
			NoteKeyDownSeconds = -1.0;
		}
		return true;
	}
	if (Params.Key == EKeys::F11)
	{
		if (Params.Event == IE_Pressed)
		{
			EnterPhotoMode();
		}
		return true;
	}
	// Menu on a pad in play: held back from IA_Pause so a hold can be a note. A tap pauses on release. Under
	// a menu (the world paused) it passes through as before, to close it.
	if (Params.Key == EKeys::Gamepad_Special_Right)
	{
		if (Params.Event == IE_Pressed && !IsPaused())
		{
			MenuHold.HoldSeconds = NoteHoldSeconds;
			MenuHold.Press(Now);
			NoteKeyDownSeconds = Now;
			bVerboseBumpStarted = false;
			return true;
		}
		if (MenuHold.IsDown() && Params.Event == IE_Released)
		{
			NoteKeyDownSeconds = -1.0;
			switch (MenuHold.Release(Now))
			{
			case EHawkeyeTapHold::Tap:
				Input_Pause(FInputActionValue());
				break;
			case EHawkeyeTapHold::Hold:
				TakePlaytestNote();
				break;
			default:
				break;
			}
			return true;
		}
		if (MenuHold.IsDown())
		{
			return true;
		}
	}
	return false;
}

void AHawkeyePlayerController::TickPlaytestKeys(double NowSeconds)
{
	if (MenuHold.Tick(NowSeconds) == EHawkeyeTapHold::Hold)
	{
		TakePlaytestNote();
	}
	if (NoteKeyDownSeconds >= 0.0 && !bVerboseBumpStarted && NowSeconds - NoteKeyDownSeconds >= VerboseBumpHoldSeconds)
	{
		bVerboseBumpStarted = true;
		if (UPlaytestSubsystem* Playtest = UPlaytestSubsystem::Get(this))
		{
			Playtest->StartVerboseBump(VerboseBumpSeconds);
		}
	}
}

int32 AHawkeyePlayerController::TakePlaytestNote()
{
	UPlaytestSubsystem* Playtest = UPlaytestSubsystem::Get(this);
	if (!Playtest || !IsLocalController())
	{
		return 0;
	}
	TWeakObjectPtr<AHawkeyePlayerController> WeakThis(this);
	return Playtest->TakeNote(this, [WeakThis](int32 Index, bool bShotOk)
	{
		if (AHawkeyePlayerController* Self = WeakThis.Get())
		{
			Self->PushHudToast(FText::GetEmpty(), FText::Format(bShotOk
				? NSLOCTEXT("Hawkeye", "PlaytestNoteSaved", "Note {0} saved")
				: NSLOCTEXT("Hawkeye", "PlaytestNoteNoShot", "Note {0} saved (no screenshot)"), FText::AsNumber(Index)));
		}
	});
}

bool AHawkeyePlayerController::IsPhotoModeActive() const
{
	return PhotoMode && PhotoMode->IsActive();
}

FString AHawkeyePlayerController::GetPhotoModeRefusal(bool bFromPauseMenu) const
{
	if (!IsLocalController())
	{
		return TEXT("not a local player");
	}
	if (IsPhotoModeActive())
	{
		return TEXT("already on");
	}
	if (bFlashbackActive)
	{
		return TEXT("a flashback");
	}
	const bool bPauseMenuOnly = bFromPauseMenu && bPauseMenuOpen && !bSettingsOpen && !bFlashbackReplayOpen;
	if ((IsPaused() && !bPauseMenuOnly) || (bPauseMenuOpen && !bPauseMenuOnly) || bMainMenuOpen || bDifficultyPromptOpen
		|| bSafehouseMenuOpen || bChallengeResultsOpen || bInventoryOpen || bPhoneOpen || bMapOpen)
	{
		return TEXT("a menu is open");
	}
	if (bCloseUpActive || (MissionFlow && MissionFlow->IsRunning()) || IsChapterTitleShowing())
	{
		return TEXT("a chapter beat");
	}
	if (const AHawkeyeGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AHawkeyeGameMode>() : nullptr;
		GameMode && GameMode->IsRestartPending())
	{
		return TEXT("reloading");
	}
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return TEXT("no pawn");
	}
	const UHealthComponent* Health = ControlledPawn->FindComponentByClass<UHealthComponent>();
	if (Health && !Health->IsAlive())
	{
		return TEXT("dead");
	}
	if (const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(ControlledPawn))
	{
		if (Kate->IsDowned())
		{
			return TEXT("downed");
		}
		if (Kate->IsQuiverWheelOpen())
		{
			return TEXT("the quiver wheel is open");
		}
	}
	return FString();
}

bool AHawkeyePlayerController::EnterPhotoMode()
{
	const FString Refusal = GetPhotoModeRefusal();
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogHawkeye, Log, TEXT("Photo mode: not now (%s)."), *Refusal);
		if (!IsPhotoModeActive())
		{
			PushHudToast(FText::GetEmpty(), NSLOCTEXT("Hawkeye", "PhotoModeRefused", "Photo mode is not available right now"));
		}
		return false;
	}
	if (!PhotoMode)
	{
		PhotoMode = NewObject<UPlaytestPhotoMode>(this, TEXT("PhotoMode"));
	}
	return PhotoMode->Enter(this);
}

void AHawkeyePlayerController::ExitPhotoMode()
{
	if (PhotoMode)
	{
		PhotoMode->Exit();
	}
}

void AHawkeyePlayerController::HandlePausePhotoModeClicked()
{
	const FString Refusal = GetPhotoModeRefusal(/*bFromPauseMenu=*/true);
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogHawkeye, Log, TEXT("Photo mode: not now (%s)."), *Refusal);
		return;
	}
	// The menu goes first (and the world unpauses for a moment); photo mode pauses it again the same frame.
	SetPauseMenuOpen(false);
	EnterPhotoMode();
}
