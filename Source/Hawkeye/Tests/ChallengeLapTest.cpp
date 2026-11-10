#include "Camera/CameraComponent.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTarget.h"
#include "Challenge/ChallengeTracker.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Tests/AutomationCommon.h"
#include "World/CityLedgeSpawner.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"
#include "World/InteractionComponent.h"
#include "World/ThugAIController.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The side challenges played by a script on the district (claude-docs/gameplay-semantics.md, "Side
 * challenges"), in the standalone game like the other laps:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap.ArcheryChallenge1+Hawkeye.Lap.TraversalChallenge1; Quit"
 *
 * Hawkeye.Lap.ArcheryChallenge1: Kate put at City_Challenge_archery_1's pedestal presses E (IA_Interact),
 * then shoots all twelve targets with the bow (IA_Fire held for a full draw, the aim led for gravity and
 * for a mover's slide, as a player who has learnt the range would). A target she misses is tried again.
 * Must complete; reports the score, the time, the medal and every target's points, and writes
 * Saved/Automation/challenge_archery_1.json.
 *
 * Hawkeye.Lap.TraversalChallenge1: first the offline survey: every grapple leg of every traversal route
 * through UGrappleComponent::IsZipClear from the ring before it (the lap's anchor survey). Then Kate at
 * City_Challenge_traversal_1 presses E and runs the route: runs and mantles by IA_Move and IA_Sprint (IA_Jump
 * when a parapet stops her), grapples by aiming at the anchor over the ring and pressing IA_Grapple, and the
 * fire escape as the lap comes down it (crouch at the parapet over the top landing, hang, drop, catch or land,
 * down to the street). Nothing teleports her once the run starts. Must complete; reports the time and the
 * medal and writes Saved/Automation/challenge_traversal_1.json.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapArcheryChallenge1, "Hawkeye.Lap.ArcheryChallenge1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapTraversalChallenge1, "Hawkeye.Lap.TraversalChallenge1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeChallengeLap
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	static const TCHAR* CrouchPath = TEXT("/Game/Input/IA_Crouch.IA_Crouch");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* InteractPath = TEXT("/Game/Input/IA_Interact.IA_Interact");
	static const TCHAR* BowAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrappleAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");
	static const FName BuildingTag(TEXT("CityBuilding"));

	/** Seconds she holds the draw: Kate's 0.8 s full draw and a frame or two. */
	static constexpr double DrawSeconds = 0.85;

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

	static const UInputAction* Action(const TCHAR* Path)
	{
		return LoadObject<UInputAction>(nullptr, Path);
	}

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

	static AChallengeStart* FindStart(UWorld* World, FName Id)
	{
		for (TActorIterator<AChallengeStart> It(World); It; ++It)
		{
			if (It->Definition && It->Definition->Id == Id)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool IsSpawnDone(UWorld* World)
	{
		TActorIterator<ACityLedgeSpawner> It(World);
		return !It || It->IsSpawnComplete();
	}

	/** The quiver the chapter grants, in case the district's mission did not. */
	static void EnsureQuiver(FAutomationTestBase* Test, UInventoryComponent* Inventory)
	{
		if (Inventory->HasBow() && Inventory->GetArrowCount(1) >= 20)
		{
			return;
		}
		Test->AddWarning(TEXT("Kate had no bow or too few arrows; granting DA_Bow_Kate, 30 standard and 6 grapple."));
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, StandardAssetPath);
		Standard.Count = 30;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, GrappleAssetPath);
		Grapple.Count = 6;
		Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, BowAssetPath), { Standard, Grapple });
	}

	/** Puts Kate at Start's stand spot facing along it, healthy, the camera behind her. */
	static void StandAt(APlayerController* PC, AHawkeyeCharacter* Kate, const AChallengeStart* Start)
	{
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Yaw = Start->GetActorRotation().Yaw;
		Kate->TeleportTo(Start->GetStandLocation() + FVector(0.f, 0.f, HalfHeight + 5.f), FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(-10.f, Yaw, 0.f));
		PC->SetViewTarget(Kate);
		// A lap before this one on the same map may have left her down, hurt or under a menu.
		if (Kate->IsDowned())
		{
			Kate->ReviveFromDown(1.f);
		}
		if (!Kate->GetHealthComponent()->IsAlive())
		{
			Kate->GetHealthComponent()->Revive(Kate->GetHealthComponent()->GetMaxHealth());
		}
		Kate->GetHealthComponent()->Heal(1000.f);
		if (AHawkeyePlayerController* Hawkeye = Cast<AHawkeyePlayerController>(PC))
		{
			Hawkeye->CloseChallengeResults();
		}
		PC->SetPause(false);
	}

	/** Why E at the pedestal did nothing, for the report. */
	static FString ExplainStart(const AHawkeyeCharacter* Kate, AChallengeStart* Start)
	{
		const UInteractionComponent* Interaction = Kate->GetInteractionComponent();
		return FString::Printf(TEXT("%.0f cm from the stand spot, focus %s, can interact %d, health %.0f, down %d, prompt \"%s\""),
			FVector::Dist2D(Kate->GetActorLocation(), Start->GetStandLocation()),
			Interaction && Interaction->GetFocusedActor() ? *Interaction->GetFocusedActor()->GetName() : TEXT("none"),
			IInteractable::Execute_CanInteract(Start, const_cast<AHawkeyeCharacter*>(Kate)) ? 1 : 0,
			Kate->GetHealthComponent()->GetCurrentHealth(), Kate->IsDowned() ? 1 : 0,
			*IInteractable::Execute_GetInteractPrompt(Start).ToString());
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	static FString MedalName(EChallengeMedal Medal)
	{
		return UChallengeRules::MedalText(Medal).ToString();
	}

	/** The anchor whose landing point is under a grapple ring (within 80 cm), or null. */
	static AGrappleAnchor* AnchorForRing(UWorld* World, const FVector& RingCentre, float RingUp)
	{
		const FVector RingFeet = RingCentre - FVector(0.f, 0.f, RingUp);
		AGrappleAnchor* Best = nullptr;
		float BestDistance = 80.f;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetLandingLocation(), RingFeet);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	/** Ring centres sit this far above the floor (create_challenges.py CHECKPOINT_UP). */
	static constexpr float RingUp = 110.f;
}

