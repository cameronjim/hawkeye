#include "Player/HawkeyeCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "Combat/BowComponent.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/FinisherComponent.h"
#include "HawkeyePlayerController.h"
#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/TakedownComponent.h"
#include "Combat/WeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/SpringArmComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeAccessibility.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/InventoryComponent.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Player/LocomotionAnim.h"
#include "Player/ParkourComponent.h"
#include "Misc/ScopeExit.h"
#include "Camera/PlayerCameraManager.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "World/InteractionComponent.h"
#include "Partner/HawkeyePartnerController.h"
#include "Combat/AimAssist.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "UObject/SoftObjectPath.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/QuiverWheelMath.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Vfx/HawkeyeVfxMath.h"
#include "Vfx/HawkeyeVfxSubsystem.h"

static TAutoConsoleVariable<int32> CVarHawkeyeDebugMovement(
	TEXT("hawkeye.DebugMovement"),
	0,
	TEXT("1 shows the player's gait, ground speed and fall height on the HUD, for reading playtest screenshots."),
	ECVF_Default);

namespace HawkeyeGasp
{
	/** The Game Animation Sample's S_PlayerInputState variable on SandboxCharacter_CMC. */
	static const FName InputStateName(TEXT("CharacterInputState"));

	/** SandboxCharacter_CMC's analog-stick flag: true when the stick is past its walk range. */
	static const FName FullMovementInputName(TEXT("FullMovementInput"));

	static FStructProperty* FindInputState(const UClass* Class)
	{
		return Class ? FindFProperty<FStructProperty>(Class, InputStateName) : nullptr;
	}

	/**
	 * A member of the user-defined struct by the name its author gave it. The struct's real
	 * property names carry a GUID suffix (WantsToSprint_1_840C...), which is why this matches the
	 * authored name and not the FName.
	 */
	static FBoolProperty* FindFlag(const FStructProperty* InputState, FName FlagName)
	{
		if (!InputState || !InputState->Struct)
		{
			return nullptr;
		}
		const FString Wanted = FlagName.ToString();
		for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
		{
			if (It->GetAuthoredName() == Wanted)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

AHawkeyeCharacter::AHawkeyeCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHawkeyeMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Ticks to blend the aim FOV and swap the body's idle/walk sequence.
	PrimaryActorTick.bCanEverTick = true;

	// Third person: the controller turns the camera boom, and the body turns to face where it
	// is going rather than where the camera is looking.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// The arm sweeps a small sphere on the Camera channel and pulls in when it hits, so backing
	// into a tenement puts the camera at the wall rather than inside it.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = HipCamera.ArmLength;
	CameraBoom->SocketOffset = HipCamera.SocketOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = CameraLagSpeed;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = CameraRotationLagSpeed;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = CameraProbeSize;
	CameraBoom->ProbeChannel = ECC_Camera;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(HipCamera.FieldOfView);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	// She gets her health back when she stays out of trouble; the difficulty sets how long that takes.
	HealthComponent->bRegenerates = true;
	HealthComponent->RegenDelay = UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Normal, EDifficultyStat::RegenDelaySeconds);
	HealthComponent->RegenPerSecond = 10.f;
	TakedownComponent = CreateDefaultSubobject<UTakedownComponent>(TEXT("TakedownComponent"));
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	NoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("NoiseEmitter"));

	// Hands only, the melee fallback while no bow is owned; the bow does the shooting.
	WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	WeaponComponent->bHasWeapon = false;
	BowComponent = CreateDefaultSubobject<UBowComponent>(TEXT("BowComponent"));
	BowComponent->bAimAssist = true;

	// Bow strikes. Numbers from docs/plans/02-prototype.md step 7: a quick jab and a slow heavy
	// that puts a thug on the floor. The lunge stands in for an attack animation (none yet).
	MeleeComponent = CreateDefaultSubobject<UMeleeComponent>(TEXT("MeleeComponent"));
	// The assist (gameplay-semantics.md, "Melee assist"): her swings close on the thug she picked and forgive him.
	MeleeComponent->bAssistSwings = true;
	// Small and at the contact point: the thug's hit flash is what reads (gameplay-semantics.md, "Combat readability").
	MeleeSparkVfx = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/NS_MeleeSpark.NS_MeleeSpark")));
	LightAttack.Name = FName(TEXT("light"));
	LightAttack.Damage = 15.f;
	LightAttack.WindupSeconds = 0.1f;
	LightAttack.RecoverSeconds = 0.2f;
	LightAttack.Range = 120.f;
	// A generous sweep (35 before the assist): punching his general direction lands.
	LightAttack.Radius = 60.f;
	LightAttack.bStagger = true;
	LightAttack.LungeDistance = 20.f;
	LightAttack.AnimRole = ECombatAnimRole::Light1;
	HeavyAttack = LightAttack;
	HeavyAttack.Name = FName(TEXT("heavy"));
	HeavyAttack.AnimRole = ECombatAnimRole::Heavy;
	HeavyAttack.Damage = 35.f;
	HeavyAttack.WindupSeconds = 0.6f;
	HeavyAttack.RecoverSeconds = 0.3f;
	HeavyAttack.bKnockdown = true;
	HeavyAttack.LungeDistance = 40.f;
	HeavyAttack.LungeSeconds = 0.15f;

	// The chain: two quick lights and a harder third that shoves him back 150 cm.
	ComboFollowAttack = LightAttack;
	ComboFollowAttack.Name = FName(TEXT("light2"));
	ComboFollowAttack.LungeDistance = 25.f;
	ComboFollowAttack.AnimRole = ECombatAnimRole::Light2;
	ComboFinishAttack = LightAttack;
	ComboFinishAttack.Name = FName(TEXT("light3"));
	ComboFinishAttack.Damage = 25.f;
	ComboFinishAttack.WindupSeconds = 0.12f;
	ComboFinishAttack.RecoverSeconds = 0.28f;
	ComboFinishAttack.KnockbackDistance = 150.f;
	ComboFinishAttack.KnockbackSeconds = 0.3f;
	ComboFinishAttack.LungeDistance = 35.f;
	ComboFinishAttack.LungeSeconds = 0.12f;
	ComboFinishAttack.AnimRole = ECombatAnimRole::Light3;

	FinisherComponent = CreateDefaultSubobject<UFinisherComponent>(TEXT("FinisherComponent"));

	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
	GrappleComponent = CreateDefaultSubobject<UGrappleComponent>(TEXT("GrappleComponent"));
	ParkourComponent = CreateDefaultSubobject<UParkourComponent>(TEXT("ParkourComponent"));

	// The whole body is visible, to the owner as well: in third person it is what the player
	// looks at. Feet on the bottom of the capsule, facing +X.
	static constexpr float CapsuleHalfHeight = 88.f;
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
		BodyMesh->SetOwnerNoSee(false);
		BodyMesh->bCastDynamicShadow = true;
		BodyMesh->SetCastShadow(true);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = WalkSpeed;
		Movement->MaxWalkSpeedCrouched = CrouchSpeed;
		Movement->NavAgentProps.bCanCrouch = true;
		Movement->bCanWalkOffLedgesWhenCrouching = true;
		Movement->bOrientRotationToMovement = true;
		Movement->bUseControllerDesiredRotation = false;
		Movement->RotationRate = FRotator(0.f, TurnRateDegrees, 0.f);
		// sqrt(2 g h) for a 90 cm apex at standard gravity; BeginPlay recomputes it from JumpHeight.
		Movement->JumpZVelocity = 420.f;
		Movement->AirControl = JumpAirControl;
		Movement->SetCrouchedHalfHeight(CrouchedCapsuleHalfHeight);
	}

	GetCapsuleComponent()->SetCapsuleSize(34.f, CapsuleHalfHeight);

	// The Pawn profile ignores Visibility, so bullets need their own channel to land on the
	// player at all - without this the thugs' shots went straight through.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);
	}
}

void AHawkeyeCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Before BeginPlay, so the bow and everything else that starts then finds her final body.
	ApplyBodyLook();
}

USkeletalMeshComponent* AHawkeyeCharacter::ApplyBodyLook()
{
	if (LookMesh || !BodyLook.IsSet() || !GetMesh())
	{
		return LookMesh;
	}
	LookMesh = HawkeyeBodyLook::Apply(*GetMesh(), BodyLook);
	return LookMesh;
}

USkeletalMeshComponent* AHawkeyeCharacter::GetBodyMesh() const
{
	return LookMesh ? LookMesh.Get() : GetMesh();
}

void AHawkeyeCharacter::BeginPlay()
{
	Super::BeginPlay();

	ApplyTuningToComponents();
	UpdateMaxWalkSpeed();
	bIndoorCamera = AHawkeyeGameMode::WantsInteriorCamera(GetWorld());
	UpdateCamera(0.f);

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AHawkeyeCharacter::HandleDeath);
		HealthComponent->OnStaggered.AddDynamic(this, &AHawkeyeCharacter::HandleStaggered);
		HealthComponent->OnHealthChanged.AddDynamic(this, &AHawkeyeCharacter::HandleHealthChanged);
	}
	if (MeleeComponent)
	{
		MeleeComponent->OnAttackLanded.AddDynamic(this, &AHawkeyeCharacter::HandleMeleeLanded);
		MeleeComponent->OnAttackMissed.AddDynamic(this, &AHawkeyeCharacter::HandleMeleeMissed);
		MeleeComponent->OnComboWindowChanged.AddDynamic(this, &AHawkeyeCharacter::HandleComboWindowChanged);
	}
	ApplyCombatAnimSet();
	if (FinisherComponent)
	{
		FinisherComponent->OnFinisherStarted.AddDynamic(this, &AHawkeyeCharacter::HandleFinisherStarted);
		FinisherComponent->OnFinisherStruck.AddDynamic(this, &AHawkeyeCharacter::HandleFinisherStruck);
	}

	UpdateBodyLocomotion();
	SilenceForeignCameras();

	BindToSettingsSubsystem();

	// Thugs hear the player through AISense_Hearing; MakeNoise on a fixed beat is enough
	// resolution for a stealth game and costs nothing per frame.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			NoiseTimerHandle, this, &AHawkeyeCharacter::EmitMovementNoise, NoiseIntervalSeconds, true);
	}
}

void AHawkeyeCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// BeginPlay may have run before the game instance had its subsystems; possession is the
	// second, reliable chance to pick the player's sensitivity up.
	BindToSettingsSubsystem();

	// The sample's graph may switch its own camera rig on as it is possessed.
	SilenceForeignCameras();
	ApplyCameraPitchLimits();
}

void AHawkeyeCharacter::ApplyCameraPitchLimits()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (CameraManager && (CameraManager->ViewPitchMin != CameraPitchMin || CameraManager->ViewPitchMax != CameraPitchMax))
	{
		CameraManager->ViewPitchMin = CameraPitchMin;
		CameraManager->ViewPitchMax = CameraPitchMax;
	}
}

void AHawkeyeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromSettingsSubsystem();
	CloseQuiverWheel(/*bSelect=*/false);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NoiseTimerHandle);
		if (World->GetTimerManager().IsTimerActive(HitStopTimerHandle))
		{
			World->GetTimerManager().ClearTimer(HitStopTimerHandle);
			EndHitStop();
		}
	}

	Super::EndPlay(EndPlayReason);
}

float AHawkeyeCharacter::GetMovementNoiseLoudness() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return 0.f;
	}

	if (bIsCrouched)
	{
		return 0.f;
	}

	if (GetVelocity().SizeSquared2D() < FMath::Square(10.f))
	{
		return 0.f;
	}

	return bIsSprinting ? SprintNoiseLoudness : WalkNoiseLoudness;
}

void AHawkeyeCharacter::EmitMovementNoise()
{
	const float Loudness = GetMovementNoiseLoudness();
	if (Loudness <= 0.f)
	{
		return;
	}

	MakeNoise(Loudness, this, GetActorLocation());
}

void AHawkeyeCharacter::HandleDeath(UHealthComponent* /*Health*/, AActor* Killer)
{
	// Once a fight the partner gets her back up. Either way she is down first, for DownedMaxSeconds at
	// most: the revive has to land by then, and with none coming any key ends it sooner.
	if (TryPartnerRevive(Killer))
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s is down (killer: %s); no revive is coming, %.0f s or any key to the last save."),
		*GetName(), *GetNameSafe(Killer), DownedMaxSeconds);
	EnterDowned(/*bInReviveExpected=*/false, nullptr);
}

void AHawkeyeCharacter::EnterDowned(bool bInReviveExpected, APawn* InReviver)
{
	bDowned = true;
	bReviveExpected = bInReviveExpected;
	bDiedFromDown = false;
	DownedSeconds = 0.f;
	Reviver = InReviver;
	StopAim();
	Crouch();
}

float AHawkeyeCharacter::GetDownedFractionLeft() const
{
	return bDowned ? FMath::Clamp(1.f - DownedSeconds / FMath::Max(DownedMaxSeconds, 0.01f), 0.f, 1.f) : 0.f;
}

void AHawkeyeCharacter::AdvanceDowned(float DeltaSeconds)
{
	if (!bDowned || bDiedFromDown)
	{
		return;
	}
	DownedSeconds += FMath::Max(DeltaSeconds, 0.f);
	if (DownedSeconds >= DownedMaxSeconds)
	{
		DieFromDown(bReviveExpected ? TEXT("the revive did not land in time") : TEXT("the down ran out"));
	}
}

bool AHawkeyeCharacter::GiveUpFromDown()
{
	if (!bDowned || bDiedFromDown || bReviveExpected || DownedSeconds < DownedGiveUpGraceSeconds)
	{
		return false;
	}
	DieFromDown(TEXT("a key while no revive was coming"));
	return true;
}

void AHawkeyeCharacter::DieFromDown(const TCHAR* Why)
{
	bDiedFromDown = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s died after %.1f s down (%s); loading the last save."), *GetName(), DownedSeconds, Why);
	// Back to the last autosave behind a fade, not to the top of the chapter.
	if (AHawkeyeGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AHawkeyeGameMode>() : nullptr)
	{
		GameMode->ReloadLastCheckpoint();
	}
}

bool AHawkeyeCharacter::TryPartnerRevive(AActor* Killer)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		if (It->GetLeader() == this && It->RequestRevive(this))
		{
			EnterDowned(/*bInReviveExpected=*/true, It->GetPawn());
			UE_LOG(LogHawkeye, Log, TEXT("%s is down (killer: %s); %s is coming to revive."), *GetName(),
				*GetNameSafe(Killer), *GetNameSafe(It->GetPawn()));
			return true;
		}
	}
	return false;
}

void AHawkeyeCharacter::ReviveFromDown(float HealthFraction)
{
	if (!bDowned || bDiedFromDown || !HealthComponent)
	{
		return;
	}
	bDowned = false;
	bReviveExpected = false;
	DownedSeconds = 0.f;
	Reviver.Reset();
	HealthComponent->Revive(HealthComponent->GetMaxHealth() * FMath::Clamp(HealthFraction, 0.01f, 1.f));
	EndCrouch(TEXT("revived from the down"));
	UpdateMaxWalkSpeed();
	UE_LOG(LogHawkeye, Log, TEXT("%s: revived at %.0f health."), *GetName(), HealthComponent->GetCurrentHealth());
}

// --- Save ------------------------------------------------------------------------------------------

FString AHawkeyeCharacter::OverrideName_Implementation() const
{
	return CharacterName.IsEmpty() ? FString() : CharacterName.ToString();
}

void AHawkeyeCharacter::SpudPreStore_Implementation(const USpudState* /*State*/)
{
	CaptureSaveSnapshot();
}

void AHawkeyeCharacter::SpudPostRestore_Implementation(const USpudState* /*State*/)
{
	ApplySaveSnapshot();
}

void AHawkeyeCharacter::CaptureSaveSnapshot()
{
	SavedHealth = HealthComponent ? HealthComponent->GetCurrentHealth() : -1.f;
	SavedBowPath.Reset();
	SavedArrowPaths.Reset();
	SavedArrowCounts.Reset();
	SavedKeycards.Reset();
	SavedActiveArrowSlot = 1;
	if (!InventoryComponent)
	{
		return;
	}
	SavedBowPath = InventoryComponent->GetBow() ? FSoftObjectPath(InventoryComponent->GetBow()).ToString() : FString();
	for (const FHawkeyeQuiverSlot& Entry : InventoryComponent->GetArrowSlots())
	{
		// A transient stand-in (tests, a missing asset) has no path to come back from.
		if (Entry.Arrow && Entry.Arrow->IsAsset())
		{
			SavedArrowPaths.Add(FSoftObjectPath(Entry.Arrow).ToString());
			SavedArrowCounts.Add(Entry.Count);
		}
	}
	SavedActiveArrowSlot = InventoryComponent->GetActiveArrowSlot();
	SavedKeycards = InventoryComponent->GetKeycards().Array();
	SavedKeycards.Sort(FNameLexicalLess());
}

