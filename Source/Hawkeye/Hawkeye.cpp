#include "Hawkeye.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogHawkeye);

/**
 * The game module. Its one job at startup: the standalone game (-game from the editor binaries)
 * does not start the Python interpreter the editor's asset scripts need. Python initialises in
 * OnPostEngineInit, after this module loads; it cost a quarter of a second of every launch and
 * 45 ms of the first frame (the plugins' init_unreal.py scripts). -EnablePython keeps it on.
 */
class FHawkeyeGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		if (!IsRunningGame() || FParse::Param(FCommandLine::Get(), TEXT("EnablePython")))
		{
			return;
		}
		if (IConsoleVariable* PythonByDefault = IConsoleManager::Get().FindConsoleVariable(TEXT("Engine.Python.IsEnabledByDefault")))
		{
			PythonByDefault->Set(false, ECVF_SetByCode);
		}
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FHawkeyeGameModule, Hawkeye, "Hawkeye");
