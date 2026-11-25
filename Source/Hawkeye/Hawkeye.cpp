#include "Hawkeye.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/CameraAudit.h"
#endif

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

	bool CommandLineIsolatesSaves(const TCHAR* CommandLine)
	{
		if (!CommandLine)
		{
			return false;
		}
		return FCString::Stristr(CommandLine, TEXT("-unattended")) != nullptr
			|| FCString::Stristr(CommandLine, TEXT("Automation")) != nullptr
			|| FCString::Stristr(CommandLine, TEXT("-HawkeyeDemo")) != nullptr;
	}

	bool IsolatesSaves()
	{
		static const bool bByCommandLine = CommandLineIsolatesSaves(FCommandLine::Get());
		return bByCommandLine || IsAutomationRun();
	}

	bool IsPlayerSaveSlot(const FString& SlotName)
	{
		return SlotName == TEXT("HawkeyeCampaign") || SlotName == TEXT("HawkeyeSettings");
	}

	FString SaveSlotNameFor(const FString& SlotName, bool bIsolated)
	{
		return bIsolated && IsPlayerSaveSlot(SlotName) ? SlotName + TEXT("Automation") : SlotName;
	}

	FString SaveSlotName(const FString& SlotName)
	{
		return SaveSlotNameFor(SlotName, IsolatesSaves());
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
#if WITH_DEV_AUTOMATION_TESTS
		// Every Hawkeye.Lap.* test runs under the camera audit (Tests/CameraAudit.h).
		HawkeyeCameraAudit::Install();
#endif
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
