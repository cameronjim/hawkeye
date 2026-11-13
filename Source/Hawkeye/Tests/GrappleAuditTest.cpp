#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Traversal/GrappleAudit.h"
#include "Traversal/GrappleReachWorld.h"
#include "World/CityMapData.h"
#include "World/GrappleAnchor.h"
#include "Hawkeye.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Grapple.Audit (2026-09-29, after "many street-side anchors are never targeted" and a campaign lap that
 * found no street spot from which the find_arrow roof's one anchor could be seen): every grapple anchor in the
 * district against a set of standing spots, with the game's own rules. The audit itself is FGrappleAudit
 * (Source/Hawkeye/Traversal), the same code the traversal debugger and the HawkeyeTraversalAudit commandlet run; what
 * this test adds is the game world: the scene is the anchors and fire escapes the game spawned, the sight lines and
 * zip sweeps are Kate's own UGrappleComponent::HasLineOfSight and IsZipClear, the lens is her spring arm's sums
 * (FGrappleReachWorld::MakeProfile on the live Kate, probe ignoring her), and the picker is her
 * SelectBestAnchorFrom, checked against the full check from every reachable view (dead on and 6.3 degrees off).
 *
 * The spots: the sidewalks every 10 m (both sides of every street centre line on the world map, at the middle of the
 * 4 m sidewalk, or nearer the centre line where that is inside a building) and, on every roof, its centre and the
 * middle of every parapet 3 m or longer (1 m in). A roof no 10 m spot reaches is tried again from a sidewalk spot every
 * 2.5 m. Per roof over 8 m: anchors reachable from the street and from another roof, and for every neighbour across a
 * gap of 1 to 25 m whether any of its spots reaches this roof. Writes Saved/Automation/grapple_audit.json. Fails on a
 * chapter 1 roof the street or the previous objective cannot reach (find_arrow may be reached down a fire escape and up
 * from the street: the street between it and cross_block is wider than the grapple's range). verify_city.py holds the
 * district-wide guarantees. Needs the district loaded: run it from the standalone game (-game), like the laps.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleAudit, "Hawkeye.Grapple.Audit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeGrappleAudit
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

	/**
	 * The oracle the game itself is: Kate's grapple component asked about the spawned anchor actors, and her spring
	 * arm's probe with her own capsule left out. A blocked sight line is traced once more only to name what blocked it.
	 */
	static FGrappleReachOracle MakeKateOracle(AHawkeyeCharacter* Kate, UGrappleComponent* Grapple, const FGrappleReachScene& Scene)
	{
		TArray<TWeakObjectPtr<AGrappleAnchor>> Actors;
		for (const TWeakObjectPtr<AActor>& Actor : Scene.AnchorActors)
		{
			Actors.Add(Cast<AGrappleAnchor>(Actor.Get()));
		}
		const TWeakObjectPtr<AHawkeyeCharacter> WeakKate(Kate);
		const TWeakObjectPtr<UGrappleComponent> WeakGrapple(Grapple);

		FGrappleReachOracle Oracle;
		Oracle.HasLineOfSight = [WeakKate, WeakGrapple, Actors](const FVector& Lens, const FGrappleReachAnchor& Anchor,
			FGrappleReachBlocker* OutBlocker) -> bool
		{
			const UGrappleComponent* Live = WeakGrapple.Get();
			const AGrappleAnchor* Actor = Actors.IsValidIndex(Anchor.Index) ? Actors[Anchor.Index].Get() : nullptr;
			if (!Live || !Actor)
			{
				return false;
			}
			if (Live->HasLineOfSight(Actor, Lens))
			{
				return true;
			}
			if (OutBlocker)
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleAuditWhy), false, WeakKate.Get());
				Params.AddIgnoredActor(Actor);
				FHitResult Hit;
				Live->GetWorld()->LineTraceSingleByChannel(Hit, Lens, Anchor.Marker, ECC_Visibility, Params);
				*OutBlocker = FGrappleReachWorld::DescribeBlocker(Hit.GetActor());
				OutBlocker->Location = Hit.ImpactPoint;
			}
			return false;
		};
		Oracle.IsZipClear = [WeakGrapple, Actors](const FVector& From, const FGrappleReachAnchor& Anchor, bool bFromGround,
			FGrappleReachBlocker* OutBlocker) -> bool
		{
			const UGrappleComponent* Live = WeakGrapple.Get();
			const AGrappleAnchor* Actor = Actors.IsValidIndex(Anchor.Index) ? Actors[Anchor.Index].Get() : nullptr;
			if (!Live || !Actor)
			{
				return false;
			}
			AActor* Blocker = nullptr;
			if (Live->IsZipClear(From, Actor, bFromGround, &Blocker))
			{
				return true;
			}
			if (OutBlocker)
			{
				*OutBlocker = FGrappleReachWorld::DescribeBlocker(Blocker);
			}
			return false;
		};
		const USpringArmComponent* Boom = Kate->GetCameraBoom();
		if (Boom && Boom->bDoCollisionTest)
		{
			const TEnumAsByte<ECollisionChannel> Channel = Boom->ProbeChannel;
			const float ProbeSize = Boom->ProbeSize;
			Oracle.ProbeLens = [WeakKate, Channel, ProbeSize](const FVector& Origin, const FVector& Desired) -> FVector
			{
				const AHawkeyeCharacter* Live = WeakKate.Get();
				if (!Live)
				{
					return Desired;
				}
				FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleViewProbe), false, Live);
				FHitResult Hit;
				return Live->GetWorld()->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, Channel, FCollisionShape::MakeSphere(ProbeSize),
					Params) ? Hit.Location : Desired;
			};
		}
		return Oracle;
	}

	/** Runs the audit and writes the report; a broken guarantee is an AddError. */
	static void Run(FAutomationTestBase* Test, UWorld* World)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		const UCityMapData* Map = LoadObject<UCityMapData>(nullptr, UCityMapData::DefaultPath);
		if (!Grapple || !Map)
		{
			Test->AddError(TEXT("Audit: needs Kate with a grapple and DA_EastVillage_Map in the district."));
			return;
		}
		Grapple->RebuildAnchorGrid();

		FGrappleReachScene Scene;
		FString Error;
		if (!FGrappleReachWorld::BuildSceneFromActors(World, Map, Kate, Scene, Error))
		{
			Test->AddError(TEXT("Audit: ") + Error);
			return;
		}
		TMap<const AGrappleAnchor*, int32> IndexOf;
		for (int32 Index = 0; Index < Scene.AnchorActors.Num(); ++Index)
		{
			IndexOf.Add(Cast<AGrappleAnchor>(Scene.AnchorActors[Index].Get()), Index);
		}

		const FGrappleReachProfile Profile = FGrappleReachWorld::MakeProfile(Kate);
		FGrappleAuditInputs Inputs = FGrappleAudit::MakeWorldInputs(World, Scene, Profile, Kate);
		Inputs.Oracle = MakeKateOracle(Kate, Grapple, Scene);
		Inputs.ObjectiveChain = { TEXT("reach_roof"), TEXT("cross_block"), TEXT("find_arrow") };
		Inputs.StreetLinkObjectives = { TEXT("find_arrow") };
		Inputs.Picker = [Grapple, &IndexOf](const FGrappleReachSpot& Spot, const FVector& Lens, const FVector& Forward, int32& OutGreyed,
			FString& OutReason) -> int32
		{
			AGrappleAnchor* Greyed = nullptr;
			const AGrappleAnchor* Picked = Grapple->SelectBestAnchorFrom(Spot.Centre, /*bFromGround=*/true, Lens, Forward, Greyed, OutReason);
			const int32* GreyedIndex = Greyed ? IndexOf.Find(Greyed) : nullptr;
			const int32* PickedIndex = Picked ? IndexOf.Find(Picked) : nullptr;
			OutGreyed = GreyedIndex ? *GreyedIndex : INDEX_NONE;
			return PickedIndex ? *PickedIndex : INDEX_NONE;
		};
		const FGrappleAuditReport Report = FGrappleAudit::Run(Inputs);

		for (const FString& Line : Report.Objectives)
		{
			Test->AddInfo(TEXT("Audit: ") + Line);
		}
		for (const FString& Problem : Report.Errors)
		{
			Test->AddError(TEXT("Audit: ") + Problem + TEXT("."));
		}
		Test->AddInfo(TEXT("Audit: ") + Report.Summary);
		UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: %s"), *Report.Summary);
		for (const FString& Miss : Report.PickerMisses)
		{
			UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: picker missed: %s"), *Miss);
		}
		Test->AddInfo(TEXT("Audit: ") + Report.PickerSummary);
		UE_LOG(LogHawkeye, Display, TEXT("Grapple audit: %s"), *Report.PickerSummary);

		const FString Path = FGrappleAuditReport::GetDefaultPath();
		Report.WriteJson(Path);
		Test->AddInfo(TEXT("Audit: wrote ") + Path);
		for (const FString& Label : Report.RoofsNoStreetAtFineStep)
		{
			Test->AddInfo(TEXT("Audit: no anchor reachable from the street on ") + Label);
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeGrappleAuditRun, FAutomationTestBase*, Test);

bool FHawkeyeGrappleAuditRun::Update()
{
	UWorld* World = HawkeyeGrappleAudit::FindWorld();
	if (!World)
	{
		Test->AddError(TEXT("Audit: no game world."));
		return true;
	}
	HawkeyeGrappleAudit::Run(Test, World);
	return true;
}

bool FHawkeyeGrappleAudit::RunTest(const FString& Parameters)
{
	if (GIsEditor)
	{
		AddWarning(TEXT("Skipped (editor): needs the district as a game world. Run it from the standalone game (-game)."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(8.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeGrappleAuditRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
