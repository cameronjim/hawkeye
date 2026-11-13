#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Player/HawkeyeCharacter.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The hero block's look (claude-docs/city-look.md): the block round the PlayerStart, by day and by night, written to
 * Saved/Screenshots/Block/<shot>_<day|night>.png. Look-at-them shots, not assertions.
 *
 *   street        eye height on the park-side sidewalk by the PlayerStart, looking down East 7th Street
 *   kate_street   Kate's own gameplay camera at the PlayerStart, turned a little down the street
 *   kate_roof     Kate's gameplay camera on the roof of the tenement across the street, looking back over it
 *   facade        3 m from that tenement's facade at first-floor height
 *   window        1.8 m from a first-floor window, a little to one side so the room behind it shows
 *   ground        down at the kerb: sidewalk, kerb and carriageway
 *   shopfront     eye height 3.6 m from the shops across the street (on their sidewalk), turned along them
 *   wide          15 m into the park, looking up at the tenement row: roofs and cornices against the sky
 *   kerb          eye height on the shop-side sidewalk by the kerb, looking along it: parked cars, plowed snow, signs
 *   tree          under the street tree nearest the PlayerStart, looking up into its branches
 *
 * The Kate shots also measure 3 s of frames before the capture (frame average and p95, and stat unit's game, draw and
 * GPU averages) and log one "BlockPerf <shot>" line each; run at 1920x1080 for the 60 fps check. The close-ups hide
 * the falling snow (a flake by the lens is a large white disc). Needs a real RHI, so it is a no-op under -nullrhi:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720
 *       -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Block; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotBlock, "Hawkeye.Screenshot.Block",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeBlockShots
{
	static const FName BuildingTag(TEXT("CityBuilding"));
	static const FName ParkTag(TEXT("CityPark"));
	static const FName CameraTag(TEXT("HawkeyeBlockShotCamera"));
	static constexpr float EyeHeight = 170.f;
	/** From the PlayerStart (3 m park side of the centre line) back onto the park-side sidewalk, cm. */
	static constexpr float SidewalkBack = 500.f;

	enum class EShot : uint8
	{
		Street,
		KateStreet,
		KateRoof,
		Facade,
		Window,
		Ground,
		Shopfront,
		Wide,
		Kerb,
		Tree,
	};

	struct FShot
	{
		EShot Shot;
		bool bNight;
		const TCHAR* Name;
	};

	static const FShot Shots[] = {
		{ EShot::Street, false, TEXT("street") },
		{ EShot::KateStreet, false, TEXT("kate_street") },
		{ EShot::KateRoof, false, TEXT("kate_roof") },
		{ EShot::Facade, false, TEXT("facade") },
		{ EShot::Window, false, TEXT("window") },
		{ EShot::Ground, false, TEXT("ground") },
		{ EShot::Shopfront, false, TEXT("shopfront") },
		{ EShot::Wide, false, TEXT("wide") },
		{ EShot::Kerb, false, TEXT("kerb") },
		{ EShot::Tree, false, TEXT("tree") },
		{ EShot::Street, true, TEXT("street") },
		{ EShot::KateStreet, true, TEXT("kate_street") },
		{ EShot::KateRoof, true, TEXT("kate_roof") },
		{ EShot::Facade, true, TEXT("facade") },
		{ EShot::Window, true, TEXT("window") },
		{ EShot::Ground, true, TEXT("ground") },
		{ EShot::Shopfront, true, TEXT("shopfront") },
		{ EShot::Wide, true, TEXT("wide") },
		{ EShot::Kerb, true, TEXT("kerb") },
		{ EShot::Tree, true, TEXT("tree") },
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
			Camera->GetCameraComponent()->SetFieldOfView(80.f);
		}
		return Camera;
	}

	/** The facade across the street from the PlayerStart: a horizontal trace along its facing at Height. */
	static bool FindFacade(UWorld* World, const FVector& Start, float Yaw, float Height, FHitResult& OutHit)
	{
		const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector From(Start.X, Start.Y, Height);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockShotFacade), false);
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			Query.AddIgnoredActor(PC->GetPawn());
		}
		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, From, From + Dir * 6000.f, ECC_Visibility, Query);
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag))
			{
				OutHit = Hit;
				return true;
			}
		}
		// Single trace fallback (a blocking hit ends a multi trace on its first blocker).
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, From, From + Dir * 6000.f, ECC_Visibility, Query) && Hit.GetActor()
			&& Hit.GetActor()->Tags.Contains(BuildingTag))
		{
			OutHit = Hit;
			return true;
		}
		return false;
	}

	/** _look.FACADE_HLSL's hash (Dave Hoskins' hash12), in float like the shader. */
	static float Hash21(float X, float Y)
	{
		auto Frac = [](float V) { return V - FMath::FloorToFloat(V); };
		float QX = Frac(X * 0.1031f), QY = Frac(Y * 0.1031f), QZ = Frac(X * 0.1031f);
		const float D = QX * (QY + 33.33f) + QY * (QZ + 33.33f) + QZ * (QX + 33.33f);
		QX += D;
		QY += D;
		QZ += D;
		return Frac((QX + QY) * QZ);
	}

	/**
	 * The middle of a first- to third-floor window on Hit's facade that the facade material lights at night (its
	 * window grid and lit hash, LitFraction 0.35), nearest the hit; the hit's own bay when none is.
	 */
	static FVector FindLitWindow(UWorld* World, const FHitResult& Hit)
	{
		const FVector2D N = FVector2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y).GetSafeNormal();
		const FVector2D T(-N.Y, N.X);
		const FVector2D P(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
		const float U0 = FVector2D::DotProduct(P, T);
		const float Plane = FMath::FloorToFloat(FVector2D::DotProduct(P, N) / 100.f) * 1.37f;
		const int32 Bay0 = FMath::FloorToInt(U0 / 160.f);
		FVector Fallback = FVector(P.X, P.Y, 330.f + 170.f);
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const int32 Bay = Bay0 + ((Step % 2) ? -(Step + 1) / 2 : Step / 2);
			for (int32 Storey = 1; Storey <= 3; ++Storey)
			{
				const float Seed = Hash21(Bay + Plane, Storey + Plane);
				if (Hash21(Seed, 3.3f) > 0.35f || Hash21(Seed, 5.1f) < 0.16f)
				{
					continue;
				}
				const FVector2D Along = P + T * ((Bay + 0.5f) * 160.f - U0);
				const FVector Centre(Along.X, Along.Y, Storey * 330.f + 95.f + 75.f);
				FHitResult Check;
				const FVector Out = Centre + FVector(N.X, N.Y, 0.f) * 150.f;
				if (World->LineTraceSingleByChannel(Check, Out, Centre - FVector(N.X, N.Y, 0.f) * 50.f, ECC_Visibility)
					&& Check.GetActor() == Hit.GetActor())
				{
					return Centre;
				}
			}
		}
		return Fallback;
	}

	/** The street tree (SM_Street_Tree_<n>, look pass 3) nearest Near, 2D. */
	static AActor* FindTreeNear(UWorld* World, const FVector& Near)
	{
		AActor* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("SM_Street_Tree_")))
			{
				continue;
			}
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Near);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	static void SetSnowfallHidden(UWorld* World, bool bHidden)
	{
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			UNiagaraComponent* Component = *It;
			if (Component->GetWorld() == World && Component->GetAsset() && Component->GetAsset()->GetName().Contains(TEXT("Snowfall")))
			{
				Component->SetHiddenInGame(bHidden);
			}
		}
	}

	static float Percentile(TArray<float> Values, float Fraction)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt(Fraction * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}
}

