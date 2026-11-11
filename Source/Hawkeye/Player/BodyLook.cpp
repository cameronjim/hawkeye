#include "Player/BodyLook.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Hawkeye.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace HawkeyeBodyLook
{
	const FName ComponentName(TEXT("BodyLook"));

	/** The object a soft reference names: already in memory, else loaded when its package is on disk. */
	template <typename T>
	static T* LoadIfOnDisk(const TSoftObjectPtr<T>& Soft)
	{
		if (T* Loaded = Soft.Get())
		{
			return Loaded;
		}
		const FString Package = Soft.ToSoftObjectPath().GetLongPackageName();
		// Checked first: loading a missing package logs a warning, and a clone without the content is normal.
		return !Package.IsEmpty() && FPackageName::DoesPackageExist(Package) ? Soft.LoadSynchronous() : nullptr;
	}

	static UClass* LoadClassIfOnDisk(const TSoftClassPtr<UAnimInstance>& Soft)
	{
		if (UClass* Loaded = Soft.Get())
		{
			return Loaded;
		}
		const FString Package = Soft.ToSoftObjectPath().GetLongPackageName();
		return !Package.IsEmpty() && FPackageName::DoesPackageExist(Package) ? Soft.LoadSynchronous() : nullptr;
	}

	bool Resolve(const FHawkeyeBodyLook& Look, USkeletalMesh*& OutMesh, UClass*& OutAnimClass)
	{
		OutMesh = Look.IsSet() ? LoadIfOnDisk(Look.Mesh) : nullptr;
		OutAnimClass = OutMesh && !Look.AnimClass.IsNull() ? LoadClassIfOnDisk(Look.AnimClass) : nullptr;
		if (!OutMesh || !OutAnimClass)
		{
			OutMesh = nullptr;
			OutAnimClass = nullptr;
			return false;
		}
		return true;
	}

	static void ApplyMaterials(USkeletalMeshComponent& Visible, const FHawkeyeBodyLook& Look)
	{
		for (int32 Slot = 0; Slot < Look.Materials.Num(); ++Slot)
		{
			if (UMaterialInterface* Material =
					Look.Materials[Slot].IsNull() ? nullptr : LoadIfOnDisk(Look.Materials[Slot]))
			{
				Visible.SetMaterial(Slot, Material);
			}
		}
	}

	USkeletalMeshComponent* Apply(USkeletalMeshComponent& Driver, const FHawkeyeBodyLook& Look)
	{
		AActor* Owner = Driver.GetOwner();
		USkeletalMesh* Mesh = nullptr;
		UClass* AnimClass = nullptr;
		if (!Owner || !Resolve(Look, Mesh, AnimClass))
		{
			if (Look.IsSet())
			{
				UE_LOG(LogHawkeye, Display,
					TEXT("%s: body look %s (AnimBP %s) is not on disk; the %s mesh stays visible. "
						 "Install its pack and run Tools\\create-content.ps1 (Tools\\import-anims.ps1 for Paragon) for "
						 "the intended look (CLAUDE.md, one-time setup)."),
					*GetNameSafe(Owner), *Look.Mesh.ToString(), *Look.AnimClass.ToString(),
					*GetNameSafe(Driver.GetSkeletalMeshAsset()));
			}
			return nullptr;
		}

		USkeletalMeshComponent* Visible = NewObject<USkeletalMeshComponent>(Owner, ComponentName);
		Visible->SetupAttachment(&Driver);
		Visible->SetSkeletalMeshAsset(Mesh);
		Visible->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Visible->SetAnimInstanceClass(AnimClass);
		Visible->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visible->SetGenerateOverlapEvents(false);
		Visible->SetCanEverAffectNavigation(false);
		Visible->SetCastShadow(true);
		Visible->bCastDynamicShadow = true;
		Visible->SetOwnerNoSee(Driver.bOwnerNoSee);
		// Arrows embed on its bones and the camera can hide it from the owner while its shadow stays.
		Visible->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// Retarget Pose From Mesh reads the driver's finished pose, so the driver ticks first.
		Visible->AddTickPrerequisiteComponent(&Driver);
		if (!Mesh->GetPhysicsAsset())
		{
			// A look with no bodies of its own never simulates: the driver's bodies take the hit reactions and the
			// ragdoll, and the driver's physics is blended into its pose after the physics step. Ticking after it
			// is what makes the look follow a ragdoll rather than the clip under it.
			Visible->SetTickGroup(TG_PostPhysics);
		}
		ApplyMaterials(*Visible, Look);
		Visible->RegisterComponent();
		Owner->AddInstanceComponent(Visible);
		for (const FName Bone : Look.HiddenBones)
		{
			if (Visible->GetBoneIndex(Bone) != INDEX_NONE)
			{
				Visible->HideBoneByName(Bone, PBO_None);
			}
		}

		// The driver keeps ticking and refreshing its bones (montages, notifies, IK, sockets) but is not drawn;
		// a hidden component casts no shadow, so there is one shadow, the look's.
		Driver.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Driver.SetVisibility(false, /*bPropagateToChildren=*/false);

		UE_LOG(LogHawkeye, Log,
			TEXT("%s: body look %s (AnimBP %s, %d material overrides, %d bones hidden) over driver %s."),
			*GetNameSafe(Owner), *GetNameSafe(Mesh), *GetNameSafe(AnimClass), Look.Materials.Num(),
			Look.HiddenBones.Num(), *GetNameSafe(Driver.GetSkeletalMeshAsset()));
		return Visible;
	}
}
