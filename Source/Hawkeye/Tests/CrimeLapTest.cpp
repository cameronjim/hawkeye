#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
#include "Crime/Civilian.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeLoot.h"
#include "Crime/CrimeRules.h"
#include "Crime/CrimeSpot.h"
#include "Crime/CrimeSubsystem.h"
#include "Crime/CrimeTracker.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/LapBot.h"
#include "Tests/LapFightPressure.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The street crimes played by a script on the district, in the standalone game like the other laps:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap.Crime; Quit"
 *
 * Each lap picks its spot, puts Kate 25 to 40 m from it where no other spot of that type is, and sets
 * hawkeye.CrimeInterval 1 and hawkeye.CrimeType <type>, so the subsystem's own rules start that crime at
 * that spot within a second. The district's placed thugs stop thinking and Clint is frozen; the crime's
 * thugs do not. She fights as a player who has learnt the melee (parries a wind-up she can see, finishes
 * anyone open to it, steps out of a swing about to land) and draws with the aim on the target's chest,
 * led for its speed and the arrow's drop.
 *
 * Hawkeye.Lap.CrimeMugging: the nearest street corner with a mugging, Kate 30 m out on the street with 20
 * standard arrows; she runs in, looses two arrows from 5 to 15 m, then fights with strikes. Both thugs
 * down with the victim alive.
 * Hawkeye.Lap.CrimeRobbery: the nearest corner with a robbery and an escape point. She runs in; the runner
 * sets off with the loot when she is 20 m out; she downs him with a bola (an arrow if the bolas are gone),
 * runs to the dropped loot and walks over it. The others are left standing.
 * Hawkeye.Lap.CrimeAmbush: the nearest alley spot (a corner when the block has none); four thugs alerted
 * from the start. She opens with a bola on the gunner (the nearest when there is none), then fights with
 * strikes and standard arrows at the gunner when he keeps his distance.
 * Hawkeye.Lap.CrimeRooftop: a roof spot with an anchor on its roof that she can grapple to from the street
 * 25 to 40 m from the spot. She aims at the anchor and presses the grapple key, zips up, and fights the two
 * thugs with strikes and the archer with arrows (a quick shot when he is drawing on her).
 *
 * Every lap must complete: the crime stopped, 5 arrows added (or up to the cap), the completion counted
 * and in the campaign state, and the "[Crime stopped]" toast with its "+5 arrows" shown. Each reports the
 * time and the hits taken, writes Saved/Automation/crime_<type>.json and fails with the reason if not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCrimeMugging, "Hawkeye.Lap.CrimeMugging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCrimeRobbery, "Hawkeye.Lap.CrimeRobbery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCrimeAmbush, "Hawkeye.Lap.CrimeAmbush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapCrimeRooftop, "Hawkeye.Lap.CrimeRooftop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeCrimeLap
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	static const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	static const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* BowAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrappleAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");
	static const TCHAR* PuttyAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Putty.DA_Arrow_Putty");
	static const TCHAR* BolaAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Bola.DA_Arrow_Bola");
	static const FName BuildingTag(TEXT("CityBuilding"));

	/** Where she is put, from the spot, and how many arrows she starts with. */
	static constexpr float StartDistance = 3000.f;
	static constexpr int32 StartArrows = 20;
	static constexpr int32 SlotStandard = 1;
	static constexpr int32 SlotPutty = 3;
	static constexpr int32 SlotBola = 4;
	/** The full draw the laps use, s (Kate's bow draws in 0.8 s). */
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

	/** A crime thug in front of her within 240 cm whose telegraph she has seen for 0.2 s. */
	static AThugCharacter* ParryChance(const TArray<TWeakObjectPtr<AThugCharacter>>& Thugs, const AHawkeyeCharacter* Kate,
		const APlayerController* PC)
	{
		const FVector Forward = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
		{
			AThugCharacter* Thug = Weak.Get();
			const EHawkeyeParryKind Kind = Thug ? UHawkeyeMeleeRules::ClassifyParry(Thug) : EHawkeyeParryKind::None;
			if (Kind == EHawkeyeParryKind::None
				|| !UHawkeyeMeleeRules::IsInFrontWithin(Kate->GetActorLocation(), Forward, Thug->GetActorLocation(), 240.f, 65.f))
			{
				continue;
			}
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			if (Kind == EHawkeyeParryKind::Burst || (Melee && Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining() >= 0.2f))
			{
				return Thug;
			}
		}
		return nullptr;
	}

	static bool CanParryNow(const AHawkeyeCharacter* Kate)
	{
		return Kate->GetMeleeComponent() && !Kate->GetMeleeComponent()->IsWindingUp() && !Kate->IsDodging()
			&& !Kate->IsStaggered() && !Kate->IsDrawingBow() && !Kate->IsLockedOutByTakedown();
	}

	/** The way to walk from Kate toward Goal: along the navmesh path when there is one, else straight. */
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

	static bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || Thug->GetHealthComponent()->IsDead();
	}

	/** A clear Visibility line from Kate's chest to his, pawns and the two of them ignored. */
	static bool Sees(UWorld* World, const AHawkeyeCharacter* Kate, const AActor* Target)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeLapSight), false, Kate);
		Params.AddIgnoredActor(Target);
		return !World->LineTraceTestByChannel(Kate->GetActorLocation() + FVector(0.f, 0.f, 40.f),
			Target->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params);
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	static void SetCVar(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(*Value, ECVF_SetByCode);
		}
	}

	/** The saved count of Type in the campaign state, or -1 without one. */
	static int32 SavedCount(const UObject* WorldContext, FName TypeKey)
	{
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(WorldContext);
		const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
		if (!Campaign)
		{
			return -1;
		}
		const int32 Row = Campaign->CrimeTypes.IndexOfByKey(TypeKey);
		return Row != INDEX_NONE && Campaign->CrimeCompletions.IsValidIndex(Row) ? Campaign->CrimeCompletions[Row] : 0;
	}

	/** The building mesh under Point (a trace from 50 cm over it straight down), or null. */
	static AActor* BuildingUnder(UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeLapRoof), false, Ignore);
		if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 50.f), Point - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
			&& Hit.GetActor() && Hit.GetActor()->ActorHasTag(BuildingTag))
		{
			return Hit.GetActor();
		}
		return nullptr;
	}

	static const TCHAR* SpotKind(const ACrimeSpot* Spot)
	{
		return !Spot ? TEXT("none") : Spot->bRooftop ? TEXT("roof") : Spot->bAlley ? TEXT("alley") : TEXT("corner");
	}
}