void AHawkeyeCharacter::ApplySaveSnapshot()
{
	if (SavedHealth < 0.f)
	{
		return;
	}
	if (HealthComponent && SavedHealth > 0.f)
	{
		HealthComponent->Revive(SavedHealth);
	}
	if (!InventoryComponent)
	{
		return;
	}
	UBowDefinition* SavedBow = SavedBowPath.IsEmpty() ? nullptr : LoadObject<UBowDefinition>(nullptr, *SavedBowPath);
	TArray<FHawkeyeQuiverSlot> Slots;
	for (int32 Index = 0; Index < SavedArrowPaths.Num() && Index < SavedArrowCounts.Num(); ++Index)
	{
		UArrowDefinition* Arrow = LoadObject<UArrowDefinition>(nullptr, *SavedArrowPaths[Index]);
		if (!Arrow)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: saved arrow %s no longer exists; dropped."), *GetName(), *SavedArrowPaths[Index]);
			continue;
		}
		FHawkeyeQuiverSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Arrow = Arrow;
		Slot.Count = SavedArrowCounts[Index];
	}
	InventoryComponent->RestoreQuiver(SavedBow, Slots, SavedActiveArrowSlot);
	for (const FName Keycard : SavedKeycards)
	{
		InventoryComponent->GiveKeycard(Keycard);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: restored from the save at %.0f health, bow %s, %d arrow type(s)."), *GetName(),
		SavedHealth, *GetNameSafe(SavedBow), Slots.Num());
}

FText AHawkeyeCharacter::GetCharacterName() const
{
	return CharacterName.IsEmpty() ? FText::FromString(GetName()) : CharacterName;
}

FString AHawkeyeCharacter::GetSwitchBlocker() const
{
	if (bDowned || (HealthComponent && !HealthComponent->IsAlive()))
	{
		return TEXT("down");
	}
	if (IsZipping())
	{
		return TEXT("mid-zip");
	}
	if (IsLockedOutByTakedown())
	{
		return TEXT("mid-takedown");
	}
	if (IsTraversing())
	{
		return TEXT("mid-traversal");
	}
	return FString();
}

void AHawkeyeCharacter::ReleaseHeldInputs()
{
	StopSprintToggle(EHawkeyeSprintStop::Released);
	bIsSprinting = false;
	bAimInputHeld = false;
	bMeleeHeld = false;
	bLightBuffered = false;
	bCrouchTapPending = false;
	bInventoryKeyHeld = false;
	MoveInputMagnitude = 0.f;
	MoveInputHeldSeconds = 0.f;
	CloseQuiverWheel(/*bSelect=*/false);
	StopAim();
	UpdateMaxWalkSpeed();
}

bool AHawkeyeCharacter::HasKeycard(FName KeycardId) const
{
	// The ring lives in the inventory now; doors and tests still ask the pawn.
	return InventoryComponent && InventoryComponent->HasKeycard(KeycardId);
}

bool AHawkeyeCharacter::GiveKeycard(FName KeycardId)
{
	return InventoryComponent && InventoryComponent->GiveKeycard(KeycardId);
}

TSet<FName> AHawkeyeCharacter::GetKeycards() const
{
	return InventoryComponent ? InventoryComponent->GetKeycards() : TSet<FName>();
}

void AHawkeyeCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// Super bound the Blueprint input events after SetupPlayerInputComponent ran; drop them here.
	DropBlueprintInputBindings();
	AddDefaultMappingContext();
	SilenceForeignCameras();
	ApplyCameraPitchLimits();
}

void AHawkeyeCharacter::DropBlueprintInputBindings()
{
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput || NativeActionBindingCount == INDEX_NONE || !UsesGaspLocomotion())
	{
		return;
	}

	int32 Dropped = 0;
	for (int32 Index = EnhancedInput->GetActionEventBindings().Num() - 1; Index >= NativeActionBindingCount; --Index)
	{
		Dropped += EnhancedInput->RemoveActionEventBinding(Index) ? 1 : 0;
	}
	// Raw key events (the sample's mouse wheel and d-pad camera controls); we bind none of our own.
	Dropped += EnhancedInput->KeyBindings.Num();
	EnhancedInput->KeyBindings.Reset();

	DroppedBlueprintInputBindings = Dropped;
	UE_LOG(LogHawkeye, Log, TEXT("%s: dropped %d Blueprint input binding(s); the sandbox graph reads CharacterInputState instead."),
		*GetNameSafe(this), Dropped);
}

void AHawkeyeCharacter::SilenceForeignCameras()
{
	if (!UsesGaspLocomotion())
	{
		return;
	}

	TInlineComponentArray<UActorComponent*> Components(this);
	for (UActorComponent* Component : Components)
	{
		if (!Component || Component == FollowCamera || !Component->IsActive())
		{
			continue;
		}
		// The sample's GameplayCamera component (GameplayCameras plugin, which this module does
		// not link) is matched by class name; it owns and drives a cine camera it spawns.
		const bool bCamera = Component->IsA<UCameraComponent>()
			|| Component->GetClass()->GetName().Contains(TEXT("GameplayCamera"));
		if (!bCamera)
		{
			continue;
		}
		Component->Deactivate();
		if (!bLoggedForeignCamera)
		{
			bLoggedForeignCamera = true;
			UE_LOG(LogHawkeye, Log, TEXT("%s: switched off the sample's camera %s (%s); the spring arm is the view."),
				*GetNameSafe(this), *Component->GetName(), *Component->GetClass()->GetName());
		}
	}
}

bool AHawkeyeCharacter::UsesGaspLocomotion() const
{
	return HawkeyeGasp::FindInputState(GetClass()) != nullptr;
}

bool AHawkeyeCharacter::IsBodyDrivenByAnimBlueprint() const
{
	const USkeletalMeshComponent* Body = GetMesh();
	return Body && Body->GetAnimationMode() == EAnimationMode::AnimationBlueprint && Body->AnimClass != nullptr;
}

bool AHawkeyeCharacter::GetGaspInputFlag(FName FlagName) const
{
	const FStructProperty* InputState = HawkeyeGasp::FindInputState(GetClass());
	const FBoolProperty* Flag = HawkeyeGasp::FindFlag(InputState, FlagName);
	return Flag && Flag->GetPropertyValue_InContainer(InputState->ContainerPtrToValuePtr<void>(this));
}

void AHawkeyeCharacter::SyncGaspInputState()
{
	FStructProperty* InputState = HawkeyeGasp::FindInputState(GetClass());
	if (!InputState)
	{
		return;
	}

	void* State = InputState->ContainerPtrToValuePtr<void>(this);
	auto SetFlag = [InputState, State](const TCHAR* Name, bool bValue)
	{
		if (FBoolProperty* Flag = HawkeyeGasp::FindFlag(InputState, FName(Name)))
		{
			Flag->SetPropertyValue_InContainer(State, bValue);
		}
	};

	SetFlag(TEXT("WantsToSprint"), CurrentGait == EHawkeyeGait::Sprint);
	// Drawing forces the aim, and the aim walks: that is the sample's share of the draw slow-down.
	SetFlag(TEXT("WantsToWalk"), CurrentGait == EHawkeyeGait::Walk || bIsAiming || IsDrawingBow());
	SetFlag(TEXT("WantsToStrafe"), bIsAiming);
	SetFlag(TEXT("WantsToAim"), bIsAiming);
	SetFlag(TEXT("WantsToCrouch"), bIsCrouched || bIsSliding);
	// The sample has no attack input today; if a later version adds one, it is fed from here.
	SetFlag(TEXT("WantsToAttack"), IsMeleeAttacking());

	if (FBoolProperty* FullInput = FindFProperty<FBoolProperty>(GetClass(), HawkeyeGasp::FullMovementInputName))
	{
		FullInput->SetPropertyValue_InContainer(this, CurrentGait != EHawkeyeGait::Walk);
	}
}

void AHawkeyeCharacter::AddDefaultMappingContext()
{
	if (!DefaultMappingContext)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultMappingContext, DefaultMappingPriority);
	}
}

UWeaponComponent* AHawkeyeCharacter::GetWeaponComponent() const
{
	return WeaponComponent ? WeaponComponent.Get() : FindComponentByClass<UWeaponComponent>();
}

void AHawkeyeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogHawkeye, Error,
			TEXT("Expected an EnhancedInputComponent; check DefaultEngine.ini input component class."));
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHawkeyeCharacter::Input_Move);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHawkeyeCharacter::Input_Look);
	}
	if (LookStickAction)
	{
		EnhancedInput->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &AHawkeyeCharacter::Input_LookStick);
	}
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_SprintStarted);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_SprintCompleted);
	}
	if (CrouchAction)
	{
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_CrouchToggle);
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_CrouchReleased);
	}
	if (FireAction)
	{
		// Hold to draw, let go to loose.
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_FirePressed);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_FireReleased);
	}
	if (AimAction)
	{
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_AimStarted);
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_AimCompleted);
	}
	if (ReloadAction)
	{
		EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Reload);
	}
	if (TakedownAction)
	{
		EnhancedInput->BindAction(TakedownAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Takedown);
	}
	if (InteractAction)
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Interact);
	}
	if (Slot1Action)
	{
		EnhancedInput->BindAction(Slot1Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot1);
	}
	if (Slot2Action)
	{
		EnhancedInput->BindAction(Slot2Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot2);
	}
	if (Slot3Action)
	{
		EnhancedInput->BindAction(Slot3Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot3);
	}
	if (Slot4Action)
	{
		EnhancedInput->BindAction(Slot4Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot4);
	}
	if (Slot5Action)
	{
		EnhancedInput->BindAction(Slot5Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot5);
	}
	if (Slot6Action)
	{
		EnhancedInput->BindAction(Slot6Action, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Slot6);
	}
	if (SlotScrollAction)
	{
		// Triggered, not Started: the wheel is an axis and every notch is its own value.
		EnhancedInput->BindAction(SlotScrollAction, ETriggerEvent::Triggered, this, &AHawkeyeCharacter::Input_SlotScroll);
	}
	if (InventoryAction)
	{
		// Started starts the hold clock; Completed decides: a tap is the inventory, a hold the wheel.
		EnhancedInput->BindAction(InventoryAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Inventory);
		EnhancedInput->BindAction(
			InventoryAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_InventoryReleased);
	}
	if (GrappleAction)
	{
		EnhancedInput->BindAction(GrappleAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_Grapple);
	}
	if (MeleeAction)
	{
		// Tap for the light, hold for the heavy: the release decides which, or the hold running out.
		EnhancedInput->BindAction(MeleeAction, ETriggerEvent::Started, this, &AHawkeyeCharacter::Input_MeleePressed);
		EnhancedInput->BindAction(MeleeAction, ETriggerEvent::Completed, this, &AHawkeyeCharacter::Input_MeleeReleased);
	}

	// Everything bound after this point is a Blueprint's; see DropBlueprintInputBindings.
	NativeActionBindingCount = EnhancedInput->GetActionEventBindings().Num();
}

void AHawkeyeCharacter::Input_Slot1(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(1);
	}
}

void AHawkeyeCharacter::Input_Slot2(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(2);
	}
}

void AHawkeyeCharacter::Input_Slot3(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(3);
	}
}

void AHawkeyeCharacter::Input_Slot4(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(4);
	}
}

void AHawkeyeCharacter::Input_Slot5(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(5);
	}
}

void AHawkeyeCharacter::Input_Slot6(const FInputActionValue& /*Value*/)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectArrowSlot(6);
	}
}

void AHawkeyeCharacter::Input_SlotScroll(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (!InventoryComponent || FMath::IsNearlyZero(Axis))
	{
		return;
	}

	if (Axis > 0.f)
	{
		InventoryComponent->SelectNextArrowSlot();
	}
	else
	{
		InventoryComponent->SelectPreviousArrowSlot();
	}
}

void AHawkeyeCharacter::Input_Inventory(const FInputActionValue& /*Value*/)
{
	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetController());
	if (PC && PC->IsInventoryOpen())
	{
		// Tab over the open inventory closes it straight away.
		PC->SetInventoryOpen(false);
		return;
	}
	const UWorld* World = GetWorld();
	bInventoryKeyHeld = true;
	InventoryKeyDownRealSeconds = World ? World->GetRealTimeSeconds() : 0.0;
}

void AHawkeyeCharacter::Input_InventoryReleased(const FInputActionValue& /*Value*/)
{
	const bool bWasTap = bInventoryKeyHeld && !bQuiverWheelOpen;
	bInventoryKeyHeld = false;
	if (bQuiverWheelOpen)
	{
		CloseQuiverWheel(/*bSelect=*/true);
		return;
	}
	if (bWasTap)
	{
		if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetController()))
		{
			PC->ToggleInventory();
		}
	}
}

void AHawkeyeCharacter::UpdateQuiverWheelHold()
{
	const UWorld* World = GetWorld();
	// Real time: the hold is the player's thumb, not game time, and the wheel itself slows the game.
	if (bInventoryKeyHeld && !bQuiverWheelOpen && World
		&& World->GetRealTimeSeconds() - InventoryKeyDownRealSeconds >= QuiverWheelHoldSeconds)
	{
		OpenQuiverWheel();
	}
}

void AHawkeyeCharacter::OpenQuiverWheel()
{
	if (bQuiverWheelOpen || !InventoryComponent)
	{
		return;
	}
	bQuiverWheelOpen = true;
	QuiverWheelCursor = FVector2D::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, QuiverWheelTimeDilation);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: quiver wheel open, time x%.2f."), *GetNameSafe(this), QuiverWheelTimeDilation);
	RefreshQuiverWheelHud();
}

void AHawkeyeCharacter::CloseQuiverWheel(bool bSelect)
{
	if (!bQuiverWheelOpen)
	{
		return;
	}
	const int32 Slot = GetQuiverWheelHighlight();
	bQuiverWheelOpen = false;
	bInventoryKeyHeld = false;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	}
	const bool bPicked = bSelect && Slot > 0 && InventoryComponent && InventoryComponent->SelectArrowSlot(Slot);
	UE_LOG(LogHawkeye, Log, TEXT("%s: quiver wheel closed on slot %d%s; slot %d nocked."), *GetNameSafe(this), Slot,
		bPicked ? TEXT("") : TEXT(" (kept)"), InventoryComponent ? InventoryComponent->GetActiveArrowSlot() : 0);
	RefreshQuiverWheelHud();
}

void AHawkeyeCharacter::SetQuiverWheelCursor(FVector2D Cursor)
{
	QuiverWheelCursor = Cursor.GetClampedToMaxSize(QuiverWheelCursorRadius);
	RefreshQuiverWheelHud();
}

int32 AHawkeyeCharacter::GetQuiverWheelHighlight() const
{
	return UQuiverWheelMath::ComputeWheelSegment(QuiverWheelCursor, HawkeyeQuiverSlotCount, QuiverWheelDeadZone);
}

void AHawkeyeCharacter::RefreshQuiverWheelHud() const
{
	const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetController());
	if (UHawkeyeHudWidget* Hud = PC ? PC->GetHawkeyeHud() : nullptr)
	{
		Hud->SetQuiverWheelState(bQuiverWheelOpen, GetQuiverWheelHighlight());
	}
}

void AHawkeyeCharacter::PlayImpactShake(float InSeconds, float InAmplitude)
{
	const float Seconds = InSeconds;
	const float Amplitude = InAmplitude * CameraShakeScale;
	if (Seconds <= 0.f)
	{
		return;
	}
	// A bigger shake wins over the tail of a smaller one; never cut a stronger one short.
	if (HitShakeRemaining > 0.f && ActiveShakeAmplitude * GetHitShakeAlpha() > Amplitude)
	{
		return;
	}
	ActiveShakeSeconds = Seconds;
	ActiveShakeAmplitude = Amplitude;
	HitShakeRemaining = Seconds;
}

void AHawkeyeCharacter::SuppressMotionBlur(float Seconds)
{
	if (Seconds <= 0.f || !FollowCamera)
	{
		return;
	}
	FPostProcessSettings& Settings = FollowCamera->PostProcessSettings;
	if (MotionBlurOffRemaining <= 0.f)
	{
		bSavedMotionBlurOverride = Settings.bOverride_MotionBlurAmount != 0;
		SavedMotionBlurAmount = Settings.MotionBlurAmount;
	}
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.f;
	MotionBlurOffRemaining = FMath::Max(MotionBlurOffRemaining, Seconds);
}

void AHawkeyeCharacter::Input_Grapple(const FInputActionValue& /*Value*/)
{
	if (GrappleComponent && !IsLockedOutByTakedown() && !IsTraversing())
	{
		GrappleComponent->TryFire();
	}
}

UHawkeyeMovementComponent* AHawkeyeCharacter::GetHawkeyeMovement() const
{
	return Cast<UHawkeyeMovementComponent>(GetCharacterMovement());
}

bool AHawkeyeCharacter::IsZipping() const
{
	return GrappleComponent && GrappleComponent->IsZipping();
}

bool AHawkeyeCharacter::IsTraversing() const
{
	return ParkourComponent && ParkourComponent->IsBusy();
}

void AHawkeyeCharacter::NotifyGrappleLanded()
{
	// A zip ends on its landing point: no fall, so no damage and no roll. The roll placeholder
	// halved the speed on arrival, which made chaining off a landing feel like wading.
	FallApexZ = GetActorLocation().Z;
	ApplyLanding(0.f);
	UpdateMaxWalkSpeed();
}

