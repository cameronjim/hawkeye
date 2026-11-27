#include "Demo/DemoStart.h"

#include "Hawkeye.h"
#include "Boss/BossDefinition.h"
#include "Boss/BossFightSubsystem.h"
#include "Boss/BossRules.h"
#include "Chase/ChaseSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Parse.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/HawkeyeHudWidget.h"
#include "Combat/BowComponent.h"
#include "BrainComponent.h"
#include "TimerManager.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const TCHAR* const FHawkeyeDemoStart::SwitchName = TEXT("HawkeyeDemoStart");
const FName FHawkeyeDemoStart::StreetGroupTag(TEXT("StreetGroup"));

namespace HawkeyeDemoStartLocal
{
	static float HalfHeightOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	}

	static float RadiusOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 35.f;
	}

	static bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || !Thug->GetHealthComponent() || Thug->GetHealthComponent()->IsDead();
	}

	/** The ground is level with the door: the sidewalk or the road beside it, not a roof or a car's top. */
	static constexpr float LevelWithDoor = 60.f;
}

// --- The command line ----------------------------------------------------------------------------------------

const TCHAR* FHawkeyeDemoStart::ClipName(EHawkeyeDemoClip Clip)
{
	switch (Clip)
	{
	case EHawkeyeDemoClip::StreetFight:
		return TEXT("street_fight");
	case EHawkeyeDemoClip::StealthTakedown:
		return TEXT("stealth_takedown");
	case EHawkeyeDemoClip::TomasPhase:
		return TEXT("tomas_phase");
	default:
		return TEXT("none");
	}
}

EHawkeyeDemoClip FHawkeyeDemoStart::ParseClip(const FString& Name)
{
	const FString Wanted = Name.TrimStartAndEnd().ToLower().Replace(TEXT("-"), TEXT("_"));
	for (const EHawkeyeDemoClip Clip : { EHawkeyeDemoClip::StreetFight, EHawkeyeDemoClip::StealthTakedown, EHawkeyeDemoClip::TomasPhase })
	{
		if (Wanted == ClipName(Clip))
		{
			return Clip;
		}
	}
	return EHawkeyeDemoClip::None;
}

EHawkeyeDemoClip FHawkeyeDemoStart::ClipFromCommandLine(const TCHAR* CommandLine, FString* OutAsked)
{
	FString Asked;
	if (CommandLine)
	{
		FParse::Value(CommandLine, *FString::Printf(TEXT("%s="), SwitchName), Asked);
	}
	if (OutAsked)
	{
		*OutAsked = Asked;
	}
	return Asked.IsEmpty() ? EHawkeyeDemoClip::None : ParseClip(Asked);
}

// --- Geometry ------------------------------------------------------------------------------------------------

float FHawkeyeDemoStart::YawFacing(const FVector& From, const FVector& To)
{
	return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
}

FVector FHawkeyeDemoStart::StreetFightSpot(const FVector& PairMid, const FVector& HeavyAt)
{
	const FVector Toward = (HeavyAt - PairMid).GetSafeNormal2D();
	return FVector(PairMid.X, PairMid.Y, PairMid.Z) + Toward * StreetFightDistance;
}

bool FHawkeyeDemoStart::IsInsideBox2D(const FVector& Point, const FVector& Centre, const FVector& HalfExtent, float Margin)
{
	return FMath::Abs(Point.X - Centre.X) <= HalfExtent.X + Margin && FMath::Abs(Point.Y - Centre.Y) <= HalfExtent.Y + Margin;
}

TArray<FVector> FHawkeyeDemoStart::DoorApproachCandidates(const FVector& DoorFloor, float OutYaw, const FVector& VolumeCentre,
	const FVector& VolumeHalfExtent)
{
	static const float Offsets[] = { 0.f, 15.f, -15.f, 30.f, -30.f, 45.f, -45.f, 60.f, -60.f };
	TArray<FVector> Out;
	for (float Distance = DoorApproachMin; Distance <= DoorApproachMax + UE_KINDA_SMALL_NUMBER; Distance += DoorApproachStep)
	{
		for (const float Offset : Offsets)
		{
			const FVector Spot = DoorFloor + FRotator(0.f, OutYaw + Offset, 0.f).Vector() * Distance;
			if (!IsInsideBox2D(Spot, VolumeCentre, VolumeHalfExtent, VolumeMargin))
			{
				Out.Add(FVector(Spot.X, Spot.Y, DoorFloor.Z));
			}
		}
	}
	return Out;
}

