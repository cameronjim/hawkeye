#include "Camera/CameraComponent.h"
#include "Combat/BowComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Dialogue/DialogueQueue.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Boss/BossFightSubsystem.h"
#include "Phone/PhoneInbox.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/ChapterEndInteractable.h"
#include "World/InteractionComponent.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Lap.ReindeerGames: chapter 2, "Reindeer Games", played by a script from the chapter's start, in the standalone
 * game like the other laps:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -Difficulty=Normal -ExecCmds="Automation RunTests Hawkeye.Lap.ReindeerGames; Quit"
 *
 * The district loads on chapter 1; the lap starts DA_CH02_ReindeerGames the way chapter select does and its opening
 * (UChapterSiteSubsystem::BeginChapter puts Kate on the site's start, the district's PlayerStart). She runs the
 * navmesh to the toy drive on Avenue B, stopping on the way to examine what the police are standing around (the
 * optional, unmarked courier: no toast, the campaign flag up); arriving wakes the reindeer crew and unlocks the foam
 * arrow. She fights as a player who has learnt the melee (parries a wind-up she can see, finishes anyone open, steps
 * out of a swing about to land, shoots the gunner, one foam arrow early). With the crew down and one sack carried the
 * campaign is saved and loaded (its own slot) and must come back the same: objectives, the flag, the events fired (no
 * line said again), the foam in the quiver, the sack carried, no crew, two sacks on the ground. Then the other sacks,
 * the table, the end card, the "Visiting hours" slides, the texts on the phone; then chapter 4 is started the way
 * chapter select does, and must come up clean (no site, its quiver with the foam and chapter 3's boomerangs, Tomas
 * waiting).
 *
 * Writes Saved/Automation/reindeer_games_lap.json and Saved/Screenshots/ReindeerGames/reindeer_*.png (the site, the
 * courier examine, the crew, the foam unlock, a sack, the toy return, the end card, a slide, the phone).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapReindeerGames, "Hawkeye.Lap.ReindeerGames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeReindeerLap
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* InteractPath = TEXT("/Game/Input/IA_Interact.IA_Interact");
	static const TCHAR* Chapter = TEXT("DA_CH02_ReindeerGames");
	static const TCHAR* NextChapter = TEXT("DA_CH04_SocialClub");
	static const TCHAR* FollowingChapter = TEXT("DA_CH03_Grotto");
	static constexpr int32 SlotBoomerang = 4;
	static const TCHAR* FirstChapter = TEXT("DA_CH01_NightShift");
	static const TCHAR* Slot = TEXT("HawkeyeReindeerGamesLap");
	static const FName Flag(TEXT("saw_courier"));
	static constexpr int32 SlotStandard = 1;
	static constexpr int32 SlotFoam = 3;
	static constexpr float FullDraw = 0.82f;

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

	static UEnhancedInputLocalPlayerSubsystem* InputOf(APlayerController* PC)
	{
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		return Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	}

	static void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = LoadObject<UInputAction>(nullptr, Path);
		if (Input && A)
		{
			Input->InjectInputForAction(A, FInputActionValue(true), {}, {});
		}
	}

	static void Hold(APlayerController* PC, const TCHAR* Path, bool bHold)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = LoadObject<UInputAction>(nullptr, Path);
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

	static FVector2D MoveTowards(const FVector& WorldDirection, float CameraYaw)
	{
		const float Angle = FMath::DegreesToRadians(WorldDirection.Rotation().Yaw - CameraYaw);
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
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

	static bool Sees(UWorld* World, const AHawkeyeCharacter* Kate, const AActor* Target)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ReindeerLapSight), false, Kate);
		Params.AddIgnoredActor(Target);
		return !World->LineTraceTestByChannel(Kate->GetActorLocation() + FVector(0.f, 0.f, 40.f),
			Target->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params);
	}

	static FVector SteerTowards(UWorld* World, const AHawkeyeCharacter* Kate, const FVector& Goal)
	{
		const FVector Here = Kate->GetActorLocation();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, Here, Goal, const_cast<AHawkeyeCharacter*>(Kate)) : nullptr;
		if (Path && Path->IsValid())
		{
			for (const FVector& Point : Path->PathPoints)
			{
				if (FVector::Dist2D(Point, Here) > 150.f)
				{
					return (Point - Here).GetSafeNormal2D();
				}
			}
		}
		return (Goal - Here).GetSafeNormal2D();
	}

	static FString ShotPath(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("ReindeerGames") / Name);
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	static FString Escape(const FString& In)
	{
		return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	static bool IsDone(const UWorld* World, FName Id)
	{
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionObjective* Objective = Missions && Missions->GetTracker() ? Missions->GetTracker()->FindObjective(Id) : nullptr;
		return Objective && Objective->IsCompleted();
	}
}

