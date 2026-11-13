#include "Hawkeye.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogHawkeye);

namespace HawkeyeAutomation
{
	static bool bPlaysAsPlayer = false;

	bool IsTestRunning()
	{
		return GIsAutomationTesting;
	}

	bool IsLaunchedForTests()
	{
		// The command line is fixed once the engine is up; read it once.
		static const bool bRunTests = FCString::Stristr(FCommandLine::Get(), TEXT("RunTests")) != nullptr;
		return bRunTests;
	}

	bool IsAutomationRun()
	{
		return IsTestRunning() || IsLaunchedForTests();
	}

	bool IsAutomationRunOrCommandlet()
	{
		return IsAutomationRun() || IsRunningCommandlet();
	}

	bool PlaysAsPlayer()
	{
		return bPlaysAsPlayer;
	}

	void SetPlaysAsPlayer(bool bPlays)
	{
		bPlaysAsPlayer = bPlays;
	}

	bool SkipsPlayerFlow()
	{
		return IsAutomationRun() && !PlaysAsPlayer();
	}
}

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