void AHawkeyeCharacter::Input_Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	// The sprint toggle only cares whether the thumb is on the stick, locked out or not (a zip, a vault).
	bStickPushedThisFrame |= !MoveInput.IsNearlyZero();
	const bool bParkourLock = ParkourComponent && ParkourComponent->IsLockingInput();
	const bool bDead = HealthComponent && !HealthComponent->IsAlive();
	// Hanging, the stick is the hang's: along the ledge shimmies, up climbs when held, and it picks a hang jump.
	if (ParkourComponent && ParkourComponent->IsHanging() && Controller && !bDead && !MoveInput.IsNearlyZero())
	{
		const FRotator HangYaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		ParkourComponent->SetHangInput(FRotationMatrix(HangYaw).GetUnitAxis(EAxis::X) * MoveInput.Y
			+ FRotationMatrix(HangYaw).GetUnitAxis(EAxis::Y) * MoveInput.X);
		return;
	}
	if (MoveInput.IsNearlyZero() || !Controller || IsLockedOutByTakedown() || IsZipping() || bParkourLock || bDead)
	{
		return;
	}

	// Remembered even when the move itself is locked out, so a dodge out of a swing goes where the
	// stick points now.
	const FRotator InputYaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	LastMoveWorldDirection = (FRotationMatrix(InputYaw).GetUnitAxis(EAxis::X) * MoveInput.Y
		+ FRotationMatrix(InputYaw).GetUnitAxis(EAxis::Y) * MoveInput.X).GetSafeNormal2D();
	LastMoveInputSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// A swing, a dodge, a stagger or the start of a landing roll owns the body for its length.
	if (IsMeleeAttacking() || IsDodging() || IsStaggered() || IsLandingInputLocked())
	{
		return;
	}

	// Keyboard diagonals come in at 1.41; the gait only cares how hard the stick is pushed.
	MoveInputMagnitude = FMath::Min(MoveInput.Size(), 1.f);
	bMoveInputThisFrame = true;
	UpdateMaxWalkSpeed();

	// A slide keeps the direction it started in; UpdateSlide does the pushing.
	if (bIsSliding)
	{
		return;
	}

	// The sample's graph owns MaxWalkSpeed, so a stumble slows her through the input instead: the
	// movement component caps speed by how hard the stick is pushed.
	const float InputScale = UsesGaspLocomotion() ? GetLandingSpeedFactor() : 1.f;
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MoveInput.Y * InputScale);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MoveInput.X * InputScale);
}

void AHawkeyeCharacter::BindToSettingsSubsystem()
{
	UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this);
	if (!SettingsSubsystem)
	{
		// No game instance: an automation world. LookSensitivity is the fallback.
		return;
	}

	ApplySettings(SettingsSubsystem->GetSettings());

	if (!SettingsSubsystem->OnSettingsChanged.IsAlreadyBound(this, &AHawkeyeCharacter::HandleSettingsChanged))
	{
		SettingsSubsystem->OnSettingsChanged.AddDynamic(this, &AHawkeyeCharacter::HandleSettingsChanged);
	}
}

void AHawkeyeCharacter::UnbindFromSettingsSubsystem()
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->OnSettingsChanged.RemoveDynamic(this, &AHawkeyeCharacter::HandleSettingsChanged);
	}
}

void AHawkeyeCharacter::HandleSettingsChanged(FHawkeyeSettings NewSettings)
{
	// Live, so the slider can be felt while the pause menu is still open.
	ApplySettings(NewSettings);
}

void AHawkeyeCharacter::ApplySettings(const FHawkeyeSettings& NewSettings)
{
	SettingsLookSensitivity = NewSettings.LookSensitivity;
	bHasSettingsLookSensitivity = true;

	SettingsStickSensitivity = NewSettings.StickSensitivity;
	bHasSettingsStickSensitivity = true;

	bInvertMouseY = NewSettings.bInvertMouseY;
	bInvertStickY = NewSettings.bInvertStickY;

	bToggleAim = NewSettings.bToggleAim;
	bToggleCrouch = NewSettings.bToggleCrouch;
	SprintMode = NewSettings.SprintMode;
	CameraShakeScale = UHawkeyeAccessibility::GetCameraShakeScale(NewSettings.bReduceCameraShake);
	FlashScale = UHawkeyeAccessibility::GetFlashScale(NewSettings.bReduceFlashing);

	const EHawkeyeDifficulty Difficulty = NewSettings.Difficulty;
	FallDamageScale = UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::FallDamage);
	ParryWindowDelta = UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::ParryWindowSeconds);
	if (HealthComponent)
	{
		HealthComponent->RegenDelay = UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::RegenDelaySeconds);
	}
	if (InventoryComponent)
	{
		InventoryComponent->SetTrickArrowCapBonus(FMath::RoundToInt(
			UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::TrickArrowCapBonus)));
	}
}

float AHawkeyeCharacter::GetEffectiveLookSensitivity() const
{
	const float Base = bHasSettingsLookSensitivity ? SettingsLookSensitivity : LookSensitivity;
	return bIsAiming ? Base * AimLookMultiplier : Base;
}

FVector2D AHawkeyeCharacter::ComputeLookDelta(FVector2D RawInput, bool bAiming) const
{
	const float Base = bHasSettingsLookSensitivity ? SettingsLookSensitivity : LookSensitivity;
	FVector2D Delta = RawInput * (bAiming ? Base * AimLookMultiplier : Base);
	if (bInvertMouseY)
	{
		Delta.Y = -Delta.Y;
	}
	return Delta;
}

void AHawkeyeCharacter::Input_Look(const FInputActionValue& Value)
{
	if (bQuiverWheelOpen)
	{
		// The mouse steers the wheel's cursor instead of the camera. IA_Look negates Y, so up is -Y.
		const FVector2D Raw = Value.Get<FVector2D>();
		SetQuiverWheelCursor(QuiverWheelCursor + FVector2D(Raw.X, -Raw.Y));
		return;
	}
	const FVector2D LookInput = ApplyAimSlowdown(ComputeLookDelta(Value.Get<FVector2D>(), bIsAiming), /*bFromStick=*/false);
	bLookFromStick = false;

	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

FVector2D AHawkeyeCharacter::ComputeStickLookDelta(FVector2D RawInput, float DeltaSeconds, bool bAiming) const
{
	if (DeltaSeconds <= 0.f)
	{
		return FVector2D::ZeroVector;
	}

	const float Sensitivity = bHasSettingsStickSensitivity ? SettingsStickSensitivity : StickSensitivity;
	const float RateMultiplier = (bAiming ? AimStickRateMultiplier : 1.f) * Sensitivity * DeltaSeconds;

	// value^1.5 (sign preserved): a light nudge stays fine for small aim corrections while the
	// stick still reaches full rate at the edge, unlike a straight linear response.
	auto Ease = [](float Axis)
	{
		return FMath::Sign(Axis) * FMath::Pow(FMath::Abs(Axis), 1.5f);
	};

	FVector2D Delta(
		Ease(RawInput.X) * StickYawDegreesPerSecond * RateMultiplier,
		Ease(RawInput.Y) * StickPitchDegreesPerSecond * RateMultiplier);
	if (bInvertStickY)
	{
		Delta.Y = -Delta.Y;
	}
	return Delta;
}

void AHawkeyeCharacter::Input_LookStick(const FInputActionValue& Value)
{
	const FVector2D RawInput = Value.Get<FVector2D>();
	if (bQuiverWheelOpen)
	{
		// The stick points straight at a segment; let go and the cursor stays where it was aimed.
		if (!RawInput.IsNearlyZero())
		{
			SetQuiverWheelCursor(FVector2D(RawInput.X, -RawInput.Y) * QuiverWheelCursorRadius);
		}
		return;
	}
	if (RawInput.IsNearlyZero())
	{
		return;
	}

	const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const FVector2D LookDelta = ApplyAimSlowdown(ComputeStickLookDelta(RawInput, DeltaSeconds, bIsAiming), /*bFromStick=*/true);
	bLookFromStick = true;

	AddControllerYawInput(LookDelta.X);
	AddControllerPitchInput(LookDelta.Y);
}

void AHawkeyeCharacter::Input_SprintStarted(const FInputActionValue& /*Value*/)
{
	// The controller sees every key before the action fires, so its last device is the one that pressed sprint.
	const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetController());
	PressSprint(PC && PC->IsUsingGamepad());
}

void AHawkeyeCharacter::Input_SprintCompleted(const FInputActionValue& /*Value*/)
{
	ReleaseSprint();
}

void AHawkeyeCharacter::PressSprint(bool bFromGamepad)
{
	if (FHawkeyeSprintToggle::UsesToggle(SprintMode, bFromGamepad))
	{
		if (!SprintToggle.Press())
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: sprint toggle off (second press)"), *GetNameSafe(this));
			bIsSprinting = false;
			UpdateMaxWalkSpeed();
			return;
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: sprint toggle on (%s)"), *GetNameSafe(this), bFromGamepad ? TEXT("pad") : TEXT("keys"));
	}
	else
	{
		// A held press takes over from a toggle left on by the other device.
		SprintToggle.Stop();
	}
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: sprint start at %.0f cm/s"), *GetNameSafe(this), GetVelocity().Size2D());
	bIsSprinting = true;

	// You cannot sprint down the sights; the aim (and any draw) drops before the speed goes up.
	StopAim();
	UpdateMaxWalkSpeed();

	// Sprinting cancels a reload; the magazine keeps whatever it had.
	if (UWeaponComponent* Weapon = GetWeaponComponent())
	{
		Weapon->CancelReload();
	}
}

void AHawkeyeCharacter::ReleaseSprint()
{
	if (SprintToggle.IsOn())
	{
		// Toggled: the release means nothing; the next press, the stick or an aim ends it.
		return;
	}
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: sprint stop at %.0f cm/s"), *GetNameSafe(this), GetVelocity().Size2D());
	bIsSprinting = false;
	UpdateMaxWalkSpeed();
}

bool AHawkeyeCharacter::StopSprintToggle(EHawkeyeSprintStop Why)
{
	if (!SprintToggle.Stop())
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: sprint toggle off (%s)"), *GetNameSafe(this), FHawkeyeSprintToggle::StopName(Why));
	bIsSprinting = false;
	UpdateMaxWalkSpeed();
	return true;
}

void AHawkeyeCharacter::UpdateSprintToggle(float DeltaSeconds)
{
	const bool bCentred = !bStickPushedThisFrame;
	bStickPushedThisFrame = false;
	if (SprintToggle.IsOn() && SprintToggle.Tick(DeltaSeconds, bCentred) == EHawkeyeSprintStop::StickCentred)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: sprint toggle off (stick centred for %.1f s)"), *GetNameSafe(this),
			SprintToggle.CentreSeconds);
		bIsSprinting = false;
		UpdateMaxWalkSpeed();
	}
}

void AHawkeyeCharacter::Input_AimStarted(const FInputActionValue& /*Value*/)
{
	PressAim();
}

void AHawkeyeCharacter::PressAim()
{
	// A toggled sprint gives way to the aim (a held one still refuses it, as before).
	StopSprintToggle(EHawkeyeSprintStop::Aim);
	// Toggled, the press flips what she is doing now (a sprint may have dropped the aim since).
	bAimInputHeld = UHawkeyeAccessibility::ResolvePress(bToggleAim, bToggleAim ? bIsAiming : bAimInputHeld);
	if (bAimInputHeld)
	{
		const bool bWasAiming = bIsAiming;
		StartAim();
		if (!bWasAiming && bIsAiming && InventoryComponent && InventoryComponent->HasBow())
		{
			BeginAimSnap();
		}
	}
	else if (!IsDrawingBow())
	{
		StopAim();
	}
}

void AHawkeyeCharacter::ReleaseAim()
{
	bAimInputHeld = UHawkeyeAccessibility::ResolveRelease(bToggleAim, bAimInputHeld);
	// A bow still drawn keeps the aim until it is released.
	if (!bAimInputHeld && !IsDrawingBow())
	{
		StopAim();
	}
}

void AHawkeyeCharacter::Input_AimCompleted(const FInputActionValue& /*Value*/)
{
	ReleaseAim();
}

bool AHawkeyeCharacter::IsDrawingBow() const
{
	return BowComponent && BowComponent->IsDrawing();
}

void AHawkeyeCharacter::NotifyBowDrawStarted()
{
	StopSprintToggle(EHawkeyeSprintStop::Aim);
	if (bIsSprinting)
	{
		bIsSprinting = false;
	}
	StartAim();
	UpdateMaxWalkSpeed();
}

void AHawkeyeCharacter::NotifyBowDrawEnded()
{
	if (!bAimInputHeld && !bStoppingAim)
	{
		StopAim();
	}
	UpdateMaxWalkSpeed();
}

void AHawkeyeCharacter::StartAim()
{
	if (bIsAiming || bIsSprinting || IsLockedOutByTakedown())
	{
		return;
	}

	bIsAiming = true;
	UpdateMaxWalkSpeed();
	ApplyRotationMode();

	if (UWeaponComponent* Weapon = GetWeaponComponent())
	{
		Weapon->SetAiming(true);
	}
}

void AHawkeyeCharacter::StopAim()
{
	// Losing the aim (a sprint, a zip) lets the string down; nothing is fired.
	if (IsDrawingBow())
	{
		TGuardValue<bool> Guard(bStoppingAim, true);
		BowComponent->CancelDraw();
	}

	if (!bIsAiming)
	{
		// Still clear the weapon: a pickup mid-aim could otherwise leave the flags disagreeing.
		if (UWeaponComponent* Weapon = GetWeaponComponent())
		{
			Weapon->SetAiming(false);
		}
		return;
	}

	bIsAiming = false;
	UpdateMaxWalkSpeed();
	ApplyRotationMode();

	if (UWeaponComponent* Weapon = GetWeaponComponent())
	{
		Weapon->SetAiming(false);
	}
}

void AHawkeyeCharacter::ApplyTuningToComponents()
{
	if (CameraBoom)
	{
		CameraBoom->CameraLagSpeed = CameraLagSpeed;
		CameraBoom->CameraRotationLagSpeed = CameraRotationLagSpeed;
		CameraBoom->ProbeSize = CameraProbeSize;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	Movement->RotationRate = FRotator(0.f, TurnRateDegrees, 0.f);
	Movement->AirControl = JumpAirControl;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->SetCrouchedHalfHeight(CrouchedCapsuleHalfHeight);

	// Apex h = v^2 / 2g, so the designer tunes the height and the velocity follows gravity.
	const float Gravity = FMath::Abs(Movement->GetGravityZ());
	if (Gravity > KINDA_SMALL_NUMBER && JumpHeight > 0.f)
	{
		Movement->JumpZVelocity = FMath::Sqrt(2.f * Gravity * JumpHeight);
	}
	ApplyRotationMode();
}

void AHawkeyeCharacter::ApplyRotationMode()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = !bIsAiming;
		Movement->bUseControllerDesiredRotation = bIsAiming;
	}
}

EHawkeyeGait AHawkeyeCharacter::SelectGait(float InputMagnitude, float InputHeldSeconds, bool bWantsSprint,
	bool bCrouched, bool bSliding, bool bAiming) const
{
	if (bSliding)
	{
		return EHawkeyeGait::Slide;
	}
	if (bCrouched)
	{
		return EHawkeyeGait::Crouch;
	}
	if (bWantsSprint)
	{
		return EHawkeyeGait::Sprint;
	}
	if (bAiming)
	{
		return EHawkeyeGait::Walk;
	}
	if (InputMagnitude >= FullInputThreshold)
	{
		return EHawkeyeGait::Run;
	}
	if (InputMagnitude >= LightInputThreshold && InputHeldSeconds > RunAfterHeldSeconds)
	{
		return EHawkeyeGait::Run;
	}
	return EHawkeyeGait::Walk;
}

float AHawkeyeCharacter::GetGaitSpeed(EHawkeyeGait Gait) const
{
	switch (Gait)
	{
	case EHawkeyeGait::Run:
		return RunSpeed;
	case EHawkeyeGait::Sprint:
		return SprintSpeed;
	case EHawkeyeGait::Crouch:
		return CrouchSpeed;
	case EHawkeyeGait::Slide:
		return SlideSpeed;
	case EHawkeyeGait::Walk:
	default:
		return WalkSpeed;
	}
}

