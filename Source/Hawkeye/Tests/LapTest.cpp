#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/FightMetrics.h"
#include "Combat/HealthComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/GrappleAuditKit.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/LapFightPressure.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "World/CityLedgeSpawner.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The stage 2 lap (docs/plans/02-prototype.md, "Done when"): Kate driven round the East Village
 * block by a script, timed and measured. Needs a real RHI and the standalone game (testing.md 2b):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap; Quit"
 *
 * Hawkeye.Lap.EastVillage: from the PlayerStart, sprint 25 m out along the street and back past the
 * start (47 m in all), auto-vault City_Test_Vault (90 cm) and auto-mantle City_Test_Mantle (150 cm)
 * at a sprint, grapple to the nearest rooftop anchor, run across that roof, grapple to another
 * building and chain on from it up to two more times before landing, then grapple on to a third roof
 * (preferring roofs with a fire escape; every planned grapple's anchor seen by the camera from the spot,
 * as the picker needs, not only zip-clear), so three roofs are crossed without the street; the JSON
 * gives roofs_without_street and roof_chain_seconds. Then come down the fire escape of the roof she ends on (crouch at the parapet over its top
 * landing: hang, drop, and then crouch at each landing's rail or let each drop catch the next rail,
 * down to a last drop to the street), and run back to the start along the navmesh. Nothing
 * teleports her after the start. Measures the time, how often the capsule was stopped for more than 0.5 s with
 * move input held, the attempts each move took, and the frame time; writes
 * Saved/Automation/lap_eastvillage.json and Saved/Screenshots/Lap/lap_{start,roof,end}.png, plus
 * chain_midair.png a moment after the first chain that redirected in the air.
 *
 * Input: movement, sprint, jump, grapple, fire and melee go through Enhanced Input injection on
 * the real IA_ assets every frame (the same bindings a key press reaches). The camera is turned by
 * setting the control rotation (what the mouse does through IA_Look). The dodge is a direct
 * TryDodge call, because its input is a Ctrl tap timed against move input inside one frame.
 * The district's thugs stop thinking for the lap. If no move gets Kate off a roof, the lap stops
 * there, incomplete, and says where and why.
 *
 * Hawkeye.Lap.RoofFight: Kate against the RoofPair on the cross_block roof with the bow and melee;
 * must win with health above 0. Writes Saved/Automation/lap_roof_fight.json.
 *
 * Hawkeye.Lap.ArcherDuel: Kate on the find_arrow roof against the ArcherPair (both archers thinking,
 * on their own sight), using the parapet for cover and the bow; must win inside 60 s with health
 * above 0. Writes Saved/Automation/lap_archer_duel.json with the hits she took and the arrows used.
 *
 * Hawkeye.Lap.StreetFight: Kate on the Avenue A sidewalk against the StreetGroup (bat, gunner, heavy)
 * with fists, strikes, arrows and two trick arrows; must win. Writes Saved/Automation/lap_street_fight.json
 * with the time, hits taken, arrows used, dodges, the annoyance share and the untelegraphed hits.
 *
 * In both melee fights she plays the melee as a player who has learnt it: a tap of V into a wind-up she
 * can see in front of her (0.2 s after it starts, a reaction) parries it, F on anyone open to a finisher
 * within reach finishes him, and her lights chain. The JSON adds parries, finishers, perfect dodges and
 * the highest combo count.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapEastVillage, "Hawkeye.Lap.EastVillage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapRoofFight, "Hawkeye.Lap.RoofFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapArcherDuel, "Hawkeye.Lap.ArcherDuel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapStreetFight, "Hawkeye.Lap.StreetFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

namespace HawkeyeLap
{
	/** The difficulty the fight ran on ("Normal"; -Difficulty= on the command line changes it). */
	static FString DifficultyName(const UObject* WorldContext)
	{
		return UDifficultySubsystem::GetDifficultyName(UHawkeyeSettingsSubsystem::GetCurrentSettings(WorldContext).Difficulty).ToString();
	}

	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	static const TCHAR* CrouchPath = TEXT("/Game/Input/IA_Crouch.IA_Crouch");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* BowAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrappleAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");
	static const TCHAR* PuttyAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Putty.DA_Arrow_Putty");
	static const TCHAR* BolaAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Bola.DA_Arrow_Bola");

	static const FName BuildingTag(TEXT("CityBuilding"));
	static const FName VaultTag(TEXT("CityTestVault"));
	static const FName MantleTag(TEXT("CityTestMantle"));
	static const FName RoofPairTag(TEXT("RoofPair"));
	static const FName ArcherPairTag(TEXT("ArcherPair"));
	static const FName StreetGroupTag(TEXT("StreetGroup"));

	/** Speed under which held input counts as the capsule being stopped, cm/s. */
	static constexpr float BlockedSpeed = 100.f;
	/** Stopped this long with input held is one "touched a wall" event, s. */
	static constexpr float BlockedSeconds = 0.5f;

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Lap") / FileName);
	}

	static void Shot(FAutomationTestBase* Test, const FString& FileName)
	{
		const FString FullPath = ShotPath(FileName);
		HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	}

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

	/** One frame of a press: Started this frame, Completed the next. */
	static void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		if (const UInputAction* A = Action(Path))
		{
			if (Input)
			{
				Input->InjectInputForAction(A, FInputActionValue(true), {}, {});
			}
		}
	}

	/** Holds (every frame until released) or releases a button action. */
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

	/** The IA_Move value that walks along WorldDirection with the camera at CameraYaw. */
	static FVector2D MoveTowards(const FVector& WorldDirection, float CameraYaw)
	{
		const float Angle = FMath::DegreesToRadians(WorldDirection.Rotation().Yaw - CameraYaw);
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
	}

	static AActor* FindTagged(UWorld* World, FName Tag)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static ACityLedgeSpawner* FindSpawner(UWorld* World)
	{
		TActorIterator<ACityLedgeSpawner> It(World);
		return It ? *It : nullptr;
	}

	static bool FindGround(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& OutGround, AActor** OutActor = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LapGround), false, Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, -5000.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		if (OutActor)
		{
			*OutActor = Hit.GetActor();
		}
		return true;
	}

	/** The CityBuilding right under Point (within 300 cm), or null. */
	static AActor* BuildingUnder(UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LapRoof), false, Ignore);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 50.f), Point - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params)
			&& Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag))
		{
			return Hit.GetActor();
		}
		return nullptr;
	}

	static FVector Feet(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/** Points the camera from where the lens is now at Target (the arm follows; call every frame). */
	static void AimAt(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Target - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
	}

	/**
	 * A thug in front of her view within 240 cm whose telegraph a tap of V parries, seen for at least
	 * ReactionSeconds (a swing's or bash's wind-up; a gunner's raised pistol). Null if none.
	 */
	static AThugCharacter* ParryChance(UWorld* World, const AHawkeyeCharacter* Kate, const APlayerController* PC,
		float ReactionSeconds = 0.2f)
	{
		const FVector Forward = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const EHawkeyeParryKind Kind = UHawkeyeMeleeRules::ClassifyParry(*It);
			if (Kind == EHawkeyeParryKind::None || FMath::Abs(It->GetActorLocation().Z - Kate->GetActorLocation().Z) > 150.f
				|| !UHawkeyeMeleeRules::IsInFrontWithin(Kate->GetActorLocation(), Forward, It->GetActorLocation(), 240.f, 65.f))
			{
				continue;
			}
			const UMeleeComponent* Melee = It->GetMeleeComponent();
			const bool bSeen = Kind == EHawkeyeParryKind::Burst
				|| (Melee && Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining() >= ReactionSeconds);
			if (bSeen)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Kate can parry this frame: not in her own wind-up, a dodge, a stagger, a draw or a takedown. */
	static bool CanParryNow(const AHawkeyeCharacter* Kate)
	{
		return Kate->GetMeleeComponent() && !Kate->GetMeleeComponent()->IsWindingUp() && !Kate->IsDodging()
			&& !Kate->IsStaggered() && !Kate->IsDrawingBow() && !Kate->IsLockedOutByTakedown();
	}

	/** A staggered or knocked-down thug is within the finisher's reach. */
	static bool CanFinish(const AHawkeyeCharacter* Kate)
	{
		return Kate->GetFinisherComponent() && Kate->GetFinisherComponent()->FindTarget() != nullptr;
	}

	static void EnsureQuiver(FAutomationTestBase* Test, UInventoryComponent* Inventory)
	{
		if (Inventory->HasBow() && Inventory->GetArrowCount(1) > 0)
		{
			return;
		}
		Test->AddWarning(TEXT("Kate had no bow or arrows on the district; DA_CH01_Rooftops should grant them."));
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, StandardAssetPath);
		Standard.Count = 30;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, GrappleAssetPath);
		Grapple.Count = 6;
		Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, BowAssetPath), { Standard, Grapple });
	}

	static void SetThugsThinking(UWorld* World, FName Tag, bool bEnabled)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (Tag.IsNone() || It->ActorHasTag(Tag))
			{
				if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
				{
					Brain->SetThinkingEnabled(bEnabled);
				}
			}
		}
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	/** One move of the lap: how many presses or approaches it took and whether it happened. */
	struct FMoveRecord
	{
		FString Name;
		int32 Attempts = 0;
		bool bSucceeded = false;
		FString Note;
	};

	/** Frame time and "stopped against something" bookkeeping shared by both runners. */
	struct FMeter
	{
		double FrameSeconds = 0.0;
		double WorstFrame = 0.0;
		int32 WorstFrameIndex = 0;
		int32 FramesOver33 = 0;
		int32 Frames = 0;
		int32 BlockedEvents = 0;
		float BlockedFor = 0.f;
		bool bBlockedLatched = false;
		TArray<FString> BlockedWhere;

		/** Frames that paid for a screenshot read-back: the test's cost, not the game's. */
		int32 CaptureFrames = 0;
		int32 FramesOver100 = 0;

		void Frame(float DeltaSeconds)
		{
			if (HawkeyeShots::DidCaptureLastFrame())
			{
				++CaptureFrames;
				return;
			}
			FramesOver100 += DeltaSeconds >= 0.1f ? 1 : 0;
			FrameSeconds += DeltaSeconds;
			if (DeltaSeconds > WorstFrame)
			{
				WorstFrame = DeltaSeconds;
				WorstFrameIndex = Frames;
			}
			FramesOver33 += DeltaSeconds > 0.0334f ? 1 : 0;
			++Frames;
		}

		/** bHeld: input is being pushed and nothing but walls should stop her. */
		/** Held input, under BlockedSpeed, and the capsule touching something 20 cm along Direction. */
		void Blocked(const AHawkeyeCharacter* Kate, bool bHeld, float DeltaSeconds, const FString& Leg, const FVector& Direction)
		{
			bool bStopped = bHeld && Kate->GetCharacterMovement()->IsMovingOnGround()
				&& Kate->GetVelocity().Size2D() < BlockedSpeed && !Direction.IsNearlyZero();
			if (bStopped)
			{
				const UCapsuleComponent* Capsule = Kate->GetCapsuleComponent();
				FCollisionQueryParams Params(SCENE_QUERY_STAT(LapBlocked), false, Kate);
				FHitResult Hit;
				const FVector From = Kate->GetActorLocation() + FVector(0.f, 0.f, 10.f);
				bStopped = Kate->GetWorld()->SweepSingleByChannel(Hit, From, From + Direction.GetSafeNormal2D() * 20.f, FQuat::Identity,
					ECC_Pawn, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 12.f),
					Params);
			}
			if (!bStopped)
			{
				BlockedFor = 0.f;
				bBlockedLatched = false;
				return;
			}
			BlockedFor += DeltaSeconds;
			if (BlockedFor >= BlockedSeconds && !bBlockedLatched)
			{
				bBlockedLatched = true;
				++BlockedEvents;
				BlockedWhere.Add(FString::Printf(TEXT("%s at %s"), *Leg, *Kate->GetActorLocation().ToCompactString()));
			}
		}

		double AverageMs() const { return Frames > 0 ? FrameSeconds / Frames * 1000.0 : 0.0; }
	};
}

// --- The lap ------------------------------------------------------------------------------------

class FHawkeyeLapRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeLapRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	enum class EStep : uint8
	{
		WaitReady,
		StreetOut,
		Vault,
		Mantle,
		PastMantle,
		StreetToAnchor,
		GrappleFromStreet,
		ZipToRoof,
		RoofRun,
		GrappleAcross,
		ZipAcross,
		GrappleOn,
		ZipOn,
		Descent,
		Return,
		Done,
	};

	using FMoveRecord = HawkeyeLap::FMoveRecord;

	FAutomationTestBase* Test;
	EStep Step = EStep::WaitReady;
	double StepStart = -1.0;
	double LapStart = -1.0;
	double LapEnd = -1.0;
	double WaitStart = -1.0;
	HawkeyeLap::FMeter Meter;
	bool bMoveHeld = false;
	FVector SteerDirection = FVector::ZeroVector;
	bool bExcludeThisFrame = false;
	int32 ExcludedFrames = 0;
	bool bSprintHeld = false;
	bool bMoveInjecting = false;
	bool bRoofShotTaken = false;
	bool bRoofShotPending = false;
	bool bDescentScripted = false;
	FString DescentNote;
	FString FailReason;

	// Street frame.
	FVector Start = FVector::ZeroVector;
	float StartYaw = 0.f;
	FVector AlongToVault = FVector::ForwardVector;
	FVector Lateral = FVector::ZeroVector;
	TWeakObjectPtr<AActor> VaultBlock;
	TWeakObjectPtr<AActor> MantleBlock;
	TArray<FVector> Waypoints;
	int32 WaypointIndex = 0;
	float StreetMetres = 0.f;
	FVector LastStreetPoint = FVector::ZeroVector;

	// Moves.
	FMoveRecord VaultMove{ TEXT("vault") };
	FMoveRecord MantleMove{ TEXT("mantle") };
	FMoveRecord Grapple1{ TEXT("grapple_street_to_roof") };
	FMoveRecord Grapple2{ TEXT("grapple_roof_to_roof") };
	FMoveRecord ChainMove{ TEXT("chain") };
	FMoveRecord Grapple3{ TEXT("grapple_third_roof") };
	FMoveRecord DescentMove{ TEXT("descent") };
	bool bObstacleSeen = false;
	bool bMoveStarted = false;
	int32 ApproachFailures = 0;

	// Grapple bookkeeping.
	TArray<TWeakObjectPtr<AGrappleAnchor>> Candidates;
	int32 CandidateIndex = 0;
	double AimStart = -1.0;
	double PressTime = -1.0;
	TWeakObjectPtr<AGrappleAnchor> ZipTarget;
	TWeakObjectPtr<AActor> Roof1;
	TWeakObjectPtr<AActor> Roof2;
	TArray<FString> RoofsVisited;
	int32 ZipsWithoutLanding = 0;
	int32 LongestChain = 0;
	TWeakObjectPtr<AGrappleAnchor> ChainTarget;
	bool bChainPressed = false;
	bool bChainedThisZip = false;
	int32 ChainPressesThisZip = 0;
	double LastChainPress = -10.0;
	bool bChainSearched = false;
	// How each chain took: redirected in the air, or started from the roof moments after landing
	// (the chain arrow still in flight when the short line ended).
	int32 ChainMidAir = 0;
	int32 ChainTouchAndGo = 0;
	float TouchSeconds = 0.f;
	float LongestTouch = 0.f;
	TArray<FString> ChainNotes;
	// Every leg of every chain, and any time on a roof between two zips of one.
	TArray<FString> ChainLegs;
	int32 ChainTouchDowns = 0;
	double ChainShotAt = -1.0;
	// Roof to roof without the street: from the first roof-to-roof press to the last landing before the descent.
	double RoofChainStart = -1.0;
	double RoofChainEnd = -1.0;
	TArray<const AActor*> RoofChainLandings;
	bool bChainShotTaken = false;
	FVector RunTarget = FVector::ZeroVector;

	// Descent.
	FVector ParapetPoint = FVector::ZeroVector;
	FVector ParapetDir = FVector::ForwardVector;
	FVector StreetBelow = FVector::ZeroVector;
	int32 DescentPhase = 0;
	double DescentStart = -1.0;
	double DescentEnd = -1.0;
	FVector DescentStand = FVector::ZeroVector;
	FVector DescentOut = FVector::ForwardVector;
	FString DescentRoute;
	int32 DescentHangs = 0;
	int32 DescentCatches = 0;
	int32 DescentLandings = 0;
	int32 DescentRetries = 0;
	float DescentLastDrop = 0.f;
	double PhaseStart = -1.0;
	bool bPushing = false;

	// Fire escapes: the top landing of each roof that has one.
	TMap<const AActor*, TWeakObjectPtr<AFireEscapeLanding>> TopLandingByRoof;
	void IndexFireEscapes(UWorld* World);
	TArray<TWeakObjectPtr<AActor>> ChainedRoofs;

	double Now(const UWorld* World) const { return World->GetTimeSeconds(); }
	void Enter(EStep NewStep, const UWorld* World)
	{
		Step = NewStep;
		StepStart = Now(World);
		Test->AddInfo(FString::Printf(TEXT("Lap %.2f s: step %d"), LapStart >= 0.0 ? Now(World) - LapStart : 0.0,
			static_cast<int32>(NewStep)));
	}

	/** Pushes IA_Move towards Target (and sprint), turning the camera behind the direction. True on arrival. */
	bool Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive, float DeltaSeconds);
	void StopMoving(APlayerController* PC);
	void SetSprint(APlayerController* PC, bool bSprint);

	/** Anchors from FilterFn, nearest first. */
	void GatherAnchors(UWorld* World, const AHawkeyeCharacter* Kate, TFunctionRef<bool(const AGrappleAnchor*)> FilterFn,
		bool bFarFirst = false);

	/**
	 * Aims at the current candidate until the grapple targets it, then presses Q. Returns 1 when
	 * an arrow is out, -1 when every candidate failed, 0 while working.
	 */
	int32 AimAndFire(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, FMoveRecord& Move);

	/** Waits out a zip. Returns 1 landed, -1 cancelled or fell, 0 still going. */
	int32 WatchZip(UWorld* World, AHawkeyeCharacter* Kate);

	/** Watches an approach to a test block for the move starting. Returns 1 done, -1 gave up, 0 going. */
	int32 WatchBlock(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, AActor* Block, FMoveRecord& Move, float DeltaSeconds);

	bool FindParapet(UWorld* World, const AHawkeyeCharacter* Kate);

	/** The grapple's own offline rule (launch point, hop, start and anchor supports) from From to Anchor. */
	bool ZipClear(UWorld* World, const AHawkeyeCharacter* Kate, const FVector& From, const AGrappleAnchor* Anchor,
		bool bFromGround = true) const;

	/**
	 * The camera with her capsule centre at From, turned to put Anchor in the middle of the screen, would mark it:
	 * in the cone, on the screen and in sight, as the grapple checks (Hawkeye.Grapple.Audit's lens). A zip that is
	 * clear but hidden behind her own parapet or a neighbour's wall was planned and never targeted.
	 */
	bool CameraSees(const AHawkeyeCharacter* Kate, const FVector& From, const AGrappleAnchor* Anchor) const
	{
		const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		const FVector Marker = Anchor->GetMarkerLocation();
		FVector Lens, Forward;
		HawkeyeGrappleView::PredictLens(Kate, From, Marker, Lens, Forward);
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Forward, (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
		return Angle <= Grapple->ConeDegrees - 3.f && Grapple->IsOnScreen(Lens, Forward, Marker) && Grapple->HasLineOfSight(Anchor, Lens);
	}

	/**
	 * Picks a spot on the roof Kate stands on and an anchor on another building (not in Exclude)
	 * whose zip from that spot is clear, preferring spots across the roof from her. Sets RunTarget
	 * and Candidates. False with a reason when there is none.
	 */
	bool PlanRoofGrapple(UWorld* World, const AHawkeyeCharacter* Kate, const TArray<const AActor*>& Exclude, FString& OutNote,
		bool bPreferEscape = false);

	/** The search behind PlanRoofGrapple, for any roof: the best spot and anchor with a clear zip. */
	bool FindRoofGrapple(UWorld* World, const AHawkeyeCharacter* Kate, const AActor* Roof, float RoofZ, const FVector& Here,
		const TArray<const AActor*>& Exclude, FVector& OutSpot, AGrappleAnchor*& OutAnchor, int32& OutSpots, int32& OutAnchors,
		int32& OutTried, bool bRequireOnward = false, bool bRequireEscape = false) const;

	/** Whether the roof under Anchor's landing point has any clear grapple to another roof. Cached. */
	bool RoofLeadsOn(UWorld* World, const AHawkeyeCharacter* Kate, const AGrappleAnchor* Anchor) const;
	mutable TMap<const AActor*, bool> OnwardCache;

	/** After the lap: how many anchored roofs near the start have any clear roof-to-roof zip. */
	void SurveyRoofGrapples(UWorld* World, const AHawkeyeCharacter* Kate);
	int32 SurveyRoofs = 0;
	int32 SurveyRoofsWithExit = 0;

	EStep AfterRun = EStep::GrappleAcross;
	FString StreetGrappleNote;
	TWeakObjectPtr<AGrappleAnchor> StreetTarget;

	/** Picks the rooftop anchor to go for from the street: the nearest whose roof has a way on. */
	void ChooseStreetAnchor(UWorld* World, AHawkeyeCharacter* Kate);

	/** Adds every other rooftop anchor in grapple range, nearest first, after the chosen one. */
	void AppendStreetFallbacks(UWorld* World, const AHawkeyeCharacter* Kate);
	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);
	FString MoveJson(const FMoveRecord& Move) const;
};

