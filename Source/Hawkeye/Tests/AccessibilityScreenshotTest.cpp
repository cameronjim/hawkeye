#include "Tests/AccessibilityScreenshots.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HawkeyePlayerController.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/HawkeyeSettingsWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The difficulty and accessibility shots (Saved/Screenshots/Kate/), run in Hawkeye.Screenshot.Kate after the
 * challenge shots and on their own as Hawkeye.Screenshot.Accessibility:
 *
 *   settings_full.png         the pause menu's Settings, both columns, at the pass's 1280x720
 *   difficulty_prompt.png     the New Game prompt (Story / Normal / Hard) over the main menu
 *   subtitles_large.png       a placeholder line at the large size (34 px) on a 60% background
 *   palette_deuteranopia.png  aiming on the street in the deuteranopia palette: the reticle, the objective
 *                             marker and compass, a thug's "!" and health bar in its colours
 *   pedestal_dimmer.png       archery_1's pedestal on its roof at night with the dimmer light, no fill
 *
 * The HUD scale is checked, not shot: at 1.4 the hotbar and the compass must report 1.4. Every thug is
 * frozen, Kate is invulnerable, and the player's settings are restored at the end.
 */
namespace HawkeyeAccessibilityShots
{
	enum class EShot : uint8
	{
		Setup,
		SettingsOpen,
		SettingsClose,
		PromptOpen,
		PromptClose,
		Subtitles,
		HudScaleCheck,
		Palette,
		PaletteGlyph,
		Pedestal,
		Cleanup,
	};

	static FHawkeyeSettings SavedSettings;
	static FTransform KateStart;
	static bool bKateWasInvulnerable = false;
	static TWeakObjectPtr<AThugCharacter> ShownThug;
	static FTransform ThugStart;
	static TWeakObjectPtr<ACameraActor> ShotCamera;

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

	/** The thug nearest Location, or null. */
	static AThugCharacter* NearestThug(UWorld* World, const FVector& Location)
	{
		AThugCharacter* Best = nullptr;
		float BestDistance = BIG_NUMBER;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), Location);
			if (!It->IsLimp() && It->GetHealthComponent() && It->GetHealthComponent()->IsAlive() && Distance < BestDistance)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	/** One step of the pass. Short, so each case stays readable. */
	static void RunShot(FAutomationTestBase* Test, UWorld* World, AHawkeyePlayerController* PC, AHawkeyeCharacter* Kate,
		UHawkeyeSettingsSubsystem* Settings, EShot Shot);
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeAccessibilityShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeAccessibilityShot::Update()
{
	using namespace HawkeyeAccessibilityShots;
	UWorld* World = FindWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(World);
	if (!World || !Kate || !Settings)
	{
		Test->AddError(TEXT("Accessibility shots: no world, no player, or no settings subsystem."));
		return true;
	}
	RunShot(Test, World, PC, Kate, Settings, static_cast<EShot>(Shot));
	return true;
}

