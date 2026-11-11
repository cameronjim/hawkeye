#include "HawkeyeEditorModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "STraversalDebuggerPanel.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "HawkeyeEditor"

DEFINE_LOG_CATEGORY(LogHawkeyeEditor);

const FName FHawkeyeEditorModule::TraversalDebuggerTabName(TEXT("HawkeyeTraversalDebugger"));

void FHawkeyeEditorModule::StartupModule()
{
	// A commandlet has no Slate to put a tab in; the audit commandlet needs nothing registered.
	if (IsRunningCommandlet())
	{
		return;
	}
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TraversalDebuggerTabName,
			FOnSpawnTab::CreateRaw(this, &FHawkeyeEditorModule::SpawnTraversalDebuggerTab))
		.SetDisplayName(LOCTEXT("TraversalDebuggerTitle", "Traversal Debugger"))
		.SetTooltipText(LOCTEXT("TraversalDebuggerTooltip", "Grapple anchors, ledges, fire escapes and the roofs the street cannot reach."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
	bTabRegistered = true;
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHawkeyeEditorModule::RegisterMenus));
}

void FHawkeyeEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (bTabRegistered && FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TraversalDebuggerTabName);
	}
	bTabRegistered = false;
}

void FHawkeyeEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu)
	{
		return;
	}
	FToolMenuSection& Section = Menu->FindOrAddSection("Hawkeye", LOCTEXT("HawkeyeSection", "Hawkeye"));
	Section.AddMenuEntry("HawkeyeTraversalDebugger",
		LOCTEXT("OpenTraversalDebugger", "Traversal Debugger"),
		LOCTEXT("OpenTraversalDebuggerTooltip", "Draw the district's grapple anchors, ledges and fire escapes, check reach from a point, "
			"and run the grapple audit."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FHawkeyeEditorModule::OpenTraversalDebugger)));
}

TSharedRef<SDockTab> FHawkeyeEditorModule::SpawnTraversalDebuggerTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STraversalDebuggerPanel)
		];
}

void FHawkeyeEditorModule::OpenTraversalDebugger()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TraversalDebuggerTabName));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHawkeyeEditorModule, HawkeyeEditor)
