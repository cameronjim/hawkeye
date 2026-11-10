#include "Vfx/SnowfallComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "HawkeyeGameMode.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "Vfx/HawkeyeVfxMath.h"
#include "Vfx/HawkeyeVfxSubsystem.h"

USnowfallComponent::USnowfallComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the camera has moved this frame, so the snow is never a frame behind it.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void USnowfallComponent::BeginPlay()
{
	Super::BeginPlay();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || SnowSystem.IsNull() || !ShouldSnowIn(GetWorld()))
	{
		SetComponentTickEnabled(false);
		return;
	}
	Snow = UHawkeyeVfxSubsystem::SpawnKept(this, SnowSystem, GetOwner()->GetActorLocation(),
		UHawkeyeVfxSubsystem::SnowfallEvent);
	SetComponentTickEnabled(Snow != nullptr);
}

bool USnowfallComponent::ShouldSnowIn(const UWorld* World)
{
	return AHawkeyeGameMode::WantsOutdoorWeather(World);
}

void USnowfallComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UHawkeyeVfxSubsystem::Kill(Snow);
	Super::EndPlay(EndPlayReason);
}

void USnowfallComponent::FollowCamera(const FVector& Location, const FVector& Forward, float DeltaSeconds)
{
	const FVector Velocity = (bHasLastCamera && DeltaSeconds > KINDA_SMALL_NUMBER)
		? (Location - LastCamera) / DeltaSeconds
		: FVector::ZeroVector;
	LastCamera = Location;
	bHasLastCamera = true;
	if (IsValid(Snow))
	{
		Snow->SetWorldLocation(HawkeyeVfxMath::ComputeSnowfallOrigin(Location, Forward, Velocity, AheadCm, HeightCm,
			LeadSeconds));
	}
}

void USnowfallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	if (Camera)
	{
		FollowCamera(Camera->GetCameraLocation(), Camera->GetCameraRotation().Vector(), DeltaTime);
	}
}
