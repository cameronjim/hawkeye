#include "World/ThugCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Boss/BossRules.h"
#include "Combat/MeleeRules.h"
#include "Combat/WeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/LocomotionAnim.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "Settings/DifficultySubsystem.h"
#include "Vfx/HawkeyeVfxSubsystem.h"

AThugCharacter::AThugCharacter()
{
	// Ticks to swap between the idle and walk sequences; the mannequin pack's AnimBP does not
	// compile headless, so the thugs drove no animation at all and stood in a T-pose.
	PrimaryActorTick.bCanEverTick = true;

	// UTakedownComponent finds candidates by tag, so it has to be set before BeginPlay.
	Tags.Add(FName(TEXT("Thug")));

	// Reused, nothing new built: the roll's body thump and the landing's snow ring, smaller.
	GroundThudSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/SFX/MS_Roll_Thump.MS_Roll_Thump")));
	GroundDustVfx = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/NS_KnockdownDust.NS_KnockdownDust")));

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->MaxHealth = 100.f;
	HealthComponent->CurrentHealth = 100.f;

	WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	WeaponComponent->bHasWeapon = true;
	// Thugs are poor shots: 12 a hit, so the player survives a few.
	WeaponComponent->Damage = 12.f;
	WeaponComponent->MagazineSize = 12;
	WeaponComponent->CurrentAmmo = 12;
	WeaponComponent->ReserveAmmo = 120;
	// Fast enough for the burst's 0.25 s spacing; the burst clock, not the weapon, sets the rhythm.
	WeaponComponent->FireRate = 300.f;
	WeaponComponent->Range = 4000.f;

	// A thug's swing never lands on the thug beside him.
	MeleeComponent = CreateDefaultSubobject<UMeleeComponent>(TEXT("MeleeComponent"));
	MeleeComponent->IgnoreTag = FName(TEXT("Thug"));
	// His clips stretch so the hit still lands at the end of the telegraph: the parry and dodge windows hold.
	MeleeComponent->bFitClipToWindup = true;

	// The wind-up is the telegraph and the dodge window: long enough to read, not so long it
	// stops being a threat. Recovery is what leaves room to hit back.
	FistsAttack.Name = FName(TEXT("fists"));
	FistsAttack.Damage = 15.f;
	FistsAttack.WindupSeconds = 0.6f;
	FistsAttack.RecoverSeconds = 0.6f;
	FistsAttack.Range = 120.f;
	FistsAttack.Radius = 35.f;
	FistsAttack.bStagger = true;
	FistsAttack.AnimRole = ECombatAnimRole::Light1;

	BatAttack = FistsAttack;
	BatAttack.Name = FName(TEXT("bat"));
	BatAttack.AnimRole = ECombatAnimRole::Heavy;
	BatAttack.Damage = 25.f;
	BatAttack.Range = 140.f;

	// The heavy: a long, glowing telegraph for the bash (dodge it or eat 250 cm of shove) and a slower
	// bat than the street thugs'. Neither is fast; he is dangerous because he does not flinch.
	ShieldBashAttack = FistsAttack;
	ShieldBashAttack.Name = FName(TEXT("bash"));
	ShieldBashAttack.AnimRole = ECombatAnimRole::Heavy;
	ShieldBashAttack.Damage = 30.f;
	ShieldBashAttack.WindupSeconds = 0.8f;
	ShieldBashAttack.RecoverSeconds = 0.8f;
	ShieldBashAttack.Range = 150.f;
	ShieldBashAttack.Radius = 45.f;
	ShieldBashAttack.KnockbackDistance = 250.f;
	ShieldBashAttack.KnockbackSeconds = 0.35f;

	HeavySwingAttack = BatAttack;
	HeavySwingAttack.Name = FName(TEXT("heavy_bat"));
	HeavySwingAttack.WindupSeconds = 1.f;
	HeavySwingAttack.RecoverSeconds = 0.9f;
	HeavySwingAttack.Range = 150.f;

	GetCapsuleComponent()->SetCapsuleSize(34.f, 96.f);

	// Both the capsule and the mesh block bullets. The capsule is the guarantee - a greybox
	// thug with no skeletal mesh still has to be killable - and the mesh is what gives the
	// hit a bone name, which is where the headshot multiplier comes from.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);

		// The mannequin is authored facing its own +Y, so a -90 degree yaw is what points it down
		// the actor's +X and makes the walk cycle agree with the direction of travel. This lived
		// only in create_world_blueprints.py, which meant a thug spawned from C++ faced ninety
		// degrees off and nothing in the test suite could see it.
		SkeletalMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));
	}

	// The bat rides in the right hand. No collision: the swing is a sweep, not the prop.
	HeldWeaponComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldWeapon"));
	HeldWeaponComponent->SetupAttachment(GetMesh(), HeldWeaponSocketName);
	HeldWeaponComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldWeaponComponent->SetCanEverAffectNavigation(false);
	HeldWeaponComponent->SetVisibility(false);

	// The archer's bow. Inert (no mesh, no draw) on every thug until OwnBow is set. With no draw
	// animation his hand hangs at his hip, so DA_Bow_Archer names no hand socket and arrows leave
	// from here: in front of his cheek, where a drawn bow is anchored, high enough to clear his parapet.
	BowComponent = CreateDefaultSubobject<UBowComponent>(TEXT("BowComponent"));
	BowComponent->ArrowLaunchOffset = FVector(45.f, 0.f, 65.f);

	// The gunner's pistol and the telegraph glint: engine shapes, so a thug needs no art for them.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto MakeProp = [this](const TCHAR* Name, UStaticMesh* PropMesh, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->SetVisibility(false);
		if (PropMesh)
		{
			Part->SetStaticMesh(PropMesh);
		}
		return Part;
	};
	// Slide 20 x 3.5 x 4 cm along the pistol's X; the grip hangs under its back half.
	PistolComponent = MakeProp(TEXT("Pistol"), Cube.Succeeded() ? Cube.Object : nullptr, GetCapsuleComponent());
	PistolComponent->SetUsingAbsoluteLocation(true);
	PistolComponent->SetUsingAbsoluteRotation(true);
	PistolComponent->SetWorldScale3D(FVector(0.2f, 0.035f, 0.04f));
	PistolGrip = MakeProp(TEXT("PistolGrip"), Cube.Succeeded() ? Cube.Object : nullptr, PistolComponent);
	PistolGrip->SetRelativeLocationAndRotation(FVector(-0.3f, 0.f, -1.6f), FRotator(-15.f, 0.f, 0.f));
	PistolGrip->SetRelativeScale3D(FVector(0.18f, 0.9f, 2.6f));

	// The riot shield: a slab placed in the world each tick (no animation holds it up). No collision: what
	// it stops is decided by the arc rule, and a blocked arrow is moved onto its face.
	ShieldComponent = MakeProp(TEXT("Shield"), Cube.Succeeded() ? Cube.Object : nullptr, GetCapsuleComponent());
	ShieldComponent->SetUsingAbsoluteLocation(true);
	ShieldComponent->SetUsingAbsoluteRotation(true);
	ShieldComponent->SetCastShadow(true);
	ShieldComponent->SetWorldScale3D(ShieldSize / 100.f);
	// The cube's own material is the editor grid; the shape material takes the smoked grey tint.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		ShieldComponent->SetMaterial(0, ShapeMaterial.Object);
	}

	GlintMesh = MakeProp(TEXT("Glint"), Sphere.Succeeded() ? Sphere.Object : nullptr, GetCapsuleComponent());
	GlintMesh->SetUsingAbsoluteLocation(true);
	GlintMesh->SetWorldScale3D(FVector(0.05f));
	TelegraphLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TelegraphLight"));
	TelegraphLight->SetupAttachment(GlintMesh);
	TelegraphLight->SetUsingAbsoluteScale(true);
	TelegraphLight->SetMobility(EComponentMobility::Movable);
	TelegraphLight->SetIntensityUnits(ELightUnits::Lumens);
	TelegraphLight->SetIntensity(GlintLumens);
	TelegraphLight->SetAttenuationRadius(150.f);
	TelegraphLight->SetCastShadows(false);
	TelegraphLight->SetVisibility(false);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = 300.f;
		// Face where you are actually going. With bUseControllerDesiredRotation the body chased
		// the control rotation instead, and the controller points that at the player the moment
		// he is seen - so an alerted thug closing the distance played a forward walk cycle while
		// travelling sideways or backwards. AThugAIController turns him to face a target only
		// when he has stopped to shoot or swing.
		Movement->bUseControllerDesiredRotation = false;
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.f, 360.f, 0.f);
	}

	bUseControllerRotationYaw = false;

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AThugCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Bound here rather than in BeginPlay: a thug killed the same frame he spawns still has to
	// drop and go limp, and a world that never begins play (tests) still wires the death path.
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AThugCharacter::HandleDeath);
		HealthComponent->OnStaggered.AddDynamic(this, &AThugCharacter::HandleStaggered);
		HealthComponent->OnHealthChanged.AddDynamic(this, &AThugCharacter::HandleHealthChanged);
	}
}

void AThugCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (const USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshRelativeTransform = SkeletalMesh->GetRelativeTransform();
	}
	RefreshHeldWeapon();
	CreateBodyMaterials();
	UpdateLocomotionAnimation();
	ApplyCombatAnimSet();
	for (APickupActor* Carried : CarriedPickups)
	{
		if (IsValid(Carried))
		{
			Carried->SetCarried(true);
		}
	}

	// The difficulty's toughness, on whatever the class gave him (100, the heavy's 200).
	const float HealthScale = UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ThugHealth);
	if (HealthComponent && !FMath::IsNearlyEqual(HealthScale, 1.f))
	{
		HealthComponent->SetMaxHealth(HealthComponent->GetMaxHealth() * HealthScale, /*bResetCurrent=*/true);
	}
}

void AThugCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGroundThud(DeltaSeconds);

	if (bCollapsing)
	{
		// A dead thug has no locomotion to update; he only has a floor to reach.
		UpdateProceduralCollapse(DeltaSeconds);
		return;
	}

	UpdateMaterialPulse(DeltaSeconds);
	AdvanceReadability(DeltaSeconds);
	if (bLimp)
	{
		return;
	}
	UpdatePistolPose();
	UpdateTelegraphGlint();
	UpdateShieldPose();

	StaggerRemaining = FMath::Max(0.f, StaggerRemaining - DeltaSeconds);
	UpdateFinisherOpening(DeltaSeconds);
	UpdateHitLean(DeltaSeconds);
	UpdateKnockdown(DeltaSeconds);
	UpdateGetUp(DeltaSeconds);
	if (!bKnockedDown)
	{
		UpdateLocomotionAnimation();
		UpdateHeldWeaponPose();
	}
}

FVector AThugCharacter::ComputeBatDirection() const
{
	const FQuat Actor = GetActorQuat();
	const FVector Hang = Actor.RotateVector(BatHangDirection.GetSafeNormal());
	const FVector Cocked = Actor.RotateVector(BatCockedDirection.GetSafeNormal());
	const FVector Swung = Actor.RotateVector(BatSwungDirection.GetSafeNormal());
	auto Blend = [](const FVector& From, const FVector& To, float Alpha)
	{
		return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From, To), FMath::Clamp(Alpha, 0.f, 1.f)).RotateVector(From);
	};

	if (!MeleeComponent || !MeleeComponent->IsAttacking())
	{
		return Hang;
	}
	const FHawkeyeMeleeAttack& Attack = MeleeComponent->GetCurrentAttack();
	// The bash is all shield; the bat stays down by his leg.
	if (Attack.Name == ShieldBashAttack.Name)
	{
		return Hang;
	}
	if (MeleeComponent->IsWindingUp())
	{
		// Up and back over the telegraph.
		const float Elapsed = Attack.WindupSeconds - MeleeComponent->GetPhaseRemaining();
		return Blend(Hang, Cocked, FMath::SmoothStep(0.f, 1.f, Attack.WindupSeconds > 0.f ? Elapsed / Attack.WindupSeconds : 1.f));
	}
	// The swing across the front, fast, then down again over the rest of the recovery.
	const float Elapsed = Attack.RecoverSeconds - MeleeComponent->GetPhaseRemaining();
	if (Elapsed < BatSwingSeconds)
	{
		return Blend(Cocked, Swung, Elapsed / BatSwingSeconds);
	}
	const float Rest = FMath::Max(Attack.RecoverSeconds - BatSwingSeconds, 0.01f);
	return Blend(Swung, Hang, FMath::SmoothStep(0.f, 1.f, (Elapsed - BatSwingSeconds) / Rest));
}

void AThugCharacter::UpdateHeldWeaponPose()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!HeldWeaponComponent || !HeldWeaponComponent->IsVisible() || !SkeletalMesh
		|| !SkeletalMesh->DoesSocketExist(HeldWeaponSocketName))
	{
		return;
	}
	// The hand's bone axes differ between skeletons and clips; the bat's direction comes from the
	// actor and the swing instead, and only its grip from the hand. The engine cylinder is 100 cm
	// along Z about its centre, scaled to the bat's length.
	const FVector Direction = ComputeBatDirection();
	const float Length = 100.f * HeldWeaponComponent->GetRelativeScale3D().Z;
	const FVector Hand = SkeletalMesh->GetSocketLocation(HeldWeaponSocketName);
	HeldWeaponComponent->SetWorldLocationAndRotation(Hand + Direction * (Length * 0.5f - BatGripFromEnd),
		FRotationMatrix::MakeFromZ(Direction).Rotator());
}

void AThugCharacter::RefreshHeldWeapon()
{
	if (!HeldWeaponComponent)
	{
		return;
	}

	const bool bShowBat = (Weapon == EThugWeapon::Bat || Weapon == EThugWeapon::Shield) && BatMesh != nullptr;
	if (bShowBat && HeldWeaponComponent->GetStaticMesh() != BatMesh)
	{
		HeldWeaponComponent->SetStaticMesh(BatMesh);
	}
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		if (SkeletalMesh->DoesSocketExist(HeldWeaponSocketName)
			&& HeldWeaponComponent->GetAttachSocketName() != HeldWeaponSocketName)
		{
			HeldWeaponComponent->AttachToComponent(
				SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform, HeldWeaponSocketName);
		}
	}
	HeldWeaponComponent->SetVisibility(bShowBat);
}