/** The chapter, played to the end. */
class FHawkeyeReindeerLap : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeReindeerLap(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual bool Update() override;

private:
	enum class EStep : uint8 { SetUp, ToCourier, Examine, ToDrive, Fight, RoundTrip, Reloading, Sacks, Table, Ending, Phone, NextChapter, Done };

	bool SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void Enter(EStep InStep, double Now);
	void Move(APlayerController* PC, const FVector2D& Value);
	void Sprint(APlayerController* PC, bool bOn);
	void RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint);
	void ReleaseInputs(APlayerController* PC);
	void Shot(const TCHAR* Name);
	void Fail(const FString& Why);
	void FightStep(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	bool UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void BeginDraw(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, int32 DrawSlot, float Seconds, double Now);
	bool Interact(APlayerController* PC, AHawkeyeCharacter* Kate, const AActor* Target);
	void NoteToasts(AHawkeyeCharacter* Kate);
	void CheckRoundTrip(UWorld* World, AHawkeyeCharacter* Kate);
	void Report(UWorld* World);

	FAutomationTestBase* Test;
	EStep Step = EStep::SetUp;
	double StepSince = 0.0;
	double WallStepSince = 0.0;
	double LapStart = -1.0;
	double WallStart = 0.0;
	TWeakObjectPtr<UWorld> LapWorld;
	TMap<FString, double> Marks;
	TArray<FString> Timeline;
	TArray<FString> Shots;
	TArray<FString> Failures;

	bool bMoveInjecting = false;
	bool bSprinting = false;
	bool bMeleeHeld = false;
	double MeleeHeldSince = 0.0;
	double NextActionAt = 0.0;
	double LastParryTap = -10.0;
	float LastHealth = 0.f;
	int32 HitsTaken = 0;
	float DamageTaken = 0.f;
	int32 Strikes = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 Dodges = 0;
	int32 ArrowsLoosed = 0;
	int32 FoamLoosed = 0;
	int32 Deaths = 0;

	bool bDrawing = false;
	double DrawStart = 0.0;
	float DrawFor = HawkeyeReindeerLap::FullDraw;
	int32 DrawSlot = 1;
	TWeakObjectPtr<AThugCharacter> DrawTarget;
	double SlotResetAt = -1.0;

	TWeakObjectPtr<AChapterEndInteractable> Courier;
	TWeakObjectPtr<AChapterEndInteractable> Table;
	TWeakObjectPtr<APickupActor> SackTarget;
	double InteractPressedAt = -10.0;
	int32 InteractPresses = 0;
	TArray<FString> ToastsSeen;
	FString FightTimes;

	// The round trip.
	FVector SavedKate = FVector::ZeroVector;
	TArray<FName> SavedObjectives;
	TArray<FName> SavedFired;
	TSet<FName> SavedKeycards;
	int32 SavedFoam = 0;
	int32 SavedSacksOnGround = 0;
	double ReloadStartedAt = 0.0;
	double ReloadSeconds = 0.0;
	bool bRoundTripChecked = false;
	FString RoundTrip;

	double SitePauseAt = -1.0;
	double SackPauseAt = -1.0;
	bool bShotSite = false;
	bool bShotCourier = false;
	bool bShotCrew = false;
	bool bShotFoam = false;
	bool bShotSack = false;
	bool bShotReturn = false;
	bool bShotEnd = false;
	bool bShotSlide = false;
	bool bShotPhone = false;
	bool bFlashbackSeen = false;
	bool bCourierToast = false;
	FString NextChapterCheck;
};

void FHawkeyeReindeerLap::Move(APlayerController* PC, const FVector2D& Value)
{
	UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeReindeerLap::InputOf(PC);
	const UInputAction* A = LoadObject<UInputAction>(nullptr, HawkeyeReindeerLap::MovePath);
	if (!Input || !A)
	{
		return;
	}
	if (Value.IsNearlyZero())
	{
		if (bMoveInjecting)
		{
			Input->StopContinuousInputInjectionForAction(A);
			bMoveInjecting = false;
		}
		return;
	}
	if (!bMoveInjecting)
	{
		Input->StartContinuousInputInjectionForAction(A, FInputActionValue(Value), {}, {});
		bMoveInjecting = true;
		return;
	}
	Input->UpdateValueOfContinuousInputInjectionForAction(A, FInputActionValue(Value));
}

void FHawkeyeReindeerLap::Sprint(APlayerController* PC, bool bOn)
{
	if (bOn != bSprinting)
	{
		HawkeyeReindeerLap::Hold(PC, HawkeyeReindeerLap::SprintPath, bOn);
		bSprinting = bOn;
	}
}

void FHawkeyeReindeerLap::RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint)
{
	Sprint(PC, bSprint);
	const FVector To = HawkeyeReindeerLap::SteerTowards(World, Kate, Goal);
	Move(PC, HawkeyeReindeerLap::MoveTowards(To, PC->GetControlRotation().Yaw));
}

void FHawkeyeReindeerLap::ReleaseInputs(APlayerController* PC)
{
	Move(PC, FVector2D::ZeroVector);
	Sprint(PC, false);
	if (bMeleeHeld)
	{
		HawkeyeReindeerLap::Hold(PC, HawkeyeReindeerLap::MeleePath, false);
		bMeleeHeld = false;
	}
	if (bDrawing)
	{
		HawkeyeReindeerLap::Hold(PC, HawkeyeReindeerLap::FirePath, false);
		bDrawing = false;
	}
}

void FHawkeyeReindeerLap::Shot(const TCHAR* Name)
{
	HawkeyeShots::Request(Test, HawkeyeReindeerLap::ShotPath(Name));
	Shots.Add(Name);
}

void FHawkeyeReindeerLap::Fail(const FString& Why)
{
	Failures.Add(Why);
	Test->AddError(TEXT("Reindeer games lap: ") + Why);
}

void FHawkeyeReindeerLap::Enter(EStep InStep, double Now)
{
	Step = InStep;
	StepSince = Now;
	WallStepSince = FPlatformTime::Seconds();
}

void FHawkeyeReindeerLap::NoteToasts(AHawkeyeCharacter* Kate)
{
	const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Kate);
	const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
	if (!Toasts || !Toasts->IsToastVisible())
	{
		return;
	}
	const FString Line = Toasts->GetToastHeading().ToString() + TEXT(" / ") + Toasts->GetToastTitle().ToString();
	if (ToastsSeen.Num() == 0 || ToastsSeen.Last() != Line)
	{
		ToastsSeen.Add(Line);
		bCourierToast |= Line.Contains(TEXT("police are standing around"));
	}
}

bool FHawkeyeReindeerLap::Interact(APlayerController* PC, AHawkeyeCharacter* Kate, const AActor* Target)
{
	const double Wall = FPlatformTime::Seconds();
	const UInteractionComponent* Interaction = Kate->GetInteractionComponent();
	if (!Interaction || Interaction->GetFocusedActor() != Target || Wall - InteractPressedAt < 0.6)
	{
		return false;
	}
	InteractPressedAt = Wall;
	++InteractPresses;
	HawkeyeReindeerLap::Tap(PC, HawkeyeReindeerLap::InteractPath);
	return true;
}