/** One crime of one type, played to the end. */
class FHawkeyeCrimeLapRunner : public IAutomationLatentCommand
{
public:
	FHawkeyeCrimeLapRunner(FAutomationTestBase* InTest, ECrimeType InType) : Test(InTest), Type(InType)
	{
		TypeName = UCrimeRules::TypeText(Type).ToString();
	}

	virtual bool Update() override;

private:
	enum class ERoofPhase : uint8 { Grapple, Zipping, Fight };

	bool SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);
	bool WantsSpot(const ACrimeSpot* Candidate) const;
	bool OthersInBand(const UCrimeSubsystem* Crimes, const FVector& At) const;
	bool PlaceOnStreet(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& At);
	bool PlaceForGrapple(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, ACrimeSpot* Roof);
	void Teleport(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ground, float Yaw);

	void Fight(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void Robbery(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	void Rooftop(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);

	/** Draws Slot at Target, released after Seconds. */
	void BeginDraw(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, int32 Slot, float Seconds, double Now);
	/** One frame of a draw: the aim held on the target, released on time. True while she is drawing. */
	bool UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now);
	/** A swing about to land beside her from someone other than Except: steps out of it. True when she dodged. */
	bool DodgeSwings(AHawkeyeCharacter* Kate, const AThugCharacter* Except, double Now);
	/** A strike (or a finisher, or a parry) at Target within reach; false when he is out of reach. */
	bool Strike(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, float Distance, double Now);
	void RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint);

	void NoteToast(AHawkeyeCharacter* Kate);
	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& Why);
	void Report(UWorld* World, AHawkeyeCharacter* Kate);
	void Move(APlayerController* PC, const FVector2D& Value);
	void Sprint(APlayerController* PC, bool bOn);

	FAutomationTestBase* Test;
	ECrimeType Type;
	FString TypeName;
	bool bSetUp = false;
	bool bStarted = false;
	bool bDone = false;
	bool bReported = false;
	double SetUpAt = 0.0;
	double StartedAt = 0.0;
	double DoneAt = 0.0;
	double NextActionAt = 0.0;
	double MeleeHeldSince = 0.0;
	double LastParryTap = -10.0;
	double NextReportAt = 0.0;
	bool bMeleeHeld = false;
	bool bMoveInjecting = false;
	bool bSprinting = false;
	float LastHealth = 0.f;
	int32 HitsTaken = 0;
	float DamageTaken = 0.f;
	int32 ArrowsLoosed = 0;
	int32 Strikes = 0;
	int32 Parries = 0;
	int32 Finishers = 0;
	int32 Dodges = 0;
	int32 ArrowsBeforeEnd = 0;
	int32 CountBefore = 0;
	int32 SavedBefore = 0;
	FString OldInterval;
	FString OldType;
	TWeakObjectPtr<ACrimeSpot> Spot;
	TWeakObjectPtr<ACivilian> Victim;
	TArray<TWeakObjectPtr<AThugCharacter>> Thugs;
	FString Why;
	FCrimeResult Result;
	bool bHasResult = false;
	FString Notes;

	// Draws.
	bool bDrawing = false;
	double DrawStart = 0.0;
	float DrawFor = HawkeyeCrimeLap::FullDraw;
	int32 DrawSlot = 1;
	TWeakObjectPtr<AThugCharacter> DrawTarget;
	double SlotResetAt = -1.0;
	TArray<FString> TrickArrows;

	// The toast.
	bool bToastSeen = false;
	FString ToastText;

	// Robbery.
	TWeakObjectPtr<AThugCharacter> Runner;
	bool bRunnerDowned = false;
	double RunnerDownedAt = -1.0;
	FString RunnerDownedBy;
	double LastShotAt = -10.0;
	float RunnerDistanceAtDown = 0.f;
	float RunnerToEscapeAtDown = 0.f;
	bool bLootPicked = false;
	TWeakObjectPtr<ACrimeLoot> LootBag;

	// Rooftop.
	ERoofPhase RoofPhase = ERoofPhase::Grapple;
	TWeakObjectPtr<AGrappleAnchor> Anchor;
	FVector Stand = FVector::ZeroVector;
	double PhaseSince = 0.0;
	double GrapplePressedAt = -10.0;
	int32 GrapplePresses = 0;
	int32 Zips = 0;
	float RoofZ = 0.f;
	bool bOnRoofAtEnd = false;
	/** A crime thug standing (not down) off the crime's roof: the leash failed. */
	FString ThugOffRoof;

	// Ambush.
	bool bOpeningTrickDone = false;
};

void FHawkeyeCrimeLapRunner::Move(APlayerController* PC, const FVector2D& Value)
{
	UEnhancedInputLocalPlayerSubsystem* Input = HawkeyeCrimeLap::InputOf(PC);
	const UInputAction* A = LoadObject<UInputAction>(nullptr, HawkeyeCrimeLap::MovePath);
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

void FHawkeyeCrimeLapRunner::Sprint(APlayerController* PC, bool bOn)
{
	if (bOn != bSprinting)
	{
		HawkeyeCrimeLap::Hold(PC, HawkeyeCrimeLap::SprintPath, bOn);
		bSprinting = bOn;
	}
}

bool FHawkeyeCrimeLapRunner::WantsSpot(const ACrimeSpot* Candidate) const
{
	if (!Candidate || !Candidate->FindCrime(Type))
	{
		return false;
	}
	switch (Type)
	{
	case ECrimeType::Mugging: return !Candidate->bRooftop && !Candidate->bAlley;
	case ECrimeType::Robbery: return !Candidate->bRooftop && Candidate->HasEscapeLocation();
	case ECrimeType::Ambush: return !Candidate->bRooftop;
	case ECrimeType::Rooftop: return Candidate->bRooftop;
	default: return false;
	}
}

bool FHawkeyeCrimeLapRunner::OthersInBand(const UCrimeSubsystem* Crimes, const FVector& At) const
{
	// Only the lap's spot may be 25 to 40 m out, so the rules can only pick it.
	return Crimes && Crimes->GetSpots().ContainsByPredicate([&](const ACrimeSpot* Other)
	{
		const float D = FVector::Dist2D(Other->GetActorLocation(), At);
		return Other != Spot.Get() && Other->FindCrime(Type) && D >= Crimes->SpawnMinDistance - 100.f && D <= Crimes->SpawnMaxDistance + 100.f;
	});
}

void FHawkeyeCrimeLapRunner::Teleport(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ground, float Yaw)
{
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Kate->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f));
	PC->SetControlRotation(FRotator(-8.f, Yaw, 0.f));
	PC->SetViewTarget(Kate);
}