/** Walks the shot list: time of day, framing, settle, (perf), capture; one capture outstanding at a time. */
class FHawkeyeBlockShots : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeBlockShots(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeBlockShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		TActorIterator<APlayerStart> StartIt(World);
		if (!World || !PC || !Kate || !StartIt)
		{
			Test->AddError(TEXT("No game world, player controller, Kate or PlayerStart."));
			return true;
		}
		if (HawkeyeShots::GetOutstanding(Test) > 0)
		{
			return false;
		}
		if (Index >= UE_ARRAY_COUNT(Shots))
		{
			SetSnowfallHidden(World, false);
			GEngine->Exec(World, TEXT("hawkeye.TimeOfDay Saved"));
			return true;
		}
		const FShot& Shot = Shots[Index];
		const double Now = FPlatformTime::Seconds();
		switch (Phase)
		{
		case 0:
			if (!bTimeSet || bLastNight != Shot.bNight)
			{
				GEngine->Exec(World, Shot.bNight ? TEXT("hawkeye.TimeOfDay Night") : TEXT("hawkeye.TimeOfDay Day"));
				bTimeSet = true;
				bLastNight = Shot.bNight;
			}
			if (!Frame(World, PC, Kate, StartIt->GetActorLocation(), StartIt->GetActorRotation().Yaw, Shot))
			{
				++Index;
				return false;
			}
			PhaseStart = Now;
			Phase = 1;
			return false;
		case 1:
			// Lumen, the exposure and Kate's camera lag settle on the new view.
			if (Now - PhaseStart < SettleSeconds)
			{
				return false;
			}
			PhaseStart = Now;
			Frames.Reset();
			Game.Reset();
			Draw.Reset();
			Gpu.Reset();
			Phase = (Shot.Shot == EShot::KateStreet || Shot.Shot == EShot::KateRoof) ? 2 : 3;
			return false;
		case 2:
			if (Now - PhaseStart < MeasureSeconds)
			{
				if (!HawkeyeShots::DidCaptureLastFrame())
				{
					Frames.Add(FApp::GetDeltaTime() * 1000.f);
					Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
					Draw.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
					Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
				}
				return false;
			}
			{
				auto Mean = [](const TArray<float>& V) { float S = 0.f; for (float X : V) { S += X; } return V.Num() ? S / V.Num() : 0.f; };
				const FString Line = FString::Printf(
					TEXT("BlockPerf %s_%s: %d frames, frame %.2f ms avg, %.2f ms p95, game %.2f, draw %.2f, GPU %.2f ms (%dx%d)."),
					Shot.Name, Shot.bNight ? TEXT("night") : TEXT("day"), Frames.Num(), Mean(Frames), Percentile(Frames, 0.95f),
					Mean(Game), Mean(Draw), Mean(Gpu), GSystemResolution.ResX, GSystemResolution.ResY);
				Test->AddInfo(Line);
				UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			}
			Phase = 3;
			return false;
		default:
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Block")
				/ FString::Printf(TEXT("%s_%s.png"), Shot.Name, Shot.bNight ? TEXT("night") : TEXT("day")));
			HawkeyeShots::Request(Test, Path, /*bShowUI=*/false);
			++Index;
			Phase = 0;
			return false;
		}
		}
	}

