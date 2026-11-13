#include "UI/HawkeyeHintSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/CombatReadability.h"
#include "Combat/HealthComponent.h"
#include "Combat/TakedownComponent.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Mission/MissionFlowController.h"
#include "Partner/HawkeyePartnerController.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/GrappleAnchor.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugCharacter.h"

namespace HawkeyeHintSubsystem
{
	static TAutoConsoleVariable<int32> CVarHints(TEXT("hawkeye.Hints"), 1,
		TEXT("First-time hints: 0 never, 1 outside automation (the Settings toggle decides), 2 always, automation too."));

	// The distances in the rules' triggers (gameplay-semantics.md, HUD, "First-time hints"), cm.
	static constexpr float VaultReach = 300.f;
	static constexpr float ClimbReach = 200.f;
	static constexpr float ClimbMinHeight = 150.f;
	static constexpr float ClimbMaxHeight = 260.f;
	static constexpr float OpenGroundReach = 300.f;
	static constexpr float MovingSpeed = 150.f;
	static constexpr float CloseThug = 300.f;
	static constexpr float RangeThugMin = 800.f;
	static constexpr float RangeThugMax = 4000.f;
	static constexpr float RangeThugConeDegrees = 30.f;
	static constexpr float FinisherReach = 800.f;
	static constexpr float PartnerReach = 500.f;
	static constexpr float FarFromStart = 4000.f;
	static constexpr float SafehouseReach = 1500.f;
	static constexpr float PedestalReach = 1000.f;
	static constexpr float CrimeNear = 1500.f;
	static constexpr float GunnerTelegraphRange = 600.f;
	static constexpr float RoofEdgeMinDrop = 300.f;
	static constexpr float ProbeSeconds = 0.2f;
	static constexpr float NoticePulseSeconds = 0.5f;

	/** Triggers that need a trace: refreshed at 5 Hz. */
	static uint64 TracedTriggers()
	{
		using T = EHawkeyeHintTrigger;
		return FHawkeyeHintQueue::Bit(T::WalkingOpenGround) | FHawkeyeHintQueue::Bit(T::NearVaultable)
			| FHawkeyeHintQueue::Bit(T::FacingClimbableWall) | FHawkeyeHintQueue::Bit(T::AtRoofEdge)
			| FHawkeyeHintQueue::Bit(T::ThugAtRange) | FHawkeyeHintQueue::Bit(T::NearSafehouse)
			| FHawkeyeHintQueue::Bit(T::NearPedestal) | FHawkeyeHintQueue::Bit(T::FarWithoutMarker);
	}
}

UHawkeyeHintSubsystem* UHawkeyeHintSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UHawkeyeHintSubsystem>() : nullptr;
}

void UHawkeyeHintSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Queue.Reset(UHawkeyeHintRules::GetDefaultRules());
}

bool UHawkeyeHintSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHawkeyeHintSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHawkeyeHintSubsystem, STATGROUP_Tickables);
}

bool UHawkeyeHintSubsystem::IsRunAllowed()
{
	const int32 Mode = HawkeyeHintSubsystem::CVarHints.GetValueOnGameThread();
	return Mode >= 2 || (Mode == 1 && !HawkeyeAutomation::IsAutomationRun());
}

FHawkeyeHintSuppression UHawkeyeHintSubsystem::GatherSuppression(const AHawkeyePlayerController* Controller)
{
	FHawkeyeHintSuppression Out;
	if (!Controller)
	{
		return Out;
	}
	const UMissionFlowController* Flow = Controller->GetMissionFlow();
	Out.bChapterBeat = (Flow && Flow->IsRunning()) || Controller->IsChapterTitleShowing() || Controller->IsCloseUpActive()
		|| Controller->IsFlashbackActive();
	const AHawkeyeCharacter* Pawn = Cast<AHawkeyeCharacter>(Controller->GetPawn());
	Out.bMenu = Controller->IsPauseMenuOpen() || Controller->IsSettingsOpen() || Controller->IsInventoryOpen()
		|| Controller->IsPhoneOpen() || Controller->IsMapOpen() || Controller->IsMainMenuOpen() || Controller->IsDifficultyPromptOpen()
		|| Controller->IsSafehouseMenuOpen() || Controller->IsFlashbackReplayOpen() || Controller->IsPaused()
		|| (Pawn && Pawn->IsQuiverWheelOpen());
	Out.bDowned = Pawn && Pawn->IsDowned();
	Out.bChallengeResults = Controller->IsChallengeResultsOpen();
	return Out;
}

