#include "Tests/EnemyScreenshots.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The enemy shots, run near the end of Hawkeye.Screenshot.Kate (every placed thug frozen but the
 * ones in the shot, Kate invulnerable throughout):
 *
 *   squad_alert.png    the StreetPair walking away from Kate 16 m down the sidewalk; the bat thug is
 *                      alerted on her, and 1.5 s later the squad alert turns the gunner too: both
 *                      facing her, one rushing, one coming to look
 *   gunner_cover.png   the StreetPair gunner behind the corner of a building at an end of the block
 *                      across East 7th Street (not the safehouse's), Kate out on the avenue, seen from 3 m
 *                      out along his line of fire: his burst into the wall, then cover (EQS), then a peek past the corner
 *                      with the pistol raised and the muzzle glint on
 *   archer_draw.png    Kate standing on the find_arrow roof, aiming at the ArcherPair archer nearest a
 *                      full draw: the purple glint on his arrow tip on the far roof
 *   archer_arrow.png   his arrow (the real bow, aimed at the roof 2.5 m beside her) stuck in the roof
 *                      next to Kate: black shaft, purple vanes
 *   archer_pickup.png  Kate walked onto it: picked up as a standard arrow, "Trickshot's arrow" toast
 */
namespace HawkeyeEnemyShots
{
	static const FName StreetPairTag(TEXT("StreetPair"));
	static const FName ArcherPairTag(TEXT("ArcherPair"));
	static const FName BuildingTag(TEXT("CityBuilding"));

	enum class EShot : uint8
	{
		Freeze,
		SquadSetup,
		GunnerSetup,
		ArcherSetup,
		ArrowFreeze,
		ArrowSetup,
		ArrowRelease,
		ArrowView,
		Pickup,
		Cleanup,
	};

	struct FSaved
	{
		TWeakObjectPtr<AThugCharacter> Thug;
		FTransform Transform;
	};

	/** Where every placed thug stood before the pass, so Cleanup can put them back. */
	static TArray<FSaved> Saved;
	static TWeakObjectPtr<AThugCharacter> Gunner;
	static TWeakObjectPtr<AThugCharacter> Archer;
	static FVector ArrowSpot = FVector::ZeroVector;
	/** A camera just behind Kate's head for the gunner shot: the spring arm catches the shop signs. */
	static TWeakObjectPtr<ACameraActor> ShotCamera;
	static bool bKateWasInvulnerable = false;

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

	static bool Ground(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& Out, AActor** OutActor = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShotGround), false, Ignore);
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
	}

	/** Points the camera from where the lens is at Point. */
	static void LookAt(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Point)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Point - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
	}

	static void Alert(AThugCharacter* Thug, AActor* Target)
	{
		if (AThugAIController* Brain = BrainOf(Thug))
		{
			Brain->SetThinkingEnabled(true);
			Brain->SetTarget(Target);
			Brain->ReportStimulus(EStimulusKind::Hearing, Target->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
		}
	}

	/**
	 * The corner at one end of the block across the street from the PlayerStart: the start faces away
	 * from the park, across East 7th Street, at the facades; stepping along the street, the last step
	 * whose trace toward the facades still meets a building. Out: the corner on the facade line, the
	 * step direction (past the corner, into the avenue) and the side (toward the facade).
	 */
	static bool FindCorner(UWorld* World, FVector& OutCorner, FVector& OutToPark, FVector& OutSide)
	{
		TActorIterator<APlayerStart> Start(World);
		if (!Start)
		{
			return false;
		}
		const FVector Side = Start->GetActorForwardVector().GetSafeNormal2D();
		const FVector Along = FVector::CrossProduct(FVector::UpVector, Side);
		const FVector Origin = Start->GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShotCorner), false);
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector StepDir = Along * Sign;
			bool bHadFacade = false;
			FVector LastHit = FVector::ZeroVector;
			for (float Step = 0.f; Step <= 8000.f; Step += 50.f)
			{
				const FVector From = Origin + StepDir * Step + FVector(0.f, 0.f, 50.f);
				FHitResult Hit;
				const bool bFacade = World->LineTraceSingleByChannel(Hit, From, From + Side * 2500.f, ECC_Visibility, Params)
					&& Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag);
				if (bFacade)
				{
					bHadFacade = true;
					LastHit = Hit.ImpactPoint;
				}
				else if (bHadFacade)
				{
					// Not the safehouse's corner: its sign board stands right where he would peek.
					bool bSafehouse = false;
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						bSafehouse = bSafehouse || (It->Tags.Contains(FName(TEXT("CitySafehouse")))
							&& FVector::Dist2D(It->GetActorLocation(), LastHit) < 1000.f);
					}
					if (bSafehouse)
					{
						break;
					}
					OutCorner = LastHit;
					OutToPark = StepDir;
					OutSide = Side;
					return true;
				}
			}
		}
		return false;
	}

	/** The nearest point 2 to 3 m from Kate on her roof that the archer's eyes see. */
	static bool FindArrowSpot(UWorld* World, const AHawkeyeCharacter* Kate, const AThugCharacter* Shooter, FVector& Out)
	{
		const FVector Feet = Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		AActor* Roof = nullptr;
		FVector Under;
		Ground(World, Feet, Feet.Z + 100.f, Kate, Under, &Roof);
		// Beside and behind her from him first: flat lines, the likeliest to clear his parapet.
		const float Behind = (Feet - Shooter->GetActorLocation()).Rotation().Yaw + 60.f;
		for (const float Radius : { 250.f, 300.f, 220.f })
		{
			for (int32 Step = 0; Step < 16; ++Step)
			{
				const float Turn = (Step % 2 == 0 ? 1.f : -1.f) * ((Step + 1) / 2) * 22.5f;
				const FVector Probe = Feet + FRotator(0.f, Behind + Turn, 0.f).Vector() * Radius;
				FVector Spot;
				AActor* Hit = nullptr;
				if (!Ground(World, Probe, Feet.Z + 150.f, Kate, Spot, &Hit) || Hit != Roof || FMath::Abs(Spot.Z - Feet.Z) > 30.f)
				{
					continue;
				}
				FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShotArrow), false, Shooter);
				Params.AddIgnoredActor(Kate);
				const FVector Target = Spot + FVector(0.f, 0.f, 5.f);
				const UBowComponent* Bow = Shooter->GetBowComponent();
				const FVector From = Bow ? Bow->GetArrowSpawnLocation() : Shooter->GetActorLocation() + FVector(0.f, 0.f, 60.f);
				// Clear of the roof and wide of Kate herself, or it sticks in her instead.
				if (!World->LineTraceTestByChannel(From, Target, ECC_Visibility, Params)
					&& FMath::PointDistToSegment(Kate->GetActorLocation(), From, Target) > 90.f)
				{
					Out = Target;
					return true;
				}
			}
		}
		return false;
	}

	/** A spot on the find_arrow roof, near the beacon, where Kate standing sees both archers. */
	static bool FindDuelSpot(UWorld* World, AHawkeyeCharacter* Kate, const TArray<AThugCharacter*>& Archers, FVector& Out)
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
		if (!Beacon || !Ground(World, Beacon->GetActorLocation(), Beacon->GetActorLocation().Z + 300.f, Kate, RoofPoint, &Roof))
		{
			return false;
		}
		float Best = BIG_NUMBER;
		for (float X = -800.f; X <= 800.f; X += 50.f)
		{
			for (float Y = -800.f; Y <= 800.f; Y += 50.f)
			{
				FVector Spot;
				AActor* Hit = nullptr;
				if (!Ground(World, RoofPoint + FVector(X, Y, 0.f), RoofPoint.Z + 300.f, Kate, Spot, &Hit) || Hit != Roof
					|| FMath::Abs(Spot.Z - RoofPoint.Z) > 40.f)
				{
					continue;
				}
				bool bAll = true;
				for (const AThugCharacter* A : Archers)
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShotDuel), false, A);
					Params.AddIgnoredActor(Kate);
					bAll = bAll && !World->LineTraceTestByChannel(A->GetActorLocation() + FVector(0.f, 0.f, 60.f),
						Spot + FVector(0.f, 0.f, 130.f), ECC_Visibility, Params);
				}
				const float Distance = FVector::Dist2D(Spot, RoofPoint);
				if (bAll && Distance < Best && Distance > 150.f)
				{
					Best = Distance;
					Out = Spot;
				}
			}
		}
		return Best < BIG_NUMBER;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeEnemyShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeEnemyShot::Update()
{
	// Inside each body, not at file scope: the unity build puts this file beside the Kate pass's own
	// FindWorld and EShot.
	using namespace HawkeyeEnemyShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		Test->AddError(TEXT("Enemy shots: no world or no Kate."));
		return true;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();

	switch (static_cast<EShot>(Shot))
	{
	case EShot::Freeze:
	{
		Saved.Reset();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			Saved.Add({ *It, It->GetActorTransform() });
		}
		SetThinking(World, false);
		HawkeyeFreezePartner(World);
		bKateWasInvulnerable = KateHealth->IsInvulnerable();
		KateHealth->SetInvulnerable(true);
		break;
	}

	case EShot::SquadSetup:
	{
		TArray<AThugCharacter*> Pair = Tagged(World, StreetPairTag);
		AThugCharacter* Bat = nullptr;
		AThugCharacter* Gun = nullptr;
		for (AThugCharacter* Thug : Pair)
		{
			(Thug->IsGunner() ? Gun : Bat) = Thug;
		}
		if (!Bat || !Gun)
		{
			Test->AddWarning(TEXT("squad_alert.png: no bat and gunner in the StreetPair."));
			break;
		}
		// Side by side on the sidewalk, facing away from her, Kate 16 m back and a little out.
		const FVector Along = (Bat->GetActorLocation() - Gun->GetActorLocation()).GetSafeNormal2D();
		const FVector Out = FVector::CrossProduct(FVector::UpVector, Along);
		FVector Feet;
		if (!Ground(World, Bat->GetActorLocation() - Along * 1600.f + Out * 300.f, Bat->GetActorLocation().Z + 400.f, Kate, Feet))
		{
			Test->AddWarning(TEXT("squad_alert.png: no ground behind the StreetPair."));
			break;
		}
		const float AwayYaw = Along.Rotation().Yaw;
		Bat->SetActorRotation(FRotator(0.f, AwayYaw, 0.f));
		// In line on the sidewalk, as they walk: sideways may be into the park wall.
		Gun->SetActorLocation(Bat->GetActorLocation() - Along * 200.f);
		Gun->SetActorRotation(FRotator(0.f, AwayYaw, 0.f));
		Stand(Kate, Feet, AwayYaw);
		PC->SetViewTarget(Kate);
		LookAt(PC, Kate, (Bat->GetActorLocation() + Gun->GetActorLocation()) * 0.5f);
		// The gunner is calm and thinking; only the bat thug has seen her.
		if (AThugAIController* GunBrain = BrainOf(Gun))
		{
			GunBrain->SetThinkingEnabled(true);
		}
		Alert(Bat, Kate);
		Test->AddInfo(FString::Printf(TEXT("squad_alert.png: bat thug alerted at %.0f cm, gunner %.0f cm from him."),
			FVector::Dist2D(Bat->GetActorLocation(), Kate->GetActorLocation()), FVector::Dist2D(Bat->GetActorLocation(), Gun->GetActorLocation())));
		break;
	}

	case EShot::GunnerSetup:
	{
		SetThinking(World, false);
		for (const FSaved& Entry : Saved)
		{
			if (AThugCharacter* Thug = Entry.Thug.Get(); Thug && Thug->ActorHasTag(StreetPairTag))
			{
				Thug->SetAlertState(EThugAlertState::Calm);
				Thug->SetActorTransform(Entry.Transform, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		TArray<AThugCharacter*> Pair = Tagged(World, StreetPairTag);
		Gunner = nullptr;
		for (AThugCharacter* Thug : Pair)
		{
			if (Thug->IsGunner())
			{
				Gunner = Thug;
			}
		}
		FVector Corner, ToPark, Side;
		if (!Gunner.IsValid() || !FindCorner(World, Corner, ToPark, Side))
		{
			Test->AddWarning(TEXT("gunner_cover.png: no gunner or no street corner by the PlayerStart."));
			break;
		}
		// He is round the corner on the side street; she is out on the avenue, where the building hides
		// him in cover but the spot he will peek from is in clear view of her and her camera.
		FVector GunFeet, KateFeet;
		// From just over head height: from higher up the trace lands on scaffold decks and awnings.
		Ground(World, Corner - Side * 90.f - ToPark * 250.f, Corner.Z + 30.f, Gunner.Get(), GunFeet);
		const FVector PeekGuess = Corner - Side * 90.f + ToPark * 120.f + FVector(0.f, 0.f, 100.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShotGunnerSpot), false, Gunner.Get());
		Params.AddIgnoredActor(Kate);
		bool bFound = false;
		for (const float Out : { 600.f, 800.f, 1000.f, 400.f, 1200.f, 300.f })
		{
			for (const float Past : { 400.f, 600.f, 300.f, 800.f, 200.f, 1000.f })
			{
				FVector Feet;
				if (!Ground(World, Corner + ToPark * Past + Side * Out, Corner.Z + 30.f, Kate, Feet) || FMath::Abs(Feet.Z - GunFeet.Z) > 60.f)
				{
					continue;
				}
				const FVector Eye = Feet + FVector(0.f, 0.f, 160.f);
				const bool bHidden = World->LineTraceTestByChannel(Eye, GunFeet + FVector(0.f, 0.f, 120.f), ECC_Visibility, Params);
				const bool bSeesPeek = !World->LineTraceTestByChannel(Eye, PeekGuess, ECC_Visibility, Params);
				if (bHidden && bSeesPeek && FVector::Dist2D(Feet, GunFeet) < 1150.f)
				{
					KateFeet = Feet;
					bFound = true;
					break;
				}
			}
			if (bFound)
			{
				break;
			}
		}
		if (!bFound)
		{
			Ground(World, Corner + ToPark * 500.f + Side * 500.f, Corner.Z + 30.f, Kate, KateFeet);
			Test->AddWarning(TEXT("gunner_cover.png: no spot on the avenue that both hides his cover and sees his peek."));
		}
		Stand(Gunner.Get(), GunFeet, (KateFeet - GunFeet).Rotation().Yaw);
		Stand(Kate, KateFeet, (PeekGuess - KateFeet).Rotation().Yaw);
		PC->SetViewTarget(Kate);
		LookAt(PC, Kate, PeekGuess);
		{
			const FVector ToPeek = (PeekGuess - KateFeet).GetSafeNormal2D();
			// Her own eye line: the gunner only peeks to where he has a line on her, so from here he shows.
			const FVector Eye = KateFeet + FVector(0.f, 0.f, 150.f) + ToPeek * 40.f;
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(Eye, (PeekGuess - Eye).Rotation(), SpawnParams);
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(55.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				ShotCamera = Camera;
				PC->SetViewTarget(Camera);
			}
		}
		Alert(Gunner.Get(), Kate);
		Test->AddInfo(FString::Printf(TEXT("gunner_cover.png: corner %s, gunner %.0f cm from Kate behind it."), *Corner.ToCompactString(),
			FVector::Dist2D(GunFeet, KateFeet)));
		break;
	}

	case EShot::ArcherSetup:
	{
		SetThinking(World, false);
		PC->SetViewTarget(Kate);
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		if (Gunner.IsValid())
		{
			Gunner->SetAlertState(EThugAlertState::Calm);
		}
		TArray<AThugCharacter*> Archers = Tagged(World, ArcherPairTag);
		FVector Spot;
		if (Archers.Num() != 2 || !FindDuelSpot(World, Kate, Archers, Spot))
		{
			Test->AddWarning(TEXT("archer_draw.png: no ArcherPair or no spot on the find_arrow roof that sees both."));
			break;
		}
		const FVector Mid = (Archers[0]->GetActorLocation() + Archers[1]->GetActorLocation()) * 0.5f;
		Stand(Kate, Spot, (Mid - Spot).Rotation().Yaw);
		PC->SetViewTarget(Kate);
		LookAt(PC, Kate, Mid);
		for (AThugCharacter* A : Archers)
		{
			if (AThugAIController* Brain = BrainOf(A))
			{
				Brain->SetThinkingEnabled(true);
			}
		}
		Test->AddInfo(FString::Printf(TEXT("archer_draw.png: Kate on the find_arrow roof, archers %.0f and %.0f cm away."),
			FVector::Dist2D(Archers[0]->GetActorLocation(), Spot), FVector::Dist2D(Archers[1]->GetActorLocation(), Spot)));
		break;
	}

	case EShot::ArrowFreeze:
		// Frozen first: the brain's switch to Stunned a frame later would let a draw down.
		SetThinking(World, false);
		Kate->StopAim();
		break;

	case EShot::ArrowSetup:
	{
		AThugCharacter* Shooter = Archer.Get();
		UBowComponent* Bow = Shooter ? Shooter->GetBowComponent() : nullptr;
		if (!Bow || !FindArrowSpot(World, Kate, Shooter, ArrowSpot))
		{
			Test->AddWarning(TEXT("archer_arrow.png: no archer or no roof beside Kate he can see."));
			break;
		}
		Bow->CancelDraw();
		Bow->SetAimOverride(AThugAIController::ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), ArrowSpot, FVector::ZeroVector,
			5000.f, World->GetGravityZ()));
		Bow->StartDraw();
		Shooter->SetTelegraphGlint(true);
		break;
	}

	case EShot::ArrowRelease:
		if (AThugCharacter* Shooter = Archer.Get())
		{
			Shooter->SetTelegraphGlint(false);
			if (UBowComponent* Bow = Shooter->GetBowComponent())
			{
				Bow->ReleaseDraw();
				Bow->ClearAimOverride();
			}
		}
		break;

	case EShot::ArrowView:
	{
		AArrowProjectile* Landed = nullptr;
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			if (It->GetArrowDefinition() && It->GetArrowDefinition()->RecoverAs && It->GetOwner() == Archer.Get())
			{
				Landed = *It;
			}
		}
		if (Landed)
		{
			ArrowSpot = Landed->GetActorLocation();
		}
		Test->AddInfo(FString::Printf(TEXT("archer_arrow.png: his arrow %s%s at %s, %.0f cm from Kate%s."),
			Landed ? TEXT("found") : TEXT("not found"), Landed && Landed->IsStuck() ? TEXT(", stuck") : TEXT(""),
			*ArrowSpot.ToCompactString(), FVector::Dist2D(ArrowSpot, Kate->GetActorLocation()),
			Landed && Landed->GetStuckInActor() ? *FString::Printf(TEXT(" in %s"), *Landed->GetStuckInActor()->GetClass()->GetName()) : TEXT("")));
		// Face it first, so the camera swings round behind her instead of looking down past her head.
		const FVector To = (ArrowSpot - Kate->GetActorLocation()).GetSafeNormal2D();
		Kate->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
		PC->SetControlRotation(FRotator(-22.f, To.Rotation().Yaw - 25.f, 0.f));
		break;
	}

	case EShot::Pickup:
	{
		AArrowProjectile* Stuck = nullptr;
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			if (It->IsStuck() && It->GetArrowDefinition() && It->GetArrowDefinition()->RecoverAs
				&& FVector::Dist(It->GetActorLocation(), ArrowSpot) < 100.f)
			{
				Stuck = *It;
			}
		}
		if (!Stuck)
		{
			Test->AddWarning(TEXT("archer_pickup.png: no Trickshot arrow stuck near Kate to pick up."));
			break;
		}
		const FVector Feet = Stuck->GetActorLocation() - (Stuck->GetActorLocation() - Kate->GetActorLocation()).GetSafeNormal2D() * 100.f;
		FVector Ground2;
		if (Ground(World, Feet, Feet.Z + 200.f, Kate, Ground2))
		{
			Stand(Kate, Ground2, Kate->GetActorRotation().Yaw);
		}
		break;
	}

	case EShot::Cleanup:
	{
		SetThinking(World, false);
		PC->SetViewTarget(Kate);
		for (const FSaved& Entry : Saved)
		{
			if (AThugCharacter* Thug = Entry.Thug.Get())
			{
				Thug->SetAlertState(EThugAlertState::Calm);
				Thug->SetTelegraphGlint(false);
				Thug->SetWeaponRaised(false, FVector::ZeroVector);
				if (Thug->GetBowComponent())
				{
					Thug->GetBowComponent()->CancelDraw();
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
		}
		Kate->StopAim();
		KateHealth->SetInvulnerable(bKateWasInvulnerable);
		break;
	}
	}
	return true;
}

