#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/HintScreenshots.h"
#include "UI/ChallengePanelWidget.h"
#include "UI/CrimePanelWidget.h"
#include "UI/HawkeyeHintSubsystem.h"
#include "UI/HawkeyeHintWidget.h"
#include "UI/HawkeyeHotbarWidget.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeHintShots
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");

	/** What the pass changed, to put back. */
	static TMap<FName, int32> SavedCounts;
	static int32 SavedCVar = 1;
	static bool bKateWasInvulnerable = false;
	static TWeakObjectPtr<AThugCharacter> Foe;
	static TArray<TWeakObjectPtr<AThugCharacter>> Frozen;
	static FVector StreetFeet = FVector::ZeroVector;
	static FVector Along = FVector::ForwardVector;
	static TArray<TWeakObjectPtr<AGrappleAnchor>> AnchorsToTry;
	static int32 AnchorTry = 0;
	static bool bAnchorFound = false;
	static int32 AnchorWaitFrames = 0;

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

	static AHawkeyePlayerController* FindController(UWorld* World)
	{
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static AHawkeyeCharacter* FindKate(UWorld* World)
	{
		AHawkeyePlayerController* PC = FindController(World);
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	static IConsoleVariable* HintsCVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.Hints"));
	}

	static void Walk(APlayerController* PC, bool bWalk)
	{
		ULocalPlayer* Local = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = Local ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local) : nullptr;
		const UInputAction* Move = LoadObject<UInputAction>(nullptr, MovePath);
		if (!Input || !Move)
		{
			return;
		}
		if (bWalk)
		{
			Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(FVector2D(0.f, 1.f)), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Move);
		}
	}

	static void Stand(ACharacter* Character, const FVector& Feet, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}

	/** Only Id may show: every other hint counts as learned. */
	static void Only(UWorld* World, FName Id)
	{
		if (UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(World))
		{
			Hints->ResetForTest({ Id });
		}
	}

	/**
	 * The shown line's checks: it is Id, fully faded in, below the middle of the screen, over the hotbar and not
	 * touching it, and clear of the challenge and crime panels.
	 */
	static void CheckLine(FAutomationTestBase* Test, const TCHAR* File, FName Id)
	{
		UWorld* World = FindWorld();
		const UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(World);
		UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(World);
		const UHawkeyeHintWidget* Line = Hud ? Hud->GetHintLine() : nullptr;
		if (!Hints || !Line)
		{
			Test->AddError(FString::Printf(TEXT("%s: no hint subsystem or no hint line on the HUD."), File));
			return;
		}
		const FHawkeyeHintRule* Shown = Hints->GetShown();
		Test->TestEqual(FString::Printf(TEXT("%s: the %s hint is up"), File, *Id.ToString()), Shown ? Shown->Id : NAME_None, Id);
		Test->TestEqual(FString::Printf(TEXT("%s: the line shows it"), File), Line->GetShownId(), Id);
		Test->TestTrue(FString::Printf(TEXT("%s: faded in"), File), Line->GetLineOpacity() > 0.99f);
		const FSlateRect Screen = Hud->GetCachedGeometry().GetRenderBoundingRect();
		const FSlateRect Rect = Line->GetCachedGeometry().GetRenderBoundingRect();
		const FSlateRect Bar = Hud->GetHotbar() ? Hud->GetHotbar()->GetCachedGeometry().GetRenderBoundingRect() : FSlateRect();
		const float Scale = Screen.GetSize().Y > 0.f ? 720.f / Screen.GetSize().Y : 1.f;
		Test->AddInfo(FString::Printf(TEXT("%s: \"%s\"; line %.0f x %.0f px at (%.0f, %.0f) on a %.0f x %.0f screen; the hotbar's top at %.0f; "
			"%.0f px between them (at 720p)."), File, *Line->GetShownText(), Rect.GetSize().X * Scale, Rect.GetSize().Y * Scale,
			(Rect.Left - Screen.Left) * Scale, (Rect.Top - Screen.Top) * Scale, Screen.GetSize().X * Scale, Screen.GetSize().Y * Scale,
			(Bar.Top - Screen.Top) * Scale, (Bar.Top - Rect.Bottom) * Scale));
		Test->TestTrue(FString::Printf(TEXT("%s: on the lower half, clear of the toasts and the compass"), File),
			Rect.Top > Screen.Top + Screen.GetSize().Y * 0.5f);
		Test->TestTrue(FString::Printf(TEXT("%s: centred"), File),
			FMath::Abs((Rect.Left + Rect.Right) * 0.5f - (Screen.Left + Screen.Right) * 0.5f) < 4.f);
		Test->TestTrue(FString::Printf(TEXT("%s: above the hotbar, not on it"), File), Rect.Bottom <= Bar.Top);
		for (const UWidget* Panel : { static_cast<const UWidget*>(Hud->GetCrimePanel()), static_cast<const UWidget*>(Hud->GetChallengePanel()) })
		{
			if (Panel && Panel->IsVisible())
			{
				const FSlateRect Other = Panel->GetCachedGeometry().GetRenderBoundingRect();
				Test->TestFalse(FString::Printf(TEXT("%s: clear of %s"), File, *Panel->GetName()), FSlateRect::DoRectanglesIntersect(Rect, Other));
			}
		}
		Test->TestTrue(FString::Printf(TEXT("%s: one line (not taller than two lines of text)"), File), Rect.GetSize().Y * Scale < 48.f);
	}
}

void HawkeyeAddHintShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeHintShots;
	// Set up: hints on for this pass only, the counts kept to put back, thugs frozen, Kate as herself at the start.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(World);
		if (PC && Cast<AHawkeyeCharacter>(PC->GetPawn()) && Cast<AHawkeyeCharacter>(PC->GetPawn())->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->bAllowSwitchingOverride = true;
			PC->SwitchCharacter();
		}
		AHawkeyeCharacter* Kate = FindKate(World);
		TActorIterator<APlayerStart> StartIt(World);
		if (!PC || !Kate || !Hints || !StartIt)
		{
			Test->AddError(TEXT("hint shots: no Kate, no PlayerStart or no hint subsystem."));
			return true;
		}
		if (!UHawkeyeSettingsSubsystem::GetCurrentSettings(World).bShowHints)
		{
			Test->AddWarning(TEXT("hint shots: the Hints setting is Off in this profile; the shots will show no line."));
		}
		SavedCounts.Reset();
		for (const FHawkeyeHintRule& Rule : UHawkeyeHintRules::GetDefaultRules())
		{
			SavedCounts.Add(Rule.Id, Hints->GetShowCount(Rule.Id));
		}
		if (IConsoleVariable* CVar = HintsCVar())
		{
			SavedCVar = CVar->GetInt();
			CVar->Set(2, ECVF_SetByCode);
		}
		Frozen.Reset();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			// Only the ones still thinking: the Kate pass keeps its own frozen, and gets them back frozen.
			AThugAIController* Brain = Cast<AThugAIController>(It->GetController());
			if (Brain && Brain->IsThinkingEnabled())
			{
				Brain->SetThinkingEnabled(false);
				Frozen.Add(*It);
			}
		}
		bKateWasInvulnerable = Kate->GetHealthComponent()->IsInvulnerable();
		Kate->GetHealthComponent()->SetInvulnerable(true);

		// The open street: from the PlayerStart, the heading with the longest clear run at waist height.
		const FVector Start = StartIt->GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HintShotStreet), false);
		Params.AddIgnoredActor(Kate);
		float BestClear = -1.f;
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Dir = FRotator(0.f, Step * 22.5f, 0.f).Vector();
			FHitResult Hit;
			const FVector From = Start + FVector(0.f, 0.f, 20.f);
			const float Clear = World->SweepSingleByChannel(Hit, From, From + Dir * 3000.f, FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeSphere(60.f), Params) ? Hit.Distance : 3000.f;
			if (Clear > BestClear)
			{
				BestClear = Clear;
				Along = Dir;
			}
		}
		FHitResult Floor;
		StreetFeet = World->LineTraceSingleByChannel(Floor, Start + FVector(0.f, 0.f, 50.f), Start - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params)
			? Floor.ImpactPoint : Start - FVector(0.f, 0.f, 90.f);
		Stand(Kate, StreetFeet, Along.Rotation().Yaw);
		PC->SetControlRotation(FRotator(-12.f, Along.Rotation().Yaw, 0.f));
		PC->SetViewTarget(Kate);
		Test->AddInfo(FString::Printf(TEXT("hint_sprint.png: walking along %.0f degrees, %.0f cm clear."), Along.Rotation().Yaw, BestClear));
		Only(World, TEXT("sprint"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// --- hint_sprint.png: walk, and the hint comes up after 3 s ---------------------------------------------
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		Walk(FindController(FindWorld()), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.7f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		CheckLine(Test, TEXT("hint_sprint.png"), TEXT("sprint"));
		HawkeyeShots::Request(Test, ShotPath(TEXT("hint_sprint.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		Walk(FindController(FindWorld()), false);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// --- hint_grapple.png: back at the start, turned to each anchor in reach until one is Ready ------------------
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		AHawkeyeCharacter* Kate = FindKate(World);
		if (!Kate)
		{
			return true;
		}
		Stand(Kate, StreetFeet, Along.Rotation().Yaw);
		TArray<AGrappleAnchor*> Near;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const float Flat = FVector::Dist2D(It->GetActorLocation(), StreetFeet);
			if (Flat < 8000.f)
			{
				Near.Add(*It);
			}
		}
		Near.Sort([](const AGrappleAnchor& A, const AGrappleAnchor& B)
		{
			return FVector::Dist(A.GetActorLocation(), StreetFeet) < FVector::Dist(B.GetActorLocation(), StreetFeet);
		});
		AnchorsToTry.Reset();
		for (AGrappleAnchor* Anchor : Near)
		{
			AnchorsToTry.Add(Anchor);
		}
		AnchorTry = 0;
		AnchorWaitFrames = 0;
		bAnchorFound = false;
		Only(World, TEXT("grapple"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindKate(World);
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		if (!PC || !Grapple)
		{
			return true;
		}
		// Each anchor gets a few frames for the camera to settle and the 0.1 s target refresh to run.
		if (AnchorWaitFrames > 0)
		{
			--AnchorWaitFrames;
			const AGrappleAnchor* Tried = AnchorsToTry.IsValidIndex(AnchorTry) ? AnchorsToTry[AnchorTry].Get() : nullptr;
			if (Grapple->GetTargetState() == EGrappleTargetState::Ready && Grapple->GetTargetAnchor())
			{
				// Ready: hold the view still here for the shot.
				AnchorWaitFrames = 0;
				bAnchorFound = true;
				const AGrappleAnchor* Target = Grapple->GetTargetAnchor();
				Test->AddInfo(FString::Printf(TEXT("hint_grapple.png: %s marked green, %.0f cm away (turned to %s, anchor %d tried)."),
					*Target->GetName(), FVector::Dist(Target->GetActorLocation(), Kate->GetActorLocation()), *GetNameSafe(Tried),
					AnchorTry + 1));
				return true;
			}
			if (AnchorWaitFrames > 0)
			{
				return false;
			}
			++AnchorTry;
		}
		if (!AnchorsToTry.IsValidIndex(AnchorTry) || AnchorTry >= 40)
		{
			Test->AddError(FString::Printf(TEXT("hint_grapple.png: none of %d anchors near the start came up Ready (last state %d)."),
				AnchorsToTry.Num(), static_cast<int32>(Grapple->GetTargetState())));
			return true;
		}
		if (const AGrappleAnchor* Anchor = AnchorsToTry[AnchorTry].Get())
		{
			// On the street 15 m out from the anchor, toward where she started, looking up at it.
			const FVector Out = (StreetFeet - Anchor->GetActorLocation()).GetSafeNormal2D();
			const FVector Probe = Anchor->GetActorLocation() + Out * 1500.f;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(HintShotAnchorGround), false);
			Params.AddIgnoredActor(Kate);
			FHitResult Floor;
			if (!Kate->GetWorld()->LineTraceSingleByChannel(Floor, FVector(Probe.X, Probe.Y, Anchor->GetActorLocation().Z - 300.f),
				FVector(Probe.X, Probe.Y, StreetFeet.Z - 500.f), ECC_Visibility, Params)
				|| Anchor->GetActorLocation().Z - Floor.ImpactPoint.Z < 600.f)
			{
				++AnchorTry;
				return false;
			}
			Stand(Kate, Floor.ImpactPoint, (-Out).Rotation().Yaw);
			const FVector To = Anchor->GetActorLocation() - Kate->GetActorLocation();
			const float Yaw = To.Rotation().Yaw;
			Kate->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			// The camera sits behind and above her: aim it a little under the anchor so the line clears her head.
			PC->SetControlRotation(FRotator(FMath::Clamp(To.Rotation().Pitch - 4.f, -20.f, 60.f), Yaw, 0.f));
		}
		AnchorWaitFrames = 15;
		return false;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		if (!bAnchorFound)
		{
			return true;
		}
		CheckLine(Test, TEXT("hint_grapple.png"), TEXT("grapple"));
		HawkeyeShots::Request(Test, ShotPath(TEXT("hint_grapple.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// --- hint_parry.png: a bat thug in front of her swings ------------------------------------------------------
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindKate(World);
		if (!World || !PC || !Kate)
		{
			return true;
		}
		Stand(Kate, StreetFeet, Along.Rotation().Yaw);
		PC->SetControlRotation(FRotator(-10.f, Along.Rotation().Yaw, 0.f));
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		if (!ThugClass)
		{
			ThugClass = AThugCharacter::StaticClass();
		}
		const FVector Feet = StreetFeet + Along * 150.f;
		const FTransform At(FRotator(0.f, (-Along).Rotation().Yaw, 0.f), Feet + FVector(0.f, 0.f, 100.f));
		AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(ThugClass, At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Thug)
		{
			Test->AddError(TEXT("hint_parry.png: could not spawn a thug."));
			return true;
		}
		Thug->Weapon = EThugWeapon::Bat;
		UGameplayStatics::FinishSpawningActor(Thug, At);
		Thug->RefreshHeldWeapon();
		if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
		Stand(Thug, Feet, (-Along).Rotation().Yaw);
		Foe = Thug;
		Only(World, TEXT("parry"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (AThugCharacter* Thug = Foe.Get())
		{
			Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack());
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		CheckLine(Test, TEXT("hint_parry.png"), TEXT("parry"));
		HawkeyeShots::Request(Test, ShotPath(TEXT("hint_parry.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Put everything back: the thug gone, the counts and the cvar as they were, the thugs thinking again.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		if (AThugCharacter* Thug = Foe.Get())
		{
			Thug->Destroy();
		}
		Foe = nullptr;
		if (UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(World))
		{
			for (const TPair<FName, int32>& Count : SavedCounts)
			{
				Hints->SetShowCount(Count.Key, Count.Value);
			}
		}
		if (IConsoleVariable* CVar = HintsCVar())
		{
			CVar->Set(SavedCVar, ECVF_SetByCode);
		}
		for (const TWeakObjectPtr<AThugCharacter>& Thug : Frozen)
		{
			if (AThugAIController* Brain = Thug.IsValid() ? Cast<AThugAIController>(Thug->GetController()) : nullptr)
			{
				Brain->SetThinkingEnabled(true);
			}
		}
		Frozen.Reset();
		if (AHawkeyeCharacter* Kate = FindKate(World))
		{
			Kate->GetHealthComponent()->SetInvulnerable(bKateWasInvulnerable);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

/** The hint shots on their own. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotHints, "Hawkeye.Screenshot.Hints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotHints::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the hint screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddHintShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