void AThugCharacter::CreateBodyMaterials()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	BodyMaterials.Reset();
	for (int32 Slot = 0; Slot < SkeletalMesh->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* Instance = SkeletalMesh->CreateAndSetMaterialInstanceDynamic(Slot))
		{
			BodyMaterials.Add(Instance);
		}
	}
}

void AThugCharacter::FlashHit()
{
	HitFlashRemaining = HitFlashSeconds;
}

void AThugCharacter::UpdateMaterialPulse(float DeltaSeconds)
{
	HitFlashRemaining = FMath::Max(0.f, HitFlashRemaining - DeltaSeconds);
	const float Flash = GetHitFlashAlpha();
	const float Telegraph = MeleeComponent && MeleeComponent->IsWindingUp() && !bLimp ? 1.f : 0.f;
	if (Flash == LastFlashWritten && Telegraph == LastTelegraphWritten)
	{
		return;
	}

	LastFlashWritten = Flash;
	LastTelegraphWritten = Telegraph;
	for (UMaterialInstanceDynamic* Instance : BodyMaterials)
	{
		if (Instance)
		{
			Instance->SetScalarParameterValue(HitFlashParameter, Flash);
			Instance->SetScalarParameterValue(TelegraphParameter, Telegraph);
		}
	}
}

void AThugCharacter::HandleHealthChanged(UHealthComponent* Health, float /*NewHealth*/, float Delta, AActor* DamageInstigator)
{
	if (Delta >= 0.f)
	{
		return;
	}
	FlashHit();
	StartHitLean(DamageInstigator);
	if (const UWorld* World = GetWorld())
	{
		LastDamagedSeconds = World->GetTimeSeconds();
	}
	// Any hit breaks a gunner's burst or an archer's draw: hitting first is the counter.
	if (Health && Health->IsAlive())
	{
		UHawkeyeAudioSubsystem::PlayAt(this, HurtSound, GetActorLocation(), TEXT("thug hurt"));
		if (AThugAIController* Brain = Cast<AThugAIController>(GetController()))
		{
			Brain->NotifyDamaged(DamageInstigator);
		}
	}
}

void AThugCharacter::SetWeaponRaised(bool bRaised, FVector AimPoint)
{
	bWeaponRaised = bRaised;
	WeaponAimPoint = AimPoint;
}

void AThugCharacter::SetTelegraphGlint(bool bOn)
{
	if (bGlintOn == bOn)
	{
		return;
	}
	bGlintOn = bOn;
	UpdateTelegraphGlint();
	if (bOn)
	{
		UHawkeyeAudioSubsystem::PlayAt(this, TelegraphSound, GetGlintLocation(), TEXT("thug telegraph"));
	}
}

FVector AThugCharacter::GetGlintLocation() const
{
	if (IsArcher() && BowComponent && BowComponent->IsDrawing())
	{
		return BowComponent->GetNockedArrowTip();
	}
	if (PistolComponent)
	{
		// The slide's front face: the engine cube is 100 cm about its centre.
		const FTransform Pistol = PistolComponent->GetComponentTransform();
		return Pistol.TransformPosition(FVector(56.f, 0.f, 0.f));
	}
	return GetActorLocation();
}

void AThugCharacter::UpdatePistolPose()
{
	if (!PistolComponent)
	{
		return;
	}
	const bool bShow = IsGunner() && !bLimp;
	if (PistolComponent->IsVisible() != bShow)
	{
		PistolComponent->SetVisibility(bShow, /*bPropagateToChildren=*/true);
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (!bShow)
	{
		return;
	}
	const FName Hand(TEXT("hand_r"));
	const FName Shoulder(TEXT("upperarm_r"));
	const bool bHasBones = Body && Body->DoesSocketExist(Hand) && Body->DoesSocketExist(Shoulder);
	const FVector HandPoint = bHasBones ? Body->GetSocketLocation(Hand) : GetActorTransform().TransformPosition(FVector(20.f, 25.f, -5.f));
	if (bWeaponRaised)
	{
		const FVector ShoulderPoint = bHasBones ? Body->GetSocketLocation(Shoulder) : GetActorTransform().TransformPosition(FVector(0.f, 20.f, 50.f));
		FRotator Aim = (WeaponAimPoint - ShoulderPoint).Rotation();
		Aim.Roll = 0.f;
		PistolComponent->SetWorldLocationAndRotation(ShoulderPoint + Aim.RotateVector(PistolRaisedOffset), Aim);
		return;
	}
	// At his side, pointing down and forward.
	PistolComponent->SetWorldLocationAndRotation(HandPoint, FRotator(-60.f, GetActorRotation().Yaw, 0.f));
}

void AThugCharacter::UpdateTelegraphGlint()
{
	if (!GlintMesh || !TelegraphLight)
	{
		return;
	}
	const bool bShow = bGlintOn && !bLimp;
	if (bShow && !bGlintTinted)
	{
		// M_Emissive (Color x Intensity) so the bead reads from the next roof; the nock material, then
		// the plain shape material, when it has not been built.
		bGlintTinted = true;
		const TSoftObjectPtr<UMaterialInterface> Emissive{ FSoftObjectPath(TEXT("/Game/Materials/M_Emissive.M_Emissive")) };
		const TSoftObjectPtr<UMaterialInterface> Nock{ FSoftObjectPath(AArrowProjectile::DefaultNockMaterialPath) };
		if (UMaterialInterface* Loaded = Emissive.LoadSynchronous())
		{
			GlintMesh->SetMaterial(0, Loaded);
		}
		else if (UMaterialInterface* NockMaterial = Nock.LoadSynchronous())
		{
			GlintMesh->SetMaterial(0, NockMaterial);
		}
		const FLinearColor Color = IsArcher() ? BowGlintColor : PistolGlintColor;
		if (UMaterialInstanceDynamic* Tint = GlintMesh->CreateDynamicMaterialInstance(0))
		{
			Tint->SetVectorParameterValue(TEXT("Color"), Color);
			Tint->SetScalarParameterValue(TEXT("Intensity"), GlintEmissive);
		}
		TelegraphLight->SetLightColor(Color);
		TelegraphLight->SetIntensity(GlintLumens);
		GlintMesh->SetWorldScale3D(FVector(IsArcher() ? 0.1f : 0.06f));
	}
	if (GlintMesh->IsVisible() != bShow)
	{
		GlintMesh->SetVisibility(bShow);
		TelegraphLight->SetVisibility(bShow);
	}
	if (bShow)
	{
		GlintMesh->SetWorldLocation(GetGlintLocation());
	}
}

void AThugCharacter::HandleStaggered(UHealthComponent* /*Health*/, AActor* DamageInstigator)
{
	// Arrows and light strikes that get round the shield still hurt the heavy, but he does not flinch:
	// only a heavy strike (Knockdown), the bola and the blast move him.
	if (IsHeavy())
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: shrugs off the stagger from %s."), *GetName(), *GetNameSafe(DamageInstigator));
		AlertTo(DamageInstigator);
		return;
	}
	HitReaction(DamageInstigator);
}