bool FHawkeyeCrimeLapRunner::PlaceOnStreet(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& At)
{
	// Open street 30 m out with a walk in not much longer than the straight line, toward the PlayerStart
	// first and then round the spot; failing that any street 30 m out with a path in at all.
	FVector Toward = FVector(1.f, 0.f, 0.f);
	if (TActorIterator<APlayerStart> Start(World); Start)
	{
		Toward = (Start->GetActorLocation() - At).GetSafeNormal2D();
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeLapGround), false, Kate);
	for (const float Detour : { 1.3f, 3.f })
	{
		for (int32 Step = 0; Step < 24; ++Step)
		{
			const float Turn = (Step % 2 == 0 ? 1.f : -1.f) * 15.f * ((Step + 1) / 2);
			const FVector Out = Toward.RotateAngleAxis(Turn, FVector::UpVector);
			const FVector Probe = At + Out * HawkeyeCrimeLap::StartDistance;
			// From high above, so a probe inside a building's footprint finds its roof, not the slab under it.
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Probe + FVector(0.f, 0.f, 20000.f), Probe - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params)
				|| Hit.ImpactPoint.Z > At.Z + 60.f || (Hit.GetActor() && Hit.GetActor()->ActorHasTag(HawkeyeCrimeLap::BuildingTag)))
			{
				continue;
			}
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, Hit.ImpactPoint + FVector(0.f, 0.f, 50.f), At, Kate) : nullptr;
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > HawkeyeCrimeLap::StartDistance * Detour)
			{
				continue;
			}
			if (OthersInBand(Crimes, Hit.ImpactPoint))
			{
				continue;
			}
			Teleport(PC, Kate, Hit.ImpactPoint, (-Out).Rotation().Yaw);
			Test->AddInfo(FString::Printf(TEXT("Crime lap: Kate at %s, %.0f m from %s, %.0f m to walk."), *Hit.ImpactPoint.ToCompactString(),
				FVector::Dist2D(Hit.ImpactPoint, At) / 100.f, *GetNameSafe(Spot.Get()), Path->GetPathLength() / 100.f));
			return true;
		}
	}
	return false;
}

bool FHawkeyeCrimeLapRunner::PlaceForGrapple(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, ACrimeSpot* Roof)
{
	using namespace HawkeyeCrimeLap;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	const FVector Centre = Roof->GetActorLocation();
	const AActor* Building = BuildingUnder(World, Centre, Kate);
	if (!Grapple || !Building)
	{
		return false;
	}
	// The anchors on the crime's own roof, the ones furthest from its middle first (they reach the street).
	TArray<AGrappleAnchor*> Anchors;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const FVector Marker = It->GetMarkerLocation();
		if (It->bEnabled && FMath::Abs(Marker.Z - Centre.Z) < 250.f && FVector::Dist2D(Marker, Centre) < 3200.f
			&& BuildingUnder(World, It->GetLandingLocation(), Kate) == Building)
		{
			Anchors.Add(*It);
		}
	}
	Anchors.Sort([&](const AGrappleAnchor& A, const AGrappleAnchor& B)
	{
		return FVector::Dist2D(A.GetMarkerLocation(), Centre) > FVector::Dist2D(B.GetMarkerLocation(), Centre);
	});
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeLapStand), false, Kate);
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	int32 Tried = 0;
	for (AGrappleAnchor* Candidate : Anchors)
	{
		const FVector Marker = Candidate->GetMarkerLocation();
		const FVector Outward = (Marker - Centre).GetSafeNormal2D();
		const float Height = Marker.Z;
		for (const float Reach : { 1.1f, 1.3f, 1.5f, 1.7f })
		{
			for (int32 Step = 0; Step < 11; ++Step)
			{
				const float Turn = (Step % 2 == 0 ? 1.f : -1.f) * 15.f * ((Step + 1) / 2);
				const FVector Probe = Marker + Outward.RotateAngleAxis(Turn, FVector::UpVector) * FMath::Max(Height * Reach, 700.f);
				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(Hit, FVector(Probe.X, Probe.Y, Height + 3000.f), FVector(Probe.X, Probe.Y, -500.f),
						ECC_Visibility, Params)
					|| Hit.ImpactPoint.Z > 200.f || (Hit.GetActor() && Hit.GetActor()->ActorHasTag(BuildingTag)))
				{
					continue;
				}
				const float FromSpot = FVector::Dist2D(Hit.ImpactPoint, Centre);
				const FVector Feet = Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 2.f);
				if (FromSpot < Crimes->SpawnMinDistance + 100.f || FromSpot > Crimes->SpawnMaxDistance - 100.f
					|| FVector::Dist(Feet, Marker) > Grapple->Range - 200.f || OthersInBand(Crimes, Hit.ImpactPoint))
				{
					continue;
				}
				++Tried;
				// Stand there, look at it the way the camera will, and ask the grapple what it would pick.
				const float Yaw = (Marker - Hit.ImpactPoint).Rotation().Yaw;
				Teleport(PC, Kate, Hit.ImpactPoint, Yaw);
				AActor* Blocker = nullptr;
				if (!Grapple->IsZipClear(Feet, Candidate, true, &Blocker))
				{
					continue;
				}
				const FVector Lens = Feet - (Marker - Feet).GetSafeNormal2D() * 330.f + FVector(0.f, 0.f, 70.f);
				if (Grapple->SelectBestAnchor(Lens, (Marker - Lens).GetSafeNormal()) != Candidate)
				{
					continue;
				}
				Anchor = Candidate;
				Stand = Hit.ImpactPoint;
				RoofZ = Centre.Z;
				AimAt(PC, Kate, Marker);
				Test->AddInfo(FString::Printf(TEXT("Crime lap: Kate on the street at %s, %.0f m from %s on its %.1f m roof, %.0f m from %s (%d stands tried)."),
					*Stand.ToCompactString(), FromSpot / 100.f, *Roof->GetName(), Centre.Z / 100.f, FVector::Dist(Feet, Marker) / 100.f,
					*Candidate->GetName(), Tried));
				return true;
			}
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Crime lap: %s has %d anchors on its roof; none reachable from a street 25 to 40 m out (%d stands tried)."),
		*Roof->GetName(), Anchors.Num(), Tried));
	return false;
}

