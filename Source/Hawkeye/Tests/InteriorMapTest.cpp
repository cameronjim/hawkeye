#include "Camera/CameraComponent.h"
#include "Combat/TakedownComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
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
#include "Player/ParkourComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/InteriorScreenshots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/DoorActor.h"
#include "World/GrappleAnchor.h"
#include "World/InteractionComponent.h"
#include "World/InteriorEntrance.h"
#include "World/InteriorExit.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The sample interior, L_Int_Sample (Tools/Interiors/Sample.json, built by Tools/Editor/generate_interior.py),
 * in a real world: it loads playable, its stairs are walkable by the navmesh, the Interior screenshot pass,
 * and a scripted walk through it from the district and back. The points below are the sample layout's own
 * (the layout frame is the map's frame): change them with the layout.
 */
namespace HawkeyeInteriorMap
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_Int_Sample");
	static const TCHAR* DistrictPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* Slot = TEXT("HawkeyeCampaignInteriorWalk");
	static const FName EntranceLabel(TEXT("City_InteriorEntrance_Sample"));
	static const FName DoorstepTag(TEXT("City_InteriorReturn_Sample"));
	static const FName RoofTag(TEXT("City_InteriorRoof_Sample"));
	static constexpr float FloorHeight = 330.f;
	static constexpr int32 ExpectedThugs = 4;
	static constexpr int32 ExpectedDoors = 3;
	static constexpr int32 ExpectedExits = 2;
	static constexpr int32 ExpectedAnchors = 2;
	static constexpr int32 MinLamps = 20;

	/** The walk from the entrance up the stairs to the gallery, feet XY, in order (Sample.json). */
	static const FVector2D StairRoute[] = {
		FVector2D(650.f, 450.f), FVector2D(650.f, 690.f), FVector2D(565.f, 725.f), FVector2D(565.f, 1140.f),
		FVector2D(735.f, 1140.f), FVector2D(735.f, 760.f), FVector2D(735.f, 690.f) };
	/** Through the stair's upper door (x 800, y 685) into the gallery. */
	static const FVector2D GalleryRoute[] = { FVector2D(900.f, 685.f), FVector2D(1020.f, 685.f) };
	/** The gallery (a mezzanine over the hall, x 800 to 1200 on floor 1) and the hall's open floor. */
	static const FBox2D Gallery(FVector2D(800.f, 0.f), FVector2D(1200.f, 1400.f));
	static const FBox2D Stair(FVector2D(500.f, 600.f), FVector2D(800.f, 1220.f));
	static const FVector MantleStart(1545.f, 640.f, 0.f);   // between the crates and the rows of chairs
	/** The tops she climbs to the balcony by: the low crate, the tall one, the gallery behind the balustrade. */
	static const FVector MantleSteps[] = { FVector(1362.f, 640.f, 120.f), FVector(1262.f, 640.f, 240.f),
		FVector(1120.f, 640.f, FloorHeight) };

	static const FVector GrappleStart(1750.f, 1000.f, 0.f);
	static const FVector RoofDoorApproach(1000.f, 230.f, FloorHeight);

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

	static AHawkeyeCharacter* FindPlayer(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	static bool IsMap(const UWorld* World, const TCHAR* Path)
	{
		// An editor-context run opens the map as a PIE copy, /Game/Maps/UEDPIE_0_L_Int_Sample.
		return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Path;

	}

	template <typename T>
	static TArray<T*> All(UWorld* World)
	{
		TArray<T*> Out;
		for (TActorIterator<T> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		return Out;
	}

	static AActor* FindTagged(UWorld* World, FName Tag)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static int32 CountTagged(UWorld* World, FName Tag)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count += It->Tags.Contains(Tag) ? 1 : 0;
		}
		return Count;
	}

	static FVector Feet(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	static UEnhancedInputLocalPlayerSubsystem* InputOf(APlayerController* PC)
	{
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		return Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	}

	static void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* Action = LoadObject<UInputAction>(nullptr, Path);
		if (Input && Action)
		{
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
		}
	}

	static void Hold(APlayerController* PC, const TCHAR* Path, bool bHold, const FInputActionValue& Value = FInputActionValue(true))
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* Action = LoadObject<UInputAction>(nullptr, Path);
		if (!Input || !Action)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(Action, Value, {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Action);
		}
	}

	/** Forward on the stick, the camera turned to Yaw: she runs that way. */
	static void Steer(APlayerController* PC, float Yaw, float Pitch = -8.f)
	{
		if (PC)
		{
			PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		}
	}

	static void StopInput(APlayerController* PC)
	{
		Hold(PC, MovePath, false);
		Hold(PC, SprintPath, false);
	}

	/** Puts Kate's feet at Where facing Yaw, the camera behind her. */
	static void Place(UWorld* World, const FVector& Where, float Yaw, float Pitch = -8.f)
	{
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Kate)
		{
			return;
		}
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Where + FVector(0.f, 0.f, Half + 2.f), FRotator(0.f, Yaw, 0.f));
		Kate->GetCharacterMovement()->StopMovementImmediately();
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		PC->SetViewTarget(Kate);
	}

	/** Thugs keep patrolling but go Calm and stay so (what a side challenge does round its start). */
	static void Pacify(UWorld* World)
	{
		for (TActorIterator<AThugAIController> It(World); It; ++It)
		{
			if (!It->IsPacified())
			{
				It->SetPacified(true);
			}
		}
	}

	static bool AnyThugAlerted(UWorld* World)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->GetAlertState() == EThugAlertState::Alerted)
			{
				return true;
			}
		}
		return false;
	}

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	/** The chapter's quiver, for a map opened straight into (no door carried one in). */
	static void GiveQuiver(UWorld* World)
	{
		AHawkeyeCharacter* Kate = FindPlayer(World);
		UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
		if (!Inventory || Inventory->HasBow())
		{
			return;
		}
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard"));
		Standard.Count = 30;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple"));
		Grapple.Count = 6;
		Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate")),
			{ Standard, Grapple });
	}

	static FString ShotPath(const TCHAR* Folder, const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / Folder / FileName);
	}
}

