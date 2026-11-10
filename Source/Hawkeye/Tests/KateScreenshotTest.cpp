#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "HawkeyePlayerController.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/ObjectiveTriggerVolume.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Tests/AutomationCommon.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UnrealClient.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "Tests/AccessibilityScreenshots.h"
#include "Tests/BowIKScreenshots.h"
#include "Tests/ChallengeScreenshots.h"
#include "Tests/CombatScreenshots.h"
#include "Tests/CrimeScreenshots.h"
#include "Tests/EnemyScreenshots.h"
#include "Tests/FastTravelScreenshots.h"
#include "Tests/HangScreenshots.h"
#include "Tests/InteriorScreenshots.h"
#include "Tests/HintScreenshots.h"
#include "Tests/MapScreenshots.h"
#include "Tests/MeleeScreenshots.h"
#include "Tests/NarrativeScreenshots.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/SaveScreenshots.h"
#include "Tests/VfxScreenshots.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Kate on the East Village block, seen through her own third-person camera, written to
 * Saved/Screenshots/Kate/. Look-at-them tools for tuning the camera, not assertions:
 *
 *   kate_street.png  hip camera, Kate on the street beside Tompkins Square Park, looking along it
 *   kate_aim.png     the same spot, aiming (camera in over the right shoulder)
 *   kate_roof.png    on the tallest roof near the start, looking across to the park
 *   kate_wall.png    back against a tenement, looking out: the arm has to pull in rather than
 *                    put the lens inside the building, and the body hides once the lens is on it
 *   kate_run.png     1.5 s into a run along the street, seen side on (IA_Move injected, so it goes
 *                    through the real input path and gait selection): the motion-matched run
 *                    pose, leaning, arms swinging
 *   kate_stop.png    0.3 s after letting go: the stop, not a snap to idle
 *   kate_aimstrafe.png aiming and strafing right, facing the camera's way
 *   kate_costume.png under the street lamp nearest the street spot, seen from a camera 2.3 m in front
 *                    of her (she turns to face her own camera, so it cannot be swung round): the black
 *                    suit's purple arm panels and chest chevron in lamp light
 *   night_street.png, day_street.png  kate_street's camera under hawkeye.TimeOfDay Night, then Day (the
 *                    setting is put back after; also on their own as Hawkeye.Screenshot.TimeOfDay, which
 *                    adds day_park.png, 15 m into the park facing the tenement row, and day_pedestal.png,
 *                    challenge_pedestal's framing by day)
 *
 *   grapple_marker.png  back on the street, looking up at a tenement anchor with its marker showing
 *   grapple_mid.png     part way along the zip, the camera following
 *   grapple_roof.png    landed on the anchor's roof
 *   vfx_zipline.png     60% along the same zip: the line from her hand to the anchor
 *   vfx_*.png           the effect shots, after the street frame time (VfxScreenshotTest.cpp)
 *
 *   kate_lookup.png  on the street 13 m out from a facade, the camera pitched straight at a rooftop
 *                    anchor: the arm shortens and lifts, so Kate stays out of the centre and the lens
 *                    off the pavement (the report gives the lens height and Kate's screen box)
 *   vault_mid.png    sprinting at City_Test_Vault (90 cm): the auto vault, mid-move
 *   mantle_mid.png   the jump key 36 cm from City_Test_Mantle (150 cm): hands on the top
 *   standing_vault.png  standing still, pressed against City_Test_Vault (90 cm), one press: mid-vault
 *   own_vault_mid.png, own_mantle_150.png, own_mantle_185.png, own_mantle_185_top.png  the same with the
 *                    sample's traversal off, so the moves are ours (the clip's root motion warped to the
 *                    probe): the vault, the 150 cm mantle (the 1 m clip) and a 185 cm copy of the mantle
 *                    block (the 2.5 m clip), mid-move and standing on top
 *   roof_edge_guard.png  walking at a tenement's roof-edge parapet, one jump press: the roof-edge guard
 *                    mantles her onto the parapet top and she stays there with the stick still held
 *   ledge_hang.png   dropped in against a tenement 230 cm under its parapet top: caught, hanging
 *   climb_top.png    the jump key from the hang: over the parapet onto the roof
 *
 *   fire_escape.png  from the street, a tenement facade with its zig-zag fire escape
 *   hang_drop.png    standing on a second-floor landing, over the rail to the hang (drop to hang)
 *   hang_shimmy.png  0.9 s into a shimmy along a tenement parapet toward a roof corner, hands staggered
 *   hang_corner.png  part way round that corner (ParkourHangDistrictTest.cpp)
 *   roof_corner_flush.png  that corner's parapet close up from its roof: one ring, the inside corner clean
 *   roof_corner_outside.png  the same corner from the air past it: both facades flush up through the parapet
 *   grapple_level.png  a level roof-to-roof zip across a street, part way along
 *
 *   bow_holstered.png  hip camera on the street: DA_Bow_Kate across Kate's back
 *   quiver_hud.png     the same frame for the hotbar: standard arrows "30", grapple "6/6"
 *   bow_draw.png       half drawn: bow in the left hand, the spread ring and the draw bar
 *   bow_hit.png        a full draw loosed at a thug 15 m down the street, stuck in him, reticle flashing
 *   arrow_stuck.png    that arrow from just behind and beside it, aimed in: shaft, three vanes, purple nock
 *   bow_hold.png, bow_aim_half.png, bow_aim_full.png, bow_aim_full_view.png, clint_draw.png,
 *   archer_draw_close.png: both hands on the bow (BowIKScreenshotTest.cpp)
 *
 *   roll_mid.png       0.2 s into the landing roll after a 6 m drop with the stick held, side on:
 *                      upside down in the tuck, motion blur off so the tumble does not smear away
 *
 *   street_patrol.png  across Avenue A from the StreetPair, patrolling the park-side sidewalk
 *   thug_walk_bat.png  4 m to the side of the StreetPair's bat thug as he walks: the bat hangs by his leg
 *   fight_roof.png     on the cross_block roof, the RoofPair alerted and rushing her
 *   fight_hit.png      her heavy landed: a thug knocked down (ragdoll)
 *   fight_dodge.png    mid-dodge, sideways from the thug still standing
 *
 *   objective_marker.png    at the PlayerStart looking toward reach_roof: the cream diamond with the
 *                           distance under it, and the compass strip at the top with the objective on it
 *   objective_offscreen.png the camera turned 150 degrees away: the marker held at the screen edge with
 *                           its arrow
 *   objective_complete.png  0.6 s after Kate is put inside City_Obj_reach_roof: the "Objective complete"
 *                           toast, looking on toward cross_block
 *   (these three come first, while reach_roof is still the current objective)
 *
 *   partner_follow.png, partner_shoot.png, banter.png, switch_clint.png: the partner pass
 *                           (PartnerScreenshotTest.cpp)
 *   squad_alert.png, gunner_cover.png, archer_draw.png, archer_arrow.png, archer_pickup.png: the
 *                           enemy pass (EnemyScreenshotTest.cpp)
 *   heavy_block.png, heavy_bash.png, archer_hold.png, thug_alert_glyph.png, aim_view_clear.png: the
 *                           fair-fight pass (CombatScreenshotTest.cpp)
 *   combo_x3.png, parry_flash.png, finisher_mid.png, hit_lean.png, strike_pose.png: the melee pass
 *                           (MeleeScreenshotTest.cpp)
 *   challenge_pedestal.png, archery_targets.png, challenge_results.png, traversal_checkpoint.png: the
 *                           side-challenge pass (ChallengeScreenshotTest.cpp)
 *   crime_marker.png, crime_mugging.png, crime_results.png: the street crime pass (CrimeScreenshotTest.cpp)
 *   settings_full.png, difficulty_prompt.png, subtitles_large.png, palette_deuteranopia.png,
 *   pedestal_dimmer.png: the difficulty and accessibility pass (AccessibilityScreenshotTest.cpp)
 *   safehouse2_door.png, fasttravel_list.png, compass_safehouses.png: the fast-travel pass (FastTravelLapTest.cpp)
 *   interior_entrance.png: the interior door on its district building (InteriorMapTest.cpp)
 *   map_open.png: the world map at the PlayerStart, icons, legend and a hovered safehouse (MapScreenshotTest.cpp)
 *   hint_sprint.png, hint_grapple.png, hint_parry.png: the first-time hint line (HintScreenshotTest.cpp)
 *   safehouse_door.png, safehouse_menu.png, main_menu.png, death_fade.png: the save pass
 *                           (SaveRoundTripTest.cpp)
 *   phone_open.png, chapter_title.png, dialogue_subtitle.png, chapter_end_closeup.png,
 *   scene_placeholder.png, chapter_complete.png: the narrative pass, run last; it completes the
 *                           chapter through its flashback's playable scene (NarrativeScreenshotTest.cpp)
 *
 * The district's placed thugs are frozen (thinking off) for every shot but their own, and Kate is
 * invulnerable through the roof fight so a swing cannot end the pass.
 *
 * Each shot reports how far the arm pulled in and warns if the lens is inside geometry. The pass
 * drops Kate 12 m onto the street and reports the landing height and health it cost, then fires
 * the grapple and reports the target, the zip's length, time and speed, and where she landed.
 * The average frame time over 60 frames is logged on the street and again on the roof.
 * Needs a real RHI; run from the standalone game (claude-docs/testing.md section 2b):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Kate; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotKate, "Hawkeye.Screenshot.Kate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

namespace HawkeyeKateShots
{
	static const FName BuildingTag(TEXT("CityBuilding"));
	static const FName ParkTag(TEXT("CityPark"));
	static constexpr float StreetOffsetFromPark = 450.f;
	static constexpr float RooftopSearchRadius = 12000.f;
	static constexpr float HipPitch = -12.f;

	enum class EShot : uint8
	{
		Street,
		Aim,
		Roof,
		Wall,
		Run,
		Stop,
		AimStrafe,
		EndMove,
		Costume,
	};

	/** The lamp light (tag CityLamp) nearest Near, 2D. */
	static ASpotLight* FindLampNear(UWorld* World, const FVector& Near)
	{
		ASpotLight* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<ASpotLight> It(World); It; ++It)
		{
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Near);
			if (It->Tags.Contains(TEXT("CityLamp")) && Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	static const TCHAR* MoveActionPath = TEXT("/Game/Input/IA_Move.IA_Move");

	/**
	 * Holds IA_Move at Value through Enhanced Input's continuous injection (every tick until
	 * stopped), or stops it when bHold is false. Injection runs the same triggers and bindings a
	 * key press would, so Input_Move picks the gait exactly as it does in play.
	 */
	static void HoldMove(APlayerController* PC, const FVector2D& Value, bool bHold)
	{
		const UInputAction* Move = LoadObject<UInputAction>(nullptr, MoveActionPath);
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = Player
			? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
		if (!Move || !Input)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Move);
		}
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
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

	static bool FindParkBounds(UWorld* World, FBox& OutBox)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParkTag))
			{
				OutBox = It->GetComponentsBoundingBox();
				return true;
			}
		}
		return false;
	}

	static bool FindTallestRoofNear(UWorld* World, const FVector& Origin, FVector& OutTopCentre)
	{
		float BestTop = -BIG_NUMBER;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(BuildingTag))
			{
				continue;
			}
			FVector Centre, Extent;
			It->GetActorBounds(false, Centre, Extent);
			if (FVector::Dist2D(Centre, Origin) <= RooftopSearchRadius && Centre.Z + Extent.Z > BestTop)
			{
				BestTop = Centre.Z + Extent.Z;
				OutTopCentre = FVector(Centre.X, Centre.Y, BestTop);
			}
		}
		return BestTop > -BIG_NUMBER;
	}

	/** The street spot beside the park on the side nearest the start, and the outward direction. */
	static bool FindStreetSpot(UWorld* World, FVector& OutSpot, FVector& OutAwayFromPark)
	{
		FBox Park;
		TActorIterator<APlayerStart> StartIt(World);
		if (!StartIt || !FindParkBounds(World, Park))
		{
			return false;
		}
		const FVector Start = StartIt->GetActorLocation();
		const FVector Edge = Park.GetClosestPointTo(Start);
		OutAwayFromPark = (Start - Edge).GetSafeNormal2D();
		if (OutAwayFromPark.IsNearlyZero())
		{
			OutAwayFromPark = FVector(0.f, 1.f, 0.f);
		}
		OutSpot = Edge + OutAwayFromPark * StreetOffsetFromPark;
		return true;
	}

	/** Drops a line from high above XY to whatever is below and returns the surface point. */
	static bool FindGround(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotGround), false, Ignore);
		FHitResult Hit;
		const FVector From(XY.X, XY.Y, FromZ);
		const FVector To(XY.X, XY.Y, -5000.f);
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	/** Stands Kate on Ground facing Yaw and looks through her own camera. */
	static void PlaceKate(AHawkeyeCharacter* Kate, APlayerController* PC, const FVector& Ground, float Yaw, float Pitch)
	{
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->StopAim();
		Kate->SetActorHiddenInGame(false);
		Kate->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		PC->SetViewTarget(Kate);
	}
}

