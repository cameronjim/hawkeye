#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHawkeyeEditor, Log, All);

/**
 * The editor module (claude-docs/tools-traversal-debugger.md). Loads in the Default phase, not PostEngineInit: the
 * engine looks a -run= commandlet's class up before it loads PostEngineInit modules
 * (FEngineLoop::PreInitPostStartupScreen).
 */
class FHawkeyeEditorModule : public IModuleInterface
{
};
