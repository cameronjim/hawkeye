#include "Boss/BossArenaHazard.h"
#include "Boss/BossDefinition.h"
#include "Boss/BossFightSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/BowComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
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
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/DifficultySubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Lap.SocialClub: chapter 4, "The Social Club", played by a script from the chapter's start, in the standalone
 * game like the other laps:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -Difficulty=Normal -ExecCmds="Automation RunTests Hawkeye.Lap.SocialClub; Quit"
 *
 * The district loads on chapter 1; the lap starts DA_CH04_SocialClub (AHawkeyeGameMode::StartChapter, what chapter
 * select does) and its opening (UBossFightSubsystem::BeginChapter puts Kate on the arena's street stand). She grapples
 * to the roof's parapet anchor (aimed at it, the key pressed only on the anchor it targets), waits out the pre-fight
 * lines, then fights as a player who has learnt the melee: parries a wind-up she can see, finishes anyone open to it
 * (Tomas when he is winded), steps out of a swing about to land, strikes otherwise, never wastes a blow on Tomas while
 * he watches, gets out of the Santa's lane when it lights up, and looses one confetti arrow at Tomas. In Tomas's own
 * phase, once he is under 85%, the campaign is saved and loaded (its own slot) and the fight must come back at his
 * phase's checkpoint: full health, no crew, fighting, Kate where she was. Then on to the end: the after lines and
 * the end card.
 *
 * Writes Saved/Automation/social_club_lap.json (times per phase, hits, the hazard's hits, winded windows, finishers,
 * parries, the barks and lines spoken, the round trip) and Saved/Screenshots/SocialClub/social_*.png (the street
 * stand, the roof at the intro, each phase, Tomas down, the end card).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapSocialClub, "Hawkeye.Lap.SocialClub",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeSocialLap
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* Chapter = TEXT("DA_CH04_SocialClub");
	static const TCHAR* Slot = TEXT("HawkeyeSocialClubLap");
	static constexpr int32 SlotStandard = 1;
	static constexpr int32 SlotConfetti = 5;

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

	static bool CanParryNow(const AHawkeyeCharacter* Kate)
	{
		return Kate->GetMeleeComponent() && !Kate->GetMeleeComponent()->IsWindingUp() && !Kate->IsDodging()
			&& !Kate->IsStaggered() && !Kate->IsDrawingBow() && !Kate->IsLockedOutByTakedown();
	}

	static AThugCharacter* ParryChance(const TArray<AThugCharacter*>& Thugs, const AHawkeyeCharacter* Kate, const APlayerController* PC)
	{
		const FVector Forward = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
		for (AThugCharacter* Thug : Thugs)
		{
			const EHawkeyeParryKind Kind = Thug ? UHawkeyeMeleeRules::ClassifyParry(Thug) : EHawkeyeParryKind::None;
			if (Kind == EHawkeyeParryKind::None
				|| !UHawkeyeMeleeRules::IsInFrontWithin(Kate->GetActorLocation(), Forward, Thug->GetActorLocation(), 260.f, 65.f))
			{
				continue;
			}
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			if (Melee && Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining() >= 0.2f)
			{
				return Thug;
			}
		}
		return nullptr;
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

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	static FString ShotPath(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("SocialClub") / Name);
	}

	static FString DifficultyName(const UWorld* World)
	{
		const UDifficultySubsystem* Difficulty = UDifficultySubsystem::Get(World);
		return Difficulty ? UDifficultySubsystem::GetDifficultyName(Difficulty->GetDifficulty()).ToString() : FString(TEXT("?"));
	}

	static FString Escape(const FString& In)
	{
		return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}
}

