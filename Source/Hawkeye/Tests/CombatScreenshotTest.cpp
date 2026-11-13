#include "Tests/CombatScreenshots.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Player/InventoryComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/PartnerScreenshots.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The fair-fight shots (Saved/Screenshots/Kate/), run in Hawkeye.Screenshot.Kate after the enemy
 * shots and on their own as Hawkeye.Screenshot.Combat:
 *
 *   heavy_block.png       the StreetGroup heavy 7 m down the sidewalk facing Kate; her full-draw arrow
 *                         stuck in his riot shield, seen close from in front and to his left
 *   heavy_bash.png        Kate 1.3 m in front of him, 0.45 s into the bash's 0.8 s telegraph: mask
 *                         glowing, shield drawn back, seen side on
 *   archer_hold.png       an ArcherPair archer drew on Kate standing on the find_arrow roof; she
 *                         crouched behind the parapet and he holds the draw (glint on), seen from
 *                         just behind her toward him
 *   thug_alert_glyph.png  the StreetPair from 7 m: a "!" over the bat thug (alerted), a "?" over the
 *                         gunner (suspicious) and the gunner's health bar after a hit
 *   aim_view_clear.png    Kate's own camera at full draw on the street, aiming along it: the raised
 *                         bow arm left of centre, the reticle area clear (the report gives where the
 *                         right hand and elbow project, from the screen centre)
 *   arrow_in_kate.png     one of Trickshot's arrows flown into Kate's chest from 5 m, front-left and
 *                         a little above: stuck in her body on a bone, not out on the capsule (the
 *                         report gives the bone and how far the tip is from the body's shapes)
 *
 * Every thug but the one in the shot is frozen; Kate is invulnerable throughout.
 */
namespace HawkeyeCombatShots
{
	static const FName StreetGroupTag(TEXT("StreetGroup"));
	static const FName StreetPairTag(TEXT("StreetPair"));
	static const FName ArcherPairTag(TEXT("ArcherPair"));
	static constexpr double FrozenClock = 2000.0;

	enum class EShot : uint8
	{
		Freeze,
		BlockSetup,
		BlockLoose,
		BlockView,
		BashSetup,
		BashStart,
		HoldSetup,
		HoldDone,
		GlyphSetup,
		GlyphTrigger,
		AimSetup,
		AimDone,
		KateArrowSetup,
		KateArrowView,
		Cleanup,
	};

	struct FSaved
	{
		TWeakObjectPtr<AThugCharacter> Thug;
		FTransform Transform;
	};

	static TArray<FSaved> Saved;
	static TWeakObjectPtr<AThugCharacter> Heavy;
	static TWeakObjectPtr<AThugCharacter> Archer;
	static TWeakObjectPtr<ACameraActor> ShotCamera;
	static TWeakObjectPtr<APointLight> FillLight;
	static TWeakObjectPtr<AArrowProjectile> KateArrow;
	static bool bKateWasInvulnerable = false;

	/** How far Point is outside the nearest of Body's physics shapes (0 inside), or -1 with no physics asset. */
	static float DistanceToBody(const USkeletalMeshComponent* Body, const FVector& Point)
	{
		const UPhysicsAsset* Physics = Body ? Body->GetPhysicsAsset() : nullptr;
		if (!Physics)
		{
			return -1.f;
		}
		float Best = TNumericLimits<float>::Max();
		for (const TObjectPtr<USkeletalBodySetup>& Setup : Physics->SkeletalBodySetups)
		{
			const int32 Bone = Setup ? Body->GetBoneIndex(Setup->BoneName) : INDEX_NONE;
			if (Bone != INDEX_NONE)
			{
				const float Distance = Setup->GetShortestDistanceToPoint(Point, Body->GetBoneTransform(Bone));
				Best = Distance >= 0.f ? FMath::Min(Best, Distance) : Best;
			}
		}
		return Best;
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

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	static AThugAIController* BrainOf(const AThugCharacter* Thug)
	{
		return Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr;
	}

	static void SetThinking(UWorld* World, bool bEnabled)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = BrainOf(*It))
			{
				Brain->SetThinkingEnabled(bEnabled);
			}
		}
	}

	static TArray<AThugCharacter*> Tagged(UWorld* World, FName Tag)
	{
		TArray<AThugCharacter*> Out;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag) && !It->IsLimp())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	static bool Ground(UWorld* World, const FVector& XY, float FromZ, const TArray<const AActor*>& Ignore, FVector& Out,
		AActor** OutActor = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatShotGround), false);
		Params.AddIgnoredActors(Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, FromZ - 6000.f), ECC_Visibility, Params))
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		if (OutActor)
		{
			*OutActor = Hit.GetActor();
		}
		return true;
	}

	static void Stand(ACharacter* Character, const FVector& Feet, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}

	static FVector FeetOf(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/** A camera at Eye looking at Target, with a soft fill light over the lens (the street is at night). */
	static void Frame(UWorld* World, APlayerController* PC, const FVector& Eye, const FVector& Target, float Fov, float FillLumens = 400.f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = ShotCamera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
			ShotCamera = Camera;
		}
		APointLight* Light = FillLight.Get();
		if (!Light)
		{
			Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
			FillLight = Light;
			if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
			{
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetAttenuationRadius(900.f);
				Lamp->SetCastShadows(false);
			}
		}
		if (!Camera)
		{
			return;
		}
		Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(Fov);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
		if (Light)
		{
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 80.f));
			Light->PointLightComponent->SetIntensity(FillLumens);
		}
	}

	static void Unframe(APlayerController* PC, AHawkeyeCharacter* Kate)
	{
		PC->SetViewTarget(Kate);
		if (APointLight* Light = FillLight.Get())
		{
			Light->PointLightComponent->SetIntensity(0.f);
		}
	}

	/** Holds Bow at Fraction of its draw on its test clock, starting the draw if it is not drawing. */
	static void HoldDraw(UBowComponent* Bow, float Fraction)
	{
		const UBowDefinition* Definition = Bow ? Bow->GetBow() : nullptr;
		if (!Definition)
		{
			return;
		}
		if (!Bow->IsDrawing())
		{
			Bow->SetTestTimeSeconds(FrozenClock);
			Bow->StartDraw();
		}
		Bow->SetTestTimeSeconds(FrozenClock + Definition->FullDrawSeconds * Fraction);
	}

	/** Kate on the sidewalk 7 m in front of the heavy (Distance), both facing each other. False with no heavy or no ground. */
	static bool FaceOff(UWorld* World, AHawkeyeCharacter* Kate, AThugCharacter* Thug, float Distance)
	{
		const FVector Forward = Thug->GetActorForwardVector().GetSafeNormal2D();
		FVector Feet;
		if (!Ground(World, Thug->GetActorLocation() + Forward * Distance, Thug->GetActorLocation().Z + 200.f, { Kate, Thug }, Feet))
		{
			return false;
		}
		Stand(Kate, Feet, (-Forward).Rotation().Yaw);
		Thug->SetActorRotation(FRotator(0.f, Forward.Rotation().Yaw, 0.f));
		return true;
	}

	/** On the find_arrow roof: a spot where Shooter's eyes see Kate's chest standing but not her crouched. */
	static bool FindHoldSpot(UWorld* World, AHawkeyeCharacter* Kate, const AThugCharacter* Shooter, FVector& Out)
	{
		AActor* Beacon = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(FName(TEXT("CityBeacon"))) && It->Tags.Contains(FName(TEXT("objective:find_arrow"))))
			{
				Beacon = *It;
				break;
			}
		}
		FVector RoofPoint;
		AActor* Roof = nullptr;
		if (!Beacon || !Ground(World, Beacon->GetActorLocation(), Beacon->GetActorLocation().Z + 300.f, { Kate }, RoofPoint, &Roof))
		{
			return false;
		}
		const FVector Eye = Shooter->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatShotHold), false, Shooter);
		Params.AddIgnoredActor(Kate);
		float Best = BIG_NUMBER;
		for (float X = -1200.f; X <= 1200.f; X += 50.f)
		{
			for (float Y = -1200.f; Y <= 1200.f; Y += 50.f)
			{
				FVector Spot;
				AActor* Hit = nullptr;
				if (!Ground(World, RoofPoint + FVector(X, Y, 0.f), RoofPoint.Z + 300.f, { Kate }, Spot, &Hit) || Hit != Roof
					|| FMath::Abs(Spot.Z - RoofPoint.Z) > 40.f)
				{
					continue;
				}
				const bool bStandingSeen = !World->LineTraceTestByChannel(Eye, Spot + FVector(0.f, 0.f, 126.f), ECC_Visibility, Params);
				const bool bCrouchedHidden = World->LineTraceTestByChannel(Eye, Spot + FVector(0.f, 0.f, 62.f), ECC_Visibility, Params)
					&& World->LineTraceTestByChannel(Eye, Spot + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params);
				// Close to the parapet between them: the nearest such spot to the beacon.
				const float Distance = FVector::Dist2D(Spot, RoofPoint);
				if (bStandingSeen && bCrouchedHidden && Distance < Best)
				{
					Best = Distance;
					Out = Spot;
				}
			}
		}
		return Best < BIG_NUMBER;
	}

	/** Where Point lands on screen, as a fraction of the view from its centre (x right, y down), or none. */
	static FString ScreenOffset(APlayerController* PC, const FVector& Point)
	{
		FVector2D Screen;
		int32 Width = 0;
		int32 Height = 0;
		PC->GetViewportSize(Width, Height);
		if (Width <= 0 || Height <= 0 || !PC->ProjectWorldLocationToScreen(Point, Screen, false))
		{
			return TEXT("off screen");
		}
		return FString::Printf(TEXT("(%+.2f, %+.2f)"), Screen.X / Width - 0.5f, Screen.Y / Height - 0.5f);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeCombatShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeCombatShot::Update()
{
	using namespace HawkeyeCombatShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	if (!World || !Kate || !Bow)
	{
		Test->AddError(TEXT("Combat shots: no world, no Kate or no bow."));
		return true;
	}

	switch (static_cast<EShot>(Shot))
	{
	case EShot::Freeze:
		Saved.Reset();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			Saved.Add({ *It, It->GetActorTransform() });
		}
		SetThinking(World, false);
		HawkeyeFreezePartner(World);
		bKateWasInvulnerable = Kate->GetHealthComponent()->IsInvulnerable();
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Heavy = nullptr;
		for (AThugCharacter* Thug : Tagged(World, StreetGroupTag))
		{
			Heavy = Thug->IsHeavy() ? Thug : Heavy.Get();
		}
		break;

	case EShot::BlockSetup:
	{
		AThugCharacter* H = Heavy.Get();
		if (!H || !FaceOff(World, Kate, H, 700.f))
		{
			Test->AddWarning(TEXT("heavy_block.png: no StreetGroup heavy, or no sidewalk in front of him."));
			break;
		}
		H->UpdateShieldPose();
		Kate->StopAim();
		const FVector Shield = H->GetShieldComponent()->GetComponentLocation() + FVector(0.f, 0.f, 10.f);
		PC->SetControlRotation((Shield - Kate->GetFollowCamera()->GetComponentLocation()).Rotation());
		Bow->SetAimOverride(AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Shield, FVector::ZeroVector, 6000.f,
			World->GetGravityZ()));
		HoldDraw(Bow, 1.f);
		break;
	}

	case EShot::BlockLoose:
		if (Bow->IsDrawing())
		{
			Bow->ReleaseDraw();
		}
		Bow->ClearAimOverride();
		Bow->ClearTestTime();
		break;

	case EShot::BlockView:
	{
		AThugCharacter* H = Heavy.Get();
		if (!H)
		{
			break;
		}
		AArrowProjectile* Stuck = nullptr;
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			if (It->GetOwner() == Kate && It->IsStuck() && It->GetRootComponent()->GetAttachParent() == H->GetShieldComponent())
			{
				Stuck = *It;
			}
		}
		Test->AddInfo(FString::Printf(TEXT("heavy_block.png: %s; the shield has blocked %d, his health %.0f of %.0f."),
			Stuck ? TEXT("her arrow is stuck in the shield") : TEXT("no arrow in the shield"), H->GetBlockCount(),
			H->GetHealthComponent()->GetCurrentHealth(), H->GetHealthComponent()->GetMaxHealth()));
		if (!Stuck)
		{
			Test->AddWarning(TEXT("heavy_block.png: no arrow stuck in the shield."));
		}
		const FVector Shield = H->GetShieldComponent()->GetComponentLocation();
		const FRotator Facing(0.f, H->GetActorRotation().Yaw, 0.f);
		Frame(World, PC, Shield + Facing.RotateVector(FVector(250.f, -190.f, 20.f)), Shield + FVector(0.f, 0.f, 5.f), 50.f);
		break;
	}

	case EShot::BashSetup:
	{
		AThugCharacter* H = Heavy.Get();
		if (!H || !FaceOff(World, Kate, H, 130.f))
		{
			Test->AddWarning(TEXT("heavy_bash.png: no heavy, or no sidewalk in front of him."));
			break;
		}
		const FVector Mid = (H->GetActorLocation() + Kate->GetActorLocation()) * 0.5f;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, H->GetActorForwardVector()).GetSafeNormal2D();
		Frame(World, PC, Mid + Side * 380.f + FVector(0.f, 0.f, 40.f), Mid + FVector(0.f, 0.f, 10.f), 55.f);
		break;
	}

	case EShot::BashStart:
		if (AThugCharacter* H = Heavy.Get())
		{
			H->GetMeleeComponent()->StartAttack(H->GetMeleeAttack(0));
		}
		break;

	case EShot::HoldSetup:
	{
		Unframe(PC, Kate);
		TArray<AThugCharacter*> Archers = Tagged(World, ArcherPairTag);
		AThugCharacter* Pick = nullptr;
		FVector Spot;
		for (AThugCharacter* A : Archers)
		{
			if (!Pick && FindHoldSpot(World, Kate, A, Spot))
			{
				Pick = A;
			}
		}
		Archer = Pick;
		if (!Pick)
		{
			Test->AddWarning(TEXT("archer_hold.png: no spot on the find_arrow roof that an archer sees standing but not crouched."));
			break;
		}
		Stand(Kate, Spot, (Pick->GetActorLocation() - Spot).Rotation().Yaw);
		if (AThugAIController* Brain = BrainOf(Pick))
		{
			Brain->SetThinkingEnabled(true);
			Brain->SetTarget(Kate);
			Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
		}
		// From behind and above her: her crouched at the bottom of the frame, the parapet, him across the gap.
		const FVector To = (Pick->GetActorLocation() - Spot).GetSafeNormal2D();
		const FVector Eye = Spot - To * 380.f + FVector::CrossProduct(FVector::UpVector, To) * 90.f + FVector(0.f, 0.f, 250.f);
		const FVector ToHer = (Spot + FVector(0.f, 0.f, 70.f) - Eye).GetSafeNormal();
		const FVector ToHim = (Pick->GetActorLocation() + FVector(0.f, 0.f, 40.f) - Eye).GetSafeNormal();
		Frame(World, PC, Eye, Eye + (ToHer * 0.45f + ToHim * 0.55f).GetSafeNormal() * 1000.f, 55.f, 250.f);
		Test->AddInfo(FString::Printf(TEXT("archer_hold.png: Kate at %s, %s %.0f cm away."), *Spot.ToCompactString(),
			*Pick->GetName(), FVector::Dist2D(Spot, Pick->GetActorLocation())));
		break;
	}

	case EShot::HoldDone:
		if (AThugAIController* Brain = BrainOf(Archer.Get()))
		{
			Brain->SetThinkingEnabled(false);
		}
		Kate->UnCrouch();
		Unframe(PC, Kate);
		break;

	case EShot::GlyphSetup:
	{
		SetThinking(World, false);
		for (const FSaved& Entry : Saved)
		{
			if (AThugCharacter* Thug = Entry.Thug.Get(); Thug && Thug->ActorHasTag(StreetPairTag) && !Thug->IsLimp())
			{
				Thug->SetActorTransform(Entry.Transform, false, nullptr, ETeleportType::TeleportPhysics);
				Thug->SetAlertState(EThugAlertState::Calm);
			}
		}
		TArray<AThugCharacter*> Pair = Tagged(World, StreetPairTag);
		if (Pair.Num() != 2)
		{
			Test->AddWarning(TEXT("thug_alert_glyph.png: no StreetPair."));
			break;
		}
		const FVector Mid = (Pair[0]->GetActorLocation() + Pair[1]->GetActorLocation()) * 0.5f;
		const FVector Along = (Pair[1]->GetActorLocation() - Pair[0]->GetActorLocation()).GetSafeNormal2D();
		const FVector Out = FVector::CrossProduct(FVector::UpVector, Along);
		FVector Feet;
		// Out toward the kerb and back along the sidewalk, so the two stand apart in her view.
		if (!Ground(World, Mid - Along * 450.f + Out * 300.f, Mid.Z + 300.f, { Kate }, Feet)
			&& !Ground(World, Mid - Along * 450.f - Out * 300.f, Mid.Z + 300.f, { Kate }, Feet))
		{
			Test->AddWarning(TEXT("thug_alert_glyph.png: no sidewalk beside the pair."));
			break;
		}
		const float ToThem = (Mid - Feet).Rotation().Yaw;
		Stand(Kate, Feet, ToThem);
		for (AThugCharacter* Thug : Pair)
		{
			Thug->SetActorRotation(FRotator(0.f, (Feet - Thug->GetActorLocation()).Rotation().Yaw, 0.f));
		}
		PC->SetViewTarget(Kate);
		PC->SetControlRotation(FRotator(-6.f, ToThem, 0.f));
		break;
	}

	case EShot::GlyphTrigger:
	{
		for (AThugCharacter* Thug : Tagged(World, StreetPairTag))
		{
			if (Thug->IsGunner())
			{
				Thug->SetAlertState(EThugAlertState::Suspicious);
				Thug->GetHealthComponent()->ApplyDamage(35.f, Kate);
			}
			else
			{
				Thug->SetAlertState(EThugAlertState::Alerted);
			}
		}
		break;
	}

	case EShot::AimSetup:
	{
		SetThinking(World, false);
		TActorIterator<APlayerStart> Start(World);
		if (!Start)
		{
			Test->AddWarning(TEXT("aim_view_clear.png: no PlayerStart."));
			break;
		}
		const FVector Away = Start->GetActorForwardVector().GetSafeNormal2D();
		const FVector Along = FVector::CrossProduct(FVector::UpVector, Away);
		FVector Feet;
		if (!Ground(World, Start->GetActorLocation() - Away * 400.f, Start->GetActorLocation().Z + 3000.f, { Kate }, Feet))
		{
			Test->AddWarning(TEXT("aim_view_clear.png: no street by the PlayerStart."));
			break;
		}
		const float Yaw = Along.Rotation().Yaw;
		Stand(Kate, Feet, Yaw);
		// Clint out of her view: the shot is about her own arm.
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			if (AHawkeyeCharacter* Clint = It->GetPartner())
			{
				Stand(Clint, Feet - Along * 1500.f, Yaw);
			}
		}
		PC->SetViewTarget(Kate);
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		Kate->StartAim();
		Bow->SetAimOverride(Kate->GetActorLocation() + Along * 2000.f + FVector(0.f, 0.f, 40.f));
		HoldDraw(Bow, 1.f);
		break;
	}

	case EShot::AimDone:
		Bow->CancelDraw();
		Bow->ClearAimOverride();
		Bow->ClearTestTime();
		Kate->StopAim();
		break;

	case EShot::KateArrowSetup:
	{
		// Trickshot's black and purple arrow if an archer carries one, else one of hers.
		UArrowDefinition* Definition = nullptr;
		for (AThugCharacter* A : Tagged(World, ArcherPairTag))
		{
			Definition = (!Definition && A->GetBowComponent()) ? A->GetBowComponent()->OwnArrow.Get() : Definition;
		}
		if (!Definition && Kate->GetInventoryComponent())
		{
			Definition = Kate->GetInventoryComponent()->GetArrowSlot(1).Arrow;
		}
		const FVector Chest = Kate->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const FVector From = Chest + Kate->GetActorRotation().RotateVector(FVector(430.f, -250.f, 60.f));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AArrowProjectile* Arrow = World->SpawnActor<AArrowProjectile>(AArrowProjectile::StaticClass(), From,
			(Chest - From).Rotation(), Params);
		KateArrow = Arrow;
		if (!Arrow || !Definition)
		{
			Test->AddWarning(TEXT("arrow_in_kate.png: no arrow to fly at her."));
			break;
		}
		Arrow->InitArrow(Definition, nullptr, 0.f, nullptr, nullptr);
		Arrow->LaunchWithVelocity((Chest - From).GetSafeNormal() * 5000.f);
		break;
	}

	case EShot::KateArrowView:
	{
		AArrowProjectile* Arrow = KateArrow.Get();
		if (!Arrow || !Arrow->IsStuck() || Arrow->GetStuckInActor() != Kate)
		{
			Test->AddWarning(TEXT("arrow_in_kate.png: the arrow is not stuck in Kate."));
			break;
		}
		const USceneComponent* Parent = Arrow->GetRootComponent()->GetAttachParent();
		const FName Bone = Arrow->GetRootComponent()->GetAttachSocketName();
		const FVector Tip = Arrow->GetActorLocation();
		Test->AddInfo(FString::Printf(TEXT("arrow_in_kate.png: stuck on %s of %s, tip %.1f cm outside her body's shapes, %.1f cm from the capsule axis (radius %.0f)."),
			*Bone.ToString(), Parent == Kate->GetBodyMesh() ? TEXT("her mesh") : *GetNameSafe(Parent), DistanceToBody(Kate->GetBodyMesh(), Tip),
			FVector::Dist2D(Tip, Kate->GetActorLocation()), Kate->GetCapsuleComponent()->GetScaledCapsuleRadius()));
		// From her front-left, level with the arrow, close enough to see where it goes in.
		const FVector Side = Kate->GetActorRotation().RotateVector(FVector(160.f, -150.f, 15.f));
		Frame(World, PC, Tip + Side, Tip - Arrow->GetActorForwardVector() * 25.f, 45.f, 300.f);
		break;
	}

	case EShot::Cleanup:
		SetThinking(World, false);
		Unframe(PC, Kate);
		if (AArrowProjectile* Arrow = KateArrow.Get())
		{
			Arrow->Destroy();
		}
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		if (APointLight* Light = FillLight.Get())
		{
			Light->Destroy();
		}
		for (const FSaved& Entry : Saved)
		{
			AThugCharacter* Thug = Entry.Thug.Get();
			if (!Thug)
			{
				continue;
			}
			Thug->SetAlertState(EThugAlertState::Calm);
			Thug->SetTelegraphGlint(false);
			if (Thug->GetBowComponent())
			{
				Thug->GetBowComponent()->CancelDraw();
			}
			if (Thug->GetHealthComponent()->IsAlive())
			{
				Thug->GetHealthComponent()->Heal(1000.f);
			}
			if (!Thug->IsLimp())
			{
				Thug->SetActorTransform(Entry.Transform, false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (AThugAIController* Brain = BrainOf(Thug))
			{
				Brain->SetTarget(nullptr);
			}
		}
		Kate->UnCrouch();
		Kate->StopAim();
		Kate->GetHealthComponent()->SetInvulnerable(bKateWasInvulnerable);
		break;
	}
	return true;
}