bool FHawkeyeCrimeLapRunner::SetUp(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCrimeLap;
	UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	FVector StartLocation = Kate->GetActorLocation();
	if (TActorIterator<APlayerStart> Start(World); Start)
	{
		StartLocation = Start->GetActorLocation();
	}
	TArray<ACrimeSpot*> Candidates;
	for (ACrimeSpot* Candidate : Crimes ? Crimes->GetSpots() : TArray<ACrimeSpot*>())
	{
		if (WantsSpot(Candidate))
		{
			Candidates.Add(Candidate);
		}
	}
	// Nearest the PlayerStart first; the ambush takes an alley first, where there is one.
	Candidates.Sort([&](const ACrimeSpot& A, const ACrimeSpot& B)
	{
		if (Type == ECrimeType::Ambush && A.bAlley != B.bAlley)
		{
			return A.bAlley;
		}
		return FVector::Dist2D(A.GetActorLocation(), StartLocation) < FVector::Dist2D(B.GetActorLocation(), StartLocation);
	});
	if (Candidates.Num() == 0)
	{
		Test->AddError(FString::Printf(TEXT("Crime lap: no crime spot on the district for a %s."), *TypeName));
		return false;
	}
	// A lap before this one on the same loaded district may have left its thugs and its last-spot rule.
	Crimes->AbortCrime();
	Crimes->DespawnLeftoversNow();
	Crimes->ForgetLastCrime();
	HawkeyeFreezePartner(World);
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
	}
	bool bPlaced = false;
	for (ACrimeSpot* Candidate : Candidates)
	{
		Spot = Candidate;
		bPlaced = Type == ECrimeType::Rooftop ? PlaceForGrapple(World, PC, Kate, Candidate) : PlaceOnStreet(World, PC, Kate, Candidate->GetActorLocation());
		if (bPlaced)
		{
			break;
		}
	}
	if (!bPlaced)
	{
		Test->AddError(FString::Printf(TEXT("Crime lap: no place 25 to 40 m from any of the %d %s spot(s) to put Kate."), Candidates.Num(), *TypeName));
		return false;
	}
	Test->AddInfo(FString::Printf(TEXT("Crime lap: the %s at %s (%s)."), *TypeName, *GetNameSafe(Spot.Get()), SpotKind(Spot.Get())));
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	if (!Inventory->HasBow())
	{
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, StandardAssetPath);
		Standard.Count = StartArrows;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, GrappleAssetPath);
		Grapple.Count = 6;
		Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, BowAssetPath), { Standard, Grapple });
	}
	Inventory->SetArrowCount(SlotStandard, StartArrows);
	if (Type == ECrimeType::Robbery || Type == ECrimeType::Ambush)
	{
		// Two bolas (and a putty the lap does not plan to use), what the chapter hands out for testing.
		for (const TPair<int32, const TCHAR*>& Trick : { TPair<int32, const TCHAR*>(SlotPutty, PuttyAssetPath), TPair<int32, const TCHAR*>(SlotBola, BolaAssetPath) })
		{
			const int32 Want = Trick.Key == SlotBola ? 2 : 1;
			if (Inventory->GetArrowCount(Trick.Key) < Want)
			{
				Inventory->AddArrows(LoadObject<UArrowDefinition>(nullptr, Trick.Value), Want - Inventory->GetArrowCount(Trick.Key));
			}
		}
	}
	Inventory->SelectArrowSlot(SlotStandard);
	Kate->GetHealthComponent()->SetInvulnerable(false);
	Kate->GetHealthComponent()->Heal(1000.f);
	LastHealth = Kate->GetHealthComponent()->GetCurrentHealth();
	CountBefore = Crimes->GetCompletions(Type);
	SavedBefore = FMath::Max(SavedCount(World, UCrimeRules::TypeKey(Type)), 0);
	IConsoleVariable* Interval = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeInterval"));
	IConsoleVariable* TypeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeType"));
	OldInterval = Interval ? Interval->GetString() : TEXT("0");
	OldType = TypeVar ? TypeVar->GetString() : TEXT("");
	SetCVar(TEXT("hawkeye.CrimeInterval"), TEXT("1"));
	SetCVar(TEXT("hawkeye.CrimeType"), TypeName);
	return true;
}

void FHawkeyeCrimeLapRunner::NoteToast(AHawkeyeCharacter* Kate)
{
	const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Kate);
	const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
	if (bToastSeen || !Toasts || !Toasts->IsToastVisible())
	{
		return;
	}
	const FString Heading = Toasts->GetToastHeading().ToString();
	const FString Title = Toasts->GetToastTitle().ToString();
	if (Heading == UCrimeRules::EndReasonText(ECrimeEndReason::Completed).ToString() && Title.Contains(TEXT("[Crime:")))
	{
		bToastSeen = true;
		ToastText = Heading + TEXT(" / ") + Title;
	}
}

void FHawkeyeCrimeLapRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FString& InWhy)
{
	using namespace HawkeyeCrimeLap;
	Move(PC, FVector2D::ZeroVector);
	Hold(PC, FirePath, false);
	Hold(PC, MeleePath, false);
	Sprint(PC, false);
	if (UBowComponent* Bow = Kate->GetBowComponent())
	{
		Bow->ClearAimOverride();
	}
	SetCVar(TEXT("hawkeye.CrimeInterval"), OldInterval);
	SetCVar(TEXT("hawkeye.CrimeType"), OldType);
	bDone = true;
	DoneAt = World->GetTimeSeconds();
	Why = InWhy;
	UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	const UCrimeTracker* Tracker = Crimes ? Crimes->GetTracker() : nullptr;
	bHasResult = Tracker && Tracker->HasLastResult() && bStarted;
	Result = bHasResult ? Tracker->GetLastResult() : FCrimeResult();
	bOnRoofAtEnd = Type == ECrimeType::Rooftop && Kate->GetActorLocation().Z > RoofZ - 50.f;
	bLootPicked = LootBag.IsValid() && LootBag->IsRecovered();
	NoteToast(Kate);
}