// --- The map loads playable ---------------------------------------------------------------------------

/**
 * Opens L_Int_Sample and checks it is a playable interior: Kate is the pawn with the indoor camera arm, no
 * snow, a navmesh, the four patrolling thugs (and at least one of them walking a few seconds in), three
 * doors (the vault's locked to the keycard the archer carries), the two exits back to the district, the
 * lamps the EMP can kill, and the two grapple anchors on the gallery rail. The enemies in detail are
 * Hawkeye.Interior.EnemiesInPlace (InteriorAITest.cpp).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSmokeLoadInteriorSample, "Hawkeye.Smoke.LoadInteriorSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSmokeLoadInteriorSample::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorMap;
	TSharedRef<TArray<TPair<TWeakObjectPtr<AActor>, FVector>>> Starts = MakeShared<TArray<TPair<TWeakObjectPtr<AActor>, FVector>>>();
	// A fresh copy: the interior AI tests leave bodies and open doors in the last one.
	AutomationOpenMap(MapPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		return IsMap(World, MapPath) && FindPlayer(World) && Nav && Nav->GetDefaultNavDataInstance();
	}, [this]()
	{
		AddError(TEXT("L_Int_Sample did not come up with a player and a navmesh within 20 s."));
		return true;
	}, 20.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Starts]()
	{
		UWorld* World = FindWorld();
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!TestNotNull(TEXT("A Hawkeye character is the pawn"), Kate))
		{
			return true;
		}
		TestTrue(TEXT("The indoor camera arm is on"), Kate->IsIndoorCamera());
		TestFalse(TEXT("No snow indoors"), AHawkeyeGameMode::WantsOutdoorWeather(World));
		TestTrue(TEXT("The game mode says interior"), AHawkeyeGameMode::WantsInteriorCamera(World));
		TestEqual(TEXT("Four thugs"), All<AThugCharacter>(World).Num(), ExpectedThugs);
		for (AThugCharacter* Thug : All<AThugCharacter>(World))
		{
			TestTrue(FString::Printf(TEXT("%s patrols two points or more"), *Thug->GetName()), Thug->PatrolPoints.Num() >= 2);
			Starts->Emplace(Thug, Thug->GetActorLocation());
		}
		// They patrol; they must not start a fight (a death here would load a save and change the map).
		Pacify(World);
		if (AHawkeyeCharacter* Player = FindPlayer(World); Player && Player->GetHealthComponent())
		{
			Player->GetHealthComponent()->SetInvulnerable(true);
		}
		const TArray<ADoorActor*> Doors = All<ADoorActor>(World);
		TestEqual(TEXT("Three doors"), Doors.Num(), ExpectedDoors);
		const ADoorActor* const* Locked = Doors.FindByPredicate([](const ADoorActor* Door) { return Door->bLocked; });
		TestTrue(TEXT("One of them is locked to the vault keycard"), Locked && (*Locked)->RequiredKeycardId == FName(TEXT("vault")));
		const TArray<APickupActor*> Pickups = All<APickupActor>(World);
		TestTrue(TEXT("The vault keycard is in a thug's pocket"), Pickups.Num() == 1
			&& Pickups[0]->PickupType == EPickupType::Keycard && Pickups[0]->KeycardId == FName(TEXT("vault")) && Pickups[0]->IsCarried());
		const TArray<AInteriorExit*> Exits = All<AInteriorExit>(World);
		TestEqual(TEXT("Two exits"), Exits.Num(), ExpectedExits);
		TestTrue(TEXT("One goes to the roof"), Exits.ContainsByPredicate([](const AInteriorExit* Exit)
		{
			return Exit->ReturnPointOverride == RoofTag;
		}));
		TestTrue(TEXT("Every exit can find the district when opened directly"), !Exits.ContainsByPredicate([](const AInteriorExit* Exit)
		{
			return Exit->FallbackDistrict.IsNull() || Exit->FallbackReturnPoint != DoorstepTag;
		}));
		const int32 Lamps = CountTagged(World, TEXT("CityLamp"));
		TestTrue(FString::Printf(TEXT("At least %d lamp actors the EMP kills (%d)"), MinLamps, Lamps), Lamps >= MinLamps);
		TestEqual(TEXT("Two grapple anchors on the gallery rail"), All<AGrappleAnchor>(World).Num(), ExpectedAnchors);
		TestNotNull(TEXT("A traversable ledge on the balustrade"), FindTagged(World, TEXT("IntLedge")));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Starts]()
	{
		int32 Moved = 0;
		for (const TPair<TWeakObjectPtr<AActor>, FVector>& Start : *Starts)
		{
			const AActor* Thug = Start.Key.Get();
			Moved += Thug && FVector::Dist2D(Thug->GetActorLocation(), Start.Value) > 100.f ? 1 : 0;
		}
		TestTrue(TEXT("Still in the interior"), IsMap(FindWorld(), MapPath));

		TestTrue(FString::Printf(TEXT("Thugs walk their patrols (%d of %d moved)"), Moved, Starts->Num()), Moved >= 1);
		return true;
	}));
	return true;
}

// --- The stairs are walkable ------------------------------------------------------------------------------

/**
 * A navmesh path from the lobby (the PlayerStart) to the gallery (a grapple anchor's landing point on floor 1)
 * exists, is complete, and climbs through the stair well: the stairs' risers are low enough for the navmesh
 * agent (35 cm) and the landings join up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorStairsNavPath, "Hawkeye.Interior.StairsNavPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInteriorStairsNavPath::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorMap;
	AutomationOpenMap(MapPath);
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!IsMap(World, MapPath) || !FindPlayer(World))
		{
			return false;
		}
		// Nothing may start a fight while the navmesh builds: a death would load a save and change the map.
		Pacify(World);
		return Nav && Nav->GetDefaultNavDataInstance() && !Nav->IsNavigationBuildInProgress();
	}, [this]()

	{
		AddError(TEXT("No navmesh in L_Int_Sample within 20 s."));
		return true;
	}, 20.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		const TArray<APlayerStart*> Starts = All<APlayerStart>(World);
		const TArray<AGrappleAnchor*> Anchors = All<AGrappleAnchor>(World);
		if (!TestEqual(TEXT("One PlayerStart"), Starts.Num(), 1) || !TestTrue(TEXT("An anchor on the gallery"), Anchors.Num() > 0))
		{
			return true;
		}
		const FVector From = Starts[0]->GetActorLocation();
		const FVector To = Anchors[0]->GetLandingLocation() + FVector(0.f, 0.f, 50.f);
		// Where along the walk the navmesh is, for a report when the path breaks.
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			TArray<FString> Probes;
			// Up flight 2 (x 735) from the mid landing to the top landing, then through the door: expected floor heights.
			const FVector Checks[] = { FVector(735.f, 1140.f, 165.f), FVector(735.f, 1000.f, 200.f), FVector(735.f, 950.f, 230.f),
				FVector(735.f, 900.f, 260.f), FVector(735.f, 850.f, 290.f), FVector(735.f, 800.f, 315.f), FVector(735.f, 700.f, 330.f),
				FVector(790.f, 685.f, 330.f), FVector(830.f, 685.f, 330.f), FVector(1000.f, 685.f, 330.f) };
			for (const FVector& Point : Checks)
			{
				FNavLocation Found;
				const bool bOn = Nav->ProjectPointToNavigation(Point + FVector(0.f, 0.f, 20.f), Found, FVector(30.f, 30.f, 40.f));
				Probes.Add(bOn ? FString::Printf(TEXT("(%.0f, %.0f) z %.0f"), Point.X, Point.Y, Found.Location.Z)
					: FString::Printf(TEXT("(%.0f, %.0f, %.0f) off"), Point.X, Point.Y, Point.Z));
			}

			AddInfo(TEXT("Navmesh along the walk: ") + FString::Join(Probes, TEXT("; ")));
		}

		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To);
		if (!TestTrue(TEXT("A path from the lobby to the gallery"), Path && Path->IsValid()))
		{
			return true;
		}
		TestFalse(TEXT("It is complete, not partial"), Path->IsPartial());
		float Top = -1.e6f;
		bool bThroughStair = false;
		for (const FVector& Point : Path->PathPoints)
		{
			Top = FMath::Max(Top, Point.Z);
			bThroughStair |= Stair.IsInside(FVector2D(Point)) && Point.Z > 60.f && Point.Z < FloorHeight - 40.f;
		}
		TestTrue(FString::Printf(TEXT("It reaches the gallery floor (top %.0f cm)"), Top), Top >= FloorHeight - 20.f);
		TestTrue(TEXT("It climbs through the stair well, between the floors"), bThroughStair);
		const FVector End = Path->PathPoints.Last();
		TestTrue(FString::Printf(TEXT("It ends on the gallery (%s)"), *End.ToCompactString()), Gallery.IsInside(FVector2D(End)));
		AddInfo(FString::Printf(TEXT("Lobby to gallery: %d points, %.0f cm."), Path->PathPoints.Num(), Path->GetPathLength()));
		return true;
	}));
	return true;
}

// --- The Interior screenshot pass -------------------------------------------------------------------------

namespace HawkeyeInteriorMap
{
	/** Queues: put Kate at Where facing Yaw and Pitch, settle, capture FileName into Saved/Screenshots/Interior. */
	static void AddShot(FAutomationTestBase* Test, const FVector& Where, float Yaw, float Pitch, const TCHAR* FileName)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Where, Yaw, Pitch]()
		{
			Place(FindWorld(), Where, Yaw, Pitch);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
		const FString Name(FileName);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Name]()
		{
			HawkeyeShots::Request(Test, ShotPath(TEXT("Interior"), Name), true);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}

	/** The sample's enemy with layout id Id (tagged enemy:<id> by generate_interior.py). */
	static AThugCharacter* Enemy(UWorld* World, const TCHAR* Id)
	{
		return Cast<AThugCharacter>(FindTagged(World, FName(*FString::Printf(TEXT("enemy:%s"), Id))));
	}

	/** Stands Thug with his feet at Where facing Yaw, stopped. */
	static void PlaceThug(AThugCharacter* Thug, const FVector& Where, float Yaw)
	{
		if (Thug)
		{
			Thug->TeleportTo(Where + FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), FRotator(0.f, Yaw, 0.f));
			Thug->GetCharacterMovement()->StopMovementImmediately();
		}
	}

	/** Queues the capture of FileName now, then half a second for it to land. */
	static void AddCapture(FAutomationTestBase* Test, const TCHAR* FileName)
	{
		const FString Name(FileName);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Name]()
		{
			HawkeyeShots::Request(Test, ShotPath(TEXT("Interior"), Name), true);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}
}