// --- Archery ---------------------------------------------------------------------------------------

class FHawkeyeArcheryChallengeRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeArcheryChallengeRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	enum class EPhase : uint8
	{
		WaitReady,
		Start,
		Pick,
		Draw,
		Watch,
		Done
	};

	/** The next target to shoot: the nearest by angle to where she looks, still up, tried fewest times. */
	AChallengeTarget* PickTarget(const AHawkeyeCharacter* Kate, const UChallengeSubsystem* Challenges) const;
	void Finish(UWorld* World, APlayerController* PC, UChallengeSubsystem* Challenges);

	FAutomationTestBase* Test;
	EPhase Phase = EPhase::WaitReady;
	double PhaseStart = -1.0;
	double RunStart = -1.0;
	TWeakObjectPtr<AChallengeStart> Start;
	TWeakObjectPtr<AChallengeTarget> Target;
	TMap<int32, int32> Tries;
	int32 Shots = 0;
	int32 Misses = 0;
	int32 LoosedBefore = 0;
};

AChallengeTarget* FHawkeyeArcheryChallengeRunner::PickTarget(const AHawkeyeCharacter* Kate, const UChallengeSubsystem* Challenges) const
{
	const FVector Eye = Kate->GetActorLocation();
	const FVector Forward = Kate->GetControlRotation().Vector();
	AChallengeTarget* Best = nullptr;
	float BestScore = BIG_NUMBER;
	for (const TWeakObjectPtr<AChallengeTarget>& Weak : Challenges->GetTargets())
	{
		AChallengeTarget* Candidate = Weak.Get();
		if (!Candidate || Candidate->IsDown())
		{
			continue;
		}
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, (Candidate->GetFaceCentre() - Eye).GetSafeNormal()), -1.f, 1.f));
		const int32* Count = Tries.Find(Candidate->GetTargetIndex());
		const float Score = Angle + (Count ? *Count : 0) * 10.f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	return Best;
}

