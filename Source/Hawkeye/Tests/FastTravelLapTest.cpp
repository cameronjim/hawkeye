#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTracker.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/FastTravelScreenshots.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/HawkeyeSafehouseWidget.h"
#include "World/InteractionComponent.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeFastTravelLap
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* Slot = TEXT("HawkeyeCampaignFastTravelLap");
	static const FName FirstId(TEXT("ch01_east_7th"));
	static const FName SecondId(TEXT("avenue_b"));
	static constexpr float DoorReach = 300.f;
	static constexpr float MaxTravelSeconds = 3.f;

	/** What the lap measured, for the JSON and the assertions. */
	struct FState
	{
		double WalkStartedAt = 0.0;
		double TravelPickedAt = 0.0;
		int32 SavesBefore = 0;
		TArray<FString> Legs;
		TArray<float> FadeSeconds;
		bool bRefused = false;
		bool bPartnerWasBenched = false;
	};

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

	static APawn* FindPartner(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyePartnerController* Partner = PC ? PC->FindPartnerController() : nullptr;
		return Partner ? Partner->GetPawn() : nullptr;
	}

	static ASafehouse* FindSafehouse(UWorld* World, FName Id)
	{
		const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
		return Safehouses ? Safehouses->FindSafehouse(Id) : nullptr;
	}

	/** Puts the player Distance in front of safehouse Id's door, facing it. */
	static bool PlaceBeforeDoor(UWorld* World, FName Id, float Distance, float Pitch)
	{
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Player = FindPlayer(World);
		const ASafehouse* Safehouse = FindSafehouse(World, Id);
		if (!PC || !Player || !Safehouse)
		{
			return false;
		}
		const FVector Out = Safehouse->GetActorForwardVector();
		const FRotator Facing(0.f, (-Out).Rotation().Yaw, 0.f);
		Player->TeleportTo(Safehouse->GetActorLocation() + Out * Distance + FVector(0.f, 0.f, 100.f), Facing);
		PC->SetControlRotation(FRotator(Pitch, Facing.Yaw, 0.f));
		PC->SetViewTarget(Player);
		return true;
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

	static void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	/** Opens safehouse Id's menu the way a player does: Interact while standing in its entry zone. */
	static bool OpenMenuAt(UWorld* World, FAutomationTestBase* Test)
	{
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		UInteractionComponent* Interaction = Kate ? Kate->GetInteractionComponent() : nullptr;
		if (!PC || !Interaction)
		{
			Test->AddError(TEXT("No player to open the safehouse menu."));
			return false;
		}
		Interaction->RefreshFocus();
		return Interaction->TryInteract() && PC->IsSafehouseMenuOpen();
	}

	/** Queues: walk in to safehouse Id from 5 m out until it is discovered. */
	static void AddWalkIn(FAutomationTestBase* Test, TSharedRef<FState> State, FName Id)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Id]()
		{
			UWorld* World = FindWorld();
			if (!PlaceBeforeDoor(World, Id, 500.f, -8.f))
			{
				Test->AddError(FString::Printf(TEXT("No safehouse %s in the district."), *Id.ToString()));
			}
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
		{
			UWorld* World = FindWorld();
			State->WalkStartedAt = World ? World->GetTimeSeconds() : 0.0;
			Walk(FindController(World), true);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Test, State, Id]()
		{
			UWorld* World = FindWorld();
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			if (!Save || !Save->IsSafehouseDiscovered(Id))
			{
				return false;
			}
			Walk(FindController(World), false);
			Test->AddInfo(FString::Printf(TEXT("Fast travel lap: walked into %s in %.1f s; discovered."), *Id.ToString(),
				World->GetTimeSeconds() - State->WalkStartedAt));
			return true;
		}, [Test, Id]()
		{
			Walk(FindController(FindWorld()), false);
			Test->AddError(FString::Printf(TEXT("Walking in did not discover %s within 8 s."), *Id.ToString()));
			return true;
		}, 8.f));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}

	/** Queues: open the menu at From, pick the list's only row (To), wait for the travel, check it. */
	static void AddTravel(FAutomationTestBase* Test, TSharedRef<FState> State, FName From, FName To)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, From, To]()
		{
			UWorld* World = FindWorld();
			AHawkeyePlayerController* PC = FindController(World);
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			if (!PC || !Save || !Test->TestTrue(FString::Printf(TEXT("Interact at %s opens its menu"), *From.ToString()), OpenMenuAt(World, Test)))
			{
				return true;
			}
			PC->SafehouseFastTravel();
			UHawkeyeSafehouseWidget* Menu = PC->GetSafehouseWidget();
			if (!Menu || !Test->TestTrue(TEXT("Fast travel shows the list"), Menu->IsListShown()))
			{
				return true;
			}
			const ASafehouse* Destination = FindSafehouse(World, To);
			Test->TestEqual(TEXT("One other safehouse is listed"), Menu->GetListRowCount(), 1);
			Test->TestTrue(TEXT("It is the destination, by name, and can be picked"), Destination && Menu->GetListRow(0).bEnabled
				&& Menu->GetListRow(0).Label.EqualTo(Destination->GetDisplayName()));
			State->SavesBefore = Save->GetSaveCount();
			State->TravelPickedAt = FPlatformTime::Seconds();
			Test->TestTrue(TEXT("Picking it starts the travel"), Menu->PickListRow(0));
			const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
			Test->TestTrue(TEXT("The travel is under way and the menu is closed"), Safehouses && Safehouses->IsTravelling()
				&& !PC->IsSafehouseMenuOpen());
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
		{
			const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(FindWorld());
			return !Safehouses || !Safehouses->IsTravelling();
		}, [Test]()
		{
			Test->AddError(TEXT("The fast travel did not finish within 6 s."));
			return true;
		}, 6.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, From, To]()
		{
			UWorld* World = FindWorld();
			const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
			const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
			const ASafehouse* Destination = FindSafehouse(World, To);
			const AHawkeyeCharacter* Kate = FindPlayer(World);
			const APawn* Clint = FindPartner(World);
			if (!Safehouses || !Save || !Destination || !Kate)
			{
				Test->AddError(TEXT("No safehouse, save or player after the travel."));
				return true;
			}
			const FVector Door = Destination->GetActorLocation();
			const float KateOff = FVector::Dist(Kate->GetActorLocation(), Door);
			const float ClintOff = Clint ? FVector::Dist(Clint->GetActorLocation(), Door) : -1.f;
			const float Fade = Safehouses->GetLastTravelSeconds();
			const double Picked = FPlatformTime::Seconds() - State->TravelPickedAt;
			Test->TestTrue(FString::Printf(TEXT("Kate at %s's door (%.0f cm)"), *To.ToString(), KateOff), KateOff <= DoorReach);
			Test->TestTrue(FString::Printf(TEXT("Clint beside her (%.0f cm from the door)"), ClintOff), Clint && ClintOff <= DoorReach);
			Test->TestTrue(FString::Printf(TEXT("Fade to fade under 3 s (%.2f s)"), Fade), Fade > 0.f && Fade < MaxTravelSeconds);
			Test->TestEqual(TEXT("The destination is the last-used safehouse"), Save->GetLastSafehouse(), To);
			Test->TestTrue(TEXT("The travel autosaved"), Save->GetSaveCount() > State->SavesBefore);
			State->FadeSeconds.Add(Fade);
			State->Legs.Add(FString::Printf(TEXT("    {\"from\": \"%s\", \"to\": \"%s\", \"fade_to_fade\": %.3f, \"pick_to_control\": %.3f, "
				"\"kate_cm\": %.0f, \"clint_cm\": %.0f}"), *From.ToString(), *To.ToString(), Fade, Picked, KateOff, ClintOff));
			Test->AddInfo(FString::Printf(TEXT("Fast travel %s -> %s: %.2f s fade to fade, Kate %.0f cm and Clint %.0f cm from the door."),
				*From.ToString(), *To.ToString(), Fade, KateOff, ClintOff));
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}
}