class FHawkeyeSocialClubLap : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeSocialClubLap(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	enum class EStep : uint8 { SetUp, Grapple, Zipping, OnRoof, Intro, Fight, Reloading, Defeated, Ending, Done };

	void Enter(EStep Next, double Now) { Step = Next; StepSince = Now; }
	void Move(APlayerController* PC, const FVector2D& Value);
	void Sprint(APlayerController* PC, bool bOn);
	void RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint);
	void ReleaseInputs(APlayerController* PC);
	bool SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void FightStep(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	bool AvoidHazard(APlayerController* PC, AHawkeyeCharacter* Kate);
	bool UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void BeginRoundTrip(UWorld* World, AHawkeyeCharacter* Kate, double Now);
	void CheckRoundTrip(UWorld* World, AHawkeyeCharacter* Kate);
	void Shot(const TCHAR* Name);
	void Fail(const FString& Why);
	void Report(UWorld* World);

	FAutomationTestBase* Test;
	EStep Step = EStep::SetUp;
	double StepSince = 0.0;
	double LapStart = -1.0;
	double WallStart = 0.0;
	TWeakObjectPtr<UWorld> LapWorld;
	TWeakObjectPtr<AGrappleAnchor> Anchor;
	bool bMoveInjecting = false;
	bool bSprinting = false;
	bool bMeleeHeld = false;
	double MeleeHeldSince = 0.0;
	double NextActionAt = 0.0;
	double LastParryTap = -10.0;
	double GrapplePressedAt = -10.0;
	int32 GrapplePresses = 0;
	int32 Zips = 0;
	float LastHealth = 0.f;
	int32 HitsTaken = 0;
	float DamageTaken = 0.f;
	int32 Strikes = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 Dodges = 0;
	int32 HazardDodges = 0;
	int32 HazardHits = 0;
	int32 Deaths = 0;
	int32 LastPhase = INDEX_NONE;
	TArray<FString> Timeline;
	TArray<FString> Shots;
	TArray<FString> Failures;
	TMap<FString, double> Marks;
	FString Notes;

	// The confetti arrow.
	bool bDrawing = false;
	bool bConfettiLoosed = false;
	double DrawStart = 0.0;
	double SlotResetAt = -1.0;

	// The round trip.
	bool bRoundTripDone = false;
	bool bRoundTripChecked = false;
	double ReloadStartedAt = 0.0;
	FVector SavedKate = FVector::ZeroVector;
	float SavedHealth = 0.f;
	float SavedBossHealth = 0.f;
	TArray<FName> SavedObjectives;
	FString RoundTrip;

	// Shots taken.
	bool bShotStreet = false;
	bool bShotIntro = false;
	bool bShotCrew = false;
	bool bShotTomas = false;
	bool bShotSanta = false;
	bool bShotDown = false;
	bool bShotEnd = false;
};

void FHawkeyeSocialClubLap::Move(APlayerController* PC, const FVector2D& Value)
{
	UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeSocialLap::InputOf(PC);
	const UInputAction* A = LoadObject<UInputAction>(nullptr, HawkeyeSocialLap::MovePath);
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

void FHawkeyeSocialClubLap::Sprint(APlayerController* PC, bool bOn)
{
	if (bOn != bSprinting)
	{
		HawkeyeSocialLap::Hold(PC, HawkeyeSocialLap::SprintPath, bOn);
		bSprinting = bOn;
	}
}

void FHawkeyeSocialClubLap::RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint)
{
	Sprint(PC, bSprint);
	Move(PC, HawkeyeSocialLap::MoveTowards(HawkeyeSocialLap::SteerTowards(World, Kate, Goal), PC->GetControlRotation().Yaw));
}

void FHawkeyeSocialClubLap::ReleaseInputs(APlayerController* PC)
{
	Move(PC, FVector2D::ZeroVector);
	Sprint(PC, false);
	if (bMeleeHeld)
	{
		HawkeyeSocialLap::Hold(PC, HawkeyeSocialLap::MeleePath, false);
		bMeleeHeld = false;
	}
	if (bDrawing)
	{
		HawkeyeSocialLap::Hold(PC, HawkeyeSocialLap::FirePath, false);
		bDrawing = false;
	}
}

void FHawkeyeSocialClubLap::Shot(const TCHAR* Name)
{
	HawkeyeShots::Request(Test, HawkeyeSocialLap::ShotPath(Name));
	Shots.Add(Name);
}

void FHawkeyeSocialClubLap::Fail(const FString& Why)
{
	Failures.Add(Why);
	Test->AddError(TEXT("Social club lap: ") + Why);
}