void FHawkeyeCrimeLapRunner::Report(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeCrimeLap;
	bReported = true;
	const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const int32 ArrowsAfter = Inventory->GetArrowCount(SlotStandard);
	const int32 Cap = Inventory->GetCap(Inventory->GetArrowSlot(SlotStandard).Arrow);
	const int32 WantArrows = FMath::Min(5, FMath::Max(Cap - ArrowsBeforeEnd, 0));
	const int32 CountAfter = Crimes ? Crimes->GetCompletions(Type) : 0;
	const int32 SavedAfter = SavedCount(World, UCrimeRules::TypeKey(Type));
	const ACivilian* Civilian = Victim.Get();
	const bool bWon = bHasResult && Result.IsCompleted() && Result.Type == Type;
	const FString Proper = TypeName.Left(1).ToUpper() + TypeName.Mid(1);
	FString Extra;
	switch (Type)
	{
	case ECrimeType::Mugging:
		Extra = FString::Printf(TEXT("  \"victim_hits\": %d,\n  \"victim_freed\": %s,\n  \"thanks_shown\": %s,\n"), Result.VictimHits,
			Civilian && Civilian->IsFreed() ? TEXT("true") : TEXT("false"), Civilian && Civilian->IsThanksShowing() ? TEXT("true") : TEXT("false"));
		break;
	case ECrimeType::Robbery:
		Extra = FString::Printf(TEXT("  \"runner_downed_by\": \"%s\",\n  \"runner_downed_at_s\": %.2f,\n  \"runner_distance_m\": %.1f,\n")
			TEXT("  \"runner_to_escape_m\": %.1f,\n  \"loot_picked_up\": %s,\n"), *RunnerDownedBy,
			RunnerDownedAt >= 0.0 ? RunnerDownedAt - StartedAt : -1.0, RunnerDistanceAtDown / 100.f, RunnerToEscapeAtDown / 100.f,
			bLootPicked ? TEXT("true") : TEXT("false"));
		break;
	case ECrimeType::Rooftop:
		Extra = FString::Printf(TEXT("  \"grapple_presses\": %d,\n  \"zips\": %d,\n  \"roof_m\": %.1f,\n  \"on_roof_at_end\": %s,\n  \"thug_off_roof\": \"%s\",\n"),
			GrapplePresses, Zips, RoofZ / 100.f, bOnRoofAtEnd ? TEXT("true") : TEXT("false"), *ThugOffRoof);
		break;
	default:
		break;
	}
	const FString Json = FString::Printf(TEXT(
		"{\n  \"test\": \"Hawkeye.Lap.Crime%s\",\n  \"completed\": %s,\n  \"end\": \"%s\",\n  \"reason\": \"%s\",\n"
		"  \"spot\": \"%s\",\n  \"spot_kind\": \"%s\",\n  \"seconds\": %.2f,\n  \"seconds_to_start\": %.2f,\n  \"hits_taken\": %d,\n"
		"  \"damage_taken\": %.1f,\n  \"kate_health\": %.1f,\n%s  \"arrows_loosed\": %d,\n  \"trick_arrows\": \"%s\",\n  \"strikes\": %d,\n"
		"  \"parries\": %d,\n  \"finishers\": %d,\n  \"dodges\": %d,\n  \"arrows_before_end\": %d,\n  \"arrows_after\": %d,\n"
		"  \"arrows_given\": %d,\n  \"count_before\": %d,\n  \"count_after\": %d,\n  \"saved_before\": %d,\n  \"saved_after\": %d,\n"
		"  \"toast_seen\": %s,\n  \"toast\": \"%s\"\n}\n"),
		*Proper,
		bWon ? TEXT("true") : TEXT("false"), *Why, bHasResult ? *UEnum::GetValueAsString(Result.Reason) : TEXT("none"),
		*GetNameSafe(Spot.Get()), SpotKind(Spot.Get()), bStarted ? DoneAt - StartedAt : 0.0, bStarted ? StartedAt - SetUpAt : 0.0, HitsTaken,
		DamageTaken, Kate->GetHealthComponent()->GetCurrentHealth(), *Extra, ArrowsLoosed, *FString::Join(TrickArrows, TEXT(", ")), Strikes,
		Parries, Finishers, Dodges, ArrowsBeforeEnd, ArrowsAfter, Result.ArrowsGiven, CountBefore, CountAfter, SavedBefore, SavedAfter,
		bToastSeen ? TEXT("true") : TEXT("false"), *ToastText);
	WriteText(FString::Printf(TEXT("crime_%s.json"), *TypeName), HawkeyeLapPressure::Finish(Test, TEXT("Crime") + TypeName, Json));
	Test->AddInfo(FString::Printf(TEXT("crime_%s.json:\n"), *TypeName) + Json);
	UE_LOG(LogTemp, Display, TEXT("[Hawkeye] crime %s: %s"), *TypeName, *Json);
	if (!bWon)
	{
		Test->AddError(FString::Printf(TEXT("The %s was not stopped (%s, %s)."), *TypeName, *Why,
			bHasResult ? *UEnum::GetValueAsString(Result.Reason) : TEXT("no result")));
		return;
	}
	Test->TestEqual(TEXT("Stopping it added 5 standard arrows (or up to the cap)"), Result.ArrowsGiven, WantArrows);
	// At least them: an arrow of hers picked up off the ground in the crime's last frame counts on top.
	Test->TestTrue(FString::Printf(TEXT("The quiver shows them (%d to %d)"), ArrowsBeforeEnd, ArrowsAfter), ArrowsAfter - ArrowsBeforeEnd >= WantArrows);
	Test->TestTrue(TEXT("There was room for them"), WantArrows > 0);
	Test->TestEqual(TEXT("One more counted"), CountAfter, CountBefore + 1);
	Test->TestEqual(TEXT("And in the campaign save"), SavedAfter, SavedBefore + 1);
	Test->TestTrue(TEXT("The reward toast showed"), bToastSeen);
	Test->TestTrue(TEXT("With the arrows on it"), ToastText.Contains(FString::Printf(TEXT("+%d arrows"), Result.ArrowsGiven)));
	switch (Type)
	{
	case ECrimeType::Mugging:
		Test->TestTrue(TEXT("The victim was freed"), Civilian && Civilian->IsFreed());
		Test->TestTrue(TEXT("With fewer than three hits on him"), Result.VictimHits < 3);
		break;
	case ECrimeType::Robbery:
		Test->TestTrue(TEXT("The runner was downed by a bola or an arrow"), bRunnerDowned && !RunnerDownedBy.IsEmpty());
		Test->TestTrue(TEXT("She picked the loot up"), bLootPicked);
		break;
	case ECrimeType::Ambush:
		Test->TestTrue(TEXT("A trick arrow was used"), TrickArrows.Num() > 0);
		Test->TestTrue(TEXT("And melee"), Strikes > 0);
		break;
	case ECrimeType::Rooftop:
		Test->TestTrue(TEXT("She grappled up"), Zips > 0);
		Test->TestTrue(TEXT("And ended it on the roof"), bOnRoofAtEnd);
		Test->TestTrue(FString::Printf(TEXT("Every crime thug stayed on its roof (%s)"), ThugOffRoof.IsEmpty() ? TEXT("all did") : *ThugOffRoof),
			ThugOffRoof.IsEmpty());
		break;
	default:
		break;
	}
}

