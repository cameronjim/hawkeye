#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/EmpPulse.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/ArrowEffects/HeldEffect.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/WeaponComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Vfx/HawkeyeVfxMath.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/CityLedgeSpawner.h"
#include "World/GrappleAnchor.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The effect rules from claude-docs/gameplay-semantics.md, "Effects": every game event asks the
 * VFX subsystem for its effect (counted whether or not a system is set, so nothing here loads
 * Content or draws), the snowfall's position, the landing puff's size, the scorch decal's placement
 * on the ground trace, the chimney picks and the screen pulse.
 */
namespace HawkeyeVfxTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static int32 Requests(const FHawkeyeTestWorld& TestWorld, FName Event)
	{
		const UHawkeyeVfxSubsystem* Vfx = TestWorld.Get()->GetSubsystem<UHawkeyeVfxSubsystem>();
		return Vfx ? Vfx->GetRequestCount(Event) : -1;
	}

	static FHitResult MakeHit(AActor* Actor, const FVector& Point, const FVector& Normal = FVector::UpVector)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = Point;
		Hit.Location = Point;
		Hit.ImpactNormal = Normal;
		Hit.Normal = Normal;
		Hit.TraceStart = Point + Normal * 100.f;
		Hit.TraceEnd = Point - Normal * 100.f;
		if (Actor)
		{
			Hit.HitObjectHandle = FActorInstanceHandle(Actor);
		}
		return Hit;
	}

	static UArrowDefinition* MakeArrow(UObject* Outer, EArrowHitEffect Effect, float Damage)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->OnHitEffect = Effect;
		Arrow->Damage = Damage;
		return Arrow;
	}

	template <typename TEffect>
	static TEffect* SpawnEffect(const FHawkeyeTestWorld& TestWorld, const FHitResult& Hit, const UArrowDefinition* Arrow)
	{
		TEffect* Effect = Cast<TEffect>(TestWorld.SpawnActor(TEffect::StaticClass(), Hit.ImpactPoint, FRotator::ZeroRotator));
		if (Effect)
		{
			Effect->InitEffect(Arrow, nullptr, Hit);
			Effect->Activate();
		}
		return Effect;
	}

	static AHawkeyeTestBlocker* SpawnFloor(const FHawkeyeTestWorld& TestWorld, float TopZ)
	{
		AHawkeyeTestBlocker* Floor = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(0.f, 0.f, TopZ - 50.f), FRotator::ZeroRotator));
		if (Floor)
		{
			Floor->SetExtent(FVector(2000.f, 2000.f, 50.f));
		}
		return Floor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxSnowfallFollowsCamera, "Hawkeye.Vfx.SnowfallFollowsCamera", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxSnowfallFollowsCamera::RunTest(const FString& Parameters)
{
	using HawkeyeVfxMath::ComputeSnowfallOrigin;
	const FVector Camera(1000.f, -500.f, 300.f);

	TestTrue(TEXT("Still, looking along +X: 600 ahead and 250 up"),
		ComputeSnowfallOrigin(Camera, FVector::ForwardVector, FVector::ZeroVector, 600.f, 250.f, 0.5f)
			.Equals(FVector(1600.f, -500.f, 550.f), 0.01f));

	const FVector Pitched = FRotator(-40.f, 90.f, 0.f).Vector();
	TestTrue(TEXT("Pitch is ignored: looking down along +Y puts it 600 along +Y at the same height"),
		ComputeSnowfallOrigin(Camera, Pitched, FVector::ZeroVector, 600.f, 250.f, 0.5f)
			.Equals(FVector(1000.f, 100.f, 550.f), 0.05f));

	TestTrue(TEXT("Running at 500 cm/s along +X leads by half a second (250 cm more)"),
		ComputeSnowfallOrigin(Camera, FVector::ForwardVector, FVector(500.f, 0.f, 0.f), 600.f, 250.f, 0.5f)
			.Equals(FVector(1850.f, -500.f, 550.f), 0.01f));

	TestTrue(TEXT("Falling does not drag it down: only ground motion leads"),
		ComputeSnowfallOrigin(Camera, FVector::ForwardVector, FVector(0.f, 0.f, -900.f), 600.f, 250.f, 0.5f)
			.Equals(FVector(1600.f, -500.f, 550.f), 0.01f));

	TestTrue(TEXT("Looking straight up keeps it over the camera"),
		ComputeSnowfallOrigin(Camera, FVector::UpVector, FVector::ZeroVector, 600.f, 250.f, 0.5f)
			.Equals(FVector(1000.f, -500.f, 550.f), 0.01f));

	// The camera moves 1000 cm; the snow moves with it, the same distance.
	const FVector Before = ComputeSnowfallOrigin(Camera, FVector::ForwardVector, FVector::ZeroVector, 600.f, 250.f, 0.f);
	const FVector After = ComputeSnowfallOrigin(Camera + FVector(0.f, 1000.f, 0.f), FVector::ForwardVector,
		FVector::ZeroVector, 600.f, 250.f, 0.f);
	TestTrue(TEXT("It follows the camera one for one"), (After - Before).Equals(FVector(0.f, 1000.f, 0.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxLandingAndFootstepShape, "Hawkeye.Vfx.LandingAndFootstepShape", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxLandingAndFootstepShape::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxMath;
	TestEqual(TEXT("No puff below 150 cm"), ComputeLandingPuffScale(149.f), 0.f);
	TestEqual(TEXT("0.6 at 150 cm"), ComputeLandingPuffScale(150.f), 0.6f);
	TestTrue(TEXT("Bigger the further she falls"), ComputeLandingPuffScale(400.f) > ComputeLandingPuffScale(200.f));
	TestEqual(TEXT("1.6 at 800 cm"), ComputeLandingPuffScale(800.f), 1.6f);
	TestEqual(TEXT("Capped at 1.6"), ComputeLandingPuffScale(5000.f), 1.6f);

	const FVector Kick = ComputeFootstepKickDirection(FVector::ForwardVector, FVector(400.f, 0.f, 0.f));
	TestTrue(TEXT("Running along +X kicks snow backward"), Kick.X < -0.5f);
	TestTrue(TEXT("And up"), Kick.Z > 0.5f);
	TestTrue(TEXT("Unit length"), FMath::IsNearlyEqual(Kick.Size(), 1.f, 1e-4f));
	const FVector Still = ComputeFootstepKickDirection(FVector::RightVector, FVector::ZeroVector);
	TestTrue(TEXT("Standing still kicks behind the way she faces"), Still.Y < -0.5f && Still.Z > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxScorchOnGround, "Hawkeye.Vfx.ScorchOnGroundTrace", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxScorchOnGround::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxMath;
	using namespace HawkeyeVfxTest;

	// Pure placement.
	FVector Location;
	FRotator Rotation;
	const FHitResult Flat = MakeHit(nullptr, FVector(10.f, 20.f, 0.f));
	TestTrue(TEXT("Flat ground takes a scorch"), ComputeScorchPlacement(Flat, 30.f, Location, Rotation));
	TestTrue(TEXT("At the hit point"), Location.Equals(FVector(10.f, 20.f, 0.f)));
	TestTrue(TEXT("Projecting down into it"), Rotation.Vector().Equals(-FVector::UpVector, 1e-3f));
	TestEqual(TEXT("With the roll asked for"), Rotation.Roll, 30.0);
	TestFalse(TEXT("A wall is not ground"),
		ComputeScorchPlacement(MakeHit(nullptr, FVector::ZeroVector, FVector::RightVector), 0.f, Location, Rotation));
	TestFalse(TEXT("No hit, no scorch"), ComputeScorchPlacement(FHitResult(), 0.f, Location, Rotation));

	// The trace, and the blast using it.
	FHawkeyeTestWorld TestWorld;
	SpawnFloor(TestWorld, 0.f);
	FHitResult Ground;
	TestTrue(TEXT("The trace finds a floor 120 cm down"), TraceGround(TestWorld.Get(), FVector(0.f, 0.f, 120.f), 250.f, Ground));
	TestTrue(TEXT("Its top"), FMath::IsNearlyEqual(Ground.ImpactPoint.Z, 0.f, 0.5f));
	TestFalse(TEXT("But not one 400 cm down"), TraceGround(TestWorld.Get(), FVector(0.f, 0.f, 400.f), 250.f, Ground));

	UArrowDefinition* Explosive = MakeArrow(TestWorld.Get(), EArrowHitEffect::Explosive, 80.f);
	SpawnEffect<AExplosiveBlast>(TestWorld, MakeHit(nullptr, FVector(0.f, 0.f, 60.f)), Explosive);
	TestEqual(TEXT("A blast 60 cm over the floor asks for one explosion"), Requests(TestWorld, UHawkeyeVfxSubsystem::ExplosionEvent), 1);
	TestEqual(TEXT("And one scorch"), Requests(TestWorld, UHawkeyeVfxSubsystem::ScorchEvent), 1);
	SpawnEffect<AExplosiveBlast>(TestWorld, MakeHit(nullptr, FVector(0.f, 0.f, 900.f)), Explosive);
	TestEqual(TEXT("A blast in the air leaves no scorch"), Requests(TestWorld, UHawkeyeVfxSubsystem::ScorchEvent), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxTrickEffects, "Hawkeye.Vfx.TrickEffectsAsk", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxTrickEffects::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxTest;
	FHawkeyeTestWorld TestWorld;
	UHawkeyeVfxSubsystem* Vfx = TestWorld.Get()->GetSubsystem<UHawkeyeVfxSubsystem>();
	if (!TestNotNull(TEXT("The world has a VFX subsystem"), Vfx))
	{
		return false;
	}
	TArray<FName> Heard;
	Vfx->OnRequested.AddLambda([&Heard](FName Event, const FVector&) { Heard.Add(Event); });

	ASmokeCloud* Smoke = SpawnEffect<ASmokeCloud>(TestWorld, MakeHit(nullptr, FVector(0.f, 0.f, 0.f)),
		MakeArrow(TestWorld.Get(), EArrowHitEffect::Smoke, 0.f));
	SpawnEffect<AEmpPulse>(TestWorld, MakeHit(nullptr, FVector(3000.f, 0.f, 0.f)), MakeArrow(TestWorld.Get(), EArrowHitEffect::EMP, 0.f));
	SpawnEffect<AHeldEffect>(TestWorld, MakeHit(nullptr, FVector(6000.f, 0.f, 0.f)),
		MakeArrow(TestWorld.Get(), EArrowHitEffect::Putty, 10.f));

	TestEqual(TEXT("The smoke asks for its cloud once"), Requests(TestWorld, UHawkeyeVfxSubsystem::SmokeCloudEvent), 1);
	TestEqual(TEXT("The EMP for its pulse once"), Requests(TestWorld, UHawkeyeVfxSubsystem::EmpPulseEvent), 1);
	TestEqual(TEXT("The putty for its splat once"), Requests(TestWorld, UHawkeyeVfxSubsystem::PuttySplatEvent), 1);
	TestTrue(TEXT("The listener heard each"), Heard.Contains(UHawkeyeVfxSubsystem::SmokeCloudEvent)
		&& Heard.Contains(UHawkeyeVfxSubsystem::EmpPulseEvent) && Heard.Contains(UHawkeyeVfxSubsystem::PuttySplatEvent));
	TestEqual(TEXT("Nothing drew: no system is set"), Vfx->GetSpawnCount(), 0);
	TestFalse(TEXT("So the smoke keeps its placeholder puffs"), Smoke && Smoke->HasEffectVfx());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxArrowEvents, "Hawkeye.Vfx.ArrowFlightAndImpacts", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxArrowEvents::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(2000.f, 0.f, 100.f), FRotator::ZeroRotator));
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(0.f, 1500.f, 100.f), FRotator::ZeroRotator));
	UBowComponent* Bow = Kate ? Kate->FindComponentByClass<UBowComponent>() : nullptr;
	if (!Kate || !Wall || !Thug || !Bow)
	{
		AddError(TEXT("Could not set up Kate, her bow, a wall and a thug."));
		return false;
	}
	UArrowDefinition* Standard = MakeArrow(TestWorld.Get(), EArrowHitEffect::None, 40.f);

	auto Loose = [&](const FVector& From) -> AArrowProjectile*
	{
		AArrowProjectile* Arrow = Cast<AArrowProjectile>(
			TestWorld.SpawnActor(AArrowProjectile::StaticClass(), From, FRotator::ZeroRotator));
		if (Arrow)
		{
			Arrow->InitArrow(Standard, nullptr, 40.f, Kate, Bow);
			Arrow->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
		}
		return Arrow;
	};

	AArrowProjectile* IntoWall = Loose(FVector(100.f, 0.f, 100.f));
	TestEqual(TEXT("An arrow in flight asks for its trail"), Requests(TestWorld, UHawkeyeVfxSubsystem::ArrowTrailEvent), 1);
	if (IntoWall)
	{
		IntoWall->HandleImpact(MakeHit(Wall, FVector(1950.f, 0.f, 100.f), -FVector::ForwardVector));
	}
	TestEqual(TEXT("Into a wall: one impact"), Requests(TestWorld, UHawkeyeVfxSubsystem::ArrowImpactEvent), 1);
	TestEqual(TEXT("And no hit spark"), Requests(TestWorld, UHawkeyeVfxSubsystem::HitSparkEvent), 0);

	AArrowProjectile* IntoThug = Loose(FVector(0.f, 1300.f, 100.f));
	if (IntoThug)
	{
		IntoThug->HandleImpact(MakeHit(Thug, Thug->GetActorLocation() - FVector(0.f, 30.f, 0.f), -FVector::RightVector));
	}
	TestEqual(TEXT("Into a thug: a hit spark, not blood"), Requests(TestWorld, UHawkeyeVfxSubsystem::HitSparkEvent), 1);
	TestEqual(TEXT("The wall's impact is still the only one"), Requests(TestWorld, UHawkeyeVfxSubsystem::ArrowImpactEvent), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxKateEvents, "Hawkeye.Vfx.FootstepLandingZipAndPulse", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxKateEvents::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator));
	AGrappleAnchor* Anchor = Cast<AGrappleAnchor>(
		TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(1800.f, 0.f, 500.f), FRotator::ZeroRotator));
	if (!Kate || !Anchor)
	{
		AddError(TEXT("Could not spawn Kate and an anchor."));
		return false;
	}

	Kate->TestPlayFootstep();
	Kate->TestPlayFootstep();
	TestEqual(TEXT("Each footstep kicks snow"), Requests(TestWorld, UHawkeyeVfxSubsystem::FootstepEvent), 2);

	Kate->TestApplyLanding(100.f);
	TestEqual(TEXT("A 1 m drop throws no snow"), Requests(TestWorld, UHawkeyeVfxSubsystem::LandingEvent), 0);
	Kate->TestApplyLanding(390.f);
	TestEqual(TEXT("A 3.9 m drop does"), Requests(TestWorld, UHawkeyeVfxSubsystem::LandingEvent), 1);

	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	Grapple->HandleArrowArrived(nullptr, Anchor);
	TestEqual(TEXT("The grapple arrow's arrival sparks at the anchor"), Requests(TestWorld, UHawkeyeVfxSubsystem::AnchorSparksEvent), 1);
	TestTrue(TEXT("And starts the zip"), Grapple->IsZipping());
	TestEqual(TEXT("Which asks for the zip line once"), Requests(TestWorld, UHawkeyeVfxSubsystem::ZipLineEvent), 1);
	for (float Elapsed = 0.f; Grapple->IsZipping() && Elapsed < 5.f; Elapsed += 0.05f)
	{
		Kate->TestTickMovement(0.05f);
	}
	TestFalse(TEXT("She lands"), Grapple->IsZipping());
	TestNull(TEXT("With no line left behind"), Grapple->GetZipLineComponent());

	Kate->PlayScreenPulse(0.5f, 1.f);
	TestEqual(TEXT("The screen pulse starts at full strength"), Kate->GetScreenPulseStrength(), 1.f);
	TestEqual(TEXT("And is counted"), Requests(TestWorld, UHawkeyeVfxSubsystem::ScreenPulseEvent), 1);
	Kate->TestTickHitReactions(0.25f);
	TestTrue(TEXT("Halfway it has eased to a quarter"), FMath::IsNearlyEqual(Kate->GetScreenPulseStrength(), 0.25f, 1e-4f));
	Kate->PlayScreenPulse(0.5f, 0.1f);
	TestTrue(TEXT("A weaker pulse does not cut it short"), FMath::IsNearlyEqual(Kate->GetScreenPulseStrength(), 0.25f, 1e-4f));
	Kate->TestTickHitReactions(0.3f);
	TestEqual(TEXT("Then it is gone"), Kate->GetScreenPulseStrength(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxGunshot, "Hawkeye.Vfx.GunshotFlashAndTracer", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxGunshot::RunTest(const FString& Parameters)
{
	using namespace HawkeyeVfxTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	UWeaponComponent* Weapon = Thug ? Thug->GetWeaponComponent() : nullptr;
	if (!Weapon)
	{
		AddError(TEXT("No thug with a weapon."));
		return false;
	}
	Weapon->SetTestTimeSeconds(0.0);
	TestTrue(TEXT("He fires"), Weapon->Fire());
	TestEqual(TEXT("One muzzle flash"), Requests(TestWorld, UHawkeyeVfxSubsystem::MuzzleFlashEvent), 1);
	TestEqual(TEXT("One tracer"), Requests(TestWorld, UHawkeyeVfxSubsystem::TracerEvent), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxChimneys, "Hawkeye.Vfx.ChimneyPicks", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxChimneys::RunTest(const FString& Parameters)
{
	TArray<FTransform> Chimneys;
	Chimneys.Add(FTransform(FRotator::ZeroRotator, FVector(5000.f, 0.f, 1200.f)));
	Chimneys.Add(FTransform(FRotator::ZeroRotator, FVector(100.f, 0.f, 900.f), FVector(1.f, 1.f, 2.f)));
	Chimneys.Add(FTransform(FRotator::ZeroRotator, FVector(-2000.f, 0.f, 1500.f)));
	const TArray<FVector> Tops = ACityLedgeSpawner::PickChimneyTops(Chimneys, FVector(0.f, 0.f, 99999.f), 2, 175.f);
	TestEqual(TEXT("Two picked"), Tops.Num(), 2);
	TestTrue(TEXT("The nearest first, measured across the ground, its top scaled with it"),
		Tops.IsValidIndex(0) && Tops[0].Equals(FVector(100.f, 0.f, 900.f + 350.f)));
	TestTrue(TEXT("Then the next nearest"), Tops.IsValidIndex(1) && Tops[1].Equals(FVector(-2000.f, 0.f, 1500.f + 175.f)));
	TestEqual(TEXT("None asked, none picked"), ACityLedgeSpawner::PickChimneyTops(Chimneys, FVector::ZeroVector, 0, 175.f).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxUnset, "Hawkeye.Vfx.UnsetDrawsNothing", HawkeyeVfxTest::Flags)

bool FHawkeyeVfxUnset::RunTest(const FString& Parameters)
{
	const TSoftObjectPtr<UNiagaraSystem> None;
	TestNull(TEXT("No world: nothing, no crash"),
		UHawkeyeVfxSubsystem::SpawnAt(nullptr, None, FVector::ZeroVector, FRotator::ZeroRotator, UHawkeyeVfxSubsystem::TracerEvent));
	TObjectPtr<UNiagaraComponent> Nothing = nullptr;
	UHawkeyeVfxSubsystem::Release(Nothing);
	UHawkeyeVfxSubsystem::Kill(Nothing);

	FHawkeyeTestWorld TestWorld;
	AActor* Context = TestWorld.SpawnActor(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNull(TEXT("Unset in a world: nothing drawn"),
		UHawkeyeVfxSubsystem::SpawnAt(Context, None, FVector::ZeroVector, FRotator::ZeroRotator, UHawkeyeVfxSubsystem::TracerEvent));
	const UHawkeyeVfxSubsystem* Vfx = TestWorld.Get()->GetSubsystem<UHawkeyeVfxSubsystem>();
	TestEqual(TEXT("But the request is counted"), Vfx ? Vfx->GetRequestCount(UHawkeyeVfxSubsystem::TracerEvent) : -1, 1);
	TestEqual(TEXT("And nothing spawned"), Vfx ? Vfx->GetSpawnCount() : -1, 0);
	return true;
}

#endif