/**
 * The sample interior through Kate's camera, written to Saved/Screenshots/Interior/:
 *
 *   interior_lobby.png    inside the front door looking across the lobby at the counter
 *   interior_hall.png     from the stage end of the hall back over the display cases to the gallery above
 *   interior_stairs.png   at the foot of the stair, looking up the first flight to the mid landing
 *   interior_gallery.png  at the gallery's balustrade, looking down into the hall
 *   int_patrol.png        from the lobby side of the hall's archway, the bat thug walking his beat in the hall
 *   int_takedown.png      a quarter second into her takedown of the lobby thug from behind
 *   int_gallery_gunner.png on the gallery, the gunner at its rail turned to her with his pistol up, the indoor arm
 *   int_vault_open.png    the vault door slid open, from the hall, looking in
 *
 * Thugs are frozen for the pass (the bat thug walks, calm, for his shot). Run from the standalone game:
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Screenshot.Interior; Quit"
 * interior_entrance.png, the district door, is in the Kate pass (HawkeyeAddInteriorShots).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotInterior, "Hawkeye.Screenshot.Interior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotInterior::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorMap;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the interior screenshots. Run them from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(MapPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		return IsMap(World, MapPath) && FindPlayer(World);
	}, [this]()
	{
		AddError(TEXT("L_Int_Sample did not load within 20 s."));
		return true;
	}, 20.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		// At once: the lobby patrol would see her standing inside the door.
		for (TActorIterator<AThugAIController> It(FindWorld()); It; ++It)
		{
			It->SetThinkingEnabled(false);
		}
		if (AHawkeyeCharacter* Kate = FindPlayer(FindWorld()); Kate && Kate->GetHealthComponent())
		{
			Kate->GetHealthComponent()->SetInvulnerable(true);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		for (TActorIterator<AThugAIController> It(World); It; ++It)
		{
			It->SetThinkingEnabled(false);
		}
		GiveQuiver(World);
		return true;

	}));
	AddShot(this, FVector(400.f, 140.f, 0.f), 90.f, -6.f, TEXT("interior_lobby.png"));
	AddShot(this, FVector(1930.f, 700.f, 40.f), 180.f, 12.f, TEXT("interior_hall.png"));
	AddShot(this, FVector(565.f, 900.f, 83.f), 90.f, 12.f, TEXT("interior_stairs.png"));
	AddShot(this, FVector(1140.f, 520.f, FloorHeight), 20.f, -24.f, TEXT("interior_gallery.png"));

	// The bat thug on his beat down the hall (x 1500), calm, from just inside the archway off the lobby: the
	// shot waits for him to be walking, in the middle of her view. The lobby thug is moved off out of the way.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		PlaceThug(Enemy(World, TEXT("lobby")), FVector(250.f, 330.f, 0.f), 60.f);
		if (AThugAIController* Brain = Cast<AThugAIController>(Enemy(World, TEXT("hall")) ? Enemy(World, TEXT("hall"))->GetController() : nullptr))
		{
			Brain->SetPacified(true);
			Brain->SetThinkingEnabled(true);
		}
		Place(World, FVector(880.f, 300.f, 0.f), 35.f, -4.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		const AThugCharacter* Thug = Enemy(World, TEXT("hall"));
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!Thug || !Kate)
		{
			return true;
		}
		const FVector To = Thug->GetActorLocation() - Kate->GetActorLocation();
		const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(35.f, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X))));
		return Thug->GetVelocity().Size2D() > 150.f && Off < 22.f;
	}, []() { return true; }, 25.f));
	AddCapture(this, TEXT("int_patrol.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		const AThugCharacter* Thug = Enemy(FindWorld(), TEXT("hall"));
		AddInfo(FString::Printf(TEXT("int_patrol.png: the bat thug at %s, %.0f cm/s."), Thug ? *Thug->GetActorLocation().ToCompactString() : TEXT("?"),
			Thug ? Thug->GetVelocity().Size2D() : 0.f));
		if (AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr)
		{
			Brain->SetThinkingEnabled(false);
		}
		return true;
	}));

	// The lobby thug from behind: F, and the shot a quarter second into it.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		PlaceThug(Enemy(World, TEXT("lobby")), FVector(250.f, 330.f, 0.f), 60.f);
		Place(World, FVector(195.f, 235.f, 0.f), 60.f, -10.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		const AHawkeyeCharacter* Kate = FindPlayer(FindWorld());
		UTakedownComponent* Takedown = Kate ? Kate->GetTakedownComponent() : nullptr;
		TestTrue(TEXT("int_takedown.png: the takedown goes in"), Takedown && Takedown->TryTakedown());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
	AddCapture(this, TEXT("int_takedown.png"));

	// The gunner at the rail over the hall with his pistol up at her, from the floor 6 m out.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		AThugCharacter* Gunner = Enemy(World, TEXT("gallery"));
		// The bat thug put out of the frame.
		PlaceThug(Enemy(World, TEXT("hall")), FVector(1950.f, 1200.f, 0.f), 90.f);
		// On the gallery with him, 4.8 m along it from where the stair's door lets her out: on his floor the parapet
		// does not hide him (from the hall floor under it only his head shows, and from the stage 9 m off he was small
		// and washed pale by the pendants over the hall), and he turns from the rail to her.
		PlaceThug(Gunner, FVector(1155.f, 450.f, FloorHeight), 109.f);
		Place(World, FVector(1000.f, 900.f, FloorHeight), -71.f, -2.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		AThugCharacter* Gunner = Enemy(World, TEXT("gallery"));
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (Gunner && Kate)
		{
			Gunner->SetWeaponRaised(true, Kate->GetActorLocation() + FVector(0.f, 0.f, 30.f));
			Gunner->SetTelegraphGlint(true);
			AddInfo(FString::Printf(TEXT("int_gallery_gunner.png: indoor arm %s, arm %.0f cm."), Kate->IsIndoorCamera() ? TEXT("on") : TEXT("off"),
				Kate->GetCameraBoom() ? Kate->GetCameraBoom()->TargetArmLength : -1.f));
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	AddCapture(this, TEXT("int_gallery_gunner.png"));

	// The vault, opened, from the hall (the archer stepped off his beat so he is not in the doorway).
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		PlaceThug(Enemy(World, TEXT("vault")), FVector(1900.f, 600.f, 0.f), 180.f);
		for (ADoorActor* Door : All<ADoorActor>(World))
		{
			if (Door->bLocked)
			{
				Door->OpenNow(FindPlayer(World));
			}
		}
		return true;
	}));
	AddShot(this, FVector(2060.f, 1060.f, 0.f), 30.f, -6.f, TEXT("int_vault_open.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

// --- The district door, in the Kate pass ----------------------------------------------------------------

void HawkeyeAddInteriorShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeInteriorMap;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		const AInteriorEntrance* Door = nullptr;
		for (AInteriorEntrance* Entrance : All<AInteriorEntrance>(World))
		{
			Door = Entrance;
		}
		if (!Door)
		{
			Test->AddError(TEXT("No City_InteriorEntrance in the district."));
			return true;
		}
		HawkeyeFreezePartner(World);
		const FVector Out = Door->GetActorForwardVector();
		Place(World, Door->GetActorLocation() + Out * 650.f + FVector(0.f, 0.f, 5.f), (-Out).Rotation().Yaw, -4.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		HawkeyeShots::Request(Test, ShotPath(TEXT("Kate"), TEXT("interior_entrance.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

// --- The scripted walk --------------------------------------------------------------------------------

namespace HawkeyeInteriorMap
{
	struct FWalk
	{
		TArray<FString> Legs;
		double LegStart = 0.0;
		double LastProgress = 0.0;
		float BestDistance = 1.e9f;
		int32 Waypoint = 0;
		int32 JumpTaps = 0;
		double LastJump = 0.0;
		bool bThugAlerted = false;
		bool bFailed = false;
		FVector Entered = FVector::ZeroVector;
	};

	static void BeginLeg(TSharedRef<FWalk> Walk)
	{
		Walk->LegStart = FPlatformTime::Seconds();
		Walk->LastProgress = Walk->LegStart;
		Walk->BestDistance = 1.e9f;
		Walk->Waypoint = 0;
		Walk->JumpTaps = 0;
	}

	static void EndLeg(FAutomationTestBase* Test, TSharedRef<FWalk> Walk, const FString& Name, bool bOk, const FString& Detail)
	{
		const double Seconds = FPlatformTime::Seconds() - Walk->LegStart;
		Walk->Legs.Add(FString::Printf(TEXT("    {\"leg\": \"%s\", \"ok\": %s, \"seconds\": %.2f, \"detail\": \"%s\"}"), *Name,
			bOk ? TEXT("true") : TEXT("false"), Seconds, *Detail));
		Test->TestTrue(FString::Printf(TEXT("Interior walk: %s (%s)"), *Name, *Detail), bOk);
		Test->AddInfo(FString::Printf(TEXT("Interior walk: %s in %.1f s: %s"), *Name, Seconds, *Detail));
		Walk->bFailed |= !bOk;
	}

	/**
	 * One frame of running Kate along Route: steer the camera at the next point and hold forward; past a
	 * point within Reach cm, on to the next; stuck for a second, tap jump. True when the last is reached.
	 */
	static bool FollowRoute(UWorld* World, TSharedRef<FWalk> Walk, const TArray<FVector2D>& Route, float Reach = 45.f)
	{
		AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Kate || Walk->Waypoint >= Route.Num())
		{
			return true;
		}
		Walk->bThugAlerted |= AnyThugAlerted(World);
		const FVector2D Here(Kate->GetActorLocation());
		const FVector2D Target = Route[Walk->Waypoint];
		const float Distance = FVector2D::Distance(Here, Target);
		if (Distance <= Reach)
		{
			++Walk->Waypoint;
			Walk->BestDistance = 1.e9f;
			if (Walk->Waypoint >= Route.Num())
			{
				StopInput(PC);
				return true;
			}
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (Distance < Walk->BestDistance - 5.f)
		{
			Walk->BestDistance = Distance;
			Walk->LastProgress = Now;
		}
		else if (Now - Walk->LastProgress > 1.0 && Now - Walk->LastJump > 1.0)
		{
			Tap(PC, JumpPath);
			++Walk->JumpTaps;
			Walk->LastJump = Now;
		}
		const FVector2D Dir = (Target - Here).GetSafeNormal();
		Steer(PC, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
		Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
		return false;
	}

	static void AddRoute(FAutomationTestBase* Test, TSharedRef<FWalk> Walk, TArray<FVector2D> Route, const FString& Name,
		float Timeout)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]() { BeginLeg(Walk); return true; }));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Walk, Route]()
		{
			return FollowRoute(FindWorld(), Walk, Route);
		}, [Test, Walk, Name, Route]()
		{
			UWorld* World = FindWorld();
			StopInput(FindController(World));
			const AHawkeyeCharacter* Kate = FindPlayer(World);
			EndLeg(Test, Walk, Name, false, FString::Printf(TEXT("stuck before point %d of %d at %s"), Walk->Waypoint + 1, Route.Num(),
				Kate ? *Kate->GetActorLocation().ToCompactString() : TEXT("?")));
			return true;
		}, Timeout));
	}

	/** Interact while standing where she is: the zone of whatever is in front of her. */
	static bool InteractHere(UWorld* World)
	{
		AHawkeyeCharacter* Kate = FindPlayer(World);
		UInteractionComponent* Interaction = Kate ? Kate->GetInteractionComponent() : nullptr;
		if (!Interaction)
		{
			return false;
		}
		Interaction->RefreshFocus();
		return Interaction->TryInteract();
	}
}

