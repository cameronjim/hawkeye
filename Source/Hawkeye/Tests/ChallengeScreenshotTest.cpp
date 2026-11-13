#include "Tests/ChallengeScreenshots.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Challenge/ChallengeCheckpoint.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTarget.h"
#include "Challenge/ChallengeTracker.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HawkeyePlayerController.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/ChallengePanelWidget.h"
#include "UI/ChallengeResultsWidget.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The side-challenge shots (Saved/Screenshots/Kate/), run in Hawkeye.Screenshot.Kate before the save pass
 * and on their own as Hawkeye.Screenshot.Challenge:
 *
 *   challenge_pedestal.png    archery_1's pedestal on its roof at night, Kate beside it: the purple top, the
 *                             turning target icon, the bracketed name over it
 *   archery_targets.png       her own camera at the start of archery_1, aiming at the nearest target: the rings
 *                             on the roofs, the purple secondary markers on every target, the challenge panel
 *   challenge_results.png     the results card after every target has been hit (by script, not by arrow)
 *   traversal_checkpoint.png  traversal_1 two rings in, on the first roof, looking at the next ring
 *
 * Every thug is frozen and Kate is invulnerable; she goes back where she was afterwards.
 */
namespace HawkeyeChallengeShots
{
	enum class EShot : uint8
	{
		Setup,
		Pedestal,
		TargetsStart,
		TargetsAim,
		ResultsHit,
		ResultsClose,
		CheckpointStart,
		CheckpointRing1,
		CheckpointRing2,
		CheckpointFace,
		Cleanup,
	};

	static TWeakObjectPtr<ACameraActor> ShotCamera;
	static TWeakObjectPtr<APointLight> FillLight;
	static TWeakObjectPtr<AChallengeStart> Archery;
	static TWeakObjectPtr<AChallengeStart> Traversal;
	static FTransform KateStart;
	static bool bKateWasInvulnerable = false;

	static constexpr float RingUp = 110.f;

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

	/** Kate with her feet at Feet facing Yaw, her camera behind her looking a little down. */
	static void StandAt(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Feet, float Yaw, float Pitch = -10.f)
	{
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Kate->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
	}

	static void AimAt(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Target - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
	}