void FHawkeyeArcheryChallengeRunner::Finish(UWorld* World, APlayerController* PC, UChallengeSubsystem* Challenges)
{
	using namespace HawkeyeChallengeLap;
	Hold(PC, FirePath, false);
	if (AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(PC->GetPawn()))
	{
		if (UBowComponent* Bow = Kate->GetBowComponent())
		{
			Bow->ClearAimOverride();
		}
	}
	const UChallengeTracker* Tracker = Challenges->GetTracker();
	if (!Tracker->HasLastResult())
	{
		Test->AddError(TEXT("Archery challenge 1: the run never ended."));
		return;
	}
	const FChallengeResult& Result = Tracker->GetLastResult();
	FString Points;
	for (int32 Index = 0; Index < Tracker->GetTargetPoints().Num(); ++Index)
	{
		Points += FString::Printf(TEXT("%s%d"), Index ? TEXT(", ") : TEXT(""), Tracker->GetTargetPoints()[Index]);
	}
	const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
	Test->AddInfo(FString::Printf(TEXT("Archery challenge 1: %s, score %d in %.2f s, %s; %d of %d targets, %d arrows (%d missed); ")
		TEXT("points [%s]; results card %s."),
		*UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Score, Result.Seconds, *MedalName(Result.Medal),
		Result.Progress, Result.Total, Shots, Misses, *Points, HawkeyePC && HawkeyePC->IsChallengeResultsOpen() ? TEXT("open") : TEXT("shut")));
	WriteText(TEXT("challenge_archery_1.json"), FString::Printf(
		TEXT("{\n  \"challenge\": \"archery_1\",\n  \"completed\": %s,\n  \"reason\": \"%s\",\n  \"score\": %d,\n  \"seconds\": %.2f,\n")
		TEXT("  \"medal\": \"%s\",\n  \"targets_down\": %d,\n  \"targets\": %d,\n  \"arrows\": %d,\n  \"missed\": %d,\n")
		TEXT("  \"points\": [%s],\n  \"best_score\": %d\n}\n"),
		Result.IsCompleted() ? TEXT("true") : TEXT("false"), *UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Score,
		Result.Seconds, *MedalName(Result.Medal), Result.Progress, Result.Total, Shots, Misses, *Points, Result.Record.BestScore));
	if (!Result.IsCompleted())
	{
		Test->AddError(FString::Printf(TEXT("Archery challenge 1 did not complete: %s with %d of %d targets down."),
			*UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Progress, Result.Total));
	}
	if (AHawkeyePlayerController* Owner = Cast<AHawkeyePlayerController>(PC); Owner && Owner->IsChallengeResultsOpen())
	{
		Owner->CloseChallengeResults();
	}
}

bool FHawkeyeArcheryChallengeRunner::Update()
{
	using namespace HawkeyeChallengeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
	if (!World || !Kate || !Challenges)
	{
		if (!World || World->GetTimeSeconds() > 30.0)
		{
			Test->AddError(TEXT("Archery challenge 1: no game world, no Kate, or no challenge subsystem."));
			return true;
		}
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (PhaseStart < 0.0)
	{
		PhaseStart = Now;
	}
	UBowComponent* Bow = Kate->GetBowComponent();

	switch (Phase)
	{
	case EPhase::WaitReady:
		if ((!IsSpawnDone(World) || !Kate->GetCharacterMovement()->IsMovingOnGround()) && Now - PhaseStart < 20.0)
		{
			return false;
		}
		Start = FindStart(World, TEXT("archery_1"));
		if (!Start.IsValid())
		{
			Test->AddError(TEXT("No City_Challenge_archery_1 pedestal on the district."));
			return true;
		}
		EnsureQuiver(Test, Kate->GetInventoryComponent());
		Kate->GetInventoryComponent()->SelectArrowSlot(1);
		StandAt(PC, Kate, Start.Get());
		Phase = EPhase::Start;
		PhaseStart = Now;
		return false;

	case EPhase::Start:
		if (Now - PhaseStart < 1.0)
		{
			return false;
		}
		if (!Challenges->IsRunning())
		{
			// A press every half second: injecting every frame is one long hold, one Started.
			if (FMath::Fmod(Now - PhaseStart, 0.5) < World->GetDeltaSeconds())
			{
				Tap(PC, InteractPath);
			}
			if (Now - PhaseStart > 4.0)
			{
				Test->AddError(FString::Printf(TEXT("Archery challenge 1: E at the pedestal started nothing (%s)."), *ExplainStart(Kate, Start.Get())));
				return true;
			}
			return false;
		}
		RunStart = Now;
		Test->AddInfo(FString::Printf(TEXT("Archery challenge 1 started from %s: %d targets, %d thugs calmed."), *Start->GetName(),
			Challenges->GetTargets().Num(), Challenges->GetCalmedThugCount()));
		LoosedBefore = Bow ? Bow->GetArrowsLoosed() : 0;
		Phase = EPhase::Pick;
		PhaseStart = Now;
		return false;

	case EPhase::Pick:
		if (!Challenges->IsRunning())
		{
			Phase = EPhase::Done;
			break;
		}
		Target = PickTarget(Kate, Challenges);
		if (!Target.IsValid())
		{
			return false;
		}
		Tries.FindOrAdd(Target->GetTargetIndex())++;
		Hold(PC, FirePath, true);
		Phase = EPhase::Draw;
		PhaseStart = Now;
		return false;

	case EPhase::Draw:
	{
		AChallengeTarget* Aimed = Target.Get();
		if (!Aimed || !Bow || !Challenges->IsRunning())
		{
			Hold(PC, FirePath, false);
			Phase = Challenges->IsRunning() ? EPhase::Pick : EPhase::Done;
			break;
		}
		// Where the arrow meets the face, dropping over its flight and the face sliding meanwhile.
		const FVector Aim = AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Aimed->GetFaceCentre(),
			Aimed->GetSlideVelocity(), 6000.f, World->GetGravityZ());
		Bow->SetAimOverride(Aim);
		AimAt(PC, Kate, Aim);
		if (Now - PhaseStart >= DrawSeconds)
		{
			Hold(PC, FirePath, false);
			++Shots;
			Phase = EPhase::Watch;
			PhaseStart = Now;
		}
		return false;
	}

	case EPhase::Watch:
	{
		// A beat for the arrow to get there (40 m is 0.7 s), then the next one.
		AChallengeTarget* Aimed = Target.Get();
		const float Flight = Aimed ? FVector::Dist(Kate->GetActorLocation(), Aimed->GetFaceCentre()) / 6000.f + 0.1f : 0.f;
		if (Aimed && !Aimed->IsDown() && Now - PhaseStart < Flight && Challenges->IsRunning())
		{
			return false;
		}
		if (Aimed && !Aimed->IsDown())
		{
			++Misses;
			Test->AddInfo(FString::Printf(TEXT("Archery challenge 1: missed target %d (try %d)."), Aimed->GetTargetIndex(),
				Tries.FindRef(Aimed->GetTargetIndex())));
		}
		Phase = Challenges->IsRunning() ? EPhase::Pick : EPhase::Done;
		PhaseStart = Now;
		return false;
	}

	case EPhase::Done:
		break;
	}

	if (Phase == EPhase::Done || (RunStart >= 0.0 && Now - RunStart > 75.0))
	{
		if (Challenges->IsRunning())
		{
			Test->AddError(TEXT("Archery challenge 1 still running 75 s in; abandoning it."));
			Challenges->AbortChallenge();
		}
		Finish(World, PC, Challenges);
		return true;
	}
	return false;
}