void AThugCharacter::AlertTo(AActor* By)
{
	// Being hit is how he finds out: he turns on whoever did it.
	if (AThugAIController* Brain = Cast<AThugAIController>(GetController()))
	{
		if (By)
		{
			Brain->ReportStimulus(EStimulusKind::Hearing, By->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			if (Cast<APawn>(By))
			{
				Brain->SetTarget(By);
			}
		}
	}
	else if (AlertState != EThugAlertState::Alerted)
	{
		SetAlertState(EThugAlertState::Alerted);
	}
}

void AThugCharacter::HitReaction(AActor* HitBy)
{
	if (bLimp || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}

	StaggerRemaining = StaggerSeconds;
	UHawkeyeAudioSubsystem::PlayAt(this, StaggerSound, GetActorLocation(), TEXT("thug stagger"));

	// A punch in the wind-up interrupts the swing: hitting first is the counter.
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}

	// Stop whatever he was doing and shove him back a step, away from the hit. On the floor the
	// ragdoll is the reaction; a shove would only drag the empty capsule about.
	if (AController* MyController = GetController())
	{
		MyController->StopMovement();
	}
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement && !bKnockedDown)
	{
		Movement->StopMovementImmediately();
		FVector Away = HitBy ? (GetActorLocation() - HitBy->GetActorLocation()).GetSafeNormal2D()
			: -GetActorForwardVector();
		if (Away.IsNearlyZero())
		{
			Away = -GetActorForwardVector();
		}
		LaunchCharacter(Away * HitShoveSpeed, true, false);
		// The clip for the side it came from; the stagger and the shove are the same with or without one.
		PlayCombatClip(UCombatAnimSet::HitRoleFor(UHawkeyeMeleeRules::ClassifyHitDirection(GetActorForwardVector(), -Away)));
	}

	AlertTo(HitBy);

	UE_LOG(LogHawkeye, Log, TEXT("%s: hit reaction (by %s), staggered %.2f s, health %.1f."), *GetName(),
		*GetNameSafe(HitBy), StaggerSeconds, HealthComponent->GetCurrentHealth());
}

void AThugCharacter::Knockdown(AActor* By)
{
	if (IsHeavy())
	{
		// Kate's heavy does not put the heavy down; it knocks his guard open for GuardBreakSeconds.
		const float Normal = StaggerSeconds;
		StaggerSeconds = GuardBreakSeconds;
		HitReaction(By);
		StaggerSeconds = Normal;
		UE_LOG(LogHawkeye, Log, TEXT("%s: guard broken by %s's heavy, open for %.1f s."), *GetName(), *GetNameSafe(By),
			GuardBreakSeconds);
		return;
	}
	KnockdownFor(By, KnockdownSeconds, KnockdownLaunchSpeed);
}

void AThugCharacter::KnockdownFor(AActor* By, float Seconds, float LaunchSpeed)
{
	if (bLimp || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}

	// Knocked over again while still getting up: the blend ends here and the ragdoll starts over.
	FinishGetUp();
	bKnockedDown = true;
	KnockdownRemaining = FMath::Max(Seconds, 0.f);
	StaggerRemaining = 0.f;
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	if (AController* MyController = GetController())
	{
		MyController->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	// Kate walks over him while he is down; the capsule still takes hits and arrows.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// A heavy or a trip goes over in the clip when he has one; a blast always throws the ragdoll.
	const float Launch = LaunchSpeed < 0.f ? KnockdownLaunchSpeed : LaunchSpeed;
	bKnockdownClip = Launch <= KnockdownLaunchSpeed && PlayKnockdownClip();
	bKnockdownRagdoll = !bKnockdownClip && BeginKnockdownRagdoll(By, Launch);
	AlertTo(By);
	ScheduleGroundThud(bKnockdownClip ? KnockdownClipThudDelay : GroundThudDelay);

	UE_LOG(LogHawkeye, Log, TEXT("%s: knocked down by %s for %.1f s (%s), health %.1f."), *GetName(), *GetNameSafe(By),
		KnockdownRemaining, bKnockdownClip ? TEXT("clip") : (bKnockdownRagdoll ? TEXT("ragdoll") : TEXT("no ragdoll")),
		HealthComponent->GetCurrentHealth());
}

bool AThugCharacter::BeginKnockdownRagdoll(AActor* By, float LaunchSpeed)
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset() || !SkeletalMesh->GetPhysicsAsset())
	{
		return false;
	}

	MeshRelativeTransform = SkeletalMesh->GetRelativeTransform();
	SkeletalMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SkeletalMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// A thrown body must not tunnel an interior's 20 cm wall.
	SkeletalMesh->SetAllUseCCD(true);
	SkeletalMesh->SetAllBodiesSimulatePhysics(true);
	SkeletalMesh->SetSimulatePhysics(true);
	SkeletalMesh->WakeAllRigidBodies();
	if (!SkeletalMesh->IsSimulatingPhysics())
	{
		StandUp();
		return false;
	}

	FVector Away = By ? (GetActorLocation() - By->GetActorLocation()).GetSafeNormal2D() : -GetActorForwardVector();
	if (Away.IsNearlyZero())
	{
		Away = -GetActorForwardVector();
	}
	// A big throw (the blast) goes up as well as out; a heavy or a trip barely leaves the floor.
	const float Lift = LaunchSpeed > KnockdownLaunchSpeed ? LaunchSpeed * 0.5f : 150.f;
	SkeletalMesh->SetAllPhysicsLinearVelocity(Away * LaunchSpeed + FVector(0.f, 0.f, Lift));
	return true;
}

void AThugCharacter::UpdateKnockdown(float DeltaSeconds)
{
	if (!bKnockedDown)
	{
		return;
	}
	KnockdownRemaining -= DeltaSeconds;
	if (KnockdownRemaining <= 0.f)
	{
		StandUp();
	}
}

void AThugCharacter::StaggerFor(AActor* By, float Seconds)
{
	const float Normal = StaggerSeconds;
	StaggerSeconds = FMath::Max(Seconds, 0.f);
	HitReaction(By);
	StaggerSeconds = Normal;
}

void AThugCharacter::Parried(AActor* By, float Seconds)
{
	if (bLimp || bKnockedDown || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	const FName Swing = MeleeComponent ? MeleeComponent->GetCurrentAttack().Name : NAME_None;
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	StaggerFor(By, Seconds);
	ParryStaggerRemaining = FMath::Max(Seconds, 0.f);
	StartHitLean(By);
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s parried by %s, staggered %.1f s."), *GetName(), *Swing.ToString(), *GetNameSafe(By),
		Seconds);
}