/** Frame one shot: move Kate, set the view. The capture comes a beat later so lag settles. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateFrameShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeKateFrameShot::Update()
{
	using namespace HawkeyeKateShots;

	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	FVector Spot, Away;
	if (!Kate || !FindStreetSpot(World, Spot, Away))
	{
		Test->AddError(TEXT("No Kate, PlayerStart or CityPark in L_District_EastVillage."));
		return true;
	}

	// Along the street, with the park on Kate's left so the camera looks past it down the block.
	const float AlongYaw = FVector::CrossProduct(FVector::UpVector, Away).Rotation().Yaw;
	FVector Ground;
	switch (static_cast<EShot>(Shot))
	{
	case EShot::Street:
		if (FindGround(World, Spot, 3000.f, Kate, Ground))
		{
			PlaceKate(Kate, PC, Ground, AlongYaw, HipPitch);
		}
		break;

	case EShot::Aim:
		Kate->StartAim();
		break;

	case EShot::Roof:
	{
		FVector Top;
		if (FindTallestRoofNear(World, Spot, Top) && FindGround(World, Top, Top.Z + 500.f, Kate, Ground))
		{
			const float ParkYaw = (-Away).Rotation().Yaw;
			PlaceKate(Kate, PC, Ground, ParkYaw, -15.f);
			Test->AddInfo(FString::Printf(TEXT("Roof at %.0f cm."), Ground.Z));
		}
		break;
	}

	case EShot::Wall:
	{
		// Walk out from the street spot, away from the park, until a tenement stops the line.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotWall), false, Kate);
		FHitResult Hit;
		FVector Street;
		const bool bHasStreet = FindGround(World, Spot, 3000.f, Kate, Street);
		const FVector Chest = Street + FVector(0.f, 0.f, 120.f);
		if (bHasStreet && World->LineTraceSingleByChannel(Hit, Chest, Chest + Away * 5000.f, ECC_Visibility, Params)
			&& FindGround(World, Hit.ImpactPoint - Away * 60.f, Hit.ImpactPoint.Z + 200.f, Kate, Ground))
		{
			PlaceKate(Kate, PC, Ground, (-Away).Rotation().Yaw, HipPitch);
			Test->AddInfo(FString::Printf(TEXT("Wall hit on %s at %s."),
				*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString()));
		}
		else
		{
			Test->AddWarning(TEXT("No wall found across the street; kate_wall.png shows the street."));
		}
		break;
	}

	case EShot::Run:
		// Camera looking at the park, so "right" on the stick runs her along the street, side on.
		if (FindGround(World, Spot, 3000.f, Kate, Ground))
		{
			PlaceKate(Kate, PC, Ground, (-Away).Rotation().Yaw, HipPitch);
			HoldMove(PC, FVector2D(1.f, 0.f), true);
		}
		break;

	case EShot::Stop:
		HoldMove(PC, FVector2D::ZeroVector, false);
		break;

	case EShot::AimStrafe:
		Kate->StartAim();
		HoldMove(PC, FVector2D(1.f, 0.f), true);
		break;

	case EShot::EndMove:
		HoldMove(PC, FVector2D::ZeroVector, false);
		Kate->StopAim();
		break;

	case EShot::Costume:
		// Under the lamp's pool, turned toward the street, the camera 160 degrees round in front.
		if (ASpotLight* Lamp = FindLampNear(World, Spot))
		{
			if (FindGround(World, Lamp->GetActorLocation(), Lamp->GetActorLocation().Z - 50.f, Kate, Ground))
			{
				PlaceKate(Kate, PC, Ground, AlongYaw, HipPitch);
				Test->AddInfo(FString::Printf(TEXT("Costume: under %s at %s."), *GetNameSafe(Lamp),
					*Ground.ToCompactString()));
			}
		}
		else
		{
			Test->AddWarning(TEXT("kate_costume.png: no CityLamp spot light."));
		}
		break;
	}

	Test->AddInfo(FString::Printf(TEXT("Kate at %s, control %s."),
		*Kate->GetActorLocation().ToCompactString(), *PC->GetControlRotation().ToCompactString()));
	return true;
}

/** Looks at Kate from a camera in front of her, a little to her right, at chest height. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateCostumeCamera, FAutomationTestBase*, Test);

bool FHawkeyeKateCostumeCamera::Update()
{
	using namespace HawkeyeKateShots;

	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!Kate)
	{
		Test->AddError(TEXT("kate_costume.png: no Kate."));
		return true;
	}
	const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Chest = Kate->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.45f);
	const FVector Front = FRotator(0.f, Kate->GetActorRotation().Yaw + 25.f, 0.f).Vector();
	const FVector Eye = Chest + Front * 230.f + FVector(0.f, 0.f, 10.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(Eye, (Chest - Eye).Rotation(), Params);
	if (!Camera)
	{
		Test->AddError(TEXT("kate_costume.png: could not spawn the camera."));
		return true;
	}
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera->GetCameraComponent()->SetFieldOfView(55.f);
	Camera->SetLifeSpan(3.f);
	PC->SetViewTarget(Camera);
	Test->AddInfo(FString::Printf(TEXT("kate_costume.png: camera at %s, Kate facing %.0f."), *Eye.ToCompactString(),
		Kate->GetActorRotation().Yaw));
	return true;
}

/** Reports where the lens ended up: arm length after the probe, and whether it is inside anything. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateReportCamera, FAutomationTestBase*, Test, FString, Label);

bool FHawkeyeKateReportCamera::Update()
{
	UWorld* World = HawkeyeKateShots::FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!Kate || !Kate->GetFollowCamera() || !Kate->GetCameraBoom())
	{
		return true;
	}

	const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
	const float Distance = FVector::Dist(Lens, Kate->GetCameraBoom()->GetComponentLocation());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotLens), false, Kate);
	const bool bInside = World->OverlapAnyTestByChannel(
		Lens, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(4.f), Params);

	Test->AddInfo(FString::Printf(TEXT("%s: arm %.0f, lens %.0f cm from pivot, FOV %.0f, in geometry=%d, body hidden=%d"),
		*Label, Kate->GetCameraBoom()->TargetArmLength, Distance, Kate->GetCurrentFOV(), bInside ? 1 : 0,
		Kate->GetMesh() && Kate->GetMesh()->bOwnerNoSee ? 1 : 0));
	Test->AddInfo(FString::Printf(TEXT("%s: %s, mesh playing %s, anim instance %s, max walk speed %.0f"),
		*Label, *Kate->GetMovementDebugText(),
		*GetNameSafe(Kate->GetMesh() && Kate->GetMesh()->GetSingleNodeInstance()
			? Kate->GetMesh()->GetSingleNodeInstance()->GetAnimationAsset() : nullptr),
		*GetNameSafe(Kate->GetMesh() && Kate->GetMesh()->GetAnimInstance()
			? Kate->GetMesh()->GetAnimInstance()->GetClass() : nullptr),
		Kate->GetCharacterMovement() ? Kate->GetCharacterMovement()->MaxWalkSpeed : 0.f));
	if (bInside)
	{
		Test->AddWarning(FString::Printf(TEXT("%s: the camera is inside geometry."), *Label));
	}
	return true;
}

/**
 * Drops Kate from DropHeight above the street spot. With bReport false it only lifts her; with
 * bReport true (seconds later) it reads back the landing the character measured and the health
 * it cost, so the Landed path is exercised in a real world and not only in a unit test.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FHawkeyeKateDrop, FAutomationTestBase*, Test, float, DropHeight, bool, bReport);

bool FHawkeyeKateDrop::Update()
{
	using namespace HawkeyeKateShots;

	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	FVector Spot, Away, Ground;
	if (!Kate || !FindStreetSpot(World, Spot, Away) || !FindGround(World, Spot, 3000.f, Kate, Ground))
	{
		Test->AddWarning(TEXT("Could not set up the drop."));
		return true;
	}

	const UHealthComponent* Health = Kate->GetHealthComponent();
	if (!bReport)
	{
		PlaceKate(Kate, PC, Ground + FVector(0.f, 0.f, DropHeight), (-Away).Rotation().Yaw, HipPitch);
		return true;
	}

	Test->AddInfo(FString::Printf(TEXT("Drop from %.0f cm: landing measured %.0f cm, health %.1f / %.1f"),
		DropHeight, Kate->GetLastFallHeight(), Health ? Health->GetCurrentHealth() : -1.f,
		Health ? Health->GetMaxHealth() : -1.f));
	if (FMath::Abs(Kate->GetLastFallHeight() - DropHeight) > 50.f)
	{
		Test->AddWarning(TEXT("The measured landing is not the drop height; check Landed and the apex tracking."));
	}
	return true;
}

namespace HawkeyeKateShots
{
	static AHawkeyeCharacter* FindKate(APlayerController*& OutPC)
	{
		UWorld* World = FindWorld();
		OutPC = World ? World->GetFirstPlayerController() : nullptr;
		return OutPC ? Cast<AHawkeyeCharacter>(OutPC->GetPawn()) : nullptr;
	}

	/** What the grapple shots measure between commands. */
	struct FGrappleRun
	{
		TWeakObjectPtr<AGrappleAnchor> Anchor;
		double ZipStartTime = -1.0;
		FVector ZipStartLocation = FVector::ZeroVector;
		float ZipLength = 0.f;
	};
	static FGrappleRun GrappleRun;
}

/** Averages FApp::GetDeltaTime over Frames frames (after a few to settle) and logs it in ms. */
class FHawkeyeKateFrameTime : public IAutomationLatentCommand
{
public:
	FHawkeyeKateFrameTime(FAutomationTestBase* InTest, const FString& InLabel, int32 InFrames)
		: Test(InTest), Label(InLabel), Frames(InFrames)
	{
	}

	virtual bool Update() override
	{
		if (SettleFrames > 0)
		{
			--SettleFrames;
			return false;
		}
		const double Delta = FApp::GetDeltaTime();
		Sum += Delta;
		Worst = FMath::Max(Worst, Delta);
		if (++Count < Frames)
		{
			return false;
		}
		const double AverageMs = Sum / Count * 1000.0;
		Test->AddInfo(FString::Printf(
			TEXT("Frame time on the %s: %.2f ms average over %d frames (%.1f fps), worst %.2f ms"),
			*Label, AverageMs, Count, 1000.0 / AverageMs, Worst * 1000.0));
		UE_LOG(LogTemp, Display, TEXT("HawkeyeFrameTime %s avg_ms=%.2f worst_ms=%.2f frames=%d"),
			*Label, AverageMs, Worst * 1000.0, Count);
		return true;
	}

private:
	FAutomationTestBase* Test;
	FString Label;
	int32 Frames;
	int32 SettleFrames = 5;
	int32 Count = 0;
	double Sum = 0.0;
	double Worst = 0.0;
};

/**
 * Stands Kate on the street and turns the camera up at the tenement anchors across from the
 * park, nearest the middle of the grapple's range first, until the grapple targets one.
 */
class FHawkeyeKateAimAtAnchor : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeKateAimAtAnchor(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeKateShots;
		APlayerController* PC = nullptr;
		AHawkeyeCharacter* Kate = FindKate(PC);
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		UWorld* World = FindWorld();
		if (!Grapple || !World)
		{
			Test->AddError(TEXT("No Kate with a grapple component."));
			return true;
		}
		if (!bListed)
		{
			ListCandidates(World, Kate);
			bListed = true;
		}
		if (Index >= 0 && World->GetTimeSeconds() < AimedAt + 1.0)
		{
			return false;
		}
		if (Index >= 0 && Grapple->GetTargetAnchor())
		{
			Report(Kate, Grapple);
			return true;
		}
		if (Index >= 0)
		{
			Diagnose(World, Kate, Candidates[Index].Get());
		}
		if (++Index >= Candidates.Num() || Index >= 20)
		{
			Test->AddWarning(TEXT("No tenement anchor became a grapple target from the street."));
			return true;
		}
		if (const AGrappleAnchor* Anchor = Candidates[Index].Get())
		{
			const float Yaw = (Anchor->GetMarkerLocation() - Stands[Index]).Rotation().Yaw;
			HawkeyeKateShots::PlaceKate(Kate, PC, Stands[Index], Yaw, 0.f);
			Aim(PC, Kate, Anchor);
		}
		AimedAt = World->GetTimeSeconds();
		return false;
	}

private:
	/**
	 * Anchors near the street spot, each paired with a place on the street out from its facade
	 * as far as the grapple's range allows (at most 17 m), so the camera looks up as shallowly
	 * as it can.
	 */
	void ListCandidates(UWorld* World, const AHawkeyeCharacter* Kate)
	{
		static constexpr float MaxStandBack = 1700.f;
		static constexpr float Reach = 2350.f;
		FVector Spot, Away;
		HawkeyeKateShots::FindStreetSpot(World, Spot, Away);
		FVector Street;
		const float StreetZ = HawkeyeKateShots::FindGround(World, Spot, 3000.f, Kate, Street) ? Street.Z : 0.f;
		TArray<TPair<float, int32>> Order;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const FVector Marker = It->GetMarkerLocation();
			const float Height = Marker.Z - StreetZ;
			if (FVector::Dist2D(Marker, Spot) > 15000.f || Height < 800.f || Height > 2250.f)
			{
				continue;
			}
			const float StandBack = FMath::Min(MaxStandBack, FMath::Sqrt(Reach * Reach - Height * Height));
			const FVector Outward = -It->GetActorForwardVector().GetSafeNormal2D();
			const FVector StandXY = Marker + Outward * StandBack;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotStand), false, Kate);
			if (!World->LineTraceSingleByChannel(Ground, FVector(StandXY.X, StandXY.Y, StreetZ + 300.f),
					FVector(StandXY.X, StandXY.Y, StreetZ - 300.f), ECC_Visibility, Params)
				|| !Ground.GetActor() || Ground.GetActor()->Tags.Contains(HawkeyeKateShots::BuildingTag))
			{
				continue;
			}
			Order.Emplace(FVector::Dist2D(Marker, Spot), Candidates.Num());
			Candidates.Add(*It);
			Stands.Add(Ground.ImpactPoint);
		}
		Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		TArray<TWeakObjectPtr<AGrappleAnchor>> SortedAnchors;
		TArray<FVector> SortedStands;
		for (const TPair<float, int32>& Pair : Order)
		{
			SortedAnchors.Add(Candidates[Pair.Value]);
			SortedStands.Add(Stands[Pair.Value]);
		}
		Candidates = MoveTemp(SortedAnchors);
		Stands = MoveTemp(SortedStands);
		Test->AddInfo(FString::Printf(TEXT("%d tenement anchors with a street spot out in front of their facade."),
			Candidates.Num()));
	}

	/** Points the control rotation so the camera, which hangs behind and right of Kate, faces Anchor. */
	static void Aim(APlayerController* PC, AHawkeyeCharacter* Kate, const AGrappleAnchor* Anchor)
	{
		if (!Anchor)
		{
			return;
		}
		const FVector Target = Anchor->GetMarkerLocation();
		FRotator Rotation = (Target - Kate->GetActorLocation()).Rotation();
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			const FVector Lens = Kate->GetActorLocation() + Rotation.RotateVector(FVector(-350.f, 70.f, 60.f));
			Rotation = (Target - Lens).Rotation();
		}
		// Looking up from the street pulls the lens in against the pavement behind her, so her body
		// fills the middle of the frame. Aim a little left of and below the anchor (well inside the
		// 30 degree cone) so the marker shows up and to the right, clear of her.
		Kate->SetActorRotation(FRotator(0.f, Rotation.Yaw, 0.f));
		PC->SetControlRotation(FRotator(Rotation.Pitch - 8.f, Rotation.Yaw - 15.f, 0.f));
	}

	/** Why a candidate was not picked: its angle from the camera, its range, and what the sight line hits. */
	void Diagnose(UWorld* World, const AHawkeyeCharacter* Kate, const AGrappleAnchor* Anchor)
	{
		if (!Anchor)
		{
			return;
		}
		const UCameraComponent* Camera = Kate->GetFollowCamera();
		const FVector Lens = Camera->GetComponentLocation();
		const FVector Marker = Anchor->GetMarkerLocation();
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Camera->GetForwardVector(), (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotSight), false, Kate);
		Params.AddIgnoredActor(Anchor);
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Lens, Marker, ECC_Visibility, Params);
		Test->AddInfo(FString::Printf(
			TEXT("Candidate %s not targeted: %.1f deg off the camera, %.0f cm from Kate, sight %s %s at %.0f of %.0f cm"),
			*Anchor->GetActorNameOrLabel(), Angle, FVector::Dist(Kate->GetActorLocation(), Marker),
			bBlocked ? TEXT("hits") : TEXT("clear"), *GetNameSafe(Hit.GetActor()), Hit.Distance,
			FVector::Dist(Lens, Marker)));
	}

	void Report(AHawkeyeCharacter* Kate, UGrappleComponent* Grapple)
	{
		AGrappleAnchor* Target = Grapple->GetTargetAnchor();
		HawkeyeKateShots::GrappleRun.Anchor = Target;
		const UCameraComponent* Camera = Kate->GetFollowCamera();
		const FVector ToAnchor = (Target->GetMarkerLocation() - Camera->GetComponentLocation()).GetSafeNormal();
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(Camera->GetForwardVector(), ToAnchor), -1.f, 1.f)));
		Test->AddInfo(FString::Printf(
			TEXT("Grapple target %s (%s): %.0f cm from Kate, %.0f cm above her, %.1f deg off the camera"),
			*GetNameSafe(Target), *Target->GetActorNameOrLabel(),
			FVector::Dist(Kate->GetActorLocation(), Target->GetMarkerLocation()),
			Target->GetMarkerLocation().Z - Kate->GetActorLocation().Z, Angle));
	}

	FAutomationTestBase* Test;
	TArray<TWeakObjectPtr<AGrappleAnchor>> Candidates;
	TArray<FVector> Stands;
	bool bListed = false;
	int32 Index = -1;
	double AimedAt = 0.0;
};