bool FHawkeyeReindeerLap::SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeReindeerLap;
	AHawkeyeGameMode* Mode = World->GetAuthGameMode<AHawkeyeGameMode>();
	UMissionDefinition* Mission = AHawkeyeGameMode::FindChapter(Chapter);
	UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	if (!Mode || !Mission || !Site || !Save)
	{
		Fail(TEXT("no game mode, chapter 2, site subsystem or save subsystem"));
		return false;
	}
	// The campaign's order: 1, 2, 3, 4.
	const UMissionDefinition* First = AHawkeyeGameMode::FindChapter(FirstChapter);
	Test->TestTrue(TEXT("Chapter 1 leads to chapter 2"), First && First->NextChapter.ToSoftObjectPath() == FSoftObjectPath(Mission));
	Test->TestTrue(TEXT("Chapter 2 leads to chapter 3"), Mission->NextChapter.ToSoftObjectPath().GetAssetName() == FollowingChapter);
	Test->TestFalse(TEXT("No partner in chapter 2"), Mission->bPartnerPresent);
	Save->SlotNameOverride = Slot;
	IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
	if (UHawkeyeCampaignState* Campaign = Save->GetCampaignState())
	{
		Campaign->CampaignFlags.Remove(Flag);
	}
	HawkeyeFreezePartner(World);
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
	}
	if (!Mode->StartChapter(Mission))
	{
		Fail(TEXT("chapter 2 would not start"));
		return false;
	}
	Site->BeginChapter(Mission);
	Test->TestEqual(TEXT("The site's crew: five reindeer"), Site->GetCrew().Num(), 5);
	Test->TestEqual(TEXT("Three sacks"), Site->GetPickups().Num(), 3);
	Test->TestEqual(TEXT("The table and the courier"), Site->GetExamines().Num(), 2);
	Test->TestEqual(TEXT("The arrival volume"), Site->GetVolumes().Num(), 1);
	Test->TestFalse(TEXT("The crew waits"), Site->IsCrewAlerted());
	for (AChapterEndInteractable* Examine : Site->GetExamines())
	{
		(Examine->ObjectiveId == TEXT("return_toys") ? Table : Courier) = Examine;
	}
	for (const AThugCharacter* Thug : Site->GetCrew())
	{
		Test->TestTrue(TEXT("Every one of the crew wears the antlers"), Thug->GetHeadwearComponent() && Thug->GetHeadwearComponent()->GetStaticMesh());
	}
	const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	Test->TestTrue(TEXT("Chapter 2 starts with the bow"), Inventory->HasBow());
	Test->TestEqual(TEXT("...and no foam yet"), Inventory->GetArrowCount(SlotFoam), 0);
	Test->TestTrue(TEXT("Kate at the site's start"), FVector::Dist2D(Kate->GetActorLocation(), Site->GetDefinition()->PlayerStart) < 200.f);
	Kate->GetInventoryComponent()->SelectArrowSlot(SlotStandard);
	LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
	LapStart = Now;
	WallStart = FPlatformTime::Seconds();
	LapWorld = World;
	Marks.Add(TEXT("start"), 0.0);
	Test->AddInfo(FString::Printf(TEXT("Reindeer games lap: Kate at %s; the table %.0f m away, the courier %.0f m."), *Kate->GetActorLocation().ToCompactString(),
		Table.IsValid() ? FVector::Dist2D(Kate->GetActorLocation(), Table->GetActorLocation()) / 100.f : -1.f,
		Courier.IsValid() ? FVector::Dist2D(Kate->GetActorLocation(), Courier->GetActorLocation()) / 100.f : -1.f));
	return Table.IsValid() && Courier.IsValid();
}

void FHawkeyeReindeerLap::BeginDraw(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, int32 InSlot, float Seconds, double Now)
{
	Move(PC, FVector2D::ZeroVector);
	Sprint(PC, false);
	Kate->GetInventoryComponent()->SelectArrowSlot(InSlot);
	DrawSlot = InSlot;
	DrawTarget = Target;
	DrawFor = Seconds;
	DrawStart = Now;
	bDrawing = true;
	HawkeyeReindeerLap::Hold(PC, HawkeyeReindeerLap::FirePath, true);
}

bool FHawkeyeReindeerLap::UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeReindeerLap;
	if (!bDrawing)
	{
		if (SlotResetAt > 0.0 && Now >= SlotResetAt)
		{
			SlotResetAt = -1.0;
			if (UBowComponent* Bow = Kate->GetBowComponent())
			{
				Bow->ClearAimOverride();
			}
			Kate->GetInventoryComponent()->SelectArrowSlot(SlotStandard);
		}
		return SlotResetAt > 0.0;
	}
	AThugCharacter* Target = DrawTarget.Get();
	UBowComponent* Bow = Kate->GetBowComponent();
	if (Target && Bow)
	{
		const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		AimAt(PC, Kate, Chest);
		Bow->SetAimOverride(AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Chest, Target->GetVelocity(),
			6000.f * FMath::Max(Bow->GetDrawFraction(), 0.4f), World->GetGravityZ()));
	}
	const bool bLoose = Now - DrawStart >= DrawFor;
	if (bLoose || IsDown(Target) || Kate->IsStaggered())
	{
		Hold(PC, FirePath, false);
		if (bLoose && !IsDown(Target))
		{
			++ArrowsLoosed;
			FoamLoosed += DrawSlot == SlotFoam ? 1 : 0;
			Timeline.Add(FString::Printf(TEXT("%.1f %s arrow at %s"), Now - LapStart, DrawSlot == SlotFoam ? TEXT("foam") : TEXT("standard"),
				*GetNameSafe(Target)));
		}
		else if (Bow)
		{
			Bow->CancelDraw();
		}
		bDrawing = false;
		NextActionAt = Now + 0.15;
		SlotResetAt = Now + 0.25;
	}
	return true;
}

