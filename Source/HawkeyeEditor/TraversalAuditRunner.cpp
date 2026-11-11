#include "TraversalAuditRunner.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HawkeyeEditorModule.h"
#include "Misc/FileHelper.h"
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
		if (!Options.ProbeIn.IsEmpty() && !RunProbe(World, OutScene, Profile, Options.ProbeIn, Options.ProbeOut, OutError))
		{
			bOk = false;
		}
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

bool FTraversalAuditRunner::RunProbe(UWorld* World, const FGrappleReachScene& Scene, const FGrappleReachProfile& Profile,
	const FString& ProbeIn, const FString& ProbeOut, FString& OutError)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ProbeIn))
	{
		OutError = FString::Printf(TEXT("cannot read the probe file %s"), *ProbeIn);
		return false;
	}
	TMap<int32, int32> ByIndex;
	for (int32 Id = 0; Id < Scene.Anchors.Num(); ++Id)
	{
		ByIndex.Add(Scene.Anchors[Id].Index, Id);
	}
	const FGrappleReachOracle Oracle = FGrappleReachWorld::MakeOracle(World, Profile, &Scene);
	const FGrappleAuditSettings Settings;
	auto Describe = [](const FGrappleReachBlocker& Blocker)
	{
		return Blocker.Name.IsEmpty() ? FString(TEXT("-\t-\t-"))
			: FString::Printf(TEXT("%s\t%s\t%s"), *Blocker.Kind.Replace(TEXT(" "), TEXT("_")), *Blocker.Name, *Blocker.Location.ToCompactString().Replace(TEXT(" "), TEXT("")));
	};
	TArray<FString> Out;
	Out.Add(TEXT("# x\ty\tanchor\tfloor_z\tverdict\tdistance\tangle\tin_view\tseen\tclear\tlens\tsight_kind\tsight_name\tsight_at\tzip_kind\tzip_name\tzip_at"));
	for (const FString& Raw : Lines)
	{
		FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> Parts;
		Line.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 3)
		{
			continue;
		}
		const double X = FCString::Atod(*Parts[0]);
		const double Y = FCString::Atod(*Parts[1]);
		const int32 Index = FCString::Atoi(*Parts[2]);
		const int32* Id = ByIndex.Find(Index);
		FVector Floor;
		const FGrappleReachQuery::EStand Stand = FGrappleReachWorld::FindSidewalkFloor(World, FVector2D(X, Y), Settings.StreetMaxFloorZ, Floor,
			Scene.SeeThrough);
		if (!Id || Stand != FGrappleReachQuery::EStand::Stand)
		{
			Out.Add(FString::Printf(TEXT("%.1f\t%.1f\t%d\t-\t%s"), X, Y, Index, !Id ? TEXT("no_anchor") : TEXT("no_floor")));
			continue;
		}
		const FGrappleReachSpot Spot{ FGrappleReachQuery::ComputeStandingCentre(Profile.Rules, Floor), INDEX_NONE };
		const FGrappleReachAnchor& Anchor = Scene.Anchors[*Id];
		FVector Lens, Forward;
		FGrappleReachQuery::PredictLens(Profile.Lens, Spot.Centre, Anchor.Marker, Oracle.ProbeLens, Lens, Forward);
		const float Angle = FGrappleReachQuery::AngleOffViewDegrees(Lens, Forward, Anchor.Marker);
		const bool bInView = Angle <= Profile.Rules.ConeDegrees && FGrappleReachQuery::IsOnScreen(Profile.Rules, Lens, Forward, Anchor.Marker);
		FGrappleReachBlocker Sight, Zip;
		const bool bSeen = Oracle.HasLineOfSight(Lens, Anchor, &Sight);
		const bool bClear = Oracle.IsZipClear(Spot.Centre, Anchor, /*bFromGround=*/true, &Zip);
		const FGrappleReachEvaluation Result = FGrappleReachQuery::Evaluate(Profile.Rules, Profile.Lens, Oracle, Spot, Scene.Anchors, *Id);
		Out.Add(FString::Printf(TEXT("%.1f\t%.1f\t%d\t%.1f\t%s\t%.1f\t%.2f\t%d\t%d\t%d\t%s\t%s\t%s"), X, Y, Index, Floor.Z,
			*FString(LexToString(Result.Verdict)).Replace(TEXT(" "), TEXT("_")), Result.Distance, Angle, bInView ? 1 : 0, bSeen ? 1 : 0,
			bClear ? 1 : 0, *Lens.ToCompactString().Replace(TEXT(" "), TEXT("")), *Describe(Sight), *Describe(Zip)));
	}
	if (!FFileHelper::SaveStringArrayToFile(Out, *ProbeOut))
	{
		OutError = FString::Printf(TEXT("cannot write the probe results %s"), *ProbeOut);
		return false;
	}
	UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: probed %d pairs into %s"), Out.Num() - 1, *ProbeOut);
	return true;
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
