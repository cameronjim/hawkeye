#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "World/ThugCharacter.h"
#include "ChapterSiteDefinition.generated.h"

class AThugCharacter;
class APickupActor;
class UStaticMesh;

/** A greybox prop of the site: an engine-free static mesh built by a content script. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteProp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Blocks pawns and arrows (tables, boxes); off for overhead dressing (banners, string lights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	bool bBlocks = true;
};

/** A point light of the site's dressing (string lights). */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site", meta = (ClampMin = "0.0"))
	float Lumens = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site", meta = (ClampMin = "0.0"))
	float Radius = 600.f;
};

/** An AObjectiveTriggerVolume: the player walking into it completes ObjectiveId. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteVolume
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName ObjectiveId;

	/** The box's centre, world cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Extent = FVector(400.f, 400.f, 200.f);
};

/** One of the site's crew: a thug spawned with his thinking off until the crew is alerted. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteThug
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftClassPtr<AThugCharacter> ThugClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	EThugWeapon Weapon = EThugWeapon::Fists;

	/** His feet, world cm (projected onto the navmesh at spawn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	float Yaw = 0.f;
};

/** A pickup lying on the site: a keycard-type APickupActor whose KeycardId an examine can ask for. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSitePickup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftClassPtr<APickupActor> PickupClass;

	/** Where it rests, world cm (it hovers above this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	/** The keycard it gives; set on the spawned pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName KeycardId;
};

/** Something the player examines (an AChapterEndInteractable), completing ObjectiveId. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteExamine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName ObjectiveId;

	/** The actor's root, world cm; the prop sits on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	float Yaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FText Prompt;

	/** What is examined. None leaves only the interact zone (the thing is a prop of its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Offered only while ObjectiveId is the current objective. Off for an optional examine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	bool bRequireCurrentObjective = true;

	/** Keycards the player must carry for it to be offered (the toy sacks at the drive table). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TArray<FName> RequiredKeycards;

	/** The close-up camera and what it looks at, in the actor's frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector CloseUpOffset = FVector(-160.f, 60.f, 120.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector CloseUpLookAt = FVector(0.f, 0.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site", meta = (ClampMin = "10.0"))
	FVector InteractZoneExtent = FVector(200.f, 200.f, 150.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site", meta = (ClampMin = "0.1"))
	float CloseUpSeconds = 2.f;

	/** Offered only once this objective is complete (the ornament waits for the chapter's end). None: no wait. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName RequiredObjective;
};

/** A door into an interior map (an AInteriorEntrance) the chapter puts on a facade while it runs. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteInteriorDoor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftObjectPtr<UWorld> Interior;

	/** The middle of the threshold at floor level, world cm; +X (Yaw) points out at the street. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	float Yaw = 0.f;

	/** The sign and the prompt ("[Santa's Grotto]"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FText DisplayName;

	/** The return point (a ReturnPoints label) the interior's front door comes back out at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName ReturnPointLabel;

	/** A tag on the door, so an objective's MarkerActorLabel can point at it. None: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName Tag;
};

/** A labelled point (a target point tagged Label): where an interior's door, front or roof, comes back out. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteReturnPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName Label;

	/** Her feet, world cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	float Yaw = 0.f;
};

/**
 * A place that reports a chapter event the first time Kate stands in it (gameplay-semantics.md, "Chapter sites",
 * triggers): the event's lines and arrows are the chapter's (UMissionDefinition::Events). Overheard talk, a first sight,
 * an unlock where something is found.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteTrigger
{
	GENERATED_BODY()

	/** The chapter event reported ("trigger:overheard"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName Event;

	/** The box's centre and half extent, world cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Extent = FVector(300.f, 300.f, 200.f);

	/** Only while no thug is Alerted (an overheard conversation stops when someone has seen her). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	bool bOnlyUnseen = false;
};

/** Something on a wall an arrow trips (an AArrowSwitch): a buzzer that pulls a guard away, a chapter event. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChapterSiteSwitch
{
	GENERATED_BODY()

	/** The switch's id; it reports the chapter event "switch:<Id>" when tripped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FName Id;

	/** The face's middle, world cm; +X (Yaw) points out of the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	float Yaw = 0.f;

	/** What it looks like (a box on the wall). None: the class's own shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** The noise it makes when tripped (a thug hears HearingRange times this), 0 for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site", meta = (ClampMin = "0.0"))
	float NoiseLoudness = 1.f;

	/** Only a returning arrow's blunt head trips it; a plain arrow sticks in it and does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Site")
	bool bReturningArrowsOnly = true;
};

/**
 * A chapter's set piece on the district (claude-docs/gameplay-semantics.md, "Chapter sites"): what UChapterSiteSubsystem
 * spawns when the chapter starts and clears when another starts. Planned from the district's records and written by a
 * content script (chapter 2's toy drive: Tools/Editor/create_site.py, _site.py); nothing is placed in the map. A chapter
 * names it on UMissionDefinition::Site; another chapter's site is another asset.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UChapterSiteDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The map this site stands in. None: the district (any map that is not an interior). A site naming an interior is
	 * one of a chapter's InteriorSites; the chapter carries on into that interior (gameplay-semantics.md, "Chapter
	 * interiors").
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TSoftObjectPtr<UWorld> Map;

	/** Where the chapter's opening puts Kate (world cm, her feet); zero leaves her where she is. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	FVector PlayerStart = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	float PlayerStartYaw = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteProp> Props;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteLight> Lights;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteVolume> Volumes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteThug> Crew;

	/** Completed when every one of the crew is down (an AThugGroupObjective at their middle). None: nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	FName CrewObjective;

	/** The crew wake and come for her when this completes (none: from the start). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	FName CrewAlertObjective;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSitePickup> Pickups;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteExamine> Examines;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteInteriorDoor> InteriorDoors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteReturnPoint> ReturnPoints;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteTrigger> Triggers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Site")
	TArray<FChapterSiteSwitch> Switches;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("ChapterSite"), GetFName());
	}
	//~ End UPrimaryDataAsset interface
};