bool FHawkeyeSocialClubLap::SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeSocialLap;
	AHawkeyeGameMode* Mode = World->GetAuthGameMode<AHawkeyeGameMode>();
	UMissionDefinition* Mission = AHawkeyeGameMode::FindChapter(Chapter);
	UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	if (!Mode || !Mission || !Fight || !Save)
	{
		Fail(TEXT("no game mode, chapter 4, boss fight or save subsystem"));
		return false;
	}
	Save->SlotNameOverride = Slot;
	IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
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
		Fail(TEXT("chapter 4 would not start"));
		return false;
	}
	Fight->BeginChapter(Mission);
	Test->TestEqual(TEXT("The fight waits on the roof"), Fight->GetState(), EBossFightState::Approach);
	Test->TestNotNull(TEXT("Tomas is there"), Fight->GetBoss());
	Test->TestEqual(TEXT("With four of his guys"), Fight->GetCrew().Num(), 4);
	Test->TestNotNull(TEXT("And the Santa"), Fight->GetHazard());
	const FVector Marker = Fight->GetDefinition()->Arena.ApproachAnchor;
	float Best = 250.f;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const float D = FVector::Dist(It->GetMarkerLocation(), Marker);
		if (It->bEnabled && D < Best)
		{
			Best = D;
			Anchor = *It;
		}
	}
	if (!Anchor.IsValid())
	{
		Fail(FString::Printf(TEXT("no grapple anchor within 2.5 m of the arena's approach anchor %s"), *Marker.ToCompactString()));
		return false;
	}
	Kate->GetInventoryComponent()->SelectArrowSlot(SlotStandard);
	LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
	LapStart = Now;
	WallStart = FPlatformTime::Seconds();
	LapWorld = World;
	Marks.Add(TEXT("start"), 0.0);
	Test->AddInfo(FString::Printf(TEXT("Social club lap: Kate on the street stand %s, roof %s at %.1f m, difficulty %s, anchor %s (%.0f cm off)."),
		*Kate->GetActorLocation().ToCompactString(), *Fight->GetDefinition()->Arena.RoofId.ToString(),
		Fight->GetDefinition()->Arena.RoofZ / 100.f, *DifficultyName(World),
		*Anchor->GetName(), Best));
	return true;
}

bool FHawkeyeSocialClubLap::AvoidHazard(APlayerController* PC, AHawkeyeCharacter* Kate)
{
	const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(Kate);
	const ABossArenaHazard* Hazard = Fight ? Fight->GetHazard() : nullptr;
	if (!Hazard || !Hazard->IsLoose())
	{
		return false;
	}
	const EBossHazardPhase Phase = Hazard->GetState().Phase;
	if (Phase != EBossHazardPhase::Telegraph && Phase != EBossHazardPhase::Sweep)
	{
		return false;
	}
	const FVector Lane = (Hazard->GetLaneEnd() - Hazard->GetLaneStart()).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Lane);
	const FVector Mid = (Hazard->GetLaneStart() + Hazard->GetLaneEnd()) * 0.5f;
	const float Off = FVector::DotProduct(Kate->GetActorLocation() - Mid, Across);
	if (FMath::Abs(Off) >= Hazard->GetTuning().HalfWidth + 130.f)
	{
		return false;
	}
	// Out of the lane the short way.
	const FVector Away = Across * (Off >= 0.f ? 1.f : -1.f);
	if (bMeleeHeld)
	{
		HawkeyeSocialLap::Hold(PC, HawkeyeSocialLap::MeleePath, false);
		bMeleeHeld = false;
	}
	Sprint(PC, true);
	Move(PC, HawkeyeSocialLap::MoveTowards(Away, PC->GetControlRotation().Yaw));
	return true;
}

bool FHawkeyeSocialClubLap::UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeSocialLap;
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
	const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	AThugCharacter* Tomas = Fight ? Fight->GetBoss() : nullptr;
	if (Tomas)
	{
		const FVector Chest = Tomas->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		AimAt(PC, Kate, Chest);
		if (UBowComponent* Bow = Kate->GetBowComponent())
		{
			Bow->SetAimOverride(Chest);
		}
	}
	if (Now - DrawStart >= 0.85 || IsDown(Tomas))
	{
		Hold(PC, FirePath, false);
		bDrawing = false;
		bConfettiLoosed = !IsDown(Tomas);
		Marks.Add(TEXT("confetti"), Now - LapStart);
		Timeline.Add(FString::Printf(TEXT("%.1f confetti arrow loosed at Tomas"), Now - LapStart));
		SlotResetAt = Now + 0.3;
		NextActionAt = Now + 0.2;
	}
	return true;
}

