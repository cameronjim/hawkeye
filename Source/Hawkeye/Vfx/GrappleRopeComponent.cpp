#include "Vfx/GrappleRopeComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

const FLinearColor UGrappleRopeComponent::RopeColor = FLinearColor(0.045f, 0.047f, 0.052f);
const TCHAR* const UGrappleRopeComponent::MaterialPath = TEXT("/Game/VFX/Materials/M_GrappleRope.M_GrappleRope");

UGrappleRopeComponent::UGrappleRopeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	// The pieces are world transforms: nothing the component is attached to may move them.
	SetAbsolute(true, true, true);
	// A rope that is a few centimetres across must not be culled with its (stale) bounds when it swings.
	bNeverDistanceCull = true;
	SetVisibility(false);
}

bool UGrappleRopeComponent::EnsureLook()
{
	if (GetStaticMesh())
	{
		return true;
	}
	if (bLookTried)
	{
		return false;
	}
	bLookTried = true;
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!Cylinder)
	{
		return false;
	}
	SetStaticMesh(Cylinder);

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (!Material)
	{
		// The look pass has not been run: the engine's shape material, tinted the same grey.
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Material)
		{
			if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Material, this))
			{
				Tint->SetVectorParameterValue(TEXT("Color"), RopeColor);
				Material = Tint;
			}
		}
	}
	if (Material)
	{
		SetMaterial(0, Material);
	}
	return true;
}

void UGrappleRopeComponent::SetPoints(const TArray<FVector>& Points, float ThicknessCm)
{
	if (Points.Num() < 2 || !EnsureLook())
	{
		Hide();
		return;
	}
	const int32 Pieces = Points.Num() - 1;
	// The engine cylinder is 100 cm high along Z and 100 cm across.
	const float Girth = FMath::Max(ThicknessCm, 0.1f) / 100.f;

	Scratch.Reset(Pieces);
	for (int32 Index = 0; Index < Pieces; ++Index)
	{
		const FVector& From = Points[Index];
		const FVector& To = Points[Index + 1];
		const FVector Span = To - From;
		// A hair longer than the piece so the corners of a bending rope overlap instead of opening.
		const float Length = FMath::Max(static_cast<float>(Span.Size()) + ThicknessCm * 0.5f, 0.1f);
		const FQuat Turn = Span.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Span).ToQuat();
		Scratch.Add(FTransform(Turn, (From + To) * 0.5f, FVector(Girth, Girth, Length / 100.f)));
	}

	if (Pieces != PieceCount || GetInstanceCount() != Pieces)
	{
		ClearInstances();
		for (int32 Index = 0; Index < Pieces; ++Index)
		{
			AddInstance(Scratch[Index], /*bWorldSpace=*/true);
		}
		PieceCount = Pieces;
	}
	else
	{
		BatchUpdateInstancesTransforms(0, Scratch, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
	if (!bShown)
	{
		bShown = true;
		SetVisibility(true);
	}
}

void UGrappleRopeComponent::Hide()
{
	if (bShown)
	{
		bShown = false;
		SetVisibility(false);
	}
}