// --- Traversal -------------------------------------------------------------------------------------

class FHawkeyeTraversalChallengeRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeTraversalChallengeRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	/** Every grapple leg of every traversal route through the grapple's own offline rule. */
	void SurveyZips(UWorld* World, AHawkeyeCharacter* Kate);

	/** Pushes IA_Move toward Target (sprinting when asked), the camera swinging behind the run. True on arrival. */
	bool Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive, float DeltaSeconds);
	void StopMoving(APlayerController* PC);

	/** One frame of a run or mantle leg toward ring Next: steer, and jump when a parapet stops her. */
	void RunLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring, float DeltaSeconds);

	/** One frame of a grapple leg: aim at the ring's anchor and fire; once landed, walk into the ring. */
	void GrappleLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring, float DeltaSeconds);

	/** One frame of the fire-escape descent to the street ring. */
	void DescentLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring, float DeltaSeconds);

	void Finish(UWorld* World, APlayerController* PC, UChallengeSubsystem* Challenges);

	FAutomationTestBase* Test;
	int32 Phase = 0;
	double PhaseStart = -1.0;
	double RunStart = -1.0;
	double LegStart = -1.0;
	int32 Leg = -1;
	TWeakObjectPtr<AChallengeStart> Start;
	bool bMoveInjecting = false;
	bool bSprintHeld = false;
	FString FailReason;
	TArray<FString> LegNotes;

	// Run legs: how long she has been stopped against something with input held.
	float StuckFor = 0.f;
	int32 Jumps = 0;
	int32 Mantles = 0;
	int32 Vaults = 0;
	bool bParkourSeen = false;

	// Grapple legs.
	TWeakObjectPtr<AGrappleAnchor> Anchor;
	double AimSince = -1.0;
	double PressedAt = -1.0;
	int32 Presses = 0;
	int32 Zips = 0;
	bool bZipped = false;
	bool bExplained = false;

	// Descent.
	int32 DescentPhase = 0;
	FVector DescentStand = FVector::ZeroVector;
	FVector DescentOut = FVector::ForwardVector;
	int32 Hangs = 0;
	int32 Catches = 0;
	int32 Retries = 0;
	bool bPushing = false;
};

void FHawkeyeTraversalChallengeRunner::StopMoving(APlayerController* PC)
{
	using namespace HawkeyeChallengeLap;
	if (bSprintHeld)
	{
		Hold(PC, SprintPath, false);
		bSprintHeld = false;
	}
	if (bMoveInjecting)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
		{
			Input->StopContinuousInputInjectionForAction(Action(MovePath));
		}
		bMoveInjecting = false;
	}
}