bool FHawkeyeCrimeLapRunner::Update()
{
	using namespace HawkeyeCrimeLap;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	if (!World || !Kate || !Crimes)
	{
		Test->AddError(TEXT("No game world, no Kate, or no crime subsystem for the crime lap."));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	if (bDone)
	{
		// The toast queue may hold another toast first: watch it for a few seconds.
		NoteToast(Kate);
		if ((bToastSeen && Now - DoneAt > 1.5) || Now - DoneAt > 8.0)
		{
			if (!bReported)
			{
				Report(World, Kate);
			}
			return true;
		}
		return false;
	}
	if (!bSetUp)
	{
		if (!SetUp(World, PC, Kate))
		{
			return true;
		}
		bSetUp = true;
		SetUpAt = Now;
		return false;
	}
	if (!bStarted)
	{
		if (Crimes->IsCrimeActive())
		{
			bStarted = true;
			StartedAt = Now;
			PhaseSince = Now;
			Victim = Crimes->GetVictim();
			Thugs = Crimes->GetThugs();
			Runner = Crimes->GetRunner();
			LootBag = Crimes->GetLoot();
			const UCrimeDefinition* Definition = Crimes->GetActiveDefinition();
			Test->AddInfo(FString::Printf(TEXT("Crime lap: %s started at %s after %.1f s, %d thugs."), *GetNameSafe(Definition),
				*GetNameSafe(Crimes->GetActiveSpot()), Now - SetUpAt, Thugs.Num()));
			Test->TestTrue(FString::Printf(TEXT("The due crime is a %s"), *TypeName), Definition && Definition->Type == Type);
			Test->TestTrue(TEXT("At the lap's spot"), Crimes->GetActiveSpot() == Spot.Get());
		}
		else if (Now - SetUpAt > 30.0)
		{
			// Crimes wait out a fight (any thug alerted, or Clint's fight clock from the lap before).
			Finish(World, PC, Kate, FString::Printf(TEXT("no crime started within 30 s (in combat: %s)"),
				AHawkeyeGameMode::IsWorldInCombat(World) ? TEXT("yes") : TEXT("no")));
		}
		return false;
	}
	const float Health = Kate->GetHealthComponent()->GetCurrentHealth();
	if (Health < LastHealth - 0.01f)
	{
		++HitsTaken;
		DamageTaken += LastHealth - Health;
		Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: Kate hit for %.0f, health %.0f"), Now - StartedAt, LastHealth - Health, Health));
	}
	LastHealth = Health;
	if (!Crimes->IsCrimeActive())
	{
		Finish(World, PC, Kate, TEXT("the crime ended"));
		return false;
	}
	ArrowsBeforeEnd = Kate->GetInventoryComponent()->GetArrowCount(SlotStandard);
	const double Limit = Type == ECrimeType::Mugging || Type == ECrimeType::Robbery ? 90.0 : 120.0;
	if (!Kate->GetHealthComponent()->IsAlive() || Kate->IsDowned() || Now - StartedAt > Limit)
	{
		Finish(World, PC, Kate, !Kate->GetHealthComponent()->IsAlive() ? TEXT("Kate died") : Kate->IsDowned() ? TEXT("Kate went down") : TEXT("timed out"));
		return false;
	}
	switch (Type)
	{
	case ECrimeType::Robbery:
		Robbery(World, PC, Kate, Now);
		break;
	case ECrimeType::Rooftop:
		Rooftop(World, PC, Kate, Now);
		break;
	default:
		Fight(World, PC, Kate, Now);
		break;
	}
	return false;
}

// --- Actions -----------------------------------------------------------------------------------------

void FHawkeyeCrimeLapRunner::BeginDraw(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, int32 Slot, float Seconds,
	double Now)
{
	Move(PC, FVector2D::ZeroVector);
	Sprint(PC, false);
	Kate->GetInventoryComponent()->SelectArrowSlot(Slot);
	DrawSlot = Slot;
	DrawTarget = Target;
	DrawFor = Seconds;
	DrawStart = Now;
	bDrawing = true;
	HawkeyeCrimeLap::Hold(PC, HawkeyeCrimeLap::FirePath, true);
}

bool FHawkeyeCrimeLapRunner::UpdateDraw(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeCrimeLap;
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
	// A swing about to land cuts the draw short.
	bool bThreat = false;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		const AThugCharacter* T = Weak.Get();
		bThreat = bThreat || (!IsDown(T) && T->GetMeleeComponent() && T->GetMeleeComponent()->IsWindingUp()
			&& T->GetMeleeComponent()->GetPhaseRemaining() < 0.3f && FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()) < 260.f);
	}
	const bool bLoose = Now - DrawStart >= DrawFor || (bThreat && Now - DrawStart >= 0.25);
	if (bLoose || IsDown(Target) || bThreat)
	{
		if (bLoose && !IsDown(Target))
		{
			Hold(PC, FirePath, false);
			++ArrowsLoosed;
			LastShotAt = Now;
			if (DrawSlot == SlotBola)
			{
				TrickArrows.Add(TEXT("bola"));
			}
			else if (DrawSlot == SlotPutty)
			{
				TrickArrows.Add(TEXT("putty"));
			}
		}
		else
		{
			// Too early to loose (or nothing left to hit): let it down.
			if (Bow)
			{
				Bow->CancelDraw();
			}
			Hold(PC, FirePath, false);
		}
		bDrawing = false;
		NextActionAt = Now + 0.15;
		// Back to the standard arrow only once this one is off the string: the release is read next frame,
		// and a slot changed in the same frame looses the new slot's arrow instead.
		SlotResetAt = Now + 0.25;
	}
	return true;
}

bool FHawkeyeCrimeLapRunner::DodgeSwings(AHawkeyeCharacter* Kate, const AThugCharacter* Except, double Now)
{
	using namespace HawkeyeCrimeLap;
	HawkeyeLapBot::FDodgeRule Rule;
	Rule.Except = Except;
	if (!HawkeyeLapBot::TryDodgeSwings(Kate, Thugs, Rule))
	{
		return false;
	}
	++Dodges;
	NextActionAt = Now + 0.3;
	return true;
}