void FHawkeyeLapRunner::SetSprint(APlayerController* PC, bool bSprint)
{
	if (bSprint != bSprintHeld)
	{
		HawkeyeLap::Hold(PC, HawkeyeLap::SprintPath, bSprint);
		bSprintHeld = bSprint;
	}
}

void FHawkeyeLapRunner::StopMoving(APlayerController* PC)
{
	SetSprint(PC, false);
	if (bMoveInjecting)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeLap::InputOf(PC))
		{
			Input->StopContinuousInputInjectionForAction(HawkeyeLap::Action(HawkeyeLap::MovePath));
		}
		bMoveInjecting = false;
	}
}

bool FHawkeyeLapRunner::Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive,
	float DeltaSeconds)
{
	const FVector To = (Target - Kate->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	if (To.Size() <= Arrive)
	{
		return true;
	}
	const FVector Direction = To.GetSafeNormal();
	SteerDirection = Direction;
	// The camera swings round behind the run at up to 540 degrees a second, as a mouse would.
	const FRotator Control = PC->GetControlRotation();
	const float WantYaw = Direction.Rotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Control.Yaw, WantYaw);
	const float MaxTurn = 540.f * DeltaSeconds;
	const float NewYaw = Control.Yaw + FMath::Clamp(DeltaYaw, -MaxTurn, MaxTurn);
	PC->SetControlRotation(FRotator(-10.f, NewYaw, 0.f));

	const FVector2D Value = HawkeyeLap::MoveTowards(Direction, NewYaw);
	if (UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeLap::InputOf(PC))
	{
		const UInputAction* Move = HawkeyeLap::Action(HawkeyeLap::MovePath);
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
	bMoveHeld = true;
	return false;
}

void FHawkeyeLapRunner::GatherAnchors(UWorld* World, const AHawkeyeCharacter* Kate, TFunctionRef<bool(const AGrappleAnchor*)> FilterFn,
	bool bFarFirst)
{
	Candidates.Reset();
	CandidateIndex = 0;
	AimStart = -1.0;
	PressTime = -1.0;
	TArray<TPair<float, AGrappleAnchor*>> Found;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		if (FilterFn(*It))
		{
			Found.Emplace(FVector::Dist(Kate->GetActorLocation(), It->GetMarkerLocation()), *It);
		}
	}
	Found.Sort([bFarFirst](const TPair<float, AGrappleAnchor*>& A, const TPair<float, AGrappleAnchor*>& B)
	{
		return bFarFirst ? A.Key > B.Key : A.Key < B.Key;
	});
	for (const TPair<float, AGrappleAnchor*>& Entry : Found)
	{
		Candidates.Add(Entry.Value);
	}
}

int32 FHawkeyeLapRunner::AimAndFire(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, FMoveRecord& Move)
{
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	if (PressTime >= 0.0)
	{
		if (Grapple->IsArrowInFlight() || Grapple->IsZipping())
		{
			Move.bSucceeded = true;
			ZipTarget = Candidates.IsValidIndex(CandidateIndex) ? Candidates[CandidateIndex] : nullptr;
			return 1;
		}
		if (Now(World) - PressTime < 0.4)
		{
			return 0;
		}
		// The press did nothing.
		PressTime = -1.0;
		if (Move.Attempts >= 3)
		{
			Move.Note = TEXT("three presses fired nothing");
			return -1;
		}
	}

	while (Candidates.IsValidIndex(CandidateIndex) && !Candidates[CandidateIndex].IsValid())
	{
		++CandidateIndex;
	}
	if (!Candidates.IsValidIndex(CandidateIndex) || CandidateIndex >= 12)
	{
		Move.Note = FString::Printf(TEXT("none of %d candidate anchors could be targeted"), Candidates.Num());
		return -1;
	}
	AGrappleAnchor* Anchor = Candidates[CandidateIndex].Get();
	if (AimStart < 0.0)
	{
		AimStart = Now(World);
	}
	HawkeyeLap::AimAt(PC, Kate, Anchor->GetMarkerLocation());
	Grapple->RefreshTarget();
	if (Grapple->GetTargetAnchor() == Anchor && Grapple->CanChain() && !Grapple->IsArrowInFlight())
	{
		++Move.Attempts;
		PressTime = Now(World);
		HawkeyeLap::Tap(PC, HawkeyeLap::GrapplePath);
		return 0;
	}
	if (Now(World) - AimStart > 0.8)
	{
		// Never became the target (out of the cone, blocked from the lens): the next one.
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FVector Marker = Anchor->GetMarkerLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LapSight), false, Kate);
		Params.AddIgnoredActor(Anchor);
		const bool bSightHit = World->LineTraceSingleByChannel(Hit, Lens, Marker, ECC_Visibility, Params);
		Test->AddInfo(FString::Printf(TEXT("Lap: %s never targeted: %.0f cm from Kate (range %.0f), %.1f deg off the lens, sight %s"),
			*Anchor->GetName(), FVector::Dist(Kate->GetActorLocation(), Marker), Grapple->Range,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Kate->GetFollowCamera()->GetForwardVector(),
				(Marker - Lens).GetSafeNormal()), -1.f, 1.f))),
			bSightHit ? *FString::Printf(TEXT("blocked by %s %.0f cm short"), *GetNameSafe(Hit.GetActor()), FVector::Dist(Lens, Marker) - Hit.Distance)
				: TEXT("clear")));
		++CandidateIndex;
		AimStart = -1.0;
	}
	return 0;
}

int32 FHawkeyeLapRunner::WatchZip(UWorld* World, AHawkeyeCharacter* Kate)
{
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
	{
		return 0;
	}
	if (!Kate->GetCharacterMovement()->IsMovingOnGround())
	{
		return Now(World) - StepStart > 6.0 ? -1 : 0;
	}
	const AGrappleAnchor* Target = ZipTarget.Get();
	if (Target && FVector::Dist2D(Kate->GetActorLocation(), Target->GetLandingLocation()) > 400.f)
	{
		return -1;
	}
	return 1;
}

int32 FHawkeyeLapRunner::WatchBlock(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, AActor* Block, FMoveRecord& Move,
	float DeltaSeconds)
{
	const UParkourComponent* Parkour = Kate->GetParkourComponent();
	const FBox Box = Block->GetComponentsBoundingBox();
	const FVector Centre = Box.GetCenter();
	const float HalfDepth = FMath::Abs(Box.GetExtent().X * AlongToVault.X) + FMath::Abs(Box.GetExtent().Y * AlongToVault.Y);
	const float Along = FVector::DotProduct(Kate->GetActorLocation() - Centre, AlongToVault);
	if (Parkour->IsBusy())
	{
		if (!bMoveStarted)
		{
			bMoveStarted = true;
			++Move.Attempts;
			Move.bSucceeded = true;
			Move.Note = FString::Printf(TEXT("%s by %s, obstacle %.0f cm at %.0f cm"),
				*UEnum::GetValueAsString(Parkour->GetLastMove()), *UEnum::GetValueAsString(Parkour->GetLastRoute()),
				Parkour->GetLastObstacle().Height, Parkour->GetLastObstacle().Distance);
			Test->AddInfo(FString::Printf(TEXT("Lap: %s on attempt %d: %s"), *Move.Name, Move.Attempts, *Move.Note));
		}
		return 0;
	}
	if (bMoveStarted)
	{
		// Finished once she is walking again on the far side.
		return Kate->GetCharacterMovement()->IsMovingOnGround() && Along < -(HalfDepth + 40.f) ? 1 : 0;
	}
	// Stuck against the block without a move: a failed attempt. Back off and run at it again.
	if (FMath::Abs(Along) < HalfDepth + 200.f && Kate->GetVelocity().Size2D() < HawkeyeLap::BlockedSpeed && Now(World) - StepStart > 0.5)
	{
		++Move.Attempts;
		++ApproachFailures;
		Test->AddWarning(FString::Printf(TEXT("Lap: %s attempt %d did not trigger (Kate %s, %s)"), *Move.Name, Move.Attempts,
			*Kate->GetActorLocation().ToCompactString(), *Kate->GetMovementDebugText()));
		if (Move.Attempts >= 3)
		{
			Move.Note = TEXT("never triggered; put past it");
			const FVector Past = Centre - AlongToVault * (HalfDepth + 150.f);
			FVector Ground;
			if (HawkeyeLap::FindGround(World, Past, Centre.Z + 500.f, Kate, Ground))
			{
				Kate->TeleportTo(Ground + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
					Kate->GetActorRotation());
			}
			return -1;
		}
		const FVector Back = Centre + AlongToVault * 700.f;
		FVector Ground;
		if (HawkeyeLap::FindGround(World, Back, Centre.Z + 500.f, Kate, Ground))
		{
			Kate->TeleportTo(Ground + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
				(-AlongToVault).Rotation());
		}
		StepStart = Now(World);
	}
	return 0;
}

bool FHawkeyeLapRunner::FindParapet(UWorld* World, const AHawkeyeCharacter* Kate)
{
	const FVector FeetAt = HawkeyeLap::Feet(Kate);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LapParapet), false, Kate);
	float Best = BIG_NUMBER;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		const FVector Dir = FRotator(0.f, Index * 15.f, 0.f).Vector();
		const FVector From = FeetAt + FVector(0.f, 0.f, 60.f);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, From + Dir * 3000.f, ECC_Visibility, Params) || Hit.Distance >= Best)
		{
			continue;
		}
		// Beyond it: a street, not another roof.
		const FVector Beyond = Hit.ImpactPoint + Dir * 150.f;
		FVector Ground;
		AActor* Under = nullptr;
		if (!HawkeyeLap::FindGround(World, Beyond, FeetAt.Z + 200.f, Kate, Ground, &Under) || Ground.Z > 300.f
			|| (Under && Under->Tags.Contains(HawkeyeLap::BuildingTag)))
		{
			continue;
		}
		Best = Hit.Distance;
		ParapetPoint = Hit.ImpactPoint;
		ParapetDir = Dir;
		StreetBelow = Ground;
	}
	return Best < BIG_NUMBER;
}

bool FHawkeyeLapRunner::ZipClear(UWorld* World, const AHawkeyeCharacter* Kate, const FVector& From, const AGrappleAnchor* Anchor,
	bool bFromGround) const
{
	(void)World;
	return Kate->GetGrappleComponent()->IsZipClear(From, Anchor, bFromGround);
}

void FHawkeyeLapRunner::IndexFireEscapes(UWorld* World)
{
	TopLandingByRoof.Reset();
	TMap<FName, const AActor*> RoofByOsm;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->Tags.Contains(HawkeyeLap::BuildingTag))
		{
			continue;
		}
		for (const FName& Tag : It->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("osm:")))
			{
				RoofByOsm.Add(Tag, *It);
			}
		}
	}
	for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
	{
		const FName* Osm = It->Tags.FindByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("osm:")); });
		const AActor* const* Roof = Osm ? RoofByOsm.Find(*Osm) : nullptr;
		if (!Roof)
		{
			continue;
		}
		TWeakObjectPtr<AFireEscapeLanding>& Top = TopLandingByRoof.FindOrAdd(*Roof);
		if (!Top.IsValid() || It->GetRecord().Floor > Top->GetRecord().Floor)
		{
			Top = *It;
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Lap: %d roofs have a fire escape"), TopLandingByRoof.Num()));
}

bool FHawkeyeLapRunner::RoofLeadsOn(UWorld* World, const AHawkeyeCharacter* Kate, const AGrappleAnchor* Anchor) const
{
	const AActor* Roof = HawkeyeLap::BuildingUnder(World, Anchor->GetLandingLocation(), Kate);
	if (!Roof)
	{
		return false;
	}
	if (const bool* Known = OnwardCache.Find(Roof))
	{
		return *Known;
	}
	FVector Spot;
	AGrappleAnchor* Exit = nullptr;
	int32 Spots = 0, Anchors = 0, Tried = 0;
	const FVector Landing = Anchor->GetLandingLocation();
	const bool bLeads = FindRoofGrapple(World, Kate, Roof, Landing.Z,
		Landing + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), {}, Spot, Exit, Spots, Anchors, Tried);
	OnwardCache.Add(Roof, bLeads);
	return bLeads;
}

bool FHawkeyeLapRunner::FindRoofGrapple(UWorld* World, const AHawkeyeCharacter* Kate, const AActor* Roof, float RoofZ, const FVector& Here,
	const TArray<const AActor*>& Exclude, FVector& OutSpot, AGrappleAnchor*& OutAnchor, int32& OutSpots, int32& OutAnchors,
	int32& OutTried, bool bRequireOnward, bool bRequireEscape) const
{
	const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Centre, Extent;
	Roof->GetActorBounds(false, Centre, Extent);

	// Anchors on other buildings that could be in range from somewhere on this roof.
	TArray<AGrappleAnchor*> Anchors;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const FVector Marker = It->GetMarkerLocation();
		if (FVector::Dist2D(Marker, Centre) > Grapple->Range + Extent.Size2D() || Marker.Z < RoofZ - 1500.f)
		{
			continue;
		}
		const AActor* Under = HawkeyeLap::BuildingUnder(World, It->GetLandingLocation(), Kate);
		if (Under && Under != Roof && !Exclude.Contains(Under) && (!bRequireEscape || TopLandingByRoof.Contains(Under))
			&& (!bRequireOnward || RoofLeadsOn(World, Kate, *It)))
		{
			Anchors.Add(*It);
		}
	}
	// Spots on the roof, every 3 m, where the capsule stands with 20 cm to spare.
	TArray<FVector> Spots;
	for (float X = Centre.X - Extent.X; X <= Centre.X + Extent.X; X += 300.f)
	{
		for (float Y = Centre.Y - Extent.Y; Y <= Centre.Y + Extent.Y; Y += 300.f)
		{
			FVector Ground;
			AActor* Under = nullptr;
			if (HawkeyeLap::FindGround(World, FVector(X, Y, 0.f), RoofZ + 250.f, Kate, Ground, &Under) && Under == Roof
				&& FMath::Abs(Ground.Z - RoofZ) < 30.f)
			{
				const FVector Stand = Ground + FVector(0.f, 0.f, HalfHeight + 2.f);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(LapSpot), false, Kate);
				if (!World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Kate->GetCapsuleComponent()->GetScaledCapsuleRadius() + 20.f, HalfHeight - 5.f), Params))
				{
					Spots.Add(Stand);
				}
			}
		}
	}
	Spots.Add(Here);

	float BestScore = -BIG_NUMBER;
	OutTried = 0;
	OutAnchor = nullptr;
	for (const FVector& Spot : Spots)
	{
		for (AGrappleAnchor* Anchor : Anchors)
		{
			const float Distance = FVector::Dist(Spot, Anchor->GetMarkerLocation());
			if (Distance < Grapple->MinRange + 200.f || Distance > Grapple->Range - 150.f)
			{
				continue;
			}
			// Across the roof (up to 15 m of run), then the shorter zip.
			const float Score = FMath::Min(FVector::Dist2D(Spot, Here), 1500.f) - 0.2f * Distance;
			if (Score <= BestScore || OutTried > 4000)
			{
				continue;
			}
			++OutTried;
			if (ZipClear(World, Kate, Spot, Anchor) && CameraSees(Kate, Spot, Anchor))
			{
				BestScore = Score;
				OutSpot = Spot;
				OutAnchor = Anchor;
			}
		}
	}
	OutSpots = Spots.Num();
	OutAnchors = Anchors.Num();
	return OutAnchor != nullptr;
}