void FHawkeyeReindeerLap::FightStep(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeReindeerLap;
	const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
	const TArray<AThugCharacter*> Crew = Site ? Site->GetCrew() : TArray<AThugCharacter*>();
	AThugCharacter* Target = nullptr;
	AThugCharacter* Ranged = nullptr;
	float Distance = BIG_NUMBER;
	float RangedDistance = BIG_NUMBER;
	for (AThugCharacter* Thug : Crew)
	{
		if (IsDown(Thug))
		{
			continue;
		}
		const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
		if (Thug->IsGunner() || Thug->IsArcher())
		{
			if (D < RangedDistance)
			{
				RangedDistance = D;
				Ranged = Thug;
			}
		}
		else if (D < Distance)
		{
			Distance = D;
			Target = Thug;
		}
	}
	if (Ranged && (!Target || RangedDistance < 190.f || (Distance > 700.f && RangedDistance < Distance)))
	{
		Target = Ranged;
		Distance = RangedDistance;
	}
	if (!Target)
	{
		Move(PC, FVector2D::ZeroVector);
		return;
	}
	if (bMeleeHeld)
	{
		if (Now - MeleeHeldSince >= 0.45)
		{
			Hold(PC, MeleePath, false);
			bMeleeHeld = false;
			NextActionAt = Now + 0.1;
		}
		return;
	}
	if (UpdateDraw(World, PC, Kate, Now))
	{
		return;
	}
	AimAt(PC, Kate, Target->GetActorLocation() + FVector(0.f, 0.f, 30.f));
	// A wind-up she has seen for 0.2 s in front of her: parry.
	const FVector Forward = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
	const bool bCanParry = Kate->GetMeleeComponent() && !Kate->GetMeleeComponent()->IsWindingUp() && !Kate->IsDodging() && !Kate->IsStaggered()
		&& !Kate->IsDrawingBow() && !Kate->IsLockedOutByTakedown();
	if (Now - LastParryTap > 0.3 && bCanParry)
	{
		for (AThugCharacter* Thug : Crew)
		{
			const EHawkeyeParryKind Kind = !IsDown(Thug) ? UHawkeyeMeleeRules::ClassifyParry(Thug) : EHawkeyeParryKind::None;
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			if (Kind != EHawkeyeParryKind::None
				&& UHawkeyeMeleeRules::IsInFrontWithin(Kate->GetActorLocation(), Forward, Thug->GetActorLocation(), 240.f, 65.f)
				&& (Kind == EHawkeyeParryKind::Burst || (Melee && Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining() >= 0.2f)))
			{
				Move(PC, FVector2D::ZeroVector);
				Tap(PC, MeleePath);
				LastParryTap = Now;
				++Parries;
				NextActionAt = Now + 0.25;
				return;
			}
		}
	}
	const bool bBusy = Kate->IsMeleeAttacking() || Kate->IsDodging() || Kate->IsStaggered() || Kate->IsLockedOutByTakedown();
	if (Now < NextActionAt || bBusy)
	{
		return;
	}
	if (Kate->GetFinisherComponent() && Kate->GetFinisherComponent()->FindTarget())
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, TakedownPath);
		++Finishers;
		NextActionAt = Now + 0.3;
		return;
	}
	if (Kate->GetDodgeCooldownRemaining() <= 0.f)
	{
		for (AThugCharacter* Thug : Crew)
		{
			const UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
			if (!IsDown(Thug) && Melee && Melee->IsWindingUp() && Melee->GetPhaseRemaining() < 0.3f && !Thug->IsIncapacitated()
				&& FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation()) <= 250.f && Thug != Target)
			{
				const FVector Away = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
				if (Kate->TryDodge((Away + FVector::CrossProduct(FVector::UpVector, Away)).GetSafeNormal2D()))
				{
					++Dodges;
					NextActionAt = Now + 0.3;
					return;
				}
			}
		}
	}
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	// One foam arrow early, at someone 4 to 15 m off: "Stay."
	if (FoamLoosed == 0 && Inventory->GetArrowCount(SlotFoam) > 0 && !Target->IsGunner())
	{
		for (AThugCharacter* Thug : Crew)
		{
			const float D = !IsDown(Thug) ? FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation()) : 0.f;
			if (D >= 400.f && D <= 1500.f && Sees(World, Kate, Thug) && Distance > 300.f)
			{
				BeginDraw(PC, Kate, Thug, SlotFoam, FullDraw, Now);
				return;
			}
		}
	}
	if (Distance > 1500.f)
	{
		RunTo(World, PC, Kate, Target->GetActorLocation(), true);
		return;
	}
	Sprint(PC, false);
	if (Distance <= 190.f)
	{
		Move(PC, FVector2D::ZeroVector);
		const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
		const bool bLight = (ThugMelee && ThugMelee->IsWindingUp()) || Target->IsKnockedDown() || Target->IsStaggered() || Strikes % 3 != 2;
		++Strikes;
		if (bLight)
		{
			Tap(PC, MeleePath);
			NextActionAt = Now + 0.32;
			return;
		}
		Hold(PC, MeleePath, true);
		bMeleeHeld = true;
		MeleeHeldSince = Now;
		return;
	}
	if (Target->IsGunner() && Distance >= 300.f && Sees(World, Kate, Target) && Inventory->GetArrowCount(SlotStandard) > 0)
	{
		BeginDraw(PC, Kate, Target, SlotStandard, FullDraw, Now);
		return;
	}
	RunTo(World, PC, Kate, Target->GetActorLocation(), false);
}

void FHawkeyeReindeerLap::CheckRoundTrip(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeReindeerLap;
	bRoundTripChecked = true;
	const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
	const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	const TArray<FName> Objectives = Missions->GetTracker()->GetCompletedObjectiveIds();
	const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const float Offset = FVector::Dist(Kate->GetActorLocation(), SavedKate);
	Test->TestEqual(TEXT("Round trip: still chapter 2"), GetNameSafe(Missions->GetCurrentMission()), FString(Chapter));
	Test->TestEqual(TEXT("Round trip: the same objectives are complete"), Objectives, SavedObjectives);
	Test->TestTrue(TEXT("Round trip: the courier's flag is up"), Campaign && Campaign->HasFlag(Flag));
	Test->TestEqual(TEXT("Round trip: the same chapter events have fired"), Events ? Events->GetFired() : TArray<FName>(), SavedFired);
	Test->TestEqual(TEXT("Round trip: no event line spoken again"), Events ? Events->GetSpokenLines().Num() : -1, 0);
	Test->TestEqual(TEXT("Round trip: the foam in the quiver"), Inventory->GetArrowCount(SlotFoam), SavedFoam);
	Test->TestTrue(TEXT("Round trip: the sack she carried, carried"), Inventory->GetKeycards().Includes(SavedKeycards) && SavedKeycards.Num() > 0);
	Test->TestEqual(TEXT("Round trip: no crew comes back"), Site ? Site->GetCrew().Num() : -1, 0);
	Test->TestEqual(TEXT("Round trip: the sacks still on the ground"), Site ? Site->GetPickups().Num() : -1, SavedSacksOnGround);
	Test->TestTrue(FString::Printf(TEXT("Round trip: Kate where she was saved (%.0f cm off)"), Offset), Offset <= 60.f);
	RoundTrip = FString::Printf(TEXT("loaded in %.1f s: %d objectives, %d events fired, foam %d, %d keycards, %d crew, %d sacks on the ground, Kate %.0f cm off"),
		ReloadSeconds, Objectives.Num(), Events ? Events->GetFired().Num() : -1, Inventory->GetArrowCount(SlotFoam), Inventory->GetKeycards().Num(),
		Site ? Site->GetCrew().Num() : -1, Site ? Site->GetPickups().Num() : -1, Offset);
	Test->AddInfo(TEXT("Reindeer games lap: round trip ") + RoundTrip);
}

