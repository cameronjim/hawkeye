#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChapterMaps.generated.h"

class UChapterSiteDefinition;
class UMissionDefinition;
class UWorld;

/**
 * Which map a chapter's content belongs in (claude-docs/gameplay-semantics.md, "Chapter interiors"). A chapter site or
 * a boss names the map it stands in; none means the district (any map that is not an interior). A chapter whose sites
 * name an interior carries on in it: the interior starts the chapter where the campaign has it, and saves are allowed
 * inside. Pure but for reading a world's package and game mode; tests call the string forms.
 */
UCLASS()
class HAWKEYE_API UHawkeyeChapterMaps : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Content naming MapPackage (a long package name, "/Game/Maps/L_Int_Grotto"; empty for none) belongs in a world
	 * whose package is WorldPackage: the same package, or, with none named, any world that is not an interior.
	 */
	static bool BelongsInWorld(const FString& MapPackage, const FString& WorldPackage, bool bWorldIsInterior);

	/** BelongsInWorld for a soft map reference and a world. */
	static bool BelongsInWorld(const TSoftObjectPtr<UWorld>& Map, const UWorld* World);

	/** The world's long package name without a PIE prefix ("/Game/Maps/L_District_EastVillage"); empty for none. */
	static FString PackageOf(const UWorld* World);

	/** The soft reference's long package name, empty for none. */
	static FString PackageOf(const TSoftObjectPtr<UWorld>& Map);

	/** An interior map: its game mode has the indoor camera (BP_GameMode_Interior). */
	static bool IsInteriorWorld(const UWorld* World);

	/**
	 * The index into Maps (each site's map package, empty for the district) of the site to dress in a world: the first
	 * that belongs there (BelongsInWorld). INDEX_NONE when none does.
	 */
	static int32 PickSite(const TArray<FString>& Maps, const FString& WorldPackage, bool bWorldIsInterior);

	/** The chapter's site for World: its Site, then its InteriorSites, by PickSite. Null when none belongs there. */
	static UChapterSiteDefinition* FindSiteFor(const UMissionDefinition* Mission, const UWorld* World);

	/** True when one of the chapter's sites names InteriorPackage: the chapter carries on inside it. */
	static bool ContinuesInto(const UMissionDefinition* Mission, const FString& InteriorPackage);
};