/** Where the HUD drew the marker against where the anchor projects, both in viewport pixels. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateReportMarker, FAutomationTestBase*, Test);

bool FHawkeyeKateReportMarker::Update()
{
	using namespace HawkeyeKateShots;
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
	const UHawkeyeHudWidget* Hud = HawkeyePC ? HawkeyePC->GetHawkeyeHud() : nullptr;
	const UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
	const AGrappleAnchor* Target = Grapple ? Grapple->GetTargetAnchor() : nullptr;
	if (!Hud || !Target)
	{
		Test->AddWarning(TEXT("No HUD or no grapple target for the marker shot."));
		return true;
	}
	FVector2D AnchorPixel;
	PC->ProjectWorldLocationToScreen(Target->GetMarkerLocation(), AnchorPixel, false);
	const FVector2D MarkerPixel = Hud->GetGrappleMarkerPosition() * UWidgetLayoutLibrary::GetViewportScale(PC);
	Test->AddInfo(FString::Printf(
		TEXT("Marker visible=%d ready=%d at (%.0f, %.0f) px, anchor projects to (%.0f, %.0f) px, hint=%d \"%s\""),
		Hud->IsGrappleMarkerVisible() ? 1 : 0, Hud->IsGrappleMarkerReady() ? 1 : 0, MarkerPixel.X, MarkerPixel.Y,
		AnchorPixel.X, AnchorPixel.Y, Hud->IsGrappleHintVisible() ? 1 : 0, *Hud->GetGrappleHintShown().ToString()));
	if (!Hud->IsGrappleMarkerVisible() || FVector2D::Distance(MarkerPixel, AnchorPixel) > 3.f)
	{
		Test->AddWarning(TEXT("The grapple marker is not on the anchor."));
	}
	if (!Hud->IsGrappleMarkerReady() || !Hud->IsGrappleHintVisible())
	{
		// The playtest bug: a green diamond with no key under it.
		Test->AddWarning(TEXT("The grapple marker shows without the key hint under it."));
	}
	return true;
}

/** Presses Q. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateFireGrapple, FAutomationTestBase*, Test);

bool FHawkeyeKateFireGrapple::Update()
{
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC);
	UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
	HawkeyeKateShots::GrappleRun.ZipStartTime = -1.0;
	const bool bFired = Grapple && Grapple->TryFire();
	Test->AddInfo(FString::Printf(TEXT("Grapple fired=%d"), bFired ? 1 : 0));
	if (!bFired)
	{
		Test->AddWarning(TEXT("The grapple did not fire."));
	}
	return true;
}

/**
 * Waits (up to TimeoutSeconds) for the zip to pass Progress, noting when it started. With
 * Progress above 1 it waits for the landing instead and reports the zip's numbers.
 */
class FHawkeyeKateWaitZip : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitZip(FAutomationTestBase* InTest, float InProgress, float InTimeoutSeconds)
		: Test(InTest), Progress(InProgress), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override;

private:
	FAutomationTestBase* Test;
	float Progress;
	float TimeoutSeconds;
	double StartTime = -1.0;
};

bool FHawkeyeKateWaitZip::Update()
{
	using namespace HawkeyeKateShots;
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
	UWorld* World = FindWorld();
	if (!Grapple || !World)
	{
		return true;
	}
	const double Now = World->GetTimeSeconds();
	FGrappleRun& Run = GrappleRun;
	if (Grapple->IsZipping() && Run.ZipStartTime < 0.0)
	{
		// The first frame seen zipping has already moved one step; back the start out of it.
		Run.ZipStartTime = Now - World->GetDeltaSeconds();
		Run.ZipLength = Grapple->GetZipLength();
		Run.ZipStartLocation = Kate->GetActorLocation();
	}
	if (StartTime < 0.0)
	{
		StartTime = Now;
	}
	const bool bTimedOut = Now - StartTime > TimeoutSeconds;

	if (Progress <= 1.f)
	{
		if (Grapple->IsZipping() && Grapple->GetZipProgress() >= Progress)
		{
			Test->AddInfo(FString::Printf(TEXT("Mid-zip: %.0f%% along, at %s, %.2f s in"),
				Grapple->GetZipProgress() * 100.f, *Kate->GetActorLocation().ToCompactString(),
				Now - Run.ZipStartTime));
			return true;
		}
		if (bTimedOut)
		{
			Test->AddWarning(FString::Printf(TEXT("The zip never reached %.0f%%."), Progress * 100.f));
		}
		return bTimedOut;
	}

	if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
	{
		if (bTimedOut)
		{
			Test->AddWarning(TEXT("Kate never landed from the zip."));
		}
		return bTimedOut;
	}
	const double Duration = Now - Run.ZipStartTime;
	const AGrappleAnchor* Anchor = Run.Anchor.Get();
	const float Feet = Kate->GetActorLocation().Z - Kate->GetSimpleCollisionHalfHeight();
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotRoof), false, Kate);
	World->LineTraceSingleByChannel(Floor, Kate->GetActorLocation(),
		Kate->GetActorLocation() - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params);
	Test->AddInfo(FString::Printf(
		TEXT("Zip: %.0f cm in %.2f s = %.0f cm/s; landed at %s, feet %.1f cm above the landing point, %.1f cm above %s, walking=%d"),
		Run.ZipLength, Duration, Duration > 0.0 ? Run.ZipLength / Duration : 0.0,
		*Kate->GetActorLocation().ToCompactString(),
		Anchor ? Feet - Anchor->GetLandingLocation().Z : -1.f, Floor.bBlockingHit ? Feet - Floor.ImpactPoint.Z : -1.f,
		*GetNameSafe(Floor.GetActor()), Kate->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0));
	return true;
}

/** After landing: level the camera to look across the roof the way she is facing. */
DEFINE_LATENT_AUTOMATION_COMMAND(FHawkeyeKateLookAcrossRoof);

bool FHawkeyeKateLookAcrossRoof::Update()
{
	APlayerController* PC = nullptr;
	if (AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC))
	{
		PC->SetControlRotation(FRotator(-15.f, Kate->GetActorRotation().Yaw, 0.f));
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeKateTakeShot::Update()
{
	const FString FullPath = HawkeyeKateShots::ShotPath(FileName);
	// With UI, so the aim shot shows the reticle and the hip shots show there is none.
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

// --- Parkour and the look-up camera -------------------------------------------------------------

namespace HawkeyeKateShots
{
	static const TCHAR* SprintActionPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");

	enum class EParkourShot : uint8
	{
		Lookup,
		VaultRun,
		MantleStand,
		MantleJump,
		VaultStand,
		RoofEdge,
		RoofEdgeHold,
		LedgeFall,
		Climb,
		EndInput,
		/** Our own moves from here: the sample's traversal off, and a 185 cm block beside the 150 cm one. */
		OwnRoute,
		TallMantleStand,
		/** The sample's traversal back on, the tall block gone. */
		SampleRoute,
	};

	/** Holds or releases IA_Sprint through Enhanced Input injection, like HoldMove. */
	static void HoldSprint(APlayerController* PC, bool bHold)
	{
		const UInputAction* Sprint = LoadObject<UInputAction>(nullptr, SprintActionPath);
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = Player
			? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
		if (!Sprint || !Input)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(Sprint, FInputActionValue(true), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Sprint);
		}
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

	static const FName TallBlockTag(TEXT("CityTestTallMantle"));

	/** A copy of City_Test_Mantle 185 cm tall, half way to City_Test_Vault, for the 2.5 m clip's height band. */
	static AActor* SpawnTallBlock(UWorld* World)
	{
		const AActor* Mantle = FindTagged(World, TEXT("CityTestMantle"));
		const AActor* Vault = FindTagged(World, TEXT("CityTestVault"));
		const UStaticMeshComponent* Mesh = Mantle ? Mantle->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		if (!Vault || !Mesh)
		{
			return nullptr;
		}
		const FBox Bounds = Mantle->GetComponentsBoundingBox();
		FTransform Transform = Mantle->GetActorTransform();
		const FVector Scale = Transform.GetScale3D();
		Transform.SetScale3D(FVector(Scale.X, Scale.Y, Scale.Z * 185.f / FMath::Max(1.f, static_cast<float>(Bounds.GetSize().Z))));
		const FVector Middle = (Mantle->GetActorLocation() + Vault->GetActorLocation()) * 0.5f;
		Transform.SetLocation(FVector(Middle.X, Middle.Y, Bounds.Min.Z + 92.5f));
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Mesh->GetStaticMesh());
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Block->GetStaticMeshComponent()->SetMaterial(Slot, Mesh->GetMaterial(Slot));
		}
		Block->SetActorTransform(Transform);
		// On the street where the 150 cm block stands, whatever the mesh's pivot.
		Block->AddActorWorldOffset(FVector(0.f, 0.f, Bounds.Min.Z - Block->GetComponentsBoundingBox().Min.Z));
		Block->Tags.Add(TallBlockTag);
		return Block;
	}

	/** The IA_Move value that walks along WorldDirection with the camera at CameraYaw. */
	static FVector2D MoveTowards(const FVector& WorldDirection, float CameraYaw)
	{
		const float Angle = FMath::DegreesToRadians(WorldDirection.Rotation().Yaw - CameraYaw);
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
	}

	/**
	 * Stands Kate Back cm in front of a test block's near face (the block's local -Y side, its depth
	 * axis), facing it, with the camera Side degrees round from behind her.
	 */
	static bool FaceTestBlock(UWorld* World, AHawkeyeCharacter* Kate, APlayerController* PC, FName Tag, float Back,
		float Side, FVector& OutDirection)
	{
		const AActor* Block = FindTagged(World, Tag);
		FVector Ground;
		if (!Block)
		{
			return false;
		}
		const FVector Axis = Block->GetActorRightVector().GetSafeNormal2D();
		const FVector Near = Block->GetActorLocation() + Block->GetActorForwardVector() * Block->GetActorScale3D().X * 50.f;
		const FVector Stand = Near - Axis * Back;
		if (!FindGround(World, Stand, Near.Z + 500.f, Kate, Ground))
		{
			return false;
		}
		OutDirection = Axis;
		PlaceKate(Kate, PC, Ground, Axis.Rotation().Yaw, -10.f);
		PC->SetControlRotation(FRotator(-10.f, Axis.Rotation().Yaw + Side, 0.f));
		return true;
	}

	/** A tenement face out from the street spot, with the top of its parapet. */
	static bool FindTenementLedge(UWorld* World, const AHawkeyeCharacter* Kate, FVector& OutFace, FVector& OutNormal,
		float& OutTopZ)
	{
		FVector Spot, Away, Street;
		if (!FindStreetSpot(World, Spot, Away) || !FindGround(World, Spot, 3000.f, Kate, Street))
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotLedge), false, Kate);
		FHitResult Hit;
		const FVector Chest = Street + FVector(0.f, 0.f, 120.f);
		if (!World->LineTraceSingleByChannel(Hit, Chest, Chest + Away * 5000.f, ECC_Visibility, Params)
			|| !Hit.GetActor() || !Hit.GetActor()->Tags.Contains(BuildingTag))
		{
			return false;
		}
		OutFace = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal.GetSafeNormal2D();
		FHitResult Top;
		const FVector Inside = OutFace - OutNormal * 10.f;
		if (!World->LineTraceSingleByChannel(Top, Inside + FVector(0.f, 0.f, 6000.f), Inside, ECC_Visibility, Params))
		{
			return false;
		}
		OutTopZ = Top.ImpactPoint.Z;
		return true;
	}

	/** An anchor on a facade seen from the street at StandBack cm out, and where to stand. */
	static bool FindLookupSpot(UWorld* World, const AHawkeyeCharacter* Kate, FVector& OutStand, FVector& OutTarget)
	{
		static constexpr float StandBack = 1300.f;
		FVector Spot, Away, Street;
		if (!FindStreetSpot(World, Spot, Away) || !FindGround(World, Spot, 3000.f, Kate, Street))
		{
			return false;
		}
		float Best = BIG_NUMBER;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const FVector Marker = It->GetMarkerLocation();
			const float Height = Marker.Z - Street.Z;
			const float Distance = FVector::Dist2D(Marker, Spot);
			if (Height < 1500.f || Height > 2200.f || Distance > 12000.f || Distance > Best)
			{
				continue;
			}
			const FVector StandXY = Marker - It->GetActorForwardVector().GetSafeNormal2D() * StandBack;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotLookup), false, Kate);
			FHitResult Ground;
			if (!World->LineTraceSingleByChannel(Ground, FVector(StandXY.X, StandXY.Y, Street.Z + 300.f),
					FVector(StandXY.X, StandXY.Y, Street.Z - 300.f), ECC_Visibility, Params)
				|| !Ground.GetActor() || Ground.GetActor()->Tags.Contains(BuildingTag))
			{
				continue;
			}
			Best = Distance;
			OutStand = Ground.ImpactPoint;
			OutTarget = Marker;
		}
		return Best < BIG_NUMBER;
	}

	/** Kate's projected screen box, in viewport pixels; false when she is off screen. */
	static bool KateScreenBox(APlayerController* PC, const AHawkeyeCharacter* Kate, FBox2D& OutBox)
	{
		const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		OutBox = FBox2D(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset((Corner & 1) ? Radius : -Radius, (Corner & 2) ? Radius : -Radius, (Corner & 4) ? Half : -Half);
			FVector2D Pixel;
			if (PC->ProjectWorldLocationToScreen(Kate->GetActorLocation() + Offset, Pixel, false))
			{
				OutBox += Pixel;
			}
		}
		return OutBox.bIsValid;
	}
}