bool FHawkeyeLapRunner::PlanRoofGrapple(UWorld* World, const AHawkeyeCharacter* Kate, const TArray<const AActor*>& Exclude, FString& OutNote,
	bool bPreferEscape)
{
	const AActor* Roof = Exclude.Num() ? Exclude.Last() : nullptr;
	if (!Roof)
	{
		OutNote = TEXT("not standing on a building");
		return false;
	}
	FVector Spot;
	AGrappleAnchor* Anchor = nullptr;
	int32 Spots = 0, Anchors = 0, Tried = 0;
	// Prefer a roof that leads on again (or, for the last hop, one with a fire escape to come
	// down); take a dead end only when nothing else is clear.
	bool bFound = FindRoofGrapple(World, Kate, Roof, HawkeyeLap::Feet(Kate).Z, Kate->GetActorLocation(), Exclude, Spot, Anchor,
		Spots, Anchors, Tried, /*bRequireOnward=*/!bPreferEscape, /*bRequireEscape=*/bPreferEscape);
	if (!bFound)
	{
		bFound = FindRoofGrapple(World, Kate, Roof, HawkeyeLap::Feet(Kate).Z, Kate->GetActorLocation(), Exclude, Spot, Anchor,
			Spots, Anchors, Tried);
	}
	Candidates.Reset();
	CandidateIndex = 0;
	AimStart = -1.0;
	PressTime = -1.0;
	if (!bFound)
	{
		OutNote = FString::Printf(TEXT("no clear zip from any of %d spots on %s to %d anchors on other buildings (%d sweeps)"),
			Spots, *Roof->GetName(), Anchors, Tried);
		return false;
	}
	RunTarget = Spot;
	Candidates.Add(Anchor);
	Test->AddInfo(FString::Printf(TEXT("Lap: plan %s from %s (%.0f m across the roof), %d spots x %d anchors, %d sweeps"),
		*GetNameSafe(Anchor), *RunTarget.ToCompactString(), FVector::Dist2D(RunTarget, Kate->GetActorLocation()) / 100.f,
		Spots, Anchors, Tried));
	return true;
}

void FHawkeyeLapRunner::SurveyRoofGrapples(UWorld* World, const AHawkeyeCharacter* Kate)
{
	// Every anchored roof within SurveyRadius of the start: is there any clear roof-to-roof zip?
	static constexpr float SurveyRadius = 15000.f;
	TSet<AActor*> Roofs;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), Start) <= SurveyRadius)
		{
			if (AActor* Under = HawkeyeLap::BuildingUnder(World, It->GetLandingLocation(), Kate))
			{
				Roofs.Add(Under);
			}
		}
	}
	for (AActor* Roof : Roofs)
	{
		FVector Centre, Extent;
		Roof->GetActorBounds(false, Centre, Extent);
		// The roof surface: the anchors' landing points sit on it.
		float RoofZ = Centre.Z + Extent.Z;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			if (HawkeyeLap::BuildingUnder(World, It->GetLandingLocation(), Kate) == Roof)
			{
				RoofZ = It->GetLandingLocation().Z;
				break;
			}
		}
		FVector Spot;
		AGrappleAnchor* Anchor = nullptr;
		int32 Spots = 0, Anchors = 0, Tried = 0;
		++SurveyRoofs;
		if (FindRoofGrapple(World, Kate, Roof, RoofZ, FVector(Centre.X, Centre.Y, RoofZ + 90.f), {}, Spot, Anchor, Spots, Anchors, Tried))
		{
			++SurveyRoofsWithExit;
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Survey: %d of %d anchored roofs within %.0f m of the start have a clear grapple to another roof"),
		SurveyRoofsWithExit, SurveyRoofs, SurveyRadius / 100.f));
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] survey: %d of %d anchored roofs within %.0f m have a clear roof-to-roof zip"),
		SurveyRoofsWithExit, SurveyRoofs, SurveyRadius / 100.f);
	if (SurveyRoofs > 0 && SurveyRoofsWithExit * 63 < 60 * SurveyRoofs)
	{
		Test->AddWarning(FString::Printf(TEXT("Only %d of %d anchored roofs have a clear roof-to-roof zip; the target is 60 of 63."),
			SurveyRoofsWithExit, SurveyRoofs));
	}
}

void FHawkeyeLapRunner::ChooseStreetAnchor(UWorld* World, AHawkeyeCharacter* Kate)
{
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector From = Kate->GetActorLocation();
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TArray<TPair<float, AGrappleAnchor*>> Rooftop;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(From, It->GetMarkerLocation());
		if (It->GetMarkerLocation().Z - From.Z > 500.f && Distance < 5000.f && Distance > Grapple->MinRange + 50.f)
		{
			Rooftop.Emplace(Distance, *It);
		}
	}
	Rooftop.Sort([](const TPair<float, AGrappleAnchor*>& A, const TPair<float, AGrappleAnchor*>& B) { return A.Key < B.Key; });

	TMap<const AActor*, bool> RoofHasExit;
	AGrappleAnchor* Nearest = Rooftop.Num() ? Rooftop[0].Value : nullptr;
	AGrappleAnchor* Chosen = nullptr;
	for (const TPair<float, AGrappleAnchor*>& Entry : Rooftop)
	{
		const AActor* Roof = HawkeyeLap::BuildingUnder(World, Entry.Value->GetLandingLocation(), Kate);
		if (!Roof)
		{
			continue;
		}
		if (!RoofHasExit.Contains(Roof))
		{
			FVector Spot;
			AGrappleAnchor* Exit = nullptr;
			int32 Spots = 0, Anchors = 0, Tried = 0;
			const FVector Landing = Entry.Value->GetLandingLocation();
			RoofHasExit.Add(Roof, FindRoofGrapple(World, Kate, Roof, Landing.Z, Landing + FVector(0.f, 0.f, HalfHeight + 2.f), {},
				Spot, Exit, Spots, Anchors, Tried));
		}
		if (RoofHasExit[Roof])
		{
			Chosen = Entry.Value;
			break;
		}
		if (RoofHasExit.Num() > 12)
		{
			break;
		}
	}
	if (!Chosen)
	{
		Chosen = Nearest;
		StreetGrappleNote = TEXT("no rooftop anchor within 50 m leads on to another roof; took the nearest");
	}
	else if (Chosen != Nearest && Nearest)
	{
		StreetGrappleNote = FString::Printf(TEXT("the nearest rooftop anchor (%s, %.0f m) is on a roof with no clear grapple onward; went for %s at %.0f m"),
			*Nearest->GetName(), FVector::Dist(From, Nearest->GetMarkerLocation()) / 100.f, *Chosen->GetName(),
			FVector::Dist(From, Chosen->GetMarkerLocation()) / 100.f);
	}
	StreetTarget = Chosen;
	Test->AddInfo(TEXT("Lap: street grapple: ") + (StreetGrappleNote.IsEmpty() ? GetNameSafe(Chosen) : StreetGrappleNote));

	Candidates.Reset();
	CandidateIndex = 0;
	AimStart = -1.0;
	PressTime = -1.0;
	Waypoints.Reset();
	WaypointIndex = -1;
	if (!Chosen)
	{
		return;
	}
	Candidates.Add(Chosen);
	if (FVector::Dist(From, Chosen->GetMarkerLocation()) < Grapple->Range - 300.f)
	{
		return;
	}
	// Out of range or out of sight: a street spot 10 to 18 m out from its facade that sees the
	// marker from head height, then along the navmesh to it.
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav)
	{
		return;
	}
	const FVector Marker = Chosen->GetMarkerLocation();
	const FVector Outward = -Chosen->GetActorForwardVector().GetSafeNormal2D();
	bool bFound = false;
	FVector Spot = FVector::ZeroVector;
	for (const float Radius : { 1000.f, 1400.f, 1800.f })
	{
		for (const float Yaw : { 0.f, 15.f, -15.f, 30.f, -30.f, 45.f, -45.f, 60.f, -60.f })
		{
			const FVector Dir = FRotator(0.f, Yaw, 0.f).RotateVector(Outward);
			const FVector Guess = Chosen->GetActorLocation() + Dir * Radius;
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(FVector(Guess.X, Guess.Y, From.Z), Projected, FVector(200.f, 200.f, 300.f))
				|| FMath::Abs(Projected.Location.Z - (From.Z - HalfHeight)) > 300.f)
			{
				continue;
			}
			const FVector Eye = Projected.Location + FVector(0.f, 0.f, 170.f);
			if (FVector::Dist(Projected.Location + FVector(0.f, 0.f, HalfHeight), Marker) > Grapple->Range - 300.f)
			{
				continue;
			}
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(LapStreetSight), false, Kate);
			Params.AddIgnoredActor(Chosen);
			if (World->LineTraceSingleByChannel(Hit, Eye, Marker, ECC_Visibility, Params)
				&& Hit.Distance < FVector::Dist(Eye, Marker) - Grapple->SightTolerance)
			{
				continue;
			}
			Spot = Projected.Location;
			bFound = true;
			break;
		}
		if (bFound)
		{
			break;
		}
	}
	if (!bFound)
	{
		StreetGrappleNote += TEXT("; no street spot found that sees it");
		return;
	}
	if (const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, Spot))
	{
		Waypoints = Path->PathPoints;
	}
	if (Waypoints.Num() == 0)
	{
		Waypoints.Add(Spot);
	}
	WaypointIndex = 0;
}

void FHawkeyeLapRunner::AppendStreetFallbacks(UWorld* World, const AHawkeyeCharacter* Kate)
{
	const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const FVector From = Kate->GetActorLocation();
	TArray<TPair<float, AGrappleAnchor*>> More;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(From, It->GetMarkerLocation());
		if (It->GetMarkerLocation().Z - From.Z > 500.f && Distance > Grapple->MinRange + 50.f && Distance < Grapple->Range - 50.f
			&& !Candidates.Contains(*It))
		{
			More.Emplace(Distance, *It);
		}
	}
	More.Sort([](const TPair<float, AGrappleAnchor*>& A, const TPair<float, AGrappleAnchor*>& B) { return A.Key < B.Key; });
	for (const TPair<float, AGrappleAnchor*>& Entry : More)
	{
		Candidates.Add(Entry.Value);
	}
	CandidateIndex = 0;
	AimStart = -1.0;
	PressTime = -1.0;
}

FString FHawkeyeLapRunner::MoveJson(const FMoveRecord& Move) const
{
	return FString::Printf(TEXT("{\"move\": \"%s\", \"attempts\": %d, \"succeeded\": %s, \"note\": \"%s\"}"), *Move.Name,
		Move.Attempts, Move.bSucceeded ? TEXT("true") : TEXT("false"), *Move.Note.ReplaceCharWithEscapedChar());
}

void FHawkeyeLapRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	StopMoving(PC);
	LapEnd = Now(World);
	const double Seconds = LapEnd - LapStart;
	const TArray<const FMoveRecord*> Moves = { &VaultMove, &MantleMove, &Grapple1, &Grapple2, &ChainMove, &Grapple3, &DescentMove };
	int32 MaxAttempts = 0;
	bool bAllFirst = true;
	TArray<FString> MoveLines;
	for (const FMoveRecord* Move : Moves)
	{
		if (Move->Attempts == 0 && !Move->bSucceeded)
		{
			MoveLines.Add(MoveJson(*Move));
			continue;
		}
		MaxAttempts = FMath::Max(MaxAttempts, Move->Attempts);
		bAllFirst &= Move->bSucceeded && Move->Attempts == 1;
		MoveLines.Add(MoveJson(*Move));
	}
	const bool bCompleted = !bDescentScripted && FailReason.IsEmpty();
	SurveyRoofGrapples(World, Kate);
	const ACityLedgeSpawner* Spawner = HawkeyeLap::FindSpawner(World);

	TArray<FString> Blocked;
	for (const FString& Where : Meter.BlockedWhere)
	{
		Blocked.Add(FString::Printf(TEXT("\"%s\""), *Where));
	}
	TArray<FString> Roofs;
	for (const FString& Roof : RoofsVisited)
	{
		Roofs.Add(FString::Printf(TEXT("\"%s\""), *Roof));
	}
	const FString Json = FString::Printf(TEXT(
		"{\n  \"test\": \"Hawkeye.Lap.EastVillage\",\n  \"completed\": %s,\n  \"teleports_after_start\": 0,\n  \"descent_failed\": %s,\n  \"descent_note\": \"%s\",\n"
		"  \"descent_route\": \"%s\",\n  \"descent_hangs\": %d,\n  \"descent_catches\": %d,\n  \"descent_landing_stops\": %d,\n"
		"  \"descent_last_drop_cm\": %.0f,\n  \"descent_seconds\": %.2f,\n"
		"  \"fail_reason\": \"%s\",\n  \"total_seconds\": %.2f,\n  \"street_metres_before_vault\": %.1f,\n"
		"  \"blocked_events\": %d,\n  \"blocked_where\": [%s],\n  \"max_attempts\": %d,\n  \"all_first_attempt\": %s,\n"
		"  \"moves\": [\n    %s\n  ],\n  \"roofs\": [%s],\n  \"longest_chain_zips\": %d,\n"
		"  \"chain_midair_redirects\": %d,\n  \"chain_touch_and_go\": %d,\n  \"chain_longest_touch_seconds\": %.2f,\n"
		"  \"chain_touchdowns\": %d,\n  \"chain_legs\": [%s],\n  \"roofs_without_street\": %d,\n  \"roof_chain_seconds\": %.2f,\n"
		"  \"frames\": %d,\n  \"excluded_script_frames\": %d,\n  \"excluded_capture_frames\": %d,\n  \"average_frame_ms\": %.2f,\n  \"worst_frame_ms\": %.2f,\n  \"worst_frame_index\": %d,\n  \"frames_over_33ms\": %d,\n  \"frames_over_100ms\": %d,\n"
		"  \"ledge_spawn_load_ms\": %.1f,\n  \"ledge_spawn_total_ms\": %.1f,\n  \"anchor_spawn_ms\": %.1f,\n"
		"  \"street_grapple_note\": \"%s\",\n  \"kate_health\": %.1f,\n  \"survey_roofs\": %d,\n  \"survey_roofs_with_clear_roof_grapple\": %d\n}\n"),
		bCompleted ? TEXT("true") : TEXT("false"), bDescentScripted ? TEXT("true") : TEXT("false"),
		*DescentNote.ReplaceCharWithEscapedChar(), *DescentRoute.ReplaceCharWithEscapedChar(), DescentHangs, DescentCatches,
		DescentLandings, DescentLastDrop, DescentEnd > 0.0 ? DescentEnd - DescentStart : -1.0,
		*FailReason.ReplaceCharWithEscapedChar(), Seconds, StreetMetres,
		Meter.BlockedEvents, *FString::Join(Blocked, TEXT(", ")), MaxAttempts, bAllFirst ? TEXT("true") : TEXT("false"),
		*FString::Join(MoveLines, TEXT(",\n    ")), *FString::Join(Roofs, TEXT(", ")), LongestChain, ChainMidAir, ChainTouchAndGo, LongestTouch,
		ChainTouchDowns, *FString::Join(ChainLegs, TEXT(", ")), Roof1.IsValid() ? RoofChainLandings.Num() + 1 : 0,
		RoofChainStart > 0.0 && RoofChainEnd > RoofChainStart ? RoofChainEnd - RoofChainStart : -1.0, Meter.Frames, ExcludedFrames, Meter.CaptureFrames,
		Meter.AverageMs(), Meter.WorstFrame * 1000.0, Meter.WorstFrameIndex, Meter.FramesOver33, Meter.FramesOver100, Spawner ? Spawner->GetLoadLedgeSpawnSeconds() * 1000.f : -1.f,
		Spawner ? Spawner->GetTotalLedgeSpawnSeconds() * 1000.f : -1.f, Spawner ? Spawner->GetAnchorSpawnSeconds() * 1000.f : -1.f,
		*StreetGrappleNote.ReplaceCharWithEscapedChar(), Kate->GetHealthComponent()->GetCurrentHealth(), SurveyRoofs, SurveyRoofsWithExit);
	HawkeyeLap::WriteText(TEXT("lap_eastvillage.json"), HawkeyeLapPressure::Finish(Test, TEXT("EastVillage"), Json));
	Test->AddInfo(TEXT("lap_eastvillage.json:\n") + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] lap: %s"), *Json);

	if (!bCompleted)
	{
		Test->AddError(FString::Printf(TEXT("The lap did not complete on its own: %s%s"), *FailReason,
			bDescentScripted ? *(TEXT(" descent: ") + DescentNote) : TEXT("")));
	}
	if (Seconds >= 120.0)
	{
		Test->AddError(FString::Printf(TEXT("Lap took %.1f s; the limit is 120 (the plan wants 90)."), Seconds));
	}
	if (MaxAttempts > 2)
	{
		Test->AddError(FString::Printf(TEXT("A move needed %d attempts; at most 2 allowed."), MaxAttempts));
	}
	if (Meter.AverageMs() >= 16.7)
	{
		Test->AddError(FString::Printf(TEXT("Average frame %.2f ms; the limit is 16.7."), Meter.AverageMs()));
	}
	HawkeyeLap::SetThugsThinking(World, NAME_None, true);
}