bool FHawkeyeTraversalChallengeRunner::Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint,
	float Arrive, float DeltaSeconds)
{
	using namespace HawkeyeChallengeLap;
	const FVector To = (Target - Kate->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	if (To.Size() <= Arrive)
	{
		return true;
	}
	const FVector Direction = To.GetSafeNormal();
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
	if (bSprint != bSprintHeld)
	{
		Hold(PC, SprintPath, bSprint);
		bSprintHeld = bSprint;
	}
	return false;
}

void FHawkeyeTraversalChallengeRunner::SurveyZips(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeChallengeLap;
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	int32 Legs = 0;
	int32 Clear = 0;
	TArray<FString> Blocked;
	for (TActorIterator<AChallengeStart> It(World); It; ++It)
	{
		const UChallengeDefinition* Definition = It->Definition;
		if (!Definition || Definition->Type != EChallengeType::Traversal)
		{
			continue;
		}
		for (int32 Index = 0; Index < Definition->Checkpoints.Num(); ++Index)
		{
			if (!Definition->CheckpointLegs.IsValidIndex(Index) || Definition->CheckpointLegs[Index] != EChallengeLeg::Grapple)
			{
				continue;
			}
			++Legs;
			const FVector Before = Index > 0 ? Definition->Checkpoints[Index - 1].GetLocation() - FVector(0.f, 0.f, RingUp)
				: Definition->StartLocation;
			const AGrappleAnchor* Over = AnchorForRing(World, Definition->Checkpoints[Index].GetLocation(), RingUp);
			AActor* Blocker = nullptr;
			const bool bClear = Over && Kate->GetGrappleComponent()->IsZipClear(Before + FVector(0.f, 0.f, HalfHeight + 2.f), Over, true, &Blocker);
			Clear += bClear ? 1 : 0;
			if (!bClear)
			{
				Blocked.Add(FString::Printf(TEXT("%s ring %d (%s)"), *Definition->Id.ToString(), Index + 1,
					Over ? *FString::Printf(TEXT("blocked by %s"), *GetNameSafe(Blocker)) : TEXT("no anchor under it")));
			}
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Traversal survey: %d of %d grapple legs clear by UGrappleComponent::IsZipClear%s%s"), Clear, Legs,
		Blocked.Num() ? TEXT("; ") : TEXT(""), *FString::Join(Blocked, TEXT("; "))));
	if (Clear < Legs)
	{
		Test->AddWarning(FString::Printf(TEXT("Traversal survey: %d grapple leg(s) not clear offline."), Legs - Clear));
	}
}

void FHawkeyeTraversalChallengeRunner::RunLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring,
	float DeltaSeconds)
{
	using namespace HawkeyeChallengeLap;
	const UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (Parkour->IsBusy())
	{
		if (!bParkourSeen)
		{
			bParkourSeen = true;
			const FString Move = UEnum::GetValueAsString(Parkour->GetActiveMove());
			Mantles += Move.Contains(TEXT("Mantle")) ? 1 : 0;
			Vaults += Move.Contains(TEXT("Vault")) ? 1 : 0;
			LegNotes.Add(FString::Printf(TEXT("ring %d: %s"), Leg + 1, *Move));
		}
		return;
	}
	bParkourSeen = false;
	Steer(PC, Kate, Ring, true, 0.f, DeltaSeconds);
	const bool bStopped = Kate->GetCharacterMovement()->IsMovingOnGround() && Kate->GetVelocity().Size2D() < 80.f;
	StuckFor = bStopped ? StuckFor + DeltaSeconds : 0.f;
	if (StuckFor > 0.6f)
	{
		// A parapet the sprint did not take on its own: the jump key's mantle or vault.
		StuckFor = 0.f;
		++Jumps;
		Tap(PC, JumpPath);
		LegNotes.Add(FString::Printf(TEXT("ring %d: jump at %s"), Leg + 1, *Kate->GetActorLocation().ToCompactString()));
	}
}