void AThugCharacter::OpenFinisherWindow(float Seconds)
{
	if (bLimp || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	FinisherWindowRemaining = FMath::Max(FinisherWindowRemaining, Seconds);
}

bool AThugCharacter::IsFinisherOpen() const
{
	return bKnockedDown || IsParryStaggered() || FinisherWindowRemaining > 0.f;
}

void AThugCharacter::UpdateFinisherOpening(float DeltaSeconds)
{
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	FinisherWindowRemaining = FMath::Max(0.f, FinisherWindowRemaining - Step);
	ParryStaggerRemaining = FMath::Max(0.f, ParryStaggerRemaining - Step);
}

void AThugCharacter::HoldStagger(float Seconds)
{
	if (!bKnockedDown && !bLimp)
	{
		StaggerRemaining = FMath::Max(StaggerRemaining, Seconds);
	}
}

bool AThugCharacter::ReceiveFinisher(AActor* By, FVector Velocity)
{
	if (bLimp || !HealthComponent || !HealthComponent->IsAlive())
	{
		return false;
	}
	// Straight to the health, round the shield: the finisher is the answer to a guard already broken. A boss
	// takes a share of his health instead (gameplay-semantics.md, "Bosses").
	const float Blow = FinisherDamageFraction > 0.f
		? UBossRules::FinisherDamage(HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(), FinisherDamageFraction)
		: HealthComponent->GetCurrentHealth() + HealthComponent->GetMaxHealth();
	HealthComponent->ApplyDamage(Blow, By);
	const bool bKilled = !HealthComponent->IsAlive();
	if (bKilled && IsRagdolling())
	{
		GetMesh()->SetAllPhysicsLinearVelocity(Velocity, /*bAddToCurrent=*/false);
	}
	return bKilled;
}

void AThugCharacter::StartHitLean(const AActor* HitBy)
{
	if (!HitBy || HitBy == this || bLimp)
	{
		return;
	}
	const FVector ToAttacker = HitBy->GetActorLocation() - GetActorLocation();
	LastHitDirection = UHawkeyeMeleeRules::ClassifyHitDirection(GetActorForwardVector(), ToAttacker);
	HitLean.Start(UHawkeyeMeleeRules::ComputeLeanDirection(GetActorForwardVector(), LastHitDirection));
}

void AThugCharacter::UpdateHitLean(float DeltaSeconds)
{
	const bool bWasLeaning = HitLean.IsActive();
	HitLean.Advance(DeltaSeconds);
	if (!bWasLeaning && !HitLean.IsActive())
	{
		return;
	}
	if (UHawkeyeBowIKAnimInstance* Hands = BowComponent ? BowComponent->GetHandsIKInstance() : nullptr)
	{
		Hands->SetHitLean(HitLean.GetDirection(), HitLean.GetAlpha());
	}
}

void AThugCharacter::StandUp()
{
	if (bKnockdownClip)
	{
		StandUpFromClip();
		return;
	}
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	bool bBlendUp = false;
	if (SkeletalMesh && Capsule && SkeletalMesh->IsSimulatingPhysics() && GetUpSeconds > 0.f)
	{
		// Stand where the body came to rest, on whatever floor is under the pelvis.
		const FVector Pelvis = SkeletalMesh->GetBoneLocation(FName(TEXT("pelvis")));
		FVector Stand = Pelvis + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
		FHitResult Floor;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugStandUp), false, this);
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Floor, Pelvis + FVector(0.f, 0.f, 50.f),
				Pelvis - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params))
		{
			Stand = Floor.ImpactPoint + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight() + 2.f);
		}

		// The bodies keep simulating where they lie, but stop carrying the component with them, and
		// they stop answering the capsule so it can stand inside them. The mesh goes back on the
		// capsule, the clip plays, and the physics weight on every body starts at 1: what shows is
		// still the ragdoll, until UpdateGetUp brings the weight down.
		SkeletalMesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
		SkeletalMesh->SetEnablePhysicsBlending(false);
		SkeletalMesh->SetAllBodiesPhysicsBlendWeight(1.f);
		SkeletalMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		SetActorLocation(Stand, false, nullptr, ETeleportType::None);
		SkeletalMesh->AttachToComponent(Capsule, FAttachmentTransformRules::KeepWorldTransform);
		SkeletalMesh->SetRelativeTransform(MeshRelativeTransform);
		bBlendUp = true;
	}
	else if (SkeletalMesh && Capsule && SkeletalMesh->IsSimulatingPhysics())
	{
		// Stand where the body came to rest, on whatever floor is under the pelvis.
		const FVector Pelvis = SkeletalMesh->GetBoneLocation(FName(TEXT("pelvis")));
		FVector Stand = Pelvis + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
		FHitResult Floor;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugStandUp), false, this);
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Floor, Pelvis + FVector(0.f, 0.f, 50.f),
				Pelvis - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params))
		{
			Stand = Floor.ImpactPoint + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight() + 2.f);
		}

		SkeletalMesh->SetSimulatePhysics(false);
		SkeletalMesh->SetAllBodiesSimulatePhysics(false);
		SkeletalMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
		SkeletalMesh->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);
		SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		SkeletalMesh->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
		SkeletalMesh->SetRelativeTransform(MeshRelativeTransform);
	}

	if (Capsule)
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	const bool bWasDown = bKnockedDown;
	bKnockedDown = false;
	bKnockdownRagdoll = false;
	KnockdownRemaining = 0.f;
	bGettingUp = bBlendUp;
	GetUpElapsed = 0.f;

	// The ragdoll left the bones wherever they fell; re-playing the clip puts him back on his feet.
	CurrentLocomotionAnim = nullptr;
	UpdateLocomotionAnimation();
	if (bWasDown)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s."), *GetName(),
			bBlendUp ? *FString::Printf(TEXT("getting up, blending out of the ragdoll over %.2f s"), GetUpSeconds) : TEXT("back on his feet"));
	}
}

void AThugCharacter::UpdateGetUp(float DeltaSeconds)
{
	if (!bGettingUp)
	{
		return;
	}
	GetUpElapsed += DeltaSeconds;
	if (bGetUpFromClip)
	{
		if (GetUpElapsed >= GetUpClipSeconds)
		{
			FinishGetUp();
		}
		return;
	}
	const float Alpha = FMath::Clamp(GetUpElapsed / GetUpSeconds, 0.f, 1.f);
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetAllBodiesPhysicsBlendWeight(1.f - FMath::SmoothStep(0.f, 1.f, Alpha));
	}
	if (Alpha >= 1.f)
	{
		FinishGetUp();
	}
}

void AThugCharacter::FinishGetUp()
{
	if (!bGettingUp)
	{
		return;
	}
	if (bGetUpFromClip)
	{
		// The clip never touched the physics; there is nothing to put back.
		bGetUpFromClip = false;
		bGettingUp = false;
		GetUpElapsed = 0.f;
		UE_LOG(LogHawkeye, Log, TEXT("%s: back on his feet."), *GetName());
		return;
	}
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetAllBodiesSimulatePhysics(false);
		SkeletalMesh->SetAllBodiesPhysicsBlendWeight(0.f);
		SkeletalMesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::SimulationUpatesComponentTransform;
		SkeletalMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
		SkeletalMesh->SetCollisionResponseToChannel(ECC_HawkeyeWeapon, ECR_Block);
	}
	bGettingUp = false;
	GetUpElapsed = 0.f;
	UE_LOG(LogHawkeye, Log, TEXT("%s: back on his feet."), *GetName());
}

void AThugCharacter::ApplyCombatAnimSet()
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

bool AThugCharacter::PlayCombatClip(ECombatAnimRole ClipRole, float FitToSeconds)
{
	return !bLimp && HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet, ClipRole, FitToSeconds) != nullptr;
}