void AHawkeyeCharacter::UpdateMaxWalkSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// An AI controller (the partner) has no stick: it names the gait it wants instead.
	const bool bAIDriven = Controller && !Controller->IsPlayerController();
	CurrentGait = bAIDriven && !bIsCrouched && !bIsSliding && !bIsAiming
		? AIGait
		: SelectGait(MoveInputMagnitude, MoveInputHeldSeconds, bIsSprinting, bIsCrouched, bIsSliding, bIsAiming);
	float Speed = GetGaitSpeed(CurrentGait);
	if (CurrentGait == EHawkeyeGait::Slide && SlideSeconds > 0.f)
	{
		const float Progress = 1.f - FMath::Clamp(SlideRemaining / SlideSeconds, 0.f, 1.f);
		Speed = FMath::Lerp(SlideSpeed, CrouchSpeed, Progress);
	}
	if (IsStumbling())
	{
		Speed = FMath::Min(Speed, RunSpeed);
	}
	Speed *= GetLandingSpeedFactor();
	if (IsDrawingBow())
	{
		Speed *= DrawWalkSpeedMultiplier;
	}

	SyncGaspInputState();
	if (UsesGaspLocomotion())
	{
		// TODO(stage2): reconcile. The sandbox graph writes MaxWalkSpeed, crouch speed and
		// acceleration from its own Walk/Run/Sprint/Crouch speed curves before the movement
		// component ticks; writing ours as well would make the two fight frame to frame.
		if (!bLoggedGaspSpeedOwnership)
		{
			bLoggedGaspSpeedOwnership = true;
			UE_LOG(LogHawkeye, Log, TEXT("%s: the Game Animation Sample graph sets movement speeds; Hawkeye gait speeds are not applied."),
				*GetNameSafe(this));
		}
		return;
	}

	// Crouch and slide move on the crouched speed; everything else on the walking one.
	const bool bLow = CurrentGait == EHawkeyeGait::Crouch || CurrentGait == EHawkeyeGait::Slide;
	Movement->MaxWalkSpeed = bLow ? RunSpeed : Speed;
	Movement->MaxWalkSpeedCrouched = bLow ? Speed : CrouchSpeed;
}

FHawkeyeCameraTargets AHawkeyeCharacter::ComputeCameraTargets(bool bAiming, float Pitch) const
{
	if (bAiming)
	{
		return AimCamera;
	}
	FHawkeyeCameraTargets Targets = HipCamera;
	const float Span = LookUpPitchFull - LookUpPitchStart;
	const float Alpha = Span > 0.f
		? FMath::Clamp((Pitch - LookUpPitchStart) / Span, 0.f, 1.f)
		: (Pitch >= LookUpPitchFull ? 1.f : 0.f);
	Targets.ArmLength = FMath::Lerp(HipCamera.ArmLength, LookUpArmLength, Alpha);
	Targets.SocketOffset.Z = FMath::Lerp(HipCamera.SocketOffset.Z, LookUpSocketZ, Alpha);
	Targets.PivotLift = FMath::Lerp(HipCamera.PivotLift, LookUpPivotLift, Alpha);
	if (bIndoorCamera)
	{
		Targets.ArmLength = FMath::Min(Targets.ArmLength, IndoorArmLength);
	}
	return Targets;
}

float AHawkeyeCharacter::GetCurrentFOV() const
{
	return FollowCamera ? FollowCamera->FieldOfView : HipCamera.FieldOfView;
}

void AHawkeyeCharacter::UpdateCamera(float DeltaSeconds)
{
	// Constant rate, so the blend really takes AimBlendSeconds; the smoothstep only shapes it.
	const float Target = bIsAiming ? 1.f : 0.f;
	AimAlpha = AimBlendSeconds > 0.f
		? FMath::FInterpConstantTo(AimAlpha, Target, DeltaSeconds, 1.f / AimBlendSeconds)
		: Target;

	const float Pitch = Controller ? FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch) : 0.f;
	FHawkeyeCameraTargets Blend = FHawkeyeCameraTargets::Lerp(
		ComputeCameraTargets(false, Pitch), ComputeCameraTargets(true, Pitch), FMath::SmoothStep(0.f, 1.f, AimAlpha));

	// A controlled drop's dip: the camera sinks and comes back up over the recovery.
	if (LandingState == EHawkeyeLanding::Dip && LandingRecoverSeconds > 0.f)
	{
		const float Phase = FMath::Clamp(LandingElapsed / LandingRecoverSeconds, 0.f, 1.f);
		Blend.SocketOffset.Z -= LandingCameraDip * FMath::Sin(PI * Phase);
	}

	// A hit on her throws the lens about for a moment; decaying, two unrelated frequencies.
	const float Shake = GetHitShakeAlpha();
	if (Shake > 0.f)
	{
		const float Time = (ActiveShakeSeconds - HitShakeRemaining) * 60.f;
		Blend.SocketOffset.Y += ActiveShakeAmplitude * Shake * FMath::Sin(Time * 1.7f);
		Blend.SocketOffset.Z += ActiveShakeAmplitude * Shake * FMath::Cos(Time * 2.3f);
	}

	// A finisher pushes the lens in on the blow.
	const float PushAlpha = FinisherComponent ? FinisherComponent->GetCameraPushAlpha() : 0.f;
	if (FinisherComponent)
	{
		Blend.ArmLength -= FinisherComponent->CameraPushDistance * PushAlpha;
	}

	// Two or more alerted thugs round her: the boom comes back and the lens tips down so they are all in
	// frame; it gives way to the aim and to the finisher's push, and indoors never passes the room's cap.
	UpdateFightCamera(DeltaSeconds);
	float FightArm = 0.f;
	float FightPitch = 0.f;
	HawkeyeCombatReadability::ComputeFightOffsets(FightCameraAlpha, FMath::SmoothStep(0.f, 1.f, AimAlpha), PushAlpha, FightCamera,
		FightArm, FightPitch);
	if (bIndoorCamera)
	{
		FightArm = FMath::Min(FightArm, FMath::Max(IndoorArmLength - Blend.ArmLength, 0.f));
	}
	Blend.ArmLength += FightArm;

	// A landed heavy punches the lens in a couple of centimetres, on real time so the hit stop does not stretch it.
	if (CameraPunchElapsed >= 0.f)
	{
		CameraPunchElapsed += FApp::GetDeltaTime();
		if (CameraPunchElapsed > HeavyCameraPunchSeconds)
		{
			CameraPunchElapsed = -1.f;
		}
	}
	Blend.ArmLength -= GetCameraPunchOffset();

	// Hanging, the lens goes over the shoulder on the open side of the wall.
	if (ParkourComponent)
	{
		ParkourComponent->ApplyHangCamera(Blend, DeltaSeconds);
	}

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = Blend.ArmLength;
		CameraBoom->SocketOffset = Blend.SocketOffset;
		CameraBoom->TargetOffset = FVector(0.f, 0.f, Blend.PivotLift);
	}
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(Blend.FieldOfView);
		// A roll tips the lens down and back up with her; the fight camera tips it down a little more.
		const float RollPitch = GetLandingCameraPitch() - FightPitch;
		if (!FMath::IsNearlyEqual(FollowCamera->GetRelativeRotation().Pitch, RollPitch))
		{
			FollowCamera->SetRelativeRotation(FRotator(RollPitch, 0.f, 0.f));
		}
	}
}

void AHawkeyeCharacter::UpdateFightCamera(float DeltaSeconds)
{
	FightRecountRemaining -= FMath::Max(DeltaSeconds, 0.f);
	UWorld* World = GetWorld();
	if (FightRecountRemaining <= 0.f && World)
	{
		FightRecountRemaining = FightRecountSeconds;
		TArray<FVector> Alerted;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const UHealthComponent* Health = It->GetHealthComponent();
			if (It->IsAlerted() && !It->IsLimp() && Health && Health->IsAlive())
			{
				Alerted.Add(It->GetActorLocation());
			}
		}
		const int32 Count = HawkeyeCombatReadability::CountEngaged(GetActorLocation(), Alerted, FightCamera.Radius, FightCamera.MaxHeight, FightCamera.FarRadius);
		const bool bWasFight = HawkeyeCombatReadability::IsFight(EngagedThugCount, FightCamera);
		const bool bFight = HawkeyeCombatReadability::IsFight(Count, FightCamera);
		if (bFight != bWasFight)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: fight camera %s (%d alerted thugs, one within %.0f cm)."), *GetNameSafe(this),
				bFight ? TEXT("in") : TEXT("out"), Count, FightCamera.Radius);
		}
		EngagedThugCount = Count;
	}
	FightCameraAlpha = HawkeyeCombatReadability::AdvanceFightAlpha(FightCameraAlpha,
		HawkeyeCombatReadability::IsFight(EngagedThugCount, FightCamera), DeltaSeconds, FightCamera);
}

float AHawkeyeCharacter::GetCameraPunchOffset() const
{
	return CameraPunchElapsed < 0.f ? 0.f
		: HawkeyeCombatReadability::ComputeCameraPunch(CameraPunchElapsed, HeavyCameraPunchSeconds, HeavyCameraPunchDistance,
			CameraShakeScale);
}

void AHawkeyeCharacter::SpawnMeleeSpark(const AActor* HitActor, bool bHeavy) const
{
	if (!HitActor)
	{
		return;
	}
	float Radius = 0.f;
	float HalfHeight = 0.f;
	HitActor->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector ToHer = (GetActorLocation() - HitActor->GetActorLocation()).GetSafeNormal2D();
	// The front of his body toward her, at the height of whichever fist or foot is nearest him (a jab at his
	// face, a roundhouse at his ribs; 2026-09-29: always chest height hid it behind her head from her camera).
	float Height = 35.f;
	if (const USkeletalMeshComponent* Body = GetMesh())
	{
		float Nearest = BIG_NUMBER;
		for (const TCHAR* Bone : { TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l") })
		{
			if (Body->GetBoneIndex(Bone) == INDEX_NONE)
			{
				continue;
			}
			const FVector At = Body->GetBoneLocation(Bone);
			const float Off = FVector::Dist2D(At, HitActor->GetActorLocation());
			if (Off < Nearest)
			{
				Nearest = Off;
				Height = FMath::Clamp(At.Z - HitActor->GetActorLocation().Z, -HalfHeight * 0.3f, HalfHeight * 0.75f);
			}
		}
	}
	// Round his body from the point facing her toward whichever side the player's lens sees past her: from
	// behind her (her own camera) that point is behind her head and shoulders (2026-09-29, hit_spark.png).
	FVector Out = ToHer;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->PlayerCameraManager)
	{
		const FVector Lens = PC->PlayerCameraManager->GetCameraLocation();
		const FVector Front = HitActor->GetActorLocation() + ToHer * Radius;
		const FVector Across = FVector::CrossProduct(FVector::UpVector, (Front - Lens).GetSafeNormal2D());
		// Her axis off the lens's line to that point, flat, cm; inside SparkClearance she hides it.
		const float Off = FVector::DotProduct(GetActorLocation() - Front, Across);
		if (FMath::Abs(Off) < SparkClearance)
		{
			// A point on his surface Slide cm across, at most 0.9 of his radius (about 65 degrees round).
			const float Slide = FMath::Min(SparkClearance - FMath::Abs(Off), 0.9f * Radius);
			Out = ToHer * FMath::Sqrt(FMath::Max(Radius * Radius - Slide * Slide, 0.f)) / FMath::Max(Radius, 1.f)
				+ Across * (Off >= 0.f ? -Slide : Slide) / FMath::Max(Radius, 1.f);
		}
	}
	const FVector Contact = HitActor->GetActorLocation() + Out * Radius + FVector(0.f, 0.f, Height);
	UHawkeyeVfxSubsystem::SpawnAt(this, MeleeSparkVfx, Contact, ToHer.Rotation(), UHawkeyeVfxSubsystem::MeleeSparkEvent,
		(bHeavy ? HeavySparkScale : LightSparkScale) * FMath::Lerp(0.6f, 1.f, FlashScale));
}

void AHawkeyeCharacter::UpdateMoveInputTiming(float DeltaSeconds)
{
	if (bMoveInputThisFrame)
	{
		MoveInputHeldSeconds += DeltaSeconds;
	}
	else
	{
		MoveInputHeldSeconds = 0.f;
		MoveInputMagnitude = 0.f;
	}
	bMoveInputThisFrame = false;
}

void AHawkeyeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateMoveInputTiming(DeltaSeconds);
	UpdateSprintToggle(DeltaSeconds);
	UpdateQuiverWheelHold();
	UpdateCrouchTap(DeltaSeconds);
	UpdateMeleeHold(DeltaSeconds);
	AdvanceMeleeFlow(DeltaSeconds);
	MeleeTargetMarker.Advance(DeltaSeconds, MeleeComponent && MeleeComponent->IsAttacking());
	UpdateDodge(DeltaSeconds);
	UpdateHitReactions(DeltaSeconds);
	AdvanceDowned(DeltaSeconds);
	if (HealthComponent)
	{
		HealthComponent->AdvanceRegen(DeltaSeconds);
	}
	UpdateLowHealthPostProcess();
	UpdateSlide(DeltaSeconds);
	UpdateFalling(DeltaSeconds);
	UpdateLanding(DeltaSeconds);
	UpdateFootsteps();
	UpdateMovementDebugSample(DeltaSeconds);
	UpdateMaxWalkSpeed();
	UpdateAimAssist(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	UpdateBodyVisibilityForCamera();
	UpdateBodyLocomotion();
	SilenceForeignCameras();
	ApplyCameraPitchLimits();
}

void AHawkeyeCharacter::UpdateBodyVisibilityForCamera()
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (!Body || !CameraBoom || !FollowCamera)
	{
		return;
	}

	// The arm places the lens after this tick, so this reads last frame's; a frame late is fine.
	const float LensDistance = FVector::Dist(FollowCamera->GetComponentLocation(), CameraBoom->GetComponentLocation());
	const bool bTooClose = LensDistance < CameraHideBodyDistance;
	if (Body->bOwnerNoSee != bTooClose)
	{
		Body->SetOwnerNoSee(bTooClose);
	}
}

void AHawkeyeCharacter::UpdateBodyLocomotion()
{
	// An AnimBP (BP_Kate's motion matching) owns the body; single-node playback would replace it.
	if (IsBodyDrivenByAnimBlueprint())
	{
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();

	UAnimSequence* Wanted = IdleAnim;
	// Off the ground: falling, the sample's traversal (Flying), a zip or one of our moves and hangs (custom).
	if (Movement && (Movement->IsFalling() || Movement->IsFlying() || Movement->MovementMode == MOVE_Custom) && FallAnim)
	{
		Wanted = FallAnim;
	}
	else if (Speed > RunAnimSpeedThreshold && RunAnim)
	{
		Wanted = RunAnim;
	}
	else if (Speed > WalkAnimSpeedThreshold)
	{
		Wanted = WalkAnim;
	}
	HawkeyeLocomotion::PlayIfChanged(GetMesh(), Wanted, CurrentLocomotionAnim);
}

void AHawkeyeCharacter::Input_CrouchToggle(const FInputActionValue& /*Value*/)
{
	PressCrouch();
}

void AHawkeyeCharacter::PressCrouch()
{
	// A crouch press ends a toggled sprint, unless it becomes (or lands in) a slide, which ends it as it stands up.
	bool bKeepSprint = bIsSliding;
	ON_SCOPE_EXIT
	{
		if (!bKeepSprint)
		{
			StopSprintToggle(EHawkeyeSprintStop::Crouch);
		}
	};
	// The roll owns the crouch until it stands her up.
	if (IsRolling())
	{
		return;
	}
	// Crouch lets go of a ledge.
	if (ParkourComponent && ParkourComponent->IsHanging())
	{
		ParkourComponent->DropFromHang();
		return;
	}
	// Mid-zip it lets go of the line.
	if (IsZipping())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: crouch cancels the zip"), *GetNameSafe(this));
		GrappleComponent->ReleaseZip(/*bJump=*/false);
		return;
	}
	// Moving toward a roof or landing edge, it goes over to the hang.
	if (ParkourComponent && !bIsSliding && !IsTraversing() && MoveInputMagnitude > 0.f
		&& ParkourComponent->TryDropToHang(LastMoveWorldDirection, TEXT("crouch toward the edge")))
	{
		bCrouchTapPending = false;
		return;
	}
	// The slide stands itself up; a second press mid-slide is not a crouch. Nor is one mid-zip.
	if (bIsSliding || IsZipping() || IsTraversing())
	{
		return;
	}

	if (IsCrouchWanted())
	{
		// Held crouch stands on the release, not on a second press.
		if (bToggleCrouch)
		{
			EndCrouch(TEXT("crouch input (toggle press)"));
		}
	}
	else if (bIsSprinting)
	{
		bKeepSprint = StartSlide();
		if (!bKeepSprint)
		{
			Crouch();
		}
	}
	else if (MoveInputMagnitude > 0.f)
	{
		// Moving, not sprinting: a tap dodges and a hold crouches, so wait for the release.
		bCrouchTapPending = true;
		CrouchTapHeldSeconds = 0.f;
		return;
	}
	else
	{
		Crouch();
	}
	UpdateMaxWalkSpeed();
}

void AHawkeyeCharacter::Input_CrouchReleased(const FInputActionValue& /*Value*/)
{
	ReleaseCrouch();
}

void AHawkeyeCharacter::ReleaseCrouch()
{
	if (bCrouchTapPending)
	{
		bCrouchTapPending = false;
		TryDodge(LastMoveWorldDirection);
		return;
	}
	const bool bWanted = IsCrouchWanted();
	if (bWanted && !UHawkeyeAccessibility::ResolveRelease(bToggleCrouch, bWanted) && !bIsSliding && !IsRolling())
	{
		EndCrouch(TEXT("crouch input (hold released)"));
		UpdateMaxWalkSpeed();
	}
}

