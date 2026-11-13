#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The jump key at the district's own walls (2026-09-29, "if you try to jump moving forward with no
 * momentum, it still takes a hundred tries to hurdle over"): the street test blocks, the park walls
 * and the rooftop parapets between tenements that Cameron's log shows vaulted from a run. For each,
 * Kate stands still 0 to 170 cm off the wall, square and 20 degrees off, and the press's probe is
 * logged with why a vault or mantle was refused; every press at a named wall must vault or mantle.
 * Then every parapet on the tenement roofs Cameron played on is pressed against from a standstill:
 * none may be refused because the landing beyond is crowded (the neighbour's parapet was in the
 * way of the one landing spot tried before). Needs the district loaded: run it from the
 * standalone game (-game), like the lap.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourDistrictWalls, "Hawkeye.Parkour.DistrictWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeParkourDistrict
{
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

	static bool GroundUnder(UWorld* World, const AActor* Ignore, const FVector& Point, float Above, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ParkourDistrictGround), false, Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, Above), Point - FVector(0.f, 0.f, 400.f),
				ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	/** A wall to test: a point on the walking side and the direction to it. */
	struct FWallCase
	{
		FString Name;
		FVector Stand;
		FVector Toward;
		/** The press must vault or mantle (false: a roof edge, a refusal is right). */
		bool bMustMove = true;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeParkourDistrictProbe, FAutomationTestBase*, Test);

