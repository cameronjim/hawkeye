#include "Vfx/HawkeyeVfxSubsystem.h"

#include "Hawkeye.h"
#include "World/HawkeyeResidentAssets.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/PlatformTime.h"
#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const FName UHawkeyeVfxSubsystem::SmokeCloudEvent(TEXT("smoke cloud"));
const FName UHawkeyeVfxSubsystem::EmpPulseEvent(TEXT("emp pulse"));
const FName UHawkeyeVfxSubsystem::ExplosionEvent(TEXT("explosion"));
const FName UHawkeyeVfxSubsystem::ScorchEvent(TEXT("scorch"));
const FName UHawkeyeVfxSubsystem::PuttySplatEvent(TEXT("putty splat"));
const FName UHawkeyeVfxSubsystem::TrickEffectEvent(TEXT("trick arrow effect"));
const FName UHawkeyeVfxSubsystem::ArrowTrailEvent(TEXT("arrow trail"));
const FName UHawkeyeVfxSubsystem::ArrowImpactEvent(TEXT("arrow impact"));
const FName UHawkeyeVfxSubsystem::HitSparkEvent(TEXT("hit spark"));
const FName UHawkeyeVfxSubsystem::MeleeSparkEvent(TEXT("melee spark"));
const FName UHawkeyeVfxSubsystem::KnockdownDustEvent(TEXT("knockdown dust"));
const FName UHawkeyeVfxSubsystem::ParryEvent(TEXT("parry ring"));
const FName UHawkeyeVfxSubsystem::BowReleaseEvent(TEXT("bow release"));
const FName UHawkeyeVfxSubsystem::ZipLineEvent(TEXT("zip line"));
const FName UHawkeyeVfxSubsystem::AnchorSparksEvent(TEXT("anchor sparks"));
const FName UHawkeyeVfxSubsystem::MuzzleFlashEvent(TEXT("muzzle flash"));
const FName UHawkeyeVfxSubsystem::TracerEvent(TEXT("tracer"));
const FName UHawkeyeVfxSubsystem::FootstepEvent(TEXT("footstep snow"));
const FName UHawkeyeVfxSubsystem::LandingEvent(TEXT("landing snow"));
const FName UHawkeyeVfxSubsystem::SnowfallEvent(TEXT("snowfall"));
const FName UHawkeyeVfxSubsystem::ChimneyEvent(TEXT("chimney wisp"));
const FName UHawkeyeVfxSubsystem::ScreenPulseEvent(TEXT("screen pulse"));

UHawkeyeVfxSubsystem* UHawkeyeVfxSubsystem::Find(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UHawkeyeVfxSubsystem>() : nullptr;
}

UNiagaraSystem* UHawkeyeVfxSubsystem::Resolve(const TSoftObjectPtr<UNiagaraSystem>& System)
{
	if (System.IsNull())
	{
		return nullptr;
	}
	UNiagaraSystem* Loaded = System.Get();
	if (!Loaded)
	{
		Loaded = System.LoadSynchronous();
		if (!Loaded)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: effect %s is missing; run Tools\\create-content.ps1."), *GetName(),
				*System.ToString());
			return nullptr;
		}
	}
	Resident.AddUnique(Loaded);
	return Loaded;
}

void UHawkeyeVfxSubsystem::PreloadFolderAsync(const FString& Path)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ Path }, /*bForceRescan=*/false);
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = false;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	TArray<FSoftObjectPath> Paths;
	for (const FAssetData& Asset : Assets)
	{
		Paths.Add(Asset.GetSoftObjectPath());
	}
	if (Paths.IsEmpty())
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	TWeakObjectPtr<UHawkeyeVfxSubsystem> WeakThis(this);
	PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths),
		FStreamableDelegateWithHandle::CreateLambda([WeakThis, Start, Path](TSharedPtr<FStreamableHandle> Handle)
		{
			UHawkeyeVfxSubsystem* This = WeakThis.Get();
			if (!This || !Handle.IsValid())
			{
				return;
			}
			TArray<UObject*> Loaded;
			Handle->GetLoadedAssets(Loaded);
			for (UObject* Object : Loaded)
			{
				if (UNiagaraSystem* Asset = Cast<UNiagaraSystem>(Object))
				{
					This->Resident.AddUnique(Asset);
					if (UHawkeyeResidentAssets* Kept = UHawkeyeResidentAssets::Get(This))
					{
						Kept->Keep(Asset);
					}
				}
			}
			UE_LOG(LogHawkeye, Log, TEXT("%s: %d effect(s) under %s streamed in %.0f ms."), *This->GetName(), Loaded.Num(), *Path,
				(FPlatformTime::Seconds() - Start) * 1000.0);
			This->PreloadHandle.Reset();
		}), FStreamableManager::DefaultAsyncLoadPriority, /*bManageActiveHandle=*/false, /*bStartStalled=*/false,
		TEXT("HawkeyePreload"));
}

