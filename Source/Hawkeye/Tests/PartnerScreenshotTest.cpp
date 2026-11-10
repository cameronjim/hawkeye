#include "Tests/PartnerScreenshots.h"

#include "Camera/CameraComponent.h"
#include "Combat/BowComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dialogue/BanterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HawkeyeGameMode.h"
#include "Mission/MissionSubsystem.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "UI/HawkeyeHudWidget.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The partner shots, run at the end of Hawkeye.Screenshot.Kate (the placed thugs frozen throughout):
 *
 *   partner_follow.png  Kate on the street by the park with Clint run up to 6 m behind her, the camera
 *                       looking back past her at him: his CLINT name tag and "Clint: following"
 *   partner_shoot.png   Kate's arrow lands on a street thug (the bow's own OnHit), and 0.65 s later
 *                       Clint is part way through his draw on the thug's chest, seen side on
 *   banter.png          an after-fight line from DT_Dialogue on the subtitle line
 *   switch_clint.png    1.5 s after the switch: the camera behind Clint, the HUD saying Clint, and Kate
 *                       (now the partner) walking in to follow him
 */
namespace HawkeyePartnerShots
{
	static const FName StreetPairTag(TEXT("StreetPair"));

	enum class EShot : uint8
	{
		Follow,
		ShootSetup,
		ShootHit,
		Banter,
		Switch,
		Cleanup,
	};

	/** The thug Clint shoots. */
	static TWeakObjectPtr<AThugCharacter> Target;

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

	static AHawkeyePartnerController* FindPartner(UWorld* World)
	{
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			if (It->GetPartner())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool FindGround(UWorld* World, const FVector& XY, TArray<const AActor*> Ignore, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerShotGround), false);
		for (const AActor* Actor : Ignore)
		{
			Params.AddIgnoredActor(Actor);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, 3000.f), FVector(XY.X, XY.Y, -3000.f),
				ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	/**
	 * A spot on East 7th Street 4 m from the PlayerStart toward the park (where City_ClintStart is), the
	 * direction along the street, and the direction away from the park (the way the start faces).
	 */
	static bool FindStreet(UWorld* World, FVector& OutSpot, FVector& OutAlong, FVector& OutAway)
	{
		TActorIterator<APlayerStart> Start(World);
		if (!Start)
		{
			return false;
		}
		OutAway = Start->GetActorForwardVector().GetSafeNormal2D();
		OutAlong = FVector::CrossProduct(FVector::UpVector, OutAway);
		OutSpot = Start->GetActorLocation() - OutAway * 400.f;
		return !OutAway.IsNearlyZero();
	}

	static void Stand(ACharacter* Character, const FVector& Ground, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
	}

	static void LookAt(APlayerController* PC, const AHawkeyeCharacter* Viewer, const FVector& Point, float PitchOffset = 0.f)
	{
		const FVector From = Viewer->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		const FRotator Rotation = (Point - From).Rotation();
		PC->SetControlRotation(FRotator(Rotation.Pitch + PitchOffset, Rotation.Yaw, 0.f));
	}

	static AThugCharacter* FindStreetThug(UWorld* World)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(StreetPairTag) && !It->IsLimp() && !It->IsGunner())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}
}

