#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reference shots of the generated East Village district (Tools/Editor/generate_city.py),
 * written to Saved/Screenshots/City/. Look-at-them tools, not assertions:
 *
 *   street_night.png    eye height on the park-side sidewalk of East 7th Street by the PlayerStart,
 *                       looking down the street: lamps, lit windows, snow on the sidewalk
 *   rooftop_night.png   on the tallest roof within 120 m of the start, looking across the roofs to
 *                       the park: snowy roofs, water towers, moonlight
 *   overview_night.png  150 m up south of Tompkins Square Park, pitched -60, facing north
 *                       (so Avenue A is on the left and Avenue B on the right if nothing is mirrored)
 *
 * The generator tags its actors (CityBuilding, CityPark) because actor labels are editor-only;
 * these shots find everything by tag. Needs a real RHI, so it is a no-op under -nullrhi. Run it
 * from the standalone game (claude-docs/testing.md section 2b):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.EastVillage; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotEastVillage, "Hawkeye.Screenshot.EastVillage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

namespace HawkeyeCityShots
{
	static const FName CameraTag(TEXT("HawkeyeCityShotCamera"));
	static const FName BuildingTag(TEXT("CityBuilding"));
	static const FName ParkTag(TEXT("CityPark"));
	static constexpr float EyeHeight = 170.f;
	static constexpr float RooftopSearchRadius = 12000.f;
	static constexpr float OverviewHeight = 15000.f;
	static constexpr float OverviewPitch = -60.f;
	/** From the PlayerStart (3 m park side of the centre line) back to the park-side sidewalk, cm. */
	static constexpr float StreetSidewalkBack = 500.f;

	enum class EShot : uint8
	{
		Street,
		Rooftop,
		Overview,
	};

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("City") / FileName);
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

	static ACameraActor* FindOrSpawnCamera(UWorld* World)
	{
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(CameraTag))
			{
				return *It;
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Camera)
		{
			Camera->Tags.Add(CameraTag);
			Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
			Camera->GetCameraComponent()->SetFieldOfView(90.f);
		}
		return Camera;
	}

	static APlayerStart* FindPlayerStart(UWorld* World)
	{
		TActorIterator<APlayerStart> It(World);
		return It ? *It : nullptr;
	}

	static bool FindPark(UWorld* World, FVector& OutCentre)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParkTag))
			{
				FVector Extent;
				It->GetActorBounds(false, OutCentre, Extent);
				return true;
			}
		}
		return false;
	}

	/** Tallest tagged building whose base is within Radius of Origin (2D). */
	static AActor* FindTallestBuildingNear(UWorld* World, const FVector& Origin, float Radius, FVector& OutTop)
	{
		AActor* Best = nullptr;
		float BestTop = -BIG_NUMBER;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(BuildingTag))
			{
				continue;
			}
			FVector Centre, Extent;
			It->GetActorBounds(false, Centre, Extent);
			if (FVector::Dist2D(Centre, Origin) > Radius)
			{
				continue;
			}
			const float Top = Centre.Z + Extent.Z;
			if (Top > BestTop)
			{
				BestTop = Top;
				Best = *It;
				OutTop = FVector(Centre.X, Centre.Y, Top);
			}
		}
		return Best;
	}
}

/** Put the shot camera where the shot wants it and look through it. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeCityFrameShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeCityFrameShot::Update()
{
	using namespace HawkeyeCityShots;

	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerStart* Start = World ? FindPlayerStart(World) : nullptr;
	if (!World || !PC || !Start)
	{
		Test->AddError(TEXT("No game world, player controller or PlayerStart in L_District_EastVillage."));
		return true;
	}

	FVector ParkCentre = FVector::ZeroVector;
	const bool bHasPark = FindPark(World, ParkCentre);
	const FVector StartLocation = Start->GetActorLocation();
	const float StartYaw = Start->GetActorRotation().Yaw;

	FVector Eye = StartLocation;
	FRotator Look = FRotator::ZeroRotator;
	switch (static_cast<EShot>(Shot))
	{
	case EShot::Street:
	{
		// The start faces the tenement row across the street; back from it onto the park-side
		// sidewalk, a quarter turn looks down the street with the park on the left.
		const FVector Across = FRotator(0.f, StartYaw, 0.f).Vector();
		const FVector OnSidewalk = StartLocation - Across * StreetSidewalkBack;
		Eye = FVector(OnSidewalk.X, OnSidewalk.Y, EyeHeight);
		Look = FRotator(-4.f, StartYaw + 90.f, 0.f);
		break;
	}

	case EShot::Rooftop:
	{
		FVector Top;
		AActor* Building = FindTallestBuildingNear(World, StartLocation, RooftopSearchRadius, Top);
		if (!Building)
		{
			Test->AddError(TEXT("No CityBuilding-tagged actor near the PlayerStart."));
			return true;
		}
		Eye = Top + FVector(0.f, 0.f, EyeHeight);
		const FVector Target = bHasPark ? ParkCentre : StartLocation;
		Look = FRotator(-8.f, (Target - Eye).GetSafeNormal2D().Rotation().Yaw, 0.f);
		Test->AddInfo(FString::Printf(TEXT("Rooftop: %s, roof at %.0f cm."), *Building->GetName(), Top.Z));
		break;
	}

	case EShot::Overview:
	{
		// South of the park (north is -Y), far enough back that the -60 degree view centres it.
		const FVector Centre = bHasPark ? ParkCentre : StartLocation;
		const float Back = OverviewHeight / FMath::Tan(FMath::DegreesToRadians(-OverviewPitch));
		Eye = FVector(Centre.X, Centre.Y + Back, OverviewHeight);
		Look = FRotator(OverviewPitch, -90.f, 0.f);
		break;
	}
	}

	ACameraActor* Camera = FindOrSpawnCamera(World);
	if (!Camera)
	{
		Test->AddError(TEXT("Could not spawn the shot camera."));
		return true;
	}
	Camera->SetActorLocationAndRotation(Eye, Look);
	PC->SetViewTarget(Camera);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorHiddenInGame(true);
	}
	Test->AddInfo(FString::Printf(TEXT("Camera at %s looking %s."), *Eye.ToCompactString(), *Look.ToCompactString()));
	return true;
}

/** Ask for one screenshot under Saved/Screenshots/City. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeCityTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeCityTakeShot::Update()
{
	const FString FullPath = HawkeyeCityShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/false);
	return true;
}

bool FHawkeyeScreenshotEastVillage::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCityShots;

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the city screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}

	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	// Map load, nav build kick-off, and Lumen settling on 600 new meshes.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));

	const TPair<EShot, const TCHAR*> Shots[] = {
		{ EShot::Street, TEXT("street_night.png") },
		{ EShot::Rooftop, TEXT("rooftop_night.png") },
		{ EShot::Overview, TEXT("overview_night.png") },
	};
	for (const TPair<EShot, const TCHAR*>& ShotAndFile : Shots)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCityFrameShot(this, static_cast<uint8>(ShotAndFile.Key)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.5f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCityTakeShot(this, FString(ShotAndFile.Value)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	}
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