bool FHawkeyeCrimeLapRunner::Strike(APlayerController* PC, AHawkeyeCharacter* Kate, AThugCharacter* Target, float Distance, double Now)
{
	using namespace HawkeyeCrimeLap;
	if (Distance > 190.f)
	{
		return false;
	}
	Move(PC, FVector2D::ZeroVector);
	Sprint(PC, false);
	const UMeleeComponent* ThugMelee = Target->GetMeleeComponent();
	const bool bLight = (ThugMelee && ThugMelee->IsWindingUp()) || Target->IsKnockedDown() || Target->IsStaggered() || Strikes % 3 != 2;
	++Strikes;
	if (bLight)
	{
		Tap(PC, MeleePath);
		NextActionAt = Now + 0.32;
		return true;
	}
	Hold(PC, MeleePath, true);
	bMeleeHeld = true;
	MeleeHeldSince = Now;
	return true;
}

void FHawkeyeCrimeLapRunner::RunTo(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Goal, bool bSprint)
{
	Sprint(PC, bSprint);
	const FVector To = HawkeyeCrimeLap::SteerTowards(World, Kate, Goal);
	Move(PC, HawkeyeCrimeLap::MoveTowards(To, PC->GetControlRotation().Yaw));
}

void FHawkeyeCrimeLapRunner::Fight(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeCrimeLap;
	// The melee thugs first; the gunner and the archer are shot unless they are in reach.
	AThugCharacter* Target = nullptr;
	AThugCharacter* Ranged = nullptr;
	float Distance = BIG_NUMBER;
	float RangedDistance = BIG_NUMBER;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		AThugCharacter* Thug = Weak.Get();
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
	if (Now - LastParryTap > 0.3 && CanParryNow(Kate) && ParryChance(Thugs, Kate, PC))
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
		NextActionAt = Now + 0.3;
		return;
	}
	if (DodgeSwings(Kate, Target, Now))
	{
		return;
	}
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	// The ambush opens with a bola on the gunner (the nearest when there is none) from 4 to 25 m, before the
	// squad closes; a draw cut short by a swing is tried again.
	bOpeningTrickDone = bOpeningTrickDone || TrickArrows.Num() > 0;
	if (Type == ECrimeType::Ambush && !bOpeningTrickDone && Inventory->GetArrowCount(SlotBola) > 0)
	{
		AThugCharacter* Trick = Ranged && Ranged->IsGunner() ? Ranged : Target;
		const float D = FVector::Dist2D(Trick->GetActorLocation(), Kate->GetActorLocation());
		if (D >= 400.f && D <= 2500.f && Sees(World, Kate, Trick) && Distance > 300.f)
		{
			Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: a bola at %s, %.0f m off."), Now - StartedAt, *Trick->GetName(), D / 100.f));
			BeginDraw(PC, Kate, Trick, SlotBola, FullDraw, Now);
			return;
		}
	}
	if (Now >= NextReportAt)
	{
		NextReportAt = Now + 3.0;
		FString States;
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
		{
			if (const AThugCharacter* T = Weak.Get())
			{
				States += FString::Printf(TEXT("%s %s %.0f hp %.0f cm; "), *T->GetName(), IsDown(T) ? TEXT("down") : TEXT("up"),
					T->GetHealthComponent()->GetCurrentHealth(), FVector::Dist2D(T->GetActorLocation(), Kate->GetActorLocation()));
			}
		}
		Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: Kate at %s, health %.0f, on %s; %s"), Now - StartedAt,
			*Kate->GetActorLocation().ToCompactString(), Kate->GetHealthComponent()->GetCurrentHealth(), *Target->GetName(), *States));
	}
	if (Distance > 1500.f)
	{
		// Run in, sprinting; the victim's clock (or the squad) is not waiting.
		RunTo(World, PC, Kate, Target->GetActorLocation(), true);
		return;
	}
	Sprint(PC, false);
	if (Strike(PC, Kate, Target, Distance, Now))
	{
		return;
	}
	const bool bSeen = Sees(World, Kate, Target);
	// The gunner and the archer keep their distance: shoot them, a quick shot when the archer is drawing on her.
	if ((Target->IsGunner() || Target->IsArcher()) && Distance >= 300.f && bSeen && Inventory->GetArrowCount(SlotStandard) > 0)
	{
		const UBowComponent* TheirBow = Target->GetBowComponent();
		const bool bDrawingOnHer = Target->IsArcher() && TheirBow && TheirBow->IsDrawing();
		BeginDraw(PC, Kate, Target, SlotStandard, bDrawingOnHer ? 0.35f : FullDraw, Now);
		return;
	}
	// The mugging's two arrows on the way in, from 5 to 15 m; after that she closes for the strikes.
	if (Type == ECrimeType::Mugging && Distance >= 500.f && ArrowsLoosed < 2 && bSeen)
	{
		BeginDraw(PC, Kate, Target, SlotStandard, FullDraw, Now);
		return;
	}
	RunTo(World, PC, Kate, Target->GetActorLocation(), false);
}

