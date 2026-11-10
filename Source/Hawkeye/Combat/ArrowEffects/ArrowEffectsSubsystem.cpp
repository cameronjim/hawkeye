#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"

#include "Hawkeye.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/TimeOfDaySubsystem.h"

const FName UArrowEffectsSubsystem::LampTag(TEXT("CityLamp"));

namespace HawkeyeArrowEffects
{
	static const FName GlowParameter(TEXT("Intensity"));
}

UArrowEffectsSubsystem* UArrowEffectsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UArrowEffectsSubsystem>() : nullptr;
}

TStatId UArrowEffectsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UArrowEffectsSubsystem, STATGROUP_Tickables);
}

// --- Smoke --------------------------------------------------------------------------------------------

void UArrowEffectsSubsystem::RegisterSmoke(ASmokeCloud* Cloud)
{
	if (Cloud)
	{
		Clouds.AddUnique(Cloud);
	}
}

void UArrowEffectsSubsystem::UnregisterSmoke(ASmokeCloud* Cloud)
{
	Clouds.RemoveAll(
		[Cloud](const TWeakObjectPtr<ASmokeCloud>& Entry) { return !Entry.IsValid() || Entry.Get() == Cloud; });
}

bool UArrowEffectsSubsystem::SegmentIntersectsSphere(
	const FVector& A, const FVector& B, const FVector& Centre, float Radius)
{
	if (Radius <= 0.f)
	{
		return false;
	}
	const FVector Closest = FMath::ClosestPointOnSegment(Centre, A, B);
	return FVector::DistSquared(Closest, Centre) <= FMath::Square(Radius);
}

bool UArrowEffectsSubsystem::IsSightBlocked(const FVector& From, const FVector& To) const
{
	for (const TWeakObjectPtr<ASmokeCloud>& Entry : Clouds)
	{
		const ASmokeCloud* Cloud = Entry.Get();
		if (Cloud && Cloud->IsBlocking() &&
			SegmentIntersectsSphere(From, To, Cloud->GetCloudCentre(), Cloud->GetCurrentRadius()))
		{
			return true;
		}
	}
	return false;
}

bool UArrowEffectsSubsystem::IsInsideSmoke(const FVector& Point) const
{
	for (const TWeakObjectPtr<ASmokeCloud>& Entry : Clouds)
	{
		const ASmokeCloud* Cloud = Entry.Get();
		if (Cloud && Cloud->IsBlocking() &&
			FVector::DistSquared(Point, Cloud->GetCloudCentre()) <= FMath::Square(Cloud->GetCurrentRadius()))
		{
			return true;
		}
	}
	return false;
}

// --- Lamps --------------------------------------------------------------------------------------------

void UArrowEffectsSubsystem::SwitchOff(AActor* Lamp, FLampOutage& Outage) const
{
	TArray<ULightComponent*> Lights;
	Lamp->GetComponents<ULightComponent>(Lights);
	for (ULightComponent* Light : Lights)
	{
		if (Light && Light->IsVisible())
		{
			Light->SetVisibility(false);
			Outage.Lights.Add(Light);
		}
	}

	// The head glows through its material's Intensity; the pole's material has none, which is fine.
	TArray<UStaticMeshComponent*> Meshes;
	Lamp->GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		UMaterialInstanceDynamic* Glow = Mesh ? Mesh->CreateDynamicMaterialInstance(0) : nullptr;
		float Intensity = 0.f;
		if (Glow &&
			Glow->GetScalarParameterValue(
				FHashedMaterialParameterInfo(HawkeyeArrowEffects::GlowParameter), Intensity) &&
			Intensity > 0.f)
		{
			Glow->SetScalarParameterValue(HawkeyeArrowEffects::GlowParameter, 0.f);
			Outage.Glows.Add(Glow);
			Outage.GlowIntensities.Add(Intensity);
		}
	}

	TArray<UAudioComponent*> Sounds;
	Lamp->GetComponents<UAudioComponent>(Sounds);
	for (UAudioComponent* Sound : Sounds)
	{
		if (Sound && Sound->IsPlaying())
		{
			Sound->Stop();
			Outage.Sounds.Add(Sound);
		}
	}
}

void UArrowEffectsSubsystem::SwitchOn(FLampOutage& Outage) const
{
	for (const TWeakObjectPtr<ULightComponent>& Light : Outage.Lights)
	{
		if (ULightComponent* Live = Light.Get())
		{
			Live->SetVisibility(true);
		}
	}
	// Once the time of day has recorded the lamp, it owns the head and the buzz: by day they stay off.
	UTimeOfDaySubsystem* TimeOfDay = UTimeOfDaySubsystem::Get(this);
	if (TimeOfDay && TimeOfDay->RefreshLamp(Outage.Lamp.Get()))
	{
		return;
	}
	for (int32 Index = 0; Index < Outage.Glows.Num(); ++Index)
	{
		if (UMaterialInstanceDynamic* Glow = Outage.Glows[Index].Get())
		{
			Glow->SetScalarParameterValue(HawkeyeArrowEffects::GlowParameter, Outage.GlowIntensities[Index]);
		}
	}
	for (const TWeakObjectPtr<UAudioComponent>& Sound : Outage.Sounds)
	{
		if (UAudioComponent* Live = Sound.Get())
		{
			Live->Play();
		}
	}
}

int32 UArrowEffectsSubsystem::DisableLampsInRadius(const FVector& Centre, float Radius, float Seconds)
{
	UWorld* World = GetWorld();
	if (!World || Radius <= 0.f)
	{
		return 0;
	}

	int32 Affected = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Lamp = *It;
		const FVector Offset = Lamp->GetActorLocation() - Centre;
		if (!Lamp->ActorHasTag(LampTag) || Offset.SizeSquared2D() > FMath::Square(Radius) ||
			FMath::Abs(Offset.Z) > LampReachHeight)
		{
			continue;
		}
		++Affected;
		FLampOutage* Existing =
			Lamps.FindByPredicate([Lamp](const FLampOutage& Entry) { return Entry.Lamp.Get() == Lamp; });
		if (Existing)
		{
			Existing->OnAgainAt = FMath::Max(Existing->OnAgainAt, Clock + Seconds);
			continue;
		}
		FLampOutage& Outage = Lamps.AddDefaulted_GetRef();
		Outage.Lamp = Lamp;
		Outage.OnAgainAt = Clock + Seconds;
		SwitchOff(Lamp, Outage);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d lamp actor(s) within %.0f cm of %s out for %.0f s."), *GetName(), Affected,
		Radius, *Centre.ToCompactString(), Seconds);
	return Affected;
}

bool UArrowEffectsSubsystem::IsLampDisabled(const AActor* Lamp) const
{
	return Lamp && Lamps.ContainsByPredicate([Lamp](const FLampOutage& Entry) { return Entry.Lamp.Get() == Lamp; });
}

void UArrowEffectsSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	for (int32 Index = Lamps.Num() - 1; Index >= 0; --Index)
	{
		if (!Lamps[Index].Lamp.IsValid() || Clock >= Lamps[Index].OnAgainAt)
		{
			// Off the list first, so the time of day sees the lamp as no longer dark when it relights it.
			FLampOutage Outage = MoveTemp(Lamps[Index]);
			Lamps.RemoveAtSwap(Index);
			SwitchOn(Outage);
			UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s back on."), *GetName(), *GetNameSafe(Outage.Lamp.Get()));
		}
	}
	Clouds.RemoveAll([](const TWeakObjectPtr<ASmokeCloud>& Entry) { return !Entry.IsValid(); });
}