void FHawkeyeTraversalChallengeRunner::GrappleLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring,
	float DeltaSeconds)
{
	using namespace HawkeyeChallengeLap;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	if (bZipped)
	{
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight() || !Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			return;
		}
		// Landed: into the ring if the zip did not put her through it.
		Steer(PC, Kate, Ring, false, 0.f, DeltaSeconds);
		return;
	}
	if (!Anchor.IsValid())
	{
		Anchor = AnchorForRing(World, Ring, RingUp);
		if (!Anchor.IsValid())
		{
			FailReason = FString::Printf(TEXT("no anchor under ring %d"), Leg + 1);
			return;
		}
		AimSince = World->GetTimeSeconds();
	}
	if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
	{
		bZipped = true;
		++Zips;
		StopMoving(PC);
		return;
	}
	if (PressedAt >= 0.0 && World->GetTimeSeconds() - PressedAt < 0.4)
	{
		return;
	}
	StopMoving(PC);
	AimAt(PC, Kate, Anchor->GetMarkerLocation());
	Grapple->RefreshTarget();
	if (Grapple->GetTargetAnchor() == Anchor.Get() && Grapple->CanChain())
	{
		++Presses;
		PressedAt = World->GetTimeSeconds();
		Tap(PC, GrapplePath);
		return;
	}
	if (World->GetTimeSeconds() - AimSince > 1.0)
	{
		// Not targeted from here: once, say why; then a step away if it is steeply overhead (the camera
		// cannot hold it), else toward it (out of range, or the lens behind a parapet).
		const FVector ToMarker = Anchor->GetMarkerLocation() - Kate->GetActorLocation();
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(ToMarker.Z, FMath::Max(ToMarker.Size2D(), 1.f)));
		if (!bExplained)
		{
			bExplained = true;
			const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ChallengeLapSight), false, Kate);
			Params.AddIgnoredActor(Anchor.Get());
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, Lens, Anchor->GetMarkerLocation(), ECC_Visibility, Params);
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Kate->GetFollowCamera()->GetForwardVector(),
				(Anchor->GetMarkerLocation() - Lens).GetSafeNormal()), -1.f, 1.f)));
			LegNotes.Add(FString::Printf(TEXT("ring %d: %s not targeted: %.0f cm, pitch %.0f, %.1f deg off the lens, sight %s"), Leg + 1,
				*Anchor->GetName(), ToMarker.Size(), Pitch, Off,
				bBlocked ? *FString::Printf(TEXT("blocked by %s %.0f cm short"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("?"),
					FVector::Dist(Lens, Anchor->GetMarkerLocation()) - Hit.Distance) : TEXT("clear")));
		}
		// Each second a different way: toward it (or back when it is steep), then to either side.
		const FVector Along = ToMarker.GetSafeNormal2D();
		const FVector Side(-Along.Y, Along.X, 0.f);
		const int32 Try = FMath::FloorToInt((World->GetTimeSeconds() - AimSince - 1.0) / 1.0) % 3;
		const FVector Step = Try == 0 ? Along * (Pitch > 50.f ? -200.f : 200.f) : Side * (Try == 1 ? 200.f : -200.f);
		Steer(PC, Kate, Kate->GetActorLocation() + Step, false, 0.f, DeltaSeconds);
		if (World->GetTimeSeconds() - AimSince > 6.0)
		{
			FailReason = FString::Printf(TEXT("ring %d: %s never became the grapple target (%.0f cm away)"), Leg + 1, *Anchor->GetName(),
				FVector::Dist(Kate->GetActorLocation(), Anchor->GetMarkerLocation()));
		}
	}
}

