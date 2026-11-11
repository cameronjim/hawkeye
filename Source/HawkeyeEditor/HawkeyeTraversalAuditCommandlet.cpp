#include "HawkeyeTraversalAuditCommandlet.h"

#include "FileHelpers.h"
#include "HawkeyeEditorModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TraversalAuditRunner.h"

namespace HawkeyeTraversalAuditExit
{
	static constexpr int32 Passed = 0;
	static constexpr int32 Failed = 1;
	static constexpr int32 CouldNotRun = 2;
}

UHawkeyeTraversalAuditCommandlet::UHawkeyeTraversalAuditCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	// The gate is the return value, whatever else the map load logged.
	UseCommandletResultAsExitCode = true;
	HelpDescription = TEXT("Runs the grapple audit on a map and fails when a street-facing roof has no anchor reachable from the street.");
	HelpUsage = TEXT("-run=HawkeyeTraversalAudit -map=/Game/Maps/<Map> [-out=<json>] [-chain=a,b,c] [-streetlinks=c] [-allowfile=<txt>] [-pawn=<class>] [-nospawn] [-probe=<pairs.txt> [-probeout=<tsv>]]");
}

TSet<FString> UHawkeyeTraversalAuditCommandlet::ParseAllowList(const TArray<FString>& Lines)
{
	TSet<FString> Allowed;
	for (const FString& Line : Lines)
	{
		FString Text = Line;
		int32 Hash = INDEX_NONE;
		if (Text.FindChar(TEXT('#'), Hash))
		{
			Text.LeftInline(Hash);
		}
		Text.TrimStartAndEndInline();
		if (Text.IsEmpty())
		{
			continue;
		}
		FString First = Text;
		int32 Space = INDEX_NONE;
		if (Text.FindChar(TEXT(' '), Space) || Text.FindChar(TEXT('\t'), Space))
		{
			First = Text.Left(Space);
		}
		Allowed.Add(First);
	}
	return Allowed;
}

int32 UHawkeyeTraversalAuditCommandlet::Main(const FString& Params)
{
	using namespace HawkeyeTraversalAuditExit;
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	const FString* MapPath = Values.Find(TEXT("map"));
	if (!MapPath || MapPath->IsEmpty())
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: -map=/Game/Maps/<Map> is required. %s"), *HelpUsage);
		return CouldNotRun;
	}
	FTraversalAuditRunner::FOptions Options;
	Options.bSpawnProps = !Switches.Contains(TEXT("nospawn"));
	if (const FString* Pawn = Values.Find(TEXT("pawn")))
	{
		Options.PawnClassPath = *Pawn;
	}
	if (const FString* Chain = Values.Find(TEXT("chain")))
	{
		Options.ObjectiveChain = FTraversalAuditRunner::ParseList(*Chain);
	}
	if (const FString* StreetLinks = Values.Find(TEXT("streetlinks")))
	{
		Options.StreetLinkObjectives = FTraversalAuditRunner::ParseList(*StreetLinks);
	}
	if (const FString* ProbeIn = Values.Find(TEXT("probe")))
	{
		Options.ProbeIn = FPaths::ConvertRelativePathToFull(*ProbeIn);
		const FString* ProbeOut = Values.Find(TEXT("probeout"));
		Options.ProbeOut = ProbeOut ? FPaths::ConvertRelativePathToFull(*ProbeOut) : FPaths::ChangeExtension(Options.ProbeIn, TEXT("out.tsv"));
	}
	TSet<FString> Allowed;
	if (const FString* AllowFile = Values.Find(TEXT("allowfile")))
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, **AllowFile))
		{
			UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: cannot read the allow file %s."), **AllowFile);
			return CouldNotRun;
		}
		Allowed = ParseAllowList(Lines);
	}
	const FString* OutArg = Values.Find(TEXT("out"));
	const FString OutPath = OutArg ? FPaths::ConvertRelativePathToFull(*OutArg) : FGrappleAuditReport::GetDefaultPath();

	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(*MapPath);
	if (!World)
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: could not load %s."), **MapPath);
		return CouldNotRun;
	}

	FGrappleReachScene Scene;
	FGrappleAuditReport Report;
	FString Error;
	if (!FTraversalAuditRunner::Run(World, Options, Scene, Report, Error))
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: %s."), *Error);
		return CouldNotRun;
	}
	if (!Report.WriteJson(OutPath))
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: could not write %s."), *OutPath);
		return CouldNotRun;
	}
	UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: wrote %s"), *OutPath);
	for (const FString& Line : Report.Objectives)
	{
		UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: %s"), *Line);
	}

	const TArray<FString> Failures = Report.FindGateFailures(Allowed);
	for (const FString& Failure : Failures)
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: street-facing roof unreachable from the street: %s"), *Failure);
	}
	for (const FString& Problem : Report.Errors)
	{
		UE_LOG(LogHawkeyeEditor, Error, TEXT("Traversal audit: %s"), *Problem);
	}
	const bool bPassed = Failures.Num() == 0 && !Report.bObjectiveChainBroken;
	UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal audit: %s (%d street-facing roofs unreached, %d allowed, chain %s)."),
		bPassed ? TEXT("PASSED") : TEXT("FAILED"), Report.StreetFacingUnreached.Num(), Report.StreetFacingUnreached.Num() - Failures.Num(),
		Options.ObjectiveChain.Num() == 0 ? TEXT("not checked") : (Report.bObjectiveChainBroken ? TEXT("broken") : TEXT("holds")));
	return bPassed ? Passed : Failed;
}
