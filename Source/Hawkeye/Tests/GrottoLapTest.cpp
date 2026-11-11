#include "Boss/BossFightSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/ReturningArrowProjectile.h"
#include "Combat/TakedownComponent.h"
#include "Components/BoxComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HawkeyeGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Mission/ObjectiveTriggerVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Phone/PhoneInbox.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/InteriorTestKit.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/LapFightPressure.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/ArrowSwitch.h"
#include "World/ChapterEndInteractable.h"
#include "World/DoorActor.h"
#include "World/InteractionComponent.h"
#include "World/InteriorEntrance.h"
#include "World/InteriorExit.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugHearing.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Chapter 3, "The Grotto", played by a script in the standalone game (docs/chapters/CH03.md; claude-docs/testing.md, the
 * grotto laps), twice:
 *
 *   Hawkeye.Lap.Grotto       the stealth route. The district loads on chapter 1; the lap starts DA_CH03_Grotto the way
 *                            chapter select does and its opening, runs the navmesh to the grotto's frontage (find_sleigh),
 *                            goes in by its door (the chapter carries on inside L_Int_Grotto), creeps up on the hall's
 *                            elf and takes him down, finds the boomerang on the stockroom shelf past the two elves talking
 *                            (it hears them), looses the boomerang at the alarm box so the corridor's guard comes to look,
 *                            catches it, takes the guard down at the box, reaches the workshop door unseen (enter_grotto),
 *                            saves and loads inside the interior and checks it came back the same, opens the door at the
 *                            keypad (open_vault), beats Santa, leaves by the roof stairs and finishes on the roof (leave_roof);
 *                            then the end card, the call from the farm, the ornament at her door (found_ornament), and
 *                            chapter 4 started after it with chapter 3's boomerangs in its quiver. No elf is ever Alerted.
 *   Hawkeye.Lap.SeenGrotto   the seen route: crouched to the stockroom shelf, the boomerang loosed out through the
 *                            archway and her back turned so it comes back into her (the self-hit: a stagger, 2 damage,
 *                            never her last health, "Ow. It came back."); then into the hall standing up: the hall's elf
 *                            sees her, the objective's title changes to its spotted one and her line plays, and it is a
 *                            fight; every elf down, the keypad, Santa, the roof.
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -Difficulty=Normal -ExecCmds="Automation RunTests Hawkeye.Lap.Grotto; Quit"
 *
 * Each in a process of its own. Writes Saved/Automation/grotto_lap.json or grotto_loud_lap.json and
 * Saved/Screenshots/Grotto/grotto_*.png (frontage, inside with an elf, the boomerang out and back, the vault, Santa,
 * the roof, the call, the ornament). The points are Grotto.json's.
 */
namespace HawkeyeGrottoLap
{
	using namespace HawkeyeInteriorKit;

	static const TCHAR* DistrictPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* GrottoPath = TEXT("/Game/Maps/L_Int_Grotto");
	static const TCHAR* Chapter = TEXT("DA_CH03_Grotto");
	static const TCHAR* PreviousChapter = TEXT("DA_CH02_ReindeerGames");
	static const TCHAR* NextChapter = TEXT("DA_CH04_SocialClub");
	static const TCHAR* Slot = TEXT("HawkeyeGrottoLap");
	static const FName RoofTag(TEXT("City_InteriorRoof_Grotto"));
	static const FName DoorTag(TEXT("Site_GrottoDoor"));
	static const FName Flag(TEXT("found_ornament"));
	static constexpr int32 SlotStandard = 1;
	static constexpr int32 SlotFoam = 3;
	static constexpr int32 SlotBoomerang = 4;

	/** Grotto.json's places (the interior map's frame, floor at 0). */
	static const FVector HallEast(1050.f, 260.f, 0.f);
	static const FVector StockArch(1330.f, 250.f, 0.f);
	static const FVector StockIn(1520.f, 230.f, 0.f);
	static const FVector Shelf(1830.f, 250.f, 0.f);
	static const FVector ShootSpot(520.f, 640.f, 0.f);
	static const FVector AlarmBox(12.f, 700.f, 150.f);
	static const FVector CorridorArch(300.f, 960.f, 0.f);
	static const FVector CorridorIn(320.f, 1150.f, 0.f);
	static const FVector WorkshopFront(1200.f, 1180.f, 0.f);
	static const FVector Keypad(1330.f, 1285.f, 0.f);
	static const FVector InWorkshop(1200.f, 1450.f, 0.f);
	static const FVector RoofStairs(1500.f, 1930.f, 0.f);
	static const FBox2D Workshop(FVector2D(700.f, 1300.f), FVector2D(1700.f, 2000.f));
	static const FBox2D Hall(FVector2D(0.f, 0.f), FVector2D(1400.f, 1000.f));

	enum class EStep : uint8 { Running, Done, Failed };

	class FRunner;
	using FStepFn = TFunction<EStep(FRunner&, UWorld*, AHawkeyePlayerController*, AHawkeyeCharacter*, double)>;

	struct FStep
	{
		FString Name;
		float Timeout = 30.f;
		FStepFn Run;
	};

	static FString ShotPath(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Grotto") / Name);
	}

	static bool IsDone(const UWorld* World, FName Id)
	{
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionObjective* Objective = Missions && Missions->GetTracker() ? Missions->GetTracker()->FindObjective(Id) : nullptr;
		return Objective && Objective->IsCompleted();
	}

	static FName CurrentId(const UWorld* World)
	{
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
		return Current ? Current->ObjectiveId : NAME_None;
	}