void FHawkeyeTraversalChallengeRunner::DescentLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring,
	float DeltaSeconds)
{
	using namespace HawkeyeChallengeLap;
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	const double Now = World->GetTimeSeconds();
	const float FeetZ = Feet(Kate).Z;
	if (DescentPhase == 0)
	{
		// The fire escape whose foot is the street ring: its top landing.
		const AFireEscapeLanding* Top = nullptr;
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			const FVector Out = It->GetActorRightVector().GetSafeNormal2D();
			const FVector Foot = It->GetActorLocation() + Out * 140.f;
			if (FVector::Dist2D(Foot, Ring) < 400.f && (!Top || It->GetRecord().Floor > Top->GetRecord().Floor))
			{
				Top = *It;
			}
		}
		if (!Top)
		{
			FailReason = TEXT("no fire escape under the descent ring");
			return;
		}
		DescentOut = Top->GetActorRightVector().GetSafeNormal2D();
		DescentStand = FVector(Top->GetActorLocation().X, Top->GetActorLocation().Y, FeetZ) - DescentOut * 110.f;
		LegNotes.Add(FString::Printf(TEXT("ring %d: down %s's escape, %d landings"), Leg + 1, *Top->GetRecord().OsmId, Top->GetRecord().Floor));
		DescentPhase = 1;
		PhaseStart = Now;
	}
	if (DescentPhase == 1)
	{
		if (Steer(PC, Kate, DescentStand, false, 40.f, DeltaSeconds) || Now - PhaseStart > 8.0)
		{
			DescentPhase = 2;
			PhaseStart = Now;
		}
	}
	else if (DescentPhase == 2)
	{
		// Push toward the edge, then crouch: the drop to hang.
		Steer(PC, Kate, FVector(DescentStand.X, DescentStand.Y, FeetZ) + DescentOut * 1000.f, false, 1.f, DeltaSeconds);
		if (Parkour->IsBusy())
		{
			StopMoving(PC);
			bPushing = false;
			DescentPhase = 3;
			PhaseStart = Now;
		}
		else if (Now - PhaseStart > 0.25 && !bPushing)
		{
			bPushing = true;
			Tap(PC, CrouchPath);
		}
		else if (Now - PhaseStart > 1.0)
		{
			bPushing = false;
			PhaseStart = Now;
			if (++Retries >= 4)
			{
				FailReason = FString::Printf(TEXT("crouch at the edge found no drop to hang at %s"), *Kate->GetActorLocation().ToCompactString());
			}
		}
	}
	else if (DescentPhase == 3)
	{
		if (Parkour->IsHanging())
		{
			if (Now - PhaseStart > 0.3)
			{
				++Hangs;
				Tap(PC, CrouchPath);
				DescentPhase = 4;
				PhaseStart = Now;
			}
		}
		else if (!Parkour->IsBusy() && Now - PhaseStart > 2.0)
		{
			// Over the edge without a hang: treat it as a drop.
			DescentPhase = 4;
			PhaseStart = Now;
		}
	}
	else if (DescentPhase == 4 && Now - PhaseStart > 0.15)
	{
		if (Parkour->IsBusy())
		{
			if (Parkour->IsHanging() || Parkour->GetActiveMove() == EHawkeyeParkourMove::LedgeGrab)
			{
				++Catches;
				DescentPhase = 3;
				PhaseStart = Now;
			}
		}
		else if (Kate->GetCharacterMovement()->IsMovingOnGround() && Now - PhaseStart > 0.2)
		{
			if (FeetZ - Ring.Z > 0.f)
			{
				// On a landing: crouch at its rail again.
				DescentStand = FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, FeetZ);
				Retries = 0;
				DescentPhase = 2;
				PhaseStart = Now;
			}
			else
			{
				DescentPhase = 5;
			}
		}
		else if (Now - PhaseStart > 5.0)
		{
			FailReason = FString::Printf(TEXT("still in the air 5 s after a drop at %s"), *Kate->GetActorLocation().ToCompactString());
		}
	}
	else if (DescentPhase == 5)
	{
		// On the street: into the ring.
		Steer(PC, Kate, Ring, false, 0.f, DeltaSeconds);
	}
}

void FHawkeyeTraversalChallengeRunner::Finish(UWorld* World, APlayerController* PC, UChallengeSubsystem* Challenges)
{
	using namespace HawkeyeChallengeLap;
	StopMoving(PC);
	const UChallengeTracker* Tracker = Challenges->GetTracker();
	if (!Tracker->HasLastResult())
	{
		Test->AddError(TEXT("Traversal challenge 1: the run never ended."));
		return;
	}
	const FChallengeResult& Result = Tracker->GetLastResult();
	Test->AddInfo(FString::Printf(TEXT("Traversal challenge 1: %s in %.2f s, %s; %d of %d rings; %d zips (%d presses), %d mantles, %d vaults, ")
		TEXT("%d jumps, descent %d hangs %d catches; %s."),
		*UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Seconds, *MedalName(Result.Medal), Result.Progress, Result.Total,
		Zips, Presses, Mantles, Vaults, Jumps, Hangs, Catches, *FString::Join(LegNotes, TEXT("; "))));
	WriteText(TEXT("challenge_traversal_1.json"), FString::Printf(
		TEXT("{\n  \"challenge\": \"traversal_1\",\n  \"completed\": %s,\n  \"reason\": \"%s\",\n  \"seconds\": %.2f,\n  \"medal\": \"%s\",\n")
		TEXT("  \"rings\": %d,\n  \"of\": %d,\n  \"zips\": %d,\n  \"mantles\": %d,\n  \"vaults\": %d,\n  \"jumps\": %d,\n  \"hangs\": %d,\n")
		TEXT("  \"catches\": %d,\n  \"fail\": \"%s\"\n}\n"),
		Result.IsCompleted() ? TEXT("true") : TEXT("false"), *UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Seconds,
		*MedalName(Result.Medal), Result.Progress, Result.Total, Zips, Mantles, Vaults, Jumps, Hangs, Catches, *FailReason));
	if (!Result.IsCompleted())
	{
		Test->AddError(FString::Printf(TEXT("Traversal challenge 1 did not complete: %s at ring %d of %d%s."),
			*UChallengeRules::EndReasonText(Result.Reason).ToString(), Result.Progress + 1, Result.Total,
			FailReason.IsEmpty() ? TEXT("") : *(TEXT(" (") + FailReason + TEXT(")"))));
	}
	if (AHawkeyePlayerController* Owner = Cast<AHawkeyePlayerController>(PC); Owner && Owner->IsChallengeResultsOpen())
	{
		Owner->CloseChallengeResults();
	}
}