int32 UHawkeyeHintSubsystem::GetShowCount(FName Id) const
{
	if (const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		return Save->GetHintShowCount(Id);
	}
	const int32* Local = LocalCounts.Find(Id);
	return Local ? *Local : 0;
}

void UHawkeyeHintSubsystem::SetShowCount(FName Id, int32 Count)
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->SetHintShowCount(Id, Count);
		return;
	}
	LocalCounts.Add(Id, Count);
}

void UHawkeyeHintSubsystem::ResetForTest(const TArray<FName>& Only)
{
	for (const FHawkeyeHintRule& Rule : UHawkeyeHintRules::GetDefaultRules())
	{
		SetShowCount(Rule.Id, Only.Num() == 0 || Only.Contains(Rule.Id) ? 0 : Rule.MaxShows);
	}
	Queue.Reset(UHawkeyeHintRules::GetDefaultRules());
	ProbeCooldown = 0.f;
	LastProbed = 0;
	bHaveLastPawn = false;
	LastPawn.Reset();
	LastHealth = -1.f;
	LastParryCount = -1;
	bWasDrawing = false;
	LastZipAnchor.Reset();
}

uint64 UHawkeyeHintSubsystem::ComputeNeededTriggers() const
{
	uint64 Needed = 0;
	for (const FHawkeyeHintRule& Rule : UHawkeyeHintRules::GetDefaultRules())
	{
		if (GetShowCount(Rule.Id) < Rule.MaxShows)
		{
			Needed |= FHawkeyeHintQueue::Bit(Rule.Trigger);
		}
	}
	return Needed;
}

FHawkeyeHintContext UHawkeyeHintSubsystem::GetContext() const
{
	FHawkeyeHintContext Context;
	const UWorld* World = GetWorld();
	const AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	Context.bGamepad = PC && PC->IsUsingGamepad();
	Context.SprintMode = UHawkeyeSettingsSubsystem::GetCurrentSettings(this).SprintMode;
	if (World && PC)
	{
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			if (It->GetLeader() == PC->GetPawn() && It->GetPartner())
			{
				Context.PartnerName = It->GetPartner()->GetCharacterName().ToString();
				break;
			}
		}
	}
	return Context;
}

FString UHawkeyeHintSubsystem::GetShownLineForLog() const
{
	const FHawkeyeHintRule* Rule = GetShown();
	return Rule ? UHawkeyeHintRules::ResolveToString(*Rule, GetContext()) : FString();
}