/**
 * The sample interior played by a script in the standalone game:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Lap.InteriorWalk; Quit"
 *
 * Its own save slot. On the district Kate walks up to City_InteriorEntrance_Sample and uses it (fade, save,
 * L_Int_Sample opens); inside, the thugs keep patrolling but see and hear nothing. She walks from the
 * entrance through the lobby, up both flights of the stair, opens the stair's upper door and walks onto the
 * gallery; is put on the hall floor and sprints at the crates under the balcony until the mantles carry her
 * over the balustrade onto the gallery; is put back on the hall floor and grapples to the gallery rail's
 * anchor; then walks to the roof door and uses it, and the district comes back with her on the building's
 * roof. Every leg asserts where she ends up; no thug may go Alerted. Writes Saved/Automation/interior_walk.json.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapInteriorWalk, "Hawkeye.Lap.InteriorWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapInteriorWalk::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorMap;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the interior walk. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	TSharedRef<FWalk> Walk = MakeShared<FWalk>();
	AutomationOpenMap(DistrictPath);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));

	// The district door: walk up to it and use it.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		UWorld* World = FindWorld();
		UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		const TArray<AInteriorEntrance*> Doors = All<AInteriorEntrance>(World);
		if (!Save || !FindPlayer(World) || Doors.Num() != 1)
		{
			AddError(TEXT("No save subsystem, player, or exactly one interior entrance in the district."));
			Walk->bFailed = true;
			return true;
		}
		Save->SlotNameOverride = Slot;
		IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
		HawkeyeFreezePartner(World);
		const FVector Out = Doors[0]->GetActorForwardVector();
		Walk->Entered = Doors[0]->GetActorLocation();
		Place(World, Doors[0]->GetActorLocation() + Out * 400.f + FVector(0.f, 0.f, 20.f), (-Out).Rotation().Yaw);
		BeginLeg(Walk);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Walk]()
	{
		UWorld* World = FindWorld();
		if (Walk->bFailed)
		{
			return true;
		}
		const TArray<AInteriorEntrance*> Doors = All<AInteriorEntrance>(World);
		AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (Doors.Num() != 1 || !Kate)
		{
			return true;
		}
		if (Doors[0]->GetEntryZone()->IsOverlappingActor(Kate))
		{
			StopInput(PC);
			return InteractHere(World) || Doors[0]->IsTravelling();
		}
		Steer(PC, (-Doors[0]->GetActorForwardVector()).Rotation().Yaw);
		Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
		return false;
	}, [this, Walk]()
	{
		StopInput(FindController(FindWorld()));
		EndLeg(this, Walk, TEXT("enter from the district"), false, TEXT("never reached the door's zone or it would not open"));
		return true;
	}, 10.f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		if (!IsMap(World, MapPath) || !FindPlayer(World))
		{
			return false;
		}
		// At once: the lobby's patrol would see her in the doorway within the second.
		Pacify(World);
		return true;
	}, [this, Walk]()
	{
		EndLeg(this, Walk, TEXT("enter from the district"), false, TEXT("L_Int_Sample did not open within 25 s"));

		return true;
	}, 25.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		UWorld* World = FindWorld();
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		const bool bIn = IsMap(World, MapPath) && Kate && Save && Save->IsInInterior();
		EndLeg(this, Walk, TEXT("enter from the district"), bIn, bIn ? FString::Printf(TEXT("in L_Int_Sample at %s, indoor arm %s"),
			*Kate->GetActorLocation().ToCompactString(), Kate->IsIndoorCamera() ? TEXT("on") : TEXT("off")) : TEXT("not inside"));
		if (Kate && Kate->GetHealthComponent())
		{
			Kate->GetHealthComponent()->SetInvulnerable(true);
		}
		Pacify(World);
		return true;
	}));

	// Lobby to gallery on foot, up the stairs.
	AddRoute(this, Walk, TArray<FVector2D>(StairRoute, UE_ARRAY_COUNT(StairRoute)), TEXT("lobby to the stair's top landing"), 30.f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		UWorld* World = FindWorld();
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (Walk->bFailed || !Kate)
		{
			return true;
		}
		const FVector Feet = HawkeyeInteriorMap::Feet(Kate);
		EndLeg(this, Walk, TEXT("lobby to the stair's top landing"), Feet.Z > FloorHeight - 25.f && Stair.IsInside(FVector2D(Feet)),
			FString::Printf(TEXT("feet at %s after %d jump tap(s)"), *Feet.ToCompactString(), Walk->JumpTaps));
		const bool bOpened = InteractHere(World);
		TestTrue(TEXT("Interact at the top landing opens the stair's upper door"), bOpened);
		BeginLeg(Walk);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	AddRoute(this, Walk, TArray<FVector2D>(GalleryRoute, UE_ARRAY_COUNT(GalleryRoute)), TEXT("through the door onto the gallery"), 10.f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		const AHawkeyeCharacter* Kate = FindPlayer(FindWorld());
		if (!Kate)
		{
			return true;
		}
		const FVector Feet = HawkeyeInteriorMap::Feet(Kate);
		EndLeg(this, Walk, TEXT("through the door onto the gallery"), Gallery.IsInside(FVector2D(Feet)) && Feet.Z > FloorHeight - 25.f,
			FString::Printf(TEXT("feet at %s"), *Feet.ToCompactString()));
		return true;
	}));

	// Mantle the balcony from the hall: sprint at the crates under it.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]()
	{
		Place(FindWorld(), MantleStart, 180.f);
		BeginLeg(Walk);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Walk]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Kate)
		{
			return true;
		}
		Walk->bThugAlerted |= AnyThugAlerted(World);
		const FVector Feet = HawkeyeInteriorMap::Feet(Kate);
		const bool bBusy = Kate->GetParkourComponent() && Kate->GetParkourComponent()->IsBusy();
		if (Gallery.IsInside(FVector2D(Feet)) && Feet.Z > FloorHeight - 25.f && !bBusy
			&& Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			StopInput(PC);
			return true;
		}
		// Up the steps one at a time: at the low crate from the floor, the tall one from the low one, the
		// balustrade from the tall one. Steer at the middle of the next, and press jump close to its face.
		const FVector2D Here(Feet);
		const FVector2D Next = Feet.Z < MantleSteps[0].Z - 30.f ? FVector2D(MantleSteps[0])
			: Feet.Z < MantleSteps[1].Z - 30.f ? FVector2D(MantleSteps[1]) : FVector2D(MantleSteps[2]);
		const FVector2D Dir = (Next - Here).GetSafeNormal();
		Steer(PC, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
		Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
		const double Now = FPlatformTime::Seconds();
		if (Now - Walk->LastProgress > 1.0)
		{
			Walk->LastProgress = Now;
			UE_LOG(LogTemp, Display, TEXT("[Hawkeye] mantle leg: feet %s, speed %.0f, mode %d, busy %d, next %s"), *Feet.ToCompactString(),
				Kate->GetVelocity().Size2D(), static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue()), bBusy ? 1 : 0,
				*Next.ToString());
		}
		if (!bBusy && FVector2D::Distance(Here, Next) < 130.f && Now - Walk->LastJump > 0.6)

		{
			Tap(PC, JumpPath);
			++Walk->JumpTaps;
			Walk->LastJump = Now;
		}
		return false;
	}, [this, Walk]()
	{
		StopInput(FindController(FindWorld()));
		return true;
	}, 20.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		const AHawkeyeCharacter* Kate = FindPlayer(FindWorld());
		if (!Kate)
		{
			return true;
		}
		const FVector Feet = HawkeyeInteriorMap::Feet(Kate);
		EndLeg(this, Walk, TEXT("mantle the balcony from the hall"), Gallery.IsInside(FVector2D(Feet)) && Feet.Z > FloorHeight - 25.f,
			FString::Printf(TEXT("feet at %s, %d jump tap(s)"), *Feet.ToCompactString(), Walk->JumpTaps));
		return true;
	}));

	// Grapple from the hall floor to the gallery rail's anchor.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]()
	{
		Place(FindWorld(), GrappleStart, 180.f, 10.f);
		BeginLeg(Walk);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Walk]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		if (!PC || !Grapple)
		{
			return true;
		}
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
		{
			return false;
		}
		if (Walk->JumpTaps > 0)
		{
			// Fired and landed.
			return Kate->GetCharacterMovement()->IsMovingOnGround();
		}
		AGrappleAnchor* Nearest = nullptr;
		for (AGrappleAnchor* Anchor : All<AGrappleAnchor>(World))
		{
			if (!Nearest || FVector::Dist(Anchor->GetActorLocation(), Kate->GetActorLocation())
				< FVector::Dist(Nearest->GetActorLocation(), Kate->GetActorLocation()))
			{
				Nearest = Anchor;
			}
		}
		if (!Nearest)
		{
			return true;
		}
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Look = (Nearest->GetMarkerLocation() - Lens).Rotation();
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
		Grapple->RefreshTarget();
		if (Grapple->GetTargetAnchor() == Nearest)
		{
			Tap(PC, GrapplePath);
			Walk->JumpTaps = 1;
		}
		return false;
	}, [this, Walk]()
	{
		return true;
	}, 12.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		UWorld* World = FindWorld();
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!Kate)
		{
			return true;
		}
		const FVector Feet = HawkeyeInteriorMap::Feet(Kate);
		const bool bFired = Walk->JumpTaps > 0;
		EndLeg(this, Walk, TEXT("grapple to the gallery anchor"), bFired && Gallery.IsInside(FVector2D(Feet)) && Feet.Z > FloorHeight - 25.f,
			FString::Printf(TEXT("%s; feet at %s"), bFired ? TEXT("fired at the rail anchor") : TEXT("never had the anchor targeted"),
				*Feet.ToCompactString()));
		return true;
	}));

	// Out through the roof door.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]()
	{
		Place(FindWorld(), RoofDoorApproach, -90.f);
		BeginLeg(Walk);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!IsMap(World, MapPath) || !Kate)
		{
			return true;
		}
		for (AInteriorExit* Exit : All<AInteriorExit>(World))
		{
			if (Exit->ReturnPointOverride == RoofTag && Exit->GetEntryZone()->IsOverlappingActor(Kate))
			{
				StopInput(PC);
				return InteractHere(World) || Exit->IsTravelling();
			}
		}
		Steer(PC, -90.f);
		Hold(PC, MovePath, true, FInputActionValue(FVector2D(0.f, 1.f)));
		return false;
	}, [this, Walk]()
	{
		StopInput(FindController(FindWorld()));
		EndLeg(this, Walk, TEXT("out through the roof door"), false, TEXT("never reached the roof door's zone"));
		return true;
	}, 8.f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		return IsMap(World, DistrictPath) && FindPlayer(World) && Save && !Save->IsLoading() && !Save->GetSceneReturn().bReturnPending;
	}, [this, Walk]()
	{
		EndLeg(this, Walk, TEXT("out through the roof door"), false, TEXT("the district did not come back within 30 s"));
		return true;
	}, 30.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk]()
	{
		UWorld* World = FindWorld();
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		const AActor* Roof = FindTagged(World, RoofTag);
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		if (IsMap(World, DistrictPath) && Kate && Roof)
		{
			const float Off = FVector::Dist(Kate->GetActorLocation(), Roof->GetActorLocation());
			EndLeg(this, Walk, TEXT("out through the roof door"), Off < 150.f && Save && !Save->IsInInterior(),
				FString::Printf(TEXT("back on the district, %.0f cm from City_InteriorRoof_Sample, %.0f cm up"), Off,
					Kate->GetActorLocation().Z));
		}
		else
		{
			EndLeg(this, Walk, TEXT("out through the roof door"), false, TEXT("no district, player or roof point"));
		}
		TestFalse(TEXT("No thug was needed: none went Alerted"), Walk->bThugAlerted);
		const FString Json = FString::Printf(TEXT("{\n  \"test\": \"Hawkeye.Lap.InteriorWalk\",\n  \"thug_alerted\": %s,\n  \"legs\": [\n%s\n  ]\n}\n"),
			Walk->bThugAlerted ? TEXT("true") : TEXT("false"), *FString::Join(Walk->Legs, TEXT(",\n")));
		WriteText(TEXT("interior_walk.json"), Json);
		AddInfo(TEXT("interior_walk.json:\n") + Json);
		if (UHawkeyeSaveSubsystem* Saves = UHawkeyeSaveSubsystem::Get(World))
		{
			IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
			Saves->SlotNameOverride.Reset();
		}
		return true;
	}));
	return true;
}

#endif