/** Sets up one parkour or look-up shot. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateParkourShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeKateParkourShot::Update()
{
	using namespace HawkeyeKateShots;
	UWorld* World = FindWorld();
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	if (!Kate || !World)
	{
		Test->AddError(TEXT("No Kate for the parkour shots."));
		return true;
	}
	FVector Direction;
	switch (static_cast<EParkourShot>(Shot))
	{
	case EParkourShot::Lookup:
	{
		FVector Stand, Target;
		if (!FindLookupSpot(World, Kate, Stand, Target))
		{
			Test->AddWarning(TEXT("No rooftop anchor with a street spot 13 m out; kate_lookup.png shows the street."));
			break;
		}
		const FVector Pivot = Stand + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FRotator Look = (Target - Pivot).Rotation();
		PlaceKate(Kate, PC, Stand, Look.Yaw, 0.f);
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Look.Pitch), Look.Yaw, 0.f));
		Test->AddInfo(FString::Printf(TEXT("Lookup: anchor %.0f cm up, %.0f cm out, pitch %.1f deg"),
			Target.Z - Stand.Z, FVector::Dist2D(Target, Stand), Look.Pitch));
		break;
	}
	case EParkourShot::VaultRun:
		if (FaceTestBlock(World, Kate, PC, TEXT("CityTestVault"), 600.f, -70.f, Direction))
		{
			HoldSprint(PC, true);
			HoldMove(PC, MoveTowards(Direction, PC->GetControlRotation().Yaw), true);
		}
		else
		{
			Test->AddWarning(TEXT("No City_Test_Vault block."));
		}
		break;
	case EParkourShot::MantleStand:
		if (!FaceTestBlock(World, Kate, PC, TEXT("CityTestMantle"), 70.f, -75.f, Direction))
		{
			Test->AddWarning(TEXT("No City_Test_Mantle block."));
		}
		break;
	case EParkourShot::MantleJump:
		Kate->Jump();
		break;
	case EParkourShot::VaultStand:
		// Pressed against the near face (capsule 2 cm off it), no stick, no speed: one press.
		if (FaceTestBlock(World, Kate, PC, TEXT("CityTestVault"), Kate->GetCapsuleComponent()->GetScaledCapsuleRadius() + 2.f,
				-60.f, Direction))
		{
			Kate->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		}
		else
		{
			Test->AddWarning(TEXT("No City_Test_Vault block."));
		}
		break;
	case EParkourShot::RoofEdge:
	{
		// The first parapet on the tenement roofs (as ParkourDistrictTest walks them) with a roof edge beyond and a top
		// she fits on: 60 cm short of it, walking at it, one press.
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotRoofEdge), false, Kate);
		bool bFound = false;
		for (float X = -9000.f; X <= -2000.f && !bFound; X += 200.f)
		{
			for (float Y = 9500.f; Y <= 13600.f && !bFound; Y += 200.f)
			{
				FHitResult Roof;
				if (!World->LineTraceSingleByChannel(Roof, FVector(X, Y, 4000.f), FVector(X, Y, 500.f), ECC_Visibility, Params)
					|| !Roof.GetActor() || !Roof.GetActor()->Tags.Contains(BuildingTag) || Roof.ImpactNormal.Z < 0.9f)
				{
					continue;
				}
				for (int32 Dir = 0; Dir < 8 && !bFound; ++Dir)
				{
					const FVector Toward = FVector::ForwardVector.RotateAngleAxis(Dir * 45.f, FVector::UpVector);
					Kate->TeleportTo(Roof.ImpactPoint + FVector(0.f, 0.f, Half + 2.f), Toward.Rotation(), false, true);
					Movement->SetMovementMode(MOVE_Walking);
					FHawkeyeParkourObstacle Wall;
					FVector Stand;
					if (!Parkour->DetectObstacleAlong(Toward, 300.f, 320.f, Wall) || !Parkour->IsRoofEdgeParapet(Wall)
						|| !Parkour->FindParapetStand(Wall, Stand))
					{
						continue;
					}
					FVector Ground;
					const FVector Spot = Wall.WallPoint + Wall.WallNormal * (Radius + 60.f);
					if (!FindGround(World, Spot, Roof.ImpactPoint.Z + 150.f, Kate, Ground) || FMath::Abs(Ground.Z - Roof.ImpactPoint.Z) > 20.f)
					{
						continue;
					}
					const FVector ToWall = -Wall.WallNormal;
					PlaceKate(Kate, PC, Ground, ToWall.Rotation().Yaw, -15.f);
					// From the roof, off to her right and behind: the parapet, her on it, and the drop past it.
					PC->SetControlRotation(FRotator(-18.f, ToWall.Rotation().Yaw - 50.f, 0.f));
					HoldMove(PC, MoveTowards(ToWall, PC->GetControlRotation().Yaw), true);
					Test->AddInfo(FString::Printf(TEXT("roof_edge_guard.png: a %.0f cm parapet at %s with a %.0f cm drop past it, on %s"),
						Wall.Height, *Wall.WallPoint.ToCompactString(), Wall.FarSideDrop, *GetNameSafe(Wall.Actor)));
					bFound = true;
				}
			}
		}
		if (!bFound)
		{
			Test->AddWarning(TEXT("roof_edge_guard.png: no roof-edge parapet with a top she fits on."));
		}
		break;
	}
	case EParkourShot::RoofEdgeHold:
	{
		// A second or so after the mantle, the stick still at the drop: where she is and whether the guard holds her.
		const UParkourComponent* Parkour = Kate->GetParkourComponent();
		const FHawkeyeParkourObstacle Wall = Parkour->GetLastObstacle();
		const float Feet = Kate->GetActorLocation().Z - Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool bOnTop = Parkour->IsPerchedOnParapet() && FMath::Abs(Feet - Wall.LedgePoint.Z) < 15.f;
		Test->AddInfo(FString::Printf(TEXT("roof_edge_guard.png: feet %.0f vs the parapet top %.0f, perched %d, %s, %.0f cm in from the face"),
			Feet, Wall.LedgePoint.Z, Parkour->IsPerchedOnParapet() ? 1 : 0, *Kate->GetMovementDebugText(),
			FVector::DotProduct(Wall.WallPoint - Kate->GetActorLocation(), Wall.WallNormal)));
		if (!bOnTop)
		{
			Test->AddWarning(TEXT("roof_edge_guard.png: Kate is not standing on the parapet top."));
		}
		break;
	}
	case EParkourShot::LedgeFall:
	{
		FVector Face, Normal;
		float TopZ = 0.f;
		if (!FindTenementLedge(World, Kate, Face, Normal, TopZ))
		{
			Test->AddWarning(TEXT("No tenement face across the street for the ledge shot."));
			break;
		}
		// In the air against the wall, feet 230 cm under the parapet top (a fire escape landing's
		// height): falling, she should catch the edge.
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Air(Face.X + Normal.X * 45.f, Face.Y + Normal.Y * 45.f, TopZ - 230.f + Half);
		Kate->StopAim();
		Kate->TeleportTo(Air, FRotator(0.f, (-Normal).Rotation().Yaw, 0.f), false, true);
		Kate->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Kate->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -10.f);
		PC->SetControlRotation(FRotator(12.f, (-Normal).Rotation().Yaw + 35.f, 0.f));
		Test->AddInfo(FString::Printf(TEXT("Ledge: parapet top %.0f cm, Kate's feet at %.0f, face at %s"),
			TopZ, Air.Z - Half, *Face.ToCompactString()));
		break;
	}
	case EParkourShot::Climb:
		Kate->Jump();
		break;
	case EParkourShot::EndInput:
		HoldSprint(PC, false);
		HoldMove(PC, FVector2D::ZeroVector, false);
		break;
	case EParkourShot::OwnRoute:
		// The sample would take these moves on the street; turned off, they are ours (warped clip root motion).
		Kate->GetParkourComponent()->bUseSampleTraversal = false;
		if (!FindTagged(World, TallBlockTag) && !SpawnTallBlock(World))
		{
			Test->AddWarning(TEXT("No City_Test_Mantle to copy for the 185 cm block."));
		}
		break;
	case EParkourShot::TallMantleStand:
		if (!FaceTestBlock(World, Kate, PC, TallBlockTag, 70.f, -75.f, Direction))
		{
			Test->AddWarning(TEXT("No 185 cm block."));
		}
		break;
	case EParkourShot::SampleRoute:
		Kate->GetParkourComponent()->bUseSampleTraversal = true;
		if (AActor* Tall = FindTagged(World, TallBlockTag))
		{
			Tall->Destroy();
		}
		break;
	}
	return true;
}

/** Waits (up to TimeoutSeconds) for a traversal move to start, or with bHang for a hang. */
class FHawkeyeKateWaitTraversal : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitTraversal(FAutomationTestBase* InTest, float InTimeoutSeconds, bool bInHang)
		: Test(InTest), TimeoutSeconds(InTimeoutSeconds), bHang(bInHang)
	{
	}

	virtual bool Update() override
	{
		APlayerController* PC = nullptr;
		AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC);
		const UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
		UWorld* World = HawkeyeKateShots::FindWorld();
		if (!Parkour || !World)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			StartTime = World->GetTimeSeconds();
		}
		const bool bReady = bHang ? Parkour->IsHanging() : Kate->IsTraversing();
		if (bReady)
		{
			const FHawkeyeParkourObstacle Obstacle = Parkour->GetLastObstacle();
			Test->AddInfo(FString::Printf(
				TEXT("Traversal after %.2f s: %s by %s; obstacle %.0f cm high, %.0f cm away, depth %.0f, on %s"),
				World->GetTimeSeconds() - StartTime, *UEnum::GetValueAsString(Parkour->GetLastMove()),
				*UEnum::GetValueAsString(Parkour->GetLastRoute()), Obstacle.Height, Obstacle.Distance, Obstacle.Depth,
				*GetNameSafe(Obstacle.Actor)));
			return true;
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds)
		{
			Test->AddWarning(FString::Printf(TEXT("No %s within %.1f s; Kate at %s, %s"), bHang ? TEXT("hang") : TEXT("traversal"),
				TimeoutSeconds, *Kate->GetActorLocation().ToCompactString(), *Kate->GetMovementDebugText()));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float TimeoutSeconds;
	bool bHang;
	double StartTime = -1.0;
};

