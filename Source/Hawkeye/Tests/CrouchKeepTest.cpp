#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/InteriorTestKit.h"
#include "World/DoorActor.h"
#include "World/InteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A toggled crouch keeps (claude-docs/gameplay-semantics.md, "Movement", crouch): in the sample auction house,
 * L_Int_Sample, Kate crouches at the foot of the stair and creeps up both flights, opens the gallery door crouched,
 * creeps through its doorway and back, down the stair and through the archway into the hall, crouched every frame;
 * a one-frame flick to falling and a 20 cm drop keep it. Then what does end it: a real fall (2.5 m) and a parkour
 * move (the flying mode the moves, the hang and the zip use), after which the movement component does not crouch
 * her again. No crouch in the test ends "unasked" (a Blueprint or engine UnCrouch, or a wish cleared behind the
 * character's back).
 */
namespace HawkeyeCrouchKeepTest
{
	using namespace HawkeyeInteriorKit;

	static constexpr EAutomationTestFlags MapFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter;

	/** The creep, as feet positions (Sample.json): up the stair by its flights' middles (the half landing at half height), the gallery door's step and back, down, the archway. */
	static const FVector StairFoot(650.f, 450.f, 0.f);
	static const FVector UpToLanding[] = { FVector(650.f, 690.f, 0.f), FVector(565.f, 725.f, 0.f), FVector(565.f, 1140.f, FloorHeight * 0.5f),
		FVector(735.f, 1140.f, FloorHeight * 0.5f), FVector(735.f, 760.f, FloorHeight), FVector(735.f, 700.f, FloorHeight) };
	static const FVector ThroughTheDoorAndDown[] = { FVector(900.f, 685.f, FloorHeight), FVector(1000.f, 700.f, FloorHeight),
		FVector(900.f, 685.f, FloorHeight), FVector(735.f, 700.f, FloorHeight), FVector(735.f, 760.f, FloorHeight),
		FVector(735.f, 1140.f, FloorHeight * 0.5f), FVector(565.f, 1140.f, FloorHeight * 0.5f), FVector(565.f, 725.f, 0.f), FVector(650.f, 690.f, 0.f),
		FVector(650.f, 450.f, 0.f), FVector(870.f, 300.f, 0.f), FVector(1000.f, 300.f, 0.f) };

	enum class EPhase : uint8 { Place, Crouch, Up, Door, Through, Flick, Lip, Fall, Recrouch, Parkour, Walking, Done };

	struct FState
	{
		EPhase Phase = EPhase::Place;
		double PhaseAt = 0.0;
		int32 Point = 0;
		double PointAt = 0.0;
		int32 EndsAtStart = 0;
		float LowestZ = 0.f;
		bool bFailed = false;
	};

	/** One frame of walking her at Goal. True when her feet are within 35 cm of it (flat) on its floor. */
	static bool WalkTo(AHawkeyeCharacter* Kate, const FVector& Goal)
	{
		const FVector Here = Feet(Kate);
		if (FVector::Dist2D(Here, Goal) < 35.f && FMath::Abs(Here.Z - Goal.Z) < 120.f)
		{
			return true;
		}
		Kate->AddMovementInput(FVector(Goal.X - Here.X, Goal.Y - Here.Y, 0.f).GetSafeNormal(), 1.f);
		return false;
	}

	/** A crouch press as a player makes one standing still: toggled, press and release; held, press and keep. */
	static void PressCrouch(AHawkeyeCharacter* Kate)
	{
		Kate->PressCrouch();
		if (Kate->IsCrouchToggle())
		{
			Kate->ReleaseCrouch();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrouchKeep, "Hawkeye.Crouch.KeepsOnStairsAndThroughDoorways", HawkeyeCrouchKeepTest::MapFlags)

bool FHawkeyeCrouchKeep::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCrouchKeepTest;
	AutomationOpenMap(MapPath, /*bForceReload=*/true);
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		if (!IsMap(World, MapPath) || !FindPlayer(World))
		{
			return false;
		}
		// Nothing but the creep: the thugs stand still and blind, and she cannot be hurt.
		FreezeThugs(World);
		if (UHealthComponent* Health = FindPlayer(World)->GetHealthComponent())
		{
			Health->SetInvulnerable(true);
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		return Nav && Nav->GetDefaultNavDataInstance() && !Nav->IsNavigationBuildInProgress();
	}, [this]()
	{
		AddError(TEXT("L_Int_Sample did not come up with a player within 25 s."));
		return true;
	}, 25.f));

	const TSharedRef<FState> S = MakeShared<FState>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]()
	{
		UWorld* World = FindWorld();
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!World || !Kate)
		{
			AddError(TEXT("Kate went away mid-test."));
			return true;
		}
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		const double Now = World->GetTimeSeconds();
		auto Next = [S, Now](EPhase Phase)
		{
			S->Phase = Phase;
			S->PhaseAt = Now;
			S->Point = 0;
			S->PointAt = Now;
		};
		auto Fail = [this, S, Kate](const FString& Why)
		{
			AddError(FString::Printf(TEXT("%s (feet %s, mode %d, last crouch end: '%s')"), *Why, *Feet(Kate).ToCompactString(),
				static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue()), *Kate->GetLastCrouchEndReason()));
			S->bFailed = true;
			return true;
		};
		if (Now - S->PhaseAt > 20.0)
		{
			return Fail(FString::Printf(TEXT("Phase %d took more than 20 s"), static_cast<int32>(S->Phase)));
		}
		// From the crouch to the end of the lip, she is crouched every frame and no crouch ends.
		const bool bMustBeCrouched = S->Phase >= EPhase::Up && S->Phase <= EPhase::Lip;
		if (bMustBeCrouched && (!Kate->bIsCrouched || Kate->GetCrouchEndCount() != S->EndsAtStart))
		{
			return Fail(FString::Printf(TEXT("She stood up in phase %d, point %d"), static_cast<int32>(S->Phase), S->Point));
		}
		if (Kate->GetUnaskedCrouchEndCount() > 0)
		{
			return Fail(TEXT("A crouch ended unasked"));
		}

		switch (S->Phase)
		{
		case EPhase::Place:
			Place(World, StairFoot, 90.f);
			Next(EPhase::Crouch);
			return false;
		case EPhase::Crouch:
			if (Now - S->PhaseAt < 0.3)
			{
				return false;
			}
			if (!Kate->IsCrouchWanted())
			{
				PressCrouch(Kate);
			}
			if (Kate->bIsCrouched)
			{
				S->EndsAtStart = Kate->GetCrouchEndCount();
				Next(EPhase::Up);
			}
			return false;
		case EPhase::Up:
		case EPhase::Through:
		{
			const FVector* Points = S->Phase == EPhase::Up ? UpToLanding : ThroughTheDoorAndDown;
			const int32 Count = S->Phase == EPhase::Up ? UE_ARRAY_COUNT(UpToLanding) : UE_ARRAY_COUNT(ThroughTheDoorAndDown);
			if (Now - S->PointAt > 8.0)
			{
				return Fail(FString::Printf(TEXT("Stuck on the way to %s"), *Points[S->Point].ToCompactString()));
			}
			if (WalkTo(Kate, Points[S->Point]))
			{
				S->PointAt = Now;
				if (++S->Point >= Count)
				{
					AddInfo(FString::Printf(TEXT("Crouched %s at %s."), S->Phase == EPhase::Up ? TEXT("up the stair") : TEXT("through the gallery door, down and through the archway"),
						*Feet(Kate).ToCompactString()));
					Next(S->Phase == EPhase::Up ? EPhase::Door : EPhase::Flick);
				}
			}
			return false;
		}
		case EPhase::Door:
		{
			ADoorActor* Door = nullptr;
			for (ADoorActor* Each : All<ADoorActor>(World))
			{
				Door = FVector2D::Distance(FVector2D(Each->GetActorLocation()), FVector2D(800.f, 685.f)) < 60.f ? Each : Door;
			}
			if (!Door)
			{
				return Fail(TEXT("No gallery door"));
			}
			if (!Door->IsOpen())
			{
				// Opened by her own interaction, crouched: the door's open event must leave the crouch alone.
				Kate->SetActorRotation(FRotator(0.f, 0.f, 0.f));
				if (UInteractionComponent* Interaction = Kate->GetInteractionComponent())
				{
					Interaction->RefreshFocus();
					Interaction->TryInteract();
				}
				if (!Door->IsOpen() && Now - S->PhaseAt > 1.0)
				{
					Door->OpenNow(Kate);
				}
				return false;
			}
			// The leaf swings a moment; she waits for it crouched.
			if (Now - S->PhaseAt > 1.5)
			{
				Next(EPhase::Through);
			}
			return false;
		}
		case EPhase::Flick:
			// A doorway or a landing flicking her to falling for a frame keeps the crouch.
			if (S->Point == 0)
			{
				Movement->SetMovementMode(MOVE_Falling);
				S->Point = 1;
				return false;
			}
			if (Now - S->PhaseAt > 0.5 && Movement->IsMovingOnGround())
			{
				AddInfo(TEXT("A one-frame fall kept the crouch."));
				Next(EPhase::Lip);
			}
			return false;
		case EPhase::Lip:
			// A drop of 20 cm (a stair's lip) keeps it.
			if (S->Point == 0)
			{
				Kate->SetActorLocation(Kate->GetActorLocation() + FVector(0.f, 0.f, 20.f));
				Movement->SetMovementMode(MOVE_Falling);
				S->Point = 1;
				return false;
			}
			if (Now - S->PhaseAt > 0.6 && Movement->IsMovingOnGround())
			{
				AddInfo(TEXT("A 20 cm drop kept the crouch."));
				Next(EPhase::Fall);
			}
			return false;
		case EPhase::Fall:
			// A real fall (2.5 m, longer than 0.3 s) stands her up.
			if (S->Point == 0)
			{
				Kate->SetActorLocation(Kate->GetActorLocation() + FVector(0.f, 0.f, 250.f));
				Movement->SetMovementMode(MOVE_Falling);
				S->Point = 1;
				return false;
			}
			if (Now - S->PhaseAt > 0.3 && Movement->IsMovingOnGround())
			{
				TestFalse(TEXT("A 2.5 m fall ends the crouch"), Kate->IsCrouchWanted() || Kate->bIsCrouched);
				TestEqual(TEXT("And says so"), Kate->GetLastCrouchEndReason(), FString(TEXT("a fall")));
				Next(EPhase::Recrouch);
			}
			return false;
		case EPhase::Recrouch:
			if (Now - S->PhaseAt < 0.3)
			{
				return false;
			}
			if (!Kate->IsCrouchWanted())
			{
				PressCrouch(Kate);
			}
			if (Kate->bIsCrouched)
			{
				Next(EPhase::Parkour);
			}
			return false;
		case EPhase::Parkour:
			// The mode a vault, a mantle, the hang and the zip move her in.
			if (S->Point == 0)
			{
				Movement->SetMovementMode(MOVE_Flying);
				S->Point = 1;
				return false;
			}
			if (Now - S->PhaseAt > 0.2)
			{
				TestFalse(TEXT("A parkour move ends the crouch"), Kate->IsCrouchWanted() || Kate->bIsCrouched);
				TestEqual(TEXT("And says so"), Kate->GetLastCrouchEndReason(), FString(TEXT("a parkour move or hang")));
				Movement->SetMovementMode(MOVE_Walking);
				Next(EPhase::Walking);
			}
			return false;
		case EPhase::Walking:
			if (Now - S->PhaseAt > 0.5)
			{
				TestFalse(TEXT("Back on the ground after the move, she is not crouched again"), Kate->IsCrouchWanted() || Kate->bIsCrouched);
				TestEqual(TEXT("No crouch ended unasked"), Kate->GetUnaskedCrouchEndCount(), 0);
				Next(EPhase::Done);
			}
			return false;
		default:
			if (Kate->IsCrouchToggle() == false)
			{
				Kate->ReleaseCrouch();
			}
			return true;
		}
	}));
	return true;
}

#endif
