#include "Camera/CameraComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/TakedownComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/InteriorTestKit.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/LapFightPressure.h"
#include "World/DoorActor.h"
#include "World/GrappleAnchor.h"
#include "World/InteractionComponent.h"
#include "World/InteriorEntrance.h"
#include "World/InteriorExit.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugHearing.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The sample auction house played by a script in the standalone game, twice (claude-docs/gameplay-semantics.md,
 * "Interiors"):
 *
 *   Hawkeye.Lap.InteriorStealth  in by the front door from the district; the lobby thug taken down from behind
 *                                in the office he patrols into, unseen; up the stair; the gunner at the gallery
 *                                rail taken down when nobody is near enough to hear the body; back down and
 *                                the bat thug taken down on the hall floor; the archer taken down on his beat by
 *                                the vault door; his keycard; the vault opened; a grapple to the gallery and
 *                                out by the roof door. Every move is input: crouch, the stick, F, E, the grapple.
 *                                Must finish with no thug ever Alerted.
 *                                Crouch is pressed as a player presses it (a toggle by default: down for 0.3 s
 *                                when she is not as the script wants), and both laps fail if she ever stands up
 *                                where the script did not press to stand.
 *   Hawkeye.Lap.InteriorLoud     the same way in, walking in the open: the lobby thug is fought, then the hall's
 *                                three (bat, gunner on the gallery, archer) with fists, heavies, dodges and
 *                                full-draw arrows, then the keycard, the vault and the roof. Must be won.
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Lap.InteriorStealth+Hawkeye.Lap.InteriorLoud; Quit"
 *
 * Each writes Saved/Automation/interior_stealth.json or interior_loud.json: seconds, alerts raised (thugs going
 * Alerted), suspicions, takedowns, hits taken, damage, the legs, and the longest camera arm seen while the fight
 * camera was up (never past IndoorArmLength). Its own save slot. The points are Sample.json's.
 */
namespace HawkeyeInteriorLap
{
	using namespace HawkeyeInteriorKit;

	static const TCHAR* DistrictPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* Slot = TEXT("HawkeyeCampaignInteriorLap");
	static const FName RoofTag(TEXT("City_InteriorRoof_Sample"));

	/** Kate's spots (Sample.json): the top of the stair, the archway into the hall, where she waits for the archer. */
	static const FVector TopLanding(735.f, 700.f, FloorHeight);
	static const FVector GalleryDoorStep(900.f, 685.f, FloorHeight);
	static const FVector StairFoot(650.f, 450.f, 0.f);
	static const FVector HallArch(870.f, 300.f, 0.f);
	static const FVector ArcherWatch(1800.f, 950.f, 0.f);
	static const FVector VaultStep(2120.f, 1150.f, 0.f);
	static const FVector InVault(2280.f, 1150.f, 0.f);
	static const FVector GrappleSpot(1750.f, 1000.f, 0.f);
	static const FVector RoofDoorApproach(1000.f, 230.f, FloorHeight);
	/** Up the stair by the middle of its flights (a navmesh path hugs the spine and stalls on a step). */
	static const FVector2D StairUp[] = { FVector2D(650.f, 450.f), FVector2D(650.f, 690.f), FVector2D(565.f, 725.f), FVector2D(565.f, 1140.f),
		FVector2D(735.f, 1140.f), FVector2D(735.f, 760.f), FVector2D(735.f, 700.f) };
	static const FVector2D StairDown[] = { FVector2D(735.f, 760.f), FVector2D(735.f, 1140.f), FVector2D(565.f, 1140.f), FVector2D(565.f, 725.f),
		FVector2D(650.f, 690.f), FVector2D(650.f, 450.f) };

	enum class EStep : uint8 { Running, Done, Failed };

	class FRunner;
	using FStepFn = TFunction<EStep(FRunner&, UWorld*, AHawkeyePlayerController*, AHawkeyeCharacter*, double)>;

	struct FStep
	{
		FString Name;
		float Timeout = 30.f;
		FStepFn Run;
	};

	class FRunner : public IAutomationLatentCommand
	{
	public:
		FRunner(FAutomationTestBase* InTest, bool bInLoud) : Test(InTest), bLoud(bInLoud) {}

		virtual bool Update() override;

		// --- what the steps share ------------------------------------------------------------------------

		FAutomationTestBase* Test;
		bool bLoud = false;
		TArray<FStep> Steps;
		int32 StepIndex = -1;
		double StepStart = 0.0;
		double LapStart = -1.0;
		FString StepDetail;
		TArray<FString> Legs;
		bool bFailed = false;
		bool bFinished = false;

		/** Movement: the path she is following, recomputed every 0.3 s; the crouch the script wants and the move hold. */
		TArray<FVector> Path;
		int32 PathIndex = 0;
		double PathAt = -10.0;
		double LastProgress = 0.0;
		float BestDistance = 1.e9f;
		double LastJump = 0.0;
		bool bCrouchWanted = false;
		bool bMoveHeld = false;

