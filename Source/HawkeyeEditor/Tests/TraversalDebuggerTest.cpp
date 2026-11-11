#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HawkeyeEditorModule.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "STraversalDebuggerPanel.h"
#include "ToolMenus.h"
#include "TraversalDebuggerSubsystem.h"
#include "TraversalDebuggerViewModel.h"
#include "Traversal/GrappleReachWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The traversal debugger as far as it can be checked without a window (claude-docs/tools-traversal-debugger.md): the
 * module loads, the tab spawner and the Tools menu entry are registered, the panel builds under -nullrhi, its rows
 * are the view model's, and on the district the subsystem's reach query runs end to end. Nothing here draws or
 * looks at pixels: the tab itself has not been seen on screen by these tests.
 */
namespace HawkeyeTraversalDebuggerTest
{
	static FGrappleReachEvaluation Result(int32 Anchor, EGrappleReachVerdict Verdict, float Distance, const FString& Blocker = FString())
	{
		FGrappleReachEvaluation Out;
		Out.Anchor = Anchor;
		Out.Verdict = Verdict;
		Out.Distance = Distance;
		Out.AngleDegrees = 4.f;
		Out.Blocker.Name = Blocker;
		return Out;
	}

	static UTraversalDebuggerSubsystem* GetSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UTraversalDebuggerSubsystem>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalDebuggerRegistrationTest, "Hawkeye.Editor.TraversalDebugger.RegistersTabAndMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalDebuggerRegistrationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("The HawkeyeEditor module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("HawkeyeEditor")));
	TestNotNull(TEXT("The debugger's editor subsystem is running"), HawkeyeTraversalDebuggerTest::GetSubsystem());

	if (!TestTrue(TEXT("Slate is up (-nullrhi still starts it)"), FSlateApplication::IsInitialized()))
	{
		return true;
	}
	const TSharedPtr<FTabSpawnerEntry> Spawner = FGlobalTabmanager::Get()->FindTabSpawnerFor(FHawkeyeEditorModule::TraversalDebuggerTabName);
	if (TestTrue(TEXT("The tab spawner is registered"), Spawner.IsValid()))
	{
		TestEqual(TEXT("under its display name"), Spawner->GetDisplayName().ToString(), FString(TEXT("Traversal Debugger")));
	}
	TestTrue(TEXT("The global tab manager can spawn it"), FGlobalTabmanager::Get()->HasTabSpawner(FHawkeyeEditorModule::TraversalDebuggerTabName));

	UToolMenu* Tools = UToolMenus::Get()->FindMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (TestNotNull(TEXT("The Tools menu is registered or extended"), Tools))
	{
		FToolMenuSection* Section = Tools->FindSection(TEXT("Hawkeye"));
		if (TestNotNull(TEXT("with a Hawkeye section"), Section))
		{
			TestNotNull(TEXT("holding the Traversal Debugger entry"), Section->FindEntry(TEXT("HawkeyeTraversalDebugger")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalDebuggerViewModelTest, "Hawkeye.Editor.TraversalDebugger.ViewModelRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalDebuggerViewModelTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalDebuggerTest;
	FGrappleReachScene Scene;
	FGrappleReachRoof Roof;
	Roof.OsmId = TEXT("W1");
	Scene.Roofs.Add(Roof);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FGrappleReachAnchor& Anchor = Scene.Anchors.AddDefaulted_GetRef();
		Anchor.Name = FString::Printf(TEXT("City_Anchor_%d"), Index);
		Anchor.Roof = Index == 4 ? INDEX_NONE : 0;
	}
	const TArray<FGrappleReachEvaluation> All = {
		Result(0, EGrappleReachVerdict::SightBlocked, 900.f, TEXT("StaticMeshActor_7")),
		Result(1, EGrappleReachVerdict::TooFar, 3000.f),
		Result(2, EGrappleReachVerdict::Reachable, 1500.f),
		Result(3, EGrappleReachVerdict::SameRoof, 400.f),
		Result(4, EGrappleReachVerdict::Reachable, 700.f),
	};

	const TArray<FGrappleReachEvaluation> Listed = FTraversalDebuggerViewModel::SelectReachResults(All);
	if (TestEqual(TEXT("Out of range and same-roof results are not listed"), Listed.Num(), 3))
	{
		TestEqual(TEXT("Reachable first, nearest first"), Listed[0].Anchor, 4);
		TestEqual(TEXT("then the farther reachable one"), Listed[1].Anchor, 2);
		TestEqual(TEXT("then the blocked one"), Listed[2].Anchor, 0);
	}
	const TArray<FTraversalDebuggerRow> Rows = FTraversalDebuggerViewModel::MakeReachRows(Scene, Listed);
	if (TestEqual(TEXT("One row per listed result"), Rows.Num(), 3))
	{
		TestEqual(TEXT("An anchor row points at its anchor"), Rows[0].Anchor, 4);
		TestEqual(TEXT("and at no roof"), Rows[0].Roof, int32(INDEX_NONE));
		TestEqual(TEXT("Label of an anchor on no roof"), Rows[0].Label, FString(TEXT("City_Anchor_4 on no roof, 7 m, 4.0 deg: reachable")));
		TestEqual(TEXT("Label with the blocker"), Rows[2].Label,
			FString(TEXT("City_Anchor_0 on W1, 9 m, 4.0 deg: view blocked (StaticMeshActor_7)")));
		TestTrue(TEXT("Reachable is green"), Rows[0].Color.G > Rows[0].Color.R);
		TestTrue(TEXT("Rows use the verdict's colour"), Rows[2].Color.Equals(FTraversalDebuggerViewModel::ColorOf(EGrappleReachVerdict::SightBlocked)));
	}
	TArray<FGrappleReachEvaluation> Stale = Listed;
	Stale.Add(Result(99, EGrappleReachVerdict::Reachable, 100.f));
	TestEqual(TEXT("A result for an anchor the scene does not have is dropped"), FTraversalDebuggerViewModel::MakeReachRows(Scene, Stale).Num(), 3);

	FGrappleAuditReport Report;
	Report.Summary = TEXT("the summary");
	for (const EGrappleRoofReach Reach : { EGrappleRoofReach::Reached, EGrappleRoofReach::NoStreetInRange, EGrappleRoofReach::Unreached,
			 EGrappleRoofReach::NoAnchors })
	{
		FGrappleAuditRoofRow& Row = Report.Roofs.AddDefaulted_GetRef();
		Row.Roof = Report.Roofs.Num() - 1;
		Row.OsmId = FString::Printf(TEXT("W%d"), Row.Roof);
		Row.Reach = Reach;
	}
	Report.Roofs[2].Why = TEXT("view blocked by a building x3");
	const TArray<FTraversalDebuggerRow> RoofRows = FTraversalDebuggerViewModel::MakeRoofRows(&Report);
	if (TestEqual(TEXT("Only unreached and out-of-range roofs are listed"), RoofRows.Num(), 2))
	{
		TestEqual(TEXT("The street-facing one (the gate's) first"), RoofRows[0].Roof, 2);
		TestTrue(TEXT("with its reason"), RoofRows[0].Label.EndsWith(TEXT(": view blocked by a building x3")));
		TestEqual(TEXT("then the one with no sidewalk in range"), RoofRows[1].Roof, 1);
		TestTrue(TEXT("named by its reach when there is no reason"), RoofRows[1].Label.EndsWith(TEXT(": no sidewalk spot in range")));
	}
	TestEqual(TEXT("No report, no roof rows"), FTraversalDebuggerViewModel::MakeRoofRows(nullptr).Num(), 0);
	TestEqual(TEXT("Summary is the report's"), FTraversalDebuggerViewModel::MakeSummaryText(&Report), FString(TEXT("the summary")));
	TestTrue(TEXT("Idle status says how to start"), FTraversalDebuggerViewModel::MakeStatusText(true, FString()).StartsWith(TEXT("Open the district")));
	TestEqual(TEXT("Status is the subsystem's"), FTraversalDebuggerViewModel::MakeStatusText(true, TEXT("busy")), FString(TEXT("busy")));
	TestTrue(TEXT("No subsystem is said"), FTraversalDebuggerViewModel::MakeStatusText(false, TEXT("busy")).Contains(TEXT("not running")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalDebuggerPanelTest, "Hawkeye.Editor.TraversalDebugger.PanelBuildsHeadless",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalDebuggerPanelTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalDebuggerTest;
	const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	if (!TestNotNull(TEXT("Subsystem"), Subsystem))
	{
		return true;
	}
	// The widget tree is built with no window around it; nothing is painted.
	const TSharedRef<STraversalDebuggerPanel> Panel = SNew(STraversalDebuggerPanel);
	const int32 Expected = Subsystem->HasReach()
		? FTraversalDebuggerViewModel::MakeReachRows(Subsystem->GetScene(), Subsystem->GetReachResults()).Num() : 0;
	TestEqual(TEXT("The reach list holds the view model's rows"), Panel->GetReachRowCount(), Expected);
	TestEqual(TEXT("The roof list holds the view model's rows"), Panel->GetRoofRowCount(),
		FTraversalDebuggerViewModel::MakeRoofRows(Subsystem->GetAuditReport()).Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalDebuggerDistrictTest, "Hawkeye.Editor.TraversalDebugger.ReachOnTheDistrict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalDebuggerDistrictTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalDebuggerTest;
	UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!Subsystem || !World || !FGrappleReachWorld::FindSpawner(World))
	{
		// The editor opens the district (EditorStartupMap); another map has nothing to check.
		AddInfo(TEXT("The editor world is not the district: skipped."));
		return true;
	}
	// The scene from the generated data and BP_Kate's rules, the way the tab's Refresh does it.
	if (!TestTrue(TEXT("Refresh builds the scene"), Subsystem->RefreshScene()))
	{
		AddError(Subsystem->GetStatus());
		return true;
	}
	TestTrue(TEXT("Its rules are BP_Kate's, read off the class default"), Subsystem->GetProfile().Source.StartsWith(TEXT("BP_Kate")));
	TestTrue(TEXT("It has the district's anchors"), Subsystem->GetScene().Anchors.Num() > 1000);

	// Reach from above the first street's first point: the floor is found and every anchor checked.
	const FGrappleReachScene& Scene = Subsystem->GetScene();
	const FVector2D Street = Scene.Streets.Num() > 0 && Scene.Streets[0].Points.Num() > 0 ? Scene.Streets[0].Points[0] : FVector2D::ZeroVector;
	TestTrue(TEXT("Reach from a street finds a floor"), Subsystem->ReachFrom(FVector(Street.X, Street.Y, 5000.f), TEXT("the test")));
	TestTrue(TEXT("and has results"), Subsystem->HasReach());
	TestTrue(TEXT("The status reports the count"), Subsystem->GetStatus().Contains(TEXT("anchors in range are reachable")));

	const TSharedRef<STraversalDebuggerPanel> Panel = SNew(STraversalDebuggerPanel);
	TestEqual(TEXT("The panel lists what the view model selects"), Panel->GetReachRowCount(),
		FTraversalDebuggerViewModel::MakeReachRows(Scene, Subsystem->GetReachResults()).Num());
	AddInfo(FString::Printf(TEXT("TraversalDebugger: %s; %d rows"), *Subsystem->GetStatus(), Panel->GetReachRowCount()));
	Subsystem->ClearDrawing();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