bool AHawkeyeCharacter::IsCrouchWanted() const
{
	// The movement component takes a crouch on its next update; until then only the wish is set.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return bIsCrouched || (Movement && Movement->bWantsToCrouch);
}

void AHawkeyeCharacter::EndCrouch(const TCHAR* Why)
{
	if (IsCrouchWanted())
	{
		PendingCrouchEndReason = Why;
		bPendingCrouchEndUnasked = false;
	}
	Super::UnCrouch();
}

void AHawkeyeCharacter::UnCrouch(bool bClientSimulation)
{
	// Our own stand-ups come through EndCrouch (Super::UnCrouch); anything landing here is a Blueprint (the
	// sample's graph) or engine code, named by its script stack so the log says who stood her up.
	if (IsCrouchWanted())
	{
		const FString Script = FFrame::GetScriptCallstack(/*bReturnEmpty=*/true, /*bTopOfStackOnly=*/true).TrimStartAndEnd();
		PendingCrouchEndReason = Script.IsEmpty() ? FString(TEXT("UnCrouch from C++ outside EndCrouch"))
			: FString::Printf(TEXT("UnCrouch from Blueprint %s"), *Script);
		bPendingCrouchEndUnasked = true;
	}
	Super::UnCrouch(bClientSimulation);
}

void AHawkeyeCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	PendingCrouchEndReason.Reset();
	bPendingCrouchEndUnasked = false;
}

void AHawkeyeCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const int32 Mode = Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1;
	const bool bStillWanted = Movement && Movement->bWantsToCrouch;
	bool bUnasked = bPendingCrouchEndUnasked;
	FString Why = PendingCrouchEndReason;
	if (Why.IsEmpty())
	{
		// The movement component stood her up on its own: a mode it cannot crouch in (it crouches her again
		// when she is back on the ground, the wish is kept), or somebody cleared the wish behind our back.
		Why = bStillWanted ? FString::Printf(TEXT("movement mode %d cannot crouch (the wish is kept)"), Mode)
			: FString(TEXT("the movement component's crouch wish was cleared outside EndCrouch"));
		bUnasked = !bStillWanted;
	}
	PendingCrouchEndReason.Reset();
	bPendingCrouchEndUnasked = false;
	LastCrouchEndReason = Why;
	++CrouchEndCount;
	if (bUnasked)
	{
		++UnaskedCrouchEndCount;
		UE_LOG(LogHawkeye, Warning, TEXT("%s: crouch ended, unasked: %s (mode %d, feet %s)"), *GetNameSafe(this), *Why, Mode,
			*(GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight())).ToCompactString());
	}
	else
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: crouch ended: %s (mode %d)"), *GetNameSafe(this), *Why, Mode);
	}
}

void AHawkeyeCharacter::UpdateCrouchTap(float DeltaSeconds)
{
	if (!bCrouchTapPending)
	{
		return;
	}
	CrouchTapHeldSeconds += DeltaSeconds;
	if (CrouchTapHeldSeconds < DodgeTapSeconds)
	{
		return;
	}
	bCrouchTapPending = false;
	if (!bIsCrouched && !bIsSliding && !IsZipping() && !IsTraversing())
	{
		Crouch();
		UpdateMaxWalkSpeed();
	}
}

// --- Melee ------------------------------------------------------------------------------------

bool AHawkeyeCharacter::IsMeleeAttacking() const
{
	return MeleeComponent && MeleeComponent->IsAttacking();
}

void AHawkeyeCharacter::Input_MeleePressed(const FInputActionValue& /*Value*/)
{
	bMeleeHeld = true;
	MeleeHeldSeconds = 0.f;
}

void AHawkeyeCharacter::Input_MeleeReleased(const FInputActionValue& /*Value*/)
{
	if (!bMeleeHeld)
	{
		return;
	}
	bMeleeHeld = false;
	StartLightAttack();
}

void AHawkeyeCharacter::UpdateMeleeHold(float DeltaSeconds)
{
	if (!bMeleeHeld)
	{
		return;
	}
	MeleeHeldSeconds += DeltaSeconds;
	if (MeleeHeldSeconds >= HeavyHoldSeconds)
	{
		bMeleeHeld = false;
		StartHeavyAttack();
	}
}

bool AHawkeyeCharacter::StartLightAttack()
{
	if (TryParry())
	{
		ParryBufferRemaining = 0.f;
		return true;
	}
	// Story: a tap a little before a telegraph starts still counts as the parry, when it starts.
	ParryBufferRemaining = FMath::Max(ParryWindowDelta, 0.f);
	if (MeleeComponent && !MeleeComponent->CanStartAttack())
	{
		// Inside the chain window, a light pressed during the last one's recovery goes when it ends
		// (with a clip: when its combo window opens).
		// A press while the hit window is still open waits for the hit or the miss to open the chain.
		if (MeleeComponent->GetPhase() == EMeleePhase::Recover && bSwingIsLight
			&& (Combo.IsChainOpen() || MeleeComponent->IsHitWindowOpen()))
		{
			bLightBuffered = true;
			return true;
		}
		return false;
	}
	return StartComboLight();
}

bool AHawkeyeCharacter::StartComboLight()
{
	const int32 Step = Combo.GetNextStep();
	FHawkeyeMeleeAttack Attack = GetComboAttack(Step);
	Attack.Damage *= Combo.GetDamageMultiplier();
	if (!StartMelee(Attack, EHawkeyeStrikePose::Light))
	{
		return false;
	}
	bSwingIsLight = true;
	SwingStep = Step;
	return true;
}

bool AHawkeyeCharacter::StartHeavyAttack()
{
	FHawkeyeMeleeAttack Attack = HeavyAttack;
	Attack.Damage *= Combo.GetDamageMultiplier();
	bLightBuffered = false;
	if (!StartMelee(Attack, EHawkeyeStrikePose::Heavy))
	{
		return false;
	}
	bSwingIsLight = false;
	Combo.EndChain();
	return true;
}

FHawkeyeMeleeAttack AHawkeyeCharacter::GetComboAttack(int32 Step) const
{
	switch (Step)
	{
	case 1:
		return ComboFollowAttack;
	case 2:
		return ComboFinishAttack;
	default:
		return LightAttack;
	}
}

bool AHawkeyeCharacter::StartMelee(const FHawkeyeMeleeAttack& Attack, EHawkeyeStrikePose Pose)
{
	if (!MeleeComponent || !MeleeComponent->CanStartAttack() || IsLockedOutByTakedown() || IsZipping()
		|| IsTraversing() || IsDodging() || IsStaggered() || IsDrawingBow() || bDowned)
	{
		return false;
	}

	// Face the thug she means to hit (the assist's pick, down the stick or the camera); with nobody there,
	// the way she meant. The blow goes that way at once; the body turns over SoftTurnSeconds and keeps on
	// him through the wind-up, and the swing closes the gap (a root-motion clip warps to him).
	const FVector Meant = GetMeleeAssistDirection();
	FVector Facing = Meant;
	AActor* Target = FindMeleeAssistTarget();
	if (Target)
	{
		Facing = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s assist picks %s at %.0f cm, %.0f deg off where she meant."), *GetNameSafe(this),
			*Attack.Name.ToString(), *GetNameSafe(Target), FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Facing, Meant.GetSafeNormal2D()), -1.f, 1.f))));
	}
	else
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s assist finds nobody within %.0f cm and %.0f deg."), *GetNameSafe(this),
			*Attack.Name.ToString(), MeleeAssistRange, MeleeAssistAngleDegrees);
	}
	if (!Facing.IsNearlyZero())
	{
		BeginSoftTurn(Facing);
	}

	EndSlide();
	MeleeComponent->SetNextAttackDirection(Facing);
	MeleeComponent->SetNextAttackTarget(Target);
	if (!MeleeComponent->StartAttack(Attack))
	{
		return false;
	}
	// The pick gets a thin ring at his feet for the swing (nobody: whatever was marked goes).
	MeleeTargetMarker.Mark(Target);
	// The clip is the arms; the strike pose is what stands in for one.
	if (MeleeComponent->GetCurrentMontage())
	{
		StrikePose.Stop();
	}
	else
	{
		StrikePose.Start(Pose, Attack.WindupSeconds);
	}
	UpdateArmPoses();
	return true;
}

FVector AHawkeyeCharacter::GetViewForward() const
{
	return Controller ? FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f).Vector() : GetActorForwardVector().GetSafeNormal2D();
}

AActor* AHawkeyeCharacter::FindSoftLockTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	TArray<AThugCharacter*> Thugs;
	TArray<FVector> Where;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (Health && Health->IsAlive() && !It->IsLimp())
		{
			Thugs.Add(*It);
			Where.Add(It->GetActorLocation());
		}
	}
	const int32 Pick = UHawkeyeMeleeRules::SelectSoftLockIndex(GetActorLocation(), GetViewForward(), Where, SoftLockRange,
		SoftLockAngleDegrees, 200.f);
	return Thugs.IsValidIndex(Pick) ? Thugs[Pick] : nullptr;
}

FVector AHawkeyeCharacter::GetMeleeAssistDirection() const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (LastMoveInputSeconds >= 0.0 && Now - LastMoveInputSeconds <= MeleeAssistInputSeconds && !LastMoveWorldDirection.IsNearlyZero())
	{
		return LastMoveWorldDirection;
	}
	return GetViewForward();
}

void AHawkeyeCharacter::GetAimAssistView(FVector& OutLocation, FRotator& OutRotation) const
{
	// The reticle looks along the control rotation, from the camera once it has been placed (a world that
	// has not ticked yet has no camera, so from her eyes).
	GetActorEyesViewPoint(OutLocation, OutRotation);
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC && PC->PlayerCameraManager && PC->PlayerCameraManager->GetCameraCacheTime() > 0.f)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
	}
	if (Controller)
	{
		OutRotation = Controller->GetControlRotation();
	}
}

bool AHawkeyeCharacter::BeginAimSnap()
{
	const FHawkeyeAimAssistTuning Tuning = UHawkeyeAimAssist::GetTuningFor(this);
	if (Tuning.SnapConeDegrees <= 0.f || !Controller)
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetAimAssistView(ViewLocation, ViewRotation);
	TArray<FHawkeyeAimAssistCandidate> Candidates;
	UHawkeyeAimAssist::GatherCandidates(GetWorld(), ViewLocation, UHawkeyeAimAssist::MaxRange, this, Candidates);
	const int32 Pick = UHawkeyeAimAssist::FindBestIndex(ViewLocation, ViewRotation.Vector(), Candidates, Tuning.SnapConeDegrees,
		UHawkeyeAimAssist::MaxRange);
	if (!Candidates.IsValidIndex(Pick))
	{
		return false;
	}
	AimAssistTarget = Candidates[Pick].Actor;
	AimSnapFrom = Controller->GetControlRotation();
	AimSnapTo = UHawkeyeAimAssist::ComputeSnapRotation(ViewLocation, Candidates[Pick].Chest);
	AimSnapRemaining = UHawkeyeAimAssist::SnapSeconds;
	UE_LOG(LogHawkeye, Log, TEXT("%s: aim snaps %.1f deg onto %s at %.0f cm."), *GetNameSafe(this),
		UHawkeyeAimAssist::AngleToPoint(ViewLocation, ViewRotation.Vector(), Candidates[Pick].Chest),
		*GetNameSafe(Candidates[Pick].Actor.Get()), FVector::Dist(ViewLocation, Candidates[Pick].Chest));
	return true;
}

void AHawkeyeCharacter::UpdateAimAssist(float DeltaSeconds)
{
	const bool bBowUp = bIsAiming && InventoryComponent && InventoryComponent->HasBow();
	if (!bBowUp || !Controller)
	{
		AimSnapRemaining = 0.f;
		return;
	}
	if (AimSnapRemaining > 0.f)
	{
		// The snap: from where the view was to his chest, eased, done on time.
		AimSnapRemaining = FMath::Max(AimSnapRemaining - FMath::Max(DeltaSeconds, 0.f), 0.f);
		const float Alpha = FMath::InterpEaseOut(0.f, 1.f, 1.f - AimSnapRemaining / UHawkeyeAimAssist::SnapSeconds, 2.f);
		FRotator View = FQuat::Slerp(AimSnapFrom.Quaternion(), AimSnapTo.Quaternion(), Alpha).Rotator();
		View.Roll = 0.f;
		Controller->SetControlRotation(View);
		return;
	}

	const FHawkeyeAimAssistTuning Tuning = UHawkeyeAimAssist::GetTuningFor(this);
	const float Pull = bLookFromStick ? Tuning.PadPullDegreesPerSecond : Tuning.MousePullDegreesPerSecond;
	if (Tuning.MagnetismConeDegrees <= 0.f || Pull <= 0.f)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetAimAssistView(ViewLocation, ViewRotation);
	TArray<FHawkeyeAimAssistCandidate> Candidates;
	UHawkeyeAimAssist::GatherCandidates(GetWorld(), ViewLocation, UHawkeyeAimAssist::MaxRange, this, Candidates);
	const int32 Pick = UHawkeyeAimAssist::FindBestIndex(ViewLocation, ViewRotation.Vector(), Candidates,
		Tuning.MagnetismConeDegrees, UHawkeyeAimAssist::MaxRange);
	if (!Candidates.IsValidIndex(Pick))
	{
		AimAssistTarget = nullptr;
		return;
	}
	AimAssistTarget = Candidates[Pick].Actor;
	// Already over his body (a head included): no pull, so a headshot is hers to line up.
	if (UHawkeyeAimAssist::IsLineOnBody(ViewLocation, ViewRotation.Vector(), Candidates[Pick]))
	{
		return;
	}
	const FRotator Control = Controller->GetControlRotation();
	const FRotator Pulled = UHawkeyeAimAssist::ComputePull(ViewRotation, ViewLocation, Candidates[Pick].Chest,
		Tuning.MagnetismConeDegrees, Pull, DeltaSeconds);
	// The camera may sit off the control rotation's origin; move the control rotation by the same turn.
	const FRotator Delta = (Pulled - ViewRotation).GetNormalized();
	Controller->SetControlRotation(FRotator(Control.Pitch + Delta.Pitch, Control.Yaw + Delta.Yaw, 0.f));
}

FVector2D AHawkeyeCharacter::ApplyAimSlowdown(FVector2D LookDelta, bool bFromStick) const
{
	const bool bBowUp = bIsAiming && InventoryComponent && InventoryComponent->HasBow();
	if (!bBowUp || LookDelta.IsNearlyZero())
	{
		return LookDelta;
	}
	const FHawkeyeAimAssistTuning Tuning = UHawkeyeAimAssist::GetTuningFor(this);
	if (Tuning.MagnetismConeDegrees <= 0.f)
	{
		return LookDelta;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetAimAssistView(ViewLocation, ViewRotation);
	TArray<FHawkeyeAimAssistCandidate> Candidates;
	UHawkeyeAimAssist::GatherCandidates(GetWorld(), ViewLocation, UHawkeyeAimAssist::MaxRange, this, Candidates);
	const int32 Pick = UHawkeyeAimAssist::FindBestIndex(ViewLocation, ViewRotation.Vector(), Candidates,
		Tuning.MagnetismConeDegrees, UHawkeyeAimAssist::MaxRange);
	if (!Candidates.IsValidIndex(Pick))
	{
		return LookDelta;
	}
	const float Angle = UHawkeyeAimAssist::AngleToPoint(ViewLocation, ViewRotation.Vector(), Candidates[Pick].Chest);
	return LookDelta * UHawkeyeAimAssist::ComputeLookScale(Angle, Tuning.MagnetismConeDegrees,
		bFromStick ? Tuning.PadSlowdown : Tuning.MouseSlowdown);
}

void AHawkeyeCharacter::SetMoveInputDirectionForTest(const FVector& WorldDirection)
{
	LastMoveWorldDirection = WorldDirection.GetSafeNormal2D();
	LastMoveInputSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

AActor* AHawkeyeCharacter::FindMeleeAssistTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	TArray<AThugCharacter*> Thugs;
	TArray<FVector> Where;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (Health && Health->IsAlive() && !It->IsLimp())
		{
			Thugs.Add(*It);
			Where.Add(It->GetActorLocation());
		}
	}
	const int32 Pick = UHawkeyeMeleeRules::SelectMeleeAssistIndex(GetActorLocation(), GetMeleeAssistDirection(), Where,
		MeleeAssistRange, MeleeAssistAngleDegrees, 200.f);
	return Thugs.IsValidIndex(Pick) ? Thugs[Pick] : nullptr;
}

void AHawkeyeCharacter::BeginSoftTurn(const FVector& Direction)
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		return;
	}
	SoftTurnYaw = Flat.Rotation().Yaw;
	SoftTurnRemaining = SoftTurnSeconds;
	if (SoftTurnSeconds <= 0.f)
	{
		SetActorRotation(FRotator(0.f, SoftTurnYaw, 0.f));
	}
}

