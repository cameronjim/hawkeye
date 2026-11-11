#include "TraversalAuditRunner.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HawkeyeEditorModule.h"
#include "Traversal/GrappleReachWorld.h"
#include "World/CityLedgeSpawner.h"
#include "World/CityMapData.h"
#include "World/GrappleAnchor.h"

FGrappleReachProfile FTraversalAuditRunner::MakeProfile(const UWorld* World, const FString& PawnClassPath)
{
	TSubclassOf<APawn> PawnClass;
	if (!PawnClassPath.IsEmpty())
	{
		PawnClass = LoadClass<APawn>(nullptr, *PawnClassPath);
		if (!PawnClass)
		{
			UE_LOG(LogHawkeyeEditor, Warning, TEXT("Traversal audit: no pawn class at %s; using the map's."), *PawnClassPath);
		}
	}
	return FGrappleReachWorld::MakePlayerProfile(World, PawnClass);
}

bool FTraversalAuditRunner::BuildScene(UWorld* World, FGrappleReachScene& OutScene, FString& OutError)
{
	const ACityLedgeSpawner* Spawner = FGrappleReachWorld::FindSpawner(World);
	const UCityLedgeData* Props = Spawner ? Spawner->Data.Get() : nullptr;
	const UCityMapData* Map = LoadObject<UCityMapData>(nullptr, UCityMapData::DefaultPath);
	const AGrappleAnchor* AnchorDefault = Spawner && Spawner->AnchorClass ? Spawner->AnchorClass->GetDefaultObject<AGrappleAnchor>()
		: GetDefault<AGrappleAnchor>();
	if (!Spawner)
	{
		OutError = TEXT("this map has no ACityLedgeSpawner (open the district)");
		return false;
	}
	return FGrappleReachWorld::BuildScene(World, Map, Props, AnchorDefault ? AnchorDefault->MarkerHeight : 20.f, OutScene, OutError);
}

bool FTraversalAuditRunner::Run(UWorld* World, const FOptions& Options, FGrappleReachScene& OutScene, FGrappleAuditReport& OutReport,
	FString& OutError)
{
	ACityLedgeSpawner* Spawner = FGrappleReachWorld::FindSpawner(World);
	const bool bSpawnHere = Options.bSpawnProps && Spawner && !Spawner->IsSpawnComplete();
	if (bSpawnHere)
	{
		const int32 Spawned = Spawner->SpawnAll();
		UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: spawned %d city props for the audit."), Spawned);
	}

	bool bOk = BuildScene(World, OutScene, OutError);
	if (bOk)
	{
		const FGrappleReachProfile Profile = MakeProfile(World, Options.PawnClassPath);
		FGrappleAuditInputs Inputs = FGrappleAudit::MakeWorldInputs(World, OutScene, Profile);
		Inputs.ObjectiveChain = Options.ObjectiveChain;
		Inputs.StreetLinkObjectives = TSet<FString>(Options.StreetLinkObjectives);
		OutReport = FGrappleAudit::Run(Inputs);
		UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: %s"), *OutReport.Summary);
		// No sidewalk spot at all means the traces hit nothing (no collision in this world), not a district with no
		// problems: an empty audit must never pass the gate.
		if (OutReport.StreetSpotCount == 0 || OutReport.RoofSpotCount == 0)
		{
			OutError = FString::Printf(TEXT("the audit found %d sidewalk and %d roof spots: its traces hit nothing, so this world has no "
				"collision to audit"), OutReport.StreetSpotCount, OutReport.RoofSpotCount);
			bOk = false;
		}
	}

	if (bSpawnHere && IsValid(Spawner))
	{
		Spawner->DestroySpawned();
	}
	return bOk;
}

TArray<FString> FTraversalAuditRunner::ParseList(const FString& Text)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty=*/true);
	TArray<FString> Out;
	for (FString& Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (!Part.IsEmpty())
		{
			Out.Add(Part);
		}
	}
	return Out;
}