void UHawkeyeVfxSubsystem::Note(FName Event, const UObject* Asset, const UObject* Context, const FVector& Location,
	bool bSpawned)
{
	++TotalRequests;
	RequestCounts.FindOrAdd(Event)++;
	SpawnCount += bSpawned ? 1 : 0;
	UE_LOG(LogHawkeye, Verbose, TEXT("Vfx: %s %s at %s (%s, #%d)%s."), *Event.ToString(), *GetNameSafe(Asset),
		*Location.ToCompactString(), *GetNameSafe(Context), TotalRequests, bSpawned ? TEXT("") : TEXT(", nothing drawn"));
	OnRequested.Broadcast(Event, Location);
}

void UHawkeyeVfxSubsystem::NoteRequest(const UObject* WorldContext, FName Event, const FVector& Location)
{
	if (UHawkeyeVfxSubsystem* Vfx = Find(WorldContext))
	{
		Vfx->Note(Event, nullptr, WorldContext, Location, false);
	}
}

UNiagaraComponent* UHawkeyeVfxSubsystem::SpawnAt(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System,
	const FVector& Location, const FRotator& Rotation, FName Event, float Scale)
{
	UHawkeyeVfxSubsystem* Vfx = Find(WorldContext);
	if (!Vfx)
	{
		return nullptr;
	}
	UNiagaraSystem* Resolved = Vfx->Resolve(System);
	UNiagaraComponent* Spawned = Resolved
		? UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Resolved, Location, Rotation, FVector(Scale),
			/*bAutoDestroy=*/true, /*bAutoActivate=*/true, ENCPoolMethod::AutoRelease, /*bPreCullCheck=*/true)
		: nullptr;
	Vfx->Note(Event, Resolved, WorldContext, Location, Spawned != nullptr);
	return Spawned;
}

UNiagaraComponent* UHawkeyeVfxSubsystem::SpawnAttached(const TSoftObjectPtr<UNiagaraSystem>& System,
	USceneComponent* AttachTo, FName Event, const FVector& Offset)
{
	UHawkeyeVfxSubsystem* Vfx = AttachTo ? Find(AttachTo) : nullptr;
	if (!Vfx)
	{
		return nullptr;
	}
	UNiagaraSystem* Resolved = Vfx->Resolve(System);
	UNiagaraComponent* Spawned = Resolved
		? UNiagaraFunctionLibrary::SpawnSystemAttached(Resolved, AttachTo, NAME_None, Offset, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/true, /*bAutoActivate=*/true, ENCPoolMethod::None,
			/*bPreCullCheck=*/false)
		: nullptr;
	Vfx->Note(Event, Resolved, AttachTo->GetOwner(), AttachTo->GetComponentLocation(), Spawned != nullptr);
	return Spawned;
}

UNiagaraComponent* UHawkeyeVfxSubsystem::SpawnKept(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System,
	const FVector& Location, FName Event)
{
	UHawkeyeVfxSubsystem* Vfx = Find(WorldContext);
	if (!Vfx)
	{
		return nullptr;
	}
	UNiagaraSystem* Resolved = Vfx->Resolve(System);
	UNiagaraComponent* Spawned = Resolved
		? UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Resolved, Location, FRotator::ZeroRotator,
			FVector::OneVector, /*bAutoDestroy=*/false, /*bAutoActivate=*/true, ENCPoolMethod::None,
			/*bPreCullCheck=*/false)
		: nullptr;
	Vfx->Note(Event, Resolved, WorldContext, Location, Spawned != nullptr);
	return Spawned;
}

UDecalComponent* UHawkeyeVfxSubsystem::SpawnDecal(const UObject* WorldContext,
	const TSoftObjectPtr<UMaterialInterface>& Material, const FVector& Location, const FRotator& Rotation, float Radius,
	float LifeSeconds, float FadeSeconds, FName Event, float DepthCm)
{
	UHawkeyeVfxSubsystem* Vfx = Find(WorldContext);
	if (!Vfx)
	{
		return nullptr;
	}
	UMaterialInterface* Resolved = nullptr;
	if (!Material.IsNull())
	{
		Resolved = Material.Get() ? Material.Get() : Material.LoadSynchronous();
		if (Resolved)
		{
			Vfx->Resident.AddUnique(Resolved);
		}
	}
	UDecalComponent* Decal = Resolved
		? UGameplayStatics::SpawnDecalAtLocation(WorldContext, Resolved, FVector(DepthCm, Radius, Radius), Location,
			Rotation, LifeSeconds)
		: nullptr;
	if (Decal && FadeSeconds > 0.f)
	{
		Decal->SetFadeOut(FMath::Max(LifeSeconds - FadeSeconds, 0.f), FadeSeconds, /*DestroyOwnerAfterFade=*/false);
	}
	Vfx->Note(Event, Resolved, WorldContext, Location, Decal != nullptr);
	return Decal;
}

void UHawkeyeVfxSubsystem::Release(TObjectPtr<UNiagaraComponent>& Component)
{
	if (IsValid(Component))
	{
		Component->SetAutoDestroy(true);
		Component->Deactivate();
	}
	Component = nullptr;
}

void UHawkeyeVfxSubsystem::Kill(TObjectPtr<UNiagaraComponent>& Component)
{
	if (IsValid(Component))
	{
		Component->DestroyComponent();
	}
	Component = nullptr;
}