void UHawkeyeHintSubsystem::Sense(AHawkeyePlayerController* Controller, float DeltaSeconds, uint64 Needed, uint64& OutTriggers,
	uint64& OutActions)
{
	using namespace HawkeyeHintSubsystem;
	using T = EHawkeyeHintTrigger;
	using A = EHawkeyeHintAction;
	OutTriggers = 0;
	OutActions = 0;
	UWorld* World = GetWorld();
	AHawkeyeCharacter* Kate = Controller ? Cast<AHawkeyeCharacter>(Controller->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		return;
	}
	auto Need = [Needed](T Trigger) { return (Needed & FHawkeyeHintQueue::Bit(Trigger)) != 0; };
	auto Hold = [&OutTriggers](T Trigger) { OutTriggers |= FHawkeyeHintQueue::Bit(Trigger); };
	auto Did = [&OutActions](A Action) { OutActions |= FHawkeyeHintQueue::Bit(Action); };

	const FVector At = Kate->GetActorLocation();
	const UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	const bool bGrounded = Movement && Movement->IsMovingOnGround();
	const float Speed = Kate->GetVelocity().Size2D();
	const bool bInFight = Kate->GetEngagedThugCount() > 0;
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	const bool bBusy = (Parkour && Parkour->IsBusy()) || Kate->IsZipping();
	if (!bHaveStart)
	{
		bHaveStart = true;
		StartLocation = At;
	}

	// --- Actions: what she did this frame ---------------------------------------------------------------
	const bool bSwitched = bHaveLastPawn && LastPawn.Get() != Kate;
	if (bSwitched)
	{
		Did(A::Switched);
	}
	if (bSwitched || !bHaveLastPawn)
	{
		// A new body: its own health, parry count and bow start the comparisons again.
		LastHealth = -1.f;
		LastParryCount = -1;
		bWasDrawing = false;
		LastZipAnchor.Reset();
	}
	bHaveLastPawn = true;
	LastPawn = Kate;

	if (Kate->IsSprinting())
	{
		Did(A::Sprinted);
	}
	if (Parkour)
	{
		const EHawkeyeParkourMove Move = Parkour->GetActiveMove();
		if (Move == EHawkeyeParkourMove::Vault || (Parkour->IsSampleTraversalActive() && Parkour->GetLastMove() == EHawkeyeParkourMove::Vault))
		{
			Did(A::Vaulted);
		}
		if (Move == EHawkeyeParkourMove::Mantle || Move == EHawkeyeParkourMove::LedgeGrab || Move == EHawkeyeParkourMove::Climb
			|| (Parkour->IsSampleTraversalActive() && Parkour->GetLastMove() == EHawkeyeParkourMove::Mantle))
		{
			Did(A::Climbed);
		}
		if (Move == EHawkeyeParkourMove::DropToHang)
		{
			Did(A::DroppedToHang);
		}
	}
	AActor* ZipAnchor = Grapple && Grapple->IsZipping() ? Grapple->GetZipAnchor() : nullptr;
	if (ZipAnchor)
	{
		Did(A::Zipped);
		if (LastZipAnchor.IsValid() && LastZipAnchor.Get() != ZipAnchor)
		{
			Did(A::Chained);
		}
	}
	LastZipAnchor = ZipAnchor;
	const bool bDrawing = Kate->IsDrawingBow();
	if (bWasDrawing && !bDrawing)
	{
		Did(A::FiredArrow);
	}
	bWasDrawing = bDrawing;
	if (Kate->IsMeleeAttacking())
	{
		Did(A::Struck);
	}
	const int32 Parries = Kate->GetParryCount();
	if (LastParryCount >= 0 && Parries > LastParryCount)
	{
		Did(A::Parried);
	}
	LastParryCount = Parries;
	if (Kate->IsDodging())
	{
		Did(A::Dodged);
	}
	if (Kate->IsPerformingFinisher())
	{
		Did(A::Finished);
	}
	if (Kate->GetTakedownComponent() && Kate->GetTakedownComponent()->IsPerformingTakedown())
	{
		Did(A::TookDown);
	}
	if (Kate->IsQuiverWheelOpen())
	{
		Did(A::OpenedWheel);
	}
	if (Controller->IsPhoneOpen())
	{
		Did(A::OpenedPhone);
	}
	if (Controller->IsMapOpen())
	{
		Did(A::OpenedMap);
	}
	if (Controller->IsSafehouseMenuOpen())
	{
		Did(A::EnteredSafehouse);
	}
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	if (Challenges && Challenges->IsRunning())
	{
		Did(A::StartedChallenge);
	}
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	if (Safehouses && Safehouses->IsTravelling())
	{
		Did(A::FastTravelled);
	}

	// --- Triggers read every frame (no traces) ------------------------------------------------------------
	if (Grapple && Need(T::GrappleReady) && !Grapple->IsZipping() && Grapple->GetTargetState() == EGrappleTargetState::Ready)
	{
		Hold(T::GrappleReady);
	}
	if (Grapple && Need(T::ChainAvailable) && Grapple->IsZipping() && Grapple->CanChain()
		&& Grapple->GetTargetState() == EGrappleTargetState::Ready && Grapple->GetTargetAnchor() != Grapple->GetZipAnchor())
	{
		Hold(T::ChainAvailable);
	}

	const UHealthComponent* Health = Kate->GetHealthComponent();
	const float HealthNow = Health ? Health->GetCurrentHealth() : 0.f;
	if (Need(T::TookHit) && LastHealth >= 0.f && HealthNow < LastHealth - 0.5f && bInFight)
	{
		Hold(T::TookHit);
	}
	LastHealth = HealthNow;

	int32 TrickArrows = 0;
	if (const UInventoryComponent* Inventory = Kate->GetInventoryComponent())
	{
		for (int32 Slot = 3; Slot <= HawkeyeQuiverSlotCount; ++Slot)
		{
			TrickArrows += FMath::Max(Inventory->GetArrowCount(Slot), 0);
		}
	}
	if (Need(T::TrickArrowGained) && TrickArrows > LastTrickArrows)
	{
		Hold(T::TrickArrowGained);
	}
	LastTrickArrows = TrickArrows;

	const bool bNeedThugs = Need(T::ThugClose) || Need(T::ParryTelegraph) || Need(T::FinisherOpen);
	if (bNeedThugs)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const AThugCharacter* Thug = *It;
			const UHealthComponent* ThugHealth = Thug->GetHealthComponent();
			if (!ThugHealth || ThugHealth->IsDead())
			{
				continue;
			}
			const float Distance = FVector::Dist(Thug->GetActorLocation(), At);
			if (Distance <= CloseThug && !Thug->IsIncapacitated())
			{
				Hold(T::ThugClose);
				float Elapsed = 0.f;
				float Windup = 0.f;
				if (HawkeyeCombatReadability::GetStrikeTelegraph(Thug, At, GunnerTelegraphRange, Elapsed, Windup))
				{
					Hold(T::ParryTelegraph);
				}
			}
			if (Distance <= FinisherReach && Thug->IsFinisherOpen() && (Thug->IsKnockedDown() || Thug->IsParryStaggered()))
			{
				Hold(T::FinisherOpen);
			}
		}
	}
	if (Need(T::TakedownReady) && Kate->GetTakedownComponent() && Kate->GetTakedownComponent()->FindTakedownTarget())
	{
		Hold(T::TakedownReady);
	}
	if (Need(T::PartnerNearInFight) && bInFight && Controller->IsSwitchingAllowed())
	{
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			const AHawkeyeCharacter* Partner = It->GetLeader() == Kate ? It->GetPartner() : nullptr;
			if (Partner && FVector::Dist(Partner->GetActorLocation(), At) <= PartnerReach)
			{
				Hold(T::PartnerNearInFight);
				break;
			}
		}
	}
	if (Need(T::UnreadText))
	{
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(this);
		if (Phone && Phone->GetUnreadCount() > 0)
		{
			Hold(T::UnreadText);
		}
	}
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const int32 Discovered = Save ? Save->GetDiscoveredSafehouses().Num() : 0;
	// The moment the second is found (or, loading a campaign with both found, the first frame).
	if (Need(T::BothSafehousesFound) && Discovered >= 2 && LastDiscovered < 2)
	{
		Hold(T::BothSafehousesFound);
	}
	LastDiscovered = Discovered;
	const int32 Noticed = Save ? Save->GetNoticedPlaces().Num() : 0;
	if (LastNoticedPlaces >= 0 && Noticed > LastNoticedPlaces)
	{
		NoticePulseRemaining = NoticePulseSeconds;
	}
	LastNoticedPlaces = Noticed;
	NoticePulseRemaining = FMath::Max(NoticePulseRemaining - DeltaSeconds, 0.f);
	if (Need(T::CrimeActive))
	{
		const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(this);
		if (Crimes && Crimes->IsCrimeActive() && FVector::Dist(Crimes->GetCrimePoint(), At) > CrimeNear)
		{
			Hold(T::CrimeActive);
		}
	}

	// --- Triggers that trace: 5 times a second --------------------------------------------------------------
	ProbeCooldown -= DeltaSeconds;
	if (ProbeCooldown > 0.f)
	{
		OutTriggers |= LastProbed & Needed;
		if (NoticePulseRemaining > 0.f && Need(T::FarWithoutMarker))
		{
			Hold(T::FarWithoutMarker);
		}
		return;
	}
	ProbeCooldown = ProbeSeconds;
	uint64 Probed = 0;
	auto HoldProbed = [&Probed](T Trigger) { Probed |= FHawkeyeHintQueue::Bit(Trigger); };

	const FVector Forward = Kate->GetActorForwardVector().GetSafeNormal2D();
	if (Parkour && bGrounded && !bBusy)
	{
		FHawkeyeParkourObstacle Ahead;
		const bool bSomethingAhead = Parkour->DetectObstacleUpTo(OpenGroundReach, ClimbMaxHeight + 10.f, Ahead) && Ahead.bFound;
		if (Need(T::WalkingOpenGround) && Speed >= MovingSpeed && !Kate->IsSprinting() && !Kate->IsAiming() && !bInFight
			&& !bSomethingAhead)
		{
			HoldProbed(T::WalkingOpenGround);
		}
		if (bSomethingAhead && Need(T::NearVaultable) && Speed >= MovingSpeed && Ahead.Distance <= VaultReach
			&& Parkour->ChooseMoveFor(Ahead, false) == EHawkeyeParkourMove::Vault)
		{
			HoldProbed(T::NearVaultable);
		}
		if (bSomethingAhead && Need(T::FacingClimbableWall) && Ahead.Distance <= ClimbReach && Ahead.Height >= ClimbMinHeight
			&& Ahead.Height <= ClimbMaxHeight)
		{
			const EHawkeyeParkourMove Move = Parkour->ChooseMoveFor(Ahead, false);
			if (Move == EHawkeyeParkourMove::Mantle || Move == EHawkeyeParkourMove::LedgeGrab)
			{
				HoldProbed(T::FacingClimbableWall);
			}
		}
		FHawkeyeParkourObstacle Edge;
		if (Need(T::AtRoofEdge) && Parkour->FindDropEdge(Forward, Edge) && Edge.LandingDrop >= RoofEdgeMinDrop)
		{
			HoldProbed(T::AtRoofEdge);
		}
	}

	FVector ViewAt = At;
	FRotator ViewRotation = Kate->GetActorRotation();
	Controller->GetPlayerViewPoint(ViewAt, ViewRotation);
	const FVector ViewForward = ViewRotation.Vector();
	if (Need(T::ThugAtRange) && Kate->GetInventoryComponent() && Kate->GetInventoryComponent()->HasBow())
	{
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(RangeThugConeDegrees));
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			AThugCharacter* Thug = *It;
			const UHealthComponent* ThugHealth = Thug->GetHealthComponent();
			const FVector ToThug = Thug->GetActorLocation() - ViewAt;
			const float Distance = FVector::Dist(Thug->GetActorLocation(), At);
			if (ThugHealth && !ThugHealth->IsDead() && !Thug->IsIncapacitated() && Distance >= RangeThugMin && Distance <= RangeThugMax
				&& FVector::DotProduct(ToThug.GetSafeNormal(), ViewForward) >= MinDot && Controller->LineOfSightTo(Thug))
			{
				HoldProbed(T::ThugAtRange);
				break;
			}
		}
	}
	if (Need(T::NearSafehouse) && Safehouses)
	{
		for (const ASafehouse* Safehouse : Safehouses->GetSafehouses())
		{
			if (Safehouse && FVector::Dist(Safehouse->GetActorLocation(), At) <= SafehouseReach)
			{
				HoldProbed(T::NearSafehouse);
				break;
			}
		}
	}
	if (Need(T::NearPedestal) && !(Challenges && Challenges->IsRunning()))
	{
		for (TActorIterator<AChallengeStart> It(World); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), At) <= PedestalReach)
			{
				HoldProbed(T::NearPedestal);
				break;
			}
		}
	}
	if (Need(T::FarWithoutMarker) && FVector::Dist2D(StartLocation, At) >= FarFromStart)
	{
		const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(this);
		const UHawkeyeObjectiveWidget* Markers = Hud ? Hud->GetObjectiveMarker() : nullptr;
		const bool bMarkerOnScreen = Markers && Markers->IsMarkerVisible() && Markers->GetMarkerPlacement().bOnScreen;
		if (!bMarkerOnScreen)
		{
			HoldProbed(T::FarWithoutMarker);
		}
	}
	LastProbed = Probed;
	OutTriggers |= Probed & Needed;
	if (NoticePulseRemaining > 0.f && Need(T::FarWithoutMarker))
	{
		Hold(T::FarWithoutMarker);
	}
}

void UHawkeyeHintSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	// Real time: the fade and the 6 s are the reader's, not the game's (a finisher's slow motion, the wheel's 0.2).
	const float Dt = FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.1f);
	const bool bAllowed = IsRunAllowed();
	const bool bEnabled = bAllowed && UHawkeyeSettingsSubsystem::GetCurrentSettings(this).bShowHints;
	const FHawkeyeHintSuppression Suppression = GatherSuppression(PC);

	uint64 Triggers = 0;
	uint64 Actions = 0;
	if (bAllowed)
	{
		// Off in Settings: no triggers to sense (no traces), but what she does is still learned.
		Sense(PC, Suppression.Any() ? 0.f : Dt, bEnabled ? ComputeNeededTriggers() : 0, Triggers, Actions);
	}
	const FHawkeyeHintStep Step = Queue.Tick(Dt, Triggers, Actions, Suppression.Any(), bEnabled,
		[this](FName Id) { return GetShowCount(Id); });

	if (!Step.Started.IsNone())
	{
		SetShowCount(Step.Started, GetShowCount(Step.Started) + 1);
		ShownHistory.Add(Step.Started);
		UE_LOG(LogHawkeye, Log, TEXT("Hint: %s \"%s\"."), *Step.Started.ToString(), *GetShownLineForLog());
	}
	for (const FName& Learned : Step.Learned)
	{
		if (const FHawkeyeHintRule* Rule = UHawkeyeHintRules::FindRule(UHawkeyeHintRules::GetDefaultRules(), Learned))
		{
			if (GetShowCount(Learned) < Rule->MaxShows)
			{
				SetShowCount(Learned, Rule->MaxShows);
				UE_LOG(LogHawkeye, Log, TEXT("Hint: %s learned."), *Learned.ToString());
			}
		}
	}
}
