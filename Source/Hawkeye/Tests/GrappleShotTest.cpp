#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/GrappleAuditKit.h"
#include "Tests/HawkeyeShots.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/ChapterEndInteractable.h"
#include "World/GrappleAnchor.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * grapple_street_to_roof.png (2026-09-29): Kate on the street under the find_arrow roof with the green diamond on
 * the facade anchor generate_city bolted to its street wall (gameplay-semantics.md, "Anchor placement guarantees").
 * The stand is the farthest sidewalk spot within 25 m from which the audit's rules (the hip camera turned to it,
 * the cone, the screen, sight, a clear zip) reach that anchor; the camera is then turned 1.5 degrees left and down of
 * it so the diamond stands clear of her, and the picker must still mark it, Ready. Written to
 * Saved/Screenshots/Kate/ beside grapple_marker.png. Standalone game only (-game, a real RHI).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotGrappleStreetToRoof, "Hawkeye.Screenshot.GrappleStreetToRoof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeGrappleShot
{
	static const FName BuildingTag(TEXT("CityBuilding"));

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

	static FString OsmOf(const AActor* Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("osm:")))
			{
				return Tag.ToString().Mid(4);
			}
		}
		return FString();
	}

	static AActor* BuildingUnder(UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleShotUnder), false, Ignore);
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, Point + FVector(0.f, 0.f, 50.f), Point - FVector(0.f, 0.f, 150.f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.GetActor() && Hit.GetActor()->Tags.Contains(BuildingTag))
			{
				return Hit.GetActor();
			}
		}
		return nullptr;
	}
}

class FHawkeyeGrappleStreetShot : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeGrappleStreetShot(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeGrappleShot;
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		if (!Grapple)
		{
			Test->AddError(TEXT("No Kate with a grapple in the district."));
			return true;
		}
		const double Now = World->GetTimeSeconds();
		if (Stage == 0)
		{
			if (!Plan(World, Kate))
			{
				return true;
			}
			// Setting the shot up is the one move made for her: the stand is where a player would walk to.
			Kate->TeleportTo(Stand, FRotator(0.f, View.Yaw, 0.f), false, true);
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			PC->SetControlRotation(View);
			PC->SetViewTarget(Kate);
			Stage = 1;
			StageAt = Now;
			return false;
		}
		if (Stage == 1)
		{
			// The camera's lag settles; the control rotation is held so the lens ends where the plan put it.
			PC->SetControlRotation(View);
			if (Now - StageAt < 2.0)
			{
				return false;
			}
			Grapple->RefreshTarget();
			const UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud();
			Test->AddInfo(FString::Printf(TEXT("grapple_street_to_roof: Kate at %s, %.0f m from %s on %s (%.1f m roof); marked %s, state %s, HUD diamond %s"),
				*Kate->GetActorLocation().ToCompactString(), FVector::Dist(Kate->GetActorLocation(), Anchor->GetMarkerLocation()) / 100.f,
				*Anchor->GetName(), *RoofOsm, RoofHeightM, *GetNameSafe(Grapple->GetTargetAnchor()),
				*UEnum::GetValueAsString(Grapple->GetTargetState()), Hud && Hud->IsGrappleMarkerReady() ? TEXT("green") : TEXT("not green")));
			Test->TestTrue(TEXT("From the street the find_arrow roof's facade anchor is the marked one"), Grapple->GetTargetAnchor() == Anchor);
			Test->TestEqual(TEXT("And a press would fire"), Grapple->GetTargetState(), EGrappleTargetState::Ready);
			Stage = 2;
			StageAt = Now;
			return false;
		}
		if (Stage == 2)
		{
			PC->SetControlRotation(View);
			if (Now - StageAt < 0.3)
			{
				return false;
			}
			HawkeyeShots::Request(Test, FPaths::ConvertRelativePathToFull(
				FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / TEXT("grapple_street_to_roof.png")));
			Stage = 3;
			return false;
		}
		PC->SetControlRotation(View);
		return HawkeyeShots::GetOutstanding(Test) == 0;
	}