TArray<FVector> FHawkeyeDemoStart::RoofEntryCandidates(const FVector& Anchor, const FVector& Centre)
{
	const FVector Inward = (Centre - Anchor).GetSafeNormal2D();
	TArray<FVector> Out;
	for (float Distance = RoofEntryMin; Distance <= RoofEntryMax + UE_KINDA_SMALL_NUMBER; Distance += RoofEntryStep)
	{
		const FVector Spot = Anchor + Inward * Distance;
		Out.Add(FVector(Spot.X, Spot.Y, Anchor.Z));
	}
	return Out;
}

// --- The world -----------------------------------------------------------------------------------------------

bool FHawkeyeDemoStart::FindGround(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& OutGround,
	AActor** OutActor)
{
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoStartGround), false, Ignore);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, -5000.f), ECC_Visibility, Params))
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	if (OutActor)
	{
		*OutActor = Hit.GetActor();
	}
	return true;
}

bool FHawkeyeDemoStart::CapsuleIsFree(UWorld* World, const APawn* Pawn, const FVector& Feet)
{
	if (!World)
	{
		return false;
	}
	const float HalfHeight = HawkeyeDemoStartLocal::HalfHeightOf(Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoStartCapsule), false, Pawn);
	return !World->OverlapBlockingTestByChannel(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(HawkeyeDemoStartLocal::RadiusOf(Pawn), HalfHeight), Params);
}

bool FHawkeyeDemoStart::PlanStreetFight(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why)
{
	if (!World)
	{
		Why = TEXT("no world");
		return false;
	}
	const AThugCharacter* Heavy = nullptr;
	FVector PairMid = FVector::ZeroVector;
	int32 PairCount = 0;
	int32 Found = 0;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (It->ActorHasTag(StreetGroupTag) && !HawkeyeDemoStartLocal::IsDown(*It))
		{
			++Found;
			if (It->IsHeavy())
			{
				Heavy = *It;
			}
			else
			{
				PairMid += It->GetActorLocation();
				++PairCount;
			}
		}
	}
	if (Found != 3 || !Heavy || PairCount != 2)
	{
		Why = FString::Printf(TEXT("expected the three StreetGroup thugs (bat, gunner, heavy), found %d"), Found);
		return false;
	}
	PairMid /= PairCount;
	// On the sidewalk toward the heavy: the pair first, the heavy some seconds later.
	const FVector Spot = StreetFightSpot(PairMid, Heavy->GetActorLocation());
	FVector Ground;
	if (!FindGround(World, Spot, PairMid.Z + 300.f, Ignore, Ground))
	{
		Why = TEXT("no sidewalk between the pair and the heavy");
		return false;
	}
	Out = FHawkeyeDemoStand();
	Out.Feet = Ground;
	Out.Face = PairMid;
	Out.Yaw = YawFacing(Ground, PairMid);
	Out.Where = FString::Printf(TEXT("on the Avenue A sidewalk %.0f m from the bat and the gunner, the heavy %.0f m behind, facing the pair"),
		FVector::Dist2D(PairMid, Ground) / 100.f, FVector::Dist2D(Heavy->GetActorLocation(), Ground) / 100.f);
	return true;
}

bool FHawkeyeDemoStart::PlanStealthTakedown(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why)
{
	const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
	const UChapterSiteDefinition* Definition = Site ? Site->GetDefinition() : nullptr;
	if (!Definition)
	{
		Why = TEXT("no chapter site is running (start the chapter first: -HawkeyeChapter=DA_CH03_Grotto)");
		return false;
	}
	if (Definition->InteriorDoors.Num() == 0)
	{
		Why = FString::Printf(TEXT("site %s has no door into an interior"), *Definition->GetName());
		return false;
	}
	const FChapterSiteInteriorDoor& Door = Definition->InteriorDoors[0];
	// The arrival volume the current objective waits on (find_sleigh): stay outside it so that walking forward enters it.
	FVector VolumeCentre = Door.Location;
	FVector VolumeHalf = FVector::ZeroVector;
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	for (const FChapterSiteVolume& Volume : Definition->Volumes)
	{
		if (Current && Volume.ObjectiveId == Current->ObjectiveId)
		{
			VolumeCentre = Volume.Location;
			VolumeHalf = Volume.Extent;
			break;
		}
	}
	return PlanDoorStand(World, Ignore, Door, VolumeCentre, VolumeHalf, Out, Why);
}