/** Waits (up to Timeout) for the moment a shot is about, then reports it. */
class FHawkeyeCombatWait : public IAutomationLatentCommand
{
public:
	FHawkeyeCombatWait(FAutomationTestBase* InTest, FString InLabel, float InTimeout)
		: Test(InTest), Label(MoveTemp(InLabel)), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeCombatShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !Kate)
		{
			return true;
		}
		const double Now = World->GetTimeSeconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		const bool bTimedOut = Now - Start >= Timeout;
		if (Label == TEXT("archer_hold.png"))
		{
			return WaitForHold(Kate, Now, bTimedOut);
		}
		if (Label == TEXT("heavy_bash.png"))
		{
			const AThugCharacter* H = Heavy.Get();
			const UMeleeComponent* Melee = H ? H->GetMeleeComponent() : nullptr;
			Test->AddInfo(FString::Printf(TEXT("heavy_bash.png: winding up %d, %.2f s of the %.1f s telegraph left, mask glow %s."),
				Melee && Melee->IsWindingUp() ? 1 : 0, Melee ? Melee->GetPhaseRemaining() : 0.f,
				H ? H->ShieldBashAttack.WindupSeconds : 0.f, Melee && Melee->IsWindingUp() ? TEXT("on") : TEXT("off")));
			return true;
		}
		if (Label == TEXT("thug_alert_glyph.png"))
		{
			FString States;
			for (const AThugCharacter* Thug : Tagged(World, StreetPairTag))
			{
				const TCHAR Glyph = Thug->GetAlertGlyph();
				States += FString::Printf(TEXT("%s glyph '%s' bar %.2f; "), *Thug->GetName(), Glyph ? *FString::Chr(Glyph) : TEXT(""),
					Thug->GetHealthBarAlpha(FVector::Dist(Thug->GetActorLocation(), Kate->GetActorLocation())));
			}
			Test->AddInfo(TEXT("thug_alert_glyph.png: ") + States);
			return true;
		}
		if (Label == TEXT("aim_view_clear.png"))
		{
			const USkeletalMeshComponent* Body = Kate->GetMesh();
			const USpringArmComponent* Arm = Kate->FindComponentByClass<USpringArmComponent>();
			const UBowComponent* Bow = Kate->GetBowComponent();
			Test->AddInfo(FString::Printf(TEXT("aim_view_clear.png: arm %.0f, socket %s, FOV %.0f, draw %.2f; from the screen centre: ")
				TEXT("hand_r %s, elbow_r %s, upperarm_r %s, head %s, bow hand %s."),
				Arm ? Arm->TargetArmLength : 0.f, Arm ? *Arm->SocketOffset.ToCompactString() : TEXT("-"), Kate->GetCurrentFOV(),
				Bow ? Bow->GetDrawFraction() : 0.f,
				Body ? *ScreenOffset(PC, Body->GetSocketLocation(TEXT("hand_r"))) : TEXT("-"),
				Body ? *ScreenOffset(PC, Body->GetSocketLocation(TEXT("lowerarm_r"))) : TEXT("-"),
				Body ? *ScreenOffset(PC, Body->GetSocketLocation(TEXT("upperarm_r"))) : TEXT("-"),
				Body ? *ScreenOffset(PC, Body->GetSocketLocation(TEXT("head"))) : TEXT("-"),
				Body ? *ScreenOffset(PC, Body->GetSocketLocation(TEXT("hand_l"))) : TEXT("-")));
			return true;
		}
		return true;
	}