		/**
		 * The crouch key as a player works it (PumpCrouch). Toggled (the default) it goes down for 0.3 s, past the
		 * dodge tap, whenever she is not where bCrouchWanted says, and comes up; held, it stays down while wanted.
		 * Every crouch that ends is checked against the stand-up presses: any other is a stand-up by herself.
		 */
		bool bCrouchKeyDown = false;
		double CrouchKeyAt = -10.0;
		bool bStandPressed = false;
		int32 CrouchEndsSeen = 0;
		int32 ScriptedStands = 0;
		TArray<FString> StandUps;

		/** The step's own scratch. */
		int32 Phase = 0;
		double LastLog = 0.0;
		double PhaseAt = 0.0;
		TWeakObjectPtr<AThugCharacter> Focus;

		/** Stats. */
		TMap<TWeakObjectPtr<AThugCharacter>, EThugAlertState> Seen;
		int32 Alerts = 0;
		int32 Suspicions = 0;
		TArray<FString> AlertNotes;
		int32 Takedowns = 0;
		bool bWasTakingDown = false;
		int32 Hits = 0;
		float Damage = 0.f;
		float LastHealth = -1.f;
		float MaxFightArm = 0.f;
		float IndoorCap = 0.f;
		int32 LightSwings = 0;
		int32 HeavySwings = 0;
		int32 Dodges = 0;
		int32 ArrowsLoosed = 0;
		int32 Finishers = 0;

		/** Melee and bow state for the fights. */
		bool bMeleeHeld = false;
		double MeleeHeldSince = 0.0;
		bool bDrawing = false;
		double DrawStart = 0.0;
		TWeakObjectPtr<AThugCharacter> DrawTarget;
		double NextActionAt = 0.0;

		/** What the script wants; PumpCrouch works the key to get her there. */
		void Crouch(AHawkeyePlayerController* /*PC*/, bool bCrouch)
		{
			bCrouchWanted = bCrouch;
		}

		/** Crouched (or asked to be) as the script wants. */
		bool IsCrouchSettled(const AHawkeyeCharacter* Kate) const
		{
			return Kate->IsCrouchWanted() == bCrouchWanted;
		}