bool FHawkeyeDemoStart::PlanDoorStand(UWorld* World, const AActor* Ignore, const FChapterSiteInteriorDoor& Door, const FVector& VolumeCentre,
	const FVector& VolumeHalfExtent, FHawkeyeDemoStand& Out, FString& Why)
{
	const APawn* Pawn = Cast<APawn>(Ignore);
	const FVector OutOfDoor = FRotator(0.f, Door.Yaw, 0.f).Vector();
	const FVector DoorEye = Door.Location + OutOfDoor * 60.f + FVector(0.f, 0.f, 120.f);
	for (const FVector& Candidate : DoorApproachCandidates(Door.Location, Door.Yaw, VolumeCentre, VolumeHalfExtent))
	{
		FVector Ground;
		if (!FindGround(World, Candidate, Door.Location.Z + 300.f, Ignore, Ground)
			|| FMath::Abs(Ground.Z - Door.Location.Z) > HawkeyeDemoStartLocal::LevelWithDoor
			|| (Pawn && !CapsuleIsFree(World, Pawn, Ground)))
		{
			continue;
		}
		// The door in view: nothing between her eyes and the step in front of it.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoStartDoorSight), false, Ignore);
		if (World->LineTraceTestByChannel(Ground + FVector(0.f, 0.f, 150.f), DoorEye, ECC_Visibility, Params))
		{
			continue;
		}
		Out = FHawkeyeDemoStand();
		Out.Feet = Ground;
		Out.Face = Door.Location + FVector(0.f, 0.f, 100.f);
		Out.Yaw = YawFacing(Ground, Door.Location);
		Out.Where = FString::Printf(TEXT("on the street %.1f m in front of the door \"%s\", facing it, %.1f m outside its arrival volume"),
			FVector::Dist2D(Ground, Door.Location) / 100.f, *Door.DisplayName.ToString(),
			FMath::Max(0.f, FMath::Max(FMath::Abs(Ground.X - VolumeCentre.X) - VolumeHalfExtent.X, FMath::Abs(Ground.Y - VolumeCentre.Y) - VolumeHalfExtent.Y)) / 100.f);
		return true;
	}
	Why = FString::Printf(TEXT("no free ground with the door in view between %.0f and %.0f m in front of it"), DoorApproachMin / 100.f,
		DoorApproachMax / 100.f);
	return false;
}

bool FHawkeyeDemoStart::PlanTomasPhase(UWorld* World, const AActor* Ignore, FHawkeyeDemoStand& Out, FString& Why, bool& bOnRoof)
{
	bOnRoof = false;
	const UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
	const UBossDefinition* Definition = Fight ? Fight->GetDefinition() : nullptr;
	if (!Definition)
	{
		Why = TEXT("no boss fight is set up (start the chapter first: -HawkeyeChapter=DA_CH04_SocialClub)");
		return false;
	}
	TArray<FVector> CrewAt;
	if (Definition->Phases.Num() > 0)
	{
		for (const FBossCrewMember& Member : Definition->Phases[0].Crew)
		{
			CrewAt.Add(Member.Location);
		}
	}
	return PlanRoofEntry(World, Ignore, Definition->Arena, CrewAt, Out, Why, bOnRoof);
}

