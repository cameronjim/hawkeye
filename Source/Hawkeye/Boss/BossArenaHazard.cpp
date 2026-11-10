#include "Boss/BossArenaHazard.h"

#include "Hawkeye.h"
#include "Boss/BossRules.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Settings/DifficultySubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace HawkeyeBossHazard
{
	/** The warning light, lm, at the height of a telegraph. */
	static constexpr float WarningLumens = 2500.f;
	/** The lane strip's thickness and lift off the roof, cm. */
	static constexpr float LaneThickness = 2.f;
	static constexpr float LaneLift = 3.f;
	/** The shudder in a telegraph, degrees, and its speed, radians a second. */
	static constexpr float ShudderDegrees = 6.f;
	static constexpr float ShudderSpeed = 22.f;
	/** The tethered sway, degrees. */
	static constexpr float SwayDegrees = 3.f;
}

ABossArenaHazard::ABossArenaHazard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	// An inflatable: it shoves her by the rules (the hit), it never traps her capsule or pushes the camera about.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(true);

	Lane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lane"));
	Lane->SetupAttachment(Root);
	Lane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lane->SetCastShadow(false);
	Lane->SetVisibility(false);
	Lane->SetUsingAbsoluteLocation(true);
	Lane->SetUsingAbsoluteRotation(true);
	Lane->SetUsingAbsoluteScale(true);

	WarningLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("WarningLight"));
	WarningLight->SetupAttachment(Body);
	WarningLight->SetIntensityUnits(ELightUnits::Lumens);
	WarningLight->SetIntensity(0.f);
	// Pure red: any green in it reads orange on the white snow once it blooms.
	WarningLight->SetLightColor(FLinearColor(1.f, 0.02f, 0.01f));
	WarningLight->SetAttenuationRadius(900.f);
	WarningLight->SetCastShadows(false);
	WarningLight->SetRelativeLocation(FVector(0.f, 0.f, 250.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	LaneMesh = Cube.Succeeded() ? Cube.Object : nullptr;
}

void ABossArenaHazard::Configure(const FVector& InStart, const FVector& InEnd, UStaticMesh* BodyMesh, UMaterialInterface* LaneMaterial,
	const FBossHazardTuning& InTuning)
{
	Start = InStart;
	End = InEnd;
	Tuning = InTuning;
	if (BodyMesh)
	{
		Body->SetStaticMesh(BodyMesh);
	}
	if (LaneMesh)
	{
		Lane->SetStaticMesh(LaneMesh);
	}
	if (LaneMaterial)
	{
		Lane->SetMaterial(0, LaneMaterial);
	}
	// The strip covers the lane end to end with the body's reach either side; the engine cube is 100 cm.
	const FVector Along = End - Start;
	const float Length = Along.Size2D() + 2.f * Tuning.HalfLength;
	Lane->SetWorldLocationAndRotation((Start + End) * 0.5f + FVector(0.f, 0.f, HawkeyeBossHazard::LaneLift),
		FRotator(0.f, Along.Rotation().Yaw, 0.f));
	Lane->SetWorldScale3D(FVector(Length / 100.f, 2.f * Tuning.HalfWidth / 100.f, HawkeyeBossHazard::LaneThickness / 100.f));
	bLoose = false;
	Seconds = 0.f;
	State = FBossHazardState();
	SetActorLocationAndRotation(Start, FRotator(0.f, Along.Rotation().Yaw, 0.f));
}

void ABossArenaHazard::SetLoose(bool bInLoose, float SecondsIn)
{
	bLoose = bInLoose;
	Seconds = FMath::Max(SecondsIn, 0.f);
	SinceLastHit = 1000.f;
	State = bLoose ? UBossRules::HazardStateAt(Seconds, Tuning) : FBossHazardState();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s (lane %s to %s)."), *GetName(), bLoose ? TEXT("comes loose") : TEXT("tethered"),
		*Start.ToCompactString(), *End.ToCompactString());
}

void ABossArenaHazard::Settle()
{
	if (!bLoose)
	{
		return;
	}
	bLoose = false;
	State.Phase = EBossHazardPhase::Waiting;
	UE_LOG(LogHawkeye, Log, TEXT("%s: settles at %s after %d hit(s)."), *GetName(), *GetBodyLocation().ToCompactString(), HitCount);
}