private:
	/** The find_arrow roof's facade anchor and the farthest sidewalk spot that reaches it. */
	bool Plan(UWorld* World, AHawkeyeCharacter* Kate)
	{
		using namespace HawkeyeGrappleShot;
		const UGrappleComponent* Grapple = Kate->GetGrappleComponent();
		const AChapterEndInteractable* End = nullptr;
		for (TActorIterator<AChapterEndInteractable> It(World); It && !End; ++It)
		{
			End = *It;
		}
		RoofOsm = End ? OsmOf(End) : FString();
		const AActor* Roof = nullptr;
		for (TActorIterator<AActor> It(World); It && !RoofOsm.IsEmpty(); ++It)
		{
			if (It->Tags.Contains(BuildingTag) && OsmOf(*It) == RoofOsm)
			{
				Roof = *It;
				break;
			}
		}
		if (!Roof)
		{
			Test->AddError(TEXT("No find_arrow roof (the chapter end's osm tag on a building)."));
			return false;
		}
		FVector Origin, Extent;
		Roof->GetActorBounds(false, Origin, Extent);
		RoofHeightM = (Origin.Z + Extent.Z) / 100.f;
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		float Best = -1.f;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			// A facade anchor: its landing point far enough in to clear the parapet from outside the wall.
			if (!It->GetLandingPoint() || It->GetLandingPoint()->GetRelativeLocation().X < 100.f
				|| BuildingUnder(World, It->GetLandingLocation(), Kate) != Roof)
			{
				continue;
			}
			const FVector Marker = It->GetMarkerLocation();
			for (float X = -2400.f; X <= 2400.f; X += 100.f)
			{
				for (float Y = -2400.f; Y <= 2400.f; Y += 100.f)
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleShotGround), false, Kate);
					FHitResult Hit;
					const FVector XY(Marker.X + X, Marker.Y + Y, 0.f);
					if (!World->LineTraceSingleByChannel(Hit, XY + FVector(0.f, 0.f, 3000.f), XY - FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params)
						|| !Hit.GetActor() || Hit.GetActor()->Tags.Contains(BuildingTag) || Hit.ImpactPoint.Z > 300.f)
					{
						continue;
					}
					const FVector Centre = Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 2.f);
					const float Distance = FVector::Dist(Centre, Marker);
					if (Distance > Grapple->Range - 150.f || Distance < Grapple->MinRange + 200.f || Distance <= Best)
					{
						continue;
					}
					FVector Lens, Forward;
					HawkeyeGrappleView::PredictLens(Kate, Centre, Marker, Lens, Forward);
					const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector::DotProduct(Forward, (Marker - Lens).GetSafeNormal()), -1.f, 1.f)));
					if (Angle > Grapple->ConeDegrees - 8.f || !Grapple->IsOnScreen(Lens, Forward, Marker)
						|| !Grapple->HasLineOfSight(*It, Lens) || !Grapple->IsZipClear(Centre, *It, true))
					{
						continue;
					}
					Best = Distance;
					Anchor = *It;
					Stand = Centre;
					const FRotator Straight = Forward.Rotation();
					// 1.5 degrees left of and below the anchor: off her head, and no nearer the corner anchor of the same roof.
					View = FRotator(Kate->ClampCameraPitch(Straight.Pitch - 1.5f), Straight.Yaw - 1.5f, 0.f);
				}
			}
		}
		if (!Anchor)
		{
			Test->AddError(FString::Printf(TEXT("No sidewalk spot within range reaches a facade anchor on the find_arrow roof %s."), *RoofOsm));
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("grapple_street_to_roof: %s on %s, from %s, %.0f m"), *Anchor->GetName(), *RoofOsm,
			*Stand.ToCompactString(), Best / 100.f));
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	int32 Stage = 0;
	double StageAt = 0.0;
	AGrappleAnchor* Anchor = nullptr;
	FVector Stand = FVector::ZeroVector;
	FRotator View = FRotator::ZeroRotator;
	FString RoofOsm;
	float RoofHeightM = 0.f;
};

bool FHawkeyeScreenshotGrappleStreetToRoof::RunTest(const FString& Parameters)
{
	if (GIsEditor)
	{
		AddInfo(TEXT("Needs the district as a game world with a real RHI: skipped in the editor. Run it from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeGrappleStreetShot(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