void AHawkeyeCharacter::UpdateSoftTurn(float DeltaSeconds)
{
	if (SoftTurnRemaining > 0.f)
	{
		// The remaining turn split evenly over the remaining time: a constant rate, done on time.
		const float Step = FMath::Min(FMath::Max(DeltaSeconds, 0.f), SoftTurnRemaining);
		const float Current = GetActorRotation().Yaw;
		const float Fraction = SoftTurnRemaining > 0.f ? Step / SoftTurnRemaining : 1.f;
		SetActorRotation(FRotator(0.f, Current + FRotator::NormalizeAxis(SoftTurnYaw - Current) * Fraction, 0.f));
		SoftTurnRemaining -= Step;
		if (SoftTurnRemaining <= KINDA_SMALL_NUMBER)
		{
			SoftTurnRemaining = 0.f;
			SetActorRotation(FRotator(0.f, SoftTurnYaw, 0.f));
		}
	}
	else if (MeleeComponent && MeleeComponent->IsWindingUp() && MeleeComponent->GetSwingTarget())
	{
		// The assist: through the rest of the wind-up she keeps her eyes on the thug she picked as he moves.
		const FVector To = (MeleeComponent->GetSwingTarget()->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			const float Current = GetActorRotation().Yaw;
			const float MaxStep = MeleeAssistTrackDegreesPerSecond * FMath::Max(DeltaSeconds, 0.f);
			const float Step = FMath::Clamp(FRotator::NormalizeAxis(To.Rotation().Yaw - Current), -MaxStep, MaxStep);
			SetActorRotation(FRotator(0.f, Current + Step, 0.f));
		}
	}

	// While a swing, a finisher or a locked-on dodge owns her facing, the stick must not turn her.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bLock = SoftTurnRemaining > 0.f || IsMeleeAttacking() || IsPerformingFinisher() || (IsDodging() && bDodgeFacesTarget);
	if (Movement && bLock)
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
		bFacingLocked = true;
	}
	else if (bFacingLocked)
	{
		bFacingLocked = false;
		ApplyRotationMode();
	}
}

void AHawkeyeCharacter::AdvanceMeleeFlow(float DeltaSeconds)
{
	Combo.Advance(DeltaSeconds);
	UpdateParryBuffer(DeltaSeconds);
	if (bLightBuffered && MeleeComponent && MeleeComponent->CanStartAttack())
	{
		bLightBuffered = false;
		if (Combo.IsChainOpen())
		{
			StartComboLight();
		}
	}
	StrikePose.Advance(DeltaSeconds);
	HitLean.Advance(DeltaSeconds);
	UpdateSoftTurn(DeltaSeconds);
	UpdateArmPoses();
}

void AHawkeyeCharacter::UpdateArmPoses()
{
	UHawkeyeBowIKAnimInstance* Hands = BowComponent ? BowComponent->GetHandsIKInstance() : nullptr;
	if (!Hands)
	{
		return;
	}
	FHawkeyeStrikePoseSample Pose = StrikePose.Sample();
	FVector LeanDirection = HitLean.GetDirection();
	float LeanAlpha = HitLean.GetAlpha();
	// Hanging, the hands go hand over hand along the edge, and the body sways with each reach.
	if (ParkourComponent && !Pose.IsActive())
	{
		ParkourComponent->GetHangArmPose(Pose, LeanDirection, LeanAlpha);
	}
	Hands->SetStrikePose(Pose, GetActorTransform());
	Hands->SetHitLean(LeanDirection, LeanAlpha);
}

void AHawkeyeCharacter::HandleMeleeLanded(AActor* HitActor, float /*DamageDealt*/, FName /*AttackName*/)
{
	// The chain's last light or a heavy is what earns the finisher; a jab on its own does not.
	const bool bComboEnder = !bSwingIsLight || SwingStep >= Combo.Settings.ChainLength - 1;
	if (AThugCharacter* Thug = bComboEnder ? Cast<AThugCharacter>(HitActor) : nullptr)
	{
		Thug->OpenFinisherWindow(ComboEnderFinisherSeconds);
	}
	if (bSwingIsLight)
	{
		Combo.NotifyLightLanded(SwingStep);
		// The clip's combo window, not the timer, closes the chain.
		if (MeleeComponent && MeleeComponent->IsComboFromNotify())
		{
			Combo.SetChainHeld(true);
		}
	}
	else
	{
		Combo.NotifyHit();
	}
	// Hit stop: the world nearly stops for two frames on a light, four on a heavy (with a small lens punch),
	// so the contact reads; a small spark where the blow met him, under his own hit flash.
	const bool bHeavy = !bSwingIsLight;
	ApplyTimeWarp(GetMeleeHitStopSeconds(bHeavy), HitStopTimeDilation);
	if (bHeavy && HeavyCameraPunchSeconds > 0.f)
	{
		CameraPunchElapsed = 0.f;
	}
	SpawnMeleeSpark(HitActor, bHeavy);
}

void AHawkeyeCharacter::HandleMeleeMissed(FName /*AttackName*/)
{
	if (!bSwingIsLight)
	{
		Combo.NotifyMiss();
		bLightBuffered = false;
		return;
	}
	// Punching the air still runs the chain on (Light1, Light2, Light3); only a landed hit counts.
	Combo.NotifyLightMissed(SwingStep);
	if (MeleeComponent && MeleeComponent->IsComboFromNotify())
	{
		Combo.SetChainHeld(true);
	}
}

void AHawkeyeCharacter::HandleComboWindowChanged(bool bOpen)
{
	if (!bOpen)
	{
		Combo.SetChainHeld(false);
	}
}

void AHawkeyeCharacter::ApplyCombatAnimSet()
{
	if (CombatAnimSet)
	{
		CombatAnimSet->Preload();
	}
	if (MeleeComponent)
	{
		MeleeComponent->SetAnimSet(CombatAnimSet);
	}
	if (BowComponent)
	{
		BowComponent->SetAnimSet(CombatAnimSet);
	}
}

void AHawkeyeCharacter::ApplyTimeWarp(float RealSeconds, float Dilation)
{
	UWorld* World = GetWorld();
	// The finisher's own slow motion is not cut short by a smaller one.
	if (!World || RealSeconds <= 0.f || IsPerformingFinisher())
	{
		return;
	}
	// The timer runs in dilated time, hence the multiply.
	UGameplayStatics::SetGlobalTimeDilation(World, Dilation);
	World->GetTimerManager().SetTimer(HitStopTimerHandle, this, &AHawkeyeCharacter::EndHitStop,
		RealSeconds * Dilation, false);
}

void AHawkeyeCharacter::EndHitStop()
{
	if (UWorld* World = GetWorld())
	{
		// Back to the wheel's slow motion if a strike landed while it was open.
		UGameplayStatics::SetGlobalTimeDilation(World, bQuiverWheelOpen ? QuiverWheelTimeDilation : 1.f);
	}
}

// --- Parry ------------------------------------------------------------------------------------

AThugCharacter* AHawkeyeCharacter::FindParryTarget(EHawkeyeParryKind& OutKind) const
{
	OutKind = EHawkeyeParryKind::None;
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AThugCharacter* Best = nullptr;
	float BestDistance = BIG_NUMBER;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		EHawkeyeParryKind Kind = EHawkeyeParryKind::None;
		if (!CanParryNow(*It, &Kind))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
			OutKind = Kind;
		}
	}
	return Best;
}

bool AHawkeyeCharacter::CanParryNow(const AThugCharacter* Thug, EHawkeyeParryKind* OutKind) const
{
	const EHawkeyeParryKind Kind = UHawkeyeMeleeRules::ClassifyParry(Thug);
	if (OutKind)
	{
		*OutKind = Kind;
	}
	if (Kind == EHawkeyeParryKind::None)
	{
		return false;
	}
	const FVector To = Thug->GetActorLocation() - GetActorLocation();
	return FMath::Abs(To.Z) <= 150.f
		&& UHawkeyeMeleeRules::IsInParryWindow(UHawkeyeMeleeRules::GetTelegraphElapsed(Thug), ParryWindowDelta)
		&& UHawkeyeMeleeRules::IsInFrontWithin(GetActorLocation(), GetViewForward(), Thug->GetActorLocation(), ParryRange,
			ParryAngleDegrees);
}

void AHawkeyeCharacter::UpdateParryBuffer(float DeltaSeconds)
{
	if (ParryBufferRemaining <= 0.f)
	{
		return;
	}
	if (TryParryFromBuffer())
	{
		ParryBufferRemaining = 0.f;
		return;
	}
	ParryBufferRemaining = FMath::Max(0.f, ParryBufferRemaining - FMath::Max(DeltaSeconds, 0.f));
}

bool AHawkeyeCharacter::TryParryFromBuffer()
{
	// Her own jab from the same tap is winding up; the parry takes its place.
	TGuardValue<bool> Guard(bParryOverOwnWindup, true);
	return TryParry();
}

bool AHawkeyeCharacter::TryParry()
{
	const bool bOwnWindup = MeleeComponent && MeleeComponent->IsWindingUp() && !bParryOverOwnWindup;
	if (!MeleeComponent || bOwnWindup || IsLockedOutByTakedown() || IsZipping() || IsTraversing()
		|| IsDodging() || IsStaggered() || IsDrawingBow() || bDowned)
	{
		return false;
	}
	EHawkeyeParryKind Kind = EHawkeyeParryKind::None;
	AThugCharacter* Thug = FindParryTarget(Kind);
	if (!Thug)
	{
		return false;
	}
	if (Kind == EHawkeyeParryKind::Burst)
	{
		AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
		if (!Brain || !Brain->DeflectFirstShot())
		{
			return false;
		}
	}
	else
	{
		Thug->Parried(this, ParryStaggerSeconds);
	}

	// Her own recovery gives way to it; a light waiting for it is spent.
	MeleeComponent->CancelAttack();
	bLightBuffered = false;
	const FVector To = (Thug->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	BeginSoftTurn(To);
	if (HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet, ECombatAnimRole::Parry))
	{
		StrikePose.Stop();
	}
	else
	{
		StrikePose.Start(EHawkeyeStrikePose::Light, 0.08f);
	}
	UpdateArmPoses();
	++ParryCount;
	if (IsPlayerControlled())
	{
		UChapterEventSubsystem::NotifyEvent(this, UHawkeyeChapterEventRules::Parry);
	}

	const FVector Contact = (GetActorLocation() + Thug->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, 40.f);
	UHawkeyeAudioSubsystem::PlayAt(this, ParrySound, Contact, TEXT("parry"));
	// "Reduce flashing" draws the ring at 30%: a smaller flash, the same read.
	UHawkeyeVfxSubsystem::SpawnAt(this, ParryVfx, Contact, (-To).Rotation(), UHawkeyeVfxSubsystem::ParryEvent, FlashScale);
	ApplyTimeWarp(ParryHitStopSeconds, HitStopTimeDilation);
	UE_LOG(LogHawkeye, Log, TEXT("%s: parried %s's %s from %.0f cm (%d parries)."), *GetNameSafe(this), *GetNameSafe(Thug),
		Kind == EHawkeyeParryKind::Burst ? TEXT("burst") : (Kind == EHawkeyeParryKind::Bash ? TEXT("bash") : TEXT("swing")),
		FVector::Dist2D(Thug->GetActorLocation(), GetActorLocation()), ParryCount);
	return true;
}

// --- Finisher ---------------------------------------------------------------------------------

bool AHawkeyeCharacter::IsPerformingFinisher() const
{
	return FinisherComponent && FinisherComponent->IsPerformingFinisher();
}

bool AHawkeyeCharacter::TryFinisher()
{
	if (!FinisherComponent || IsLockedOutByTakedown() || IsZipping() || IsTraversing() || IsStaggered() || IsDodging()
		|| bDowned || (HealthComponent && !HealthComponent->IsAlive()))
	{
		return false;
	}
	const bool bBowOut = BowComponent && BowComponent->GetBow() && (BowComponent->IsBowRaised() || IsDrawingBow());
	if (!FinisherComponent->FindTarget())
	{
		return false;
	}
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	bLightBuffered = false;
	bMeleeHeld = false;
	if (IsDrawingBow())
	{
		BowComponent->CancelDraw();
	}
	EndSlide();
	return FinisherComponent->TryFinisher(bBowOut);
}

void AHawkeyeCharacter::HandleFinisherStarted(AActor* Target, EHawkeyeFinisherStyle Style)
{
	SoftTurnRemaining = 0.f;
	const bool bBow = Style == EHawkeyeFinisherStyle::Bow;
	const float Duration = FinisherComponent ? FinisherComponent->DurationSeconds : 1.2f;
	if (HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet,
			bBow ? ECombatAnimRole::FinisherBow : ECombatAnimRole::FinisherAttacker, Duration))
	{
		StrikePose.Stop();
	}
	else
	{
		StrikePose.Start(bBow ? EHawkeyeStrikePose::BowSweep : EHawkeyeStrikePose::Heavy,
			FinisherComponent ? FinisherComponent->StrikeAtSeconds : 0.3f);
	}
	if (AThugCharacter* Thug = Cast<AThugCharacter>(Target))
	{
		Thug->PlayCombatClip(ECombatAnimRole::FinisherVictim);
	}
	UpdateArmPoses();
}

void AHawkeyeCharacter::HandleFinisherStruck(AActor* /*Target*/)
{
	Combo.NotifyHit();
}

// --- Dodge ------------------------------------------------------------------------------------

bool AHawkeyeCharacter::TryDodge(FVector WorldDirection)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const FVector Direction = WorldDirection.GetSafeNormal2D();
	if (!Movement || Direction.IsNearlyZero() || bIsSprinting || IsDodging() || DodgeCooldownRemaining > 0.f
		|| IsLockedOutByTakedown() || IsZipping() || IsTraversing() || IsStaggered() || IsRolling())
	{
		return false;
	}
	// On the ground only; a test world's character has no floor, so falling is the one refusal.
	if (Movement->IsFalling() || Movement->IsFlying())
	{
		return false;
	}

	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	StopAim();
	if (IsCrouchWanted())
	{
		EndCrouch(TEXT("dodge"));
	}

	DodgeRemaining = DodgeSeconds;
	DodgeInvulnerableRemaining = DodgeInvulnerableSeconds;
	DodgeCooldownRemaining = DodgeCooldownSeconds;
	bLightBuffered = false;

	// Locked on, she keeps her eyes on him while the dash goes where the stick says.
	const AActor* Facing = FindSoftLockTarget();
	bDodgeFacesTarget = Facing != nullptr;
	if (Facing)
	{
		BeginSoftTurn(Facing->GetActorLocation() - GetActorLocation());
	}
	CheckPerfectDodge();
	// The clip for where the dash goes relative to where she will be facing; the dash below moves her.
	const FVector Face = Facing ? (Facing->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : GetActorForwardVector();
	HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet,
		UCombatAnimSet::DodgeRoleFor(UHawkeyeMeleeRules::ClassifyHitDirection(Face, Direction)), DodgeSeconds);
	if (HealthComponent && !HealthComponent->IsInvulnerable() && DodgeInvulnerableSeconds > 0.f)
	{
		HealthComponent->SetInvulnerable(true);
		bDodgeOwnsInvulnerability = true;
	}

	// A root motion force: it overrides the gait speed the sample's graph writes, so the dash
	// covers exactly DodgeDistance, and it stops dead at the end instead of sliding on.
	TSharedPtr<FRootMotionSource_ConstantForce> Dash = MakeShared<FRootMotionSource_ConstantForce>();
	Dash->InstanceName = FName(TEXT("Dodge"));
	Dash->AccumulateMode = ERootMotionAccumulateMode::Override;
	Dash->Priority = 5;
	Dash->Force = Direction * (DodgeDistance / DodgeSeconds);
	Dash->Duration = DodgeSeconds;
	Dash->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Dash->FinishVelocityParams.ClampVelocity = WalkSpeed;
	Movement->ApplyRootMotionSource(Dash);

	UE_LOG(LogHawkeye, Log, TEXT("%s: dodge %.0f cm toward %s, invulnerable %.2f s."), *GetNameSafe(this),
		DodgeDistance, *Direction.ToCompactString(), DodgeInvulnerableSeconds);
	return true;
}

void AHawkeyeCharacter::CheckPerfectDodge()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const EHawkeyeParryKind Kind = UHawkeyeMeleeRules::ClassifyParry(*It);
		// An unblockable swing is the one a dodge is for: it counts as much as a parryable one.
		const bool bMelee = Kind == EHawkeyeParryKind::Swing || Kind == EHawkeyeParryKind::Bash || UHawkeyeMeleeRules::IsUnblockableWindup(*It);
		if (bMelee && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= PerfectDodgeRange)
		{
			++PerfectDodgeCount;
			ApplyTimeWarp(PerfectDodgeSeconds, PerfectDodgeDilation);
			// TODO(stage3): refill 10% focus here once the focus meter exists.
			UE_LOG(LogHawkeye, Log, TEXT("%s: perfect dodge through %s's %s (%d so far)."), *GetNameSafe(this), *It->GetName(),
				Kind == EHawkeyeParryKind::Bash ? TEXT("bash") : (Kind == EHawkeyeParryKind::None ? TEXT("unblockable") : TEXT("swing")),
				PerfectDodgeCount);
			return;
		}
	}
}