bool AThugCharacter::PlayKnockdownClip()
{
	UAnimMontage* Montage = UCombatAnimSet::Resolve(CombatAnimSet, ECombatAnimRole::Knockdown);
	UAnimInstance* Instance = Montage ? HawkeyeCombatAnim::Play(GetMesh(), Montage) : nullptr;
	KnockdownMontage = Instance ? Montage : nullptr;
	KnockdownInstance = Instance;
	return Instance != nullptr;
}

void AThugCharacter::StandUpFromClip()
{
	// The get-up cuts the knockdown clip; without one the knockdown clip blends back into his
	// locomotion over GetUpSeconds, where the ragdoll would have blended.
	UAnimInstance* GetUp = HawkeyeCombatAnim::PlayRole(GetMesh(), CombatAnimSet, ECombatAnimRole::GetUp);
	UAnimMontage* GetUpMontage = GetUp ? UCombatAnimSet::Resolve(CombatAnimSet, ECombatAnimRole::GetUp) : nullptr;
	if (!GetUp)
	{
		HawkeyeCombatAnim::Stop(KnockdownInstance.Get(), KnockdownMontage, GetUpSeconds);
	}
	GetUpClipSeconds = GetUpMontage ? GetUpMontage->GetPlayLength() : GetUpSeconds;
	KnockdownMontage = nullptr;
	KnockdownInstance = nullptr;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	bKnockedDown = false;
	bKnockdownClip = false;
	KnockdownRemaining = 0.f;
	bGettingUp = true;
	bGetUpFromClip = true;
	GetUpElapsed = 0.f;
	UE_LOG(LogHawkeye, Log, TEXT("%s: getting up (%s, %.2f s)."), *GetName(), GetUpMontage ? *GetUpMontage->GetName()
		: TEXT("knockdown clip blending out"), GetUpClipSeconds);
}

UAnimSequence* AThugCharacter::SelectLocomotionAnim() const
{
	const float Speed = GetVelocity().Size2D();
	if (Speed > RunAnimSpeedThreshold && RunAnim)
	{
		return RunAnim;
	}
	return Speed > WalkAnimSpeedThreshold ? WalkAnim : IdleAnim;
}

void AThugCharacter::UpdateLocomotionAnimation()
{
	if (bLimp)
	{
		return;
	}

	// A greybox thug with no mesh still tracks which animation it would be playing, which is
	// what the test asserts on; there is simply nothing to play it through.
	HawkeyeLocomotion::PlayIfChanged(GetMesh(), SelectLocomotionAnim(), CurrentLocomotionAnim);
}

void AThugCharacter::SetAlertState(EThugAlertState NewState)
{
	if (AlertState == NewState)
	{
		return;
	}

	const EThugAlertState OldState = AlertState;
	AlertState = NewState;

	UE_LOG(LogHawkeye, Verbose, TEXT("%s: alert state %d -> %d."),
		*GetName(), static_cast<int32>(OldState), static_cast<int32>(NewState));

	AlertGlyph.Trigger(NewState == EThugAlertState::Alerted, NewState == EThugAlertState::Suspicious);
	OnAlertStateChanged.Broadcast(OldState, NewState);
}

bool AThugCharacter::CanBeTakenDown_Implementation(AActor* /*Attacker*/)
{
	// The stealth reward: once he has confirmed you, you have to fight him.
	return !bRefusesTakedown && AlertState != EThugAlertState::Alerted && HealthComponent && HealthComponent->IsAlive();
}

void AThugCharacter::OnTakedown_Implementation(AActor* Attacker)
{
	GoLimp(Attacker);
	DropLoot();

	if (HealthComponent)
	{
		// Routed through health so OnDeath listeners (and the drop) behave identically to a
		// bullet. The attacker goes with it, so the death line names who did it instead of None.
		HealthComponent->ApplyDamage(9999.f, Attacker);
	}
}

void AThugCharacter::HandleDeath(UHealthComponent* /*Health*/, AActor* Killer)
{
	// Log-level, not Verbose: "did anything I shot actually die" is the first question of
	// every playtest, and it has to be answerable from the default log.
	UE_LOG(LogHawkeye, Log, TEXT("%s died (killed by %s, alert state %d)."),
		*GetName(), *GetNameSafe(Killer), static_cast<int32>(AlertState));
	UHawkeyeAudioSubsystem::PlayAt(this, DeathSound, GetActorLocation(), TEXT("thug death"));

	// Already on the floor from a knockdown: the body is down, no second thud.
	if (!bKnockedDown)
	{
		ScheduleGroundThud(GroundThudDelay);
	}
	else if (GroundThudRemaining > 0.f)
	{
		// Killed mid-fall: the ragdoll takes him the rest of the way, no later than the clip would have.
		ScheduleGroundThud(FMath::Min(GroundThudRemaining, GroundThudDelay));
	}
	GoLimp(Killer);
	DropLoot();
}

void AThugCharacter::ScheduleGroundThud(float Delay)
{
	ScheduledThudDelay = Delay;
	GroundThudRemaining = FMath::Max(Delay, KINDA_SMALL_NUMBER);
}

void AThugCharacter::UpdateGroundThud(float DeltaSeconds)
{
	if (GroundThudRemaining <= 0.f)
	{
		return;
	}
	GroundThudRemaining -= FMath::Max(DeltaSeconds, 0.f);
	if (GroundThudRemaining > 0.f)
	{
		return;
	}
	GroundThudRemaining = 0.f;
	++GroundThudCount;
	// Under his hips, wherever the ragdoll or the clip took them; on the ground a trace finds below.
	const USkeletalMeshComponent* Body = GetMesh();
	const bool bHasPelvis = Body && Body->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE;
	FVector At = bHasPelvis ? Body->GetBoneLocation(TEXT("pelvis")) : GetActorLocation();
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugGroundThud), false, this);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 20.f), At - FVector(0.f, 0.f, 250.f), ECC_Visibility, Params))
		{
			At = Hit.ImpactPoint + FVector(0.f, 0.f, 3.f);
		}
	}
	UHawkeyeAudioSubsystem::PlayAt(this, GroundThudSound, At, TEXT("thug ground thud"));
	UHawkeyeVfxSubsystem::SpawnAt(this, GroundDustVfx, At, FRotator::ZeroRotator, UHawkeyeVfxSubsystem::KnockdownDustEvent,
		GroundDustScale);
}

void AThugCharacter::RestoreAsDead()
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	// Whatever he carried was dropped the first time he died; the save does not keep pickups.
	bLootDropped = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: dead in the save; putting him down again."), *GetName());
	HealthComponent->SetInvulnerable(false);
	HealthComponent->ApplyDamage(HealthComponent->GetMaxHealth() * 10.f, nullptr);
}

void AThugCharacter::SpudPreStore_Implementation(const USpudState* /*State*/)
{
	bSavedDead = HealthComponent && !HealthComponent->IsAlive();
}

void AThugCharacter::SpudPostRestore_Implementation(const USpudState* /*State*/)
{
	if (bSavedDead)
	{
		RestoreAsDead();
	}
}