void FHawkeyeSocialClubLap::FightStep(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeSocialLap;
	UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	const UBossDefinition* Def = Fight ? Fight->GetDefinition() : nullptr;
	AThugCharacter* Tomas = Fight ? Fight->GetBoss() : nullptr;
	if (!Def || !Def->Phases.IsValidIndex(Fight->GetPhaseIndex()))
	{
		return;
	}
	const FBossPhaseDefinition& Phase = Def->Phases[Fight->GetPhaseIndex()];
	TArray<AThugCharacter*> Targets;
	for (AThugCharacter* Thug : Fight->GetCrew())
	{
		if (!IsDown(Thug))
		{
			Targets.Add(Thug);
		}
	}
	if (Phase.bBossFights && !IsDown(Tomas))
	{
		Targets.Add(Tomas);
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
	if (AvoidHazard(PC, Kate))
	{
		++HazardDodges;
		return;
	}
	AThugCharacter* Target = nullptr;
	float Distance = BIG_NUMBER;
	for (AThugCharacter* Thug : Targets)
	{
		const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
		// Tomas winded comes first: that is the window.
		const float Score = D - (Thug == Tomas && Fight->IsWinded() ? 2000.f : 0.f);
		if (Score < Distance - (Target && Target == Tomas && Fight->IsWinded() ? 2000.f : 0.f))
		{
			Distance = D;
			Target = Thug;
		}
	}
	if (!Target)
	{
		Move(PC, FVector2D::ZeroVector);
		Sprint(PC, false);
		return;
	}
	Distance = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
	AimAt(PC, Kate, Target->GetActorLocation() + FVector(0.f, 0.f, 30.f));
	if (Now - LastParryTap > 0.3 && CanParryNow(Kate) && ParryChance(Targets, Kate, PC))
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, MeleePath);
		LastParryTap = Now;
		++Parries;
		NextActionAt = Now + 0.25;
		return;
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
		NextActionAt = Now + 0.4;
		Timeline.Add(FString::Printf(TEXT("%.1f finisher on %s"), Now - LapStart, *GetNameSafe(Kate->GetFinisherComponent()->FindTarget())));
		return;
	}
	// A swing about to land: step out of it.
	if (Kate->GetDodgeCooldownRemaining() <= 0.f)
	{
		for (AThugCharacter* Thug : Targets)
		{
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			if (Melee && Melee->IsWindingUp() && Melee->GetPhaseRemaining() < 0.3f && !Thug->IsIncapacitated()
				&& FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation()) <= 260.f && Thug != Target)
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
	// One confetti arrow at Tomas from a few metres, once the Santa phase has given them.
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const bool bWantsConfetti = Phase.bHazardActive && !bConfettiLoosed && Tomas && !IsDown(Tomas)
		&& Inventory->GetArrowCount(SlotConfetti) > 0 && !Fight->IsWinded();
	const float TomasDistance = Tomas ? FVector::Dist2D(Tomas->GetActorLocation(), Kate->GetActorLocation()) : BIG_NUMBER;
	if (bWantsConfetti && TomasDistance < 450.f)
	{
		// Room for a draw first: a few steps back from him, along the lane's side she is on.
		const FVector Away = (Kate->GetActorLocation() - Tomas->GetActorLocation()).GetSafeNormal2D();
		Sprint(PC, true);
		Move(PC, MoveTowards(Away, PC->GetControlRotation().Yaw));
		return;
	}
	if (bWantsConfetti && TomasDistance <= 1500.f)
	{
		Target = Tomas;
		Move(PC, FVector2D::ZeroVector);
		Sprint(PC, false);
		Inventory->SelectArrowSlot(SlotConfetti);
		DrawStart = Now;
		bDrawing = true;
		Hold(PC, FirePath, true);
		return;
	}
	if (Distance <= 190.f)
	{
		Move(PC, FVector2D::ZeroVector);
		Sprint(PC, false);
		const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
		const bool bLight = (ThugMelee && ThugMelee->IsWindingUp()) || Target->IsKnockedDown() || Target->IsStaggered()
			|| Strikes % 3 != 2;
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
	RunTo(World, PC, Kate, Target->GetActorLocation(), Distance > 900.f);
}

void FHawkeyeSocialClubLap::BeginRoundTrip(UWorld* World, AHawkeyeCharacter* Kate, double Now)
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
	const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	bRoundTripDone = true;
	if (!Save || !Fight || !Fight->GetBoss())
	{
		Fail(TEXT("no save subsystem or Tomas for the round trip"));
		return;
	}
	SavedKate = Kate->GetActorLocation();
	SavedHealth = Kate->GetHealthComponent()->GetCurrentHealth();
	SavedBossHealth = Fight->GetBoss()->GetHealthComponent()->GetCurrentHealth();
	SavedObjectives = UMissionSubsystem::Get(World)->GetTracker()->GetCompletedObjectiveIds();
	if (!Save->SaveCampaign(TEXT("social club lap mid-fight")) || !Save->LoadCampaign())
	{
		Fail(TEXT("the mid-fight save or load did not start"));
		return;
	}
	ReloadStartedAt = FPlatformTime::Seconds();
	Timeline.Add(FString::Printf(TEXT("%.1f saved and loaded mid-fight: Tomas %.0f health, Kate %.0f"), Now - LapStart, SavedBossHealth,
		SavedHealth));
	Step = EStep::Reloading;
}

