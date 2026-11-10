#include "World/FireEscapeLanding.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"

namespace HawkeyeFireEscape
{
	/** The visible bars, cm. */
	static constexpr float TopRail = 5.f;
	static constexpr float MidRail = 3.f;
	static constexpr float Post = 5.f;
	static constexpr float LadderRail = 4.f;
	static constexpr float Rung = 3.f;
	static constexpr float RungSpacing = 30.f;
	/** Ladder: its centre this far in from the landing's end, this far out from the facade, this wide. */
	static constexpr float LadderInFromEnd = 35.f;
	static constexpr float LadderOut = 30.f;
	static constexpr float LadderWidth = 40.f;
	/** The traversable block stands this far proud of the rail, as the roof ledges do of the facade. */
	static constexpr float LedgeOutset = 2.f;
	static constexpr int32 BoxCount = 4;

	/** A unit-cube instance filling Box (in the landing frame) placed by Frame. */
	static FTransform CubeFor(const FTransform& Frame, const FBox& Box)
	{
		const FTransform Local(FQuat::Identity, Box.GetCenter(), Box.GetSize() / 100.f);
		return Local * Frame;
	}

	/** A unit-cylinder instance (axis Z, 100 x 100) from A to B, Diameter across, in the landing frame. */
	static FTransform CylinderFor(const FTransform& Frame, const FVector& A, const FVector& B, float Diameter)
	{
		const FVector Axis = B - A;
		const FQuat Turn = FQuat::FindBetweenNormals(FVector::UpVector, Axis.GetSafeNormal());
		const FTransform Local(Turn, (A + B) * 0.5f, FVector(Diameter / 100.f, Diameter / 100.f, Axis.Size() / 100.f));
		return Local * Frame;
	}

	static FTransform FrameOf(const FCityFireEscapeRecord& Record)
	{
		return FTransform(Record.Transform.GetRotation(), Record.Transform.GetLocation());
	}
}

AFireEscapeLanding::AFireEscapeLanding()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	static const TCHAR* Names[HawkeyeFireEscape::BoxCount] = { TEXT("Slab"), TEXT("OuterRail"), TEXT("EndRailA"), TEXT("EndRailB") };
	for (int32 Index = 0; Index < HawkeyeFireEscape::BoxCount; ++Index)
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(Names[Index]);
		Box->SetupAttachment(Root);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Box->SetCanEverAffectNavigation(false);
		Box->SetGenerateOverlapEvents(false);
		Box->SetHiddenInGame(true);
		Boxes.Add(Box);
	}
	ApplyRecord(Record);
}

void AFireEscapeLanding::ApplyRecord(const FCityFireEscapeRecord& InRecord)
{
	Record = InRecord;
	TArray<FBox> Local;
	BuildCollisionBoxes(Record, Local);
	for (int32 Index = 0; Index < Boxes.Num() && Index < Local.Num(); ++Index)
	{
		Boxes[Index]->SetRelativeLocation(Local[Index].GetCenter());
		Boxes[Index]->SetBoxExtent(Local[Index].GetExtent());
	}
}

void AFireEscapeLanding::BuildCollisionBoxes(const FCityFireEscapeRecord& Record, TArray<FBox>& OutLocalBoxes)
{
	const float HalfLength = Record.SlabSize.X * 0.5f;
	const float Inner = Record.FacadeGap;
	const float Outer = Record.FacadeGap + Record.SlabSize.Y;
	const float T = Record.RailThickness;
	const float H = Record.RailHeight;
	OutLocalBoxes.Reset();
	OutLocalBoxes.Add(FBox(FVector(-HalfLength, Inner, -Record.SlabSize.Z), FVector(HalfLength, Outer, 0.f)));
	OutLocalBoxes.Add(FBox(FVector(-HalfLength, Outer - T, 0.f), FVector(HalfLength, Outer, H)));
	OutLocalBoxes.Add(FBox(FVector(-HalfLength, Inner, 0.f), FVector(-HalfLength + T, Outer, H)));
	OutLocalBoxes.Add(FBox(FVector(HalfLength - T, Inner, 0.f), FVector(HalfLength, Outer, H)));
}