bool FHawkeyeLapRunner::Update()
{
	using namespace HawkeyeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		if (!World || Now(World) > 30.0)
		{
			Test->AddError(TEXT("No game world or no Kate for the lap."));
			return true;
		}
		return false;
	}
	const float DeltaSeconds = World->GetDeltaSeconds();
	const double UpdateStart = FPlatformTime::Seconds();
	if (StepStart < 0.0)
	{
		StepStart = Now(World);
	}
	bMoveHeld = false;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	FString Leg;

	switch (Step)
	{
	case EStep::WaitReady:
	{
		const ACityLedgeSpawner* Spawner = FindSpawner(World);
		const bool bReady = (!Spawner || Spawner->IsSpawnComplete()) && Kate->GetCharacterMovement()->IsMovingOnGround();
		if (!bReady && Now(World) - StepStart < 20.0)
		{
			return false;
		}
		APlayerStart* PlayerStart = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			PlayerStart = *It;
			break;
		}
		VaultBlock = FindTagged(World, VaultTag);
		MantleBlock = FindTagged(World, MantleTag);
		FVector Ground;
		if (!PlayerStart || !VaultBlock.IsValid() || !MantleBlock.IsValid()
			|| !FindGround(World, PlayerStart->GetActorLocation(), PlayerStart->GetActorLocation().Z + 200.f, Kate, Ground))
		{
			Test->AddError(TEXT("No PlayerStart or no parkour test blocks on the district."));
			return true;
		}
		SetThugsThinking(World, NAME_None, false);
		IndexFireEscapes(World);
		EnsureQuiver(Test, Kate->GetInventoryComponent());
		Start = Ground;
		StartYaw = PlayerStart->GetActorRotation().Yaw;
		const FVector VaultCentre = VaultBlock->GetComponentsBoundingBox().GetCenter();
		const FVector Across = FRotator(0.f, StartYaw, 0.f).Vector();
		const FVector Along(-Across.Y, Across.X, 0.f);
		AlongToVault = FVector::DotProduct(VaultCentre - Start, Along) >= 0.f ? Along : -Along;
		// The outward lane: 3 m to whichever side a capsule sweep finds clear for 26 m.
		float BestClear = -1.f;
		for (const float Side : { 1.f, -1.f })
		{
			const FVector From = Start + Across * Side * 300.f + FVector(0.f, 0.f, 100.f);
			const FVector To = From + AlongToVault * 2600.f;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(LapLane), false, Kate);
			FHitResult Hit;
			const bool bHit = World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(40.f, 60.f), Params);
			const float Clear = bHit ? Hit.Distance : 2600.f;
			if (Clear > BestClear)
			{
				BestClear = Clear;
				Lateral = Across * Side * 300.f;
			}
		}
		Waypoints = { Start + Lateral + AlongToVault * 200.f, Start + Lateral + AlongToVault * 2500.f,
			Start + AlongToVault * 1800.f, Start - AlongToVault * 1100.f };
		WaypointIndex = 0;
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Start + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, AlongToVault.Rotation().Yaw, 0.f));
		PC->SetControlRotation(FRotator(-10.f, AlongToVault.Rotation().Yaw, 0.f));
		PC->SetViewTarget(Kate);
		Kate->GetHealthComponent()->Heal(1000.f);
		Test->AddInfo(FString::Printf(TEXT("Lap start %s, vault along %s, outward lane %s clear %.0f cm"),
			*Start.ToCompactString(), *AlongToVault.ToCompactString(), *Lateral.ToCompactString(), BestClear));
		Shot(Test, TEXT("lap_start.png"));
		LapStart = Now(World);
		LastStreetPoint = Kate->GetActorLocation();
		Enter(EStep::StreetOut, World);
		break;
	}

	case EStep::StreetOut:
		Leg = TEXT("street out");
		StreetMetres += FVector::Dist2D(Kate->GetActorLocation(), LastStreetPoint) / 100.f;
		LastStreetPoint = Kate->GetActorLocation();
		if (Steer(PC, Kate, Waypoints[WaypointIndex], true, 150.f, DeltaSeconds))
		{
			++WaypointIndex;
			if (WaypointIndex >= 3)
			{
				bMoveStarted = false;
				Enter(EStep::Vault, World);
			}
		}
		break;

	case EStep::Vault:
	case EStep::Mantle:
	{
		const bool bVault = Step == EStep::Vault;
		Leg = bVault ? TEXT("vault") : TEXT("mantle");
		AActor* Block = (bVault ? VaultBlock : MantleBlock).Get();
		FMoveRecord& Move = bVault ? VaultMove : MantleMove;
		if (bVault && !bMoveStarted && Move.Attempts == 0)
		{
			StreetMetres += FVector::Dist2D(Kate->GetActorLocation(), LastStreetPoint) / 100.f;
			LastStreetPoint = Kate->GetActorLocation();
		}
		Steer(PC, Kate, Waypoints[3], true, 100.f, DeltaSeconds);
		const int32 Result = WatchBlock(World, PC, Kate, Block, Move, DeltaSeconds);
		if (Result != 0)
		{
			bMoveStarted = false;
			Enter(bVault ? EStep::Mantle : EStep::PastMantle, World);
		}
		break;
	}

	case EStep::PastMantle:
		Leg = TEXT("past mantle");
		if (Steer(PC, Kate, Waypoints[3], true, 150.f, DeltaSeconds) || Now(World) - StepStart > 4.0)
		{
			StopMoving(PC);
			ChooseStreetAnchor(World, Kate);
			if (Waypoints.Num() > 0 && WaypointIndex >= 0)
			{
				Enter(EStep::StreetToAnchor, World);
			}
			else
			{
				AppendStreetFallbacks(World, Kate);
				Enter(EStep::GrappleFromStreet, World);
			}
		}
		break;

	case EStep::StreetToAnchor:
	{
		Leg = TEXT("street to anchor");
		const AGrappleAnchor* Target = StreetTarget.Get();
		(void)Target;
		bool bArrived = !Waypoints.IsValidIndex(WaypointIndex);
		if (!bArrived && Steer(PC, Kate, Waypoints[WaypointIndex], true, 120.f, DeltaSeconds))
		{
			++WaypointIndex;
		}
		if (bArrived || Now(World) - StepStart > 15.0)
		{
			StopMoving(PC);
			AppendStreetFallbacks(World, Kate);
			Enter(EStep::GrappleFromStreet, World);
		}
		break;
	}

	case EStep::GrappleFromStreet:
	case EStep::GrappleAcross:
	case EStep::GrappleOn:
	{
		FMoveRecord& Move = Step == EStep::GrappleFromStreet ? Grapple1 : (Step == EStep::GrappleAcross ? Grapple2 : Grapple3);
		const int32 Result = AimAndFire(World, PC, Kate, Move);
		if (Result > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Lap: %s fired at %s on press %d"), *Move.Name, *GetNameSafe(ZipTarget.Get()), Move.Attempts));
			if (Step == EStep::GrappleAcross)
			{
				RoofChainStart = Now(World);
			}
			bChainPressed = false;
			bChainedThisZip = false;
			ChainPressesThisZip = 0;
			ChainTarget.Reset();
			bChainSearched = false;
			Enter(Step == EStep::GrappleFromStreet ? EStep::ZipToRoof : (Step == EStep::GrappleAcross ? EStep::ZipAcross : EStep::ZipOn), World);
		}
		else if (Result < 0)
		{
			FailReason = Move.Name + TEXT(": ") + Move.Note;
			Test->AddWarning(TEXT("Lap: ") + FailReason);
			// Without a roof there is nothing to come down from; run home from here.
			Enter(Step == EStep::GrappleFromStreet ? EStep::Return : EStep::Descent, World);
			WaypointIndex = -1;
		}
		break;
	}

	case EStep::ZipToRoof:
	case EStep::ZipAcross:
	case EStep::ZipOn:
	{
		// Chaining: from 40% along, look at an anchor on a building not yet visited and press as
		// soon as the chain window is open.
		if (!Grapple->IsZipping() && Grapple->IsArrowInFlight() && Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			TouchSeconds += DeltaSeconds;
		}
		if (ChainShotAt > 0.0 && !bChainShotTaken && Now(World) >= ChainShotAt)
		{
			// A moment after the first mid-air redirect, the camera on the new anchor.
			bChainShotTaken = true;
			Shot(Test, TEXT("chain_midair.png"));
		}
		if (Grapple->IsZipping())
		{
			AGrappleAnchor* Current = Grapple->GetZipAnchor();
			if (Current && Current != ZipTarget.Get() && bChainPressed)
			{
				// The chain took: redirected in the air, or (the arrow slower than the rest of a
				// short line) a new zip from the roof a moment after touching down.
				const bool bMidAir = Grapple->GetZipLaunch().Equals(Grapple->GetZipStart(), 1.f);
				ChainMidAir += bMidAir ? 1 : 0;
				ChainTouchAndGo += bMidAir ? 0 : 1;
				ChainTouchDowns += bMidAir ? 0 : 1;
				ChainLegs.Add(FString::Printf(TEXT("\"%s -> %s: %s, new line %.0f cm\""), *GetNameSafe(ZipTarget.Get()),
					*GetNameSafe(Current), bMidAir ? TEXT("redirected in the air") : *FString::Printf(TEXT("touched down %.2f s"), TouchSeconds),
					Grapple->GetZipLength()));
				if (bMidAir && ChainShotAt < 0.0)
				{
					ChainShotAt = Now(World) + 0.12;
				}
				LongestTouch = FMath::Max(LongestTouch, bMidAir ? 0.f : TouchSeconds);
				ChainNotes.Add(FString::Printf(TEXT("%s %s"), *GetNameSafe(Current),
					bMidAir ? TEXT("in the air") : *FString::Printf(TEXT("after %.2f s on the roof"), TouchSeconds)));
				ChainMove.Note = FString::Join(ChainNotes, TEXT(", then "));
				TouchSeconds = 0.f;
				if (const AGrappleAnchor* Previous = ZipTarget.Get())
				{
					ChainedRoofs.Add(BuildingUnder(World, Previous->GetLandingLocation(), Kate));
				}
				ChainMove.bSucceeded = true;
				ZipTarget = Current;
				ZipsWithoutLanding = FMath::Max(ZipsWithoutLanding, 1) + 1;
				bChainedThisZip = true;
				bChainPressed = false;
				ChainTarget.Reset();
				bChainSearched = false;
				ChainPressesThisZip = 0;
				Test->AddInfo(FString::Printf(TEXT("Lap: chained to %s"), *GetNameSafe(Current)));
			}
			else if (!Grapple->IsArrowInFlight() && Grapple->GetZipProgress() >= 0.4f && Current && Now(World) - LastChainPress > 0.45
				&& FMath::Max(ZipsWithoutLanding, 1) < 3 && Step != EStep::ZipToRoof)
			{
				if (!ChainTarget.IsValid() && !bChainSearched)
				{
					bChainSearched = true;
					const FVector End = Current->GetLandingLocation();
					const FVector Dir = (End - Kate->GetActorLocation()).GetSafeNormal2D();
					const AActor* Across = BuildingUnder(World, End, Kate);
					// Where she will be when the chain is pressed: a few frames on, just past the window.
					const float Progress = Grapple->GetZipProgress();
					const float PressProgress = Progress >= Grapple->ChainMinProgress ? Progress : Grapple->ChainMinProgress + 0.05f;
					const FVector ZipEndCentre = Grapple->ComputeZipEnd(Current);
					const FVector PressAt = Kate->GetActorLocation()
						+ (ZipEndCentre - Kate->GetActorLocation()) * FMath::Clamp((PressProgress - Progress) / FMath::Max(1.f - Progress, 0.01f), 0.f, 1.f);
					int32 Blocked = 0;
					// A roof with a fire escape first (the chain may be the last hop), then any roof.
					for (const bool bEscapeOnly : { true, false })
					{
						float Best = BIG_NUMBER;
						for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
						{
							const FVector Landing = It->GetLandingLocation();
							const float FromEnd = FVector::Dist(End, It->GetMarkerLocation());
							const float FromPress = FVector::Dist(PressAt, It->GetMarkerLocation());
							if (FromEnd < 900.f || FromPress > Grapple->Range - 150.f || FromPress < Grapple->MinRange + 100.f)
							{
								continue;
							}
							const AActor* Under = BuildingUnder(World, Landing, Kate);
							if (!Under || Under == Across || Under == Roof1.Get() || Under == Roof2.Get()
								|| ChainedRoofs.Contains(Under) || (bEscapeOnly && !TopLandingByRoof.Contains(Under)))
							{
								continue;
							}
							const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
								FVector::DotProduct(Dir, (Landing - End).GetSafeNormal2D()), -1.f, 1.f)));
							if (Angle >= 90.f || Angle >= Best)
							{
								continue;
							}
							if (!ZipClear(World, Kate, PressAt, *It, /*bFromGround=*/false) || !CameraSees(Kate, PressAt, *It))
							{
								++Blocked;
								continue;
							}
							Best = Angle;
							ChainTarget = *It;
						}
						if (ChainTarget.IsValid())
						{
							break;
						}
					}
					if (!ChainTarget.IsValid())
					{
						const FString None = FString::Printf(TEXT("no clear anchor on a%s building ahead of the zip (%d blocked)"),
							ChainNotes.Num() ? TEXT("nother") : TEXT(" third"), Blocked);
						ChainMove.Note = ChainNotes.Num() ? FString::Join(ChainNotes, TEXT(", then ")) + TEXT("; ") + None : None;
					}
				}
				if (AGrappleAnchor* Next = ChainTarget.Get())
				{
					AimAt(PC, Kate, Next->GetMarkerLocation());
					Grapple->RefreshTarget();
					// A player looks again before letting go: the line from here, not from where the
					// search guessed she would be, has to be clear.
					if (Grapple->CanChain() && Grapple->GetTargetAnchor() == Next && !Grapple->IsArrowInFlight()
						&& ChainPressesThisZip < 3 && !ZipClear(World, Kate, Kate->GetActorLocation(), Next, /*bFromGround=*/false))
					{
						ChainMove.Note = FString::Printf(TEXT("%s not clear from %.0f%% along; searching again"), *GetNameSafe(Next),
							Grapple->GetZipProgress() * 100.f);
						ChainTarget.Reset();
						bChainSearched = false;
					}
					else if (Grapple->CanChain() && Grapple->GetTargetAnchor() == Next && !Grapple->IsArrowInFlight()
						&& ChainPressesThisZip < 3)
					{
						++ChainPressesThisZip;
						LastChainPress = Now(World);
						ChainMove.Attempts = FMath::Max(ChainMove.Attempts, ChainPressesThisZip);
						bChainPressed = true;
						Tap(PC, GrapplePath);
					}
				}
			}
			// Only time on the roof between two zips counts as a touch.
			TouchSeconds = 0.f;
		}

		const int32 Result = WatchZip(World, Kate);
		if (Result == 0)
		{
			break;
		}
		if (Result < 0)
		{
			FMoveRecord& Move = Step == EStep::ZipToRoof ? Grapple1 : (Step == EStep::ZipAcross ? Grapple2 : Grapple3);
			Move.bSucceeded = false;
			Move.Note = FString::Printf(TEXT("zip to %s cancelled or missed; Kate at %s"), *GetNameSafe(ZipTarget.Get()),
				*Kate->GetActorLocation().ToCompactString());
			FailReason = Move.Name + TEXT(": ") + Move.Note;
			Test->AddWarning(TEXT("Lap: ") + FailReason);
			WaypointIndex = -1;
			Enter(Kate->GetActorLocation().Z - Start.Z < 300.f ? EStep::Return : EStep::Descent, World);
			break;
		}
		AActor* Roof = BuildingUnder(World, Kate->GetActorLocation() - FVector(0.f, 0.f, 88.f), Kate);
		const FString RoofName = Roof ? Roof->GetName() : TEXT("?");
		RoofsVisited.Add(RoofName);
		LongestChain = FMath::Max(LongestChain, FMath::Max(ZipsWithoutLanding, 1));
		ZipsWithoutLanding = 0;
		Test->AddInfo(FString::Printf(TEXT("Lap %.1f s: landed on %s at %s"), Now(World) - LapStart, *RoofName,
			*Kate->GetActorLocation().ToCompactString()));
		if (Step != EStep::ZipToRoof)
		{
			RoofChainEnd = Now(World);
			RoofChainLandings.AddUnique(Roof);
		}
		if (Step == EStep::ZipToRoof || Step == EStep::ZipAcross)
		{
			const bool bFirst = Step == EStep::ZipToRoof;
			if (bFirst)
			{
				Roof1 = Roof;
			}
			else
			{
				Roof2 = Roof;
			}
			if (bFirst && !bRoofShotTaken)
			{
				// Taken a moment into the run across the roof, once the camera has come down
				// from looking up at the anchor.
				bRoofShotPending = true;
			}
			// The roofs landed on, with the one she stands on last. A building whose anchor a chain
			// only passed through may be landed on later.
			// After a chain the buildings it passed through are left out too: the third roof is a new one.
			TArray<const AActor*> Exclude = { Roof1.Get() };
			if (!bFirst)
			{
				for (const TWeakObjectPtr<AActor>& Passed : ChainedRoofs)
				{
					if (Passed.IsValid() && Passed.Get() != Roof2.Get())
					{
						Exclude.AddUnique(Passed.Get());
					}
				}
				Exclude.Add(Roof2.Get());
			}
			FString Note;
			FMoveRecord& Next = bFirst ? Grapple2 : Grapple3;
			if (PlanRoofGrapple(World, Kate, Exclude, Note, /*bPreferEscape=*/!bFirst))
			{
				AfterRun = bFirst ? EStep::GrappleAcross : EStep::GrappleOn;
				Enter(EStep::RoofRun, World);
			}
			else
			{
				// The second roof-to-roof hop is where the geometry allows; without one she comes
				// down from here and the lap goes on.
				Next.Note = Note;
				if (bFirst)
				{
					FailReason = Next.Name + TEXT(": ") + Note;
				}
				Test->AddWarning(TEXT("Lap: ") + Next.Name + TEXT(": ") + Note);
				ParapetPoint = FVector::ZeroVector;
				DescentPhase = 0;
				Enter(EStep::Descent, World);
			}
		}
		else
		{
			ParapetPoint = FVector::ZeroVector;
			DescentPhase = 0;
			Enter(EStep::Descent, World);
		}
		break;
	}

	case EStep::RoofRun:
		Leg = TEXT("roof run");
		if (bRoofShotPending && Now(World) - StepStart > 0.8)
		{
			bRoofShotPending = false;
			bRoofShotTaken = true;
			Shot(Test, TEXT("lap_roof.png"));
		}
		if (Steer(PC, Kate, RunTarget, true, 60.f, DeltaSeconds) || Now(World) - StepStart > 10.0)
		{
			StopMoving(PC);
			Enter(AfterRun, World);
		}
		break;

	case EStep::Descent:
	{
		// Down the roof's fire escape: crouch at the parapet over its top landing (drop to hang),
		// crouch to drop, and at each landing either catch its rail on the way down or stand on it
		// and crouch at its rail; the last drop is to the street. Without an escape, a hang drop off
		// the nearest parapet with a street beyond it.
		Leg = TEXT("descent");
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		const float FeetZ = HawkeyeLap::Feet(Kate).Z;
		if (DescentPhase == 0)
		{
			DescentStart = Now(World);
			AActor* Roof = BuildingUnder(World, Kate->GetActorLocation() - FVector(0.f, 0.f, 88.f), Kate);
			const TWeakObjectPtr<AFireEscapeLanding>* Top = Roof ? TopLandingByRoof.Find(Roof) : nullptr;
			if (Top && Top->IsValid())
			{
				const AFireEscapeLanding* Landing = Top->Get();
				DescentOut = Landing->GetActorRightVector().GetSafeNormal2D();
				// On the roof, square behind the landing's middle: parapet (30), capsule (34), a margin.
				const FVector Facade = Landing->GetActorLocation();
				DescentStand = FVector(Facade.X, Facade.Y, FeetZ) - DescentOut * 110.f;
				DescentRoute = FString::Printf(TEXT("fire escape of %s, %d landings, top at %.0f cm"), *GetNameSafe(Roof),
					Landing->GetRecord().Floor, Landing->GetActorLocation().Z - Start.Z);
			}
			else if (FindParapet(World, Kate))
			{
				DescentOut = ParapetDir.GetSafeNormal2D();
				DescentStand = FVector(ParapetPoint.X, ParapetPoint.Y, FeetZ) - DescentOut * 80.f;
				DescentRoute = FString::Printf(TEXT("no fire escape on %s: hang drop off the parapet, %.0f m up"), *GetNameSafe(Roof),
					(FeetZ - Start.Z) / 100.f);
			}
			else
			{
				DescentNote = FString::Printf(TEXT("no fire escape on %s and no parapet with a street beyond it"), *GetNameSafe(Roof));
				bDescentScripted = true;
				DescentPhase = 9;
				break;
			}
			Test->AddInfo(TEXT("Lap: descent by ") + DescentRoute);
			DescentPhase = 1;
			PhaseStart = Now(World);
		}
		if (DescentPhase == 1)
		{
			// Walk to the spot behind the edge.
			if (Steer(PC, Kate, DescentStand, false, 40.f, DeltaSeconds) || Now(World) - PhaseStart > 10.0)
			{
				DescentPhase = 2;
				PhaseStart = Now(World);
			}
		}
		else if (DescentPhase == 2)
		{
			// Push toward the edge for a moment (into the parapet or the rail), then crouch.
			Steer(PC, Kate, FVector(DescentStand.X, DescentStand.Y, FeetZ) + DescentOut * 1000.f, false, 1.f, DeltaSeconds);
			if (Parkour->IsBusy())
			{
				StopMoving(PC);
				DescentMove.Attempts = FMath::Max(DescentMove.Attempts, DescentRetries + 1);
				DescentRetries = 0;
				DescentPhase = 3;
				PhaseStart = Now(World);
			}
			else if (Now(World) - PhaseStart > 0.25 && !bPushing)
			{
				bPushing = true;
				Tap(PC, CrouchPath);
			}
			else if (Now(World) - PhaseStart > 1.0)
			{
				bPushing = false;
				PhaseStart = Now(World);
				if (++DescentRetries >= 3)
				{
					FHawkeyeParkourObstacle Edge;
					const bool bEdge = Parkour->FindDropEdge(DescentOut, Edge);
					DescentNote = FString::Printf(TEXT("crouch at the edge found no drop to hang three times at %s (edge probe %d, %.0f cm lip, %.0f cm drop)"),
						*Kate->GetActorLocation().ToCompactString(), bEdge ? 1 : 0, Edge.Height, Edge.LandingDrop);
					bDescentScripted = true;
					DescentPhase = 9;
				}
			}
		}
		else if (DescentPhase == 3)
		{
			// Going over or hanging: once hanging, a beat, then crouch drops.
			bPushing = false;
			if (Parkour->IsHanging())
			{
				if (Now(World) - PhaseStart > 0.3)
				{
					++DescentHangs;
					DescentLastDrop = FeetZ - Start.Z;
					Tap(PC, CrouchPath);
					DescentPhase = 4;
					PhaseStart = Now(World);
				}
			}
			else if (!Parkour->IsBusy() && Now(World) - PhaseStart > 2.0)
			{
				DescentNote = FString::Printf(TEXT("the move over the edge did not end in a hang (at %s)"), *Kate->GetActorLocation().ToCompactString());
				bDescentScripted = true;
				DescentPhase = 9;
			}
		}
		else if (DescentPhase == 4 && Now(World) - PhaseStart > 0.15)
		{
			// Falling: a catch goes back to the hang; a landing on a landing crouches at its rail;
			// the street ends the descent.
			if (Parkour->IsBusy())
			{
				if (Parkour->IsHanging() || Parkour->GetActiveMove() == EHawkeyeParkourMove::LedgeGrab)
				{
					++DescentCatches;
					DescentPhase = 3;
					PhaseStart = Now(World);
				}
			}
			else if (Kate->GetCharacterMovement()->IsMovingOnGround() && Now(World) - PhaseStart > 0.2)
			{
				if (FeetZ - Start.Z > 200.f)
				{
					++DescentLandings;
					DescentStand = FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, FeetZ);
					DescentRetries = 0;
					DescentPhase = 2;
					PhaseStart = Now(World);
				}
				else
				{
					DescentMove.bSucceeded = true;
					DescentEnd = Now(World);
					DescentMove.Note = FString::Printf(TEXT("%s: %d hangs, %d catches, %d stops on a landing, last drop from %.0f cm, %.1f s"),
						*DescentRoute, DescentHangs, DescentCatches, DescentLandings, DescentLastDrop, DescentEnd - DescentStart);
					Test->AddInfo(TEXT("Lap: down: ") + DescentMove.Note);
					WaypointIndex = -1;
					Enter(EStep::Return, World);
				}
			}
			else if (Now(World) - PhaseStart > 5.0)
			{
				DescentNote = FString::Printf(TEXT("still in the air 5 s after a drop (at %s)"), *Kate->GetActorLocation().ToCompactString());
				bDescentScripted = true;
				DescentPhase = 9;
			}
		}
		if (DescentPhase == 9)
		{
			// No teleport: the lap stops here and says why.
			DescentMove.Note = DescentNote;
			FailReason = TEXT("descent: ") + DescentNote;
			Test->AddWarning(TEXT("Lap: ") + FailReason);
			Shot(Test, TEXT("lap_end.png"));
			Finish(World, PC, Kate);
			Enter(EStep::Done, World);
			return false;
		}
		if (Now(World) - DescentStart > 45.0 && DescentPhase != 9 && Step == EStep::Descent)
		{
			DescentNote = FString::Printf(TEXT("descent took over 45 s (phase %d at %s)"), DescentPhase, *Kate->GetActorLocation().ToCompactString());
			bDescentScripted = true;
			DescentMove.Note = DescentNote;
			FailReason = TEXT("descent: ") + DescentNote;
			Finish(World, PC, Kate);
			Enter(EStep::Done, World);
			return false;
		}
		break;
	}

	case EStep::Return:
		Leg = TEXT("return");
		if (WaypointIndex < 0)
		{
			if (!Kate->GetCharacterMovement()->IsMovingOnGround())
			{
				break;
			}
			Waypoints.Reset();
			if (const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Kate->GetActorLocation(), Start))
			{
				Waypoints = Path->PathPoints;
			}
			if (Waypoints.Num() == 0)
			{
				Waypoints.Add(Start);
			}
			Waypoints.Last() = Start;
			WaypointIndex = 0;
			Test->AddInfo(FString::Printf(TEXT("Lap %.1f s: running home, %d path points, %.0f m straight line"),
				Now(World) - LapStart, Waypoints.Num(), FVector::Dist2D(Kate->GetActorLocation(), Start) / 100.f));
		}
		if (Steer(PC, Kate, Waypoints[WaypointIndex], true, WaypointIndex == Waypoints.Num() - 1 ? 150.f : 120.f, DeltaSeconds))
		{
			++WaypointIndex;
			if (WaypointIndex >= Waypoints.Num())
			{
				Shot(Test, TEXT("lap_end.png"));
				Finish(World, PC, Kate);
				Enter(EStep::Done, World);
				return false;
			}
		}
		break;

	case EStep::Done:
		// A beat so the end shot is written before the next test.
		return Now(World) - StepStart > 1.0;
	}

	if (LapStart >= 0.0 && Step != EStep::Done)
	{
		// A frame that follows one where the script itself planned for more than 20 ms is its cost, not the game's.
		if (!bExcludeThisFrame)
		{
			Meter.Frame(DeltaSeconds);
		}
		else
		{
			++ExcludedFrames;
		}
		bExcludeThisFrame = FPlatformTime::Seconds() - UpdateStart > 0.02;
		const bool bFree = !Kate->IsTraversing() && !Kate->IsZipping();
		Meter.Blocked(Kate, bMoveHeld && bFree, DeltaSeconds, Leg, SteerDirection);
		if (!bMoveHeld && bMoveInjecting && Step != EStep::Vault && Step != EStep::Mantle)
		{
			StopMoving(PC);
		}
		if (Now(World) - LapStart > 240.0)
		{
			FailReason = FString::Printf(TEXT("timed out in step %d"), static_cast<int32>(Step));
			Finish(World, PC, Kate);
			Enter(EStep::Done, World);
		}
	}
	return false;
}