void AThugCharacter::GoLimp(AActor* Killer)
{
	if (bLimp)
	{
		return;
	}
	bLimp = true;

	// Stop the AI first: a behaviour tree still issuing move orders fights the ragdoll.
	if (AController* MyController = GetController())
	{
		MyController->StopMovement();
		MyController->UnPossess();
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		// The capsule stops driving the mesh; the bodies do.
		Movement->SetComponentTickEnabled(false);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// A swing in progress dies with him; a knockdown or a get-up ends here.
	if (MeleeComponent)
	{
		MeleeComponent->CancelAttack();
	}
	if (BowComponent)
	{
		BowComponent->CancelDraw();
	}
	SetTelegraphGlint(false);
	if (PistolComponent)
	{
		PistolComponent->SetVisibility(false, true);
	}
	// The shield goes down with his arm.
	if (ShieldComponent && ShieldComponent->IsVisible() && GetMesh() && GetMesh()->DoesSocketExist(FName(TEXT("lowerarm_l"))))
	{
		ShieldComponent->SetUsingAbsoluteLocation(false);
		ShieldComponent->SetUsingAbsoluteRotation(false);
		ShieldComponent->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform, FName(TEXT("lowerarm_l")));
	}
	bKnockedDown = false;
	FinishGetUp();

	// A greybox thug may have no skeletal mesh at all; ragdoll only when there is something to
	// sim, and only when the mesh has a physics asset to sim it with.
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: died with no skeletal mesh; nothing to drop."), *GetName());
		SetActorTickEnabled(false);
		return;
	}

	// The mesh is attached to the capsule in the character's default layout, and a simulating
	// body that is still welded to its parent will not fall.
	SkeletalMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	if (SkeletalMesh->GetPhysicsAsset())
	{
		SkeletalMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SkeletalMesh->SetAllUseCCD(true);
		SkeletalMesh->SetAllBodiesSimulatePhysics(true);
		SkeletalMesh->SetSimulatePhysics(true);
		SkeletalMesh->WakeAllRigidBodies();

		if (SkeletalMesh->IsSimulatingPhysics())
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: went down by ragdoll (physics asset %s)."),
				*GetName(), *GetNameSafe(SkeletalMesh->GetPhysicsAsset()));

			// The corpse stays where it lands; nothing should ever try to move the actor again.
			SetActorTickEnabled(false);
			return;
		}

		// A physics asset whose bodies are named for a different skeleton leaves
		// InitArticulated with no root body, and SetSimulatePhysics silently does nothing.
		// That is the bug that had thugs freezing upright, so never trust it: check.
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s: physics asset %s did not start simulating (no matching root body); "
				 "falling back to the procedural collapse."),
			*GetName(), *GetNameSafe(SkeletalMesh->GetPhysicsAsset()));
		SkeletalMesh->SetSimulatePhysics(false);
	}
	else
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s: the mesh %s has no physics asset; falling back to the procedural collapse."),
			*GetName(), *GetNameSafe(SkeletalMesh->GetSkeletalMeshAsset()));
	}

	BeginProceduralCollapse(Killer);
}

bool AThugCharacter::IsRagdolling() const
{
	const USkeletalMeshComponent* SkeletalMesh = GetMesh();
	return SkeletalMesh != nullptr && SkeletalMesh->IsSimulatingPhysics();
}

float AThugCharacter::GetCollapseAlpha() const
{
	if (!bLimp || CollapseSeconds <= 0.f)
	{
		return bCollapsing ? 0.f : (bLimp ? 1.f : 0.f);
	}
	return FMath::Clamp(CollapseElapsed / CollapseSeconds, 0.f, 1.f);
}

void AThugCharacter::BeginProceduralCollapse(AActor* Killer)
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh)
	{
		return;
	}

	// A body that is still mid-walk-cycle while it tips over reads as a bug, so freeze the pose.
	SkeletalMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	if (DeathAnim)
	{
		CurrentLocomotionAnim = DeathAnim;
		SkeletalMesh->PlayAnimation(DeathAnim, /*bLooping=*/false);
	}
	else
	{
		CurrentLocomotionAnim = nullptr;
		SkeletalMesh->Stop();
	}

	// Nothing should collide with the corpse; it is a prop from here on.
	SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// He falls away from whoever put him down, or straight forward when that is not known.
	FVector Away = GetActorForwardVector().GetSafeNormal2D();
	if (Killer)
	{
		const FVector Delta = (GetActorLocation() - Killer->GetActorLocation()).GetSafeNormal2D();
		if (!Delta.IsNearlyZero())
		{
			Away = Delta;
		}
	}
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}

	// Rotating about Up x Away carries the body's up vector towards Away: he pitches over
	// that way whatever direction the mesh itself happens to be facing.
	CollapseAxis = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
	if (CollapseAxis.IsNearlyZero())
	{
		CollapseAxis = FVector::RightVector;
	}

	CollapseStartLocation = SkeletalMesh->GetComponentLocation();
	CollapseStartRotation = SkeletalMesh->GetComponentQuat();
	CollapseElapsed = 0.f;
	bCollapsing = true;

	// The collapse is driven from Tick, so the actor has to keep ticking through it.
	SetActorTickEnabled(true);
}

void AThugCharacter::UpdateProceduralCollapse(float DeltaSeconds)
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh)
	{
		bCollapsing = false;
		return;
	}

	CollapseElapsed += DeltaSeconds;
	const float Alpha = CollapseSeconds > 0.f
		? FMath::Clamp(CollapseElapsed / CollapseSeconds, 0.f, 1.f) : 1.f;
	// Accelerating, not linear: a body does not tip over at a constant rate.
	const float Eased = Alpha * Alpha;

	const FQuat Tip(CollapseAxis, FMath::DegreesToRadians(CollapsePitchDegrees * Eased));
	SkeletalMesh->SetWorldLocationAndRotation(
		CollapseStartLocation - FVector(0.f, 0.f, CollapseDropDistance * Eased),
		Tip * CollapseStartRotation);

	if (Alpha >= 1.f)
	{
		bCollapsing = false;
		SetActorTickEnabled(false);
	}
}