	static TArray<AThugCharacter*> Elves(UWorld* World)
	{
		TArray<AThugCharacter*> Out;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->Tags.ContainsByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("enemy:")); }))
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	static void AimCamera(AHawkeyePlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Target - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
	}

	class FRunner : public IAutomationLatentCommand
	{
	public:
		FRunner(FAutomationTestBase* InTest, bool bInLoud) : Test(InTest), bLoud(bInLoud) {}

		virtual bool Update() override;

		FAutomationTestBase* Test;
		bool bLoud = false;
		TArray<FStep> Steps;
		int32 StepIndex = -1;
		double StepStart = 0.0;
		double LapStart = -1.0;
		FString StepDetail;
		TArray<FString> Legs;
		TArray<FString> Shots;
		TMap<FString, double> Marks;
		bool bFailed = false;
		bool bFinished = false;
		TWeakObjectPtr<UWorld> LastWorld;

		TArray<FVector> Path;
		int32 PathIndex = 0;
		double PathAt = -10.0;
		double LastProgress = 0.0;
		float BestDistance = 1.e9f;
		double LastJump = 0.0;
		bool bCrouchWanted = false;
		bool bMoveHeld = false;
		bool bSprintHeld = false;
		bool bCrouchKeyDown = false;
		double CrouchKeyAt = -10.0;

		int32 Phase = 0;
		double PhaseAt = 0.0;
		double LastLog = 0.0;

		TMap<TWeakObjectPtr<AThugCharacter>, EThugAlertState> Seen;
		int32 ElfAlerts = 0;
		int32 ElfSuspicions = 0;
		TArray<FString> AlertNotes;
		int32 Takedowns = 0;
		bool bWasTakingDown = false;
		int32 Hits = 0;
		float Damage = 0.f;
		float LastHealth = -1.f;
		int32 LightSwings = 0;
		int32 HeavySwings = 0;
		int32 Dodges = 0;
		int32 ArrowsLoosed = 0;
		int32 Finishers = 0;
		bool bMeleeHeld = false;
		double MeleeHeldSince = 0.0;
		bool bDrawing = false;
		double DrawStart = 0.0;
		TWeakObjectPtr<AThugCharacter> DrawTarget;
		double NextActionAt = 0.0;

		/** The boomerang: when it was loosed, how it came home. */
		int32 CaughtAtStart = 0;
		int32 SelfHitsAtStart = 0;
		float HealthBeforeSelfHit = 0.f;
		double BoomerangRetryAt = 0.0;
		FString BoomerangNote;

		/** The round trip inside the interior. */
		FVector SavedKate = FVector::ZeroVector;
		TArray<FName> SavedObjectives;
		TArray<FName> SavedFired;
		int32 SavedBoomerangs = 0;
		int32 SavedElvesDown = 0;
		double ReloadStartedAt = 0.0;
		FString RoundTrip;
		FString ChapterFourCheck;
		double FightStart = 0.0;
		double FightSeconds = 0.0;

		void Note(const FString& Text) { StepDetail = Text; }

		void Shot(const TCHAR* Name)
		{
			HawkeyeShots::Request(Test, ShotPath(Name));
			Shots.Add(Name);
		}

		void Crouch(bool bCrouch) { bCrouchWanted = bCrouch; }

		bool IsCrouchSettled(const AHawkeyeCharacter* Kate) const { return Kate->IsCrouchWanted() == bCrouchWanted; }

		void PumpCrouch(AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (!Kate->IsCrouchToggle())
			{
				if (bCrouchKeyDown != bCrouchWanted)
				{
					Hold(PC, CrouchPath, bCrouchWanted);
					bCrouchKeyDown = bCrouchWanted;
				}
				return;
			}
			if (bCrouchKeyDown)
			{
				if (Now - CrouchKeyAt >= 0.3)
				{
					Hold(PC, CrouchPath, false);
					bCrouchKeyDown = false;
					CrouchKeyAt = Now;
				}
				return;
			}
			if (IsCrouchSettled(Kate) || Now - CrouchKeyAt < 0.15 || Kate->IsSliding() || Kate->IsRolling() || Kate->IsTraversing()
				|| Kate->IsZipping() || Kate->IsLockedOutByTakedown())
			{
				return;
			}
			Hold(PC, CrouchPath, true);
			bCrouchKeyDown = true;
			CrouchKeyAt = Now;
		}

		void StopMoving(AHawkeyePlayerController* PC)
		{
			if (bMoveHeld)
			{
				Hold(PC, MovePath, false);
				bMoveHeld = false;
			}
		}

		void Sprint(AHawkeyePlayerController* PC, bool bOn)
		{
			if (bOn != bSprintHeld)
			{
				Hold(PC, TEXT("/Game/Input/IA_Sprint.IA_Sprint"), bOn);
				bSprintHeld = bOn;
			}
		}

		void Forward(AHawkeyePlayerController* PC, float Yaw, float Pitch = -8.f)
		{
			PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
			Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
			bMoveHeld = true;
		}

		/** Lets go of every key the lap might be holding (a level change starts a new world and a new pawn). */
		void ReleaseAll(AHawkeyePlayerController* PC)
		{
			for (const TCHAR* Action : { MovePath, CrouchPath, FirePath, MeleePath, TEXT("/Game/Input/IA_Sprint.IA_Sprint") })
			{
				Hold(PC, Action, false);
			}
			bMoveHeld = bSprintHeld = bCrouchKeyDown = bMeleeHeld = bDrawing = false;
		}

		void ResetPath()
		{
			Path.Reset();
			PathIndex = 0;
			PathAt = -10.0;
			BestDistance = 1.e9f;
		}

		bool SteerTo(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, float Reach, double Now)
		{
			const FVector Here = Feet(Kate);
			if (FVector::Dist2D(Here, Goal) <= Reach && FMath::Abs(Here.Z - Goal.Z) < 150.f)
			{
				StopMoving(PC);
				return true;
			}
			if (Now - PathAt > 0.3)
			{
				PathAt = Now;
				Path.Reset();
				if (const UNavigationPath* Found = UNavigationSystemV1::FindPathToLocationSynchronously(World, Here + FVector(0.f, 0.f, 50.f),
					Goal + FVector(0.f, 0.f, 50.f)); Found && Found->IsValid())
				{
					Path = Found->PathPoints;
				}
				PathIndex = 1;
			}
			while (PathIndex < Path.Num() && FVector::Dist2D(Here, Path[PathIndex]) < 40.f && FMath::Abs(Here.Z - Path[PathIndex].Z) < 150.f)
			{
				++PathIndex;
			}
			while (PathIndex + 1 < Path.Num() && FVector::Dist2D(Here, Path[PathIndex + 1]) < FVector::Dist2D(Path[PathIndex], Path[PathIndex + 1])
				&& FMath::Abs(Here.Z - Path[PathIndex + 1].Z) < 150.f)
			{
				++PathIndex;
			}
			const FVector Next = PathIndex < Path.Num() ? Path[PathIndex] : Goal;
			const float Distance = FVector::Dist2D(Here, Goal);
			if (Distance < BestDistance - 5.f)
			{
				BestDistance = Distance;
				LastProgress = Now;
			}
			else if (Now - LastProgress > 1.5 && Now - LastJump > 1.5)
			{
				LastJump = Now;
				if (!bCrouchWanted)
				{
					Tap(PC, JumpPath);
				}
			}
			const FVector2D Dir = FVector2D(Next - Here).GetSafeNormal();
			Forward(PC, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
			return false;
		}

		static bool InteractHere(AHawkeyeCharacter* Kate)
		{
			UInteractionComponent* Interaction = Kate ? Kate->GetInteractionComponent() : nullptr;
			if (!Interaction)
			{
				return false;
			}
			Interaction->RefreshFocus();
			return Interaction->TryInteract();
		}

		static bool IsQuiet(UWorld* World, const AThugCharacter* Victim, const AHawkeyeCharacter* Kate, FString& OutWho)
		{
			for (AThugCharacter* Other : All<AThugCharacter>(World))
			{
				const AThugAIController* Brain = BrainOf(Other);
				if (Other == Victim || IsDown(Other) || !Brain || !Brain->IsThinkingEnabled())
				{
					continue;
				}
				const float Range = Brain->HearingRange * GetDefault<UTakedownComponent>()->TakedownNoiseLoudness;
				const HawkeyeThugHearing::FHeardNoise Heard = HawkeyeThugHearing::Hear(World, Victim->GetActorLocation(),
					Other->GetActorLocation() + FVector(0.f, 0.f, 60.f), Range, { Victim, Other, Kate });
				if (Heard.bHeard)
				{
					OutWho = Other->GetName();
					return false;
				}
			}
			return true;
		}

		static bool IsInHisEyes(UWorld* World, const AThugCharacter* Thug, const AHawkeyeCharacter* Kate)
		{
			const FVector To = Kate->GetActorLocation() - Thug->GetActorLocation();
			if (To.Size2D() > 1600.f || FVector::DotProduct(Thug->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) < 0.5f)
			{
				return false;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrottoLapEyes), false, Kate);
			Params.AddIgnoredActor(Thug);
			return !World->LineTraceTestByChannel(Thug->GetActorLocation() + FVector(0.f, 0.f, 60.f), Kate->GetActorLocation(), ECC_Visibility, Params);
		}

		void TrackStats(UWorld* World, AHawkeyeCharacter* Kate);
		EStep Fight(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const TArray<AThugCharacter*>& Group, double Now);
		void EndStep(bool bOk, double Now);
		void Finish(UWorld* World);
	};

	void FRunner::TrackStats(UWorld* World, AHawkeyeCharacter* Kate)
	{
		for (AThugCharacter* Thug : Elves(World))
		{
			const EThugAlertState State = Thug->GetAlertState();
			EThugAlertState& Was = Seen.FindOrAdd(Thug, EThugAlertState::Calm);
			if (State != Was && !IsDown(Thug))
			{
				const FString Where = StepIndex >= 0 && StepIndex < Steps.Num() ? Steps[StepIndex].Name : TEXT("?");
				if (State == EThugAlertState::Alerted)
				{
					++ElfAlerts;
					AlertNotes.Add(FString::Printf(TEXT("%s alerted in '%s'"), *Thug->GetName(), *Where));
				}
				else if (State == EThugAlertState::Suspicious && Was == EThugAlertState::Calm)
				{
					++ElfSuspicions;
					AlertNotes.Add(FString::Printf(TEXT("%s suspicious in '%s'"), *Thug->GetName(), *Where));
				}
			}
			Was = State;
		}
		if (!Kate)
		{
			return;
		}
		const bool bTakingDown = Kate->GetTakedownComponent() && Kate->GetTakedownComponent()->IsPerformingTakedown();
		Takedowns += bTakingDown && !bWasTakingDown ? 1 : 0;
		bWasTakingDown = bTakingDown;
		if (const UHealthComponent* Health = Kate->GetHealthComponent())
		{
			const float Now = Health->GetCurrentHealth();
			if (LastHealth >= 0.f && Now < LastHealth - 0.01f)
			{
				++Hits;
				Damage += LastHealth - Now;
			}
			LastHealth = Now;
		}
	}

	/** Fists and heavies, dodges when a swing is about to land, a finisher whenever one is open, arrows at range. */
	EStep FRunner::Fight(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const TArray<AThugCharacter*>& Group, double Now)
	{
		UBowComponent* Bow = Kate->GetBowComponent();
		AThugCharacter* Nearest = nullptr;
		float Distance = BIG_NUMBER;
		int32 Alive = 0;
		for (AThugCharacter* Thug : Group)
		{
			if (IsDown(Thug))
			{
				continue;
			}
			++Alive;
			const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
			if (D < Distance)
			{
				Distance = D;
				Nearest = Thug;
			}
		}
		if (Alive == 0)
		{
			StopMoving(PC);
			Hold(PC, FirePath, false);
			Hold(PC, MeleePath, false);
			bMeleeHeld = bDrawing = false;
			if (Bow)
			{
				Bow->ClearAimOverride();
			}
			return EStep::Done;
		}
		if (Kate->IsDowned() || !Kate->GetHealthComponent()->IsAlive())
		{
			Note(TEXT("Kate went down"));
			return EStep::Failed;
		}
		AThugCharacter* Target = bDrawing && DrawTarget.IsValid() && !IsDown(DrawTarget.Get()) ? DrawTarget.Get() : Nearest;
		AimCamera(PC, Kate, Target->GetActorLocation() + FVector(0.f, 0.f, 30.f));
		if (bMeleeHeld)
		{
			if (Now - MeleeHeldSince >= 0.45)
			{
				Hold(PC, MeleePath, false);
				bMeleeHeld = false;
				NextActionAt = Now + 0.1;
			}
			StopMoving(PC);
			return EStep::Running;
		}
		if (bDrawing)
		{
			if (AThugCharacter* Aimed = DrawTarget.Get(); Aimed && Bow)
			{
				Bow->SetAimOverride(Aimed->GetActorLocation() + FVector(0.f, 0.f, 30.f));
			}
			if (Now - DrawStart >= 0.9 || IsDown(DrawTarget.Get()))
			{
				++ArrowsLoosed;
				Hold(PC, FirePath, false);
				bDrawing = false;
				NextActionAt = Now + 0.2;
				if (Bow)
				{
					Bow->ClearAimOverride();
				}
			}
			return EStep::Running;
		}
		const bool bBusy = Kate->IsMeleeAttacking() || Kate->IsDodging() || Kate->IsStaggered() || Kate->IsLockedOutByTakedown();
		if (!bBusy && Kate->GetDodgeCooldownRemaining() <= 0.f)
		{
			for (const AThugCharacter* T : Group)
			{
				const UMeleeComponent* Melee = T && !IsDown(T) ? T->GetMeleeComponent() : nullptr;
				if (!Melee || T->IsGunner() || T->IsArcher() || !Melee->IsWindingUp() || Melee->GetPhaseRemaining() > 0.3f
					|| FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) > Melee->GetCurrentAttack().Range + 120.f)
				{
					continue;
				}
				const FVector Away = (Kate->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
				if (Kate->TryDodge((Away + FVector::CrossProduct(FVector::UpVector, Away)).GetSafeNormal2D()))
				{
					++Dodges;
					NextActionAt = Now + 0.3;
					return EStep::Running;
				}
			}
		}
		if (bBusy || Now < NextActionAt)
		{
			return EStep::Running;
		}
		if (Kate->GetFinisherComponent() && Kate->GetFinisherComponent()->FindTarget())
		{
			StopMoving(PC);
			Tap(PC, TakedownPath);
			++Finishers;
			NextActionAt = Now + 0.3;
			return EStep::Running;
		}
		const float Flat = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
		if (Flat > 700.f && Bow && Kate->GetInventoryComponent()->GetArrowCount(SlotStandard) > 0)
		{
			StopMoving(PC);
			Kate->GetInventoryComponent()->SelectArrowSlot(SlotStandard);
			DrawTarget = Target;
			DrawStart = Now;
			bDrawing = true;
			Hold(PC, FirePath, true);
			return EStep::Running;
		}
		if (Flat > 180.f)
		{
			SteerTo(World, PC, Kate, Target->GetActorLocation() - FVector(0.f, 0.f, 96.f), 150.f, Now);
			return EStep::Running;
		}
		StopMoving(PC);
		int32 Near = 0;
		for (const AThugCharacter* T : Group)
		{
			Near += !IsDown(T) && FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) <= 400.f ? 1 : 0;
		}
		const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
		if ((ThugMelee && ThugMelee->IsWindingUp()) || Target->IsIncapacitated() || Near > 1)
		{
			Tap(PC, MeleePath);
			++LightSwings;
			NextActionAt = Now + 0.32;
			return EStep::Running;
		}
		Hold(PC, MeleePath, true);
		bMeleeHeld = true;
		MeleeHeldSince = Now;
		++HeavySwings;
		return EStep::Running;
	}

	void FRunner::EndStep(bool bOk, double Now)
	{
		const FString& Name = Steps[StepIndex].Name;
		Legs.Add(FString::Printf(TEXT("    {\"leg\": \"%s\", \"ok\": %s, \"at\": %.1f, \"seconds\": %.2f, \"detail\": \"%s\"}"), *Name,
			bOk ? TEXT("true") : TEXT("false"), Now - LapStart, Now - StepStart, *StepDetail.ReplaceCharWithEscapedChar()));
		Test->AddInfo(FString::Printf(TEXT("Grotto %s: %s in %.1f s: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Name, Now - StepStart, *StepDetail));
		if (!bOk)
		{
			Test->AddError(FString::Printf(TEXT("Grotto %s lap: '%s' failed: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Name, *StepDetail));
			bFailed = true;
		}
	}

	void FRunner::Finish(UWorld* World)
	{
		bFinished = true;
		if (AHawkeyePlayerController* PC = FindController(World))
		{
			ReleaseAll(PC);
		}
		const double Seconds = FPlatformTime::Seconds() - LapStart;
		FString MarkText;
		for (const TPair<FString, double>& Mark : Marks)
		{
			MarkText += FString::Printf(TEXT("%s\"%s\": %.1f"), MarkText.IsEmpty() ? TEXT("") : TEXT(", "), *Mark.Key, Mark.Value);
		}
		const FString Json = FString::Printf(TEXT("{\n  \"test\": \"Hawkeye.Lap.%s\",\n  \"completed\": %s,\n  \"seconds\": %.1f,\n  \"marks\": {%s},\n"
			"  \"elf_alerts\": %d,\n  \"elf_suspicions\": %d,\n  \"takedowns\": %d,\n  \"hits_taken\": %d,\n  \"damage_taken\": %.1f,\n"
			"  \"light_swings\": %d,\n  \"heavy_swings\": %d,\n  \"dodges\": %d,\n  \"arrows\": %d,\n  \"finishers\": %d,\n  \"santa_fight_seconds\": %.1f,\n"
			"  \"boomerang\": \"%s\",\n  \"round_trip\": \"%s\",\n  \"chapter_four\": \"%s\",\n  \"shots\": \"%s\",\n  \"notes\": \"%s\",\n  \"legs\": [\n%s\n  ]\n}\n"),
			bLoud ? TEXT("SeenGrotto") : TEXT("Grotto"), bFailed ? TEXT("false") : TEXT("true"), Seconds, *MarkText, ElfAlerts, ElfSuspicions, Takedowns,
			Hits, Damage, LightSwings, HeavySwings, Dodges, ArrowsLoosed, Finishers, FightSeconds, *BoomerangNote.ReplaceCharWithEscapedChar(),
			*RoundTrip.ReplaceCharWithEscapedChar(), *ChapterFourCheck.ReplaceCharWithEscapedChar(), *FString::Join(Shots, TEXT(", ")),
			*FString::Join(AlertNotes, TEXT("; ")).ReplaceCharWithEscapedChar(), *FString::Join(Legs, TEXT(",\n")));
		WriteText(bLoud ? TEXT("grotto_loud_lap.json") : TEXT("grotto_lap.json"),
			HawkeyeLapPressure::Finish(Test, bLoud ? TEXT("SeenGrotto") : TEXT("Grotto"), Json));
		Test->AddInfo(FString::Printf(TEXT("%s:\n%s"), bLoud ? TEXT("grotto_loud_lap.json") : TEXT("grotto_lap.json"), *Json));
		UE_LOG(LogTemp, Display, TEXT("[Hawkeye] grotto %s lap: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Json);
		if (!bLoud)
		{
			Test->TestEqual(FString::Printf(TEXT("Stealth: no elf was ever Alerted (%s)"), *FString::Join(AlertNotes, TEXT("; "))), ElfAlerts, 0);
		}
		if (UHawkeyeSaveSubsystem* Saves = World ? UHawkeyeSaveSubsystem::Get(World) : nullptr)
		{
			IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
			Saves->SlotNameOverride.Reset();
		}
	}

	bool FRunner::Update()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (bFinished)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (StepIndex < 0)
		{
			StepIndex = 0;
			StepStart = Now;
			LapStart = Now;
			ResetPath();
		}
		if (World && World != LastWorld.Get())
		{
			// A level change: the old world's held keys went with it.
			LastWorld = World;
			if (PC)
			{
				ReleaseAll(PC);
			}
			ResetPath();
			LastHealth = Kate && Kate->GetHealthComponent() ? Kate->GetHealthComponent()->GetCurrentHealth() : -1.f;
		}
		if (World && IsMap(World, GrottoPath))
		{
			TrackStats(World, Kate);
		}
		if (StepIndex >= Steps.Num())
		{
			Finish(World);
			return true;
		}
		FStep& Step = Steps[StepIndex];
		EStep Result = EStep::Running;
		if (Now - StepStart > Step.Timeout)
		{
			Note(FString::Printf(TEXT("timed out after %.0f s (%s); Kate at %s"), Step.Timeout, *StepDetail,
				Kate ? *Kate->GetActorLocation().ToCompactString() : TEXT("?")));
			Result = EStep::Failed;
		}
		else if (World && PC && Kate)
		{
			PumpCrouch(PC, Kate, Now);
			Result = Step.Run(*this, World, PC, Kate, Now);
		}
		if (Result == EStep::Running)
		{
			return false;
		}
		if (PC)
		{
			StopMoving(PC);
		}
		EndStep(Result == EStep::Done, Now);
		if (Result == EStep::Failed)
		{
			Finish(World);
			return true;
		}
		++StepIndex;
		StepStart = Now;
		StepDetail.Reset();
		Phase = 0;
		PhaseAt = Now;
		ResetPath();
		return false;
	}

	// --- the steps -------------------------------------------------------------------------------------------

	static void Add(FRunner& R, const FString& Name, float Timeout, FStepFn Fn)
	{
		R.Steps.Add({ Name, Timeout, MoveTemp(Fn) });
	}

	/** Walk (or creep) to Goal in this map. */
	static void AddWalk(FRunner& R, const FString& Name, const FVector& Goal, bool bCrouch, float Reach = 45.f, float Timeout = 40.f, bool bSprint = false)
	{
		Add(R, Name, Timeout, [Goal, bCrouch, Reach, bSprint](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (Run.bCrouchWanted != bCrouch)
			{
				Run.StopMoving(PC);
				Run.Crouch(bCrouch);
				Run.PhaseAt = Now;
			}
			if (!Run.IsCrouchSettled(Kate) && Now - Run.PhaseAt < 1.0)
			{
				return EStep::Running;
			}
			Run.Sprint(PC, bSprint && !bCrouch);
			if (Run.SteerTo(World, PC, Kate, Goal, Reach, Now))
			{
				Run.Sprint(PC, false);
				Run.Note(FString::Printf(TEXT("at %s"), *Feet(Kate).ToCompactString()));
				return EStep::Done;
			}
			Run.Note(FString::Printf(TEXT("walking to %s, at %s"), *Goal.ToCompactString(), *Feet(Kate).ToCompactString()));
			return EStep::Running;
		});
	}

	/** Creep up behind the enemy Id and take him down once Go says so (InteriorLapTest's stalk). */
	static void AddStalk(FRunner& R, const FString& Name, const TCHAR* Id, TFunction<bool(const AThugCharacter*)> Go, float Timeout = 60.f)
	{
		const FString Enemy(Id);
		Add(R, Name, Timeout, [Enemy, Go](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			AThugCharacter* Thug = HawkeyeInteriorKit::Enemy(World, *Enemy);
			if (!Thug)
			{
				Run.Note(TEXT("no such enemy"));
				return EStep::Failed;
			}
			if (IsDown(Thug))
			{
				Run.Crouch(true);
				Run.Note(FString::Printf(TEXT("%s down at %s"), *Enemy, *Thug->GetActorLocation().ToCompactString()));
				return Now - Run.PhaseAt > 1.3 ? EStep::Done : EStep::Running;
			}
			if (Thug->IsAlerted())
			{
				Run.Note(FString::Printf(TEXT("%s saw her (%s)"), *Enemy, *Run.StepDetail));
				return EStep::Failed;
			}
			if (Now - Run.LastLog > 0.5)
			{
				Run.LastLog = Now;
				UE_LOG(LogTemp, Display, TEXT("[Hawkeye] grotto stalk %s: Kate %s %s, him %s yaw %.0f %s %.0f cm/s; %s"), *Enemy, *Feet(Kate).ToCompactString(),
					Kate->bIsCrouched ? TEXT("crouched") : TEXT("standing"), *Thug->GetActorLocation().ToCompactString(), Thug->GetActorRotation().Yaw,
					Thug->GetAlertState() == EThugAlertState::Suspicious ? TEXT("suspicious") : TEXT("calm"), Thug->GetVelocity().Size2D(), *Run.StepDetail);
			}
			if (!Run.bCrouchWanted)
			{
				Run.StopMoving(PC);
				Run.Crouch(true);
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if ((!Run.IsCrouchSettled(Kate) && Now - Run.PhaseAt < 1.0) || Kate->IsLockedOutByTakedown())
			{
				return EStep::Running;
			}
			UTakedownComponent* Takedown = Kate->GetTakedownComponent();
			FString Loud;
			if (Takedown && Takedown->FindTakedownTarget() == Thug)
			{
				Run.StopMoving(PC);
				if (FRunner::IsQuiet(World, Thug, Kate, Loud))
				{
					Tap(PC, TakedownPath);
					Run.PhaseAt = Now;
					Run.Note(FString::Printf(TEXT("took %s down at %s"), *Enemy, *Thug->GetActorLocation().ToCompactString()));
				}
				else
				{
					Run.Note(FString::Printf(TEXT("behind %s, waiting: %s would hear the body"), *Enemy, *Loud));
				}
				return EStep::Running;
			}
			if (Run.Phase == 0 && !Go(Thug))
			{
				Run.StopMoving(PC);
				Run.Note(FString::Printf(TEXT("waiting for %s (at %s)"), *Enemy, *Thug->GetActorLocation().ToCompactString()));
				return EStep::Running;
			}
			Run.Phase = 1;
			if (FRunner::IsInHisEyes(World, Thug, Kate))
			{
				Run.StopMoving(PC);
				Run.Note(FString::Printf(TEXT("in %s's eyes, frozen"), *Enemy));
				return EStep::Running;
			}
			const FVector Behind = Thug->GetActorLocation() - Thug->GetActorForwardVector().GetSafeNormal2D() * 95.f
				- FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			Run.SteerTo(World, PC, Kate, Behind, 25.f, Now);
			Run.Note(FString::Printf(TEXT("closing on %s, %.0f cm"), *Enemy, FVector::Dist2D(Kate->GetActorLocation(), Thug->GetActorLocation())));
			return EStep::Running;
		});
	}

	/** In the district: chapter select's way into chapter 3 and its opening, with the chapter order checked. */
	static void AddStart(FRunner& R)
	{
		Add(R, TEXT("start chapter 3 from the district"), 30.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (!IsMap(World, DistrictPath))
			{
				return EStep::Running;
			}
			AHawkeyeGameMode* Mode = World->GetAuthGameMode<AHawkeyeGameMode>();
			UMissionDefinition* Mission = AHawkeyeGameMode::FindChapter(Chapter);
			const UMissionDefinition* Previous = AHawkeyeGameMode::FindChapter(PreviousChapter);
			UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			if (!Mode || !Mission || !Site || !Save)
			{
				Run.Note(TEXT("no game mode, chapter 3, site or save subsystem"));
				return EStep::Failed;
			}
			Run.Test->TestTrue(TEXT("Chapter 2 leads to chapter 3"), Previous && Previous->NextChapter.ToSoftObjectPath() == FSoftObjectPath(Mission));
			Run.Test->TestEqual(TEXT("Chapter 3 leads to chapter 4"), Mission->NextChapter.ToSoftObjectPath().GetAssetName(), FString(NextChapter));
			Run.Test->TestFalse(TEXT("No partner in chapter 3"), Mission->bPartnerPresent);
			Save->SlotNameOverride = Slot;
			IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
			if (UHawkeyeCampaignState* Campaign = Save->GetCampaignState())
			{
				Campaign->CampaignFlags.Remove(Flag);
			}
			HawkeyeFreezePartner(World);
			FreezeThugs(World);
			if (!Mode->StartChapter(Mission))
			{
				Run.Note(TEXT("chapter 3 would not start"));
				return EStep::Failed;
			}
			Site->BeginChapter(Mission);
			const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
			Run.Test->TestEqual(TEXT("The street site: the grotto's door"), Site->GetInteriorDoors().Num(), 1);
			Run.Test->TestEqual(TEXT("...its doorstep and the roof point"), Site->GetReturnPoints().Num(), 2);
			Run.Test->TestEqual(TEXT("...the frontage's and the roof's volumes"), Site->GetVolumes().Num(), 2);
			Run.Test->TestEqual(TEXT("...the ornament"), Site->GetExamines().Num(), 1);
			Run.Test->TestEqual(TEXT("Chapter 3 starts with chapter 2's foam"), Inventory->GetArrowCount(SlotFoam), 2);
			Run.Test->TestEqual(TEXT("...and no boomerang yet"), Inventory->GetArrowCount(SlotBoomerang), 0);
			Run.Test->TestTrue(TEXT("Kate at the site's start"), FVector::Dist2D(Kate->GetActorLocation(), Site->GetDefinition()->PlayerStart) < 200.f);
			Run.LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
			Run.LapStart = Now;
			Run.Marks.Add(TEXT("start"), 0.0);
			Run.Note(FString::Printf(TEXT("Kate at %s, the door %.0f m away"), *Kate->GetActorLocation().ToCompactString(),
				Site->GetInteriorDoors().Num() ? FVector::Dist2D(Kate->GetActorLocation(), Site->GetInteriorDoors()[0]->GetActorLocation()) / 100.f : -1.f));
			return EStep::Done;
		});
	}

	/** Run the navmesh to the grotto's frontage (find_sleigh), then look at the door. */
	static void AddFrontage(FRunner& R, bool bWalk)
	{
		Add(R, TEXT("to the grotto's frontage"), 120.f, [bWalk](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			const TArray<AInteriorEntrance*> Doors = Site ? Site->GetInteriorDoors() : TArray<AInteriorEntrance*>();
			if (Doors.Num() != 1)
			{
				Run.Note(TEXT("no grotto door"));
				return EStep::Failed;
			}
			const FVector Out = Doors[0]->GetActorForwardVector();
			const FVector Front = Doors[0]->GetActorLocation() + Out * 380.f;
			if (!bWalk && Run.Phase == 0)
			{
				// The loud lap does not run the street again: put down in front of the grotto (a [lap] move).
				Place(World, Front, (-Out).Rotation().Yaw);
				Run.Phase = 1;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (IsDone(World, TEXT("find_sleigh")) && Run.SteerTo(World, PC, Kate, Front, 80.f, Now))
			{
				Run.Sprint(PC, false);
				AimCamera(PC, Kate, Doors[0]->GetActorLocation() + FVector(0.f, 0.f, 220.f));
				if (Run.Phase < 2)
				{
					Run.Phase = 2;
					Run.PhaseAt = Now;
					return EStep::Running;
				}
				if (Now - Run.PhaseAt > 1.0 && !Run.Shots.Contains(TEXT("grotto_frontage.png")))
				{
					Run.Shot(TEXT("grotto_frontage.png"));
					Run.Marks.Add(TEXT("frontage"), Now - Run.LapStart);
				}
				if (Run.Shots.Contains(TEXT("grotto_frontage.png")) && HawkeyeShots::GetOutstanding(Run.Test) == 0)
				{
					Run.Test->TestEqual(TEXT("The frontage completes find_sleigh; enter_grotto is next"), CurrentId(World), FName(TEXT("enter_grotto")));
					Run.Note(FString::Printf(TEXT("find_sleigh done, at the door %s"), *Doors[0]->GetActorLocation().ToCompactString()));
					return EStep::Done;
				}
				return EStep::Running;
			}
			Run.Sprint(PC, FVector::Dist2D(Kate->GetActorLocation(), Front) > 800.f);
			Run.SteerTo(World, PC, Kate, Front, 80.f, Now);
			Run.Note(FString::Printf(TEXT("%.0f m to go"), FVector::Dist2D(Kate->GetActorLocation(), Front) / 100.f));
			return EStep::Running;
		});
	}

	/** In by the grotto's door: the chapter carries on in L_Int_Grotto. */
	static void AddEnter(FRunner& R)
	{
		Add(R, TEXT("in by the grotto's door"), 60.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (IsMap(World, DistrictPath))
			{
				const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
				const TArray<AInteriorEntrance*> Doors = Site ? Site->GetInteriorDoors() : TArray<AInteriorEntrance*>();
				if (Doors.Num() == 1)
				{
					if (Doors[0]->IsTravelling())
					{
						Run.StopMoving(PC);
						return EStep::Running;
					}
					if (Doors[0]->GetEntryZone()->IsOverlappingActor(Kate))
					{
						Run.StopMoving(PC);
						FRunner::InteractHere(Kate);
						return EStep::Running;
					}
					Run.Forward(PC, (-Doors[0]->GetActorForwardVector()).Rotation().Yaw);
				}
				return EStep::Running;
			}
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			if (!IsMap(World, GrottoPath) || !Save || Save->IsLoading() || !Missions || !Missions->GetCurrentMission())
			{
				return EStep::Running;
			}
			if (Run.Phase < 10)
			{
				Run.Phase = 10;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Now - Run.PhaseAt < 1.0)
			{
				return EStep::Running;
			}
			HawkeyeFreezePartner(World);
			const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			Run.Test->TestTrue(TEXT("Inside: a chapter interior (saves allowed)"), Save->IsInChapterInterior());
			Run.Test->TestEqual(TEXT("Inside: chapter 3 carries on"), GetNameSafe(Missions->GetCurrentMission()), FString(Chapter));
			Run.Test->TestTrue(TEXT("Inside: find_sleigh still done"), IsDone(World, TEXT("find_sleigh")));
			Run.Test->TestEqual(TEXT("Inside: enter_grotto is current"), CurrentId(World), FName(TEXT("enter_grotto")));
			Run.Test->TestTrue(TEXT("Inside: the interior's site is dressed"), Site && Site->GetDefinition() && Site->GetPropCount() > 10 && Site->GetSwitches().Num() == 1);
			Run.Test->TestEqual(TEXT("Inside: four elves"), Elves(World).Num(), 4);
			for (const AThugCharacter* Elf : Elves(World))
			{
				Run.Test->TestTrue(TEXT("Every elf wears his hat"), Elf->GetHeadwearComponent() && Elf->GetHeadwearComponent()->GetStaticMesh());
			}
			Run.Test->TestTrue(TEXT("Inside: Santa waits in the workshop"), Fight && Fight->GetBoss() && Fight->GetState() == EBossFightState::Approach
				&& Workshop.IsInside(FVector2D(Fight->GetBoss()->GetActorLocation())));
			Run.Test->TestEqual(TEXT("Inside: she carried her quiver in (the foam)"), Kate->GetInventoryComponent()->GetArrowCount(SlotFoam), 2);
			Run.Test->TestTrue(TEXT("Inside: no event fired twice coming in (the hook and the outside not again)"),
				Events && Events->GetSpokenLines().Num() <= 1);
			Run.Marks.Add(TEXT("inside"), Now - Run.LapStart);
			Run.LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
			Run.Note(FString::Printf(TEXT("in L_Int_Grotto at %s, %d prop(s) dressed"), *Feet(Kate).ToCompactString(), Site ? Site->GetPropCount() : 0));
			return EStep::Done;
		});
	}

	/** Wait with the hall's elf in frame for the inside's screenshot. */
	static void AddInsideShot(FRunner& R)
	{
		Add(R, TEXT("the grotto with an elf in frame"), 20.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(true);
			const AThugCharacter* Elf = Enemy(World, TEXT("hall"));
			if (!Elf)
			{
				return EStep::Failed;
			}
			AimCamera(PC, Kate, Elf->GetActorLocation() + FVector(0.f, 0.f, 20.f));
			if (!Run.Shots.Contains(TEXT("grotto_inside.png")) && Now - Run.StepStart > 1.5)
			{
				Run.Shot(TEXT("grotto_inside.png"));
			}
			if (Run.Shots.Contains(TEXT("grotto_inside.png")) && HawkeyeShots::GetOutstanding(Run.Test) == 0)
			{
				const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
				Run.Test->TestTrue(TEXT("Inside the door: the first elf's line"), Events && Events->GetFired().Contains(TEXT("trigger:first_elf")));
				return EStep::Done;
			}
			return EStep::Running;
		});
	}

	/** On the stockroom shelf: the boomerang, the overheard talk on the way. */
	static void AddBoomerangPickup(FRunner& R)
	{
		AddWalk(R, TEXT("creep to the hall's east side"), HallEast, true, 60.f);
		AddWalk(R, TEXT("creep through the stockroom's archway"), StockIn, true, 50.f);
		Add(R, TEXT("the boomerang on the shelf"), 30.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(true);
			if (!Run.IsCrouchSettled(Kate))
			{
				return EStep::Running;
			}
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			if (Run.SteerTo(World, PC, Kate, Shelf, 60.f, Now) && Events && Events->GetFired().Contains(TEXT("trigger:boomerang")))
			{
				const int32 Count = Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang);
				Run.Test->TestEqual(TEXT("The boomerang found: two in slot 4"), Count, 2);
				Run.Test->TestTrue(TEXT("The two elves were overheard"), Events->GetFired().Contains(TEXT("trigger:overheard")));
				Run.Marks.Add(TEXT("boomerang"), Now - Run.LapStart);
				Run.Note(FString::Printf(TEXT("%d boomerangs; overheard %s"), Count, Events->GetFired().Contains(TEXT("trigger:overheard")) ? TEXT("yes") : TEXT("no")));
				return Count == 2 ? EStep::Done : EStep::Failed;
			}
			return EStep::Running;
		});
		AddWalk(R, TEXT("creep back out of the stockroom"), StockArch, true, 50.f);
	}

	/** The boomerang at the alarm box: it trips, the guard comes, it comes back into her hand. */
	static void AddLure(FRunner& R)
	{
		AddWalk(R, TEXT("creep to where the alarm box shows"), ShootSpot, true, 50.f);
		Add(R, TEXT("the boomerang at the alarm box"), 25.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			UBowComponent* Bow = Kate->GetBowComponent();
			UInventoryComponent* Inventory = Kate->GetInventoryComponent();
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			const AArrowSwitch* Switch = Site && Site->GetSwitches().Num() == 1 ? Site->GetSwitches()[0] : nullptr;
			if (!Bow || !Switch)
			{
				Run.Note(TEXT("no bow or no alarm box"));
				return EStep::Failed;
			}
			AimCamera(PC, Kate, AlarmBox);
			if (Run.Phase == 0)
			{
				Run.StopMoving(PC);
				Inventory->SelectArrowSlot(SlotBoomerang);
				Run.CaughtAtStart = AReturningArrowProjectile::CaughtCount;
				Run.SelfHitsAtStart = AReturningArrowProjectile::SelfHitCount;
				Bow->SetAimOverride(Switch->GetActorLocation());
				Hold(PC, FirePath, true);
				Run.bDrawing = true;
				Run.Phase = 1;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Run.Phase == 1)
			{
				Bow->SetAimOverride(Switch->GetActorLocation());
				if (Now - Run.PhaseAt < 0.85)
				{
					return EStep::Running;
				}
				Hold(PC, FirePath, false);
				Run.bDrawing = false;
				++Run.ArrowsLoosed;
				// Slowed while it flies, so the screenshots catch it (a [lap] aid: the flight lasts a fifth of a second).
				UGameplayStatics::SetGlobalTimeDilation(World, 0.1f);
				Run.Phase = 2;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			// In flight: a screenshot going out and one coming back.
			AReturningArrowProjectile* Flight = nullptr;
			for (AReturningArrowProjectile* Each : All<AReturningArrowProjectile>(World))
			{
				Flight = Each->GetReturnPhase() == EHawkeyeReturnPhase::Out || Each->GetReturnPhase() == EHawkeyeReturnPhase::Back ? Each : Flight;
			}
			if (Flight && Flight->GetReturnPhase() == EHawkeyeReturnPhase::Out && !Run.Shots.Contains(TEXT("grotto_boomerang_out.png"))
				&& Now - Run.PhaseAt > 0.6)
			{
				Run.Shot(TEXT("grotto_boomerang_out.png"));
			}
			if (Flight && Flight->GetReturnPhase() == EHawkeyeReturnPhase::Back && !Run.Shots.Contains(TEXT("grotto_boomerang_back.png"))
				&& FVector::Dist2D(Flight->GetActorLocation(), Kate->GetActorLocation()) < 220.f)
			{
				Run.Shot(TEXT("grotto_boomerang_back.png"));
			}
			const bool bHome = AReturningArrowProjectile::CaughtCount > Run.CaughtAtStart || AReturningArrowProjectile::SelfHitCount > Run.SelfHitsAtStart;
			if (!bHome || HawkeyeShots::GetOutstanding(Run.Test) > 0)
			{
				Run.Note(FString::Printf(TEXT("in flight (%s)"), Flight ? *UEnum::GetValueAsString(Flight->GetReturnPhase()) : TEXT("gone")));
				if (Now - Run.PhaseAt > 15.0)
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
					return EStep::Failed;
				}
				return EStep::Running;
			}
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			Bow->ClearAimOverride();
			Inventory->SelectArrowSlot(SlotStandard);
			const bool bCaught = AReturningArrowProjectile::CaughtCount > Run.CaughtAtStart;
			Run.BoomerangNote = FString::Printf(TEXT("at the alarm box: tripped %s, %s, back in the quiver (%d)"), Switch->IsTripped() ? TEXT("yes") : TEXT("no"),
				bCaught ? TEXT("caught") : TEXT("it hit her"), Inventory->GetArrowCount(SlotBoomerang));
			Run.Test->TestTrue(TEXT("The boomerang tripped the alarm box"), Switch->IsTripped());
			Run.Test->TestTrue(TEXT("...came back and she caught it (she faced it)"), bCaught);
			Run.Test->TestEqual(TEXT("...and it is back in the quiver"), Inventory->GetArrowCount(SlotBoomerang), 2);
			Run.Marks.Add(TEXT("alarm"), Now - Run.LapStart);
			Run.Note(Run.BoomerangNote);
			return Switch->IsTripped() ? EStep::Done : EStep::Failed;
		});
		// The guard comes to look at the buzz: taken down at the box.
		AddStalk(R, TEXT("take the guard down at the alarm box"), TEXT("guard"), [](const AThugCharacter* Thug)
		{
			return FVector::Dist2D(Thug->GetActorLocation(), AlarmBox) < 700.f;
		}, 45.f);
	}

	/** To the workshop door unseen: enter_grotto. */
	static void AddWorkshopDoor(FRunner& R)
	{
		AddWalk(R, TEXT("to the corridor's archway"), CorridorArch, true, 50.f);
		AddWalk(R, TEXT("into the corridor"), CorridorIn, true, 50.f);
		Add(R, TEXT("to the workshop door (enter_grotto)"), 40.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(Run.bLoud ? false : true);
			if (IsDone(World, TEXT("enter_grotto")))
			{
				const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
				const UMissionObjective* Enter = Missions->GetTracker()->FindObjective(TEXT("enter_grotto"));
				Run.Test->TestEqual(TEXT("enter_grotto done spotted only in the loud lap"), Enter && Enter->bSpotted, Run.bLoud);
				Run.Marks.Add(TEXT("workshop_door"), Now - Run.LapStart);
				Run.Note(FString::Printf(TEXT("enter_grotto done%s; open_vault current: %s"), Enter && Enter->bSpotted ? TEXT(" (spotted)") : TEXT(" unseen"),
					*CurrentId(World).ToString()));
				return EStep::Done;
			}
			Run.SteerTo(World, PC, Kate, WorkshopFront, 40.f, Now);
			return EStep::Running;
		});
	}

	/** The keypad: open_vault; the door opens on it. */
	static void AddKeypad(FRunner& R)
	{
		Add(R, TEXT("the keypad (open_vault)"), 30.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(false);
			ADoorActor* Door = nullptr;
			for (ADoorActor* Each : All<ADoorActor>(World))
			{
				Door = Each->OpensOnObjective == FName(TEXT("open_vault")) ? Each : Door;
			}
			if (!Door)
			{
				Run.Note(TEXT("no workshop door"));
				return EStep::Failed;
			}
			if (IsDone(World, TEXT("open_vault")))
			{
				AHawkeyePlayerController* HPC = PC;
				if (HPC->IsCloseUpActive() || !Door->IsOpen())
				{
					return EStep::Running;
				}
				if (Run.Phase < 5)
				{
					Run.Phase = 5;
					Run.PhaseAt = Now;
					return EStep::Running;
				}
				if (Now - Run.PhaseAt > 0.5)
				{
					const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
					Run.Test->TestTrue(TEXT("The vault's line"), Events && Events->GetFired().Contains(TEXT("objective_completed:open_vault")));
					Run.Marks.Add(TEXT("vault"), Now - Run.LapStart);
					const UNavigationPath* Through = UNavigationSystemV1::FindPathToLocationSynchronously(World, WorkshopFront + FVector(0.f, 0.f, 50.f),
						InWorkshop + FVector(0.f, 0.f, 50.f));
					Run.Note(FString::Printf(TEXT("the workshop door opened on open_vault; a path through it: %s"),
						Through && Through->IsValid() && !Through->IsPartial() ? *FString::Printf(TEXT("%.0f cm"), Through->GetPathLength()) : TEXT("none")));
					return EStep::Done;
				}
				return EStep::Running;
			}
			if (Run.SteerTo(World, PC, Kate, Keypad + FVector(0.f, -110.f, 0.f), 45.f, Now))
			{
				AimCamera(PC, Kate, Keypad + FVector(0.f, 0.f, 120.f));
				if (Now - Run.PhaseAt > 0.7)
				{
					Run.PhaseAt = Now;
					FRunner::InteractHere(Kate);
				}
			}
			return EStep::Running;
		});
	}

	/** Save inside the interior and load it: everything comes back as it was. */
	static void AddRoundTrip(FRunner& R)
	{
		Add(R, TEXT("save and load inside the grotto"), 60.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			if (!Save || !Missions || !Events)
			{
				return EStep::Failed;
			}
			if (Run.Phase == 0)
			{
				if (Save->IsLoading() || !Kate->GetCharacterMovement()->IsMovingOnGround())
				{
					return EStep::Running;
				}
				Run.SavedKate = Kate->GetActorLocation();
				Run.SavedObjectives = Missions->GetTracker()->GetCompletedObjectiveIds();
				Run.SavedFired = Events->GetFired();
				Run.SavedBoomerangs = Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang);
				Run.SavedElvesDown = 0;
				for (const AThugCharacter* Elf : Elves(World))
				{
					Run.SavedElvesDown += IsDown(Elf) ? 1 : 0;
				}
				if (!Save->SaveCampaign(TEXT("grotto lap inside the interior")))
				{
					Run.Note(TEXT("the save inside the interior was refused"));
					return EStep::Failed;
				}
				if (!Save->LoadCampaign())
				{
					Run.Note(TEXT("the load did not start"));
					return EStep::Failed;
				}
				Run.ReloadStartedAt = Now;
				Run.Phase = 1;
				return EStep::Running;
			}
			if (World == Run.LastWorld.Get() && Run.Phase == 1 && Now - Run.ReloadStartedAt < 0.5)
			{
				return EStep::Running;
			}
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			if (Save->IsLoading() || !IsMap(World, GrottoPath) || !Site || !Site->GetDefinition() || Missions->GetCurrentMission() == nullptr)
			{
				return EStep::Running;
			}
			if (Run.Phase == 1)
			{
				Run.Phase = 2;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Now - Run.PhaseAt < 1.0)
			{
				return EStep::Running;
			}
			HawkeyeFreezePartner(World);
			int32 Down = 0;
			for (const AThugCharacter* Elf : Elves(World))
			{
				Down += IsDown(Elf) ? 1 : 0;
			}
			ADoorActor* Door = nullptr;
			for (ADoorActor* Each : All<ADoorActor>(World))
			{
				Door = Each->OpensOnObjective == FName(TEXT("open_vault")) ? Each : Door;
			}
			const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
			const float Off = FVector::Dist(Kate->GetActorLocation(), Run.SavedKate);
			Run.Test->TestTrue(TEXT("Round trip: back inside the grotto, a chapter interior"), Save->IsInChapterInterior());
			Run.Test->TestEqual(TEXT("Round trip: the same objectives"), Missions->GetTracker()->GetCompletedObjectiveIds(), Run.SavedObjectives);
			Run.Test->TestEqual(TEXT("Round trip: the same events fired"), Events->GetFired(), Run.SavedFired);
			Run.Test->TestEqual(TEXT("Round trip: no line said again"), Events->GetSpokenLines().Num(), 0);
			Run.Test->TestEqual(TEXT("Round trip: the boomerangs"), Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang), Run.SavedBoomerangs);
			Run.Test->TestEqual(TEXT("Round trip: the elves taken down stay down"), Down, Run.SavedElvesDown);
			Run.Test->TestTrue(TEXT("Round trip: the workshop door as it was (shut until the keypad)"), Door && Door->IsOpen() == IsDone(World, TEXT("open_vault")));
			// Before the meet (open_vault) a load puts Santa back waiting in the workshop ("Bosses", save).
			Run.Test->TestTrue(TEXT("Round trip: Santa waits in the workshop"), Fight && Fight->GetBoss() && Fight->GetState() == EBossFightState::Approach);
			Run.Test->TestTrue(FString::Printf(TEXT("Round trip: Kate where she was (%.0f cm off)"), Off), Off < 60.f);
			Run.RoundTrip = FString::Printf(TEXT("loaded in %.1f s inside L_Int_Grotto: %d objectives, %d events, %d boomerangs, %d elves down, door %s, Santa %s, Kate %.0f cm off"),
				Run.PhaseAt - Run.ReloadStartedAt, Run.SavedObjectives.Num(), Events->GetFired().Num(), Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang),
				Down, Door && Door->IsOpen() ? TEXT("open") : TEXT("shut"), Fight && Fight->GetBoss() ? TEXT("waiting") : TEXT("missing"), Off);
			Run.LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
			Run.Marks.Add(TEXT("round_trip"), Now - Run.LapStart);
			Run.Note(Run.RoundTrip);
			return EStep::Done;
		});
	}

	/** Into the workshop: Santa's line, then the fight to the end. */
	static void AddSanta(FRunner& R)
	{
		Add(R, TEXT("Santa"), 150.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(false);
			UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
			if (!Fight)
			{
				return EStep::Failed;
			}
			AThugCharacter* Santa = Fight->GetBoss();
			const EBossFightState State = Fight->GetState();
			const bool bOutside = !Workshop.IsInside(FVector2D(Feet(Kate))) || Feet(Kate).Y < Workshop.Min.Y + 80.f;
			const bool bSantaClose = Santa && FVector::Dist2D(Santa->GetActorLocation(), Kate->GetActorLocation()) < 260.f;
			if (State == EBossFightState::Approach || (State == EBossFightState::Fight && bOutside && !bSantaClose))
			{
				// Square on to the doorway, then straight through it (the path through a door just opened can lag the
				// navmesh's rebuild), then on into the room.
				const FVector Here = Feet(Kate);
				Run.Crouch(false);
				if (Here.Y < Workshop.Min.Y + 80.f)
				{
					// Along the corridor to the doorway's middle, then straight through it.
					const FVector2D To = FMath::Abs(Here.X - WorkshopFront.X) > 20.f && Here.Y < Workshop.Min.Y - 40.f
						? FVector2D(WorkshopFront.X - Here.X, WorkshopFront.Y - Here.Y).GetSafeNormal() : FVector2D(0.f, 1.f);
					Run.Forward(PC, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
				}
				else
				{
					Run.SteerTo(World, PC, Kate, InWorkshop, 50.f, Now);
				}
				if (Now - Run.LastLog > 0.5)
				{
					Run.LastLog = Now;
					ADoorActor* Door = nullptr;
					for (ADoorActor* Each : All<ADoorActor>(World))
					{
						Door = Each->OpensOnObjective == FName(TEXT("open_vault")) ? Each : Door;
					}
					UE_LOG(LogTemp, Display, TEXT("[Hawkeye] grotto approach: Kate %s crouch %d; door %s open %d leaf %s"), *Here.ToCompactString(),
						Kate->bIsCrouched ? 1 : 0, Door ? *Door->GetActorLocation().ToCompactString() : TEXT("-"), Door && Door->IsOpen() ? 1 : 0,
						Door ? *Door->GetLeafWorldCentre().ToCompactString() : TEXT("-"));
				}
				Run.Note(FString::Printf(TEXT("into the workshop, at %s"), *Here.ToCompactString()));
				return EStep::Running;
			}
			if (State == EBossFightState::Intro)
			{
				// In the room as Santa says his line: the sleighs first (the vault), then him.
				Run.StopMoving(PC);
				const bool bStill = Kate->GetVelocity().Size2D() < 20.f;
				if (!Run.Shots.Contains(TEXT("grotto_vault.png")))
				{
					AimCamera(PC, Kate, FVector(1630.f, 1640.f, 60.f));
					if (bStill && Now - Run.PhaseAt > 0.3)
					{
						Run.Shot(TEXT("grotto_vault.png"));
					}
					else if (Run.PhaseAt < Run.StepStart || !bStill)
					{
						Run.PhaseAt = Now;
					}
					return EStep::Running;
				}
				return EStep::Running;
			}
			if (State == EBossFightState::Fight)
			{
				if (Run.FightStart <= 0.0)
				{
					Run.FightStart = Now;
				}
				if (Fight->IsInBeat())
				{
					Run.StopMoving(PC);
					return EStep::Running;
				}
				// Santa's picture: the first time he stops to get his breath back with her in the room and near him.
				if (Santa && !Run.Shots.Contains(TEXT("grotto_santa.png")) && Fight->IsWinded() && !bOutside
					&& FVector::Dist2D(Santa->GetActorLocation(), Kate->GetActorLocation()) < 400.f)
				{
					Run.StopMoving(PC);
					AimCamera(PC, Kate, Santa->GetActorLocation() + FVector(0.f, 0.f, 40.f));
					Run.Shot(TEXT("grotto_santa.png"));
					return EStep::Running;
				}
				Run.Fight(World, PC, Kate, { Santa }, World->GetTimeSeconds());
				Run.Note(FString::Printf(TEXT("Santa %.0f hp%s"), Santa ? Santa->GetHealthComponent()->GetCurrentHealth() : -1.f,
					Fight->IsWinded() ? TEXT(", winded") : TEXT("")));
				return EStep::Running;
			}
			if (State == EBossFightState::Defeated || State == EBossFightState::Done)
			{
				if (Run.FightSeconds <= 0.0 && Run.FightStart > 0.0)
				{
					Run.FightSeconds = Now - Run.FightStart;
				}
				Run.StopMoving(PC);
				Hold(PC, MeleePath, false);
				Hold(PC, FirePath, false);
				Run.bMeleeHeld = Run.bDrawing = false;
				if (Santa && !Run.Shots.Contains(TEXT("grotto_santa_down.png")) && Fight->IsInBeat())
				{
					Run.Shot(TEXT("grotto_santa_down.png"));
				}
				if (IsDone(World, TEXT("beat_santa")) && HawkeyeShots::GetOutstanding(Run.Test) == 0)
				{
					const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
					Run.Test->TestEqual(TEXT("Santa down: leave_roof is current"), CurrentId(World), FName(TEXT("leave_roof")));
					Run.Marks.Add(TEXT("santa_down"), Now - Run.LapStart);
					Run.Note(FString::Printf(TEXT("Santa down after %.1f s of fighting"), Run.FightSeconds));
					return EStep::Done;
				}
			}
			return EStep::Running;
		});
	}

	/** Out by the roof stairs; on the roof, leave_roof; the end card and back to roaming. */
	static void AddRoof(FRunner& R)
	{
		Add(R, TEXT("out by the roof stairs, onto the roof"), 90.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (IsMap(World, GrottoPath))
			{
				for (AInteriorExit* Exit : All<AInteriorExit>(World))
				{
					if (Exit->ReturnPointOverride != RoofTag)
					{
						continue;
					}
					if (Exit->IsTravelling())
					{
						Run.StopMoving(PC);
						return EStep::Running;
					}
					if (Exit->GetEntryZone()->IsOverlappingActor(Kate))
					{
						Run.StopMoving(PC);
						FRunner::InteractHere(Kate);
						return EStep::Running;
					}
					if (Run.SteerTo(World, PC, Kate, RoofStairs, 40.f, Now))
					{
						Run.Forward(PC, 90.f);
					}
					return EStep::Running;
				}
				Run.Note(TEXT("no roof stairs"));
				return EStep::Failed;
			}
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const AActor* Roof = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				Roof = It->Tags.Contains(RoofTag) ? *It : Roof;
			}
			if (!IsMap(World, DistrictPath) || !Save || Save->IsLoading() || Save->GetSceneReturn().bReturnPending || !Roof)
			{
				return EStep::Running;
			}
			if (Run.Phase < 10)
			{
				Run.Phase = 10;
				Run.PhaseAt = Now;
				// The district opened afresh: its own thugs are up again and nothing here is theirs to fight.
				FreezeThugs(World);
				Run.Test->TestTrue(TEXT("On the roof: the chapter carried out (Santa beaten)"), IsDone(World, TEXT("beat_santa")));
				Run.Test->TestFalse(TEXT("On the roof: no longer in an interior"), Save->IsInInterior());
				const float Off = FVector::Dist2D(Kate->GetActorLocation(), Roof->GetActorLocation());
				Run.Test->TestTrue(FString::Printf(TEXT("On the roof point (%.0f cm off)"), Off), Off < 150.f);
				return EStep::Running;
			}
			HawkeyeFreezePartner(World);
			if (Now - Run.PhaseAt > 1.2 && !Run.Shots.Contains(TEXT("grotto_roof.png")))
			{
				PC->SetControlRotation(FRotator(-12.f, Kate->GetActorRotation().Yaw, 0.f));
				Run.Shot(TEXT("grotto_roof.png"));
				return EStep::Running;
			}
			if (!Run.Shots.Contains(TEXT("grotto_roof.png")) || HawkeyeShots::GetOutstanding(Run.Test) > 0)
			{
				return EStep::Running;
			}
			// A few steps onto the roof's middle: leave_roof.
			if (!IsDone(World, TEXT("leave_roof")))
			{
				const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
				const AObjectiveTriggerVolume* Volume = nullptr;
				for (AObjectiveTriggerVolume* Each : Site ? Site->GetVolumes() : TArray<AObjectiveTriggerVolume*>())
				{
					Volume = Each->ObjectiveId == FName(TEXT("leave_roof")) ? Each : Volume;
				}
				if (!Volume)
				{
					Run.Note(TEXT("no leave_roof volume"));
					return EStep::Failed;
				}
				const FVector2D To = FVector2D(Volume->GetActorLocation() - Kate->GetActorLocation()).GetSafeNormal();
				Run.Forward(PC, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
				return EStep::Running;
			}
			Run.Test->TestTrue(TEXT("The roof completes the chapter"), UMissionSubsystem::Get(World)->IsMissionComplete());
			Run.Marks.Add(TEXT("roof"), Now - Run.LapStart);
			Run.Note(TEXT("leave_roof done on the grotto's roof: the chapter is complete"));
			return EStep::Done;
		});
		Add(R, TEXT("the end card and back to roaming"), 60.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			const UMissionFlowController* Flow = PC->GetMissionFlow();
			if (Flow && Flow->GetStep() == EMissionFlowStep::EndCard && !Run.Shots.Contains(TEXT("grotto_endcard.png")))
			{
				Run.Shot(TEXT("grotto_endcard.png"));
			}
			if ((Flow && Flow->IsRunning()) || HawkeyeShots::GetOutstanding(Run.Test) > 0 || Now - Run.StepStart < 2.0)
			{
				return EStep::Running;
			}
			Run.Marks.Add(TEXT("roaming"), Now - Run.LapStart);
			Run.Note(TEXT("back to roaming"));
			return EStep::Done;
		});
	}

	/** The video call from the farm: the phone call's voices as subtitles. */
	static void AddCall(FRunner& R)
	{
		Add(R, TEXT("the video call from the farm"), 60.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
			const UPhoneInbox* Inbox = Phone ? Phone->GetInbox() : nullptr;
			const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Kate);
			if (!Inbox || !Inbox->HasArrived(TEXT("msg_ch03_call")))
			{
				Run.Note(TEXT("waiting for the call"));
				return EStep::Running;
			}
			const FString Shown = Hud && Hud->IsSubtitleVisible() ? Hud->GetSubtitleShown().ToString() : FString();
			if (!Run.Shots.Contains(TEXT("grotto_call.png")) && Shown.StartsWith(TEXT("Lila")))
			{
				Run.Shot(TEXT("grotto_call.png"));
				Run.Note(FString::Printf(TEXT("on the call: \"%s\""), *Shown));
			}
			const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(World);
			const bool bPlayed = Dialogue && Dialogue->GetQueue() && Dialogue->GetQueue()->HasPlayedSequence(TEXT("seq_ch03_call"));
			if (bPlayed && Run.Shots.Contains(TEXT("grotto_call.png")) && !Dialogue->IsBusy() && HawkeyeShots::GetOutstanding(Run.Test) == 0)
			{
				Run.Marks.Add(TEXT("call"), Now - Run.LapStart);
				return EStep::Done;
			}
			return EStep::Running;
		});
	}

	/** The ornament at her door: put near it (a [lap] move: it is across the district), examine, the flag. */
	static void AddOrnament(FRunner& R)
	{
		Add(R, TEXT("the ornament at her door"), 45.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			AChapterEndInteractable* Ornament = Site && Site->GetExamines().Num() == 1 ? Site->GetExamines()[0] : nullptr;
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			if (!Ornament || !Save)
			{
				Run.Note(TEXT("no ornament"));
				return EStep::Failed;
			}
			const FVector Front = Ornament->GetActorLocation() + Ornament->GetActorForwardVector() * 130.f;
			if (Run.Phase == 0)
			{
				Place(World, Front, (-Ornament->GetActorForwardVector()).Rotation().Yaw);
				Run.Phase = 1;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (IsDone(World, TEXT("examine_ornament")))
			{
				Run.Note(TEXT("examined"));
				if (PC->IsCloseUpActive() || HawkeyeShots::GetOutstanding(Run.Test) > 0)
				{
					return EStep::Running;
				}
				const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
				Run.Test->TestTrue(TEXT("The ornament raises found_ornament"), Save->GetCampaignState()->HasFlag(Flag));
				Run.Test->TestTrue(TEXT("...and its lines"), Events && Events->GetFired().Contains(TEXT("objective_completed:examine_ornament")));
				Run.Marks.Add(TEXT("ornament"), Now - Run.LapStart);
				return EStep::Done;
			}
			if (PC->IsCloseUpActive())
			{
				return EStep::Running;
			}
			Run.Crouch(false);
			AimCamera(PC, Kate, Ornament->GetActorLocation() + FVector(0.f, 0.f, 100.f));
			if (!Run.Shots.Contains(TEXT("grotto_ornament.png")))
			{
				if (Now - Run.PhaseAt > 1.2)
				{
					Run.Shot(TEXT("grotto_ornament.png"));
				}
				return EStep::Running;
			}
			if (HawkeyeShots::GetOutstanding(Run.Test) > 0)
			{
				return EStep::Running;
			}
			UInteractionComponent* Interaction = Kate->GetInteractionComponent();
			if (Interaction)
			{
				Interaction->RefreshFocus();
			}
			const AActor* Focus = Interaction ? Interaction->GetFocusedActor() : nullptr;
			Run.Note(FString::Printf(TEXT("focus %s"), *GetNameSafe(Focus)));
			// Only the ornament: her door's own zone is a step away.
			if (Focus == Ornament && Now - Run.PhaseAt > 0.8)
			{
				Run.PhaseAt = Now;
				Interaction->TryInteract();
			}
			return EStep::Running;
		});
	}

	/** Chapter 4 the way chapter select starts it after chapter 3: its quiver holds the foam and the boomerangs. */
	static void AddChapterFour(FRunner& R)
	{
		Add(R, TEXT("chapter 4 after chapter 3"), 20.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			AHawkeyeGameMode* Mode = World->GetAuthGameMode<AHawkeyeGameMode>();
			UMissionDefinition* Four = AHawkeyeGameMode::FindChapter(NextChapter);
			const bool bStarted = Mode && Four && Mode->StartChapter(Four);
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
			const int32 Foam = Kate->GetInventoryComponent()->GetArrowCount(SlotFoam);
			const int32 Boomerangs = Kate->GetInventoryComponent()->GetArrowCount(SlotBoomerang);
			Run.Test->TestTrue(TEXT("Chapter 4 starts after chapter 3"), bStarted);
			Run.Test->TestNull(TEXT("Chapter 4: the grotto's street site is gone"), Site ? Site->GetDefinition() : nullptr);
			Run.Test->TestTrue(TEXT("Chapter 4: Tomas waits on his roof"), Fight && Fight->GetBoss() && Fight->GetState() == EBossFightState::Approach);
			Run.Test->TestEqual(TEXT("Chapter 4's quiver: chapter 2's foam"), Foam, 2);
			Run.Test->TestEqual(TEXT("...and chapter 3's boomerangs"), Boomerangs, 2);
			Run.ChapterFourCheck = FString::Printf(TEXT("chapter 4 %s, Tomas %s, foam %d, boomerang %d"), bStarted ? TEXT("started") : TEXT("refused"),
				Fight && Fight->GetBoss() ? TEXT("waiting") : TEXT("missing"), Foam, Boomerangs);
			Run.Note(Run.ChapterFourCheck);
			return bStarted ? EStep::Done : EStep::Failed;
		});
	}

	// --- the loud lap's own steps ----------------------------------------------------------------------------

	/** Straight across the hall standing up: the hall's elf sees her; the objective's title and her line say so. */
	static void AddSeen(FRunner& R)
	{
		Add(R, TEXT("walk into the hall and be seen"), 30.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(false);
			const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
			const UMissionObjective* Enter = Missions && Missions->GetTracker() ? Missions->GetTracker()->FindObjective(TEXT("enter_grotto")) : nullptr;
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			if (Enter && Enter->bSpotted)
			{
				Run.Test->TestTrue(TEXT("Seen: the objective's title is the spotted one"), Enter->Title.ToString().StartsWith(TEXT("Get to the workshop door")));
				Run.Test->TestTrue(TEXT("Seen: her line"), Events && Events->GetFired().Contains(TEXT("spotted:enter_grotto")));
				Run.Test->TestFalse(TEXT("Seen: the chapter goes on (not failed)"), Missions->IsMissionComplete());
				Run.Marks.Add(TEXT("seen"), Now - Run.LapStart);
				Run.Note(FString::Printf(TEXT("seen; the HUD says \"%s\""), *Enter->Title.ToString()));
				return EStep::Done;
			}
			Run.SteerTo(World, PC, Kate, FVector(700.f, 620.f, 0.f), 60.f, Now);
			return EStep::Running;
		});
	}

	/**
	 * At the stockroom shelf, the boomerang loosed west out through the archway, and her back turned on it: it comes back
	 * into her (the self-hit, its line, a stagger, never her last health). Retried if a hit knocks her out of the draw.
	 */
	static void AddSelfHit(FRunner& R)
	{
		Add(R, TEXT("the boomerang loosed, her back turned: it comes back into her"), 40.f, [](FRunner& Run, UWorld* World,
			AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			UBowComponent* Bow = Kate->GetBowComponent();
			UInventoryComponent* Inventory = Kate->GetInventoryComponent();
			if (!Bow || Inventory->GetArrowCount(SlotBoomerang) == 0 && Run.Phase == 0)
			{
				Run.Note(TEXT("no bow or no boomerang"));
				return EStep::Failed;
			}
			const FVector West(Feet(Kate).X - 900.f, 250.f, 150.f);
			if (Run.Phase == 0)
			{
				Run.Crouch(false);
				if (Kate->IsStaggered() || Kate->IsMeleeAttacking() || Kate->IsDodging() || Now < Run.BoomerangRetryAt || !Run.IsCrouchSettled(Kate))
				{
					return EStep::Running;
				}
				Run.StopMoving(PC);
				AimCamera(PC, Kate, West);
				Inventory->SelectArrowSlot(SlotBoomerang);
				Run.CaughtAtStart = AReturningArrowProjectile::CaughtCount;
				Run.SelfHitsAtStart = AReturningArrowProjectile::SelfHitCount;
				Run.HealthBeforeSelfHit = Kate->GetHealthComponent()->GetCurrentHealth();
				Bow->SetAimOverride(West);
				Hold(PC, FirePath, true);
				Run.bDrawing = true;
				Run.Phase = 1;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Run.Phase == 1)
			{
				Bow->SetAimOverride(West);
				AimCamera(PC, Kate, West);
				if (Kate->IsStaggered() || (Now - Run.PhaseAt > 0.25 && !Kate->IsDrawingBow()))
				{
					Bow->CancelDraw();
					Hold(PC, FirePath, false);
					Run.bDrawing = false;
					Bow->ClearAimOverride();
					Run.Phase = 0;
					Run.BoomerangRetryAt = Now + 1.0;
					return EStep::Running;
				}
				if (Now - Run.PhaseAt < 0.85)
				{
					return EStep::Running;
				}
				Hold(PC, FirePath, false);
				Run.bDrawing = false;
				++Run.ArrowsLoosed;
				Run.Phase = 2;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Run.Phase == 2)
			{
				const bool bOut = AReturningArrowProjectile::CaughtCount > Run.CaughtAtStart || AReturningArrowProjectile::SelfHitCount > Run.SelfHitsAtStart
					|| All<AReturningArrowProjectile>(World).Num() > 0;
				if (!bOut)
				{
					if (Now - Run.PhaseAt > 0.5)
					{
						// Nothing left the bow: try again.
						Bow->ClearAimOverride();
						Run.Phase = 0;
						Run.BoomerangRetryAt = Now + 0.5;
					}
					else
					{
						// Still aimed until it is away.
						Bow->SetAimOverride(West);
						AimCamera(PC, Kate, West);
					}
					return EStep::Running;
				}
				// Her back to it, the camera with her.
				Bow->ClearAimOverride();
				const float Away = (Feet(Kate) - West).GetSafeNormal2D().Rotation().Yaw;
				PC->SetControlRotation(FRotator(-8.f, Away, 0.f));
				Kate->SetActorRotation(FRotator(0.f, Away, 0.f));
				if (bOut)
				{
					Run.Phase = 3;
					Run.PhaseAt = Now;
				}
				return EStep::Running;
			}
			const float Away = (Feet(Kate) - West).GetSafeNormal2D().Rotation().Yaw;
			PC->SetControlRotation(FRotator(-8.f, Away, 0.f));
			AReturningArrowProjectile* Flight = nullptr;
			for (AReturningArrowProjectile* Each : All<AReturningArrowProjectile>(World))
			{
				Flight = Each->GetReturnPhase() == EHawkeyeReturnPhase::Back ? Each : Flight;
			}
			if (Flight && !Run.Shots.Contains(TEXT("grotto_boomerang_self.png")))
			{
				Run.Shot(TEXT("grotto_boomerang_self.png"));
			}
			const bool bHome = AReturningArrowProjectile::CaughtCount > Run.CaughtAtStart || AReturningArrowProjectile::SelfHitCount > Run.SelfHitsAtStart;
			if (!bHome || HawkeyeShots::GetOutstanding(Run.Test) > 0)
			{
				Run.Note(FString::Printf(TEXT("in flight (%s)"), Flight ? TEXT("coming back") : TEXT("out")));
				return Now - Run.PhaseAt > 8.0 ? EStep::Failed : EStep::Running;
			}
			const bool bSelfHit = AReturningArrowProjectile::SelfHitCount > Run.SelfHitsAtStart;
			const UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(World);
			const float After = Kate->GetHealthComponent()->GetCurrentHealth();
			Run.Test->TestTrue(TEXT("Her back to it: it came back into her"), bSelfHit);
			Run.Test->TestTrue(TEXT("...\"Ow. It came back.\""), Events && Events->GetFired().Contains(TEXT("arrow_self_hit:DA_Arrow_Boomerang")));
			Run.Test->TestTrue(TEXT("...alive, never her last health"), Kate->GetHealthComponent()->IsAlive() && After >= 1.f);
			Run.Test->TestEqual(TEXT("...and the boomerang back in the quiver"), Inventory->GetArrowCount(SlotBoomerang), 2);
			Run.BoomerangNote = FString::Printf(TEXT("loosed out of the stockroom, her back turned: %s, health %.0f to %.0f"),
				bSelfHit ? TEXT("self-hit") : TEXT("caught"), Run.HealthBeforeSelfHit, After);
			Inventory->SelectArrowSlot(SlotStandard);
			Run.Marks.Add(TEXT("self_hit"), Now - Run.LapStart);
			Run.Note(Run.BoomerangNote);
			return bSelfHit ? EStep::Done : EStep::Failed;
		});
	}

	/** Fight every elf. */
	static void AddLoudFight(FRunner& R)
	{
		Add(R, TEXT("fight the elves"), 180.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(false);
			const TArray<AThugCharacter*> Group = Elves(World);
			const EStep Result = Run.Fight(World, PC, Kate, Group, World->GetTimeSeconds());
			FString States;
			for (const AThugCharacter* Thug : Group)
			{
				States += FString::Printf(TEXT("%s %s; "), *Thug->GetName(), IsDown(Thug) ? TEXT("down") : Thug->IsAlerted() ? TEXT("alerted") : TEXT("up"));
			}
			Run.Note(FString::Printf(TEXT("Kate %.0f hp; %s"), Kate->GetHealthComponent()->GetCurrentHealth(), *States));
			if (Result == EStep::Done)
			{
				Run.Marks.Add(TEXT("elves_down"), Now - Run.LapStart);
			}
			return Result;
		});
	}

	static TSharedRef<FRunner> MakeRunner(FAutomationTestBase* Test, bool bLoud)
	{
		const TSharedRef<FRunner> Runner = MakeShared<FRunner>(Test, bLoud);
		AddStart(*Runner);
		AddFrontage(*Runner, !bLoud);
		AddEnter(*Runner);
		if (!bLoud)
		{
			AddInsideShot(*Runner);
			// The hall's elf walking west: she follows him from behind.
			AddStalk(*Runner, TEXT("take the hall's elf down"), TEXT("hall"), [](const AThugCharacter* Thug)
			{
				return Thug->GetVelocity().X < -50.f && Thug->GetActorLocation().X < 900.f;
			});
			AddBoomerangPickup(*Runner);
			AddLure(*Runner);
		}
		else
		{
			// Crouched to the stockroom shelf (the boomerang), the self-hit there, then out into the hall standing up.
			AddWalk(*Runner, TEXT("creep to the hall's east side"), HallEast, true, 60.f);
			AddWalk(*Runner, TEXT("creep to the stockroom shelf"), Shelf, true, 60.f, 60.f);
			AddSelfHit(*Runner);
			AddSeen(*Runner);
			AddLoudFight(*Runner);
		}
		AddWorkshopDoor(*Runner);
		if (!bLoud)
		{
			AddRoundTrip(*Runner);
		}
		AddKeypad(*Runner);
		AddSanta(*Runner);
		AddRoof(*Runner);
		if (!bLoud)
		{
			AddCall(*Runner);
			AddOrnament(*Runner);
			AddChapterFour(*Runner);
		}
		return Runner;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapGrotto, "Hawkeye.Lap.Grotto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapGrotto::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	using namespace HawkeyeGrottoLap;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the grotto lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(DistrictPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeRunner(this, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapGrottoLoud, "Hawkeye.Lap.SeenGrotto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapGrottoLoud::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	using namespace HawkeyeGrottoLap;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the loud grotto lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(DistrictPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeRunner(this, true));
	return true;
}

#endif