APawn* ABossArenaHazard::FindTarget() const
{
	if (TargetOverride.IsValid())
	{
		return TargetOverride.Get();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

FVector ABossArenaHazard::GetBodyLocation() const
{
	return FMath::Lerp(Start, End, State.Along);
}

void ABossArenaHazard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceHazard(DeltaSeconds);
}

void ABossArenaHazard::Pin(float PinFor)
{
	if (!bLoose || PinFor <= 0.f)
	{
		return;
	}
	PinRemaining = FMath::Max(PinRemaining, PinFor);
	++PinCount;
	UE_LOG(LogHawkeye, Log, TEXT("%s: pinned for %.1f s at %s (%s)."), *GetName(), PinFor, *GetBodyLocation().ToCompactString(),
		*UEnum::GetValueAsString(State.Phase));
}

void ABossArenaHazard::AddPushable(AActor* Prop, float Radius)
{
	if (Prop)
	{
		Pushables.Add({ Prop, FMath::Max(Radius, 1.f) });
	}
}

void ABossArenaHazard::Offsets(const APawn* Pawn, float& OutAcross, float& OutAlong, float& OutUp) const
{
	const FVector Body3 = GetBodyLocation();
	const FVector LaneDir = (End - Start).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, LaneDir);
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector To = Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeight) - Body3;
	OutAcross = FVector::DotProduct(To, Across);
	OutAlong = FVector::DotProduct(To, LaneDir);
	OutUp = To.Z;
}

bool ABossArenaHazard::Reaches(const APawn* Pawn, float SecondsSinceLastHit) const
{
	if (!Pawn || !bLoose || bHeld || PinRemaining > 0.f)
	{
		return false;
	}
	float Across = 0.f;
	float Along = 0.f;
	float Up = 0.f;
	Offsets(Pawn, Across, Along, Up);
	return Up >= -150.f && Up <= 350.f && UBossRules::HazardHits(State.Phase, Across, Along, SecondsSinceLastHit, Tuning);
}

void ABossArenaHazard::PushProps()
{
	if (State.Phase != EBossHazardPhase::Sweep || Pushables.Num() == 0)
	{
		return;
	}
	const FVector LaneDir = (End - Start).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, LaneDir);
	const float LaneLength = FVector::Dist2D(Start, End);
	const float BodyAlong = State.Along * LaneLength;
	for (const FPushable& Entry : Pushables)
	{
		AActor* Prop = Entry.Actor.Get();
		if (!Prop)
		{
			continue;
		}
		const FVector At = Prop->GetActorLocation();
		const float PropAlong = FVector::DotProduct(At - Start, LaneDir);
		const float PropAcross = FVector::DotProduct(At - Start, Across);
		const float NewAlong = UBossRules::PushedAlong(PropAlong, PropAcross, Entry.Radius, BodyAlong, GetSweepDirection(), LaneLength, Tuning);
		if (!FMath::IsNearlyEqual(NewAlong, PropAlong, 0.5f))
		{
			// Shoved on ahead of the body, tumbling a little the way it goes.
			Prop->SetActorLocationAndRotation(At + LaneDir * (NewAlong - PropAlong),
				Prop->GetActorRotation() + FRotator(0.f, 9.f * GetSweepDirection(), 0.f));
			++PushCount;
		}
	}
}

void ABossArenaHazard::AdvanceHazard(float DeltaSeconds)
{
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	WobbleTime += Step;
	SinceLastHit += Step;
	const bool bWasPinned = PinRemaining > 0.f;
	PinRemaining = FMath::Max(0.f, PinRemaining - Step);
	if (bWasPinned && PinRemaining <= 0.f)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the pin lets go."), *GetName());
	}
	if (bLoose && (bHeld || bWasPinned))
	{
		// Held or pinned: the clock stands still, nothing is hit; the body still sways where it is.
		PlaceBody(Step);
		return;
	}
	if (bLoose)
	{
		const EBossHazardPhase Before = State.Phase;
		Seconds += Step;
		State = UBossRules::HazardStateAt(Seconds, Tuning);
		if (State.Phase == EBossHazardPhase::Telegraph)
		{
			TelegraphSeenThisCycle = State.Alpha * Tuning.TelegraphSeconds;
		}
		else if (Before == EBossHazardPhase::Rest && State.Phase != EBossHazardPhase::Rest)
		{
			TelegraphSeenThisCycle = 0.f;
		}
	}
	PlaceBody(Step);
	if (bLoose)
	{
		PushProps();
		CheckHit(FindTarget());
	}
}