/** Where the hands and capsule are against the obstacle, and whether the capsule is inside anything. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateReportParkour, FAutomationTestBase*, Test, FString, Label);

bool FHawkeyeKateReportParkour::Update()
{
	UWorld* World = HawkeyeKateShots::FindWorld();
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC);
	const UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!Parkour || !World)
	{
		return true;
	}
	const FHawkeyeParkourObstacle Obstacle = Parkour->GetLastObstacle();
	const USkeletalMeshComponent* Body = Kate->GetMesh();
	const FVector HandL = Body ? Body->GetSocketLocation(TEXT("hand_l")) : FVector::ZeroVector;
	const FVector HandR = Body ? Body->GetSocketLocation(TEXT("hand_r")) : FVector::ZeroVector;
	const UCapsuleComponent* Capsule = Kate->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KateShotClip), false, Kate);
	const bool bClipping = World->OverlapBlockingTestByChannel(Kate->GetActorLocation(), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() - 4.f, Capsule->GetScaledCapsuleHalfHeight() - 4.f),
		Params);
	const float Feet = Kate->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	Test->AddInfo(FString::Printf(
		TEXT("%s: move %s (%s) hanging=%d sample=%d; feet %.0f cm vs ledge top %.0f; hands %.0f / %.0f cm above the top, "
			 "%.0f / %.0f cm out from the face; capsule %.0f cm out; in geometry=%d"),
		*Label, *UEnum::GetValueAsString(Parkour->GetActiveMove()), *UEnum::GetValueAsString(Parkour->GetLastRoute()),
		Parkour->IsHanging() ? 1 : 0, Parkour->IsSampleTraversalActive() ? 1 : 0, Feet, Obstacle.LedgePoint.Z,
		HandL.Z - Obstacle.LedgePoint.Z, HandR.Z - Obstacle.LedgePoint.Z,
		FVector::DotProduct(HandL - Obstacle.WallPoint, Obstacle.WallNormal),
		FVector::DotProduct(HandR - Obstacle.WallPoint, Obstacle.WallNormal),
		FVector::DotProduct(Kate->GetActorLocation() - Obstacle.WallPoint, Obstacle.WallNormal), bClipping ? 1 : 0));
	if (bClipping)
	{
		Test->AddWarning(FString::Printf(TEXT("%s: Kate's capsule is inside geometry."), *Label));
	}
	return true;
}

/** Where the look-up camera ended up: lens height over the ground, arm, and whether Kate covers the centre. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateReportLookup, FAutomationTestBase*, Test);

bool FHawkeyeKateReportLookup::Update()
{
	UWorld* World = HawkeyeKateShots::FindWorld();
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC);
	if (!Kate || !World || !PC)
	{
		return true;
	}
	const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
	FVector Ground;
	const bool bGround = HawkeyeKateShots::FindGround(World, Lens + FVector(0.f, 0.f, 50.f), Lens.Z + 50.f, Kate, Ground);
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	FBox2D Box;
	const bool bOnScreen = HawkeyeKateShots::KateScreenBox(PC, Kate, Box);
	const FVector2D Centre(SizeX * 0.5f, SizeY * 0.5f);
	const bool bCoversCentre = bOnScreen && Box.IsInside(Centre);
	const float Coverage = bOnScreen && SizeX > 0 && SizeY > 0
		? (FMath::Min<float>(Box.Max.X, SizeX) - FMath::Max<float>(Box.Min.X, 0.f))
			* (FMath::Min<float>(Box.Max.Y, SizeY) - FMath::Max<float>(Box.Min.Y, 0.f)) / (SizeX * SizeY)
		: 0.f;
	Test->AddInfo(FString::Printf(
		TEXT("kate_lookup: pitch %.1f, arm %.0f, socket Z %.0f, lens %.0f cm above the ground, %.0f cm from the pivot; "
			 "Kate's box (%.0f,%.0f)-(%.0f,%.0f) of %dx%d, covers centre=%d, %.0f%% of the frame"),
		FRotator::NormalizeAxis(PC->GetControlRotation().Pitch), Kate->GetCameraBoom()->TargetArmLength,
		Kate->GetCameraBoom()->SocketOffset.Z, bGround ? Lens.Z - Ground.Z : -1.f,
		FVector::Dist(Lens, Kate->GetCameraBoom()->GetComponentLocation()), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y,
		SizeX, SizeY, bCoversCentre ? 1 : 0, Coverage * 100.f));
	if (bCoversCentre)
	{
		Test->AddWarning(TEXT("kate_lookup: Kate covers the centre of the frame."));
	}
	if (bGround && Lens.Z - Ground.Z < 20.f)
	{
		Test->AddWarning(TEXT("kate_lookup: the lens is down on the pavement."));
	}
	return true;
}

// --- The bow ------------------------------------------------------------------------------------

namespace HawkeyeKateShots
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");
	static const TCHAR* BowAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrappleAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");
	static constexpr float ThugDistance = 1500.f;

	enum class EBowShot : uint8
	{
		Holster,
		DrawStart,
		DrawCancel,
		HitSetup,
		HitAim,
		HitDraw,
		HitRelease,
		StuckView,
		Cleanup,
	};

	/** The thug the hit shot aims at. */
	static TWeakObjectPtr<AThugCharacter> TargetThug;

	static AThugCharacter* SpawnThug(UWorld* World, const FVector& Ground, float Yaw, bool bWithBrain)
	{
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		if (!ThugClass)
		{
			ThugClass = AThugCharacter::StaticClass();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const float HalfHeight = GetDefault<AThugCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		AThugCharacter* Thug = World->SpawnActor<AThugCharacter>(ThugClass,
			Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), Params);
		if (Thug && !bWithBrain)
		{
			// A target, not a fight: without its brain it stands still and never shoots back.
			if (AController* Brain = Thug->GetController())
			{
				Brain->UnPossess();
				Brain->Destroy();
			}
		}
		return Thug;
	}

	/** Makes sure Kate carries the chapter's quiver; warns when the mission did not grant it. */
	static void EnsureQuiver(FAutomationTestBase* Test, UInventoryComponent* Inventory)
	{
		if (Inventory->HasBow() && Inventory->GetArrowCount(1) > 0)
		{
			return;
		}
		Test->AddWarning(TEXT("Kate had no bow or arrows on the district; DA_CH01_Rooftops should grant them."));
		UBowDefinition* Bow = LoadObject<UBowDefinition>(nullptr, BowAssetPath);
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, StandardAssetPath);
		Standard.Count = 30;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, GrappleAssetPath);
		Grapple.Count = 6;
		Inventory->ApplyStartingQuiver(Bow, { Standard, Grapple });
	}

	/** Where the bow sits against the bones it hangs off, for tuning the holster and the grip. */
	static void ReportBow(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, const TCHAR* Label)
	{
		const UBowComponent* Bow = Kate->GetBowComponent();
		const UStaticMeshComponent* Mesh = Bow ? Bow->GetBowMeshComponent() : nullptr;
		const UInventoryComponent* Inventory = Kate->GetInventoryComponent();
		if (!Mesh || !Inventory)
		{
			Test->AddWarning(FString::Printf(TEXT("%s: no bow mesh on Kate."), Label));
			return;
		}
		const FTransform Actor = Kate->GetActorTransform();
		const FVector Spine = Kate->GetMesh()->GetSocketLocation(Bow->HolsterBone);
		const FVector Hand = Kate->GetMesh()->GetSocketLocation(Inventory->GetBow()->HandSocket);
		Test->AddInfo(FString::Printf(
			TEXT("%s: bow %s mesh %s at %s (actor frame %s), rot %s; spine_03 %s, hand %s (actor frame); draw %.2f, spread %.2f; quiver %d / %d, slot %d"),
			Label, *GetNameSafe(Inventory->GetBow()), *GetNameSafe(Mesh->GetStaticMesh()),
			*Mesh->GetComponentLocation().ToCompactString(),
			*Actor.InverseTransformPosition(Mesh->GetComponentLocation()).ToCompactString(),
			*Mesh->GetComponentRotation().ToCompactString(),
			*Actor.InverseTransformPosition(Spine).ToCompactString(),
			*Actor.InverseTransformPosition(Hand).ToCompactString(),
			Bow->GetDrawFraction(), Bow->GetCurrentSpreadDegrees(),
			Inventory->GetArrowCount(1), Inventory->GetArrowCount(2), Inventory->GetActiveArrowSlot()));
	}

	/** Points the camera from where it is now at Target. The arm follows, so do it twice. */
	static void AimCameraAt(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Rotation = (Target - Lens).Rotation();
		PC->SetControlRotation(FRotator(Rotation.Pitch, Rotation.Yaw, 0.f));
		Kate->SetActorRotation(FRotator(0.f, Rotation.Yaw, 0.f));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateBowShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeKateBowShot::Update()
{
	using namespace HawkeyeKateShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
	if (!Bow || !Inventory)
	{
		Test->AddError(TEXT("No Kate with a bow component and an inventory."));
		return true;
	}

	switch (static_cast<EBowShot>(Shot))
	{
	case EBowShot::Holster:
		EnsureQuiver(Test, Inventory);
		ReportBow(Test, Kate, TEXT("bow_holstered.png"));
		break;

	case EBowShot::DrawStart:
		if (!Bow->StartDraw())
		{
			Test->AddWarning(TEXT("The bow would not draw."));
		}
		break;

	case EBowShot::DrawCancel:
		ReportBow(Test, Kate, TEXT("bow_draw.png"));
		Bow->CancelDraw();
		break;

	case EBowShot::HitSetup:
	{
		FVector Spot, Away, Ground, ThugGround;
		if (!FindStreetSpot(World, Spot, Away) || !FindGround(World, Spot, 3000.f, Kate, Ground))
		{
			Test->AddWarning(TEXT("No street spot for the bow hit."));
			break;
		}
		const float AlongYaw = FVector::CrossProduct(FVector::UpVector, Away).Rotation().Yaw;
		PlaceKate(Kate, PC, Ground, AlongYaw, 0.f);
		const FVector Along = FRotator(0.f, AlongYaw, 0.f).Vector();
		if (!FindGround(World, Ground + Along * ThugDistance, Ground.Z + 400.f, Kate, ThugGround))
		{
			Test->AddWarning(TEXT("No ground 15 m down the street for the thug."));
			break;
		}
		TargetThug = SpawnThug(World, ThugGround, AlongYaw + 180.f, false);
		Kate->StartAim();
		Test->AddInfo(FString::Printf(TEXT("Thug %s at %s, %.0f cm from Kate."), *GetNameSafe(TargetThug.Get()),
			*ThugGround.ToCompactString(), FVector::Dist2D(ThugGround, Ground)));
		break;
	}

	case EBowShot::HitAim:
		if (AThugCharacter* Thug = TargetThug.Get())
		{
			// The chest, plus the 30 cm an arrow drops over 15 m at full draw.
			AimCameraAt(PC, Kate, Thug->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
		break;

	case EBowShot::HitDraw:
		if (AThugCharacter* Thug = TargetThug.Get())
		{
			AimCameraAt(PC, Kate, Thug->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
		Bow->StartDraw();
		break;

	case EBowShot::HitRelease:
		ReportBow(Test, Kate, TEXT("bow_hit (release)"));
		if (!Bow->ReleaseDraw())
		{
			Test->AddWarning(TEXT("The release did not fire."));
		}
		// The player keeps the aim button held through the shot, so the camera stays in.
		Kate->StartAim();
		break;

	case EBowShot::StuckView:
	{
		// From behind the arrow, the way the player sees one she has just put in something.
		Kate->StopAim();
		const AArrowProjectile* Stuck = nullptr;
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			if (It->IsStuck())
			{
				Stuck = *It;
				break;
			}
		}
		FVector Ground;
		if (!Stuck)
		{
			Test->AddWarning(TEXT("arrow_stuck.png: no stuck arrow to look at."));
			break;
		}
		const FVector Back = -Stuck->GetActorForwardVector().GetSafeNormal2D();
		if (!FindGround(World, Stuck->GetActorLocation() + Back * 250.f + FVector(0.f, 0.f, 200.f), Stuck->GetActorLocation().Z + 200.f, Kate, Ground))
		{
			Test->AddWarning(TEXT("arrow_stuck.png: no ground behind the arrow."));
			break;
		}
		PlaceKate(Kate, PC, Ground, (-Back).Rotation().Yaw, HipPitch);
		// Kate to one side and the camera in over her shoulder, so she is not between the lens and
		// the arrow and it is close enough to read.
		Kate->SetActorLocation(Kate->GetActorLocation() + Back * -130.f + FVector::CrossProduct(FVector::UpVector, Back) * 110.f);
		Kate->StartAim();
		AimCameraAt(PC, Kate, Stuck->GetActorLocation());
		Test->AddInfo(FString::Printf(TEXT("arrow_stuck: arrow in %s at %s, Kate at %s"), *GetNameSafe(Stuck->GetStuckInActor()),
			*Stuck->GetActorLocation().ToCompactString(), *Kate->GetActorLocation().ToCompactString()));
		break;
	}

	case EBowShot::Cleanup:
		Kate->StopAim();
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			It->Destroy();
		}
		if (AThugCharacter* Thug = TargetThug.Get())
		{
			Thug->Destroy();
		}
		TargetThug.Reset();
		break;

	}
	return true;
}

/** Waits (up to TimeoutSeconds) for an arrow to stick, then reports what it hit. */
class FHawkeyeKateWaitArrowHit : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitArrowHit(FAutomationTestBase* InTest, float InTimeoutSeconds)
		: Test(InTest), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = HawkeyeKateShots::FindWorld();
		if (!World)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			StartTime = World->GetTimeSeconds();
		}
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			if (It->IsStuck())
			{
				const AThugCharacter* Thug = HawkeyeKateShots::TargetThug.Get();
				Test->AddInfo(FString::Printf(TEXT("Arrow stuck in %s at %s after %.2f s; thug health %.1f, staggered %d."),
					*GetNameSafe(It->GetStuckInActor()), *It->GetActorLocation().ToCompactString(),
					World->GetTimeSeconds() - StartTime,
					Thug && Thug->GetHealthComponent() ? Thug->GetHealthComponent()->GetCurrentHealth() : -1.f,
					Thug && Thug->IsStaggered() ? 1 : 0));
				if (It->GetStuckInActor() != Thug)
				{
					Test->AddWarning(TEXT("The arrow did not stick in the thug."));
				}
				return true;
			}
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds)
		{
			Test->AddWarning(TEXT("No arrow stuck anywhere."));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float TimeoutSeconds;
	double StartTime = -1.0;
};

// --- The first fight --------------------------------------------------------------------------

namespace HawkeyeKateFight
{
	static const FName RoofPairTag(TEXT("RoofPair"));
	static const FName StreetPairTag(TEXT("StreetPair"));

	/** Kate stands this far from the roof pair's midpoint when the fight starts. */
	static constexpr float RoofStandOff = 450.f;

	/**
	 * And this far across the avenue from the street pair's patrol line. Outside their 35 degree
	 * cone until they are 16 m off, and by then more than their 15 m sight radius away.
	 */
	static constexpr float StreetStandOff = 1100.f;

	/** How far ahead of the pair, towards the patrol point they walk to next, Kate stands across from. */
	static constexpr float StreetAhead = 900.f;

	/** Yaw the camera swings round by for the heavy's frame, so the body on the floor is not behind her. */
	static constexpr float HitCameraYaw = 65.f;

	enum class EFightShot : uint8
	{
		/** Every placed thug stops thinking, so the other shots are not a brawl. */
		FreezeAll,
		/** Kate across Avenue A from the street pair, who patrol. */
		StreetSetup,
		/** Kate beside the street pair's bat thug as he walks, the camera on him. */
		BatCloseup,
		/** Kate on the cross_block roof, the roof pair still frozen, so the camera can settle. */
		RoofSetup,
		/** The roof pair alerted: they rush her. */
		RoofAlert,
		/** The heavy, once a roof thug is in reach. */
		Heavy,
		/** The camera swung round to see the knocked-down thug beside her. */
		HitCamera,
		/** A dodge sideways from the other thug. */
		Dodge,
		/** Thugs frozen again, Kate mortal again. */
		Cleanup,
	};

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
		Out.Sort([](const AThugCharacter& A, const AThugCharacter& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	static void SetThinking(UWorld* World, FName Tag, bool bEnabled)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (Tag.IsNone() || It->ActorHasTag(Tag))
			{
				if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
				{
					Brain->SetThinkingEnabled(bEnabled);
				}
			}
		}
	}

	static FVector Midpoint(const TArray<AThugCharacter*>& Thugs)
	{
		FVector Sum = FVector::ZeroVector;
		for (const AThugCharacter* Thug : Thugs)
		{
			Sum += Thug->GetActorLocation();
		}
		return Thugs.Num() ? Sum / Thugs.Num() : Sum;
	}

	static AThugCharacter* Nearest(const TArray<AThugCharacter*>& Thugs, const FVector& From, float& OutDistance)
	{
		AThugCharacter* Best = nullptr;
		OutDistance = TNumericLimits<float>::Max();
		for (AThugCharacter* Thug : Thugs)
		{
			const float Distance = FVector::Dist2D(Thug->GetActorLocation(), From);
			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				Best = Thug;
			}
		}
		return Best;
	}

	static void Report(FAutomationTestBase* Test, UWorld* World, const AHawkeyeCharacter* Kate, const TCHAR* Label)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const FVector To = Kate->GetActorLocation() - It->GetActorLocation();
			const float Facing = FVector::DotProduct(It->GetActorForwardVector(), To.GetSafeNormal2D());
			Test->AddInfo(FString::Printf(
				TEXT("%s: %s at %s, %.0f cm from Kate, facing her %.2f, speed %.0f, health %.0f, alert %d, swing %d, down %d, anim %s"),
				Label, *It->GetName(), *It->GetActorLocation().ToCompactString(), To.Size2D(), Facing,
				It->GetVelocity().Size2D(), It->GetHealthComponent()->GetCurrentHealth(),
				static_cast<int32>(It->GetAlertState()), It->GetMeleeComponent()->IsAttacking() ? 1 : 0,
				It->IsKnockedDown() ? 1 : 0, *GetNameSafe(It->GetCurrentLocomotionAnim())));
		}
		Test->AddInfo(FString::Printf(TEXT("%s: Kate at %s, health %.0f, dodging %d, attacking %d"), Label,
			*Kate->GetActorLocation().ToCompactString(), Kate->GetHealthComponent()->GetCurrentHealth(),
			Kate->IsDodging() ? 1 : 0, Kate->IsMeleeAttacking() ? 1 : 0));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateFightShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeKateFightShot::Update()
{
	using namespace HawkeyeKateShots;
	using namespace HawkeyeKateFight;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!Kate)
	{
		Test->AddError(TEXT("No Kate for the fight shots."));
		return true;
	}

	switch (static_cast<EFightShot>(Shot))
	{
	case EFightShot::FreezeAll:
		SetThinking(World, NAME_None, false);
		// Clint stands at his start and the banter stays quiet until the partner shots.
		HawkeyeFreezePartner(World);
		Test->AddInfo(FString::Printf(TEXT("Froze the placed thugs: %d on the roof, %d on the street."),
			Tagged(World, RoofPairTag).Num(), Tagged(World, StreetPairTag).Num()));
		break;

	case EFightShot::StreetSetup:
	{
		const TArray<AThugCharacter*> Pair = Tagged(World, StreetPairTag);
		if (Pair.Num() != 2 || Pair[0]->PatrolPoints.Num() != 2)
		{
			Test->AddWarning(TEXT("No patrolling street pair on the district."));
			break;
		}
		// They walk towards whichever patrol point is further from them (they may already have
		// walked the first leg before the pass froze them).
		const FVector P0 = Pair[0]->PatrolPoints[0]->GetActorLocation();
		const FVector P1 = Pair[0]->PatrolPoints[1]->GetActorLocation();
		const FVector Here = Midpoint(Pair);
		const bool bToP0 = FVector::DistSquared2D(Here, P0) > FVector::DistSquared2D(Here, P1);
		const FVector A = bToP0 ? P0 : P1;
		const FVector B = bToP0 ? P1 : P0;
		const FVector Along = (A - B).GetSafeNormal2D();
		FVector Across = FVector::CrossProduct(FVector::UpVector, Along);
		// Out into the avenue, away from the park: the side the thugs are not walking on.
		FBox Park;
		if (FindParkBounds(World, Park) && FVector::DotProduct(Park.GetCenter() - A, Across) > 0.f)
		{
			Across = -Across;
		}
		const FVector Mid = Midpoint(Pair) + Along * StreetAhead;
		FVector Ground;
		if (!FindGround(World, Mid + Across * StreetStandOff, 3000.f, Kate, Ground))
		{
			Test->AddWarning(TEXT("No ground across the avenue from the street pair."));
			break;
		}
		PlaceKate(Kate, PC, Ground, (-Across).Rotation().Yaw, HipPitch);
		SetThinking(World, StreetPairTag, true);
		// Between where they start and where they will be at the shot.
		AimCameraAt(PC, Kate, Midpoint(Pair) + Along * (StreetAhead * 0.5f) + FVector(0.f, 0.f, 20.f));
		Report(Test, World, Kate, TEXT("street_patrol (setup)"));
		break;
	}

	case EFightShot::BatCloseup:
	{
		AThugCharacter* Batter = nullptr;
		for (AThugCharacter* Thug : Tagged(World, StreetPairTag))
		{
			if (Thug->Weapon == EThugWeapon::Bat)
			{
				Batter = Thug;
			}
		}
		if (!Batter)
		{
			Test->AddWarning(TEXT("thug_walk_bat.png: no bat thug in the street pair."));
			break;
		}
		// Square to his path, outside his sight cone, so he keeps walking.
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Batter->GetActorForwardVector().GetSafeNormal2D());
		FVector Ground;
		for (const float Sign : { 1.f, -1.f })
		{
			if (FindGround(World, Batter->GetActorLocation() + Side * Sign * 400.f, Batter->GetActorLocation().Z + 300.f, Kate, Ground)
				&& FMath::Abs(Ground.Z - (Batter->GetActorLocation().Z - 96.f)) < 60.f)
			{
				PlaceKate(Kate, PC, Ground, (-Side * Sign).Rotation().Yaw, HipPitch);
				AimCameraAt(PC, Kate, Batter->GetActorLocation() + Batter->GetActorForwardVector() * 80.f);
				break;
			}
		}
		Report(Test, World, Kate, TEXT("thug_walk_bat (setup)"));
		break;
	}

	case EFightShot::RoofSetup:
	{
		SetThinking(World, StreetPairTag, false);
		const TArray<AThugCharacter*> Pair = Tagged(World, RoofPairTag);
		if (Pair.Num() != 2)
		{
			Test->AddWarning(TEXT("No roof pair on the district."));
			break;
		}
		const FVector Mid = Midpoint(Pair);
		FVector Axis = (Pair[1]->GetActorLocation() - Pair[0]->GetActorLocation()).GetSafeNormal2D();
		// Along the roof's long axis, at whichever end leaves the camera room behind her: the
		// cross_block roof backs onto a taller neighbour at one end.
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector Stand = Mid - Axis * Sign * RoofStandOff + FVector(0.f, 0.f, 60.f);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(KateFightCamera), false, Kate);
			if (!World->LineTraceSingleByChannel(Hit, Stand, Stand - Axis * Sign * 400.f, ECC_Camera, Params))
			{
				Axis *= Sign;
				break;
			}
		}
		FVector Ground;
		if (!FindGround(World, Mid - Axis * RoofStandOff, Mid.Z + 300.f, Kate, Ground))
		{
			Test->AddWarning(TEXT("No roof to stand on beside the roof pair."));
			break;
		}
		PlaceKate(Kate, PC, Ground, Axis.Rotation().Yaw, HipPitch);
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Report(Test, World, Kate, TEXT("fight_roof (setup)"));
		break;
	}

	case EFightShot::RoofAlert:
	{
		const TArray<AThugCharacter*> Pair = Tagged(World, RoofPairTag);
		SetThinking(World, RoofPairTag, true);
		for (AThugCharacter* Thug : Pair)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
			{
				Brain->SetTarget(Kate);
				Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			}
		}
		break;
	}

	case EFightShot::Heavy:
	{
		float Distance = 0.f;
		const AThugCharacter* Target = Nearest(Tagged(World, RoofPairTag), Kate->GetActorLocation(), Distance);
		if (!Kate->StartHeavyAttack())
		{
			Test->AddWarning(TEXT("The heavy would not start."));
		}
		Test->AddInfo(FString::Printf(TEXT("fight_hit: heavy started at %s, %.0f cm away."), *GetNameSafe(Target), Distance));
		break;
	}

	case EFightShot::HitCamera:
	{
		const FRotator Control = PC->GetControlRotation();
		PC->SetControlRotation(FRotator(Control.Pitch - 8.f, Control.Yaw + HitCameraYaw, 0.f));
		break;
	}

	case EFightShot::Dodge:
	{
		float Distance = 0.f;
		const TArray<AThugCharacter*> Standing = Tagged(World, RoofPairTag).FilterByPredicate(
			[](const AThugCharacter* Thug) { return !Thug->IsKnockedDown(); });
		const AThugCharacter* Threat = Nearest(Standing, Kate->GetActorLocation(), Distance);
		// Sideways to whoever is still coming, so the camera sees her slip past him.
		const FVector From = Threat ? (Kate->GetActorLocation() - Threat->GetActorLocation()).GetSafeNormal2D()
			: -Kate->GetActorForwardVector();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, From);
		if (!Kate->TryDodge(Side))
		{
			Test->AddWarning(TEXT("The dodge was refused."));
		}
		Report(Test, World, Kate, TEXT("fight_dodge (start)"));
		break;
	}

	case EFightShot::Cleanup:
		SetThinking(World, NAME_None, false);
		Kate->GetHealthComponent()->SetInvulnerable(false);
		break;
	}
	return true;
}