void AThugCharacter::DropLoot()
{
	if (bLootDropped)
	{
		return;
	}
	bLootDropped = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Index = 0;
	const int32 Drops = DropOnDeath.Num() + CarriedPickups.Num();
	for (const TSubclassOf<APickupActor>& PickupClass : DropOnDeath)
	{
		if (!PickupClass)
		{
			continue;
		}

		// Fan the drops around the body so two of them never land inside each other, and drop
		// them to floor level: the pickups hover back up to their own height from there.
		const float FanDegrees = 360.f / FMath::Max(Drops, 1) * Index;
		const FVector Fan = FVector(DropSpacing, 0.f, 0.f).RotateAngleAxis(FanDegrees, FVector::UpVector);
		const FVector Offset = GetActorRotation().RotateVector(Fan)
			+ FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = this;

		if (World->SpawnActor<APickupActor>(PickupClass, GetActorLocation() + Offset, FRotator::ZeroRotator, SpawnParams))
		{
			++Index;
		}
	}
	// What he carried comes out of his pocket onto the same fan.
	for (APickupActor* Carried : CarriedPickups)
	{
		if (!IsValid(Carried) || !Carried->IsCarried())
		{
			continue;
		}
		const FVector Fan = FVector(DropSpacing, 0.f, 0.f).RotateAngleAxis(360.f / FMath::Max(Drops, 1) * Index, FVector::UpVector);
		Carried->DropAt(GetActorLocation() + GetActorRotation().RotateVector(Fan)
			- FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		++Index;
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s dropped %d pickup(s)."), *GetName(), Index);
}

// --- The heavy's shield ---------------------------------------------------------------------------------

FHawkeyeMeleeAttack AThugCharacter::GetMeleeAttack(int32 SwingIndex) const
{
	if (ComboAttacks.Num() > 0)
	{
		return ComboAttacks[FMath::Abs(SwingIndex) % ComboAttacks.Num()];
	}
	switch (Weapon)
	{
	case EThugWeapon::Bat:
		return BatAttack;
	case EThugWeapon::Shield:
		return (SwingIndex % 2 == 0) ? ShieldBashAttack : HeavySwingAttack;
	default:
		return FistsAttack;
	}
}

bool AThugCharacter::IsInShieldArc(const FVector& Facing, const FVector& ToAttacker, float ArcDegrees)
{
	const FVector Forward = Facing.GetSafeNormal2D();
	const FVector To = ToAttacker.GetSafeNormal2D();
	if (Forward.IsNearlyZero() || To.IsNearlyZero())
	{
		return false;
	}
	const float HalfArc = FMath::Clamp(ArcDegrees * 0.5f, 0.f, 180.f);
	return FVector::DotProduct(Forward, To) >= FMath::Cos(FMath::DegreesToRadians(HalfArc)) - KINDA_SMALL_NUMBER;
}

bool AThugCharacter::IsShieldRaised() const
{
	return IsHeavy() && !bLimp && HealthComponent && HealthComponent->IsAlive() && !IsIncapacitated();
}

bool AThugCharacter::BlocksHitFrom(const FVector& FromDirection) const
{
	return IsShieldRaised() && IsInShieldArc(GetActorForwardVector(), FromDirection, ShieldArcDegrees);
}

bool AThugCharacter::TryBlock(AActor* Attacker, FVector FromDirection, const FString& What)
{
	if (!BlocksHitFrom(FromDirection))
	{
		return false;
	}
	++BlockCount;
	UE_LOG(LogHawkeye, Log, TEXT("%s: shield blocks %s from %s (%d blocked)."), *GetName(), *What, *GetNameSafe(Attacker),
		BlockCount);
	UHawkeyeAudioSubsystem::PlayAt(this, BlockSound,
		ShieldComponent ? ShieldComponent->GetComponentLocation() : GetActorLocation(), TEXT("shield block"));
	if (Cast<APawn>(Attacker))
	{
		AlertTo(Attacker);
	}
	return true;
}

bool AThugCharacter::ComputeShieldImpact(const FVector& Start, const FVector& Direction, FVector& OutPoint) const
{
	if (!IsHeavy() || !ShieldComponent)
	{
		return false;
	}
	const FTransform Shield = ShieldComponent->GetComponentTransform();
	const FVector Normal = Shield.GetUnitAxis(EAxis::X);
	const FVector Face = Shield.GetLocation() + Normal * (ShieldSize.X * 0.5f);
	const FVector Dir = Direction.GetSafeNormal();
	const float Along = FVector::DotProduct(Dir, Normal);
	// Coming at the face: meet its plane. Glancing along it: the nearest point of the face.
	const FVector Point = FMath::Abs(Along) > KINDA_SMALL_NUMBER
		? Start + Dir * (FVector::DotProduct(Face - Start, Normal) / Along)
		: FVector::PointPlaneProject(Start, Face, Normal);
	FVector Local = Shield.InverseTransformPositionNoScale(Point);
	Local.X = ShieldSize.X * 0.5f;
	Local.Y = FMath::Clamp(Local.Y, -ShieldSize.Y * 0.45f, ShieldSize.Y * 0.45f);
	Local.Z = FMath::Clamp(Local.Z, -ShieldSize.Z * 0.45f, ShieldSize.Z * 0.45f);
	OutPoint = Shield.TransformPositionNoScale(Local);
	return true;
}

void AThugCharacter::UpdateShieldPose()
{
	if (!ShieldComponent)
	{
		return;
	}
	const bool bShow = IsHeavy() && !bLimp;
	if (ShieldComponent->IsVisible() != bShow)
	{
		ShieldComponent->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}
	if (!bShieldTinted)
	{
		bShieldTinted = true;
		if (UMaterialInstanceDynamic* Tint = ShieldComponent->CreateDynamicMaterialInstance(0))
		{
			Tint->SetVectorParameterValue(TEXT("Color"), ShieldColor);
		}
	}
	// Guard in front of his left side; pulled in over the bash's wind-up, driven out at the start of its recovery.
	FVector Offset = ShieldGuardOffset;
	float Turn = 0.f;
	if (MeleeComponent && MeleeComponent->IsAttacking() && MeleeComponent->GetCurrentAttack().Name == ShieldBashAttack.Name)
	{
		const FHawkeyeMeleeAttack& Bash = MeleeComponent->GetCurrentAttack();
		if (MeleeComponent->IsWindingUp())
		{
			const float Alpha = Bash.WindupSeconds > 0.f ? 1.f - MeleeComponent->GetPhaseRemaining() / Bash.WindupSeconds : 1.f;
			Offset.X -= 12.f * FMath::SmoothStep(0.f, 1.f, Alpha);
			Offset.Y += 8.f * Alpha;
		}
		else
		{
			const float Elapsed = Bash.RecoverSeconds - MeleeComponent->GetPhaseRemaining();
			const float Out = Elapsed < 0.15f ? Elapsed / 0.15f : FMath::Max(0.f, 1.f - (Elapsed - 0.15f) / 0.4f);
			Offset.X += ShieldBashReach * Out;
			Offset.Y += 8.f * Out;
		}
	}
	else if (IsIncapacitated())
	{
		// Guard open: the shield swings out wide and low.
		Offset += FVector(-15.f, -25.f, -20.f);
		Turn = -50.f;
	}
	ShieldComponent->SetWorldLocationAndRotation(GetActorTransform().TransformPosition(Offset),
		FRotator(0.f, GetActorRotation().Yaw + Turn, 0.f));
	ShieldComponent->SetWorldScale3D(ShieldSize / 100.f);
}

// --- Readability ----------------------------------------------------------------------------------------

void AThugCharacter::AdvanceReadability(float DeltaSeconds)
{
	AlertGlyph.Advance(DeltaSeconds);
}

float AThugCharacter::GetHealthBarAlpha(float Distance) const
{
	const UWorld* World = GetWorld();
	if (!HealthComponent || bLimp || !World)
	{
		return 0.f;
	}
	return FHawkeyeAlertGlyph::ComputeHealthBarAlpha(HealthComponent->GetHealthPercent(), Distance,
		static_cast<float>(World->GetTimeSeconds() - LastDamagedSeconds));
}

FVector AThugCharacter::GetOverheadLocation() const
{
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
	return GetActorLocation() + FVector(0.f, 0.f, HalfHeight + 28.f);
}

FVector AThugCharacter::GetGlyphLocation() const
{
	const USkeletalMeshComponent* Body = GetMesh();
	if (Body && Body->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		return Body->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, GlyphHeadClearance);
	}
	return GetOverheadLocation();
}