// --- The roof fight -----------------------------------------------------------------------------

class FHawkeyeRoofFightRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeRoofFightRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	FAutomationTestBase* Test;
	bool bSetUp = false;
	bool bDone = false;
	double FightStart = -1.0;
	double DoneAt = -1.0;
	double DrawStart = 0.0;
	double NextActionAt = 0.0;
	float LastHealth = 0.f;
	float DamageTaken = 0.f;
	int32 HitsTaken = 0;
	int32 ArrowsLoosed = 0;
	int32 LightSwings = 0;
	int32 HeavySwings = 0;
	int32 Dodges = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 MaxCombo = 0;
	double LastParryTap = -10.0;
	bool bDrawing = false;
	bool bMeleeHeld = false;
	double MeleeHeldSince = 0.0;
	bool bMoveInjecting = false;
	HawkeyeLap::FMeter Meter;
	TArray<TWeakObjectPtr<AThugCharacter>> Pair;

	static bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || Thug->GetHealthComponent()->IsDead();
	}

	void Move(APlayerController* PC, const FVector2D& Value)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeLap::InputOf(PC);
		const UInputAction* A = HawkeyeLap::Action(HawkeyeLap::MovePath);
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
		}
		else
		{
			Input->UpdateValueOfContinuousInputInjectionForAction(A, FInputActionValue(Value));
		}
	}

	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why);
};

void FHawkeyeRoofFightRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why)
{
	Move(PC, FVector2D::ZeroVector);
	HawkeyeLap::Hold(PC, HawkeyeLap::FirePath, false);
	HawkeyeLap::Hold(PC, HawkeyeLap::MeleePath, false);
	bDone = true;
	DoneAt = World->GetTimeSeconds();
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	int32 Dead = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Pair)
	{
		Dead += IsDown(Thug.Get()) ? 1 : 0;
	}
	const bool bWon = Dead == Pair.Num() && Pair.Num() == 2 && Health > 0.f;
	const FString Json = FString::Printf(TEXT(
		"{\n  \"test\": \"Hawkeye.Lap.RoofFight\",\n  \"difficulty\": \"%s\",\n  \"won\": %s,\n  \"end\": \"%s\",\n  \"seconds\": %.2f,\n"
		"  \"kate_health\": %.1f,\n  \"kate_max_health\": %.1f,\n  \"hits_taken\": %d,\n  \"damage_taken\": %.1f,\n"
		"  \"thugs_down\": %d,\n  \"arrows_loosed\": %d,\n  \"light_swings\": %d,\n"
		"  \"heavy_swings\": %d,\n  \"dodges\": %d,\n  \"parries\": %d,\n  \"finishers\": %d,\n"
		"  \"perfect_dodges\": %d,\n  \"max_combo\": %d,\n  \"average_frame_ms\": %.2f\n}\n"),
		*HawkeyeLap::DifficultyName(Kate), bWon ? TEXT("true") : TEXT("false"), *Why, DoneAt - FightStart, Health, Kate->GetHealthComponent()->GetMaxHealth(),
		HitsTaken, DamageTaken, Dead, ArrowsLoosed, LightSwings, HeavySwings, Dodges, Parries, Finishers,
		Kate->GetPerfectDodgeCount(), MaxCombo, Meter.AverageMs());
	const FString WithFights = HawkeyeLapPressure::Finish(Test, TEXT("RoofFight"), Json);
	HawkeyeLap::WriteText(TEXT("lap_roof_fight.json"), WithFights);
	HawkeyeLap::WriteText(FString::Printf(TEXT("lap_roof_fight_%s.json"), *HawkeyeLap::DifficultyName(Kate).ToLower()), WithFights);
	Test->AddInfo(TEXT("lap_roof_fight.json:\n") + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] roof fight: %s"), *Json);
	if (!bWon)
	{
		Test->AddError(FString::Printf(TEXT("Kate did not win the roof fight (%s): health %.0f, %d of 2 thugs down."), *Why, Health, Dead));
	}
	HawkeyeLap::SetThugsThinking(World, NAME_None, true);
}