void HawkeyeAccessibilityShots::RunShot(FAutomationTestBase* Test, UWorld* World, AHawkeyePlayerController* PC,
	AHawkeyeCharacter* Kate, UHawkeyeSettingsSubsystem* Settings, EShot Shot)
{
	UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Kate);
	switch (Shot)
	{
	case EShot::Setup:
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
		SavedSettings = Settings->GetStoredSettings();
		KateStart = Kate->GetActorTransform();
		bKateWasInvulnerable = Kate->GetHealthComponent()->IsInvulnerable();
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Kate->StopAim();
		PC->SetViewTarget(Kate);
		break;

	case EShot::SettingsOpen:
	{
		PC->SetPauseMenuOpen(true);
		PC->OpenSettings();
		const UHawkeyeSettingsWidget* Screen = PC->GetSettingsWidget();
		Test->TestTrue(TEXT("settings_full.png: the settings screen is open"), PC->IsSettingsOpen());
		Test->AddInfo(FString::Printf(TEXT("settings_full.png: %d rows, difficulty reads %s."), Screen ? Screen->GetRowCount() : 0,
			Screen ? *Screen->GetDifficultyShown().ToString() : TEXT("-")));
		break;
	}

	case EShot::SettingsClose:
		PC->CloseSettings();
		PC->SetPauseMenuOpen(false);
		break;

	case EShot::PromptOpen:
		PC->ShowMainMenu();
		PC->ShowDifficultyPrompt();
		Test->TestTrue(TEXT("difficulty_prompt.png: the prompt is up"), PC->IsDifficultyPromptOpen());
		break;

	case EShot::PromptClose:
		PC->HideDifficultyPrompt();
		PC->HideMainMenu();
		PC->SetViewTarget(Kate);
		break;

	case EShot::Subtitles:
		Settings->SetSubtitleSize(EHawkeyeSubtitleSize::Large);
		Settings->SetSubtitleBackgroundOpacity(0.6f);
		if (Hud)
		{
			Hud->ShowSubtitle(FText::FromString(TEXT("Kate")),
				FText::FromString(TEXT("[Placeholder line: the subtitle at the large size, on its background.]")), 6.f);
			Test->TestEqual(TEXT("subtitles_large.png: 34 px"), Hud->GetSubtitleFontSize(), 34);
		}
		break;

	case EShot::HudScaleCheck:
		Settings->SetHudScale(1.4f);
		if (Hud)
		{
			Test->TestEqual(TEXT("HUD scale 1.4: the hotbar"), Hud->GetHotbarRenderScale(), 1.4f, 0.001f);
			const UHawkeyeObjectiveWidget* Markers = Hud->GetObjectiveMarker();
			Test->TestEqual(TEXT("HUD scale 1.4: the compass"), Markers ? Markers->GetHudScale() : 0.f, 1.4f, 0.001f);
			Test->AddInfo(FString::Printf(TEXT("HUD scale 1.4: compass %.0f px, marker %.1f px."),
				Markers ? Markers->GetCompassWidth() : 0.f, Markers ? Markers->GetMarkerSize() : 0.f));
		}
		Settings->SetHudScale(1.f);
		if (Hud)
		{
			Hud->ShowSubtitle(FText::GetEmpty(), FText::GetEmpty(), 0.f);
		}
		Settings->SetSubtitleSize(SavedSettings.SubtitleSize);
		Settings->SetSubtitleBackgroundOpacity(SavedSettings.SubtitleBackgroundOpacity);
		break;

	case EShot::Palette:
	{
		Settings->SetColorPalette(EHawkeyeColorPalette::Deuteranopia);
		// A thug 7 m ahead on the street, to carry the glyph and the bar.
		const FVector Ahead = Kate->GetActorLocation() + Kate->GetActorForwardVector().GetSafeNormal2D() * 700.f;
		if (AThugCharacter* Thug = NearestThug(World, Ahead))
		{
			ShownThug = Thug;
			ThugStart = Thug->GetActorTransform();
			Thug->TeleportTo(Ahead + FVector(0.f, 150.f, 0.f), (Kate->GetActorLocation() - Ahead).Rotation(), false, true);
			Thug->GetHealthComponent()->ApplyDamage(35.f, nullptr);
		}
		Kate->StartAim();
		break;
	}

	case EShot::PaletteGlyph:
		if (AThugCharacter* Thug = ShownThug.Get())
		{
			// The "!" lives 0.6 s: raised here, captured just after.
			Thug->SetAlertState(EThugAlertState::Calm);
			Thug->SetAlertState(EThugAlertState::Alerted);
		}
		if (Hud)
		{
			Test->TestEqual(TEXT("palette_deuteranopia.png: the HUD's palette"), Hud->GetPaletteShown(), EHawkeyeColorPalette::Deuteranopia);
		}
		break;

	case EShot::Pedestal:
	{
		Kate->StopAim();
		Settings->SetColorPalette(SavedSettings.ColorPalette);
		if (AThugCharacter* Thug = ShownThug.Get())
		{
			Thug->TeleportTo(ThugStart.GetLocation(), ThugStart.Rotator(), false, true);
			Thug->GetHealthComponent()->Heal(1000.f);
			Thug->SetAlertState(EThugAlertState::Calm);
		}
		AChallengeStart* Range = FindStart(World, TEXT("archery_1"));
		if (!Range)
		{
			Test->AddError(TEXT("pedestal_dimmer.png: no archery_1 pedestal."));
			break;
		}
		const FVector Foot = Range->GetActorLocation();
		const FVector Forward = Range->GetActorForwardVector().GetSafeNormal2D();
		const FVector Side(-Forward.Y, Forward.X, 0.f);
		// Kate beside it; the camera back and up, so the pedestal sits in the roof it used to wash purple.
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Foot + Forward * 150.f + Side * 120.f + FVector(0.f, 0.f, HalfHeight + 2.f), Forward.Rotation(), false, true);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		ShotCamera = Camera;
		if (Camera)
		{
			const FVector Eye = Foot - Forward * 520.f - Side * 260.f + FVector(0.f, 0.f, 330.f);
			Camera->SetActorLocationAndRotation(Eye, (Foot + Forward * 300.f - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(70.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC->SetViewTarget(Camera);
		}
		const UPointLightComponent* Light = Range->GetGlowLight();
		Test->AddInfo(FString::Printf(TEXT("pedestal_dimmer.png: light %.0f lm over %.0f cm, cap glow %.1f."),
			Light ? Light->Intensity : 0.f, Light ? Light->AttenuationRadius : 0.f, Range->CapGlow));
		break;
	}

	case EShot::Cleanup:
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		PC->SetViewTarget(Kate);
		Settings->SetSettings(SavedSettings);
		Kate->TeleportTo(KateStart.GetLocation(), KateStart.Rotator(), false, true);
		Kate->GetHealthComponent()->Heal(1000.f);
		Kate->GetHealthComponent()->SetInvulnerable(bKateWasInvulnerable);
		break;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeAccessibilityTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeAccessibilityTakeShot::Update()
{
	HawkeyeShots::Request(Test, HawkeyeAccessibilityShots::ShotPath(FileName), /*bShowUI=*/true);
	return true;
}

void HawkeyeAddAccessibilityShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeAccessibilityShots::EShot;
	auto Shot = [Test](EShot Which, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAccessibilityShot(Test, static_cast<uint8>(Which)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};
	auto Take = [Test](const TCHAR* File, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAccessibilityTakeShot(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};

	Shot(EShot::Setup, 0.5f);
	Shot(EShot::SettingsOpen, 1.f);
	Take(TEXT("settings_full.png"), 0.5f);
	Shot(EShot::SettingsClose, 0.5f);

	Shot(EShot::PromptOpen, 1.f);
	Take(TEXT("difficulty_prompt.png"), 0.5f);
	Shot(EShot::PromptClose, 1.f);

	Shot(EShot::Subtitles, 0.8f);
	Take(TEXT("subtitles_large.png"), 0.5f);
	Shot(EShot::HudScaleCheck, 0.3f);

	Shot(EShot::Palette, 1.2f);
	Shot(EShot::PaletteGlyph, 0.25f);
	Take(TEXT("palette_deuteranopia.png"), 0.5f);

	// The roof's props and the night lighting need a moment after the jump across the district.
	Shot(EShot::Pedestal, 2.5f);
	Take(TEXT("pedestal_dimmer.png"), 0.5f);

	Shot(EShot::Cleanup, 0.5f);
}

/** The accessibility shots on their own, for tuning without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotAccessibility, "Hawkeye.Screenshot.Accessibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotAccessibility::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the accessibility screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddAccessibilityShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