/** Waits (up to TimeoutSeconds) for a roof thug to come within Reach of Kate. */
class FHawkeyeKateWaitThugInReach : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitThugInReach(FAutomationTestBase* InTest, float InReach, float InTimeoutSeconds)
		: Test(InTest), Reach(InReach), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = HawkeyeKateShots::FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Kate = PC ? PC->GetPawn() : nullptr;
		if (!Kate)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			StartTime = World->GetTimeSeconds();
		}
		float Distance = 0.f;
		HawkeyeKateFight::Nearest(HawkeyeKateFight::Tagged(World, HawkeyeKateFight::RoofPairTag), Kate->GetActorLocation(), Distance);
		if (Distance <= Reach)
		{
			Test->AddInfo(FString::Printf(TEXT("A roof thug is %.0f cm from Kate after %.2f s."), Distance,
				World->GetTimeSeconds() - StartTime));
			return true;
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds)
		{
			Test->AddWarning(FString::Printf(TEXT("No roof thug came within %.0f cm (nearest %.0f)."), Reach, Distance));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float Reach;
	float TimeoutSeconds;
	double StartTime = -1.0;
};

/** Waits (up to TimeoutSeconds) for a roof thug to be knocked down, then reports the fight. */
class FHawkeyeKateWaitKnockdown : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitKnockdown(FAutomationTestBase* InTest, float InTimeoutSeconds)
		: Test(InTest), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = HawkeyeKateShots::FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!Kate)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			StartTime = World->GetTimeSeconds();
		}
		for (const AThugCharacter* Thug : HawkeyeKateFight::Tagged(World, HawkeyeKateFight::RoofPairTag))
		{
			if (Thug->IsKnockedDown())
			{
				Test->AddInfo(FString::Printf(TEXT("%s knocked down after %.2f s (ragdoll %d)."), *Thug->GetName(),
					World->GetTimeSeconds() - StartTime, Thug->IsRagdolling() ? 1 : 0));
				HawkeyeKateFight::Report(Test, World, Kate, TEXT("fight_hit"));
				return true;
			}
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds)
		{
			Test->AddWarning(TEXT("The heavy knocked nobody down."));
			HawkeyeKateFight::Report(Test, World, Kate, TEXT("fight_hit (missed)"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float TimeoutSeconds;
	double StartTime = -1.0;
};

/** Reports the fight state at the moment of a shot. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateReportFight, FAutomationTestBase*, Test, FString, Label);

bool FHawkeyeKateReportFight::Update()
{
	UWorld* World = HawkeyeKateShots::FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (const AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr)
	{
		HawkeyeKateFight::Report(Test, World, Kate, *Label);
	}
	return true;
}

// --- The ways down: fire escape, hang from a landing, a level zip --------------------------------

namespace HawkeyeKateShots
{
	enum class EWaysDownShot : uint8
	{
		FireEscape,
		LandingStand,
		LandingDrop,
		LevelZipSetup,
		LevelZipGo,
	};

	/** The four-storey-or-taller fire escape nearest the street spot: its lowest landing. */
	static AFireEscapeLanding* FindShowcaseEscape(UWorld* World)
	{
		FVector Spot, Away;
		if (!FindStreetSpot(World, Spot, Away))
		{
			return nullptr;
		}
		TMap<FName, int32> Floors;
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			for (const FName& Tag : It->Tags)
			{
				if (Tag.ToString().StartsWith(TEXT("osm:")))
				{
					Floors.FindOrAdd(Tag) = FMath::Max(Floors.FindRef(Tag), It->GetRecord().Floor);
				}
			}
		}
		AFireEscapeLanding* Best = nullptr;
		float BestDistance = BIG_NUMBER;
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			const FName* Osm = It->Tags.FindByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("osm:")); });
			if (It->GetRecord().Floor != 1 || !Osm || Floors.FindRef(*Osm) < 4)
			{
				continue;
			}
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Spot);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	static AFireEscapeLanding* FindLanding(UWorld* World, const AFireEscapeLanding* Lowest, int32 Floor)
	{
		if (!Lowest)
		{
			return nullptr;
		}
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			if (It->GetRecord().OsmId == Lowest->GetRecord().OsmId && It->GetRecord().Floor == Floor)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** A roof spot and an anchor on another roof within 60 cm of the same height, a clear level zip. */
	static bool FindLevelZip(UWorld* World, const AHawkeyeCharacter* Kate, FVector& OutStand, AGrappleAnchor*& OutAnchor)
	{
		FVector Spot, Away;
		if (!FindStreetSpot(World, Spot, Away))
		{
			return false;
		}
		const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		TArray<AGrappleAnchor*> Anchors;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Spot) < 9000.f)
			{
				Anchors.Add(*It);
			}
		}
		Anchors.Sort([&Spot](const AGrappleAnchor& A, const AGrappleAnchor& B)
		{
			return FVector::Dist2D(A.GetActorLocation(), Spot) < FVector::Dist2D(B.GetActorLocation(), Spot);
		});
		for (AGrappleAnchor* From : Anchors)
		{
			const FVector Stand = From->GetLandingLocation();
			for (AGrappleAnchor* To : Anchors)
			{
				const float Distance = FVector::Dist2D(Stand, To->GetLandingLocation());
				if (To == From || Distance < 1200.f || Distance > 2000.f
					|| FMath::Abs(To->GetLandingLocation().Z - Stand.Z) > 60.f)
				{
					continue;
				}
				// Across a gap: the middle of the line is over the street, not a roof.
				FVector Mid;
				if (FindGround(World, (Stand + To->GetLandingLocation()) * 0.5f, Stand.Z + 500.f, Kate, Mid) && Mid.Z > Stand.Z - 600.f)
				{
					continue;
				}
				if (Grapple->IsZipClear(Stand + FVector(0.f, 0.f, Half + 2.f), To, true))
				{
					OutStand = Stand;
					OutAnchor = To;
					return true;
				}
			}
		}
		return false;
	}
}