bool FHawkeyeRoofFightRunner::Update()
{
	using namespace HawkeyeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		Test->AddError(TEXT("No game world or no Kate for the roof fight."));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	if (bDone)
	{
		return Now - DoneAt > 1.0;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();

	if (!bSetUp)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(RoofPairTag) && !IsDown(*It))
			{
				Pair.Add(*It);
			}
		}
		if (Pair.Num() != 2)
		{
			Test->AddError(FString::Printf(TEXT("Expected the two RoofPair thugs, found %d."), Pair.Num()));
			return true;
		}
		SetThugsThinking(World, NAME_None, false);
		EnsureQuiver(Test, Kate->GetInventoryComponent());
		Kate->GetInventoryComponent()->SelectArrowSlot(1);
		// Kate on the roof 5 to 9 m from the pair along its long axis, at whichever end has roof.
		const FVector Mid = (Pair[0]->GetActorLocation() + Pair[1]->GetActorLocation()) * 0.5f;
		FVector Axis = (Pair[1]->GetActorLocation() - Pair[0]->GetActorLocation()).GetSafeNormal2D();
		FVector Ground = FVector::ZeroVector;
		bool bPlaced = false;
		for (const float Standoff : { 900.f, 700.f, 500.f })
		{
			for (const float Sign : { 1.f, -1.f })
			{
				AActor* Under = nullptr;
				const FVector Spot = Mid - Axis * Sign * Standoff;
				if (FindGround(World, Spot, Mid.Z + 300.f, Kate, Ground, &Under) && FMath::Abs(Ground.Z - (Mid.Z - 90.f)) < 60.f
					&& Under && Under->Tags.Contains(BuildingTag))
				{
					Axis *= Sign;
					bPlaced = true;
					break;
				}
			}
			if (bPlaced)
			{
				break;
			}
		}
		if (!bPlaced)
		{
			Test->AddError(TEXT("No roof to stand on beside the roof pair."));
			return true;
		}
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Axis.Rotation().Yaw, 0.f));
		PC->SetControlRotation(FRotator(-8.f, Axis.Rotation().Yaw, 0.f));
		PC->SetViewTarget(Kate);
		KateHealth->SetInvulnerable(false);
		KateHealth->Heal(1000.f);
		LastHealth = KateHealth->GetCurrentHealth();
		// The pair alerted on her: they rush.
		SetThugsThinking(World, RoofPairTag, true);
		for (const TWeakObjectPtr<AThugCharacter>& Thug : Pair)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
			{
				Brain->SetTarget(Kate);
				Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			}
		}
		Test->AddInfo(FString::Printf(TEXT("Roof fight: Kate at %s, %.0f cm from the pair, health %.0f"),
			*Kate->GetActorLocation().ToCompactString(), FVector::Dist2D(Kate->GetActorLocation(), Mid), LastHealth));
		bSetUp = true;
		FightStart = Now;
		return false;
	}

	Meter.Frame(World->GetDeltaSeconds());

	// Hits taken: every drop in Kate's health (regeneration only ever raises it).
	const float Health = KateHealth->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++HitsTaken;
		DamageTaken += LastHealth - Health;
		Test->AddInfo(FString::Printf(TEXT("Roof fight %.1f s: Kate hit for %.0f, health %.0f"), Now - FightStart, LastHealth - Health, Health));
	}
	LastHealth = Health;

	if (!KateHealth->IsAlive())
	{
		Finish(World, PC, Kate, TEXT("Kate died"));
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
		Finish(World, PC, Kate, TEXT("both thugs down"));
		return false;
	}
	if (Now - FightStart > 90.0)
	{
		Finish(World, PC, Kate, TEXT("timed out"));
		return false;
	}

	// Look at whoever is nearest, at the chest.
	AimAt(PC, Kate, Target->GetActorLocation() + FVector(0.f, 0.f, 30.f));
	MaxCombo = FMath::Max(MaxCombo, Kate->GetComboCount());
	const bool bBusy = Kate->IsMeleeAttacking() || Kate->IsDodging() || Kate->IsStaggered() || Kate->IsLockedOutByTakedown();
	const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();

	// A melee hold in progress: release after 0.45 s for the heavy.
	if (bMeleeHeld)
	{
		if (Now - MeleeHeldSince >= 0.45)
		{
			Hold(PC, MeleePath, false);
			bMeleeHeld = false;
			NextActionAt = Now + 0.1;
		}
		Move(PC, FVector2D::ZeroVector);
		return false;
	}

	if (bDrawing)
	{
		// Loose at full draw, or early if one closes in.
		if (Now - DrawStart >= 0.82 || Distance < 260.f)
		{
			Hold(PC, FirePath, false);
			bDrawing = false;
			++ArrowsLoosed;
			NextActionAt = Now + 0.15;
		}
		return false;
	}

	// A wind-up in front of her: a tap of V parries it, even out of her own recovery.
	if (Now - LastParryTap > 0.3 && CanParryNow(Kate) && ParryChance(World, Kate, PC))
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, MeleePath);
		LastParryTap = Now;
		++Parries;
		NextActionAt = Now + 0.25;
		return false;
	}

	if (Now < NextActionAt || bBusy)
	{
		return false;
	}

	// Anyone open to a finisher within reach (down, parried, or just hit by a combo ender): F finishes him.
	if (CanFinish(Kate))
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, TakedownPath);
		++Finishers;
		NextActionAt = Now + 0.3;
		return false;
	}

	// Any thug close and about to land a swing: step out of it sideways (a direct call: see the
	// file comment). A competent player watches the glowing mask.
	int32 Near = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Other : Pair)
	{
		const AThugCharacter* Thug = Other.Get();
		if (IsDown(Thug))
		{
			continue;
		}
		const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
		Near += D <= 400.f ? 1 : 0;
		const UMeleeComponent* Melee = Thug->GetMeleeComponent();
		if (D <= 250.f && Melee && Melee->IsWindingUp() && Melee->GetPhaseRemaining() < 0.3f && !Thug->IsIncapacitated()
			&& Kate->GetDodgeCooldownRemaining() <= 0.f && !(Thug == Target && Distance <= 150.f))
		{
			const FVector Away = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
			if (Kate->TryDodge((Away + FVector::CrossProduct(FVector::UpVector, Away)).GetSafeNormal2D()))
			{
				++Dodges;
				NextActionAt = Now + 0.3;
				return false;
			}
		}
	}

	if (Distance <= 190.f)
	{
		Move(PC, FVector2D::ZeroVector);
		// A light lands in 0.1 s and cancels a wind-up; the heavy (0.6 s) only when one is alone
		// and not already swinging.
		const bool bTargetSwinging = ThugMelee && ThugMelee->IsWindingUp();
		if (bTargetSwinging || Target->IsKnockedDown() || Target->IsStaggered() || Near > 1)
		{
			Tap(PC, MeleePath);
			++LightSwings;
			NextActionAt = Now + 0.32;
		}
		else
		{
			Hold(PC, MeleePath, true);
			bMeleeHeld = true;
			MeleeHeldSince = Now;
			++HeavySwings;
		}
		return false;
	}

	if (Distance <= 450.f)
	{
		// Close the gap on foot rather than draw with one this close.
		const FVector To = (Target->GetActorLocation() - Kate->GetActorLocation()).GetSafeNormal2D();
		Move(PC, MoveTowards(To, PC->GetControlRotation().Yaw));
		return false;
	}

	Move(PC, FVector2D::ZeroVector);
	Hold(PC, FirePath, true);
	bDrawing = true;
	DrawStart = Now;
	return false;
}

// --- The archer duel --------------------------------------------------------------------------------

/**
 * Hawkeye.Lap.ArcherDuel: Kate on the find_arrow roof against the ArcherPair, with the bow and the
 * parapet. She crouches behind cover (a spot where both archers' lines to her crouched body are
 * blocked but a standing one is clear), stands to shoot only when neither archer is past the start of
 * a draw, looses at full draw on the weaker archer she can see, and crouches again. A fresh draw of
 * 0.9 s or more on either archer while she is up sends her down at once. Must win inside 60 s.
 *
 * An archer who loses her mid-draw holds it (up to 2.5 s) and looses the moment she shows again. She
 * plays it as a first-time player would: she does not know about the hold, so with a glint still up she
 * stands HoldGuessSeconds after ducking, as if the draw had been let down. The first arrow out of a
 * held draw teaches her; from then on she waits for every glint to go out. That lesson is the exchange
 * the duel measures.
 */
class FHawkeyeArcherDuelRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeArcherDuelRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	enum class EPhase : uint8
	{
		Hidden,
		Rising,
		Drawing,
		Moving,
	};

	FAutomationTestBase* Test;
	bool bSetUp = false;
	bool bDone = false;
	double FightStart = -1.0;
	double DoneAt = -1.0;
	double PhaseSince = 0.0;
	EPhase Phase = EPhase::Hidden;
	float LastHealth = 0.f;
	float DamageTaken = 0.f;
	int32 HitsTaken = 0;
	int32 ArrowsAtStart = 0;
	int32 Pops = 0;
	int32 Aborts = 0;
	/** When she last went down behind the parapet, and how long after it she guesses a hold has run out. */
	double DuckedAt = -10.0;
	double HoldGuessSeconds = 1.5;
	/** Set by the first arrow she takes out of a held draw: from then on she waits the glint out. */
	bool bLearnedHolds = false;
	/** Draws the archers held after losing her, and how many of those they loosed on her return. */
	int32 Holds = 0;
	int32 HoldShots = 0;
	TMap<TWeakObjectPtr<AThugCharacter>, bool> WasHolding;
	TMap<TWeakObjectPtr<AThugCharacter>, int32> LoosedBefore;
	FVector Cover = FVector::ZeroVector;
	FString CoverNote;
	HawkeyeLap::FMeter Meter;
	TArray<TWeakObjectPtr<AThugCharacter>> Archers;
	TWeakObjectPtr<AThugCharacter> Target;
	/** Where on the target she aims, cm above his capsule centre: what shows over his parapet. */
	float TargetHeight = 30.f;
	/** How high above her feet her arrows leave the bow, cm. */
	float HandHeight = 120.f;

	static bool IsDown(const AThugCharacter* Archer)
	{
		return !Archer || Archer->IsLimp() || Archer->GetHealthComponent()->IsDead();
	}

	static FVector Eye(const AThugCharacter* Archer)
	{
		return Archer->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	}

	/** A clear Visibility line from Archer's eyes to Point, pawns ignored. */
	static bool Sees(UWorld* World, const AThugCharacter* Archer, const FVector& Point, const AActor* Kate)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DuelSight), false, Archer);
		Params.AddIgnoredActor(Kate);
		return !World->LineTraceTestByChannel(Eye(Archer), Point, ECC_Visibility, Params);
	}

	/** Seconds into the draw an archer is, or -1 when he is not drawing. */
	static float DrawOf(const AThugCharacter* Archer)
	{
		const UBowComponent* Bow = Archer ? Archer->GetBowComponent() : nullptr;
		return Bow && Bow->IsDrawing() ? Bow->GetDrawElapsed() : -1.f;
	}

	bool FindCover(UWorld* World, AHawkeyeCharacter* Kate, const FVector& Beacon, AActor* Roof);
	/** IA_Move injection toward Goal (zero stops it), as a stick would. */
	void Move(APlayerController* PC, const FVector2D& Value);
	bool bMoveInjecting = false;
	TWeakObjectPtr<AActor> RoofActor;
	double NoShotSince = -1.0;
	int32 Moves = 0;
	/** Taps IA_Crouch (a toggle) when she is not already where bDown says, at most every 0.25 s. */
	void Crouch(APlayerController* PC, bool bDown);
	double LastCrouchTap = -10.0;
	double LastStatus = -10.0;
	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why);
};

void FHawkeyeArcherDuelRunner::Move(APlayerController* PC, const FVector2D& Value)
{
	UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeLap::InputOf(PC);
	const UInputAction* A = HawkeyeLap::Action(HawkeyeLap::MovePath);
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
	}
	else
	{
		Input->UpdateValueOfContinuousInputInjectionForAction(A, FInputActionValue(Value));
	}
}

void FHawkeyeArcherDuelRunner::Crouch(APlayerController* PC, bool bDown)
{
	const ACharacter* Kate = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!Kate || !World || Kate->bIsCrouched == bDown || World->GetTimeSeconds() - LastCrouchTap < 0.25)
	{
		return;
	}
	LastCrouchTap = World->GetTimeSeconds();
	HawkeyeLap::Tap(PC, HawkeyeLap::CrouchPath);
}

bool FHawkeyeArcherDuelRunner::FindCover(UWorld* World, AHawkeyeCharacter* Kate, const FVector& Beacon, AActor* Roof)
{
	using namespace HawkeyeLap;
	const float Crouched = Kate->GetCharacterMovement()->GetCrouchedHalfHeight();
	const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float Best = BIG_NUMBER;
	int32 Candidates = 0;
	for (float X = -1200.f; X <= 1200.f; X += 50.f)
	{
		for (float Y = -1200.f; Y <= 1200.f; Y += 50.f)
		{
			FVector Ground;
			AActor* Under = nullptr;
			if (!FindGround(World, Beacon + FVector(X, Y, 0.f), Beacon.Z + 300.f, Kate, Ground, &Under) || Under != Roof
				|| FMath::Abs(Ground.Z - Beacon.Z) > 40.f)
			{
				continue;
			}
			// Room for her capsule standing.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DuelRoom), false, Kate);
			if (World->OverlapBlockingTestByChannel(Ground + FVector(0.f, 0.f, HalfHeight + 5.f), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Radius + 5.f, HalfHeight), Params))
			{
				continue;
			}
			// Crouched she is hidden from both; standing she sees at least one.
			const FVector Low = Ground + FVector(0.f, 0.f, Crouched);
			const FVector High = Ground + FVector(0.f, 0.f, HandHeight);
			bool bHidden = true;
			int32 Seen = 0;
			int32 Living = 0;
			for (const TWeakObjectPtr<AThugCharacter>& Archer : Archers)
			{
				if (IsDown(Archer.Get()))
				{
					continue;
				}
				++Living;
				bHidden = bHidden && !Sees(World, Archer.Get(), Low, Kate) && !Sees(World, Archer.Get(), Low + FVector(0.f, 0.f, 15.f), Kate);
				// An arrow from her hand clears everything on the way to his head.
				FCollisionQueryParams ArrowParams(SCENE_QUERY_STAT(DuelArrowLine), false, Kate);
				ArrowParams.AddIgnoredActor(Archer.Get());
				Seen += World->LineTraceTestByChannel(High, Archer->GetActorLocation() + FVector(0.f, 0.f, 55.f), ECC_Visibility, ArrowParams)
					? 0 : 1;
			}
			if (!bHidden || Seen == 0)
			{
				continue;
			}
			++Candidates;
			// Nearest where she is (the beacon at the start), and reaching every archer still up when it can.
			const float Score = FVector::Dist2D(Ground, Beacon) + (Seen == Living ? 0.f : 600.f);
			if (Score < Best)
			{
				Best = Score;
				Cover = Ground;
				CoverNote = FString::Printf(TEXT("%.0f cm away, her arrows reach %d of %d"), FVector::Dist2D(Ground, Beacon),
					Seen, Living);
			}
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Archer duel: %d cover spots on the find_arrow roof."), Candidates));
	return Candidates > 0;
}

void FHawkeyeArcherDuelRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why)
{
	HawkeyeLap::Hold(PC, HawkeyeLap::FirePath, false);
	Move(PC, FVector2D::ZeroVector);
	Crouch(PC, false);
	if (UBowComponent* Bow = Kate->GetBowComponent())
	{
		Bow->ClearAimOverride();
	}
	bDone = true;
	DoneAt = World->GetTimeSeconds();
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	int32 Down = 0;
	int32 TheirArrows = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Archer : Archers)
	{
		Down += IsDown(Archer.Get()) ? 1 : 0;
		if (const AThugCharacter* A = Archer.Get())
		{
			TheirArrows += A->GetBowComponent() ? A->GetBowComponent()->GetArrowsLoosed() : 0;
		}
	}
	const int32 ArrowsUsed = Kate->GetBowComponent() ? Kate->GetBowComponent()->GetArrowsLoosed() - ArrowsAtStart : 0;
	// Where her arrows ended up, for tuning: stuck in what, how far from her.
	TMap<FString, int32> StuckIn;
	for (TActorIterator<AArrowProjectile> It(World); It; ++It)
	{
		if (It->GetOwner() == Kate && It->IsStuck())
		{
			const AActor* In = It->GetStuckInActor();
			StuckIn.FindOrAdd(FString::Printf(TEXT("%s@%.0fcm z%+.0f"), In ? *In->GetClass()->GetName() : TEXT("nothing"),
				FVector::Dist2D(It->GetActorLocation(), Kate->GetActorLocation()) / 100.f * 100.f,
				It->GetActorLocation().Z - Kate->GetActorLocation().Z))++;
		}
	}
	for (const TPair<FString, int32>& Entry : StuckIn)
	{
		Test->AddInfo(FString::Printf(TEXT("Archer duel: %d of her arrows stuck in %s"), Entry.Value, *Entry.Key));
	}
	const double Seconds = DoneAt - FightStart;
	const bool bWon = Down == Archers.Num() && Archers.Num() == 2 && Health > 0.f && Seconds <= 60.0;
	const FString Json = FString::Printf(TEXT(
		"{\n  \"test\": \"Hawkeye.Lap.ArcherDuel\",\n  \"difficulty\": \"%s\",\n  \"won\": %s,\n  \"end\": \"%s\",\n  \"seconds\": %.2f,\n"
		"  \"kate_health\": %.1f,\n  \"hits_taken\": %d,\n  \"damage_taken\": %.1f,\n  \"archers_down\": %d,\n"
		"  \"arrows_used\": %d,\n  \"archer_arrows\": %d,\n  \"pops\": %d,\n  \"aborted_pops\": %d,\n  \"cover_moves\": %d,\n"
		"  \"archer_holds\": %d,\n  \"hold_shots\": %d,\n  \"hold_guess_seconds\": %.2f,\n  \"cover\": \"%s\",\n  \"average_frame_ms\": %.2f\n}\n"),
		*HawkeyeLap::DifficultyName(Kate), bWon ? TEXT("true") : TEXT("false"), *Why, Seconds, Health, HitsTaken, DamageTaken, Down, ArrowsUsed, TheirArrows,
		Pops, Aborts, Moves, Holds, HoldShots, HoldGuessSeconds, *CoverNote, Meter.AverageMs());
	const FString WithFights = HawkeyeLapPressure::Finish(Test, TEXT("ArcherDuel"), Json);
	HawkeyeLap::WriteText(TEXT("lap_archer_duel.json"), WithFights);
	HawkeyeLap::WriteText(FString::Printf(TEXT("lap_archer_duel_%s.json"), *HawkeyeLap::DifficultyName(Kate).ToLower()), WithFights);
	Test->AddInfo(TEXT("lap_archer_duel.json:\n") + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] archer duel: %s"), *Json);
	if (!bWon)
	{
		Test->AddError(FString::Printf(TEXT("Kate did not win the archer duel (%s): health %.0f, %d of 2 archers down in %.1f s."),
			*Why, Health, Down, Seconds));
	}
	HawkeyeLap::SetThugsThinking(World, NAME_None, true);
}