void ABossArenaHazard::PlaceBody(float /*DeltaSeconds*/)
{
	const FVector Along = (End - Start).GetSafeNormal2D();
	const float Yaw = Along.IsNearlyZero() ? GetActorRotation().Yaw : Along.Rotation().Yaw;
	float Roll = 0.f;
	const bool bStill = bHeld || PinRemaining > 0.f;
	if (!bLoose || bStill)
	{
		Roll = HawkeyeBossHazard::SwayDegrees * FMath::Sin(WobbleTime * 1.3f);
	}
	else if (State.Phase == EBossHazardPhase::Telegraph)
	{
		Roll = HawkeyeBossHazard::ShudderDegrees * FMath::Sin(WobbleTime * HawkeyeBossHazard::ShudderSpeed);
	}
	else if (State.Phase == EBossHazardPhase::Sweep)
	{
		// It leans into the way it is going.
		Roll = (State.Sweeps % 2 == 0 ? 1.f : -1.f) * 8.f;
	}
	SetActorLocationAndRotation(GetBodyLocation(), FRotator(0.f, Yaw, Roll));
	const bool bWarn = bLoose && !bStill && State.Phase == EBossHazardPhase::Telegraph;
	Lane->SetVisibility(bWarn || (bLoose && !bStill && State.Phase == EBossHazardPhase::Sweep));
	WarningLight->SetIntensity(bWarn ? HawkeyeBossHazard::WarningLumens * (0.5f + 0.5f * FMath::Abs(FMath::Sin(WobbleTime * 8.f))) : 0.f);
}

void ABossArenaHazard::CheckHit(APawn* Target)
{
	if (!Target)
	{
		return;
	}
	UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
	if (!Health || !Health->IsAlive())
	{
		return;
	}
	const FVector Body3 = GetBodyLocation();
	const FVector LaneDir = (End - Start).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, LaneDir);
	const ACharacter* Character = Cast<ACharacter>(Target);
	const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Feet = Target->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	const FVector To = Feet - Body3;
	// Over it (a jump onto it) or well below the roof does not count.
	if (To.Z < -150.f || To.Z > 350.f)
	{
		return;
	}
	const float DistanceAcross = FVector::DotProduct(To, Across);
	const float DistanceAlong = FVector::DotProduct(To, LaneDir);
	if (!UBossRules::HazardHits(State.Phase, DistanceAcross, DistanceAlong, SinceLastHit, Tuning))
	{
		return;
	}
	const float Damage = Tuning.Damage * UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ThugDamage);
	SinceLastHit = 0.f;
	++HitCount;
	DamageDealt += Damage;
	TelegraphBeforeLastHit = TelegraphSeenThisCycle;
	Health->ApplyDamage(Damage, this);
	// Out of the lane, the short way, and the way it is going.
	const float Side = DistanceAcross >= 0.f ? 1.f : -1.f;
	const float Going = State.Sweeps % 2 == 0 ? 1.f : -1.f;
	if (ACharacter* Hit = Cast<ACharacter>(Target))
	{
		const FVector Shove = (Across * Side * 0.8f + LaneDir * Going * 0.6f).GetSafeNormal2D() * Tuning.KnockbackSpeed + FVector(0.f, 0.f, 250.f);
		Hit->LaunchCharacter(Shove, true, true);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: hits %s for %.0f (%.0f cm across, %.0f along, after a %.1f s telegraph)."), *GetName(),
		*Target->GetName(), Damage, DistanceAcross, DistanceAlong, TelegraphBeforeLastHit);
}