	/** A camera at Eye looking at Target, with a soft fill light by the lens (the roofs are dark at night). */
	static void Frame(UWorld* World, APlayerController* PC, const FVector& Eye, const FVector& Target, float Fov, float Fill)
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
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 60.f));
			Light->PointLightComponent->SetIntensity(Fill);
		}
	}

	static void DimFill()
	{
		if (APointLight* Light = FillLight.Get())
		{
			Light->PointLightComponent->SetIntensity(0.f);
		}
	}

	/** The nearest target still up, from Kate. */
	static AChallengeTarget* NearestTarget(const UChallengeSubsystem* Challenges, const AHawkeyeCharacter* Kate)
	{
		AChallengeTarget* Best = nullptr;
		float BestDistance = BIG_NUMBER;
		for (const TWeakObjectPtr<AChallengeTarget>& Weak : Challenges->GetTargets())
		{
			AChallengeTarget* Target = Weak.Get();
			const float Distance = Target ? FVector::Dist(Target->GetFaceCentre(), Kate->GetActorLocation()) : BIG_NUMBER;
			if (Target && !Target->IsDown() && Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Target;
			}
		}
		return Best;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeChallengeShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeChallengeShot::Update()
{
	using namespace HawkeyeChallengeShots;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
	if (!World || !Kate || !Challenges)
	{
		Test->AddError(TEXT("Challenge shots: no world, no player, or no challenge subsystem."));
		return true;
	}
	AChallengeStart* Range = Archery.Get();
	AChallengeStart* Route = Traversal.Get();

	switch (static_cast<EShot>(Shot))
	{
	case EShot::Setup:
	{
		// Back to Kate if the partner pass left the player on Clint.
		if (Kate->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->bAllowSwitchingOverride = true;
			PC->SwitchCharacter();
			Kate = Cast<AHawkeyeCharacter>(PC->GetPawn());
		}
		HawkeyeFreezePartner(World);
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		KateStart = Kate->GetActorTransform();
		bKateWasInvulnerable = Kate->GetHealthComponent()->IsInvulnerable();
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Kate->StopAim();
		Archery = FindStart(World, TEXT("archery_1"));
		Traversal = FindStart(World, TEXT("traversal_1"));
		if (!Archery.IsValid() || !Traversal.IsValid())
		{
			Test->AddError(TEXT("Challenge shots: no City_Challenge_archery_1 or City_Challenge_traversal_1 on the district."));
		}
		break;
	}

	case EShot::Pedestal:
	{
		if (!Range)
		{
			break;
		}
		const FVector Foot = Range->GetActorLocation();
		const FVector Forward = Range->GetActorForwardVector().GetSafeNormal2D();
		const FVector Side(-Forward.Y, Forward.X, 0.f);
		// She stands off its shoulder, looking at it; the camera looks back at both from the targets' side.
		StandAt(PC, Kate, Foot + Forward * 110.f + Side * 90.f, (-Forward - Side * 0.6f).Rotation().Yaw);
		Frame(World, PC, Foot + Forward * 430.f + Side * 170.f + FVector(0.f, 0.f, 175.f), Foot + FVector(0.f, 0.f, 135.f), 55.f, 120.f);
		break;
	}

	case EShot::TargetsStart:
	{
		if (!Range)
		{
			break;
		}
		DimFill();
		PC->SetViewTarget(Kate);
		StandAt(PC, Kate, Range->GetStandLocation(), Range->GetActorRotation().Yaw);
		if (!Challenges->StartChallenge(Range, Kate))
		{
			Test->AddError(TEXT("archery_targets.png: the archery challenge did not start."));
			break;
		}
		Kate->StartAim();
		if (const AChallengeTarget* Nearest = NearestTarget(Challenges, Kate))
		{
			AimAt(PC, Kate, Nearest->GetFaceCentre());
		}
		break;
	}

	case EShot::TargetsAim:
	{
		const AChallengeTarget* Nearest = NearestTarget(Challenges, Kate);
		if (Nearest)
		{
			AimAt(PC, Kate, Nearest->GetFaceCentre() + FVector(0.f, 0.f, 40.f));
		}
		const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Kate);
		const UChallengePanelWidget* Panel = Hud ? Hud->GetChallengePanel() : nullptr;
		const UHawkeyeObjectiveWidget* Markers = Hud ? Hud->GetObjectiveMarker() : nullptr;
		Test->AddInfo(FString::Printf(TEXT("archery_targets.png: %d targets, nearest %.0f m; panel %s (%s, %s); %d secondary markers."),
			Challenges->GetTargets().Num(), Nearest ? FVector::Dist(Nearest->GetFaceCentre(), Kate->GetActorLocation()) / 100.f : 0.f,
			Panel && Panel->IsPanelVisible() ? TEXT("up") : TEXT("down"), Panel ? *Panel->GetClockText().ToString() : TEXT("-"),
			Panel ? *Panel->GetProgressText().ToString() : TEXT("-"), Markers ? Markers->GetSecondaryMarkerCount() : 0));
		break;
	}

	case EShot::ResultsHit:
	{
		Kate->StopAim();
		// Every target, a spread of rings, as if she had shot the range well.
		int32 Index = 0;
		TArray<AChallengeTarget*> Up;
		for (const TWeakObjectPtr<AChallengeTarget>& Weak : Challenges->GetTargets())
		{
			if (AChallengeTarget* Target = Weak.Get(); Target && !Target->IsDown())
			{
				Up.Add(Target);
			}
		}
		for (AChallengeTarget* Target : Up)
		{
			const float Off = (Index % 3) == 0 ? 3.f : ((Index % 3) == 1 ? 12.f : 24.f);
			Target->HandleArrowHit(Target->GetFaceCentre() + FVector(0.f, 0.f, Off), Kate);
			++Index;
		}
		const UChallengeResultsWidget* Card = PC->GetChallengeResultsWidget();
		const TArray<FText> Lines = Card ? Card->GetLines() : TArray<FText>();
		FString Shown;
		for (const FText& Line : Lines)
		{
			Shown += (Shown.IsEmpty() ? TEXT("") : TEXT(" | ")) + Line.ToString();
		}
		Test->AddInfo(FString::Printf(TEXT("challenge_results.png: card %s: %s"), PC->IsChallengeResultsOpen() ? TEXT("open") : TEXT("shut"), *Shown));
		if (!PC->IsChallengeResultsOpen())
		{
			Test->AddWarning(TEXT("challenge_results.png: the results card did not open."));
		}
		break;
	}

	case EShot::ResultsClose:
		PC->CloseChallengeResults();
		break;

	case EShot::CheckpointStart:
		if (!Route)
		{
			break;
		}
		StandAt(PC, Kate, Route->GetStandLocation(), Route->GetActorRotation().Yaw);
		if (!Challenges->StartChallenge(Route, Kate))
		{
			Test->AddError(TEXT("traversal_checkpoint.png: the traversal challenge did not start."));
		}
		break;

	case EShot::CheckpointRing1:
	case EShot::CheckpointRing2:
	{
		const UChallengeDefinition* Definition = Route ? Route->Definition.Get() : nullptr;
		const int32 Ring = static_cast<EShot>(Shot) == EShot::CheckpointRing1 ? 0 : 1;
		if (Definition && Definition->Checkpoints.IsValidIndex(Ring + 1))
		{
			const FVector Here = Definition->Checkpoints[Ring].GetLocation() - FVector(0.f, 0.f, RingUp);
			const FVector Next = Definition->Checkpoints[Ring + 1].GetLocation();
			StandAt(PC, Kate, Here, (Next - Here).Rotation().Yaw);
		}
		break;
	}

	case EShot::CheckpointFace:
	{
		const UChallengeDefinition* Definition = Route ? Route->Definition.Get() : nullptr;
		const int32 Next = Challenges->GetTracker()->GetNextCheckpointIndex();
		if (Definition && Definition->Checkpoints.IsValidIndex(Next))
		{
			const FVector Ring = Definition->Checkpoints[Next].GetLocation();
			const FVector To = Ring - Kate->GetActorLocation();
			// Her back to the camera, the ring ahead and a little right of her.
			PC->SetControlRotation(FRotator(-8.f, To.Rotation().Yaw - 8.f, 0.f));
			Kate->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
			const AChallengeCheckpoint* Shown = Challenges->GetCheckpoints().IsValidIndex(Next) ? Challenges->GetCheckpoints()[Next].Get() : nullptr;
			Test->AddInfo(FString::Printf(TEXT("traversal_checkpoint.png: heading for ring %d of %d, %.0f m ahead, ring state %s."), Next + 1,
				Definition->Checkpoints.Num(), To.Size() / 100.f, Shown ? *UEnum::GetValueAsString(Shown->GetState()) : TEXT("-")));
			if (Next < 2)
			{
				Test->AddWarning(FString::Printf(TEXT("traversal_checkpoint.png: only %d rings taken before the shot."), Next));
			}
		}
		break;
	}

	case EShot::Cleanup:
		Challenges->bShowResultsCard = false;
		Challenges->AbortChallenge();
		Challenges->bShowResultsCard = true;
		PC->CloseChallengeResults();
		PC->SetViewTarget(Kate);
		Kate->StopAim();
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		if (APointLight* Light = FillLight.Get())
		{
			Light->Destroy();
		}
		Kate->TeleportTo(KateStart.GetLocation(), KateStart.Rotator(), false, true);
		Kate->GetHealthComponent()->Heal(1000.f);
		Kate->GetHealthComponent()->SetInvulnerable(bKateWasInvulnerable);
		break;
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeChallengeTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeChallengeTakeShot::Update()
{
	HawkeyeShots::Request(Test, HawkeyeChallengeShots::ShotPath(FileName), /*bShowUI=*/true);
	return true;
}

void HawkeyeAddChallengeShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeChallengeShots::EShot;
	auto Shot = [Test](EShot Which, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeShot(Test, static_cast<uint8>(Which)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};
	auto Take = [Test](const TCHAR* File, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeTakeShot(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};

	Shot(EShot::Setup, 0.5f);
	// The icon turns and the name faces the camera; long enough for the roof's props to stream in.
	Shot(EShot::Pedestal, 2.5f);
	Take(TEXT("challenge_pedestal.png"), 0.5f);

	Shot(EShot::TargetsStart, 1.2f);
	Shot(EShot::TargetsAim, 1.2f);
	Take(TEXT("archery_targets.png"), 0.5f);

	// The card pauses the game; the capture still draws it.
	Shot(EShot::ResultsHit, 1.f);
	Take(TEXT("challenge_results.png"), 0.5f);
	Shot(EShot::ResultsClose, 0.5f);

	// Two rings taken (the tracker needs a tick in each), then the third ahead on the first roof.
	Shot(EShot::CheckpointStart, 0.6f);
	Shot(EShot::CheckpointRing1, 0.4f);
	Shot(EShot::CheckpointRing2, 0.8f);
	Shot(EShot::CheckpointFace, 1.5f);
	Take(TEXT("traversal_checkpoint.png"), 0.5f);

	Shot(EShot::Cleanup, 0.5f);
}

void HawkeyeAddChallengePedestalShot(FAutomationTestBase* Test, const FString& FileName)
{
	using EShot = HawkeyeChallengeShots::EShot;
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeShot(Test, static_cast<uint8>(EShot::Setup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeShot(Test, static_cast<uint8>(EShot::Pedestal)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeTakeShot(Test, FileName));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeChallengeShot(Test, static_cast<uint8>(EShot::Cleanup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

/** The challenge shots on their own, for tuning without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotChallenge, "Hawkeye.Screenshot.Challenge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotChallenge::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the challenge screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddChallengeShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