bool FHawkeyeArcherDuelRunner::Update()
{
	using namespace HawkeyeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		Test->AddError(TEXT("No game world or no Kate for the archer duel."));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	if (bDone)
	{
		return Now - DoneAt > 1.0;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();

	if (!bSetUp)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(ArcherPairTag) && !IsDown(*It))
			{
				Archers.Add(*It);
			}
		}
		AActor* Beacon = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(FName(TEXT("CityBeacon"))) && It->Tags.Contains(FName(TEXT("objective:find_arrow"))))
			{
				Beacon = *It;
				break;
			}
		}
		if (Archers.Num() != 2 || !Beacon)
		{
			Test->AddError(FString::Printf(TEXT("Expected the two ArcherPair archers and the find_arrow beacon, found %d and %s."),
				Archers.Num(), Beacon ? TEXT("the beacon") : TEXT("no beacon")));
			return true;
		}
		SetThugsThinking(World, NAME_None, false);
		EnsureQuiver(Test, Kate->GetInventoryComponent());
		Kate->GetInventoryComponent()->SelectArrowSlot(1);
		FVector RoofPoint;
		AActor* Roof = nullptr;
		FindGround(World, Beacon->GetActorLocation(), Beacon->GetActorLocation().Z + 300.f, Kate, RoofPoint, &Roof);
		// Her arrows leave from the bow hand (no draw animation: it stays near her hip), not her eyes.
		if (const UBowComponent* KateBow = Kate->GetBowComponent())
		{
			const float Feet = Kate->GetActorLocation().Z - Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			HandHeight = FMath::Clamp(static_cast<float>(KateBow->GetArrowSpawnLocation().Z - Feet), 60.f, 170.f);
		}
		RoofActor = Roof;
		if (!Roof || !FindCover(World, Kate, RoofPoint, Roof))
		{
			Test->AddError(TEXT("No cover on the find_arrow roof from both archers."));
			return true;
		}
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Mid = (Archers[0]->GetActorLocation() + Archers[1]->GetActorLocation()) * 0.5f;
		const float Yaw = (Mid - Cover).Rotation().Yaw;
		Kate->TeleportTo(Cover + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(-4.f, Yaw, 0.f));
		PC->SetViewTarget(Kate);
		KateHealth->SetInvulnerable(false);
		KateHealth->Heal(1000.f);
		LastHealth = KateHealth->GetCurrentHealth();
		ArrowsAtStart = Kate->GetBowComponent() ? Kate->GetBowComponent()->GetArrowsLoosed() : 0;
		Crouch(PC, true);
		// Both archers on their own sight: aggressive within 30 m of her and with a line to her.
		SetThugsThinking(World, ArcherPairTag, true);
		Test->AddInfo(FString::Printf(TEXT("Archer duel: Kate at %s (%s); archers at %.0f and %.0f cm."), *Cover.ToCompactString(),
			*CoverNote, FVector::Dist2D(Archers[0]->GetActorLocation(), Cover), FVector::Dist2D(Archers[1]->GetActorLocation(), Cover)));
		bSetUp = true;
		FightStart = Now;
		PhaseSince = Now;
		return false;
	}

	Meter.Frame(World->GetDeltaSeconds());
	const float Health = KateHealth->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++HitsTaken;
		DamageTaken += LastHealth - Health;
		Test->AddInfo(FString::Printf(TEXT("Archer duel %.1f s: Kate hit for %.0f, health %.0f"), Now - FightStart, LastHealth - Health, Health));
	}
	// The holds: when one starts, and whether it ended in a shot.
	for (const TWeakObjectPtr<AThugCharacter>& Archer : Archers)
	{
		const AThugAIController* Brain = Archer.IsValid() ? Cast<AThugAIController>(Archer->GetController()) : nullptr;
		const bool bHolding = Brain && Brain->IsHoldingDraw();
		bool& bWas = WasHolding.FindOrAdd(Archer);
		int32& Loosed = LoosedBefore.FindOrAdd(Archer);
		if (bHolding && !bWas)
		{
			++Holds;
			Loosed = Brain->GetArrowsLoosed();
		}
		else if (!bHolding && bWas && Brain && Brain->GetArrowsLoosed() > Loosed)
		{
			++HoldShots;
			bLearnedHolds = true;
			Test->AddInfo(FString::Printf(TEXT("Archer duel %.1f s: %s loosed on her out of a held draw; she waits the glints out from now on."),
				Now - FightStart, *Archer->GetName()));
		}
		bWas = bHolding;
	}
	LastHealth = Health;
	if (!KateHealth->IsAlive() || Kate->IsDowned())
	{
		Finish(World, PC, Kate, TEXT("Kate went down"));
		return false;
	}
	if (Now - FightStart > 60.0)
	{
		Finish(World, PC, Kate, TEXT("timed out"));
		return false;
	}

	// The weakest archer she can see from standing.
	// Her chest once she stands, wherever she is now (the capsule is shorter while she crouches).
	const float FeetZ = Kate->GetActorLocation().Z - Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Standing(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, FeetZ + HandHeight);
	AThugCharacter* Pick = nullptr;
	float PickHeight = 30.f;
	float Danger = -1.f;
	bool bAnyHold = false;
	int32 Alive = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Archer : Archers)
	{
		AThugCharacter* A = Archer.Get();
		if (IsDown(A))
		{
			continue;
		}
		++Alive;
		const AThugAIController* ABrain = Cast<AThugAIController>(A->GetController());
		if (ABrain && ABrain->IsHoldingDraw())
		{
			bAnyHold = true;
		}
		else
		{
			Danger = FMath::Max(Danger, DrawOf(A));
		}
		// The chest if it shows over his parapet, else his head.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DuelAim), false, Kate);
		Params.AddIgnoredActor(A);
		float Height = -1.f;
		for (const float Try : { 30.f, 55.f, 70.f })
		{
			if (!World->LineTraceTestByChannel(Standing, A->GetActorLocation() + FVector(0.f, 0.f, Try), ECC_Visibility, Params))
			{
				Height = Try;
				break;
			}
		}
		if (Height >= 0.f && (!Pick || A->GetHealthComponent()->GetCurrentHealth() < Pick->GetHealthComponent()->GetCurrentHealth()))
		{
			Pick = A;
			PickHeight = Height;
		}
	}
	if (Alive == 0)
	{
		Finish(World, PC, Kate, TEXT("both archers down"));
		return false;
	}

	UBowComponent* Bow = Kate->GetBowComponent();
	if (Now - LastStatus >= 3.0)
	{
		LastStatus = Now;
		Test->AddInfo(FString::Printf(TEXT("Archer duel %.1f s: phase %d, crouched %d, target in sight %s, worst draw %.2f s, alive %d."),
			Now - FightStart, static_cast<int32>(Phase), Kate->bIsCrouched ? 1 : 0, *GetNameSafe(Pick), Danger, Alive));
	}
	switch (Phase)
	{
	case EPhase::Hidden:
		Crouch(PC, true);
		NoShotSince = Pick ? -1.0 : (NoShotSince < 0.0 ? Now : NoShotSince);
		if (NoShotSince >= 0.0 && Now - NoShotSince > 1.0 && RoofActor.IsValid())
		{
			// Nobody left in reach from here: new cover that reaches the rest, walked to crouched.
			const FVector Feet = Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			if (FindCover(World, Kate, Feet, RoofActor.Get()) && FVector::Dist2D(Cover, Feet) > 40.f)
			{
				Test->AddInfo(FString::Printf(TEXT("Archer duel %.1f s: moving to new cover %s (%s)."), Now - FightStart,
					*Cover.ToCompactString(), *CoverNote));
				Phase = EPhase::Moving;
				PhaseSince = Now;
				++Moves;
			}
			NoShotSince = Now;
		}
		// Up only when nobody is more than a moment into a draw (her 0.8 s draw beats his 1.2 s) and any held
		// draw should, by her clock, have been let down.
		if (Now - PhaseSince >= 0.5 && Kate->bIsCrouched && Pick && Danger < 0.25f
			&& (!bAnyHold || (!bLearnedHolds && Now - DuckedAt >= HoldGuessSeconds)))
		{
			Target = Pick;
			TargetHeight = PickHeight;
			Crouch(PC, false);
			Phase = EPhase::Rising;
			PhaseSince = Now;
			++Pops;
		}
		break;
	case EPhase::Rising:
		Crouch(PC, false);
		if (Target.IsValid())
		{
			AimAt(PC, Kate, Target->GetActorLocation() + FVector(0.f, 0.f, TargetHeight));
		}
		if (Now - PhaseSince >= 0.2 && !Kate->bIsCrouched)
		{
			Hold(PC, FirePath, true);
			Phase = EPhase::Drawing;
			PhaseSince = Now;
		}
		break;
	case EPhase::Drawing:
	{
		AThugCharacter* A = Target.Get();
		if (A && Bow)
		{
			// Where the arrow will meet him, dropping over the flight.
			const FVector Chest = A->GetActorLocation() + FVector(0.f, 0.f, TargetHeight);
			const FVector Aim = AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Chest, A->GetVelocity(),
				6000.f, World->GetGravityZ());
			Bow->SetAimOverride(Aim);
			AimAt(PC, Kate, Aim);
		}
		const bool bThreat = Danger >= 0.9f;
		if (Now - PhaseSince >= 0.82 || bThreat || IsDown(A))
		{
			Hold(PC, FirePath, false);
			Aborts += (bThreat && Now - PhaseSince < 0.82) ? 1 : 0;
			Crouch(PC, true);
			Phase = EPhase::Hidden;
			PhaseSince = Now;
			DuckedAt = Now;
		}
		break;
	}
	case EPhase::Moving:
	{
		Crouch(PC, true);
		const FVector To = Cover - Kate->GetActorLocation();
		if (To.Size2D() <= 40.f || Now - PhaseSince > 8.0)
		{
			Move(PC, FVector2D::ZeroVector);
			Phase = EPhase::Hidden;
			PhaseSince = Now;
			break;
		}
		if (Kate->bIsCrouched)
		{
			Move(PC, MoveTowards(To.GetSafeNormal2D(), PC->GetControlRotation().Yaw));
		}
		break;
	}
	}
	return false;
}

// --- The street fight ----------------------------------------------------------------------------

/**
 * Hawkeye.Lap.StreetFight: Kate on the Avenue A sidewalk against the StreetGroup (the bat, the gunner
 * and the heavy), with fists, heavy strikes, standard arrows and two trick arrows (a bola and putty,
 * both for the heavy). She starts between the pair and the heavy's patrol, so the heavy arrives last.
 *
 * What she does, in order of urgency: dodge a swing whose telegraph is about to land; break a gunner's
 * telegraph with a quick shot (any hit breaks a burst); then fight the nearest threat: the bat with
 * lights and heavies, the heavy with the bola (then lights while he is down), putty (then lights from
 * behind while he is held) and heavy strikes (then lights while his guard is broken), the gunner with
 * full-draw arrows. Must win inside 120 s with health above 0.
 *
 * Writes Saved/Automation/lap_street_fight.json: time, hits taken, arrows used, dodges, the annoyance
 * (seconds and share of the fight Kate was staggered or down; the target is under 15%) and the hits
 * that landed with no telegraph from their attacker in the second before (must be 0).
 */
class FHawkeyeStreetFightRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeStreetFightRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	FAutomationTestBase* Test;
	bool bSetUp = false;
	bool bDone = false;
	double FightStart = -1.0;
	double DoneAt = -1.0;
	double NextActionAt = 0.0;
	double LastStatus = -10.0;
	float LastHealth = 0.f;
	float DamageTaken = 0.f;
	int32 ArrowsAtStart = 0;
	int32 LightSwings = 0;
	int32 HeavySwings = 0;
	int32 Dodges = 0;
	int32 QuickShots = 0;
	int32 FullShots = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 MaxCombo = 0;
	double LastParryTap = -10.0;
	TArray<FString> TrickArrows;
	bool bBolaUsed = false;
	bool bPuttyUsed = false;
	bool bMoveInjecting = false;
	bool bMeleeHeld = false;
	double MeleeHeldSince = 0.0;
	bool bDrawing = false;
	double DrawStart = 0.0;
	float DrawFor = 0.82f;
	int32 DrawSlot = 1;
	TWeakObjectPtr<AThugCharacter> DrawTarget;
	FHawkeyeFightMetrics Metrics;
	HawkeyeLap::FMeter Meter;
	TStrongObjectPtr<UHawkeyeTestListener> Listener;
	TArray<TWeakObjectPtr<AThugCharacter>> Group;

	static bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || Thug->GetHealthComponent()->IsDead();
	}

	static bool IsMeleeThug(const AThugCharacter* Thug)
	{
		return Thug && !Thug->IsGunner() && !Thug->IsArcher();
	}

	/** A clear line from Kate's chest to his. */
	static bool Sees(UWorld* World, const AHawkeyeCharacter* Kate, const AThugCharacter* Thug)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(StreetSight), false, Kate);
		Params.AddIgnoredActor(Thug);
		return !World->LineTraceTestByChannel(Kate->GetActorLocation() + FVector(0.f, 0.f, 40.f),
			Thug->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params);
	}

	void Move(APlayerController* PC, const FVector2D& Value)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeLap::InputOf(PC);
		const UInputAction* A = HawkeyeLap::Action(HawkeyeLap::MovePath);
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
		}
		else
		{
			Input->UpdateValueOfContinuousInputInjectionForAction(A, FInputActionValue(Value));
		}
	}

	void MoveTo(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Goal)
	{
		const FVector To = (Goal - Kate->GetActorLocation()).GetSafeNormal2D();
		Move(PC, HawkeyeLap::MoveTowards(To, PC->GetControlRotation().Yaw));
	}

	/** Starts a draw of Slot at Target, released after Seconds. */
	void BeginDraw(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, int32 Slot, float Seconds, double Now)
	{
		Move(PC, FVector2D::ZeroVector);
		Kate->GetInventoryComponent()->SelectArrowSlot(Slot);
		DrawSlot = Slot;
		DrawTarget = Target;
		DrawFor = Seconds;
		DrawStart = Now;
		bDrawing = true;
		HawkeyeLap::Hold(PC, HawkeyeLap::FirePath, true);
	}

	void NoteTelegraphs(double Now);
	void NoteHits(double Now);
	/** Dodges a swing about to land; true when she did. */
	bool TryDodgeSwings(AHawkeyeCharacter* Kate, double Now);
	void FightMelee(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, float Distance, double Now);
	void FightHeavy(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Heavy, float Distance, double Now);
	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why);
};

void FHawkeyeStreetFightRunner::NoteTelegraphs(double Now)
{
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Group)
	{
		const AThugCharacter* Thug = Weak.Get();
		if (IsDown(Thug))
		{
			continue;
		}
		const AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
		const bool bSwing = Thug->GetMeleeComponent() && Thug->GetMeleeComponent()->IsWindingUp();
		const bool bRaised = Brain && Thug->IsGunner() && Brain->IsTelegraphing();
		if (bSwing || bRaised)
		{
			Metrics.NoteTelegraph(Thug->GetName(), Now);
		}
	}
}

void FHawkeyeStreetFightRunner::NoteHits(double Now)
{
	if (!Listener.IsValid())
	{
		return;
	}
	for (const TObjectPtr<AActor>& By : Listener->DamageInstigators)
	{
		const FString Name = GetNameSafe(By.Get());
		if (!Metrics.NoteDamage(Name, Now))
		{
			Test->AddWarning(FString::Printf(TEXT("Street fight %.1f s: hit by %s with no telegraph in the second before."),
				Now - FightStart, *Name));
		}
	}
	Listener->DamageInstigators.Reset();
}

bool FHawkeyeStreetFightRunner::TryDodgeSwings(AHawkeyeCharacter* Kate, double Now)
{
	if (Kate->GetDodgeCooldownRemaining() > 0.f)
	{
		return false;
	}
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Group)
	{
		const AThugCharacter* Thug = Weak.Get();
		if (IsDown(Thug) || !IsMeleeThug(Thug) || Thug->IsIncapacitated())
		{
			continue;
		}
		const UMeleeComponent* Melee = Thug->GetMeleeComponent();
		const float Reach = Melee ? Melee->GetCurrentAttack().Range + 110.f : 0.f;
		const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
		if (!Melee || !Melee->IsWindingUp() || Melee->GetPhaseRemaining() > 0.3f || D > Reach)
		{
			continue;
		}
		// Sideways and back: out of the sweep, not out of reach of her own next swing.
		const FVector Away = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
		if (Kate->TryDodge((Away + FVector::CrossProduct(FVector::UpVector, Away)).GetSafeNormal2D()))
		{
			++Dodges;
			NextActionAt = Now + 0.3;
			return true;
		}
	}
	return false;
}

void FHawkeyeStreetFightRunner::FightMelee(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target,
	float Distance, double Now)
{
	using namespace HawkeyeLap;
	if (Distance > 190.f)
	{
		MoveTo(PC, Kate, Target->GetActorLocation());
		return;
	}
	Move(PC, FVector2D::ZeroVector);
	const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
	int32 Near = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Other : Group)
	{
		Near += !IsDown(Other.Get()) && FVector::Dist2D(Other->GetActorLocation(), Kate->GetActorLocation()) <= 400.f ? 1 : 0;
	}
	// A light lands in 0.1 s and cancels a wind-up; the heavy only on one who is alone and not swinging.
	if ((ThugMelee && ThugMelee->IsWindingUp()) || Target->IsIncapacitated() || Near > 1)
	{
		Tap(PC, MeleePath);
		++LightSwings;
		NextActionAt = Now + 0.32;
		return;
	}
	Hold(PC, MeleePath, true);
	bMeleeHeld = true;
	MeleeHeldSince = Now;
	++HeavySwings;
}