bool FHawkeyeDemoStart::PlanRoofEntry(UWorld* World, const AActor* Ignore, const FBossArena& Arena, const TArray<FVector>& CrewAt,
	FHawkeyeDemoStand& Out, FString& Why, bool& bOnRoof)
{
	bOnRoof = false;
	const APawn* Pawn = Cast<APawn>(Ignore);
	FVector CrewMid = FVector::ZeroVector;
	for (const FVector& Member : CrewAt)
	{
		CrewMid += Member;
	}
	CrewMid = CrewAt.Num() > 0 ? CrewMid / CrewAt.Num() : Arena.Centre;

	// The roof, at the parapet anchor's side: the fight's own watch sees her feet on it and completes meet_tomas.
	if (Arena.Outline.Num() >= 3 && !Arena.ApproachAnchor.IsZero())
	{
		for (const FVector& Candidate : RoofEntryCandidates(Arena.ApproachAnchor, Arena.Centre))
		{
			FVector Ground;
			if (!FindGround(World, Candidate, Arena.RoofZ + 200.f, Ignore, Ground) || FMath::Abs(Ground.Z - Arena.RoofZ) > 40.f
				|| !UBossRules::IsOnRoof(Arena.Outline, Arena.RoofZ, Ground) || (Pawn && !CapsuleIsFree(World, Pawn, Ground)))
			{
				continue;
			}
			bool bClear = FVector::Dist2D(Ground, Arena.BossSpot) >= RoofCrewClearance;
			for (const FVector& Member : CrewAt)
			{
				bClear &= FVector::Dist2D(Ground, Member) >= RoofCrewClearance;
			}
			if (!bClear)
			{
				continue;
			}
			Out = FHawkeyeDemoStand();
			Out.Feet = Ground;
			Out.Face = CrewMid + FVector(0.f, 0.f, 90.f);
			Out.Yaw = YawFacing(Ground, CrewMid);
			Out.Where = FString::Printf(TEXT("on the social club roof %.1f m in from the parapet anchor, facing Tomas's crew %.1f m away"),
				FVector::Dist2D(Ground, Arena.ApproachAnchor) / 100.f, FVector::Dist2D(Ground, CrewMid) / 100.f);
			bOnRoof = true;
			return true;
		}
	}

	// No roof spot: the street stand under the anchor, looking up at it (the chapter's own opening spot).
	if (Arena.PlayerStart.IsZero())
	{
		Why = TEXT("the arena has no roof entry free and no street stand");
		return false;
	}
	FVector Ground = Arena.PlayerStart;
	FindGround(World, Arena.PlayerStart, Arena.PlayerStart.Z + 200.f, Ignore, Ground);
	Out = FHawkeyeDemoStand();
	Out.Feet = Ground;
	Out.Face = Arena.ApproachAnchor;
	Out.Yaw = YawFacing(Ground, Arena.ApproachAnchor);
	const FVector To = Arena.ApproachAnchor - (Ground + FVector(0.f, 0.f, 160.f));
	Out.Pitch = FMath::Clamp(To.Rotation().Pitch, -10.f, 40.f);
	Out.Where = FString::Printf(TEXT("on the street stand %.1f m from the roof's parapet anchor, facing it (grapple up with Q; no roof spot was free)"),
		FVector::Dist2D(Ground, Arena.ApproachAnchor) / 100.f);
	return true;
}

void FHawkeyeDemoStart::PutKate(APawn* Kate, const FHawkeyeDemoStand& Stand, bool bCutCamera)
{
	if (!Kate)
	{
		return;
	}
	const FRotator Facing(0.f, Stand.Yaw, 0.f);
	Kate->TeleportTo(Stand.Feet + FVector(0.f, 0.f, HawkeyeDemoStartLocal::HalfHeightOf(Kate) + 2.f), Facing, false, true);
	if (AController* Controller = Kate->GetController())
	{
		Controller->SetControlRotation(FRotator(Stand.Pitch, Stand.Yaw, 0.f));
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			PC->SetViewTarget(Kate);
		}
	}
	if (bCutCamera)
	{
		AHawkeyeCharacter::CutCameraFor(Kate);
	}
}