void AHawkeyeCharacter::UpdateDodge(float DeltaSeconds)
{
	DodgeCooldownRemaining = FMath::Max(0.f, DodgeCooldownRemaining - DeltaSeconds);
	DodgeRemaining = FMath::Max(0.f, DodgeRemaining - DeltaSeconds);
	if (DodgeRemaining <= 0.f)
	{
		bDodgeFacesTarget = false;
	}
	if (DodgeInvulnerableRemaining <= 0.f)
	{
		return;
	}
	DodgeInvulnerableRemaining = FMath::Max(0.f, DodgeInvulnerableRemaining - DeltaSeconds);
	if (DodgeInvulnerableRemaining <= 0.f && bDodgeOwnsInvulnerability)
	{
		bDodgeOwnsInvulnerability = false;
		if (HealthComponent)
		{
			HealthComponent->SetInvulnerable(false);
		}
	}
}

// --- Hit reactions ----------------------------------------------------------------------------

void AHawkeyeCharacter::HandleStaggered(UHealthComponent* /*Health*/, AActor* DamageInstigator)
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	StaggerRemaining = PlayerStaggerSeconds;
	bMeleeHeld = false;
	UHawkeyeAudioSubsystem::PlayAt(this, StaggerSound, GetActorLocation(), TEXT("stagger"));
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	if (IsDrawingBow())
	{
		BowComponent->CancelDraw();
	}
	FVector Away = DamageInstigator ? (GetActorLocation() - DamageInstigator->GetActorLocation()).GetSafeNormal2D()
		: -GetActorForwardVector();
	if (Away.IsNearlyZero())
	{
		Away = -GetActorForwardVector();
	}
	LaunchCharacter(Away * PlayerStaggerShove, true, false);
	HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet,
		UCombatAnimSet::HitRoleFor(UHawkeyeMeleeRules::ClassifyHitDirection(GetActorForwardVector(), -Away)));
	UE_LOG(LogHawkeye, Log, TEXT("%s: staggered by %s, health %.1f."), *GetNameSafe(this),
		*GetNameSafe(DamageInstigator), HealthComponent->GetCurrentHealth());
}

void AHawkeyeCharacter::HandleHealthChanged(UHealthComponent* /*Health*/, float /*NewHealth*/, float Delta, AActor* DamageInstigator)
{
	// Someone else hurting her shakes the camera and leans her away; a fall does not (it has the landing dip).
	if (Delta < 0.f && DamageInstigator && DamageInstigator != this)
	{
		PlayImpactShake(HitShakeSeconds, HitShakeAmplitude);
		const EHawkeyeHitDirection Side = UHawkeyeMeleeRules::ClassifyHitDirection(GetActorForwardVector(),
			DamageInstigator->GetActorLocation() - GetActorLocation());
		HitLean.Start(UHawkeyeMeleeRules::ComputeLeanDirection(GetActorForwardVector(), Side));
	}
}

void AHawkeyeCharacter::UpdateHitReactions(float DeltaSeconds)
{
	StaggerRemaining = FMath::Max(0.f, StaggerRemaining - DeltaSeconds);
	HitShakeRemaining = FMath::Max(0.f, HitShakeRemaining - DeltaSeconds);
	if (MotionBlurOffRemaining > 0.f)
	{
		MotionBlurOffRemaining = FMath::Max(0.f, MotionBlurOffRemaining - DeltaSeconds);
		if (MotionBlurOffRemaining <= 0.f && FollowCamera)
		{
			FollowCamera->PostProcessSettings.bOverride_MotionBlurAmount = bSavedMotionBlurOverride;
			FollowCamera->PostProcessSettings.MotionBlurAmount = SavedMotionBlurAmount;
		}
	}
	UpdateScreenPulse(DeltaSeconds);
}

float AHawkeyeCharacter::GetScreenPulseStrength() const
{
	if (ScreenPulseRemaining <= 0.f || ScreenPulseSeconds <= 0.f)
	{
		return 0.f;
	}
	// Sharp on, eased off.
	const float Alpha = ScreenPulseRemaining / ScreenPulseSeconds;
	return ScreenPulseStart * Alpha * Alpha;
}

void AHawkeyeCharacter::PlayScreenPulse(float Seconds, float InStrength)
{
	// "Reduce flashing" softens the EMP's chromatic split to 30%.
	const float Strength = InStrength * FlashScale;
	if (Seconds <= 0.f || Strength <= 0.f || GetScreenPulseStrength() > Strength)
	{
		return;
	}
	ScreenPulseSeconds = Seconds;
	ScreenPulseRemaining = Seconds;
	ScreenPulseStart = FMath::Clamp(Strength, 0.f, 1.f);
	UHawkeyeVfxSubsystem::NoteRequest(this, UHawkeyeVfxSubsystem::ScreenPulseEvent, GetActorLocation());
	if (!ScreenPulseInstance && FollowCamera && !ScreenPulseMaterial.IsNull())
	{
		// Loaded here, not at BeginPlay: most of a run never sees an EMP.
		if (UMaterialInterface* Material = ScreenPulseMaterial.LoadSynchronous())
		{
			ScreenPulseInstance = UMaterialInstanceDynamic::Create(Material, this);
			FollowCamera->PostProcessSettings.AddBlendable(ScreenPulseInstance, 0.f);
		}
	}
	UpdateScreenPulse(0.f);
}

void AHawkeyeCharacter::UpdateScreenPulse(float DeltaSeconds)
{
	if (ScreenPulseRemaining <= 0.f && ScreenPulseStart <= 0.f)
	{
		return;
	}
	ScreenPulseRemaining = FMath::Max(0.f, ScreenPulseRemaining - DeltaSeconds);
	const float Strength = GetScreenPulseStrength();
	if (ScreenPulseRemaining <= 0.f)
	{
		ScreenPulseStart = 0.f;
	}
	if (ScreenPulseInstance && FollowCamera)
	{
		ScreenPulseInstance->SetScalarParameterValue(TEXT("Intensity"), Strength);
		// Weight 0 once it is over, so the pass is skipped rather than run as a no-op.
		FollowCamera->PostProcessSettings.AddBlendable(ScreenPulseInstance, Strength > 0.f ? 1.f : 0.f);
	}
}

float AHawkeyeCharacter::ComputeLowHealthAlpha(float HealthPercent) const
{
	if (LowHealthThreshold <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp((LowHealthThreshold - HealthPercent) / LowHealthThreshold, 0.f, 1.f);
}

void AHawkeyeCharacter::UpdateLowHealthPostProcess()
{
	if (!FollowCamera || !HealthComponent)
	{
		return;
	}
	const float Alpha = HealthComponent->IsAlive() ? ComputeLowHealthAlpha(HealthComponent->GetHealthPercent()) : 1.f;
	if (FMath::IsNearlyEqual(Alpha, LastLowHealthAlpha, 0.005f))
	{
		return;
	}
	LastLowHealthAlpha = Alpha;

	// Only override while it shows, so at full health the level's own post process is untouched.
	FPostProcessSettings& Settings = FollowCamera->PostProcessSettings;
	const bool bOn = Alpha > 0.f;
	const float Saturation = FMath::Lerp(1.f, LowHealthSaturation, Alpha);
	Settings.bOverride_ColorSaturation = bOn;
	Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);
	Settings.bOverride_VignetteIntensity = bOn;
	Settings.VignetteIntensity = FMath::Lerp(0.4f, LowHealthVignette, Alpha);
}

bool AHawkeyeCharacter::StartSlide()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || bIsSliding || !bIsSprinting || !Movement->IsMovingOnGround())
	{
		return false;
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < SlideMinSpeed)
	{
		return false;
	}

	bIsSliding = true;
	SlideRemaining = SlideSeconds;
	SlideDirection = GetVelocity().GetSafeNormal2D();
	PreSlideGroundFriction = Movement->GroundFriction;
	Movement->GroundFriction = SlideGroundFriction;

	// Straight to the movement component, so the capsule is low this frame rather than next tick.
	Movement->bWantsToCrouch = true;
	Movement->Crouch();
	UpdateMaxWalkSpeed();

	UE_LOG(LogHawkeye, Log, TEXT("%s: slide start at %.0f cm/s, capsule half-height %.0f"),
		*GetNameSafe(this), Speed, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
	return true;
}

void AHawkeyeCharacter::UpdateSlide(float DeltaSeconds)
{
	if (!bIsSliding)
	{
		return;
	}

	SlideRemaining -= DeltaSeconds;
	AddMovementInput(SlideDirection, 1.f);
	if (SlideRemaining <= 0.f)
	{
		EndSlide();
	}
}

void AHawkeyeCharacter::EndSlide()
{
	if (!bIsSliding)
	{
		return;
	}

	bIsSliding = false;
	SlideRemaining = 0.f;
	// A toggled sprint that slid is done: she comes up out of the slide at a run.
	StopSprintToggle(EHawkeyeSprintStop::SlideEnded);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GroundFriction = PreSlideGroundFriction;
		// UnCrouch checks for headroom; under a low ceiling the character stays crouched.
		PendingCrouchEndReason = TEXT("slide end");
		bPendingCrouchEndUnasked = false;
		Movement->bWantsToCrouch = false;
		Movement->UnCrouch();
	}
	UpdateMaxWalkSpeed();

	UE_LOG(LogHawkeye, Log, TEXT("%s: slide end at %.0f cm/s, crouched=%d"),
		*GetNameSafe(this), GetVelocity().Size2D(), bIsCrouched ? 1 : 0);
}

void AHawkeyeCharacter::Jump()
{
	if (IsLandingInputLocked() || bDowned)
	{
		return;
	}
	if (IsZipping())
	{
		// The jump key lets go of the line; the catch rule still applies on the way down.
		UE_LOG(LogHawkeye, Log, TEXT("%s: jump cancels the zip"), *GetNameSafe(this));
		GrappleComponent->ReleaseZip(/*bJump=*/true);
		return;
	}
	if (ParkourComponent)
	{
		// Hanging, jump climbs (with the stick to one side it leaps along, with it back it hops round). Mid-move
		// the press waits for the end of the move. Otherwise an obstacle in the fan ahead turns it into a vault,
		// mantle or ledge grab, and open ground gets a plain jump that keeps looking for a mantle or a ledge on
		// the way up and down.
		if (ParkourComponent->IsHanging())
		{
			ParkourComponent->JumpFromHang();
			return;
		}
		if (ParkourComponent->IsBusy())
		{
			ParkourComponent->BufferJump();
			return;
		}
		EndSlide();
		if (ParkourComponent->TryParkour(false))
		{
			return;
		}
		// Nothing ahead, an edge behind: down to the hang on it.
		if (ParkourComponent->TryDropToHang(-GetActorForwardVector(), TEXT("jump facing away from the edge")))
		{
			return;
		}
	}
	EndSlide();
	Super::Jump();
	if (ParkourComponent)
	{
		ParkourComponent->ArmLateCatch();
	}
}

void AHawkeyeCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	const UHawkeyeMovementComponent* Movement = GetHawkeyeMovement();
	if (Movement && (Movement->MovementMode == MOVE_Custom || Movement->MovementMode == MOVE_Flying))
	{
		// A parkour move or hang, a zip, or the sample's traversal (which runs in Flying) ends the crouch; she comes
		// out of it standing. (The movement component would stand her up for it and crouch her again after.)
		EndSlide();
		if (IsCrouchWanted() && !bDowned && !IsRolling())
		{
			EndCrouch(Movement->IsTraversing() ? TEXT("a parkour move or hang")
				: Movement->IsZipping() ? TEXT("a zip") : TEXT("the sample's traversal"));
		}
	}
	if (Movement && Movement->IsFalling())
	{
		// Measured from the top of the arc, so a jump off a roof counts its rise as well.
		FallApexZ = GetActorLocation().Z;
		FallSeconds = 0.f;
		EndSlide();
		// Not rising: stepped off an edge or out of the sample's traversal. Any way off a zip, a hang or one of
		// our moves counts, a jump's let-go and the hop's push off the wall included, since that is what she lets go of.
		const bool bFromZip = PrevMovementMode == MOVE_Custom && PreviousCustomMode == static_cast<uint8>(EHawkeyeCustomMovement::Zip);
		const bool bFromTraversal = PrevMovementMode == MOVE_Custom
			&& PreviousCustomMode == static_cast<uint8>(EHawkeyeCustomMovement::Traversal);
		bControlledDrop = bFromZip || bFromTraversal || (Movement->Velocity.Z <= 0.f
			&& (PrevMovementMode == MOVE_Walking || PrevMovementMode == MOVE_NavWalking || PrevMovementMode == MOVE_Flying));
		if (bControlledDrop)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: controlled drop from %s at feet %.0f"), *GetNameSafe(this),
				bFromZip ? TEXT("a zip") : bFromTraversal ? TEXT("a hang or a parkour move")
					: PrevMovementMode == MOVE_Flying ? TEXT("the sample's traversal") : TEXT("an edge"),
				GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		}
	}
}

void AHawkeyeCharacter::UpdateFalling(float DeltaSeconds)
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement && Movement->IsFalling())
	{
		FallApexZ = FMath::Max(FallApexZ, GetActorLocation().Z);
		FallSeconds += FMath::Max(DeltaSeconds, 0.f);
		// A real fall stands her up; a lip or a one-frame flick to falling keeps the crouch.
		const float Drop = FallApexZ - GetActorLocation().Z;
		if (IsCrouchWanted() && !bIsSliding && !IsRolling() && !bDowned && FallSeconds >= CrouchKeepFallSeconds
			&& Drop >= CrouchKeepFallHeight)
		{
			EndCrouch(TEXT("a fall"));
		}
	}
}

float AHawkeyeCharacter::GetCurrentFallHeight() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || !Movement->IsFalling())
	{
		return 0.f;
	}
	return FMath::Max(0.f, FMath::Max(FallApexZ, GetActorLocation().Z) - GetActorLocation().Z);
}

void AHawkeyeCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	const float Z = GetActorLocation().Z;
	ApplyLanding(FMath::Max(0.f, FMath::Max(FallApexZ, Z) - Z));
}

float AHawkeyeCharacter::ComputeFallDamageFraction(float FallHeight) const
{
	if (FallHeight < FallDamageMinHeight)
	{
		return 0.f;
	}

	const float Span = FallDamageMaxHeight - FallDamageMinHeight;
	const float Alpha = Span > 0.f ? FMath::Clamp((FallHeight - FallDamageMinHeight) / Span, 0.f, 1.f) : 1.f;
	return FMath::Lerp(FallDamageMinFraction, FallDamageMaxFraction, Alpha);
}

const FName AHawkeyeCharacter::IntensityParameter(TEXT("Intensity"));

void AHawkeyeCharacter::UpdateFootsteps()
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const FVector Feet = GetActorLocation();
	const float Travelled = bHasFootLocation ? FVector::Dist2D(Feet, LastFootLocation) : 0.f;
	LastFootLocation = Feet;
	bHasFootLocation = true;
	const bool bAlive = !HealthComponent || HealthComponent->IsAlive();
	const bool bGrounded = Movement && Movement->IsMovingOnGround() && !IsZipping() && bAlive;
	if (Footsteps.Advance(Travelled, bIsSprinting, bGrounded))
	{
		PlayFootstep();
	}
}

EHawkeyeFootstepSurface AHawkeyeCharacter::GetFootstepSurface() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const AActor* Floor = Movement && Movement->CurrentFloor.IsWalkableFloor() ? Movement->CurrentFloor.HitResult.GetActor() : nullptr;
	return Floor ? HawkeyeAudioMath::ClassifyFootstepSurface(Floor->Tags) : EHawkeyeFootstepSurface::Snow;
}

const TArray<TSoftObjectPtr<USoundBase>>& AHawkeyeCharacter::GetFootstepSoundsFor(EHawkeyeFootstepSurface Surface) const
{
	const TArray<TSoftObjectPtr<USoundBase>>& Set = Surface == EHawkeyeFootstepSurface::Wood ? WoodFootstepSounds
		: Surface == EHawkeyeFootstepSurface::Carpet ? CarpetFootstepSounds : FootstepSounds;
	return Set.Num() > 0 ? Set : FootstepSounds;
}

void AHawkeyeCharacter::PlayFootstep()
{
	++FootstepCount;
	const EHawkeyeFootstepSurface Surface = GetFootstepSurface();
	// Only snow kicks up; a board or a carpet does not.
	if (Surface == EHawkeyeFootstepSurface::Snow)
	{
		KickFootstepSnow();
	}
	const TArray<TSoftObjectPtr<USoundBase>>& Sounds = GetFootstepSoundsFor(Surface);
	if (Sounds.Num() == 0)
	{
		return;
	}
	int32 Index = FMath::RandRange(0, Sounds.Num() - 1);
	if (Sounds.Num() > 1 && Index == LastFootstepIndex)
	{
		Index = (Index + 1) % Sounds.Num();
	}
	LastFootstepIndex = Index;
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	UHawkeyeAudioSubsystem::PlayAt(this, Sounds[Index], GetActorLocation() - FVector(0.f, 0.f, HalfHeight),
		Surface == EHawkeyeFootstepSurface::Snow ? TEXT("footstep") : Surface == EHawkeyeFootstepSurface::Wood
		? TEXT("footstep wood") : TEXT("footstep carpet"), bIsSprinting ? 1.f : 0.8f);
}