/** Waits (up to TimeoutSeconds) for the moment a shot is about, then reports it. */
class FHawkeyeEnemyWait : public IAutomationLatentCommand
{
public:
	FHawkeyeEnemyWait(FAutomationTestBase* InTest, FString InLabel, float InTimeout)
		: Test(InTest), Label(MoveTemp(InLabel)), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeEnemyShots;
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
		if (Label == TEXT("gunner_cover.png"))
		{
			AThugCharacter* Gun = Gunner.Get();
			const AThugAIController* Brain = BrainOf(Gun);
			if (Gun)
			{
				LookAt(PC, Kate, Gun->GetActorLocation() + FVector(0.f, 0.f, 40.f));
				// On his own line of fire, 3 m out toward her and a little to the side: wherever he peeks
				// from, he has a line on her, so this view of him is open, the corner beside him.
				if (ACameraActor* Camera = ShotCamera.Get())
				{
					const FVector ToKate = (Kate->GetActorLocation() - Gun->GetActorLocation()).GetSafeNormal2D();
					const FVector Eye = Gun->GetActorLocation() + ToKate * 320.f
						+ FVector::CrossProduct(FVector::UpVector, ToKate) * 60.f + FVector(0.f, 0.f, 40.f);
					Camera->SetActorLocationAndRotation(Eye, (Gun->GetActorLocation() + FVector(0.f, 0.f, 30.f) - Eye).Rotation());
				}
			}
			// She is loosing arrows at his corner: he keeps hearing her while he hides.
			if (Now - LastPing > 1.0 && Brain && Gun)
			{
				LastPing = Now;
				const_cast<AThugAIController*>(Brain)->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 1.f);
			}
			const bool bReady = Brain && Brain->GetGunnerPhase() == EGunnerPhase::Peeking && Brain->IsTelegraphing()
				&& Now - Start > 1.0;
			if (!bReady && !bTimedOut)
			{
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("gunner_cover.png: after %.1f s, phase %s, telegraph %d, glint %d, pistol raised %d, cover %d at %s, %.0f cm from Kate."),
				Now - Start, Brain ? *UEnum::GetValueAsString(Brain->GetGunnerPhase()) : TEXT("-"), Brain && Brain->IsTelegraphing() ? 1 : 0,
				Gun && Gun->IsTelegraphGlintOn() ? 1 : 0, Gun && Gun->IsWeaponRaised() ? 1 : 0, Brain && Brain->HasCoverPoint() ? 1 : 0,
				Brain ? *Brain->GetCoverPoint().ToCompactString() : TEXT("-"), Gun ? FVector::Dist2D(Gun->GetActorLocation(), Kate->GetActorLocation()) : 0.f));
			if (!bReady)
			{
				Test->AddWarning(TEXT("gunner_cover.png: the gunner never peeked with a telegraph inside the timeout."));
			}
			return true;
		}
		if (Label == TEXT("archer_draw.png"))
		{
			AThugCharacter* Best = nullptr;
			float Draw = -1.f;
			for (AThugCharacter* A : Tagged(World, ArcherPairTag))
			{
				const UBowComponent* Bow = A->GetBowComponent();
				const float Elapsed = Bow && Bow->IsDrawing() ? Bow->GetDrawElapsed() : -1.f;
				if (Elapsed > Draw)
				{
					Draw = Elapsed;
					Best = A;
				}
			}
			if (Best)
			{
				Archer = Best;
				if (!Kate->IsAiming())
				{
					Kate->StartAim();
				}
				LookAt(PC, Kate, Best->GetActorLocation() + FVector(0.f, 0.f, 50.f));
			}
			const bool bReady = Draw >= 0.75f;
			if (!bReady && !bTimedOut)
			{
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("archer_draw.png: %s %.2f s into his 1.2 s draw, glint %d, %.0f cm from Kate."),
				*GetNameSafe(Best), Draw, Best && Best->IsTelegraphGlintOn() ? 1 : 0,
				Best ? FVector::Dist2D(Best->GetActorLocation(), Kate->GetActorLocation()) : 0.f));
			if (!bReady)
			{
				Test->AddWarning(TEXT("archer_draw.png: no archer was drawing on Kate inside the timeout."));
			}
			if (!Archer.IsValid())
			{
				TArray<AThugCharacter*> Pair = Tagged(World, ArcherPairTag);
				Archer = Pair.Num() > 0 ? Pair[0] : nullptr;
			}
			return true;
		}
		if (Label == TEXT("squad_alert.png"))
		{
			TArray<AThugCharacter*> Pair = Tagged(World, StreetPairTag);
			int32 Facing = 0;
			FString States;
			for (AThugCharacter* Thug : Pair)
			{
				const FVector To = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
				Facing += FVector::DotProduct(Thug->GetActorForwardVector().GetSafeNormal2D(), To) > 0.5f ? 1 : 0;
				States += FString::Printf(TEXT("%s %s; "), *Thug->GetName(), *UEnum::GetValueAsString(Thug->GetAlertState()));
			}
			const bool bReady = Facing == Pair.Num() && Pair.Num() == 2 && Now - Start > 1.6;
			if (!bReady && !bTimedOut)
			{
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("squad_alert.png: after %.1f s, %d of %d facing her: %s"), Now - Start, Facing, Pair.Num(), *States));
			if (!bReady)
			{
				Test->AddWarning(TEXT("squad_alert.png: the squad alert did not turn both thugs toward her in time."));
			}
			return true;
		}
		if (Label == TEXT("archer_pickup.png"))
		{
			const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
			const AHawkeyePlayerController* HPC = Cast<AHawkeyePlayerController>(PC);
			const UHawkeyeHudWidget* Hud = HPC ? HPC->GetHawkeyeHud() : nullptr;
			const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
			Test->AddInfo(FString::Printf(TEXT("archer_pickup.png: %d standard arrows, toast '%s' / '%s'."),
				Inventory ? Inventory->GetArrowCount(1) : -1, Toasts && Toasts->IsToastVisible() ? *Toasts->GetToastHeading().ToString() : TEXT(""),
				Toasts && Toasts->IsToastVisible() ? *Toasts->GetToastTitle().ToString() : TEXT("")));
			return true;
		}
		return true;
	}

