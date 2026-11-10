#include "Combat/ArrowEffects/SmokeCloud.h"

#include "Hawkeye.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Vfx/HawkeyeVfxSubsystem.h"

ASmokeCloud::ASmokeCloud()
{
	// A loose ring of puffs round a big central one, some low and wide, some high: reads as a
	// cloud from street level rather than as a ball.
	PuffLayout = {
		FVector4f(0.f, 0.f, 0.05f, 0.9f),
		FVector4f(0.55f, 0.f, -0.2f, 0.6f),
		FVector4f(-0.5f, 0.2f, -0.2f, 0.62f),
		FVector4f(0.1f, 0.55f, -0.15f, 0.58f),
		FVector4f(-0.15f, -0.55f, -0.2f, 0.6f),
		FVector4f(0.35f, 0.35f, 0.3f, 0.5f),
		FVector4f(-0.35f, -0.3f, 0.35f, 0.5f),
		FVector4f(0.4f, -0.4f, 0.15f, 0.48f),
		FVector4f(-0.4f, 0.45f, 0.2f, 0.46f),
	};
	for (int32 Index = 0; Index < PuffCount; ++Index)
	{
		UStaticMeshComponent* Puff = MakeVisualPart(*FString::Printf(TEXT("Puff%d"), Index), SphereMesh);
		Puff->SetRelativeScale3D(FVector(0.01f));
		Puffs.Add(Puff);
	}
}

void ASmokeCloud::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* Material = LoadEffectMaterial(FxMaterialPath);
	PuffMaterials.Reset();
	for (UStaticMeshComponent* Puff : Puffs)
	{
		TintPart(Puff, Material, SmokeColor, 0.f);
		PuffMaterials.Add(Puff ? Cast<UMaterialInstanceDynamic>(Puff->GetMaterial(0)) : nullptr);
	}
}

void ASmokeCloud::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this))
	{
		Effects->UnregisterSmoke(this);
	}
	Super::EndPlay(EndPlayReason);
}

FVector ASmokeCloud::GetCloudCentre() const
{
	return ImpactPoint + FVector(0.f, 0.f, CentreLift);
}

bool ASmokeCloud::IsBlocking() const
{
	return bActivated && Elapsed < LifeSeconds;
}

float ASmokeCloud::GetCurrentRadius() const
{
	if (!IsBlocking())
	{
		return 0.f;
	}
	// It blinds at the size it has reached, so an arrow landing between a thug and Kate buys a
	// moment's grace but not an instant wall.
	const float Grow = FMath::Clamp(Elapsed / GrowSeconds, 0.f, 1.f);
	return Radius * FMath::Lerp(0.35f, 1.f, FMath::Sin(Grow * HALF_PI));
}

void ASmokeCloud::Activate()
{
	Super::Activate();
	if (UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this))
	{
		Effects->RegisterSmoke(this);
		bRegistered = true;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: smoke cloud %.0f cm for %.0f s at %s."), *GetName(), Radius, LifeSeconds,
		*GetCloudCentre().ToCompactString());
	if (SpawnEffectVfx(UHawkeyeVfxSubsystem::SmokeCloudEvent, GetCloudCentre()))
	{
		HidePlaceholders();
	}
	UpdatePuffs();
}

void ASmokeCloud::AdvanceEffect(float DeltaSeconds)
{
	Super::AdvanceEffect(DeltaSeconds);
	if (!bActivated)
	{
		return;
	}
	UpdatePuffs();
	if (Elapsed >= LifeSeconds)
	{
		if (UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this))
		{
			Effects->UnregisterSmoke(this);
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: smoke cleared."), *GetName());
		Destroy();
	}
}

void ASmokeCloud::UpdatePuffs()
{
	const float Grow = FMath::Clamp(Elapsed / GrowSeconds, 0.f, 1.f);
	const float FadeStart = FMath::Max(LifeSeconds - FadeSeconds, 0.f);
	const float Fade = FadeSeconds > 0.f ? 1.f - FMath::Clamp((Elapsed - FadeStart) / FadeSeconds, 0.f, 1.f) : 1.f;
	const float Size = Radius * FMath::Lerp(0.35f, 1.f, FMath::Sin(Grow * HALF_PI));
	const float Drift = Elapsed * 6.f;

	SetActorLocation(GetCloudCentre());
	for (int32 Index = 0; Index < Puffs.Num() && Index < PuffLayout.Num(); ++Index)
	{
		UStaticMeshComponent* Puff = Puffs[Index];
		if (!Puff)
		{
			continue;
		}
		const FVector4f& Layout = PuffLayout[Index];
		// The engine sphere is 100 cm across; each puff drifts outward and up slowly as it lives.
		const FVector Offset = FVector(Layout.X, Layout.Y, Layout.Z) * Size + FVector(0.f, 0.f, Drift * (Index % 3));
		Puff->SetRelativeLocation(Offset);
		Puff->SetRelativeScale3D(FVector(Size * Layout.W * 2.f / 100.f));
		if (PuffMaterials.IsValidIndex(Index) && PuffMaterials[Index])
		{
			PuffMaterials[Index]->SetScalarParameterValue(
				TEXT("Opacity"), PuffOpacity * Fade * FMath::Min(1.f, Grow * 2.f));
		}
	}
}