void AFireEscapeLanding::BuildParts(const FCityFireEscapeRecord& Record, TArray<FTransform>& OutCubes, TArray<FTransform>& OutCylinders)
{
	using namespace HawkeyeFireEscape;
	const FTransform Frame = FrameOf(Record);
	const float L = Record.SlabSize.X * 0.5f;
	const float Inner = Record.FacadeGap;
	const float Outer = Record.FacadeGap + Record.SlabSize.Y;
	const float H = Record.RailHeight;

	// Slab.
	OutCubes.Add(CubeFor(Frame, FBox(FVector(-L, Inner, -Record.SlabSize.Z), FVector(L, Outer, 0.f))));
	// Top and middle rails on the outer side and both ends.
	for (const float Z : { H - TopRail * 0.5f, H * 0.5f })
	{
		const float S = Z > H * 0.75f ? TopRail : MidRail;
		OutCubes.Add(CubeFor(Frame, FBox(FVector(-L, Outer - S, Z - S * 0.5f), FVector(L, Outer, Z + S * 0.5f))));
		OutCubes.Add(CubeFor(Frame, FBox(FVector(-L, Inner, Z - S * 0.5f), FVector(-L + S, Outer, Z + S * 0.5f))));
		OutCubes.Add(CubeFor(Frame, FBox(FVector(L - S, Inner, Z - S * 0.5f), FVector(L, Outer, Z + S * 0.5f))));
	}
	// Posts: the two outer corners, the middle of the outer rail, and where the ends meet the facade.
	for (const FVector2D& At : { FVector2D(-L, Outer), FVector2D(L, Outer), FVector2D(0.f, Outer), FVector2D(-L, Inner), FVector2D(L, Inner) })
	{
		const float X0 = FMath::Clamp(At.X - Post * 0.5f, -L, L - Post);
		const float Y0 = FMath::Clamp(At.Y - Post * 0.5f, Inner, Outer - Post);
		OutCubes.Add(CubeFor(Frame, FBox(FVector(X0, Y0, 0.f), FVector(X0 + Post, Y0 + Post, H))));
	}

	// The ladder down to the landing below, at the LadderSide end.
	if (Record.LadderDrop > Record.SlabSize.Z)
	{
		const float Side = Record.LadderSide >= 0.f ? 1.f : -1.f;
		const float CX = Side * (L - LadderInFromEnd);
		const float Y = Inner + LadderOut;
		const float Bottom = -Record.LadderDrop;
		const float Top = -Record.SlabSize.Z;
		for (const float DX : { -LadderWidth * 0.5f, LadderWidth * 0.5f })
		{
			OutCylinders.Add(CylinderFor(Frame, FVector(CX + DX, Y, Bottom), FVector(CX + DX, Y, Top), LadderRail));
		}
		for (float Z = Bottom + RungSpacing; Z < Top - 5.f; Z += RungSpacing)
		{
			OutCylinders.Add(CylinderFor(Frame, FVector(CX - LadderWidth * 0.5f, Y, Z), FVector(CX + LadderWidth * 0.5f, Y, Z), Rung));
		}
	}
}

FTransform AFireEscapeLanding::ComputeLedgeTransform(const FCityFireEscapeRecord& Record)
{
	using namespace HawkeyeFireEscape;
	// The block's local X runs along the landing's -X and its local -Y points out of the facade,
	// so it is the landing frame turned 180 degrees, with its pivot corner at the +X end.
	const FTransform Frame = FrameOf(Record);
	const float L = Record.SlabSize.X * 0.5f;
	const float Outer = Record.FacadeGap + Record.SlabSize.Y + LedgeOutset;
	const float Height = Record.RailHeight + Record.SlabSize.Z;
	const float Depth = Record.RailThickness + LedgeOutset;
	const FTransform Local(FRotator(0.f, 180.f, 0.f), FVector(L, Outer, -Record.SlabSize.Z),
		FVector(Record.SlabSize.X / 100.f, Depth / 100.f, Height / 100.f));
	return Local * Frame;
}

void AFireEscapeLanding::ComputeOuterRailTop(const FCityFireEscapeRecord& Record, FVector& OutA, FVector& OutB)
{
	const FTransform Frame = HawkeyeFireEscape::FrameOf(Record);
	const float L = Record.SlabSize.X * 0.5f;
	const float Outer = Record.FacadeGap + Record.SlabSize.Y;
	OutA = Frame.TransformPosition(FVector(L, Outer, Record.RailHeight));
	OutB = Frame.TransformPosition(FVector(-L, Outer, Record.RailHeight));
}
