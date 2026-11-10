#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

DECLARE_LOG_CATEGORY_EXTERN(LogHawkeyeEditor, Log, All);

/**
 * The editor module: registers the traversal debugger's tab and its Tools menu entry. The debugger's state lives in
 * UTraversalDebuggerSubsystem; the commandlet needs nothing registered (claude-docs/tools-traversal-debugger.md).
 *
 * Loads in the Default phase, not PostEngineInit: the engine looks a -run= commandlet's class up before it loads
 * PostEngineInit modules (FEngineLoop::PreInitPostStartupScreen), and the menu waits for UToolMenus' startup callback.
 */
class FHawkeyeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** The traversal debugger's tab id. */
	static const FName TraversalDebuggerTabName;

private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnTraversalDebuggerTab(const FSpawnTabArgs& Args);
	static void OpenTraversalDebugger();

	bool bTabRegistered = false;
};