void FHawkeyeSocialClubLap::CheckRoundTrip(UWorld* World, AHawkeyeCharacter* Kate)
{
	bRoundTripChecked = true;
	const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	const AThugCharacter* Tomas = Fight ? Fight->GetBoss() : nullptr;
	const UHealthComponent* Health = Tomas ? Tomas->GetHealthComponent() : nullptr;
	const float Offset = FVector::Dist(Kate->GetActorLocation(), SavedKate);
	const TArray<FName> Objectives = UMissionSubsystem::Get(World)->GetTracker()->GetCompletedObjectiveIds();
	Test->TestEqual(TEXT("Round trip: the fight is on again"), Fight ? Fight->GetState() : EBossFightState::Idle, EBossFightState::Fight);
	Test->TestEqual(TEXT("Round trip: at Tomas's phase, the checkpoint"), Fight ? Fight->GetPhaseIndex() : -1, 1);
	Test->TestTrue(TEXT("Round trip: Tomas back at full health (the phase's checkpoint)"), Health && Health->GetHealthPercent() > 0.999f);
	Test->TestEqual(TEXT("Round trip: no crew"), Fight ? Fight->GetCrew().Num() : -1, 0);
	Test->TestTrue(FString::Printf(TEXT("Round trip: Kate where she was saved (%.0f cm off)"), Offset), Offset <= 60.f);
	Test->TestEqual(TEXT("Round trip: Kate's health came back"), Kate->GetHealthComponent()->GetCurrentHealth(), SavedHealth, 0.5f);
	Test->TestEqual(TEXT("Round trip: the same objectives are complete"), Objectives, SavedObjectives);
	RoundTrip = FString::Printf(TEXT("saved with Tomas at %.0f, loaded in %.1f s: phase %d, Tomas %.0f of %.0f, %d crew, Kate %.0f cm off, health %.0f -> %.0f"),
		SavedBossHealth, FPlatformTime::Seconds() - ReloadStartedAt, Fight ? Fight->GetPhaseIndex() : -1, Health ? Health->GetCurrentHealth() : 0.f,
		Health ? Health->GetMaxHealth() : 0.f, Fight ? Fight->GetCrew().Num() : -1, Offset, SavedHealth, Kate->GetHealthComponent()->GetCurrentHealth());
	Test->AddInfo(TEXT("Social club lap: round trip ") + RoundTrip);
}