private:
	bool WaitForHold(AHawkeyeCharacter* Kate, double Now, bool bTimedOut)
	{
		using namespace HawkeyeCombatShots;
		AThugCharacter* A = Archer.Get();
		const AThugAIController* Brain = BrainOf(A);
		const UBowComponent* Bow = A ? A->GetBowComponent() : nullptr;
		if (!A || !Brain || !Bow)
		{
			return true;
		}
		// Standing until he is well into a draw, then down behind the parapet.
		if (!Kate->bIsCrouched && Bow->IsDrawing() && Bow->GetDrawElapsed() >= 0.6f)
		{
			Kate->Crouch();
		}
		if (Brain->IsHoldingDraw() && HoldSeen < 0.0)
		{
			HoldSeen = Now;
		}
		const bool bReady = HoldSeen >= 0.0 && Now - HoldSeen >= 0.6;
		if (!bReady && !bTimedOut)
		{
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("archer_hold.png: after %.1f s, Kate crouched %d, %s holding %d (%.1f s into the draw), glint %d."),
			Now - Start, Kate->bIsCrouched ? 1 : 0, *A->GetName(), Brain->IsHoldingDraw() ? 1 : 0, Bow->GetDrawElapsed(),
			A->IsTelegraphGlintOn() ? 1 : 0));
		if (!bReady)
		{
			Test->AddWarning(TEXT("archer_hold.png: the archer never held a draw inside the timeout."));
		}
		return true;
	}

	FAutomationTestBase* Test;
	FString Label;
	float Timeout;
	double Start = -1.0;
	double HoldSeen = -1.0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeCombatTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeCombatTakeShot::Update()
{
	const FString FullPath = HawkeyeCombatShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

void HawkeyeAddCombatShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeCombatShots::EShot;
	auto Shot = [Test](EShot Which, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCombatShot(Test, static_cast<uint8>(Which)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};
	auto Wait = [Test](const TCHAR* Label, float Timeout)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCombatWait(Test, FString(Label), Timeout));
	};
	auto Take = [Test](const TCHAR* File)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCombatTakeShot(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	};

	Shot(EShot::Freeze, 0.3f);
	Shot(EShot::BlockSetup, 1.2f);
	Shot(EShot::BlockLoose, 0.8f);
	Shot(EShot::BlockView, 0.5f);
	Take(TEXT("heavy_block.png"));

	Shot(EShot::BashSetup, 0.8f);
	Shot(EShot::BashStart, 0.45f);
	Wait(TEXT("heavy_bash.png"), 0.f);
	Take(TEXT("heavy_bash.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));

	Shot(EShot::HoldSetup, 0.2f);
	Wait(TEXT("archer_hold.png"), 10.f);
	Take(TEXT("archer_hold.png"));
	Shot(EShot::HoldDone, 0.5f);

	Shot(EShot::GlyphSetup, 1.f);
	Shot(EShot::GlyphTrigger, 0.2f);
	Wait(TEXT("thug_alert_glyph.png"), 0.f);
	Take(TEXT("thug_alert_glyph.png"));

	Shot(EShot::AimSetup, 1.5f);
	Wait(TEXT("aim_view_clear.png"), 0.f);
	Take(TEXT("aim_view_clear.png"));
	Shot(EShot::AimDone, 0.3f);

	Shot(EShot::KateArrowSetup, 0.6f);
	Shot(EShot::KateArrowView, 0.4f);
	Take(TEXT("arrow_in_kate.png"));

	Shot(EShot::Cleanup, 0.5f);
}

/** The fair-fight shots on their own, for tuning without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotCombat, "Hawkeye.Screenshot.Combat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotCombat::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the combat screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddCombatShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