bool FHawkeyeReindeerLap::Update()
{
	using namespace HawkeyeReindeerLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
	if (!World || !PC || !Kate)
	{
		if (Step == EStep::Reloading || (LapStart >= 0.0 && FPlatformTime::Seconds() - WallStart < 400.0))
		{
			return false;
		}
		Fail(TEXT("no game world or Kate"));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);

	if (LapWorld.IsValid() && World != LapWorld.Get() && Step != EStep::Reloading)
	{
		if (Save && Save->IsLoading())
		{
			return false;
		}
		++Deaths;
		Timeline.Add(FString::Printf(TEXT("a reload the lap did not ask for (death %d)"), Deaths));
		LapWorld = World;
		bMoveInjecting = bSprinting = bMeleeHeld = bDrawing = false;
		LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
		LapStart = Now - (FPlatformTime::Seconds() - WallStart);
	}
	if (Step == EStep::Reloading)
	{
		if (World == LapWorld.Get() || (Save && Save->IsLoading()) || !Site || !Site->GetDefinition())
		{
			if (FPlatformTime::Seconds() - ReloadStartedAt > 60.0)
			{
				Fail(TEXT("the mid-chapter load did not finish within 60 s"));
				Report(World);
				return true;
			}
			return false;
		}
		LapWorld = World;
		bMoveInjecting = bSprinting = bMeleeHeld = bDrawing = false;
		LapStart = Now - (FPlatformTime::Seconds() - WallStart);
		LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
		ReloadSeconds = FPlatformTime::Seconds() - ReloadStartedAt;
		// The load built the site again: its examines are new actors.
		for (AChapterEndInteractable* Examine : Site->GetExamines())
		{
			(Examine->ObjectiveId == TEXT("return_toys") ? Table : Courier) = Examine;
		}
		CheckRoundTrip(World, Kate);
		Timeline.Add(FString::Printf(TEXT("%.1f loaded back: %s"), Now - LapStart, *RoundTrip));
		Enter(EStep::Sacks, Now);
		return false;
	}
	if (Step == EStep::SetUp)
	{
		if (!SetUp(World, PC, Kate, Now))
		{
			Report(World);
			return true;
		}
		Enter(EStep::ToCourier, Now);
		return false;
	}

	const double T = Now - LapStart;
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++HitsTaken;
		DamageTaken += LastHealth - Health;
	}
	LastHealth = Health;
	NoteToasts(Kate);
	if (FPlatformTime::Seconds() - WallStart > 420.0 && Step < EStep::Ending)
	{
		Fail(FString::Printf(TEXT("the lap ran out of time at step %d"), (int32)Step));
		ReleaseInputs(PC);
		Report(World);
		return true;
	}

	switch (Step)
	{
	case EStep::ToCourier:
	{
		AChapterEndInteractable* Target = Courier.Get();
		if (!Target)
		{
			Fail(TEXT("no courier examine"));
			Report(World);
			return true;
		}
		const UMissionObjective* Current = Missions->GetCurrentObjective();
		if (T < 0.5)
		{
			Test->TestEqual(TEXT("At the start the current objective is the drive"), Current ? Current->ObjectiveId : NAME_None, FName(TEXT("reach_drive")));
			FVector Marker;
			Test->TestTrue(TEXT("...and it has a marker (the arrival volume)"), Missions->GetCurrentObjectiveLocation(Marker));
		}
		const float D = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
		if (D < 160.f)
		{
			Move(PC, FVector2D::ZeroVector);
			Sprint(PC, false);
			AimAt(PC, Kate, Target->GetActorLocation());
			Marks.Add(TEXT("courier_reached"), T);
			Enter(EStep::Examine, Now);
			return false;
		}
		RunTo(World, PC, Kate, Target->GetActorLocation(), D > 600.f);
		return false;
	}
	case EStep::Examine:
	{
		AChapterEndInteractable* Target = Courier.Get();
		Move(PC, FVector2D::ZeroVector);
		if (HawkeyePC && HawkeyePC->IsCloseUpActive() && !bShotCourier && Now - StepSince > 1.2)
		{
			bShotCourier = true;
			Shot(TEXT("reindeer_courier.png"));
		}
		if (IsDone(World, TEXT("examine_courier")))
		{
			if (HawkeyePC && HawkeyePC->IsCloseUpActive())
			{
				return false;
			}
			const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
			Test->TestTrue(TEXT("The courier raised the campaign flag"), Campaign && Campaign->HasFlag(Flag));
			Test->TestFalse(TEXT("The chapter is not complete"), Missions->IsMissionComplete());
			Test->TestEqual(TEXT("The current objective is still the drive"), Missions->GetCurrentObjective() ? Missions->GetCurrentObjective()->ObjectiveId : NAME_None,
				FName(TEXT("reach_drive")));
			Marks.Add(TEXT("courier_examined"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f examined the courier"), T));
			Enter(EStep::ToDrive, Now);
			return false;
		}
		if (Target)
		{
			AimAt(PC, Kate, Target->GetActorLocation());
			Interact(PC, Kate, Target);
		}
		if (Now - StepSince > 8.0)
		{
			Fail(FString::Printf(TEXT("the courier was not examined (focus %s, %d presses)"),
				*GetNameSafe(Kate->GetInteractionComponent() ? Kate->GetInteractionComponent()->GetFocusedActor() : nullptr), InteractPresses));
			Enter(EStep::ToDrive, Now);
		}
		return false;
	}
	case EStep::ToDrive:
	{
		const FVector Drive = Table.IsValid() ? Table->GetActorLocation() : Kate->GetActorLocation();
		// The site from 18 m: stop, look at the table, let the camera settle, then the shot.
		if (!bShotSite && FVector::Dist2D(Drive, Kate->GetActorLocation()) < 1800.f)
		{
			Move(PC, FVector2D::ZeroVector);
			Sprint(PC, false);
			AimAt(PC, Kate, Drive + FVector(0.f, 0.f, 120.f));
			if (SitePauseAt < 0.0)
			{
				SitePauseAt = Now;
			}
			if (Now - SitePauseAt > 1.0)
			{
				bShotSite = true;
				Shot(TEXT("reindeer_site.png"));
			}
			return false;
		}
		if (IsDone(World, TEXT("reach_drive")))
		{
			Marks.Add(TEXT("arrived"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f at the drive: the crew wakes"), T));
			Test->TestTrue(TEXT("Arriving woke the crew"), Site && Site->IsCrewAlerted());
			Enter(EStep::Fight, Now);
			return false;
		}
		RunTo(World, PC, Kate, Drive, true);
		return false;
	}
	case EStep::Fight:
	{
		const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
		const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(World);
		const FName Speaking = Dialogue && Dialogue->GetQueue() ? Dialogue->GetQueue()->GetCurrentLine() : NAME_None;
		if (!bShotCrew && Now - StepSince > 0.8 && Site)
		{
			bShotCrew = true;
			const TArray<AThugCharacter*> Crew = Site->GetCrew();
			if (Crew.Num() > 0)
			{
				AimAt(PC, Kate, Crew[0]->GetActorLocation() + FVector(0.f, 0.f, 40.f));
			}
			Shot(TEXT("reindeer_crew.png"));
		}
		if (!bShotFoam && Speaking == TEXT("ch02_foam_unlocked") && HawkeyeShots::GetOutstanding(Test) == 0)
		{
			bShotFoam = true;
			Test->TestTrue(TEXT("The foam is in the quiver as the unlock line plays"), Inventory->GetArrowCount(SlotFoam) > 0);
			Shot(TEXT("reindeer_foam.png"));
		}
		if (IsDone(World, TEXT("stop_crew")))
		{
			ReleaseInputs(PC);
			Marks.Add(TEXT("crew_down"), T);
			FightTimes = FString::Printf(TEXT("%.1f s"), T - Marks.FindRef(TEXT("arrived")));
			Timeline.Add(FString::Printf(TEXT("%.1f the crew is down (%s of fighting)"), T, *FightTimes));
			Enter(EStep::Sacks, Now);
			return false;
		}
		FightStep(World, PC, Kate, Now);
		return false;
	}
	case EStep::Sacks:
	{
		const TArray<APickupActor*> Sacks = Site ? Site->GetPickups() : TArray<APickupActor*>();
		const int32 Carried = Kate->GetInventoryComponent()->GetKeycards().Num();
		// One sack carried: the round trip, once.
		if (Carried >= 1 && !bRoundTripChecked && Save && !Kate->IsDrawingBow())
		{
			ReleaseInputs(PC);
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			SavedKate = Kate->GetActorLocation();
			SavedObjectives = Missions->GetTracker()->GetCompletedObjectiveIds();
			SavedFired = Events ? Events->GetFired() : TArray<FName>();
			SavedKeycards = Kate->GetInventoryComponent()->GetKeycards();
			SavedFoam = Kate->GetInventoryComponent()->GetArrowCount(SlotFoam);
			SavedSacksOnGround = Sacks.Num();
			if (!Save->SaveCampaign(TEXT("reindeer games lap mid-chapter")) || !Save->LoadCampaign())
			{
				Fail(TEXT("the mid-chapter save or load did not start"));
				bRoundTripChecked = true;
				return false;
			}
			ReloadStartedAt = FPlatformTime::Seconds();
			Timeline.Add(FString::Printf(TEXT("%.1f saved and loaded mid-chapter, one sack carried"), T));
			Step = EStep::Reloading;
			return false;
		}
		if (Sacks.Num() == 0)
		{
			ReleaseInputs(PC);
			Marks.Add(TEXT("sacks"), T);
			Enter(EStep::Table, Now);
			return false;
		}
		APickupActor* Nearest = nullptr;
		for (APickupActor* Sack : Sacks)
		{
			if (!Nearest || FVector::Dist2D(Sack->GetActorLocation(), Kate->GetActorLocation()) < FVector::Dist2D(Nearest->GetActorLocation(), Kate->GetActorLocation()))
			{
				Nearest = Sack;
			}
		}
		const float D = FVector::Dist2D(Nearest->GetActorLocation(), Kate->GetActorLocation());
		if (D < 100.f)
		{
			Move(PC, FVector2D::ZeroVector);
			Sprint(PC, false);
			AimAt(PC, Kate, Nearest->GetActorLocation());
			if (!bShotSack)
			{
				// Stopped and looking down at it for a second first, so the shot is not a blur.
				if (SackPauseAt < 0.0)
				{
					SackPauseAt = Now;
				}
				if (Now - SackPauseAt > 1.0 && HawkeyeShots::GetOutstanding(Test) == 0)
				{
					bShotSack = true;
					Shot(TEXT("reindeer_sack.png"));
				}
				return false;
			}
			// The sacks lie a metre apart: whichever one she is focused on is picked up.
			const AActor* Focused = Kate->GetInteractionComponent() ? Kate->GetInteractionComponent()->GetFocusedActor() : nullptr;
			const bool bOnASack = Focused && Sacks.Contains(Cast<APickupActor>(const_cast<AActor*>(Focused)));
			if (!(bOnASack && Interact(PC, Kate, Focused)) && Now - StepSince > 30.0)
			{
				Fail(FString::Printf(TEXT("a sack could not be picked up (focus %s)"), *GetNameSafe(Focused)));
				Report(World);
				return true;
			}
			return false;
		}
		RunTo(World, PC, Kate, Nearest->GetActorLocation(), D > 600.f);
		if (D < 250.f)
		{
			Move(PC, MoveTowards((Nearest->GetActorLocation() - Kate->GetActorLocation()).GetSafeNormal2D(), PC->GetControlRotation().Yaw));
		}
		return false;
	}
	case EStep::Table:
	{
		AChapterEndInteractable* Target = Table.Get();
		if (!Target)
		{
			Fail(TEXT("no table"));
			Report(World);
			return true;
		}
		if (IsDone(World, TEXT("return_toys")))
		{
			Marks.Add(TEXT("toys_returned"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f the toys are back: the chapter is complete"), T));
			Test->TestTrue(TEXT("The toys back complete the chapter"), Missions->IsMissionComplete());
			Enter(EStep::Ending, Now);
			return false;
		}
		const float D = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
		if (D < 180.f)
		{
			Move(PC, FVector2D::ZeroVector);
			Sprint(PC, false);
			AimAt(PC, Kate, Target->GetActorLocation());
			Interact(PC, Kate, Target);
			if (Now - StepSince > 40.0)
			{
				Fail(FString::Printf(TEXT("the table did not take the toys (focus %s)"),
					*GetNameSafe(Kate->GetInteractionComponent() ? Kate->GetInteractionComponent()->GetFocusedActor() : nullptr)));
				Report(World);
				return true;
			}
			return false;
		}
		RunTo(World, PC, Kate, Target->GetActorLocation() + Target->GetActorForwardVector() * 120.f, D > 600.f);
		return false;
	}
	case EStep::Ending:
	{
		const UMissionFlowController* Flow = HawkeyePC ? HawkeyePC->GetMissionFlow() : nullptr;
		const double Wall = FPlatformTime::Seconds() - WallStepSince;
		if (!bShotReturn && HawkeyePC && HawkeyePC->IsCloseUpActive() && Wall > 0.8)
		{
			bShotReturn = true;
			Shot(TEXT("reindeer_return.png"));
		}
		if (!bShotEnd && Flow && Flow->GetStep() == EMissionFlowStep::EndCard)
		{
			bShotEnd = true;
			Shot(TEXT("reindeer_endcard.png"));
		}
		if (HawkeyePC && HawkeyePC->IsFlashbackActive())
		{
			bFlashbackSeen = true;
			if (!bShotSlide && HawkeyeShots::GetOutstanding(Test) == 0 && Wall > 4.0)
			{
				bShotSlide = true;
				Shot(TEXT("reindeer_slides.png"));
			}
		}
		const bool bRunning = Flow && Flow->IsRunning();
		if (bFlashbackSeen && !bRunning && !(HawkeyePC && HawkeyePC->IsFlashbackActive()) && Wall > 3.0)
		{
			Marks.Add(TEXT("back_roaming"), T);
			Enter(EStep::Phone, Now);
			return false;
		}
		if (Wall > 90.0)
		{
			Fail(FString::Printf(TEXT("the end of the chapter did not finish (flashback seen: %s)"), bFlashbackSeen ? TEXT("yes") : TEXT("no")));
			Enter(EStep::Phone, Now);
		}
		return false;
	}
	case EStep::Phone:
	{
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
		const UPhoneInbox* Inbox = Phone ? Phone->GetInbox() : nullptr;
		const double Wall = FPlatformTime::Seconds() - WallStepSince;
		const bool bAll = Inbox && Inbox->HasArrived(TEXT("msg_ch02_01")) && Inbox->HasArrived(TEXT("msg_ch02_02")) && Inbox->HasArrived(TEXT("msg_ch02_05"));
		if (!bAll)
		{
			if (Wall > 40.0)
			{
				Fail(TEXT("chapter 2's texts did not all arrive within 40 s of the slides"));
				Enter(EStep::NextChapter, Now);
			}
			return false;
		}
		if (HawkeyePC && !HawkeyePC->IsPhoneOpen() && !bShotPhone)
		{
			HawkeyePC->TogglePhone();
			WallStepSince = FPlatformTime::Seconds() - 1.0;
			bShotPhone = true;
			return false;
		}
		if (bShotPhone && Wall > 2.0 && !Shots.Contains(TEXT("reindeer_phone.png")))
		{
			Shot(TEXT("reindeer_phone.png"));
			return false;
		}
		if (Shots.Contains(TEXT("reindeer_phone.png")) && Wall > 4.0 && HawkeyeShots::GetOutstanding(Test) == 0)
		{
			if (HawkeyePC && HawkeyePC->IsPhoneOpen())
			{
				HawkeyePC->TogglePhone();
			}
			Enter(EStep::NextChapter, Now);
		}
		return false;
	}
	case EStep::NextChapter:
	{
		if (FPlatformTime::Seconds() - WallStepSince < 1.0)
		{
			return false;
		}
		// Chapter select's next entry, entered straight after chapter 2.
		AHawkeyeGameMode* Mode = World->GetAuthGameMode<AHawkeyeGameMode>();
		UMissionDefinition* Four = AHawkeyeGameMode::FindChapter(NextChapter);
		const bool bStarted = Mode && Four && Mode->StartChapter(Four);
		Test->TestTrue(TEXT("Chapter 4 starts after chapter 2"), bStarted);
		int32 SiteActors = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			SiteActors += It->ActorHasTag(UChapterSiteSubsystem::SiteTag) && !It->IsActorBeingDestroyed() ? 1 : 0;
		}
		const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
		const int32 Foam = Kate->GetInventoryComponent()->GetArrowCount(SlotFoam);
		Test->TestNull(TEXT("Chapter 4: the toy drive is gone"), Site ? Site->GetDefinition() : nullptr);
		Test->TestEqual(TEXT("...with every one of its actors"), SiteActors, 0);
		Test->TestTrue(TEXT("Chapter 4: Tomas waits on his roof"), Fight && Fight->GetState() == EBossFightState::Approach && Fight->GetBoss());
		Test->TestEqual(TEXT("Chapter 4's quiver holds the foam unlocked before it"), Foam, 2);
		// Opened from chapter select past chapter 3: its grant holds chapter 3's boomerang too.
		const int32 Boomerangs = Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang);
		Test->TestEqual(TEXT("...and chapter 3's boomerangs"), Boomerangs, 2);
		NextChapterCheck = FString::Printf(TEXT("chapter 4 %s, %d site actors left, Tomas %s, foam %d, boomerang %d"), bStarted ? TEXT("started") : TEXT("refused"),
			SiteActors, Fight && Fight->GetBoss() ? TEXT("waiting") : TEXT("missing"), Foam, Boomerangs);
		Enter(EStep::Done, Now);
		Report(World);
		return true;
	}
	default:
		return true;
	}
}

void FHawkeyeReindeerLap::Report(UWorld* World)
{
	using namespace HawkeyeReindeerLap;
	const UChapterEventSubsystem* Events = World ? UChapterEventSubsystem::Get(World) : nullptr;
	const UHawkeyeSaveSubsystem* Save = World ? UHawkeyeSaveSubsystem::Get(World) : nullptr;
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	const bool bComplete = Marks.Contains(TEXT("toys_returned"));
	const TArray<FName> Fired = Events ? Events->GetFired() : TArray<FName>();
	Test->TestTrue(TEXT("The lap completes the chapter"), bComplete);
	Test->TestTrue(TEXT("The courier was examined (the optional variant)"), Marks.Contains(TEXT("courier_examined")));
	Test->TestFalse(TEXT("The courier's completion showed no toast (unmarked)"), bCourierToast);
	Test->TestTrue(TEXT("The flag is up at the end"), Campaign && Campaign->HasFlag(Flag));
	Test->TestTrue(TEXT("The round trip happened and was checked"), bRoundTripChecked);
	Test->TestTrue(TEXT("A foam arrow was loosed"), FoamLoosed > 0);
	Test->TestTrue(TEXT("The first parry happened"), Parries > 0);
	Test->TestTrue(TEXT("The end card showed"), bShotEnd);
	Test->TestTrue(TEXT("The Visiting hours slides played"), bFlashbackSeen);
	const double FightSeconds = Marks.Contains(TEXT("crew_down")) ? Marks.FindRef(TEXT("crew_down")) - Marks.FindRef(TEXT("arrived")) : -1.0;
	FString Json = TEXT("{\n");
	Json += FString::Printf(TEXT("  \"completed\": %s,\n  \"wall_seconds\": %.1f,\n  \"fight_seconds\": %.1f,\n  \"reload_seconds\": %.1f,\n"),
		bComplete ? TEXT("true") : TEXT("false"), FPlatformTime::Seconds() - WallStart, FightSeconds, ReloadSeconds);
	TArray<FString> Lines;
	for (const TPair<FString, double>& Mark : Marks)
	{
		Lines.Add(FString::Printf(TEXT("\"%s\": %.1f"), *Mark.Key, Mark.Value));
	}
	Json += TEXT("  \"marks\": {") + FString::Join(Lines, TEXT(", ")) + TEXT("},\n");
	Json += FString::Printf(TEXT("  \"hits_taken\": %d,\n  \"damage_taken\": %.0f,\n  \"strikes\": %d,\n  \"parries\": %d,\n  \"finishers\": %d,\n  \"dodges\": %d,\n"),
		HitsTaken, DamageTaken, Strikes, Parries, Finishers, Dodges);
	Json += FString::Printf(TEXT("  \"arrows_loosed\": %d,\n  \"foam_loosed\": %d,\n  \"deaths\": %d,\n  \"flag\": %s,\n"), ArrowsLoosed, FoamLoosed, Deaths,
		Campaign && Campaign->HasFlag(Flag) ? TEXT("true") : TEXT("false"));
	Lines.Reset();
	for (const FName Event : Fired)
	{
		Lines.Add(TEXT("\"") + Event.ToString() + TEXT("\""));
	}
	Json += TEXT("  \"events_fired\": [") + FString::Join(Lines, TEXT(", ")) + TEXT("],\n");
	Json += FString::Printf(TEXT("  \"round_trip\": \"%s\",\n  \"next_chapter\": \"%s\",\n"), *Escape(RoundTrip), *Escape(NextChapterCheck));
	Lines.Reset();
	for (const FString& Toast : ToastsSeen)
	{
		Lines.Add(TEXT("\"") + Escape(Toast) + TEXT("\""));
	}
	Json += TEXT("  \"toasts\": [") + FString::Join(Lines, TEXT(", ")) + TEXT("],\n");
	Lines.Reset();
	for (const FString& Line : Timeline)
	{
		Lines.Add(TEXT("\"") + Escape(Line) + TEXT("\""));
	}
	Json += TEXT("  \"timeline\": [\n    ") + FString::Join(Lines, TEXT(",\n    ")) + TEXT("\n  ],\n");
	Lines.Reset();
	for (const FString& Name : Shots)
	{
		Lines.Add(TEXT("\"") + Name + TEXT("\""));
	}
	Json += TEXT("  \"shots\": [") + FString::Join(Lines, TEXT(", ")) + TEXT("],\n");
	Lines.Reset();
	for (const FString& Why : Failures)
	{
		Lines.Add(TEXT("\"") + Escape(Why) + TEXT("\""));
	}
	Json += TEXT("  \"failures\": [") + FString::Join(Lines, TEXT(", ")) + TEXT("]\n}\n");
	WriteText(TEXT("reindeer_games_lap.json"), Json);
	Test->AddInfo(TEXT("reindeer_games_lap.json:\n") + Json);
	if (UHawkeyeSaveSubsystem* MutableSave = World ? UHawkeyeSaveSubsystem::Get(World) : nullptr)
	{
		IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
		MutableSave->SlotNameOverride.Reset();
	}
}

bool FHawkeyeLapReindeerGames::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the reindeer games lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeReindeerLap(this));
	return true;
}

#endif