void FHawkeyeCrimeLapRunner::Robbery(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeCrimeLap;
	UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World);
	ACrimeLoot* Loot = Crimes ? Crimes->GetLoot() : nullptr;
	AThugCharacter* Carrier = Runner.Get();
	if (UpdateDraw(World, PC, Kate, Now))
	{
		return;
	}
	if (bMeleeHeld)
	{
		Hold(PC, MeleePath, false);
		bMeleeHeld = false;
	}
	const bool bCarried = Loot && Loot->IsCarried();
	if (!bRunnerDowned && Loot && !bCarried)
	{
		bRunnerDowned = true;
		RunnerDownedAt = Now;
		RunnerDownedBy = TrickArrows.Num() > 0 && Now - LastShotAt < 2.0 ? TrickArrows.Last() : (Now - LastShotAt < 2.0 ? TEXT("arrow") : TEXT(""));
		RunnerDistanceAtDown = Carrier ? FVector::Dist2D(Carrier->GetActorLocation(), Kate->GetActorLocation()) : 0.f;
		RunnerToEscapeAtDown = Carrier ? FVector::Dist2D(Carrier->GetActorLocation(), Crimes->GetEscapeLocation()) : 0.f;
		Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: the runner dropped the loot (%s), %.0f m from her, %.0f m short of the escape."),
			Now - StartedAt, RunnerDownedBy.IsEmpty() ? TEXT("not by her") : *RunnerDownedBy, RunnerDistanceAtDown / 100.f,
			RunnerToEscapeAtDown / 100.f));
	}
	if (Loot && !bCarried)
	{
		// Over to the bag and onto it; step out of any swing on the way.
		if (Now >= NextActionAt && !Kate->IsDodging() && DodgeSwings(Kate, nullptr, Now))
		{
			return;
		}
		const FVector Bag = Loot->GetActorLocation();
		AimAt(PC, Kate, Bag);
		const float D = FVector::Dist2D(Bag, Kate->GetActorLocation());
		bLootPicked = Loot->IsRecovered();
		if (D < 60.f)
		{
			Move(PC, FVector2D::ZeroVector);
			return;
		}
		RunTo(World, PC, Kate, Bag, D > 600.f);
		if (D < 250.f)
		{
			// The last steps straight at it: the navmesh path may end a little off the bag.
			Move(PC, MoveTowards((Bag - Kate->GetActorLocation()).GetSafeNormal2D(), PC->GetControlRotation().Yaw));
		}
		return;
	}
	if (!Carrier)
	{
		Move(PC, FVector2D::ZeroVector);
		return;
	}
	const float Distance = FVector::Dist2D(Carrier->GetActorLocation(), Kate->GetActorLocation());
	if (Now >= NextReportAt)
	{
		NextReportAt = Now + 2.0;
		Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: runner %s %.0f m from her, %.0f m from the escape, %s, speed %.0f."), Now - StartedAt,
			*Carrier->GetName(), Distance / 100.f, FVector::Dist2D(Carrier->GetActorLocation(), Crimes->GetEscapeLocation()) / 100.f,
			Crimes->HasRunnerSetOff() ? TEXT("running") : TEXT("waiting"), Carrier->GetVelocity().Size2D()));
	}
	AimAt(PC, Kate, Carrier->GetActorLocation() + FVector(0.f, 0.f, 30.f));
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const bool bSeen = Sees(World, Kate, Carrier);
	// A bola (an arrow once they are gone) from 22 m in, a second after the last shot missed.
	if (Distance <= 2200.f && bSeen && Now - LastShotAt > 1.0 && Now >= NextActionAt && !Kate->IsDodging())
	{
		const int32 Slot = Inventory->GetArrowCount(SlotBola) > 0 ? SlotBola : SlotStandard;
		BeginDraw(PC, Kate, Carrier, Slot, FullDraw, Now);
		return;
	}
	RunTo(World, PC, Kate, Carrier->GetActorLocation(), true);
}

void FHawkeyeCrimeLapRunner::Rooftop(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, double Now)
{
	using namespace HawkeyeCrimeLap;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	switch (RoofPhase)
	{
	case ERoofPhase::Grapple:
	{
		AGrappleAnchor* Target = Anchor.Get();
		if (!Target || !Grapple)
		{
			Finish(World, PC, Kate, TEXT("no anchor to grapple to"));
			return;
		}
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
		{
			RoofPhase = ERoofPhase::Zipping;
			PhaseSince = Now;
			return;
		}
		Move(PC, FVector2D::ZeroVector);
		AimAt(PC, Kate, Target->GetMarkerLocation());
		if (Now - GrapplePressedAt < 0.4)
		{
			return;
		}
		Grapple->RefreshTarget();
		if (Grapple->GetTargetAnchor() == Target && Grapple->CanChain())
		{
			++GrapplePresses;
			GrapplePressedAt = Now;
			Tap(PC, GrapplePath);
			return;
		}
		if (Now - PhaseSince > 4.0)
		{
			Finish(World, PC, Kate, FString::Printf(TEXT("%s never targeted from the street (targeting %s)"), *Target->GetName(),
				*GetNameSafe(Grapple->GetTargetAnchor())));
		}
		return;
	}
	case ERoofPhase::Zipping:
		Move(PC, FVector2D::ZeroVector);
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight() || !Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			if (Now - PhaseSince > 6.0)
			{
				Finish(World, PC, Kate, TEXT("the zip never landed"));
			}
			return;
		}
		++Zips;
		if (Kate->GetActorLocation().Z > RoofZ - 50.f)
		{
			Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: landed on the roof at %s after %d press(es)."), Now - StartedAt,
				*Kate->GetActorLocation().ToCompactString(), GrapplePresses));
			RoofPhase = ERoofPhase::Fight;
			PhaseSince = Now;
			return;
		}
		Test->AddInfo(FString::Printf(TEXT("Crime lap %.1f s: the zip ended at %s, not on the %.1f m roof; again."), Now - StartedAt,
			*Kate->GetActorLocation().ToCompactString(), RoofZ / 100.f));
		if (Zips >= 3)
		{
			Finish(World, PC, Kate, TEXT("three zips and not on the roof"));
			return;
		}
		RoofPhase = ERoofPhase::Grapple;
		PhaseSince = Now;
		return;
	case ERoofPhase::Fight:
	default:
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
		{
			const AThugCharacter* Thug = Weak.Get();
			if (ThugOffRoof.IsEmpty() && !IsDown(Thug) && !Thug->GetCharacterMovement()->IsFlying() && Thug->GetActorLocation().Z < RoofZ - 300.f)
			{
				ThugOffRoof = FString::Printf(TEXT("%s at %s, %.1f s"), *Thug->GetName(), *Thug->GetActorLocation().ToCompactString(), Now - StartedAt);
				Test->AddInfo(TEXT("Crime lap: a crime thug left the roof: ") + ThugOffRoof);
			}
		}
		Fight(World, PC, Kate, Now);
		return;
	}
}

// --- The tests -------------------------------------------------------------------------------------

static bool HawkeyeStartCrimeLap(FAutomationTestBase* Test, ECrimeType Type)
{
	if (!FApp::CanEverRender())
	{
		Test->AddWarning(TEXT("Skipped (no RHI): the crime lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCrimeLapRunner(Test, Type));
	return true;
}

bool FHawkeyeLapCrimeMugging::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCrimeLap(this, ECrimeType::Mugging);
}

bool FHawkeyeLapCrimeRobbery::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCrimeLap(this, ECrimeType::Robbery);
}

bool FHawkeyeLapCrimeAmbush::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCrimeLap(this, ECrimeType::Ambush);
}

bool FHawkeyeLapCrimeRooftop::RunTest(const FString& Parameters)
{
	HawkeyeLapPressure::Start();
	return HawkeyeStartCrimeLap(this, ECrimeType::Rooftop);
}

#endif
