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

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("ChapterSite"), GetFName());
	}
	//~ End UPrimaryDataAsset interface
};