void HawkeyeFreezePartner(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		It->SetThinkingEnabled(false);
	}
	if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()))
	{
		if (UBanterComponent* Banter = PC->GetBanter())
		{
			Banter->SetComponentTickEnabled(false);
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyePartnerShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyePartnerShot::Update()
{
	using namespace HawkeyePartnerShots;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (static_cast<EShot>(Shot) == EShot::Follow && World && PC)
	{
		// Act 1's chapter benches Clint and refuses the switch (DA_CH01_NightShift); the partner's own shots bring
		// him on stage for themselves, and Cleanup puts the chapter's cast back.
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			It->SetBenched(false);
		}
		PC->bAllowSwitchingOverride = true;
	}
	AHawkeyeCharacter* Lead = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	AHawkeyePartnerController* Brain = World ? FindPartner(World) : nullptr;
	AHawkeyeCharacter* Partner = Brain ? Brain->GetPartner() : nullptr;
	FVector Spot, Along, Away, Ground;
	if (!Lead || !Partner || !FindStreet(World, Spot, Along, Away))
	{
		Test->AddError(TEXT("No Kate, no partner Clint, or no street by the park for the partner shots."));
		return true;
	}

	switch (static_cast<EShot>(Shot))
	{
	case EShot::Follow:
		if (!FindGround(World, Spot, { Lead, Partner }, Ground))
		{
			Test->AddError(TEXT("No ground at the follow spot."));
			break;
		}
		Lead->GetHealthComponent()->SetInvulnerable(true);
		Stand(Lead, Ground, Along.Rotation().Yaw);
		// 12 m back along the street: he runs up to the 6 m he settles at.
		if (FindGround(World, Spot - Along * 1200.f, { Lead, Partner }, Ground))
		{
			Stand(Partner, Ground, Along.Rotation().Yaw);
		}
		Brain->CancelCommand();
		Brain->SetThinkingEnabled(true);
		PC->SetViewTarget(Lead);
		LookAt(PC, Lead, Spot - Along * 700.f, -6.f);
		break;

	case EShot::ShootSetup:
	{
		AThugCharacter* Thug = FindStreetThug(World);
		Target = Thug;
		if (!Thug)
		{
			Test->AddError(TEXT("No street thug for Clint to shoot."));
			break;
		}
		// Clint at the spot, the thug 15 m up the street facing him.
		if (FindGround(World, Spot, { Lead, Partner, Thug }, Ground))
		{
			Stand(Partner, Ground, Along.Rotation().Yaw);
		}
		if (FindGround(World, Spot + Along * 1500.f, { Lead, Partner, Thug }, Ground))
		{
			Stand(Thug, Ground, (-Along).Rotation().Yaw);
		}
		// Kate behind him and a little out, inside his follow band, looking up the street past him.
		if (FindGround(World, Spot - Along * 420.f + Away * 150.f, { Lead, Partner, Thug }, Ground))
		{
			Stand(Lead, Ground, Along.Rotation().Yaw);
		}
		LookAt(PC, Lead, Spot + Along * 1500.f, -3.f);
		break;
	}

	case EShot::ShootHit:
		if (AThugCharacter* Thug = Target.Get())
		{
			// What Kate's arrow does when it lands: the bow's OnHit, which the partner listens to.
			Lead->GetBowComponent()->NotifyArrowHit(Thug, 0.f, false);
			Test->AddInfo(FString::Printf(TEXT("partner_shoot: Kate's hit on %s at %.0f cm from Clint."), *Thug->GetName(),
				FVector::Dist(Thug->GetActorLocation(), Partner->GetActorLocation())));
		}
		break;

	case EShot::Banter:
		LookAt(PC, Lead, Partner->GetActorLocation(), -6.f);
		if (UBanterComponent* Banter = PC->GetBanter())
		{
			Banter->PlaySituation(EHawkeyeBanterSituation::AfterFight);
		}
		break;

	case EShot::Switch:
	{
		// Kate 9 m from him, so once she is the partner she walks in to 6 m.
		if (FindGround(World, Partner->GetActorLocation() + Away * 900.f, { Lead, Partner }, Ground))
		{
			Stand(Lead, Ground, Along.Rotation().Yaw);
		}
		AHawkeyeCharacter* Kate = Lead;
		AHawkeyeCharacter* Clint = Partner;
		if (!PC->SwitchCharacter())
		{
			Test->AddError(FString::Printf(TEXT("The switch was refused: %s."), *PC->GetSwitchRefusal()));
			break;
		}
		Clint->GetHealthComponent()->SetInvulnerable(true);
		LookAt(PC, Clint, Kate->GetActorLocation() - Away * 300.f, -6.f);
		break;
	}

	case EShot::Cleanup:
		// Back to Kate, everyone mortal again, the partner and the banter still.
		if (Lead->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->SwitchCharacter();
		}
		HawkeyeFreezePartner(World);
		Lead->GetHealthComponent()->SetInvulnerable(false);
		Partner->GetHealthComponent()->SetInvulnerable(false);
		PC->bAllowSwitchingOverride = false;
		if (const UMissionSubsystem* Missions = UMissionSubsystem::Get(World))
		{
			AHawkeyeGameMode::ApplyChapterCast(World, Missions->GetCurrentMission());
		}
		break;
	}
	return true;
}

/** What the partner is doing at the moment of a shot. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyePartnerReport, FAutomationTestBase*, Test, FString, Label);

bool FHawkeyePartnerReport::Update()
{
	using namespace HawkeyePartnerShots;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	const AHawkeyeCharacter* Lead = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	const AHawkeyePartnerController* Brain = World ? FindPartner(World) : nullptr;
	const AHawkeyeCharacter* Partner = Brain ? Brain->GetPartner() : nullptr;
	const UHawkeyeHudWidget* Hud = PC ? PC->GetHawkeyeHud() : nullptr;
	if (!Lead || !Partner)
	{
		Test->AddError(FString::Printf(TEXT("%s: no lead or partner to report."), *Label));
		return true;
	}
	const UBowComponent* Bow = Partner->GetBowComponent();
	Test->AddInfo(FString::Printf(
		TEXT("%s: playing %s, partner %s at %.0f cm (%s, StateTree %d, mode %s, target %s, drawing %d at %.0f%%, speed %.0f); ")
		TEXT("HUD name '%s', partner line '%s', tag %d, subtitle '%s'"),
		*Label, *Lead->GetCharacterName().ToString(), *Partner->GetCharacterName().ToString(),
		FVector::Dist(Lead->GetActorLocation(), Partner->GetActorLocation()), *Brain->GetStatusText().ToString(),
		Brain->IsUsingStateTree() ? 1 : 0, *UEnum::GetValueAsString(Brain->GetMode()), *GetNameSafe(Brain->GetAttackTarget()),
		Bow && Bow->IsDrawing() ? 1 : 0, Bow ? Bow->GetDrawFraction() * 100.f : 0.f, Partner->GetVelocity().Size2D(),
		Hud ? *Hud->GetCharacterNameShown().ToString() : TEXT("-"), Hud ? *Hud->GetPartnerStatusShown().ToString() : TEXT("-"),
		Hud && Hud->IsPartnerTagVisible() ? 1 : 0, Hud && Hud->IsSubtitleVisible() ? *Hud->GetSubtitleShown().ToString() : TEXT("")));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyePartnerTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyePartnerTakeShot::Update()
{
	const FString FullPath = HawkeyePartnerShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

void HawkeyeAddPartnerShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyePartnerShots::EShot;
	auto Shot = [Test](EShot Which) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyePartnerShot(Test, static_cast<uint8>(Which))); };
	auto Capture = [Test](const TCHAR* File)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyePartnerReport(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyePartnerTakeShot(Test, FString(File)));
	};

	Shot(EShot::Follow);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.5f));
	Capture(TEXT("partner_follow.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	Shot(EShot::ShootSetup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	Shot(EShot::ShootHit);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.65f));
	Capture(TEXT("partner_shoot.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.5f));

	Shot(EShot::Banter);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	Capture(TEXT("banter.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));

	Shot(EShot::Switch);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	Capture(TEXT("switch_clint.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	Shot(EShot::Cleanup);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

#endif