/** Sets up one of the ways-down shots. */
class FHawkeyeKateWaysDownShot : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaysDownShot(FAutomationTestBase* InTest, uint8 InShot) : Test(InTest), Shot(InShot) {}

	virtual bool Update() override
	{
		using namespace HawkeyeKateShots;
		UWorld* World = FindWorld();
		APlayerController* PC = nullptr;
		AHawkeyeCharacter* Kate = FindKate(PC);
		if (!Kate || !World)
		{
			Test->AddError(TEXT("No Kate for the ways-down shots."));
			return true;
		}
		AFireEscapeLanding* Lowest = FindShowcaseEscape(World);
		switch (static_cast<EWaysDownShot>(Shot))
		{
		case EWaysDownShot::FireEscape:
		{
			if (!Lowest)
			{
				Test->AddWarning(TEXT("No fire escape of four landings near the street spot."));
				break;
			}
			// On the street 9 m out and 3 m along, looking up at the escape's middle.
			const FVector Out = Lowest->GetActorRightVector().GetSafeNormal2D();
			const FVector Along = Lowest->GetActorForwardVector().GetSafeNormal2D();
			FVector Ground;
			if (!FindGround(World, Lowest->GetActorLocation() + Out * 900.f + Along * 300.f, Lowest->GetActorLocation().Z + 200.f, Kate, Ground))
			{
				break;
			}
			const FVector Middle = Lowest->GetActorLocation() + FVector(0.f, 0.f, 500.f);
			const FRotator Look = (Middle - (Ground + FVector(0.f, 0.f, 170.f))).Rotation();
			PlaceKate(Kate, PC, Ground, Look.Yaw, Kate->ClampCameraPitch(Look.Pitch));
			Test->AddInfo(FString::Printf(TEXT("Fire escape: %s, lowest landing %.0f cm up at %s"), *Lowest->GetRecord().OsmId,
				Lowest->GetActorLocation().Z - Ground.Z, *Lowest->GetActorLocation().ToCompactString()));
			break;
		}
		case EWaysDownShot::LandingStand:
		{
			AFireEscapeLanding* Landing = FindLanding(World, Lowest, 2);
			if (!Landing)
			{
				Test->AddWarning(TEXT("No second-floor landing for hang_drop.png."));
				break;
			}
			const FVector Out = Landing->GetActorRightVector().GetSafeNormal2D();
			PlaceKate(Kate, PC, Landing->GetActorLocation() + Out * 45.f, Out.Rotation().Yaw, -10.f);
			break;
		}
		case EWaysDownShot::LandingDrop:
		{
			AFireEscapeLanding* Landing = FindLanding(World, Lowest, 2);
			if (!Landing)
			{
				break;
			}
			const FVector Out = Landing->GetActorRightVector().GetSafeNormal2D();
			const bool bStarted = Kate->GetParkourComponent()->TryDropToHang(Out, TEXT("screenshot"));
			// Seen from out over the street, a little to one side and below.
			PC->SetControlRotation(FRotator(8.f, (-Out).Rotation().Yaw + 30.f, 0.f));
			Test->AddInfo(FString::Printf(TEXT("hang_drop: drop to hang from the %s landing %s"), *Landing->GetRecord().OsmId,
				bStarted ? TEXT("started") : TEXT("REFUSED")));
			if (!bStarted)
			{
				Test->AddWarning(TEXT("hang_drop.png: no drop to hang from the landing."));
			}
			break;
		}
		case EWaysDownShot::LevelZipSetup:
		{
			if (Kate->GetParkourComponent()->IsHanging())
			{
				Kate->GetParkourComponent()->DropFromHang();
			}
			FVector Stand;
			AGrappleAnchor* Anchor = nullptr;
			if (!FindLevelZip(World, Kate, Stand, Anchor))
			{
				Test->AddWarning(TEXT("No clear level roof-to-roof zip near the street spot for grapple_level.png."));
				break;
			}
			const FRotator Look = (Anchor->GetMarkerLocation() - Stand).Rotation();
			PlaceKate(Kate, PC, Stand, Look.Yaw, 0.f);
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			LevelAnchor = Anchor;
			Test->AddInfo(FString::Printf(TEXT("grapple_level: from %s to %s, %.0f cm, height difference %.0f cm"),
				*Stand.ToCompactString(), *Anchor->GetName(), FVector::Dist(Stand, Anchor->GetLandingLocation()),
				Anchor->GetLandingLocation().Z - Stand.Z));
			break;
		}
		case EWaysDownShot::LevelZipGo:
		{
			AGrappleAnchor* Anchor = LevelAnchor.Get();
			if (!Anchor)
			{
				break;
			}
			UGrappleComponent* Grapple = Kate->GetGrappleComponent();
			Grapple->StartZip(Anchor);
			// Side on, so the gap below shows.
			const FVector Dir = (Anchor->GetLandingLocation() - Kate->GetActorLocation()).GetSafeNormal2D();
			PC->SetControlRotation(FRotator(-8.f, Dir.Rotation().Yaw - 70.f, 0.f));
			break;
		}
		}
		return true;
	}

	static TWeakObjectPtr<AGrappleAnchor> LevelAnchor;

private:
	FAutomationTestBase* Test;
	uint8 Shot;
};

TWeakObjectPtr<AGrappleAnchor> FHawkeyeKateWaysDownShot::LevelAnchor;

/** Waits until the zip is Fraction of the way along (or over), up to TimeoutSeconds; reports it. */
class FHawkeyeKateWaitZipFraction : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitZipFraction(FAutomationTestBase* InTest, float InFraction, float InTimeoutSeconds)
		: Test(InTest), Fraction(InFraction), TimeoutSeconds(InTimeoutSeconds) {}

	virtual bool Update() override
	{
		APlayerController* PC = nullptr;
		AHawkeyeCharacter* Kate = HawkeyeKateShots::FindKate(PC);
		UWorld* World = HawkeyeKateShots::FindWorld();
		if (!Kate || !World)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			StartTime = World->GetTimeSeconds();
		}
		const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		if (Grapple->IsZipping() && Grapple->GetZipProgress() >= Fraction)
		{
			Test->AddInfo(FString::Printf(TEXT("grapple_level: %.0f%% along, feet %.0f cm above the launch roof"),
				Grapple->GetZipProgress() * 100.f, Kate->GetActorLocation().Z - Grapple->GetZipStart().Z));
			return true;
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds || (!Grapple->IsZipping() && World->GetTimeSeconds() - StartTime > 0.3))
		{
			Test->AddWarning(FString::Printf(TEXT("grapple_level: the zip ended or stalled before %.0f%% (Kate at %s)"), Fraction * 100.f,
				*Kate->GetActorLocation().ToCompactString()));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float Fraction;
	float TimeoutSeconds;
	double StartTime = -1.0;
};

// --- The landing roll -----------------------------------------------------------------------------

namespace HawkeyeKateShots
{
	/** Where the roll started, for the distance it covered. */
	static FVector RollStart = FVector::ZeroVector;
}

/**
 * Lifts Kate 6 m above the street (camera on the park, so holding right runs her along the street
 * side on) and holds the stick right. Then waits for the roll and returns once it is MidSeconds in.
 */
class FHawkeyeKateWaitRoll : public IAutomationLatentCommand
{
public:
	FHawkeyeKateWaitRoll(FAutomationTestBase* InTest, float InMidSeconds, float InTimeoutSeconds)
		: Test(InTest), MidSeconds(InMidSeconds), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		using namespace HawkeyeKateShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!Kate)
		{
			return true;
		}
		if (StartTime < 0.0)
		{
			FVector Spot, Away, Ground;
			if (!FindStreetSpot(World, Spot, Away) || !FindGround(World, Spot, 3000.f, Kate, Ground))
			{
				Test->AddWarning(TEXT("roll_mid.png: no street spot."));
				return true;
			}
			StartTime = World->GetTimeSeconds();
			PlaceKate(Kate, PC, Ground + FVector(0.f, 0.f, 600.f), (-Away).Rotation().Yaw, HipPitch);
			HoldMove(PC, FVector2D(1.f, 0.f), true);
			return false;
		}
		if (Kate->IsRolling())
		{
			if (!bRolling)
			{
				bRolling = true;
				RollStart = Kate->GetActorLocation();
				Test->AddInfo(FString::Printf(TEXT("roll_mid: rolling after a %.0f cm landing toward %s, capsule half-height %.0f"),
					Kate->GetLastFallHeight(), *Kate->GetRollDirection().ToCompactString(),
					Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
			}
			if (Kate->GetLandingElapsed() >= MidSeconds)
			{
				Test->AddInfo(FString::Printf(TEXT("roll_mid: %.2f s in, camera pitch %.1f, input locked %d, %.0f cm travelled, body at %s rot %s scale %s"),
					Kate->GetLandingElapsed(), Kate->GetLandingCameraPitch(), Kate->IsLandingInputLocked() ? 1 : 0,
					FVector::Dist2D(RollStart, Kate->GetActorLocation()), *Kate->GetMesh()->GetRelativeLocation().ToCompactString(),
					*Kate->GetMesh()->GetRelativeRotation().ToCompactString(), *Kate->GetMesh()->GetRelativeScale3D().ToCompactString()));
				TArray<USkeletalMeshComponent*> Meshes;
				Kate->GetComponents(Meshes);
				for (const USkeletalMeshComponent* Mesh : Meshes)
				{
					Test->AddInfo(FString::Printf(TEXT("roll_mid: skeletal mesh %s (%s) on %s, visible %d, world rot %s"), *Mesh->GetName(),
						*GetNameSafe(Mesh->GetSkeletalMeshAsset()), *GetNameSafe(Mesh->GetAttachParent()), Mesh->IsVisible() ? 1 : 0,
						*Mesh->GetComponentRotation().ToCompactString()));
				}
				return true;
			}
		}
		if (World->GetTimeSeconds() - StartTime > TimeoutSeconds)
		{
			Test->AddWarning(FString::Printf(TEXT("roll_mid.png: no roll (landing %.0f cm, state %d)."), Kate->GetLastFallHeight(),
				static_cast<int32>(Kate->GetLandingState())));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	float MidSeconds;
	float TimeoutSeconds;
	double StartTime = -1.0;
	bool bRolling = false;
};

/** After the roll: lets go of the stick and reports how far it carried her. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateEndRoll, FAutomationTestBase*, Test);

bool FHawkeyeKateEndRoll::Update()
{
	using namespace HawkeyeKateShots;
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	if (!Kate)
	{
		return true;
	}
	HoldMove(PC, FVector2D::ZeroVector, false);
	Test->AddInfo(FString::Printf(TEXT("roll: over, %.0f cm from where it started (includes the run after it), capsule half-height %.0f, rolling %d"),
		FVector::Dist2D(RollStart, Kate->GetActorLocation()), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),
		Kate->IsRolling() ? 1 : 0));
	return true;
}

// --- Objective marker, compass and toasts -------------------------------------------------------

namespace HawkeyeKateShots
{
	enum class EObjectiveShot : uint8
	{
		Marker,
		Offscreen,
		Complete,
	};

	static AObjectiveTriggerVolume* FindObjectiveVolume(UWorld* World, FName ObjectiveId)
	{
		for (TActorIterator<AObjectiveTriggerVolume> It(World); It; ++It)
		{
			if (It->ObjectiveId == ObjectiveId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static UHawkeyeObjectiveWidget* FindObjectiveWidget(APlayerController* PC)
	{
		const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
		const UHawkeyeHudWidget* Hud = HawkeyePC ? HawkeyePC->GetHawkeyeHud() : nullptr;
		return Hud ? Hud->GetObjectiveMarker() : nullptr;
	}

	/** Control rotation from Kate's head toward Target, pitch kept where the arm stays behind her. */
	static FRotator LookAt(const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const FVector From = Kate->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		const FVector Delta = Target - From;
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, Delta.Size2D()));
		return FRotator(FMath::Clamp(Pitch, -30.f, 30.f), Delta.Rotation().Yaw, 0.f);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeKateObjectiveShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeKateObjectiveShot::Update()
{
	using namespace HawkeyeKateShots;

	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	UWorld* World = FindWorld();
	const AObjectiveTriggerVolume* Roof = World ? FindObjectiveVolume(World, TEXT("reach_roof")) : nullptr;
	if (!Kate || !Roof)
	{
		Test->AddError(TEXT("objective shots: no Kate or City_Obj_reach_roof."));
		return true;
	}

	FVector Ground;
	switch (static_cast<EObjectiveShot>(Shot))
	{
	case EObjectiveShot::Marker:
	{
		TActorIterator<APlayerStart> StartIt(World);
		const FVector Start = StartIt ? StartIt->GetActorLocation() : Kate->GetActorLocation();
		if (FindGround(World, Start, Start.Z + 300.f, Kate, Ground))
		{
			PlaceKate(Kate, PC, Ground, 0.f, 0.f);
			PC->SetControlRotation(LookAt(Kate, Roof->GetActorLocation()));
		}
		break;
	}

	case EObjectiveShot::Offscreen:
	{
		FRotator Away = PC->GetControlRotation();
		Away.Yaw += 150.f;
		Away.Pitch = HipPitch;
		PC->SetControlRotation(Away);
		break;
	}

	case EObjectiveShot::Complete:
	{
		// Onto the roof inside the volume: the overlap completes reach_roof and the toast starts.
		const FVector Centre = Roof->GetActorLocation();
		if (FindGround(World, Centre, Centre.Z, Kate, Ground))
		{
			const AObjectiveTriggerVolume* Next = FindObjectiveVolume(World, TEXT("cross_block"));
			PlaceKate(Kate, PC, Ground, 0.f, 0.f);
			PC->SetControlRotation(Next ? LookAt(Kate, Next->GetActorLocation()) : FRotator(-10.f, 0.f, 0.f));
		}
		break;
	}
	}
	return true;
}

/** Logs what the objective HUD is showing, and fails the shot it was framed for if it is not there. */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FHawkeyeKateReportObjective, FAutomationTestBase*, Test, FString, Label,
	uint8, Shot);