void AHawkeyeCharacter::KickFootstepSnow()
{
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	const FVector Kick = HawkeyeVfxMath::ComputeFootstepKickDirection(GetActorForwardVector(), GetVelocity());
	UHawkeyeVfxSubsystem::SpawnAt(this, FootstepVfx, GetActorLocation() - FVector(0.f, 0.f, HalfHeight - 3.f),
		Kick.Rotation(), UHawkeyeVfxSubsystem::FootstepEvent, bIsSprinting ? 1.f : 0.75f);
}

void AHawkeyeCharacter::ApplyLanding(float FallHeight)
{
	LastFallHeight = FallHeight;
	const float LandIntensity = HawkeyeAudioMath::ComputeLandingIntensity(FallHeight);
	if (LandIntensity > 0.f)
	{
		if (UAudioComponent* Thud = UHawkeyeAudioSubsystem::PlayAt(this, LandSound, GetActorLocation(), TEXT("land")))
		{
			Thud->SetFloatParameter(IntensityParameter, LandIntensity);
		}
	}
	const float PuffScale = HawkeyeVfxMath::ComputeLandingPuffScale(FallHeight);
	if (PuffScale > 0.f)
	{
		const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		UHawkeyeVfxSubsystem::SpawnAt(this, LandingVfx, GetActorLocation() - FVector(0.f, 0.f, HalfHeight - 3.f),
			FRotator::ZeroRotator, UHawkeyeVfxSubsystem::LandingEvent, PuffScale);
	}
	if (FallHeight >= 50.f)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: landed from %.0f cm at %.0f cm/s"),
			*GetNameSafe(this), FallHeight, GetVelocity().Size2D());
	}

	if (FallHeight > RollHeight)
	{
		// Moving: roll along the stick (or the way she was travelling). Standing: stumble.
		FVector Direction = MoveInputMagnitude > 0.1f ? LastMoveWorldDirection : FVector::ZeroVector;
		if (Direction.IsNearlyZero() && GetVelocity().Size2D() >= RollMinSpeed)
		{
			Direction = GetVelocity().GetSafeNormal2D();
		}
		if (!Direction.IsNearlyZero())
		{
			StartRoll(Direction);
		}
		else
		{
			if (IsRolling())
			{
				EndRoll();
			}
			LandingState = EHawkeyeLanding::Stumble;
			LandingElapsed = 0.f;
			UE_LOG(LogHawkeye, Log, TEXT("%s: landed standing from %.0f cm: stumble %.2f s"), *GetNameSafe(this), FallHeight,
				StumbleSeconds);
		}
	}
	else if (bControlledDrop && FallHeight > ControlledDropDipHeight)
	{
		if (IsRolling())
		{
			EndRoll();
		}
		LandingState = EHawkeyeLanding::Dip;
		LandingElapsed = 0.f;
		UE_LOG(LogHawkeye, Log, TEXT("%s: controlled drop landed from %.0f cm: landing dip"), *GetNameSafe(this), FallHeight);
	}
	bControlledDrop = false;

	const float Fraction = ComputeFallDamageFraction(FallHeight) * FallDamageScale;
	if (Fraction > 0.f && HealthComponent && HealthComponent->IsAlive())
	{
		// Soft by design: a fall takes a chunk, never the last point.
		const float Damage = FMath::Min(Fraction * HealthComponent->GetMaxHealth(),
			HealthComponent->GetCurrentHealth() - 1.f);
		if (Damage > 0.f)
		{
			HealthComponent->ApplyDamage(Damage, nullptr);
			UE_LOG(LogHawkeye, Log, TEXT("%s: fall damage %.1f (%.0f%% of max) from %.0f cm, health now %.1f"),
				*GetNameSafe(this), Damage, Fraction * 100.f, FallHeight, HealthComponent->GetCurrentHealth());
		}
	}
	UpdateMaxWalkSpeed();
}

float AHawkeyeCharacter::GetLandingSpeedFactor() const
{
	switch (LandingState)
	{
	case EHawkeyeLanding::Stumble:
		return StumbleSeconds > 0.f ? FMath::Clamp(LandingElapsed / StumbleSeconds, 0.f, 1.f) : 1.f;
	case EHawkeyeLanding::Dip:
		return LandingSpeedMultiplier;
	default:
		return 1.f;
	}
}

float AHawkeyeCharacter::GetLandingCameraPitch() const
{
	if (!IsRolling() || RollSeconds <= 0.f)
	{
		return 0.f;
	}
	return -RollCameraPitchDegrees * FMath::Sin(PI * FMath::Clamp(LandingElapsed / RollSeconds, 0.f, 1.f));
}

void AHawkeyeCharacter::StartRoll(const FVector& Direction)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	USkeletalMeshComponent* Body = GetMesh();
	if (!Movement)
	{
		return;
	}
	UHawkeyeAudioSubsystem::PlayAt(this, RollSound, GetActorLocation(), TEXT("roll"));
	if (IsRolling())
	{
		EndRoll();
	}

	EndSlide();
	StopAim();
	RollDirection = Direction.GetSafeNormal2D();
	LandingState = EHawkeyeLanding::Roll;
	LandingElapsed = 0.f;
	SetActorRotation(FRotator(0.f, RollDirection.Rotation().Yaw, 0.f));

	// Low for the length of the roll; the crouch is ours to undo only if she was standing.
	PreRollCrouchedHalfHeight = Movement->GetCrouchedHalfHeight();
	Movement->SetCrouchedHalfHeight(RollCapsuleHalfHeight);
	bRollOwnsCrouch = !bIsCrouched;
	Movement->bWantsToCrouch = true;
	Movement->Crouch();

	// A root motion force, like the dodge: it overrides the gait speed the sample's graph writes, so
	// the roll covers RollDistance whatever gait she landed in.
	TSharedPtr<FRootMotionSource_ConstantForce> Carry = MakeShared<FRootMotionSource_ConstantForce>();
	Carry->InstanceName = FName(TEXT("LandingRoll"));
	Carry->AccumulateMode = ERootMotionAccumulateMode::Override;
	Carry->Priority = 5;
	Carry->Force = RollDirection * (RollDistance / RollSeconds);
	Carry->Duration = RollSeconds;
	Carry->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Carry->FinishVelocityParams.ClampVelocity = RunSpeed;
	Movement->ApplyRootMotionSource(Carry);

	if (Body)
	{
		RollMeshBaseLocation = Body->GetRelativeLocation();
		RollMeshBaseRotation = Body->GetRelativeRotation().Quaternion();
		RollMeshBaseScale = Body->GetRelativeScale3D();
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s: landing roll %.0f cm toward %s over %.2f s, capsule half-height %.0f, input locked %.2f s"),
		*GetNameSafe(this), RollDistance, *RollDirection.ToCompactString(), RollSeconds,
		GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), RollInputLockSeconds);
}

void AHawkeyeCharacter::ApplyRollPose()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || !IsRolling())
	{
		return;
	}
	// Once forward head over heels about the capsule centre, squashed into a tuck so the head clears
	// the floor when she is upside down, then straightened for the rest of the roll.
	const float Phase = FMath::Clamp(LandingElapsed / FMath::Max(RollTumbleSeconds, 0.01f), 0.f, 1.f);
	const float Angle = 360.f * FMath::SmoothStep(0.f, 1.f, Phase);
	const float Tuck = FMath::Clamp(1.5f * FMath::Sin(PI * Phase), 0.f, 1.f);
	const float Height = FMath::Lerp(1.f, RollTuckScale, Tuck);
	const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	// The feet ride from the capsule's bottom up to just under its centre as she tucks.
	const float FeetZ = FMath::Lerp(RollMeshBaseLocation.Z, -HalfHeight * Height * 0.9f, Tuck);
	const FQuat Spin = FRotator(-Angle, 0.f, 0.f).Quaternion();
	Body->SetRelativeLocationAndRotation(FVector(RollMeshBaseLocation.X, RollMeshBaseLocation.Y, 0.f)
		+ Spin.RotateVector(FVector(0.f, 0.f, FeetZ)), Spin * RollMeshBaseRotation);
	Body->SetRelativeScale3D(FVector(RollMeshBaseScale.X, RollMeshBaseScale.Y, RollMeshBaseScale.Z * Height));
}

void AHawkeyeCharacter::EndRoll()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetRelativeLocationAndRotation(RollMeshBaseLocation, RollMeshBaseRotation);
		Body->SetRelativeScale3D(RollMeshBaseScale);
	}
	if (Movement)
	{
		Movement->RemoveRootMotionSource(FName(TEXT("LandingRoll")));
		if (bRollOwnsCrouch)
		{
			// UnCrouch checks for headroom; under a low ceiling she stays crouched.
			PendingCrouchEndReason = TEXT("roll end");
			bPendingCrouchEndUnasked = false;
			Movement->bWantsToCrouch = false;
			Movement->UnCrouch();
		}
		if (PreRollCrouchedHalfHeight > 0.f)
		{
			Movement->SetCrouchedHalfHeight(PreRollCrouchedHalfHeight);
		}
	}
	bRollOwnsCrouch = false;
	LandingState = EHawkeyeLanding::None;
	LandingElapsed = 0.f;
	if (FollowCamera)
	{
		FollowCamera->SetRelativeRotation(FRotator::ZeroRotator);
	}
	UpdateMaxWalkSpeed();
}

void AHawkeyeCharacter::UpdateLanding(float DeltaSeconds)
{
	if (LandingState == EHawkeyeLanding::None)
	{
		return;
	}
	LandingElapsed += DeltaSeconds;
	switch (LandingState)
	{
	case EHawkeyeLanding::Roll:
	{
		// Rolling off an edge ends it: the override force would hold her level in the air.
		const UCharacterMovementComponent* Movement = GetCharacterMovement();
		if (LandingElapsed >= RollSeconds || (Movement && Movement->IsFalling()))
		{
			EndRoll();
		}
		else
		{
			ApplyRollPose();
		}
		break;
	}
	case EHawkeyeLanding::Stumble:
		if (LandingElapsed >= StumbleSeconds)
		{
			LandingState = EHawkeyeLanding::None;
		}
		break;
	case EHawkeyeLanding::Dip:
		if (LandingElapsed >= LandingRecoverSeconds)
		{
			LandingState = EHawkeyeLanding::None;
		}
		break;
	default:
		break;
	}
}

bool AHawkeyeCharacter::IsMovementDebugEnabled()
{
	return CVarHawkeyeDebugMovement.GetValueOnGameThread() != 0;
}

void AHawkeyeCharacter::UpdateMovementDebugSample(float DeltaSeconds)
{
	const FVector Location = GetActorLocation();
	if (bDebugHasLocation && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		// Smoothed over about a tenth of a second, so a shimmy reads steady rather than frame to frame.
		const float Instant = FVector::Dist(Location, DebugLastLocation) / DeltaSeconds;
		DebugTraversalSpeed = FMath::FInterpTo(DebugTraversalSpeed, Instant, DeltaSeconds, 12.f);
	}
	DebugLastLocation = Location;
	bDebugHasLocation = true;

	const EHawkeyeParkourMove Move = ParkourComponent ? ParkourComponent->GetActiveMove() : EHawkeyeParkourMove::None;
	DebugMoveSeconds = Move == DebugMoveSeen ? DebugMoveSeconds + DeltaSeconds : 0.f;
	DebugMoveSeen = Move;
}

FString AHawkeyeCharacter::FormatTraversalDebugState(bool bHanging, bool bShimmying, EHawkeyeParkourMove Move,
	float MoveSeconds, bool bSampleTraversal)
{
	const TCHAR* MoveName = nullptr;
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault: MoveName = TEXT("vault"); break;
	case EHawkeyeParkourMove::Mantle: MoveName = TEXT("mantle"); break;
	case EHawkeyeParkourMove::LedgeGrab: MoveName = TEXT("ledge grab"); break;
	case EHawkeyeParkourMove::Climb: MoveName = TEXT("climb"); break;
	case EHawkeyeParkourMove::DropToHang: MoveName = TEXT("drop to hang"); break;
	case EHawkeyeParkourMove::HangCorner: MoveName = TEXT("corner"); break;
	case EHawkeyeParkourMove::HangLeap: MoveName = TEXT("leap"); break;
	case EHawkeyeParkourMove::HangHop: MoveName = TEXT("hop"); break;
	default: break;
	}
	if (MoveName)
	{
		return FString::Printf(TEXT("%s %.2f s"), MoveName, MoveSeconds);
	}
	if (bHanging)
	{
		return bShimmying ? TEXT("hang shimmy") : TEXT("hang");
	}
	return bSampleTraversal ? TEXT("traversal") : FString();
}

FString AHawkeyeCharacter::GetMovementDebugText() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();

	// On a ledge or in a move the gait means nothing: the line names the traversal and its smoothed speed.
	if (ParkourComponent)
	{
		const bool bHanging = ParkourComponent->IsHanging();
		const FString Traversal = FormatTraversalDebugState(bHanging, ParkourComponent->IsShimmying(),
			ParkourComponent->GetActiveMove(), DebugMoveSeconds, ParkourComponent->IsSampleTraversalActive());
		if (!Traversal.IsEmpty())
		{
			return FString::Printf(TEXT("%s  %.0f cm/s%s  last landing %.0f cm"), *Traversal, DebugTraversalSpeed,
				bHanging ? *FString::Printf(TEXT("  along %.0f cm"), ParkourComponent->GetHangAlong()) : TEXT(""),
				LastFallHeight);
		}
	}

	const TCHAR* State = TEXT("walk");
	if (IsZipping())
	{
		State = TEXT("zip");
	}
	else if (Movement && Movement->IsFalling())
	{
		State = TEXT("air");
	}
	else if (Speed < 10.f && CurrentGait != EHawkeyeGait::Slide)
	{
		State = bIsCrouched ? TEXT("crouch") : TEXT("idle");
	}
	else
	{
		switch (CurrentGait)
		{
		case EHawkeyeGait::Run: State = TEXT("run"); break;
		case EHawkeyeGait::Sprint: State = TEXT("sprint"); break;
		case EHawkeyeGait::Crouch: State = TEXT("crouch"); break;
		case EHawkeyeGait::Slide: State = TEXT("slide"); break;
		default: break;
		}
	}

	return FString::Printf(TEXT("%s  %.0f cm/s  fall %.0f cm  last landing %.0f cm%s"),
		State, Speed, GetCurrentFallHeight(), LastFallHeight,
		IsRolling() ? TEXT("  (roll)") : (IsStumbling() ? TEXT("  (stumble)") : (IsRecoveringFromLanding() ? TEXT("  (dip)") : TEXT(""))));
}

bool AHawkeyeCharacter::IsLockedOutByTakedown() const
{
	return bScriptedLock || (TakedownComponent && TakedownComponent->IsPerformingTakedown()) || IsPerformingFinisher();
}

void AHawkeyeCharacter::Input_FirePressed(const FInputActionValue& /*Value*/)
{
	if (bQuiverWheelOpen || IsLockedOutByTakedown() || IsMeleeAttacking() || IsDodging() || IsStaggered() || bDowned)
	{
		return;
	}

	// With a bow, left click draws. Without one it is Hands: a punch, which is quiet, the stealth
	// option that does not bring the block down on you.
	const bool bHasBow = InventoryComponent && InventoryComponent->HasBow();
	if (bHasBow && BowComponent)
	{
		if (!IsZipping() && !IsTraversing())
		{
			BowComponent->StartDraw();
		}
		return;
	}

	if (UWeaponComponent* Weapon = GetWeaponComponent())
	{
		Weapon->Fire();
	}
}

void AHawkeyeCharacter::Input_FireReleased(const FInputActionValue& /*Value*/)
{
	if (BowComponent && BowComponent->IsDrawing())
	{
		BowComponent->ReleaseDraw();
	}
}

void AHawkeyeCharacter::Input_Reload(const FInputActionValue& /*Value*/)
{
	if (UWeaponComponent* Weapon = GetWeaponComponent())
	{
		Weapon->Reload();
	}
}

void AHawkeyeCharacter::Input_Takedown(const FInputActionValue& /*Value*/)
{
	// Bound before Input_Interact (see SetupPlayerInputComponent), so on the gamepad's shared Y
	// button this runs first and, if it lands, tells Input_Interact to skip this press.
	// The stealth takedown first (from behind, unaware); otherwise the loud one, the finisher.
	bTookDownThisPress = (TakedownComponent && TakedownComponent->TryTakedown()) || TryFinisher();
	TakedownPressFrame = GFrameCounter;
}

void AHawkeyeCharacter::Input_Interact(const FInputActionValue& /*Value*/)
{
	// Only the same press: on a keyboard F and E are different keys, and a takedown on F must not
	// swallow the next E (it did, and the challenge pedestals ignored the first E after a fight).
	const bool bSamePress = bTookDownThisPress && TakedownPressFrame == GFrameCounter;
	bTookDownThisPress = false;
	if (bSamePress)
	{
		return;
	}

	if (InteractionComponent)
	{
		InteractionComponent->TryInteract();
	}

	OnInteractPressed();
}