void FHawkeyeStreetFightRunner::FightHeavy(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Heavy,
	float Distance, double Now)
{
	using namespace HawkeyeLap;
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const AThugAIController* Brain = Cast<AThugAIController>(Heavy->GetController());
	const bool bHeld = Brain && Brain->IsHeld();
	const bool bOpen = Heavy->IsIncapacitated();
	// The bola first, from range, while he walks in behind the shield.
	if (!bBolaUsed && !bOpen && Distance >= 350.f && Distance <= 1800.f && Inventory->GetArrowCount(4) > 0 && Sees(World, Kate, Heavy))
	{
		bBolaUsed = true;
		TrickArrows.Add(TEXT("bola"));
		BeginDraw(PC, Kate, Heavy, 4, 0.82f, Now);
		return;
	}
	// Putty once the bola has been spent and he is up again, from a step or two back.
	if (bBolaUsed && !bPuttyUsed && !bOpen && !bHeld && Distance >= 300.f && Distance <= 1500.f && Inventory->GetArrowCount(3) > 0
		&& Sees(World, Kate, Heavy))
	{
		bPuttyUsed = true;
		TrickArrows.Add(TEXT("putty"));
		BeginDraw(PC, Kate, Heavy, 3, 0.82f, Now);
		return;
	}
	if (bBolaUsed && !bPuttyUsed && !bOpen && !bHeld && Distance < 300.f)
	{
		// Step back to make room for the putty shot.
		MoveTo(PC, Kate, Kate->GetActorLocation() + (Kate->GetActorLocation() - Heavy->GetActorLocation()).GetSafeNormal2D() * 200.f);
		return;
	}
	// Held: round behind him (he cannot turn), then lights into his back.
	if (bHeld && !bOpen)
	{
		const FVector Behind = Heavy->GetActorLocation() - Heavy->GetActorForwardVector().GetSafeNormal2D() * 120.f;
		const bool bIsBehind = !Heavy->BlocksHitFrom(Kate->GetActorLocation() - Heavy->GetActorLocation());
		if (!bIsBehind || (FVector::Dist2D(Kate->GetActorLocation(), Behind) > 90.f && Distance > 170.f))
		{
			// Wide round his side, so she never walks into the shield face.
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Heavy->GetActorForwardVector()).GetSafeNormal2D();
			const FVector Waypoint = bIsBehind ? Behind : Heavy->GetActorLocation() + Side * 170.f;
			MoveTo(PC, Kate, Waypoint);
			return;
		}
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, MeleePath);
		++LightSwings;
		NextActionAt = Now + 0.32;
		return;
	}
	if (Distance > 190.f)
	{
		MoveTo(PC, Kate, Heavy->GetActorLocation());
		return;
	}
	Move(PC, FVector2D::ZeroVector);
	if (bOpen)
	{
		// Down, staggered or getting up: the shield is down; lights land.
		Tap(PC, MeleePath);
		++LightSwings;
		NextActionAt = Now + 0.32;
		return;
	}
	// Standing behind the shield: only a heavy gets through, and only when his own swing is not about to land.
	const UMeleeComponent* Melee = Heavy->GetMeleeComponent();
	if (Melee && Melee->IsWindingUp())
	{
		return;
	}
	Hold(PC, MeleePath, true);
	bMeleeHeld = true;
	MeleeHeldSince = Now;
	++HeavySwings;
}

void FHawkeyeStreetFightRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why)
{
	Move(PC, FVector2D::ZeroVector);
	HawkeyeLap::Hold(PC, HawkeyeLap::FirePath, false);
	HawkeyeLap::Hold(PC, HawkeyeLap::MeleePath, false);
	if (UBowComponent* Bow = Kate->GetBowComponent())
	{
		Bow->ClearAimOverride();
	}
	Kate->GetInventoryComponent()->SelectArrowSlot(1);
	if (Listener.IsValid())
	{
		Kate->GetHealthComponent()->OnHealthChanged.RemoveDynamic(Listener.Get(), &UHawkeyeTestListener::HandleHealthChanged);
	}
	bDone = true;
	DoneAt = World->GetTimeSeconds();
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	int32 Down = 0;
	FString Blocks;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Group)
	{
		Down += IsDown(Thug.Get()) ? 1 : 0;
		if (const AThugCharacter* T = Thug.Get(); T && T->IsHeavy())
		{
			Blocks = FString::Printf(TEXT("%d"), T->GetBlockCount());
		}
	}
	const double Seconds = DoneAt - FightStart;
	const int32 ArrowsUsed = Kate->GetBowComponent() ? Kate->GetBowComponent()->GetArrowsLoosed() - ArrowsAtStart : 0;
	const float Annoyance = Metrics.GetAnnoyanceFraction();
	const bool bWon = Down == Group.Num() && Group.Num() == 3 && Health > 0.f && Seconds <= 120.0;
	FString Untelegraphed;
	for (const FString& Note : Metrics.GetUntelegraphedNotes())
	{
		Untelegraphed += (Untelegraphed.IsEmpty() ? TEXT("") : TEXT("; ")) + Note;
	}
	const FString Json = FString::Printf(TEXT(
		"{\n  \"test\": \"Hawkeye.Lap.StreetFight\",\n  \"difficulty\": \"%s\",\n  \"won\": %s,\n  \"end\": \"%s\",\n  \"seconds\": %.2f,\n"
		"  \"kate_health\": %.1f,\n  \"hits_taken\": %d,\n  \"damage_taken\": %.1f,\n  \"thugs_down\": %d,\n"
		"  \"arrows_used\": %d,\n  \"full_draw_shots\": %d,\n  \"quick_shots\": %d,\n  \"trick_arrows\": \"%s\",\n"
		"  \"light_swings\": %d,\n  \"heavy_swings\": %d,\n  \"dodges\": %d,\n  \"parries\": %d,\n"
		"  \"finishers\": %d,\n  \"perfect_dodges\": %d,\n  \"max_combo\": %d,\n  \"shield_blocks\": %s,\n"
		"  \"staggered_or_down_seconds\": %.2f,\n  \"annoyance_percent\": %.1f,\n  \"untelegraphed_hits\": %d,\n"
		"  \"untelegraphed_detail\": \"%s\",\n  \"average_frame_ms\": %.2f\n}\n"),
		*HawkeyeLap::DifficultyName(Kate), bWon ? TEXT("true") : TEXT("false"), *Why, Seconds, Health, Metrics.GetHits(), DamageTaken, Down, ArrowsUsed,
		FullShots, QuickShots, *FString::Join(TrickArrows, TEXT(", ")), LightSwings, HeavySwings, Dodges, Parries, Finishers,
		Kate->GetPerfectDodgeCount(), MaxCombo, Blocks.IsEmpty() ? TEXT("0") : *Blocks, Metrics.GetDisabledSeconds(), Annoyance * 100.f, Metrics.GetUntelegraphedHits(),
		*Untelegraphed, Meter.AverageMs());
	const FString WithFights = HawkeyeLapPressure::Finish(Test, TEXT("StreetFight"), Json);
	HawkeyeLap::WriteText(TEXT("lap_street_fight.json"), WithFights);
	HawkeyeLap::WriteText(FString::Printf(TEXT("lap_street_fight_%s.json"), *HawkeyeLap::DifficultyName(Kate).ToLower()), WithFights);
	Test->AddInfo(TEXT("lap_street_fight.json:\n") + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] street fight: %s"), *Json);
	if (!bWon)
	{
		Test->AddError(FString::Printf(TEXT("Kate did not win the street fight (%s): health %.0f, %d of 3 down in %.1f s."),
			*Why, Health, Down, Seconds));
	}
	if (Metrics.GetUntelegraphedHits() > 0)
	{
		Test->AddError(FString::Printf(TEXT("%d hit(s) landed with no telegraph in the second before: %s"),
			Metrics.GetUntelegraphedHits(), *Untelegraphed));
	}
	if (Annoyance >= 0.15f)
	{
		Test->AddWarning(FString::Printf(TEXT("Kate spent %.0f%% of the fight staggered or down; the target is under 15%%."),
			Annoyance * 100.f));
	}
	HawkeyeLap::SetThugsThinking(World, NAME_None, true);
}

bool FHawkeyeStreetFightRunner::Update()
{
	using namespace HawkeyeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		Test->AddError(TEXT("No game world or no Kate for the street fight."));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	if (bDone)
	{
		return Now - DoneAt > 1.0;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();

	if (!bSetUp)
	{
		AThugCharacter* HeavyThug = nullptr;
		FVector PairMid = FVector::ZeroVector;
		int32 PairCount = 0;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(StreetGroupTag) && !IsDown(*It))
			{
				Group.Add(*It);
				if (It->IsHeavy())
				{
					HeavyThug = *It;
				}
				else
				{
					PairMid += It->GetActorLocation();
					++PairCount;
				}
			}
		}
		if (Group.Num() != 3 || !HeavyThug || PairCount != 2)
		{
			Test->AddError(FString::Printf(TEXT("Expected the three StreetGroup thugs (bat, gunner, heavy), found %d."), Group.Num()));
			return true;
		}
		PairMid /= PairCount;
		SetThugsThinking(World, NAME_None, false);
		// Kate alone: Clint's arrows would muddy who hit whom (and one of his once hit her).
		HawkeyeFreezePartner(World);
		UInventoryComponent* Inventory = Kate->GetInventoryComponent();
		EnsureQuiver(Test, Inventory);
		for (const TPair<int32, const TCHAR*>& Trick : { TPair<int32, const TCHAR*>(3, PuttyAssetPath), TPair<int32, const TCHAR*>(4, BolaAssetPath) })
		{
			if (Inventory->GetArrowCount(Trick.Key) <= 0)
			{
				Inventory->AddArrows(LoadObject<UArrowDefinition>(nullptr, Trick.Value), 1);
			}
		}
		Inventory->SelectArrowSlot(1);
		// On the sidewalk 9 m from the pair toward the heavy: the pair first, the heavy some seconds later.
		const FVector Toward = (HeavyThug->GetActorLocation() - PairMid).GetSafeNormal2D();
		FVector Ground;
		if (!FindGround(World, PairMid + Toward * 900.f, PairMid.Z + 300.f, Kate, Ground))
		{
			Test->AddError(TEXT("No sidewalk between the pair and the heavy."));
			return true;
		}
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Yaw = (-Toward).Rotation().Yaw;
		Kate->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(-8.f, Yaw, 0.f));
		PC->SetViewTarget(Kate);
		KateHealth->SetInvulnerable(false);
		KateHealth->Heal(1000.f);
		LastHealth = KateHealth->GetCurrentHealth();
		ArrowsAtStart = Kate->GetBowComponent() ? Kate->GetBowComponent()->GetArrowsLoosed() : 0;
		Listener.Reset(NewObject<UHawkeyeTestListener>());
		KateHealth->OnHealthChanged.AddDynamic(Listener.Get(), &UHawkeyeTestListener::HandleHealthChanged);
		SetThugsThinking(World, StreetGroupTag, true);
		for (const TWeakObjectPtr<AThugCharacter>& Thug : Group)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
			{
				Brain->SetTarget(Kate);
				Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			}
		}
		Test->AddInfo(FString::Printf(TEXT("Street fight: Kate at %s, pair %.0f cm away, heavy %.0f cm away, health %.0f."),
			*Kate->GetActorLocation().ToCompactString(), FVector::Dist2D(PairMid, Kate->GetActorLocation()),
			FVector::Dist2D(HeavyThug->GetActorLocation(), Kate->GetActorLocation()), LastHealth));
		bSetUp = true;
		FightStart = Now;
		return false;
	}

	const float DeltaSeconds = World->GetDeltaSeconds();
	Meter.Frame(DeltaSeconds);
	Metrics.Tick(DeltaSeconds, Kate->IsStaggered() || Kate->IsDowned());
	NoteTelegraphs(Now);
	NoteHits(Now);
	const float Health = KateHealth->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		DamageTaken += LastHealth - Health;
		Test->AddInfo(FString::Printf(TEXT("Street fight %.1f s: Kate hit for %.0f, health %.0f"), Now - FightStart, LastHealth - Health, Health));
	}
	LastHealth = Health;
	if (!KateHealth->IsAlive() || Kate->IsDowned())
	{
		Finish(World, PC, Kate, TEXT("Kate went down"));
		return false;
	}
	if (Now - FightStart > 120.0)
	{
		Finish(World, PC, Kate, TEXT("timed out"));
		return false;
	}

	AThugCharacter* Nearest = nullptr;
	AThugCharacter* Gunner = nullptr;
	float Distance = BIG_NUMBER;
	int32 Alive = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Group)
	{
		AThugCharacter* Thug = Weak.Get();
		if (IsDown(Thug))
		{
			continue;
		}
		++Alive;
		Gunner = Thug->IsGunner() ? Thug : Gunner;
		const float D = FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation());
		if (D < Distance)
		{
			Distance = D;
			Nearest = Thug;
		}
	}
	if (Alive == 0)
	{
		Finish(World, PC, Kate, TEXT("all three down"));
		return false;
	}
	if (Now - LastStatus >= 3.0)
	{
		LastStatus = Now;
		FString States;
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Group)
		{
			if (const AThugCharacter* T = Weak.Get())
			{
				States += FString::Printf(TEXT("%s %.0f hp %.0f cm; "), *T->GetName(), T->GetHealthComponent()->GetCurrentHealth(),
					FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()));
			}
		}
		Test->AddInfo(FString::Printf(TEXT("Street fight %.1f s: health %.0f; %s"), Now - FightStart, Health, *States));
	}

	// Look at the one she is dealing with.
	AThugCharacter* Focus = bDrawing && DrawTarget.IsValid() && !IsDown(DrawTarget.Get()) ? DrawTarget.Get() : Nearest;
	AimAt(PC, Kate, Focus->GetActorLocation() + FVector(0.f, 0.f, 30.f));

	if (bMeleeHeld)
	{
		if (Now - MeleeHeldSince >= 0.45)
		{
			Hold(PC, MeleePath, false);
			bMeleeHeld = false;
			NextActionAt = Now + 0.1;
		}
		Move(PC, FVector2D::ZeroVector);
		return false;
	}
	if (bDrawing)
	{
		AThugCharacter* Target = DrawTarget.Get();
		UBowComponent* Bow = Kate->GetBowComponent();
		if (Target && Bow)
		{
			const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
			Bow->SetAimOverride(AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Chest, Target->GetVelocity(),
				6000.f * FMath::Max(Bow->GetDrawFraction(), 0.4f), World->GetGravityZ()));
		}
		// A swing about to land cuts the draw short.
		bool bThreat = false;
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Group)
		{
			const AThugCharacter* T = Weak.Get();
			bThreat = bThreat || (!IsDown(T) && IsMeleeThug(T) && T->GetMeleeComponent()->IsWindingUp()
				&& T->GetMeleeComponent()->GetPhaseRemaining() < 0.3f
				&& FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) < 260.f);
		}
		if (Now - DrawStart >= DrawFor || IsDown(Target) || (bThreat && Now - DrawStart >= 0.25))
		{
			Hold(PC, FirePath, false);
			bDrawing = false;
			NextActionAt = Now + 0.15;
			if (Bow)
			{
				Bow->ClearAimOverride();
			}
			Kate->GetInventoryComponent()->SelectArrowSlot(1);
		}
		else if (bThreat)
		{
			// Too early to loose: let it down and get out.
			Kate->GetBowComponent()->CancelDraw();
			Hold(PC, FirePath, false);
			bDrawing = false;
		}
		return false;
	}

	MaxCombo = FMath::Max(MaxCombo, Kate->GetComboCount());
	// A wind-up (or a raised pistol) in front of her: a tap of V parries it, even out of her own recovery.
	if (Now - LastParryTap > 0.3 && CanParryNow(Kate) && ParryChance(World, Kate, PC))
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, MeleePath);
		LastParryTap = Now;
		++Parries;
		NextActionAt = Now + 0.25;
		return false;
	}
	const bool bBusy = Kate->IsMeleeAttacking() || Kate->IsDodging() || Kate->IsStaggered() || Kate->IsLockedOutByTakedown();
	if (bBusy || Now < NextActionAt)
	{
		if (!bBusy && Now < NextActionAt)
		{
			TryDodgeSwings(Kate, Now);
		}
		return false;
	}
	if (TryDodgeSwings(Kate, Now))
	{
		return false;
	}
	// Anyone open to a finisher within reach (down, parried, or just hit by a combo ender): F finishes him.
	if (CanFinish(Kate))
	{
		Move(PC, FVector2D::ZeroVector);
		Tap(PC, TakedownPath);
		++Finishers;
		NextActionAt = Now + 0.3;
		return false;
	}

	// The gunner raising his pistol with a line on her: a quick shot breaks the burst.
	const AThugAIController* GunBrain = Gunner ? Cast<AThugAIController>(Gunner->GetController()) : nullptr;
	const bool bMeleeClose = Nearest && IsMeleeThug(Nearest) && Distance < 300.f;
	if (GunBrain && GunBrain->IsTelegraphing() && !bMeleeClose && Sees(World, Kate, Gunner)
		&& FVector::Dist2D(Gunner->GetActorLocation(), Kate->GetActorLocation()) < 2500.f)
	{
		++QuickShots;
		BeginDraw(PC, Kate, Gunner, 1, 0.3f, Now);
		return false;
	}

	// The nearest melee thug within 7 m, else the gunner, else whoever is nearest.
	AThugCharacter* Target = (Nearest && IsMeleeThug(Nearest) && Distance < 700.f) ? Nearest : (Gunner ? Gunner : Nearest);
	const float TargetDistance = FVector::Dist2D(Target->GetActorLocation(), Kate->GetActorLocation());
	if (Target->IsHeavy())
	{
		FightHeavy(World, PC, Kate, Target, TargetDistance, Now);
		return false;
	}
	if (Target->IsGunner() && TargetDistance > 450.f)
	{
		if (Sees(World, Kate, Target))
		{
			++FullShots;
			BeginDraw(PC, Kate, Target, 1, 0.82f, Now);
		}
		else
		{
			MoveTo(PC, Kate, Target->GetActorLocation());
		}
		return false;
	}
	FightMelee(PC, Kate, Target, TargetDistance, Now);
	return false;
}

// --- Tests --------------------------------------------------------------------------------------

bool FHawkeyeLapEastVillage::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeLapRunner(this));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

bool FHawkeyeLapRoofFight::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the roof fight. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeRoofFightRunner(this));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

bool FHawkeyeLapArcherDuel::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the archer duel. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeArcherDuelRunner(this));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

bool FHawkeyeLapStreetFight::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the street fight. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeStreetFightRunner(this));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