bool FHawkeyeKateReportObjective::Update()
{
	using namespace HawkeyeKateShots;

	APlayerController* PC = nullptr;
	FindKate(PC);
	const UHawkeyeObjectiveWidget* Widget = FindObjectiveWidget(PC);
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(PC);
	if (!Widget || !Missions)
	{
		Test->AddError(FString::Printf(TEXT("%s: no objective widget or mission subsystem."), *Label));
		return true;
	}

	const FObjectiveMarkerPlacement Placement = Widget->GetMarkerPlacement();
	const UMissionObjective* Current = Missions->GetCurrentObjective();
	Test->AddInfo(FString::Printf(
		TEXT("%s: current %s, marker visible %d on screen %d at (%.0f, %.0f) arrow %.0f deg, distance '%s', ")
		TEXT("view bearing %.0f, compass icon %d at %+.0f px, toast '%s' / '%s'"),
		*Label, Current ? *Current->ObjectiveId.ToString() : TEXT("none"), Widget->IsMarkerVisible() ? 1 : 0,
		Placement.bOnScreen ? 1 : 0, Placement.Position.X, Placement.Position.Y, Placement.ArrowAngleDegrees,
		*Widget->GetDistanceText().ToString(), Widget->GetViewBearing(), Widget->IsCompassIconVisible() ? 1 : 0,
		Widget->GetCompassIconOffset(), *Widget->GetToastHeading().ToString(), *Widget->GetToastTitle().ToString()));

	const TArray<UMissionObjective*> Objectives = Missions->GetActiveObjectives();
	switch (static_cast<EObjectiveShot>(Shot))
	{
	case EObjectiveShot::Marker:
		if (!Widget->IsMarkerVisible() || !Placement.bOnScreen)
		{
			Test->AddError(FString::Printf(TEXT("%s: the objective marker is not on screen."), *Label));
		}
		break;
	case EObjectiveShot::Offscreen:
		if (!Widget->IsMarkerVisible() || Placement.bOnScreen)
		{
			Test->AddError(FString::Printf(TEXT("%s: the marker should be held at the screen edge."), *Label));
		}
		break;
	case EObjectiveShot::Complete:
		if (Objectives.Num() == 0 || !Objectives[0]->IsCompleted() || !Widget->IsToastVisible()
			|| !Widget->GetToastTitle().EqualTo(Objectives[0]->Title))
		{
			Test->AddError(FString::Printf(TEXT("%s: no \"Objective complete\" toast for reach_roof."), *Label));
		}
		break;
	}
	return true;
}

/** Kate 15 m inside the park, facing the tenement row across East 7th Street, her camera tipped up to the roofs. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeKateParkShot, FAutomationTestBase*, Test);

bool FHawkeyeKateParkShot::Update()
{
	using namespace HawkeyeKateShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	FVector Spot, Away, Ground;
	if (!Kate || !FindStreetSpot(World, Spot, Away))
	{
		Test->AddError(TEXT("day_park.png: no Kate, PlayerStart or CityPark."));
		return true;
	}
	const FVector InPark = Spot - Away * (StreetOffsetFromPark + 1500.f);
	if (FindGround(World, InPark, 3000.f, Kate, Ground))
	{
		PlaceKate(Kate, PC, Ground, Away.Rotation().Yaw, 6.f);
		Test->AddInfo(FString::Printf(TEXT("day_park.png: Kate at %s facing yaw %.0f."), *Ground.ToCompactString(), Away.Rotation().Yaw));
	}
	else
	{
		Test->AddWarning(TEXT("day_park.png: no ground in the park."));
	}
	return true;
}

namespace HawkeyeKateShots
{
	/**
	 * night_street.png and day_street.png from kate_street's camera; with bDayRound, also day_park.png (from the
	 * park at the tenement row) and day_pedestal.png (challenge_pedestal's framing) while it is Day.
	 * hawkeye.TimeOfDay does not save, and "Saved" hands the district back to whatever the player's setting is.
	 */
	static void AddTimeOfDayShots(FAutomationTestBase* Test, bool bDayRound = false)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("hawkeye.TimeOfDay Night")));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(Test, static_cast<uint8>(EShot::Street)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(Test, TEXT("night_street.png")));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
		// Lumen and the sky light's real-time capture take a couple of seconds to settle on the new sky.
		ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("hawkeye.TimeOfDay Day")));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(Test, TEXT("day_street.png")));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
		if (bDayRound)
		{
			ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkShot(Test));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
			ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(Test, TEXT("day_park.png")));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
			HawkeyeAddChallengePedestalShot(Test, TEXT("day_pedestal.png"));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("hawkeye.TimeOfDay Saved")));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotTimeOfDay, "Hawkeye.Screenshot.TimeOfDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

/**
 * The time-of-day pair from the Kate pass plus the day round (day_park.png, day_pedestal.png), for tuning the
 * day without the whole pass.
 */
bool FHawkeyeScreenshotTimeOfDay::RunTest(const FString& Parameters)
{
	using namespace HawkeyeKateShots;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the time of day screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(HawkeyeKateFight::EFightShot::FreezeAll)));
	AddTimeOfDayShots(this, /*bDayRound=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

bool FHawkeyeScreenshotKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeKateShots;

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the Kate screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}

	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));

	// The district's thugs stand still until their own shots.
	using EFight = HawkeyeKateFight::EFightShot;
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::FreezeAll)));

	// The objective marker, first, while reach_roof is still current and its volume untriggered.
	using EObjective = HawkeyeKateShots::EObjectiveShot;
	const TPair<EObjective, const TCHAR*> ObjectiveShots[] = {
		{ EObjective::Marker, TEXT("objective_marker.png") },
		{ EObjective::Offscreen, TEXT("objective_offscreen.png") },
	};
	for (const TPair<EObjective, const TCHAR*>& ShotAndFile : ObjectiveShots)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateObjectiveShot(this, static_cast<uint8>(ShotAndFile.Key)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportObjective(this, FString(ShotAndFile.Value),
			static_cast<uint8>(ShotAndFile.Key)));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, FString(ShotAndFile.Value)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}
	// Inside the 2 s the toast is up.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateObjectiveShot(this, static_cast<uint8>(EObjective::Complete)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportObjective(this, TEXT("objective_complete.png"),
		static_cast<uint8>(EObjective::Complete)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("objective_complete.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// The debug line goes into the shots so gait and fall height can be read off them.
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("hawkeye.DebugMovement 1")));

	const TPair<EShot, const TCHAR*> Shots[] = {
		{ EShot::Street, TEXT("kate_street.png") },
		{ EShot::Aim, TEXT("kate_aim.png") },
		{ EShot::Roof, TEXT("kate_roof.png") },
		{ EShot::Wall, TEXT("kate_wall.png") },
	};
	for (const TPair<EShot, const TCHAR*>& ShotAndFile : Shots)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(ShotAndFile.Key)));
		// Camera lag and the aim blend both need to arrive before the capture.
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, FString(ShotAndFile.Value)));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, FString(ShotAndFile.Value)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	}
	// The costume, close, in lamp light, from a camera in front of her.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Costume)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateCostumeCamera(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("kate_costume.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// The world map open at the PlayerStart: the district, its icons and the legend (MapScreenshotTest.cpp).
	HawkeyeAddMapShots(this);

	// The first-time hints: sprint, grapple and parry, each on the line above the hotbar (HintScreenshotTest.cpp).
	HawkeyeAddHintShots(this);

	// The same street by night and by day (hawkeye.TimeOfDay), then back to the setting.
	AddTimeOfDayShots(this);

	// The bow: on her back, the quiver, half drawn, then an arrow into a thug 15 m away.
	using EBow = HawkeyeKateShots::EBowShot;
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Street)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::Holster)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("bow_holstered.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("quiver_hud.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::DrawStart)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("bow_draw.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::DrawCancel)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::HitSetup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::HitAim)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::HitDraw)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.85f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::HitRelease)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitArrowHit(this, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("bow_hit.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::StuckView)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("arrow_stuck.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateBowShot(this, static_cast<uint8>(EBow::Cleanup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Both hands on the bow, close up: at rest in the hand, half and full draw, Clint and an archer.
	HawkeyeAddBowIKShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// Locomotion: a run, the stop after it, and an aimed strafe, captured mid-move.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Run)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("kate_run.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("kate_run.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Stop)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("kate_stop.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("kate_stop.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::AimStrafe)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("kate_aimstrafe.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("kate_aimstrafe.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::EndMove)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// A 12 m drop: past the roll height and the fall-damage threshold.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateDrop(this, 1200.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateDrop(this, 1200.f, true));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// A 6 m drop with the stick held: the landing roll, side on. Motion blur off for this one: at
	// the middle of the tumble the body turns over 1300 degrees a second and smears to nothing.
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("ShowFlag.MotionBlur 0")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitRoll(this, 0.2f, 4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("roll_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("ShowFlag.MotionBlur 2")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateEndRoll(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Frame time at the street start, before anything moves.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Street)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameTime(this, TEXT("street"), 60));

	// The effects, 30 m down the street (VfxScreenshotTest.cpp), then back to the street spot.
	HawkeyeAddVfxShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameShot(this, static_cast<uint8>(EShot::Street)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));

	// The grapple: marker, mid-zip, landed.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateAimAtAnchor(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportMarker(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("grapple_marker.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFireGrapple(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitZip(this, 0.45f, 3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("grapple_mid.png")));
	// Further along, the line from her hand to the anchor (NS_ZipLine).
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitZip(this, 0.6f, 3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("vfx_zipline.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitZip(this, 2.f, 5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateLookAcrossRoof());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("grapple_roof.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("grapple_roof.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFrameTime(this, TEXT("roof"), 60));

	// Looking steeply up from the street, then the parkour moves.
	using EParkour = HawkeyeKateShots::EParkourShot;
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::Lookup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportLookup(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("kate_lookup.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("kate_lookup.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::VaultRun)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 4.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("vault_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("vault_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::EndInput)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 2.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.45f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("mantle_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("mantle_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));

	// From a standstill against the 90 cm block, one press (2026-09-29): a vault, not a hop into the wall.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::VaultStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 1.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("standing_vault.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("standing_vault.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));

	// The same moves as our own (2026-11-10): the clip's root motion warped to the probe, at two mantle heights.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::OwnRoute)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::VaultStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 1.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("own_vault_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("own_vault_mid.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 1.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.35f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("own_mantle_150.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("own_mantle_150.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::TallMantleStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 1.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("own_mantle_185.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("own_mantle_185.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("own_mantle_185_top.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("own_mantle_185_top.png")));
	// The capture is queued: the block stays until it is taken.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::SampleRoute)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Walking at a roof-edge parapet, one press (2026-09-29, a 1679 cm fall): onto its top, and she stays there.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::RoofEdge)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::MantleJump)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 1.f, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::RoofEdgeHold)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("roof_edge_guard.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::EndInput)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::LedgeFall)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 3.f, true));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("ledge_hang.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("ledge_hang.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateParkourShot(this, static_cast<uint8>(EParkour::Climb)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.4f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateLookAcrossRoof());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("climb_top.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportCamera(this, TEXT("climb_top.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("climb_top.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// The ways down: a fire escape from the street, hanging from one of its landings, a level zip.
	using EWaysDown = HawkeyeKateShots::EWaysDownShot;
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaysDownShot(this, static_cast<uint8>(EWaysDown::FireEscape)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("fire_escape.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaysDownShot(this, static_cast<uint8>(EWaysDown::LandingStand)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaysDownShot(this, static_cast<uint8>(EWaysDown::LandingDrop)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitTraversal(this, 2.f, true));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportParkour(this, TEXT("hang_drop.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("hang_drop.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	// Shimmying along a tenement parapet and round its corner (ParkourHangDistrictTest.cpp).
	HawkeyeAddHangShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaysDownShot(this, static_cast<uint8>(EWaysDown::LevelZipSetup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaysDownShot(this, static_cast<uint8>(EWaysDown::LevelZipGo)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitZipFraction(this, 0.45f, 3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("grapple_level.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));

	// The first fight: the street pair on patrol, then the roof pair.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::StreetSetup)));
	// Up to PatrolWaitSeconds at the point they stand on, then the walk towards her.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportFight(this, TEXT("street_patrol.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("street_patrol.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::BatCloseup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportFight(this, TEXT("thug_walk_bat.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("thug_walk_bat.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::RoofSetup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::RoofAlert)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportFight(this, TEXT("fight_roof.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("fight_roof.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitThugInReach(this, 170.f, 4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::Heavy)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateWaitKnockdown(this, 1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::HitCamera)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.35f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("fight_hit.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::Dodge)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.15f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateReportFight(this, TEXT("fight_dodge.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateTakeShot(this, TEXT("fight_dodge.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeKateFightShot(this, static_cast<uint8>(EFight::Cleanup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// The enemies, while the street pair is still standing: the squad alert, the gunner's peek from
	// cover, the archer's draw and his arrow.
	HawkeyeAddEnemyShots(this);

	// Fair fights: the heavy's shield and bash, an archer's held draw, the alert glyphs, the aim view.
	HawkeyeAddCombatShots(this);

	// Melee: a combo counter at x3, a parry, a finisher in its slow motion, a hit lean, a punch's arm.
	HawkeyeAddMeleeShots(this);

	// Clint: following, shooting what she hit, a banter line, and the switch to him.
	HawkeyeAddPartnerShots(this);

	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("hawkeye.DebugMovement 0")));

	// The side challenges: a pedestal, the archery range and its results card, a traversal ring ahead.
	HawkeyeAddChallengeShots(this);

	// A street crime: the HUD line and marker toward a mugging, the thugs over the victim, the toast.
	HawkeyeAddCrimeShots(this);

	// Difficulty and accessibility: the settings screen, the New Game prompt, large subtitles, a palette,
	// and the dimmer pedestal. The player's settings are put back.
	HawkeyeAddAccessibilityShots(this);

	// The second safehouse's door, the fast-travel list from it, and both houses on the compass.
	HawkeyeAddFastTravelShots(this);

	// The "[Auction house]" door into L_Int_Sample on its building by the park.
	HawkeyeAddInteriorShots(this);

	// The safehouse, the main menu and a death that loads the last save. It reloads the map.
	HawkeyeAddSaveShots(this);

	// The phone, the title card, a scripted line, and chapter 1's end played through its playable
	// scene and back. Last of all: it completes the chapter and travels twice.
	HawkeyeAddNarrativeShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