bool FHawkeyeSocialClubLap::Update()
{
	using namespace HawkeyeSocialLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !PC || !Kate)
	{
		// Mid level change (the round trip, a death's reload): wait for the new world.
		if (Step == EStep::Reloading || (FPlatformTime::Seconds() - WallStart < 400.0 && LapStart >= 0.0))
		{
			return false;
		}
		Fail(TEXT("no game world or Kate"));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);

	// A world the lap did not load itself (Kate went down and the checkpoint loaded): carry on from it.
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
		Enter(Fight && Fight->GetState() == EBossFightState::Approach ? EStep::Grapple : EStep::Fight, Now);
	}
	if (Step == EStep::Reloading)
	{
		if (World == LapWorld.Get() || (Save && Save->IsLoading()) || !Fight || Fight->GetState() == EBossFightState::Approach)
		{
			if (FPlatformTime::Seconds() - ReloadStartedAt > 60.0)
			{
				Fail(TEXT("the mid-fight load did not finish within 60 s"));
				Report(World);
				return true;
			}
			return false;
		}
		LapWorld = World;
		bMoveInjecting = bSprinting = bMeleeHeld = bDrawing = false;
		// The world clock starts again with the new world; keep the lap's clock running on.
		LapStart = Now - (FPlatformTime::Seconds() - WallStart);
		LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
		CheckRoundTrip(World, Kate);
		Enter(EStep::Fight, Now);
		NextActionAt = Now + 0.5;
		return false;
	}

	if (Step == EStep::SetUp)
	{
		if (!SetUp(World, PC, Kate, Now))
		{
			Report(World);
			return true;
		}
		Enter(EStep::Grapple, Now);
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
	if (FPlatformTime::Seconds() - WallStart > 420.0 && Step != EStep::Done)
	{
		Fail(FString::Printf(TEXT("timed out at step %d after %.0f s"), static_cast<int32>(Step), T));
		ReleaseInputs(PC);
		Report(World);
		return true;
	}
	if (Fight && Fight->GetPhaseIndex() != LastPhase && Fight->GetState() == EBossFightState::Fight)
	{
		LastPhase = Fight->GetPhaseIndex();
		Marks.Add(FString::Printf(TEXT("phase_%d"), LastPhase), T);
		Timeline.Add(FString::Printf(TEXT("%.1f phase %d starts"), T, LastPhase));
	}
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	switch (Step)
	{
	case EStep::Grapple:
	{
		AGrappleAnchor* Target = Anchor.Get();
		if (!Target || !Grapple)
		{
			Fail(TEXT("no anchor to grapple to"));
			Report(World);
			return true;
		}
		if (!bShotStreet && Now - StepSince > 0.8)
		{
			bShotStreet = true;
			AimAt(PC, Kate, Target->GetMarkerLocation());
			Shot(TEXT("social_street.png"));
		}
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
		{
			Enter(EStep::Zipping, Now);
			return false;
		}
		Move(PC, FVector2D::ZeroVector);
		AimAt(PC, Kate, Target->GetMarkerLocation());
		if (Now - StepSince < 1.5 || Now - GrapplePressedAt < 0.4)
		{
			return false;
		}
		Grapple->RefreshTarget();
		if (Grapple->GetTargetAnchor() == Target && Grapple->CanChain())
		{
			++GrapplePresses;
			GrapplePressedAt = Now;
			Tap(PC, GrapplePath);
			return false;
		}
		if (Now - StepSince > 8.0)
		{
			Fail(FString::Printf(TEXT("%s never targeted from the street stand (targeting %s)"), *Target->GetName(),
				*GetNameSafe(Grapple->GetTargetAnchor())));
			Report(World);
			return true;
		}
		return false;
	}
	case EStep::Zipping:
		Move(PC, FVector2D::ZeroVector);
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight() || !Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			if (Now - StepSince > 8.0)
			{
				Fail(TEXT("the zip never landed"));
				Report(World);
				return true;
			}
			return false;
		}
		++Zips;
		if (Kate->GetActorLocation().Z > Fight->GetDefinition()->Arena.RoofZ - 50.f)
		{
			Marks.Add(TEXT("landed"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f landed on the roof at %s after %d press(es)"), T, *Kate->GetActorLocation().ToCompactString(),
				GrapplePresses));
			Enter(EStep::OnRoof, Now);
			return false;
		}
		if (Zips >= 3)
		{
			Fail(TEXT("three zips and not on the roof"));
			Report(World);
			return true;
		}
		Enter(EStep::Grapple, Now);
		return false;
	case EStep::OnRoof:
		if (Fight->GetState() == EBossFightState::Intro || Fight->GetState() == EBossFightState::Fight)
		{
			Move(PC, FVector2D::ZeroVector);
			Marks.Add(TEXT("met"), T);
			Test->TestTrue(TEXT("meet_tomas completes on the roof"), Missions->GetTracker()->FindObjective(TEXT("meet_tomas"))->IsCompleted());
			Enter(EStep::Intro, Now);
			return false;
		}
		RunTo(World, PC, Kate, Fight->GetDefinition()->Arena.Centre, false);
		if (Now - StepSince > 10.0)
		{
			Fail(TEXT("on the roof 10 s and meet_tomas never completed"));
			Report(World);
			return true;
		}
		return false;
	case EStep::Intro:
		Move(PC, FVector2D::ZeroVector);
		if (Fight->GetBoss())
		{
			AimAt(PC, Kate, Fight->GetBoss()->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		}
		if (!bShotIntro && Now - StepSince > 4.0)
		{
			bShotIntro = true;
			Shot(TEXT("social_intro.png"));
		}
		if (Fight->GetState() == EBossFightState::Fight)
		{
			Marks.Add(TEXT("fight"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f the intro is over (%.1f s); the fight starts"), T, Now - StepSince));
			Enter(EStep::Fight, Now);
		}
		else if (Now - StepSince > 60.0)
		{
			Fail(TEXT("the intro never ended"));
			Report(World);
			return true;
		}
		return false;
	case EStep::Fight:
	{
		if (Fight->GetState() == EBossFightState::Defeated || Fight->GetState() == EBossFightState::Done)
		{
			ReleaseInputs(PC);
			Marks.Add(TEXT("tomas_down"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f Tomas is down"), T));
			Enter(EStep::Defeated, Now);
			return false;
		}
		const int32 PhaseIndex = Fight->GetPhaseIndex();
		const double InPhase = T - Marks.FindRef(FString::Printf(TEXT("phase_%d"), PhaseIndex));
		if (PhaseIndex == 0 && !bShotCrew && InPhase > 4.0)
		{
			bShotCrew = true;
			Shot(TEXT("social_crew.png"));
		}
		if (PhaseIndex == 1 && !bShotTomas && (Fight->IsWinded() || InPhase > 12.0))
		{
			bShotTomas = true;
			Shot(Fight->IsWinded() ? TEXT("social_tomas_winded.png") : TEXT("social_tomas.png"));
		}
		const ABossArenaHazard* Hazard = Fight->GetHazard();
		if (PhaseIndex == 2 && !bShotSanta && Hazard && Hazard->IsLoose() && Hazard->GetState().Phase == EBossHazardPhase::Sweep
			&& Hazard->GetState().Alpha > 0.35f)
		{
			bShotSanta = true;
			Shot(TEXT("social_santa.png"));
		}
		const AThugCharacter* Tomas = Fight->GetBoss();
		if (PhaseIndex == 1 && !bRoundTripDone && Tomas && Tomas->GetHealthComponent()->GetHealthPercent() < 0.85f && !Fight->IsWinded()
			&& Kate->GetCharacterMovement()->IsMovingOnGround() && !Kate->IsMeleeAttacking() && !bDrawing && !bMeleeHeld)
		{
			ReleaseInputs(PC);
			BeginRoundTrip(World, Kate, Now);
			return false;
		}
		FightStep(World, PC, Kate, Now);
		return false;
	}
	case EStep::Defeated:
		Move(PC, FVector2D::ZeroVector);
		if (!bShotDown && Now - StepSince > 1.5)
		{
			bShotDown = true;
			if (Fight->GetBoss())
			{
				AimAt(PC, Kate, Fight->GetBoss()->GetActorLocation());
			}
			Shot(TEXT("social_down.png"));
		}
		if (Missions->IsMissionComplete())
		{
			Marks.Add(TEXT("chapter_complete"), T);
			Timeline.Add(FString::Printf(TEXT("%.1f the chapter is complete (%.1f s of after lines)"), T, Now - StepSince));
			Enter(EStep::Ending, Now);
		}
		else if (Now - StepSince > Fight->GetDefinition()->AfterMaxSeconds + 10.0)
		{
			Fail(TEXT("the chapter never completed after Tomas went down"));
			Report(World);
			return true;
		}
		return false;
	case EStep::Ending:
	{
		const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
		const UMissionFlowController* Flow = HawkeyePC ? HawkeyePC->GetMissionFlow() : nullptr;
		const bool bCard = Flow && Flow->GetStep() == EMissionFlowStep::EndCard;
		if (!bShotEnd && bCard && Now - StepSince > 1.0)
		{
			bShotEnd = true;
			Shot(TEXT("social_endcard.png"));
		}
		if ((bShotEnd && Now - StepSince > 2.0) || Now - StepSince > 12.0)
		{
			Test->TestTrue(TEXT("The end card shows"), bShotEnd);
			Enter(EStep::Done, Now);
			Report(World);
			return true;
		}
		return false;
	}
	default:
		return true;
	}
}

void FHawkeyeSocialClubLap::Report(UWorld* World)
{
	using namespace HawkeyeSocialLap;
	const UBossFightSubsystem* Fight = World ? UBossFightSubsystem::Get(World) : nullptr;
	const UMissionSubsystem* Missions = World ? UMissionSubsystem::Get(World) : nullptr;
	const ABossArenaHazard* Hazard = Fight ? Fight->GetHazard() : nullptr;
	HazardHits = Hazard ? Hazard->GetHitCount() : 0;
	const bool bComplete = Missions && Missions->IsMissionComplete();
	Test->TestTrue(TEXT("The lap completes the chapter"), bComplete);
	Test->TestTrue(TEXT("The round trip happened and was checked"), bRoundTripChecked);
	Test->TestTrue(TEXT("Tomas went winded at least once"), Fight && Fight->GetWindedCount() > 0);
	Test->TestTrue(TEXT("The confetti arrow was loosed"), bConfettiLoosed);
	FString Json = TEXT("{\n");
	Json += FString::Printf(TEXT("  \"completed\": %s,\n"), bComplete ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"wall_seconds\": %.1f,\n"), FPlatformTime::Seconds() - WallStart);
	Json += FString::Printf(TEXT("  \"difficulty\": \"%s\",\n"), World ? *DifficultyName(World) : TEXT(""));
	Json += TEXT("  \"marks\": {");
	TArray<FString> MarkLines;
	for (const TPair<FString, double>& Mark : Marks)
	{
		MarkLines.Add(FString::Printf(TEXT("\"%s\": %.1f"), *Mark.Key, Mark.Value));
	}
	Json += FString::Join(MarkLines, TEXT(", ")) + TEXT("},\n");
	Json += FString::Printf(TEXT("  \"hits_taken\": %d,\n  \"damage_taken\": %.0f,\n  \"hazard_hits\": %d,\n  \"hazard_dodges\": %d,\n"), HitsTaken,
		DamageTaken, HazardHits, HazardDodges);
	Json += FString::Printf(TEXT("  \"strikes\": %d,\n  \"parries\": %d,\n  \"finishers\": %d,\n  \"dodges\": %d,\n  \"deaths\": %d,\n"), Strikes, Parries,
		Finishers, Dodges, Deaths);
	Json += FString::Printf(TEXT("  \"winded\": %d,\n  \"confetti\": %s,\n"), Fight ? Fight->GetWindedCount() : 0, bConfettiLoosed ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"round_trip\": \"%s\",\n"), *Escape(RoundTrip));
	TArray<FString> Quoted;
	if (Fight)
	{
		for (const FString& Bark : Fight->GetBarksSpoken())
		{
			Quoted.Add(TEXT("\"") + Escape(Bark) + TEXT("\""));
		}
	}
	Json += TEXT("  \"barks\": [") + FString::Join(Quoted, TEXT(", ")) + TEXT("],\n");
	Quoted.Reset();
	for (const FString& Line : Timeline)
	{
		Quoted.Add(TEXT("\"") + Escape(Line) + TEXT("\""));
	}
	Json += TEXT("  \"timeline\": [\n    ") + FString::Join(Quoted, TEXT(",\n    ")) + TEXT("\n  ],\n");
	Quoted.Reset();
	for (const FString& Name : Shots)
	{
		Quoted.Add(TEXT("\"") + Name + TEXT("\""));
	}
	Json += TEXT("  \"shots\": [") + FString::Join(Quoted, TEXT(", ")) + TEXT("],\n");
	Quoted.Reset();
	for (const FString& Why : Failures)
	{
		Quoted.Add(TEXT("\"") + Escape(Why) + TEXT("\""));
	}
	Json += TEXT("  \"failures\": [") + FString::Join(Quoted, TEXT(", ")) + TEXT("]\n}\n");
	WriteText(TEXT("social_club_lap.json"), Json);
	Test->AddInfo(FString::Printf(TEXT("Social club lap: %s in %.1f s wall; hits %d (%.0f), hazard hits %d, strikes %d, parries %d, finishers %d, winded %d, deaths %d."),
		bComplete ? TEXT("completed") : TEXT("NOT completed"), FPlatformTime::Seconds() - WallStart, HitsTaken, DamageTaken, HazardHits, Strikes,
		Parries, Finishers, Fight ? Fight->GetWindedCount() : 0, Deaths));
	for (const FString& Line : Timeline)
	{
		Test->AddInfo(TEXT("  ") + Line);
	}
	if (UHawkeyeSaveSubsystem* Save = World ? UHawkeyeSaveSubsystem::Get(World) : nullptr)
	{
		IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
		Save->SlotNameOverride.Reset();
	}
}

bool FHawkeyeLapSocialClub::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the social club lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	if (GIsEditor)
	{
		AddInfo(TEXT("The social club lap saves and loads the district; run it from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeSocialClubLap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