private:
	FAutomationTestBase* Test;
	FString Label;
	float Timeout;
	double Start = -1.0;
	double LastPing = -1.0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeEnemyTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeEnemyTakeShot::Update()
{
	using namespace HawkeyeEnemyShots;
	const FString FullPath = ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

void HawkeyeAddEnemyShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeEnemyShots;
	auto Shot = [Test](EShot Which) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeEnemyShot(Test, static_cast<uint8>(Which))); };
	auto Wait = [Test](const TCHAR* Label, float Timeout) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeEnemyWait(Test, FString(Label), Timeout)); };
	auto Take = [Test](const TCHAR* File) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeEnemyTakeShot(Test, FString(File))); };

	Shot(EShot::Freeze);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));

	Shot(EShot::SquadSetup);
	Wait(TEXT("squad_alert.png"), 3.5f);
	Take(TEXT("squad_alert.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	Shot(EShot::GunnerSetup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	Wait(TEXT("gunner_cover.png"), 14.f);
	Take(TEXT("gunner_cover.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	Shot(EShot::ArcherSetup);
	Wait(TEXT("archer_draw.png"), 10.f);
	Take(TEXT("archer_draw.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));

	Shot(EShot::ArrowFreeze);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	Shot(EShot::ArrowSetup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.3f));
	Shot(EShot::ArrowRelease);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	Shot(EShot::ArrowView);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	Take(TEXT("archer_arrow.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	Shot(EShot::Pickup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	Wait(TEXT("archer_pickup.png"), 0.f);
	Take(TEXT("archer_pickup.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	Shot(EShot::Cleanup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

#endif