		void PumpCrouch(AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (!Kate->IsCrouchToggle())
			{
				if (bCrouchKeyDown != bCrouchWanted)
				{
					Hold(PC, CrouchPath, bCrouchWanted);
					bCrouchKeyDown = bCrouchWanted;
					bStandPressed = bStandPressed || !bCrouchWanted;
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
			bStandPressed = bStandPressed || !bCrouchWanted;
		}

		/** Every crouch that ended since last frame: the script's stand-up press, or she stood up by herself. */
		void CheckStandUps(AHawkeyeCharacter* Kate)
		{
			while (CrouchEndsSeen < Kate->GetCrouchEndCount())
			{
				++CrouchEndsSeen;
				const FString& Why = Kate->GetLastCrouchEndReason();
				if (bStandPressed && Why.StartsWith(TEXT("crouch input")))
				{
					++ScriptedStands;
					bStandPressed = false;
					continue;
				}
				StandUps.Add(FString::Printf(TEXT("in '%s' at %s: %s"), StepIndex >= 0 && StepIndex < Steps.Num() ? *Steps[StepIndex].Name : TEXT("?"),
					*Feet(Kate).ToCompactString(), *Why));
				UE_LOG(LogTemp, Warning, TEXT("[Hawkeye] Kate stood up without the script asking %s"), *StandUps.Last());
			}
		}

		void StopMoving(AHawkeyePlayerController* PC)
		{
			if (bMoveHeld)
			{
				Hold(PC, MovePath, false);
				bMoveHeld = false;
			}
		}

		void Forward(AHawkeyePlayerController* PC, float Yaw, float Pitch = -8.f)
		{
			PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
			Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
			bMoveHeld = true;
		}

		void ResetPath()
		{
			Path.Reset();
			PathIndex = 0;
			PathAt = -10.0;
			BestDistance = 1.e9f;
		}

		/**
		 * One frame of walking her to Goal along the navmesh: the path is found again every 0.3 s and she runs at
		 * its next point with the camera turned that way. True when she is within Reach (flat) on Goal's floor.
		 * Stuck a second and a half (a door's jamb, a chair), a jump tap; crouched, a sidestep instead.
		 */
		bool SteerTo(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, float Reach, double Now)
		{
			const FVector Here = Feet(Kate);
			if (FVector::Dist2D(Here, Goal) <= Reach && FMath::Abs(Here.Z - Goal.Z) < 120.f)
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
			while (PathIndex < Path.Num() && FVector::Dist2D(Here, Path[PathIndex]) < 30.f && FMath::Abs(Here.Z - Path[PathIndex].Z) < 150.f)
			{
				++PathIndex;
			}
			// Past a point already (a fresh path starts at the nav link's near end behind her): on to the next.
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

		/** Interact with whatever she stands at. */
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

		/** Would the body falling at Victim reach anyone else's ears (the takedown's 0.6 noise, walls and doors)? */
		static bool IsQuiet(UWorld* World, const AThugCharacter* Victim, const AHawkeyeCharacter* Kate, FString& OutWho)
		{
			for (AThugCharacter* Other : All<AThugCharacter>(World))
			{
				const AThugAIController* Brain = BrainOf(Other);
				if (Other == Victim || IsDown(Other) || !Brain)
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

		/** Inside his sight cone by a margin (60 degrees, 16 m) with a clear line: stand still and hope. */
		static bool IsInHisEyes(UWorld* World, const AThugCharacter* Thug, const AHawkeyeCharacter* Kate)
		{
			const FVector To = Kate->GetActorLocation() - Thug->GetActorLocation();
			if (To.Size2D() > 1600.f || FVector::DotProduct(Thug->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) < 0.5f)
			{
				return false;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(InteriorLapEyes), false, Kate);
			Params.AddIgnoredActor(Thug);
			return !World->LineTraceTestByChannel(Thug->GetActorLocation() + FVector(0.f, 0.f, 60.f), Kate->GetActorLocation(), ECC_Visibility, Params);
		}

		void EnsureQuiver(AHawkeyeCharacter* Kate)
		{
			UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
			if (!Inventory || Inventory->HasBow())
			{
				return;
			}
			FHawkeyeQuiverSlot Standard;
			Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard"));
			Standard.Count = 30;
			FHawkeyeQuiverSlot Grapple;
			Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple"));
			Grapple.Count = 6;
			Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate")),
				{ Standard, Grapple });
		}

		void Note(const FString& Text)
		{
			StepDetail = Text;
		}

		void TrackStats(UWorld* World, AHawkeyeCharacter* Kate);
		EStep Fight(UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, const TArray<AThugCharacter*>& Group, double Now);
		void EndStep(bool bOk, double Now);
		void Finish(UWorld* World);
	};

	void FRunner::TrackStats(UWorld* World, AHawkeyeCharacter* Kate)
	{
		for (AThugCharacter* Thug : All<AThugCharacter>(World))
		{
			const EThugAlertState State = Thug->GetAlertState();
			EThugAlertState& Was = Seen.FindOrAdd(Thug, EThugAlertState::Calm);
			if (State != Was && !IsDown(Thug))
			{
				if (State == EThugAlertState::Alerted)
				{
					++Alerts;
					AlertNotes.Add(FString::Printf(TEXT("%s alerted in '%s' at %.1f s"), *Thug->GetName(),
						StepIndex >= 0 && StepIndex < Steps.Num() ? *Steps[StepIndex].Name : TEXT("?"), FPlatformTime::Seconds() - LapStart));
				}
				else if (State == EThugAlertState::Suspicious && Was == EThugAlertState::Calm)
				{
					++Suspicions;
					AlertNotes.Add(FString::Printf(TEXT("%s suspicious in '%s' at %.1f s"), *Thug->GetName(),
						StepIndex >= 0 && StepIndex < Steps.Num() ? *Steps[StepIndex].Name : TEXT("?"), FPlatformTime::Seconds() - LapStart));
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
		IndoorCap = 250.f;   // AHawkeyeCharacter::IndoorArmLength (protected), gameplay-semantics.md
		if (Kate->GetFightCameraAlpha() > 0.01f && Kate->GetCameraBoom())
		{
			MaxFightArm = FMath::Max(MaxFightArm, Kate->GetCameraBoom()->TargetArmLength);
		}
	}

	/** Fists and heavies for the melee thugs, full-draw arrows for the gunner and the archer, dodges when a swing is about to land. */
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
			// A thug up on the gallery counts as far: she deals with the floor first.
			const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation())
				+ (FMath::Abs(Thug->GetActorLocation().Z - Kate->GetActorLocation().Z) > 200.f ? 800.f : 0.f);
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
		// Steering at a thug behind the hall's crates can catch a ledge (2026-11-11: hung 130 s while the gunner shot
		// her); a player lets go with crouch and fights on.
		if (const UParkourComponent* Parkour = Kate->GetParkourComponent(); Parkour && Parkour->IsHanging())
		{
			StopMoving(PC);
			if (Now >= NextActionAt)
			{
				Note(TEXT("let go of a ledge the steer caught"));
				Tap(PC, CrouchPath);
				NextActionAt = Now + 0.4;
			}
			return EStep::Running;
		}
		AThugCharacter* Target = bDrawing && DrawTarget.IsValid() && !IsDown(DrawTarget.Get()) ? DrawTarget.Get() : Nearest;
		const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Chest - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));

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
			AThugCharacter* Aimed = DrawTarget.Get();
			if (Aimed && Bow)
			{
				Bow->SetAimOverride(AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Aimed->GetActorLocation()
					+ FVector(0.f, 0.f, 30.f), Aimed->GetVelocity(), 6000.f * FMath::Max(Bow->GetDrawFraction(), 0.4f), World->GetGravityZ()));
			}
			bool bThreat = false;
			for (const AThugCharacter* T : Group)
			{
				bThreat |= !IsDown(T) && !T->IsGunner() && !T->IsArcher() && T->GetMeleeComponent() && T->GetMeleeComponent()->IsWindingUp()
					&& FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) < 280.f;
			}
			if (Now - DrawStart >= 0.9 || IsDown(Aimed) || bThreat)
			{
				if (bThreat && Now - DrawStart < 0.3 && Bow)
				{
					Bow->CancelDraw();
				}
				else
				{
					++ArrowsLoosed;
				}
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
		// A swing about to land within reach: out of it, sideways and back.
		if (!bBusy && Kate->GetDodgeCooldownRemaining() <= 0.f)
		{
			for (const AThugCharacter* T : Group)
			{
				const UMeleeComponent* Melee = T && !IsDown(T) ? T->GetMeleeComponent() : nullptr;
				if (!Melee || T->IsGunner() || T->IsArcher() || !Melee->IsWindingUp() || Melee->GetPhaseRemaining() > 0.3f
					|| FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) > Melee->GetCurrentAttack().Range + 110.f)
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
		const bool bRanged = Target->IsGunner() || Target->IsArcher();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(InteriorLapLine), false, Kate);
		Params.AddIgnoredActor(Target);
		const bool bLine = !World->LineTraceTestByChannel(Kate->GetActorLocation() + FVector(0.f, 0.f, 50.f), Chest, ECC_Visibility, Params);
		const float Flat = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
		const bool bOtherFloor = FMath::Abs(Target->GetActorLocation().Z - Kate->GetActorLocation().Z) > 200.f;
		if (bRanged && bLine && (Flat > 250.f || bOtherFloor) && Bow && Kate->GetInventoryComponent()->GetArrowCount(1) > 0)
		{
			StopMoving(PC);
			Kate->GetInventoryComponent()->SelectArrowSlot(1);
			DrawTarget = Target;
			DrawStart = Now;
			bDrawing = true;
			Hold(PC, FirePath, true);
			return EStep::Running;
		}
		if (bOtherFloor)
		{
			// Up on the gallery and hidden: stand where the rail shows him, out on the floor.
			SteerTo(World, PC, Kate, FVector(1700.f, Target->GetActorLocation().Y, 0.f), 80.f, Now);
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
		if ((ThugMelee && ThugMelee->IsWindingUp()) || Target->IsIncapacitated() || Near > 1 || bRanged)
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
		Legs.Add(FString::Printf(TEXT("    {\"leg\": \"%s\", \"ok\": %s, \"seconds\": %.2f, \"detail\": \"%s\"}"), *Name, bOk ? TEXT("true") : TEXT("false"),
			Now - StepStart, *StepDetail.ReplaceCharWithEscapedChar()));
		Test->AddInfo(FString::Printf(TEXT("Interior %s: %s in %.1f s: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Name, Now - StepStart, *StepDetail));
		if (!bOk)
		{
			Test->AddError(FString::Printf(TEXT("Interior %s lap: '%s' failed: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Name, *StepDetail));
			bFailed = true;
		}
	}

	void FRunner::Finish(UWorld* World)
	{
		bFinished = true;
		if (bCrouchKeyDown)
		{
			Hold(FindController(World), CrouchPath, false);
			bCrouchKeyDown = false;
		}
		const double Seconds = FPlatformTime::Seconds() - LapStart;
		const FString Json = FString::Printf(TEXT("{\n  \"test\": \"Hawkeye.Lap.%s\",\n  \"completed\": %s,\n  \"seconds\": %.1f,\n  \"alerts\": %d,\n"
			"  \"suspicions\": %d,\n  \"takedowns\": %d,\n  \"hits_taken\": %d,\n  \"damage_taken\": %.1f,\n  \"light_swings\": %d,\n"
			"  \"heavy_swings\": %d,\n  \"dodges\": %d,\n  \"arrows\": %d,\n  \"finishers\": %d,\n  \"max_fight_camera_arm\": %.0f,\n"
			"  \"indoor_arm_length\": %.0f,\n  \"stand_ups_asked\": %d,\n  \"stand_ups_by_herself\": %d,\n  \"notes\": \"%s\",\n  \"legs\": [\n%s\n  ]\n}\n"),
			bLoud ? TEXT("InteriorLoud") : TEXT("InteriorStealth"), bFailed ? TEXT("false") : TEXT("true"), Seconds, Alerts, Suspicions, Takedowns,
			Hits, Damage, LightSwings, HeavySwings, Dodges, ArrowsLoosed, Finishers, MaxFightArm, IndoorCap, ScriptedStands, StandUps.Num(),
			*FString::Join(AlertNotes, TEXT("; ")).ReplaceCharWithEscapedChar(), *FString::Join(Legs, TEXT(",\n")));
		WriteText(bLoud ? TEXT("interior_loud.json") : TEXT("interior_stealth.json"),
			HawkeyeLapPressure::Finish(Test, bLoud ? TEXT("InteriorLoud") : TEXT("InteriorStealth"), Json));
		Test->AddInfo(FString::Printf(TEXT("%s:\n%s"), bLoud ? TEXT("interior_loud.json") : TEXT("interior_stealth.json"), *Json));
		UE_LOG(LogTemp, Display, TEXT("[Hawkeye] interior %s lap: %s"), bLoud ? TEXT("loud") : TEXT("stealth"), *Json);
		if (!bLoud)
		{
			Test->TestEqual(TEXT("Stealth: no thug was ever Alerted"), Alerts, 0);
		}
		// A crouch ends only where the script pressed to stand (gameplay-semantics.md, "Movement", crouch).
		Test->TestEqual(FString::Printf(TEXT("Kate never stood up by herself (%s)"), *FString::Join(StandUps, TEXT("; "))), StandUps.Num(), 0);
		if (MaxFightArm > 0.f)
		{
			Test->TestTrue(FString::Printf(TEXT("The fight camera's arm never passed the indoor cap (%.0f of %.0f cm)"), MaxFightArm, IndoorCap),
				MaxFightArm <= IndoorCap + 0.5f);
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
		if (World && IsMap(World, MapPath))
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
			CheckStandUps(Kate);
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
			if (PC)
			{
				Crouch(PC, false);
			}
			Finish(World);
			return true;
		}
		++StepIndex;
		StepStart = Now;
		StepDetail.Reset();
		Phase = 0;
		PhaseAt = Now;
		Focus.Reset();
		ResetPath();
		return false;
	}

	// --- the steps ---------------------------------------------------------------------------------------

	/** The district door: walk up to it, use it, wait for the interior and let go of the fade. */
	static void AddEntry(FRunner& R)
	{
		R.Steps.Add({ TEXT("in by the front door from the district"), 45.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC,
			AHawkeyeCharacter* Kate, double Now)
		{
			if (Run.Phase == 0)
			{
				UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
				const TArray<AInteriorEntrance*> Doors = All<AInteriorEntrance>(World);
				if (!Save || Doors.Num() != 1)
				{
					Run.Note(TEXT("no save subsystem or not exactly one interior entrance in the district"));
					return EStep::Failed;
				}
				Save->SlotNameOverride = Slot;
				IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
				HawkeyeFreezePartner(World);
				const FVector Out = Doors[0]->GetActorForwardVector();
				Place(World, Doors[0]->GetActorLocation() + Out * 400.f + FVector(0.f, 0.f, 20.f), (-Out).Rotation().Yaw);
				Run.Phase = 1;
				Run.PhaseAt = Now;
				return EStep::Running;
			}
			if (Run.Phase == 1)
			{
				if (Now - Run.PhaseAt < 0.5)
				{
					return EStep::Running;
				}
				const TArray<AInteriorEntrance*> Doors = All<AInteriorEntrance>(World);
				if (Doors.Num() == 1)
				{
					if (Doors[0]->GetEntryZone()->IsOverlappingActor(Kate))
					{
						Run.StopMoving(PC);
						if (FRunner::InteractHere(Kate) || Doors[0]->IsTravelling())
						{
							Run.Phase = 2;
						}
						return EStep::Running;
					}
					Run.Forward(PC, (-Doors[0]->GetActorForwardVector()).Rotation().Yaw);
				}
				else if (IsMap(World, MapPath))
				{
					Run.Phase = 2;
				}
				return EStep::Running;
			}
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			if (!IsMap(World, MapPath) || !Save || !Save->IsInInterior())
			{
				return EStep::Running;
			}
			Run.EnsureQuiver(Kate);
			HawkeyeFreezePartner(World);
			Run.LapStart = Now;
			Run.LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
			Run.Note(FString::Printf(TEXT("in L_Int_Sample at %s, indoor arm %s, %.0f health"), *Kate->GetActorLocation().ToCompactString(),
				Kate->IsIndoorCamera() ? TEXT("on") : TEXT("off"), Run.LastHealth));
			return EStep::Done;
		} });
	}

	/** Walk (or creep) to Goal. */
	static void AddWalk(FRunner& R, const FString& Name, const FVector& Goal, bool bCrouch, float Reach = 45.f, float Timeout = 40.f)
	{
		R.Steps.Add({ Name, Timeout, [Goal, bCrouch, Reach](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (Run.bCrouchWanted != bCrouch)
			{
				Run.StopMoving(PC);
				Run.Crouch(PC, bCrouch);
				Run.PhaseAt = Now;
			}
			// Let the crouch press land standing still.
			if (!Run.IsCrouchSettled(Kate) && Now - Run.PhaseAt < 1.0)
			{
				return EStep::Running;
			}
			if (Run.SteerTo(World, PC, Kate, Goal, Reach, Now))
			{
				Run.Note(FString::Printf(TEXT("at %s"), *Feet(Kate).ToCompactString()));
				return EStep::Done;
			}
			Run.Note(FString::Printf(TEXT("walking to %s, at %s"), *Goal.ToCompactString(), *Feet(Kate).ToCompactString()));
			return EStep::Running;
		} });
	}

	/** Walk Points in order (reach 45 cm each), a jump tap when stuck a second; ends on the last. */
	static void AddRoute(FRunner& R, const FString& Name, TArray<FVector2D> Points, float Timeout = 40.f)
	{
		R.Steps.Add({ Name, Timeout, [Points](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(PC, false);
			if (Run.Phase >= Points.Num())
			{
				Run.Note(FString::Printf(TEXT("at %s"), *Feet(Kate).ToCompactString()));
				return EStep::Done;
			}
			const FVector2D Here(Kate->GetActorLocation());
			const FVector2D Target = Points[Run.Phase];
			const float Distance = FVector2D::Distance(Here, Target);
			if (Distance <= 45.f)
			{
				++Run.Phase;
				Run.BestDistance = 1.e9f;
				return EStep::Running;
			}
			if (Distance < Run.BestDistance - 5.f)
			{
				Run.BestDistance = Distance;
				Run.LastProgress = Now;
			}
			else if (Now - Run.LastProgress > 1.0 && Now - Run.LastJump > 1.0)
			{
				Tap(PC, JumpPath);
				Run.LastJump = Now;
			}
			const FVector2D Dir = (Target - Here).GetSafeNormal();
			Run.Forward(PC, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
			Run.Note(FString::Printf(TEXT("point %d of %d, at %s"), Run.Phase + 1, Points.Num(), *Feet(Kate).ToCompactString()));
			return EStep::Running;
		} });
	}

	/**
	 * Creep up behind the enemy Id and take him down: wait (still, crouched) until Go says the moment has come,
	 * then close on the spot a metre behind him, standing still whenever she is inside his eyes, and press F the
	 * moment the takedown is on and nobody else would hear the body.
	 */
	static void AddStalk(FRunner& R, const FString& Name, const TCHAR* Id, TFunction<bool(const AThugCharacter*)> Go, float Timeout = 60.f)
	{
		const FString Enemy(Id);
		R.Steps.Add({ Name, Timeout, [Enemy, Go](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			AThugCharacter* Thug = HawkeyeInteriorKit::Enemy(World, *Enemy);
			if (!Thug)
			{
				Run.Note(TEXT("no such enemy"));
				return EStep::Failed;
			}
			if (IsDown(Thug))
			{
				Run.Crouch(PC, true);
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
				// Where both of them are, twice a second: a stalk that goes wrong can be read back from the log.
				Run.LastLog = Now;
				UE_LOG(LogTemp, Display, TEXT("[Hawkeye] stalk %s: Kate %s %s%s%s (noise %.1f), him %s yaw %.0f %s %.0f cm/s; %s"), *Enemy, *Feet(Kate).ToCompactString(), Kate->bIsCrouched ? TEXT("crouched") : TEXT("standing"), Kate->IsDodging() ? TEXT(" dodging") : TEXT(""), Kate->GetParkourComponent() && Kate->GetParkourComponent()->IsBusy() ? *FString::Printf(TEXT(" parkour, mode %d"), static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue())) : *FString::Printf(TEXT(", mode %d"), static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue())), Kate->GetMovementNoiseLoudness(),
					*Thug->GetActorLocation().ToCompactString(), Thug->GetActorRotation().Yaw,
					Thug->GetAlertState() == EThugAlertState::Suspicious ? TEXT("suspicious") : TEXT("calm"), Thug->GetVelocity().Size2D(), *Run.StepDetail);
			}
			if (!Run.bCrouchWanted)
			{
				Run.StopMoving(PC);
				Run.Crouch(PC, true);
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
		} });
	}

	/** At the stair's top landing, wait for the gunner to stand at an end of his beat, then open the gallery door. */
	static void AddOpenGalleryDoor(FRunner& R)
	{
		R.Steps.Add({ TEXT("the gallery door, when the gunner stands at an end of his beat"), 40.f, [](FRunner& Run, UWorld* World,
			AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			const AThugCharacter* Gunner = Enemy(World, TEXT("gallery"));
			ADoorActor* Door = nullptr;
			for (ADoorActor* Each : All<ADoorActor>(World))
			{
				Door = FVector2D::Distance(FVector2D(Each->GetActorLocation()), FVector2D(800.f, 685.f)) < 60.f ? Each : Door;
			}
			if (!Door)
			{
				Run.Note(TEXT("no gallery door"));
				return EStep::Failed;
			}
			if (Door->IsOpen())
			{
				return EStep::Done;
			}
			const bool bStill = Gunner && !IsDown(Gunner) && Gunner->GetVelocity().Size2D() < 20.f
				&& (FVector::Dist2D(Gunner->GetActorLocation(), FVector(1150.f, 250.f, 0.f)) < 150.f
					|| FVector::Dist2D(Gunner->GetActorLocation(), FVector(1150.f, 1150.f, 0.f)) < 150.f);
			if (!bStill && Gunner && !IsDown(Gunner))
			{
				Run.Note(FString::Printf(TEXT("waiting for the gunner to stop (at %s)"), *Gunner->GetActorLocation().ToCompactString()));
				return EStep::Running;
			}
			Run.StopMoving(PC);
			PC->SetControlRotation(FRotator(-8.f, 0.f, 0.f));
			FRunner::InteractHere(Kate);
			Run.Note(FString::Printf(TEXT("opened with the gunner still at %s"), Gunner ? *Gunner->GetActorLocation().ToCompactString() : TEXT("?")));
			return Door->IsOpen() ? EStep::Done : EStep::Running;
		} });
	}

	/** Pick up the dropped vault keycard. */
	static void AddKeycard(FRunner& R)
	{
		R.Steps.Add({ TEXT("the archer's keycard"), 20.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (Kate->HasKeycard(TEXT("vault")))
			{
				Run.Note(TEXT("she has the vault card"));
				return EStep::Done;
			}
			const APickupActor* Card = nullptr;
			for (const APickupActor* Each : All<APickupActor>(World))
			{
				Card = Each->KeycardId == FName(TEXT("vault")) && !Each->IsCarried() ? Each : Card;
			}
			if (!Card)
			{
				Run.Note(TEXT("no dropped vault keycard"));
				return EStep::Running;
			}
			const FVector At = Card->GetActorLocation() - FVector(0.f, 0.f, Card->HoverHeight);
			if (Run.SteerTo(World, PC, Kate, At, 50.f, Now))
			{
				FRunner::InteractHere(Kate);
			}
			Run.Note(FString::Printf(TEXT("the card is at %s"), *At.ToCompactString()));
			return EStep::Running;
		} });
	}

	/** Open the vault with the card and walk in. */
	static void AddVault(FRunner& R)
	{
		R.Steps.Add({ TEXT("open the vault and step in"), 25.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			ADoorActor* Door = nullptr;
			for (ADoorActor* Each : All<ADoorActor>(World))
			{
				Door = Each->bLocked ? Each : Door;
			}
			if (!Door)
			{
				Run.Note(TEXT("no locked door"));
				return EStep::Failed;
			}
			if (!Door->IsOpen())
			{
				if (Run.SteerTo(World, PC, Kate, VaultStep, 40.f, Now))
				{
					PC->SetControlRotation(FRotator(-8.f, 0.f, 0.f));
					FRunner::InteractHere(Kate);
				}
				Run.Note(TEXT("at the vault door"));
				return EStep::Running;
			}
			if (Run.SteerTo(World, PC, Kate, InVault, 60.f, Now))
			{
				Run.Note(FString::Printf(TEXT("in the vault at %s"), *Feet(Kate).ToCompactString()));
				return EStep::Done;
			}
			return EStep::Running;
		} });
	}

	/** From the hall floor to the gallery rail's anchor by grapple, then the roof door and the district's roof. */
	static void AddRoofExit(FRunner& R)
	{
		AddWalk(R, TEXT("to the grapple spot on the hall floor"), GrappleSpot, false, 60.f);
		R.Steps.Add({ TEXT("grapple to the gallery"), 15.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			UGrappleComponent* Grapple = Kate->GetGrappleComponent();
			if (!Grapple)
			{
				return EStep::Failed;
			}
			if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
			{
				return EStep::Running;
			}
			if (Run.Phase == 1)
			{
				const FVector Here = Feet(Kate);
				const bool bUp = Gallery.IsInside(FVector2D(Here)) && Here.Z > FloorHeight - 25.f;
				Run.Note(FString::Printf(TEXT("feet at %s"), *Here.ToCompactString()));
				return Kate->GetCharacterMovement()->IsMovingOnGround() ? (bUp ? EStep::Done : EStep::Failed) : EStep::Running;
			}
			AGrappleAnchor* Nearest = nullptr;
			for (AGrappleAnchor* Anchor : All<AGrappleAnchor>(World))
			{
				Nearest = !Nearest || FVector::Dist(Anchor->GetActorLocation(), Kate->GetActorLocation())
					< FVector::Dist(Nearest->GetActorLocation(), Kate->GetActorLocation()) ? Anchor : Nearest;
			}
			if (!Nearest)
			{
				return EStep::Failed;
			}
			const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
			const FRotator Look = (Nearest->GetMarkerLocation() - Lens).Rotation();
			PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
			Grapple->RefreshTarget();
			if (Grapple->GetTargetAnchor() == Nearest)
			{
				Tap(PC, GrapplePath);
				Run.Phase = 1;
			}
			return EStep::Running;
		} });
		AddWalk(R, TEXT("along the gallery to the roof door"), RoofDoorApproach, false, 50.f, 20.f);
		R.Steps.Add({ TEXT("out by the roof door"), 45.f, [](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			if (IsMap(World, MapPath))
			{
				for (AInteriorExit* Exit : All<AInteriorExit>(World))
				{
					if (Exit->ReturnPointOverride == RoofTag && Exit->GetEntryZone()->IsOverlappingActor(Kate))
					{
						Run.StopMoving(PC);
						FRunner::InteractHere(Kate);
						return EStep::Running;
					}
				}
				Run.Forward(PC, -90.f);
				return EStep::Running;
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
			const float Off = FVector::Dist(Kate->GetActorLocation(), Roof->GetActorLocation());
			Run.Note(FString::Printf(TEXT("back on the district, %.0f cm from the roof point"), Off));
			return Off < 150.f && !Save->IsInInterior() ? EStep::Done : EStep::Failed;
		} });
	}

	/** Fight the enemies Ids until they are down (the loud lap). */
	static void AddFight(FRunner& R, const FString& Name, TArray<FString> Ids, float Timeout)
	{
		R.Steps.Add({ Name, Timeout, [Ids](FRunner& Run, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate, double Now)
		{
			Run.Crouch(PC, false);
			TArray<AThugCharacter*> Group;
			for (const FString& Id : Ids)
			{
				if (AThugCharacter* Thug = Enemy(World, *Id))
				{
					Group.Add(Thug);
				}
			}
			const double WorldNow = World->GetTimeSeconds();
			const EStep Result = Run.Fight(World, PC, Kate, Group, WorldNow);
			FString States;
			for (const AThugCharacter* Thug : Group)
			{
				States += FString::Printf(TEXT("%s %s %.0f hp; "), *Thug->GetName(), IsDown(Thug) ? TEXT("down") : Thug->IsAlerted() ? TEXT("alerted") : TEXT("up"),
					Thug->GetHealthComponent()->GetCurrentHealth());
			}
			Run.Note(FString::Printf(TEXT("Kate %.0f hp; %s"), Kate->GetHealthComponent()->GetCurrentHealth(), *States));
			return Result;
		} });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapInteriorStealth, "Hawkeye.Lap.InteriorStealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapInteriorStealth::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	using namespace HawkeyeInteriorLap;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the interior stealth lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(DistrictPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	const TSharedRef<FRunner> Runner = MakeShared<FRunner>(this, false);
	AddEntry(*Runner);
	// The lobby thug walks into the office and stands there with his back to the door: she follows him in.
	AddStalk(*Runner, TEXT("take the lobby thug down in the office, from behind"), TEXT("lobby"), [](const AThugCharacter*) { return true; });
	AddWalk(*Runner, TEXT("out of the office to the foot of the stair"), StairFoot, false);
	AddRoute(*Runner, TEXT("up the stair to the top landing"), TArray<FVector2D>(StairUp, UE_ARRAY_COUNT(StairUp)));
	AddOpenGalleryDoor(*Runner);
	AddStalk(*Runner, TEXT("take the gunner at the gallery rail down, unheard"), TEXT("gallery"), [](const AThugCharacter*) { return true; }, 90.f);
	AddWalk(*Runner, TEXT("back through the gallery door"), TopLanding, false, 45.f);
	AddRoute(*Runner, TEXT("back down the stair"), TArray<FVector2D>(StairDown, UE_ARRAY_COUNT(StairDown)));
	AddWalk(*Runner, TEXT("creep to the archway into the hall"), HallArch, true);
	// The bat thug walking north up the hall: she crosses behind him to his stop by the display cases.
	AddStalk(*Runner, TEXT("take the bat thug down on the hall floor"), TEXT("hall"), [](const AThugCharacter* Thug)
	{
		return Thug->GetVelocity().Y < -50.f && Thug->GetActorLocation().Y > 650.f;
	});
	AddWalk(*Runner, TEXT("creep to where the archer's beat shows"), ArcherWatch, true, 50.f);
	// The archer walking south to the vault end of his beat: she follows him down it.
	AddStalk(*Runner, TEXT("take the archer down by the vault door"), TEXT("vault"), [](const AThugCharacter* Thug)
	{
		return Thug->GetVelocity().Y > 50.f;
	});
	AddKeycard(*Runner);
	AddVault(*Runner);
	AddRoofExit(*Runner);
	FAutomationTestFramework::Get().EnqueueLatentCommand(Runner);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapInteriorLoud, "Hawkeye.Lap.InteriorLoud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapInteriorLoud::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	using namespace HawkeyeInteriorLap;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the interior loud lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(DistrictPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	const TSharedRef<FRunner> Runner = MakeShared<FRunner>(this, true);
	AddEntry(*Runner);
	AddFight(*Runner, TEXT("fight the lobby thug"), { TEXT("lobby") }, 60.f);
	AddWalk(*Runner, TEXT("through the archway into the hall"), HallArch, false, 60.f);
	AddFight(*Runner, TEXT("fight the hall: bat, gunner on the gallery, archer"), { TEXT("hall"), TEXT("gallery"), TEXT("vault") }, 150.f);
	AddKeycard(*Runner);
	AddVault(*Runner);
	AddRoofExit(*Runner);
	FAutomationTestFramework::Get().EnqueueLatentCommand(Runner);
	return true;
}

#endif