bool FHawkeyeParkourDistrictProbe::Update()
{
	using namespace HawkeyeParkourDistrict;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!Parkour)
	{
		Test->AddError(TEXT("No Kate in the district."));
		return true;
	}
	const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();

	TArray<FWallCase> Cases;
	// Vaults in Cameron's log (start, end): parapets between tenement roofs.
	const FVector Logged[][2] = {
		{ FVector(-2747.63f, 12945.30f, 1677.15f), FVector(-3033.47f, 12871.94f, 1647.00f) },
		{ FVector(-3719.08f, 13264.52f, 1647.15f), FVector(-3967.22f, 13212.13f, 1627.00f) },
		{ FVector(-8089.32f, 10167.01f, 1711.36f), FVector(-8314.45f, 10044.44f, 1677.00f) },
	};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Logged); ++I)
	{
		const FVector D = (Logged[I][1] - Logged[I][0]).GetSafeNormal2D();
		Cases.Add({ FString::Printf(TEXT("logged vault %d"), I), Logged[I][0], D });
		Cases.Add({ FString::Printf(TEXT("logged vault %d, back"), I), Logged[I][1], -D });
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const bool bBlock = It->Tags.Contains(TEXT("CityTestVault")) || It->Tags.Contains(TEXT("CityTestMantle"));
		const bool bPark = It->Tags.Contains(TEXT("CityParkWall"));
		if (!bBlock && !bPark)
		{
			continue;
		}
		FVector Origin, Extent;
		It->GetActorBounds(false, Origin, Extent);
		// The blocks' depth runs along their local Y (generate_city.test_block_spots).
		const FVector Axis = It->GetActorRightVector().GetSafeNormal2D();
		Cases.Add({ It->GetActorNameOrLabel(), Origin - Axis * 300.f, Axis });
		Cases.Add({ It->GetActorNameOrLabel() + TEXT(", back"), Origin + Axis * 300.f, -Axis });
	}

	int32 Failures = 0;
	int32 Presses = 0;
	for (const FWallCase& Case : Cases)
	{
		FVector Ground;
		if (!GroundUnder(World, Kate, Case.Stand, 150.f, Ground))
		{
			Test->AddWarning(FString::Printf(TEXT("%s: no ground at %s"), *Case.Name, *Case.Stand.ToCompactString()));
			continue;
		}
		Kate->TeleportTo(Ground + FVector(0.f, 0.f, Half + 2.f), Case.Toward.Rotation(), false, true);
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector::ZeroVector;
		FHawkeyeParkourObstacle Wall;
		if (!Parkour->DetectObstacleAlong(Case.Toward, 600.f, 320.f, Wall))
		{
			Test->AddWarning(FString::Printf(TEXT("%s: no wall within 6 m of %s"), *Case.Name, *Ground.ToCompactString()));
			continue;
		}
		const FVector N = Wall.WallNormal;
		for (const float Off : { 170.f, 100.f, 30.f, 10.f, 5.f, 0.f })
		{
			for (const float Angle : { 0.f, 20.f, -20.f })
			{
				const FVector Spot = Wall.WallPoint + N * (Radius + Off + 0.5f);
				FVector SpotGround;
				if (!GroundUnder(World, Kate, Spot, 120.f, SpotGround))
				{
					continue;
				}
				const FVector Face = (-N).RotateAngleAxis(Angle, FVector::UpVector);
				Kate->TeleportTo(SpotGround + FVector(0.f, 0.f, Half + 2.f), Face.Rotation(), false, true);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector::ZeroVector;
				FHawkeyeParkourObstacle Obstacle;
				EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
				FString WhyNot;
				Parkour->ProbeJumpFan(Face, Parkour->ManualTriggerDistance, false, Obstacle, Move, WhyNot);
				const FString Line = FString::Printf(
					TEXT("%s, %.0f cm off at %+.0f deg (feet %.0f): %s; top %.0f, dist %.0f, depth %.0f, clear %d (drop %.0f)%s, stand %d, on %s"),
					*Case.Name, Off, Angle, SpotGround.Z, Move == EHawkeyeParkourMove::None ? *WhyNot : *UEnum::GetValueAsString(Move),
					Obstacle.Height, Obstacle.Distance, Obstacle.Depth, Obstacle.bClearBeyond ? 1 : 0, Obstacle.LandingDrop,
					Obstacle.BeyondWhyNot.IsEmpty() ? TEXT("") : *(TEXT(" [") + Obstacle.BeyondWhyNot + TEXT("]")),
					Obstacle.bStandingSurface ? 1 : 0, *GetNameSafe(Obstacle.Actor));
				const bool bMoved = Move == EHawkeyeParkourMove::Vault || Move == EHawkeyeParkourMove::Mantle
					|| Move == EHawkeyeParkourMove::LedgeGrab;
				++Presses;
				if (Case.bMustMove && !bMoved)
				{
					++Failures;
					Test->AddError(Line);
				}
			}
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Named walls: %d case(s), %d press(es), %d refused"), Cases.Num(), Presses, Failures));

	// The tenement roofs Cameron played on (x -9000..-2000, y 9500..13600): every 200 cm, every
	// parapet (a 60 to 110 cm top) within 3 m in eight directions, pressed against at 0 cm, square.
	TMap<FString, int32> Outcomes;
	TArray<FString> Examples;
	int32 RoofPresses = 0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ParkourDistrictRoof), false, Kate);
	for (float X = -9000.f; X <= -2000.f; X += 200.f)
	{
		for (float Y = 9500.f; Y <= 13600.f; Y += 200.f)
		{
			FHitResult Roof;
			if (!World->LineTraceSingleByChannel(Roof, FVector(X, Y, 4000.f), FVector(X, Y, 500.f), ECC_Visibility, Params)
				|| !Roof.GetActor() || !Roof.GetActor()->Tags.Contains(TEXT("CityBuilding")) || Roof.ImpactNormal.Z < 0.9f)
			{
				continue;
			}
			for (int32 Dir = 0; Dir < 8; ++Dir)
			{
				const FVector Toward = FVector::ForwardVector.RotateAngleAxis(Dir * 45.f, FVector::UpVector);
				Kate->TeleportTo(Roof.ImpactPoint + FVector(0.f, 0.f, Half + 2.f), Toward.Rotation(), false, true);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector::ZeroVector;
				FHawkeyeParkourObstacle Wall;
				if (!Parkour->DetectObstacleAlong(Toward, 300.f, 320.f, Wall) || Wall.Height < 60.f || Wall.Height > 110.f)
				{
					continue;
				}
				const FVector Spot = Wall.WallPoint + Wall.WallNormal * (Radius + 0.5f);
				FVector SpotGround;
				if (!GroundUnder(World, Kate, Spot, 60.f, SpotGround) || FMath::Abs(SpotGround.Z - Roof.ImpactPoint.Z) > 20.f)
				{
					continue;
				}
				Kate->TeleportTo(SpotGround + FVector(0.f, 0.f, Half + 2.f), (-Wall.WallNormal).Rotation(), false, true);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector::ZeroVector;
				FHawkeyeParkourObstacle Obstacle;
				EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
				FString WhyNot;
				Parkour->ProbeJumpFan(-Wall.WallNormal, Parkour->ManualTriggerDistance, false, Obstacle, Move, WhyNot);
				++RoofPresses;
				FString Key;
				if (Move != EHawkeyeParkourMove::None)
				{
					Key = UEnum::GetValueAsString(Move);
				}
				else
				{
					// The cause without the numbers: "no room to land", "no floor within", ...
					Key = WhyNot;
					int32 Paren = INDEX_NONE;
					if (Key.FindChar(TEXT('('), Paren))
					{
						Key = Key.Mid(Paren + 1);
					}
					Key.ReplaceInline(TEXT("0"), TEXT(""));
					for (TCHAR Digit = TEXT('1'); Digit <= TEXT('9'); ++Digit)
					{
						Key.ReplaceInline(*FString(1, &Digit), TEXT(""));
					}
					Key = Key.Left(60);
					if (Outcomes.FindRef(Key) < 6)
					{
						Examples.Add(FString::Printf(TEXT("at %s facing %.0f: %s"), *SpotGround.ToCompactString(),
							(-Wall.WallNormal).Rotation().Yaw, *WhyNot));
					}
				}
				Outcomes.FindOrAdd(Key)++;
			}
		}
	}
	Test->AddInfo(FString::Printf(TEXT("Roof parapets: %d press(es) against them"), RoofPresses));
	for (const TPair<FString, int32>& Outcome : Outcomes)
	{
		Test->AddInfo(FString::Printf(TEXT("  %5d  %s"), Outcome.Value, *Outcome.Key));
	}
	for (const FString& Example : Examples)
	{
		Test->AddInfo(TEXT("  refused ") + Example);
	}
	// A roof edge (no floor within 8 m) and a parapet backed by a taller neighbour (no back edge) are
	// right to refuse; a landing that is merely crowded by the next building's parapet is not.
	for (const TPair<FString, int32>& Outcome : Outcomes)
	{
		if (Outcome.Key.Contains(TEXT("no room to land")) || Outcome.Key.Contains(TEXT("not a floor")))
		{
			Test->AddError(FString::Printf(TEXT("%d press(es) against a roof parapet refused: %s"), Outcome.Value, *Outcome.Key));
		}
	}
	return true;
}

bool FHawkeyeParkourDistrictWalls::RunTest(const FString& Parameters)
{
	if (GIsEditor)
	{
		AddWarning(TEXT("Skipped (editor): needs the district as a game world. Run it from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeParkourDistrictProbe(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