bool FHawkeyeDemoStart::Apply(UWorld* World, EHawkeyeDemoClip Clip, FString& OutWhat)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Kate = PC ? PC->GetPawn() : nullptr;
	if (!Kate)
	{
		OutWhat = TEXT("no player pawn to place");
		return false;
	}
	FHawkeyeDemoStand Stand;
	FString Why;
	bool bPlanned = false;
	switch (Clip)
	{
	case EHawkeyeDemoClip::StreetFight:
		bPlanned = PlanStreetFight(World, Kate, Stand, Why);
		break;
	case EHawkeyeDemoClip::StealthTakedown:
		bPlanned = PlanStealthTakedown(World, Kate, Stand, Why);
		break;
	case EHawkeyeDemoClip::TomasPhase:
	{
		bool bOnRoof = false;
		bPlanned = PlanTomasPhase(World, Kate, Stand, Why, bOnRoof);
		break;
	}
	default:
		Why = TEXT("no clip");
		break;
	}
	if (!bPlanned)
	{
		OutWhat = FString::Printf(TEXT("could not place Kate for %s: %s"), ClipName(Clip), *Why);
		return false;
	}

	FString Extra;
	if (Clip == EHawkeyeDemoClip::StreetFight)
	{
		// The district's own chapter opens on a chase: the thief runs off and losing him reloads the level. A street fight
		// clip is free roam, so the chase and its chapter are put away (a chapter named with -HawkeyeChapter is left alone).
		UChaseSubsystem* Chase = UChaseSubsystem::Get(World);
		if (Chase && Chase->IsChaseRunning())
		{
			Chase->StopChase();
			if (UMissionSubsystem* Missions = UMissionSubsystem::Get(World))
			{
				Missions->AbortMission();
			}
			// Aborting fires no event: repaint the HUD's objective line so the chase's title does not stay on screen.
			TArray<UUserWidget*> Huds;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Huds, UHawkeyeHudWidget::StaticClass(), false);
			for (UUserWidget* Hud : Huds)
			{
				Cast<UHawkeyeHudWidget>(Hud)->RefreshObjective();
			}
			Extra = TEXT("; the chapter's chase was put away (free roam)");
		}
	}
	PutKate(Kate, Stand, /*bCutCamera=*/true);
	if (Clip == EHawkeyeDemoClip::StreetFight)
	{
		// The group patrols on while the game loads and the owner gets ready to record, and had walked 30 m off in 8 s: hold the
		// three where they stand (facing Kate's way, 9 m off) until she first moves or looses an arrow, then let them go.
		TArray<TWeakObjectPtr<AThugCharacter>> Held;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->ActorHasTag(StreetGroupTag) && !HawkeyeDemoStartLocal::IsDown(*It))
			{
				if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
				{
					// Stopping ST_Thug stops the patrol task that keeps walking (SetHeld and SetThinkingEnabled leave it running).
					Brain->StopMovement();
					if (UBrainComponent* Tree = Brain->FindComponentByClass<UBrainComponent>())
					{
						Tree->StopLogic(TEXT("clip start hold"));
					}
					Held.Add(*It);
				}
			}
		}
		const AHawkeyeCharacter* HawkeyeKate = Cast<AHawkeyeCharacter>(Kate);
		const int32 LoosedAtStart = HawkeyeKate && HawkeyeKate->GetBowComponent() ? HawkeyeKate->GetBowComponent()->GetArrowsLoosed() : 0;
		const FVector StandFeet = Kate->GetActorLocation();
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda(
			[World = TWeakObjectPtr<UWorld>(World), Kate = TWeakObjectPtr<APawn>(Kate), Held, LoosedAtStart, StandFeet, Handle]()
			{
				const AHawkeyeCharacter* Who = Cast<AHawkeyeCharacter>(Kate.Get());
				const bool bMoved = !Who || FVector::Dist2D(Who->GetActorLocation(), StandFeet) > 300.f
					|| (Who->GetBowComponent() && Who->GetBowComponent()->GetArrowsLoosed() != LoosedAtStart);
				if (!bMoved || !World.IsValid())
				{
					return;
				}
				for (const TWeakObjectPtr<AThugCharacter>& Thug : Held)
				{
					if (AThugAIController* Brain = Thug.IsValid() ? Cast<AThugAIController>(Thug->GetController()) : nullptr)
					{
						if (UBrainComponent* Tree = Brain->FindComponentByClass<UBrainComponent>())
						{
							Tree->StartLogic();
						}
					}
				}
				World->GetTimerManager().ClearTimer(*Handle);
			}), 0.25f, true);
		Extra += FString::Printf(TEXT("; the %d StreetGroup thugs hold still until she moves or shoots"), Held.Num());
	}
	OutWhat = FString::Printf(TEXT("Kate put %s: feet %s, yaw %.0f%s"), *Stand.Where, *Stand.Feet.ToCompactString(), Stand.Yaw, *Extra);
	return true;
}