/**
 * Fast travel played by a script on the district, in the standalone game:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Lap.FastTravel; Quit"
 *
 * Its own save slot, no safehouse found. Kate walks into [Safehouse 1] and then [Safehouse 2] (each put
 * 5 m out, walking at the door until the entry zone discovers it); a challenge started under her
 * refuses fast travel; then from [Safehouse 2]'s menu she travels to [Safehouse 1] and back. Each
 * travel must leave Kate and Clint (brought off Act 1's bench for the lap) within 3 m of the destination's door, take under 3 s fade to fade,
 * autosave and record the destination as last used. Writes Saved/Automation/fast_travel.json.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapFastTravel, "Hawkeye.Lap.FastTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapFastTravel::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFastTravelLap;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the fast travel lap. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	TSharedRef<FState> State = MakeShared<FState>();
	AutomationOpenMap(MapPath);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = FindWorld();
		UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		if (!Save || !FindPlayer(World) || !FindSafehouse(World, FirstId) || !FindSafehouse(World, SecondId))
		{
			AddError(TEXT("No save subsystem, player, or the two safehouses in the district."));
			return true;
		}
		Save->SlotNameOverride = Slot;
		IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
		Save->GetCampaignState()->DiscoveredSafehouses.Reset();
		// Act 1's chapter benches Clint (DA_CH01_NightShift has no partner), and a benched partner is off stage and
		// does not travel. The lap is about the travel taking both of them, so it brings him on for itself and puts
		// the chapter's cast back at the end.
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			if (It->IsBenched())
			{
				State->bPartnerWasBenched = true;
				It->SetBenched(false);
			}
		}
		HawkeyeFreezePartner(World);
		TestNotNull(TEXT("Clint is on stage for the lap"), FindPartner(World));
		return true;
	}));

	AddWalkIn(this, State, FirstId);
	AddWalkIn(this, State, SecondId);

	// A challenge on: the list is refused with the toast, and nothing moves.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
		const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Challenges || !Challenges->GetTracker() || !Safehouses || !Kate)
		{
			AddError(TEXT("No challenge subsystem for the refusal check."));
			return true;
		}
		UChallengeDefinition* Route = NewObject<UChallengeDefinition>();
		Route->Id = TEXT("fast_travel_lap_route");
		Route->Type = EChallengeType::Traversal;
		Route->TimeLimitSeconds = 150.f;
		Route->AreaRadius = 100000.f;
		Route->Checkpoints.Add(FTransform(Kate->GetActorLocation() + FVector(5000.f, 0.f, 0.f)));
		Route->CheckpointLegs.Add(EChallengeLeg::Run);
		const FVector Before = Kate->GetActorLocation();
		TestTrue(TEXT("A challenge starts"), Challenges->GetTracker()->StartRun(Route));
		TestTrue(TEXT("Fast travel is refused during it"), !PC->FastTravelTo(FirstId) && !Safehouses->IsTravelling());
		TestEqual(TEXT("With the toast's words"), Safehouses->GetTravelRefusalNow().ToString(), FString(TEXT("[Can't fast travel now]")));
		TestTrue(TEXT("And Kate did not move"), FVector::Dist(Before, Kate->GetActorLocation()) < 1.f);
		Challenges->GetTracker()->AbortRun();
		PC->CloseChallengeResults();
		State->bRefused = true;
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	AddTravel(this, State, SecondId, FirstId);
	AddTravel(this, State, FirstId, SecondId);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		float Worst = 0.f;
		for (const float Seconds : State->FadeSeconds)
		{
			Worst = FMath::Max(Worst, Seconds);
		}
		const FString Json = FString::Printf(TEXT("{\n  \"test\": \"Hawkeye.Lap.FastTravel\",\n  \"refused_during_challenge\": %s,\n"
			"  \"worst_fade_to_fade\": %.3f,\n  \"legs\": [\n%s\n  ]\n}\n"), State->bRefused ? TEXT("true") : TEXT("false"), Worst,
			*FString::Join(State->Legs, TEXT(",\n")));
		WriteText(TEXT("fast_travel.json"), Json);
		AddInfo(TEXT("fast_travel.json:\n") + Json);
		UE_LOG(LogTemp, Display, TEXT("[Hawkeye] fast travel: %s"), *Json);
		TestEqual(TEXT("Both legs travelled"), State->FadeSeconds.Num(), 2);
		if (State->bPartnerWasBenched)
		{
			for (TActorIterator<AHawkeyePartnerController> It(FindWorld()); It; ++It)
			{
				It->SetBenched(true);
			}
		}
		if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(FindWorld()))
		{
			IFileManager::Get().Delete(*USpudSubsystem::GetSaveGameFilePath(Slot), false, true, true);
			Save->SlotNameOverride.Reset();
		}
		return true;
	}));
	return true;
}

// --- The fast-travel pass of the Kate screenshots ----------------------------------------------------

void HawkeyeAddFastTravelShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeFastTravelLap;

	// Before either safehouse is found: on the street below a challenge pedestal, "Mark nearest safehouse"
	// marks an unfound one ("?" and "[Unknown safehouse]"), the compass has both houses hollow and the
	// pedestal's medal, and the pedestal has its world marker.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World);
		if (PC && Kate && Kate->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->bAllowSwitchingOverride = true;
			PC->SwitchCharacter();
			Kate = FindPlayer(World);
		}
		HawkeyeFreezePartner(World);
		const ASafehouse* Second = FindSafehouse(World, SecondId);
		const AChallengeStart* Pedestal = Challenges && Second ? Challenges->FindNearestStart(Second->GetActorLocation()) : nullptr;
		if (!PC || !Kate || !Pedestal)
		{
			Test->AddError(TEXT("compass_markers.png: no player or no challenge pedestal."));
			return true;
		}
		// 50 to 75 m from the pedestal on a street or sidewalk, looking up at it.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CompassMarkersShot), false, Kate);
		bool bPlaced = false;
		for (int32 Step = 0; Step < 48 && !bPlaced; ++Step)
		{
			const float Distance = Step < 24 ? 5500.f : 7000.f;
			const FVector Probe = Pedestal->GetActorLocation() + FRotator(0.f, 15.f * Step, 0.f).Vector() * Distance;
			FHitResult Hit;
			const AActor* Ground = World->LineTraceSingleByChannel(Hit, FVector(Probe.X, Probe.Y, 20000.f),
				FVector(Probe.X, Probe.Y, -500.f), ECC_Visibility, Params) ? Hit.GetActor() : nullptr;
			if (!Ground || !(Ground->ActorHasTag(TEXT("CityRoad")) || Ground->ActorHasTag(TEXT("CitySidewalk"))))
			{
				continue;
			}
			bPlaced = true;
			const FVector ToPedestal = Pedestal->GetActorLocation() - Hit.ImpactPoint;
			const FRotator Facing(0.f, ToPedestal.Rotation().Yaw, 0.f);
			Kate->TeleportTo(Hit.ImpactPoint + FVector(0.f, 0.f, 100.f), Facing);
			PC->SetControlRotation(FRotator(FMath::Clamp(ToPedestal.Rotation().Pitch, 0.f, 20.f), Facing.Yaw, 0.f));
		}
		Test->TestTrue(TEXT("A street by the pedestal to stand on"), bPlaced);
		Test->TestTrue(TEXT("Mark nearest safehouse marks one before any is found"), PC->MarkNearestSafehouse());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		const AHawkeyePlayerController* PC = FindController(World);
		const UHawkeyeHudWidget* Hud = PC ? PC->GetHawkeyeHud() : nullptr;
		const UHawkeyeObjectiveWidget* Compass = Hud ? Hud->GetObjectiveMarker() : nullptr;
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		const bool bNoneFound = Save && Save->GetDiscoveredSafehouses().Num() == 0;
		Test->TestEqual(TEXT("Both safehouses are on the compass, found or not"), Compass ? Compass->GetCompassSafehouseCount() : 0, 2);
		Test->TestTrue(TEXT("The pedestal's medal is on the compass"), Compass && Compass->GetCompassChallengeCount() >= 1);
		if (bNoneFound)
		{
			Test->TestFalse(TEXT("Unfound houses are hollow"), Compass && Compass->IsCompassSafehouseDiscovered(0));
			Test->TestEqual(TEXT("The marker names it the unknown safehouse"), Compass ? Compass->GetMarkedSafehouseLabel().ToString() : FString(),
				FString(TEXT("[Unknown safehouse]")));
		}
		Test->AddInfo(FString::Printf(TEXT("compass_markers.png: %d house(s), %d medal(s), %d pedestal marker(s), marked \"%s\"."),
			Compass ? Compass->GetCompassSafehouseCount() : 0, Compass ? Compass->GetCompassChallengeCount() : 0,
			Compass ? Compass->GetChallengeMarkerCount() : 0, Compass ? *Compass->GetMarkedSafehouseLabel().ToString() : TEXT("")));
		HawkeyeShots::Request(Test, ShotPath(TEXT("compass_markers.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(FindWorld()))
		{
			Safehouses->ClearSafehouseMarker();
		}
		return true;
	}));

	// The second door from across the pavement.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		const AHawkeyeCharacter* Player = FindPlayer(World);
		if (PC && Player && Player->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->bAllowSwitchingOverride = true;
			PC->SwitchCharacter();
		}
		HawkeyeFreezePartner(World);
		if (!PlaceBeforeDoor(World, SecondId, 600.f, -6.f))
		{
			Test->AddError(TEXT("No second safehouse (City_Safehouse_2) in the district."));
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		HawkeyeShots::Request(Test, ShotPath(TEXT("safehouse2_door.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// Walk-in discovers the second; the first is marked found as the save pass would; the menu's list.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World))
		{
			Save->DiscoverSafehouse(FirstId);
		}
		PlaceBeforeDoor(World, SecondId, 150.f, -6.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		Test->TestTrue(TEXT("Walking up to the second safehouse discovered it"), Save && Save->IsSafehouseDiscovered(SecondId));
		if (PC && OpenMenuAt(World, Test))
		{
			PC->SafehouseFastTravel();
		}
		Test->TestTrue(TEXT("The fast-travel list is up"), PC && PC->GetSafehouseWidget() && PC->GetSafehouseWidget()->IsListShown());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.7f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		HawkeyeShots::Request(Test, ShotPath(TEXT("fasttravel_list.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// Between the two, far enough out that both houses sit on the compass, the nearer one marked.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		const ASafehouse* First = FindSafehouse(World, FirstId);
		const ASafehouse* Second = FindSafehouse(World, SecondId);
		if (!PC || !Kate || !First || !Second)
		{
			return true;
		}
		PC->CloseSafehouseMenu();
		const FVector Mid = (First->GetActorLocation() + Second->GetActorLocation()) * 0.5f;
		const FVector Across = FVector::CrossProduct(Second->GetActorLocation() - First->GetActorLocation(), FVector::UpVector).GetSafeNormal2D();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FastTravelCompassShot), false, Kate);
		// On a street or the park inside the district (not a roof, not the bare slab past its edge), as far
		// off the line as that allows so the two houses sit apart on the strip.
		bool bPlaced = false;
		for (int32 Step = 0; Step < 24 && !bPlaced; ++Step)
		{
			const float Out = (Step % 2 == 0 ? 1.f : -1.f) * (16000.f - 600.f * (Step / 2));
			const FVector Probe = Mid + Across * Out;
			FHitResult Hit;
			const AActor* Ground = World->LineTraceSingleByChannel(Hit, Probe + FVector(0.f, 0.f, 20000.f), Probe - FVector(0.f, 0.f, 500.f),
				ECC_Visibility, Params) ? Hit.GetActor() : nullptr;
			const bool bStreet = Ground && (Ground->ActorHasTag(TEXT("CityRoad")) || Ground->ActorHasTag(TEXT("CitySidewalk")));
			if (!bStreet)
			{
				continue;
			}
			const FString Under = Ground->ActorHasTag(TEXT("CityRoad")) ? TEXT("a road") : TEXT("a sidewalk");
			bPlaced = true;
			const FRotator Facing(0.f, (Mid - Hit.ImpactPoint).Rotation().Yaw, 0.f);
			Kate->TeleportTo(Hit.ImpactPoint + FVector(0.f, 0.f, 100.f), Facing);
			PC->SetControlRotation(FRotator(-6.f, Facing.Yaw, 0.f));
			Test->AddInfo(FString::Printf(TEXT("Compass shot from %s on %s, %.0f m off the line between the safehouses."),
				*Hit.ImpactPoint.ToCompactString(), *Under, FMath::Abs(Out) / 100.f));
		}
		Test->TestTrue(TEXT("A street between the safehouses to stand on"), bPlaced);
		Test->TestTrue(TEXT("Mark nearest safehouse marks one"), PC->MarkNearestSafehouse());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		const AHawkeyePlayerController* PC = FindController(FindWorld());
		const UHawkeyeHudWidget* Hud = PC ? PC->GetHawkeyeHud() : nullptr;
		const UHawkeyeObjectiveWidget* Compass = Hud ? Hud->GetObjectiveMarker() : nullptr;
		Test->TestEqual(TEXT("Both safehouses are houses on the compass"), Compass ? Compass->GetCompassSafehouseCount() : 0, 2);
		HawkeyeShots::Request(Test, ShotPath(TEXT("compass_safehouses.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(FindWorld()))
		{
			Safehouses->ClearSafehouseMarker();
		}
		return true;
	}));
}

/** The fast-travel shots on their own: the compass markers, the second door, the list, both houses. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotFastTravel, "Hawkeye.Screenshot.FastTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotFastTravel::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the fast-travel screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddFastTravelShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