bool FHawkeyeTraversalChallengeRunner::Update()
{
	using namespace HawkeyeChallengeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
	if (!World || !Kate || !Challenges)
	{
		if (!World || World->GetTimeSeconds() > 30.0)
		{
			Test->AddError(TEXT("Traversal challenge 1: no game world, no Kate, or no challenge subsystem."));
			return true;
		}
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const float DeltaSeconds = World->GetDeltaSeconds();
	if (PhaseStart < 0.0)
	{
		PhaseStart = Now;
	}

	if (Phase == 0)
	{
		if ((!IsSpawnDone(World) || !Kate->GetCharacterMovement()->IsMovingOnGround()) && Now - PhaseStart < 20.0)
		{
			return false;
		}
		Start = FindStart(World, TEXT("traversal_1"));
		if (!Start.IsValid())
		{
			Test->AddError(TEXT("No City_Challenge_traversal_1 pedestal on the district."));
			return true;
		}
		EnsureQuiver(Test, Kate->GetInventoryComponent());
		SurveyZips(World, Kate);
		StandAt(PC, Kate, Start.Get());
		Phase = 1;
		PhaseStart = Now;
		return false;
	}
	if (Phase == 1)
	{
		if (Now - PhaseStart < 1.0)
		{
			return false;
		}
		if (!Challenges->IsRunning())
		{
			// A press every half second: injecting every frame is one long hold, one Started.
			if (FMath::Fmod(Now - PhaseStart, 0.5) < World->GetDeltaSeconds())
			{
				Tap(PC, InteractPath);
			}
			if (Now - PhaseStart > 4.0)
			{
				Test->AddError(FString::Printf(TEXT("Traversal challenge 1: E at the pedestal started nothing (%s)."), *ExplainStart(Kate, Start.Get())));
				return true;
			}
			return false;
		}
		RunStart = Now;
		Test->AddInfo(FString::Printf(TEXT("Traversal challenge 1 started from %s: %d rings, %d thugs calmed."), *Start->GetName(),
			Challenges->GetCheckpoints().Num(), Challenges->GetCalmedThugCount()));
		Phase = 2;
		return false;
	}

	const UChallengeDefinition* Definition = Start.IsValid() ? Start->Definition.Get() : nullptr;
	const int32 Next = Challenges->GetTracker()->GetNextCheckpointIndex();
	if (!Challenges->IsRunning() || !Definition || Next == INDEX_NONE || !FailReason.IsEmpty() || Now - RunStart > 160.0)
	{
		if (Challenges->IsRunning())
		{
			Test->AddError(FString::Printf(TEXT("Traversal challenge 1 stopped at ring %d: %s."), Next + 1,
				FailReason.IsEmpty() ? TEXT("over 160 s") : *FailReason));
			Challenges->AbortChallenge();
		}
		Finish(World, PC, Challenges);
		return true;
	}
	if (Next != Leg)
	{
		// A new leg: reset its bookkeeping.
		Leg = Next;
		LegStart = Now;
		StuckFor = 0.f;
		Anchor = nullptr;
		PressedAt = -1.0;
		bZipped = false;
		bExplained = false;
		DescentPhase = 0;
		Retries = 0;
		Test->AddInfo(FString::Printf(TEXT("Traversal %.1f s: ring %d by %s"), Now - RunStart, Leg + 1,
			Definition->CheckpointLegs.IsValidIndex(Leg) ? *UEnum::GetValueAsString(Definition->CheckpointLegs[Leg]) : TEXT("?")));
	}
	const FVector Ring = Definition->Checkpoints[Leg].GetLocation();
	const EChallengeLeg How = Definition->CheckpointLegs.IsValidIndex(Leg) ? Definition->CheckpointLegs[Leg] : EChallengeLeg::Run;
	switch (How)
	{
	case EChallengeLeg::Grapple:
		GrappleLeg(World, PC, Kate, Ring, DeltaSeconds);
		break;
	case EChallengeLeg::Descent:
		DescentLeg(World, PC, Kate, Ring, DeltaSeconds);
		break;
	default:
		RunLeg(World, PC, Kate, Ring, DeltaSeconds);
		break;
	}
	if (Now - LegStart > 30.0 && FailReason.IsEmpty())
	{
		FailReason = FString::Printf(TEXT("ring %d took over 30 s (Kate at %s, %s)"), Leg + 1, *Kate->GetActorLocation().ToCompactString(),
			*Kate->GetMovementDebugText());
	}
	return false;
}

bool FHawkeyeLapArcheryChallenge1::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the archery challenge. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeArcheryChallengeRunner(this));
	return true;
}

bool FHawkeyeLapTraversalChallenge1::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the traversal challenge. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTraversalChallengeRunner(this));
	return true;
}

#endif