private:
	bool Frame(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Start, float StartYaw,
		const HawkeyeBlockShots::FShot& Shot)
	{
		using namespace HawkeyeBlockShots;
		const bool bCloseUp = Shot.Shot == EShot::Facade || Shot.Shot == EShot::Window || Shot.Shot == EShot::Ground
			|| Shot.Shot == EShot::Shopfront || Shot.Shot == EShot::Kerb || Shot.Shot == EShot::Tree;
		SetSnowfallHidden(World, bCloseUp);

		const FVector Across = FRotator(0.f, StartYaw, 0.f).Vector();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Across);
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		auto PlaceKate = [&](const FVector& Feet, float Yaw, float Pitch)
		{
			Kate->StopAim();
			Kate->SetActorHiddenInGame(false);
			Kate->TeleportTo(Feet + FVector(0.f, 0.f, Half + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
			PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
			PC->SetViewTarget(Kate);
		};
		auto Look = [&](const FVector& Eye, const FRotator& Rotation)
		{
			ACameraActor* Camera = FindOrSpawnCamera(World);
			if (!Camera)
			{
				return false;
			}
			Kate->SetActorHiddenInGame(true);
			Camera->SetActorLocationAndRotation(Eye, Rotation);
			PC->SetViewTarget(Camera);
			Test->AddInfo(FString::Printf(TEXT("%s: camera at %s looking %s."), Shot.Name, *Eye.ToCompactString(),
				*Rotation.ToCompactString()));
			return true;
		};

		FHitResult Facade;
		const bool bFacade = FindFacade(World, Start, StartYaw, 500.f, Facade);
		switch (Shot.Shot)
		{
		case EShot::Street:
		{
			const FVector OnSidewalk = Start - Across * SidewalkBack;
			return Look(FVector(OnSidewalk.X, OnSidewalk.Y, EyeHeight), FRotator(-3.f, StartYaw + 90.f, 0.f));
		}
		case EShot::KateStreet:
			PlaceKate(FVector(Start.X, Start.Y, 0.f), StartYaw + 35.f, -6.f);
			return true;
		case EShot::KateRoof:
		{
			if (!bFacade)
			{
				Test->AddError(TEXT("kate_roof: no facade across the street from the PlayerStart."));
				return false;
			}
			FVector Centre, Extent;
			Facade.GetActor()->GetActorBounds(false, Centre, Extent);
			const float Roof = Centre.Z + Extent.Z;
			// 4 m in from the facade, on the roof, looking back over the street and the park.
			const FVector Spot = Facade.ImpactPoint - Facade.ImpactNormal.GetSafeNormal2D() * 400.f;
			FHitResult Down;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockShotRoof), false, Kate);
			const bool bRoof = World->LineTraceSingleByChannel(Down, FVector(Spot.X, Spot.Y, Roof + 300.f),
				FVector(Spot.X, Spot.Y, Roof - 2000.f), ECC_Visibility, Query);
			const FVector Feet = bRoof ? Down.ImpactPoint : FVector(Spot.X, Spot.Y, Roof);
			PlaceKate(Feet, Facade.ImpactNormal.Rotation().Yaw - 30.f, -12.f);
			Test->AddInfo(FString::Printf(TEXT("kate_roof: on %s at %s."), *Facade.GetActor()->GetName(), *Feet.ToCompactString()));
			return true;
		}
		case EShot::Facade:
		case EShot::Window:
		{
			if (!bFacade)
			{
				Test->AddError(TEXT("No facade across the street from the PlayerStart."));
				return false;
			}
			const FVector Out = Facade.ImpactNormal.GetSafeNormal2D();
			const FVector Along = FVector::CrossProduct(FVector::UpVector, Out);
			if (Shot.Shot == EShot::Facade)
			{
				const FVector Eye = Facade.ImpactPoint + Out * 300.f;
				return Look(FVector(Eye.X, Eye.Y, 520.f), FRotator(-2.f, (-Out).Rotation().Yaw + 12.f, 0.f));
			}
			// A window the material lights at night, from 1.8 m out and a little to one side so the room shows.
			const FVector Window = FindLitWindow(World, Facade);
			const FVector Eye = Window + Out * 180.f - Along * 70.f;
			return Look(Eye, (Window - Eye).Rotation());
		}
		case EShot::Shopfront:
		{
			if (!bFacade)
			{
				Test->AddError(TEXT("shopfront: no facade across the street from the PlayerStart."));
				return false;
			}
			const FVector Out = Facade.ImpactNormal.GetSafeNormal2D();
			const FVector Eye = Facade.ImpactPoint + Out * 360.f;   // on the sidewalk, in front of the parked cars
			return Look(FVector(Eye.X, Eye.Y, EyeHeight), FRotator(4.f, (-Out).Rotation().Yaw + 28.f, 0.f));
		}
		case EShot::Wide:
		{
			const FVector Back = Start - Across * 1500.f;
			return Look(FVector(Back.X, Back.Y, EyeHeight), FRotator(9.f, StartYaw + 10.f, 0.f));
		}
		case EShot::Kerb:
		{
			if (!bFacade)
			{
				Test->AddError(TEXT("kerb: no facade across the street from the PlayerStart."));
				return false;
			}
			const FVector Out = Facade.ImpactNormal.GetSafeNormal2D();
			const FVector Along = FVector::CrossProduct(FVector::UpVector, Out);
			const FVector Eye = Facade.ImpactPoint + Out * 300.f - Along * 600.f;
			return Look(FVector(Eye.X, Eye.Y, EyeHeight), FRotator(-7.f, Along.Rotation().Yaw + 8.f, 0.f));
		}
		case EShot::Tree:
		{
			const AActor* Tree = FindTreeNear(World, Start);
			if (!Tree)
			{
				Test->AddError(TEXT("tree: no SM_Street_Tree_ actor."));
				return false;
			}
			const FVector Base = Tree->GetActorLocation();
			const FVector Eye = Base + Across * 380.f + Side * 120.f;
			return Look(FVector(Eye.X, Eye.Y, 150.f), FRotator(28.f, (Base - Eye).Rotation().Yaw, 0.f));
		}
		case EShot::Ground:
		{
			// The tenement side's kerb, from the sidewalk, looking down and out across it into the road.
			const FVector Kerb = bFacade ? Facade.ImpactPoint + Facade.ImpactNormal.GetSafeNormal2D() * 250.f : Start;
			const FVector Eye(Kerb.X, Kerb.Y, 160.f);
			return Look(Eye, FRotator(-42.f, (-Across).Rotation().Yaw + 30.f, 0.f));
		}
		}
		return false;
	}

	static constexpr double SettleSeconds = 3.0;
	static constexpr double MeasureSeconds = 3.0;
	FAutomationTestBase* Test = nullptr;
	int32 Index = 0;
	int32 Phase = 0;
	double PhaseStart = 0.0;
	bool bTimeSet = false;
	bool bLastNight = false;
	TArray<float> Frames;
	TArray<float> Game;
	TArray<float> Draw;
	TArray<float> Gpu;
};

bool FHawkeyeScreenshotBlock::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the block screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	// Map load, the spawner's instances and Lumen settling.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBlockShots(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
