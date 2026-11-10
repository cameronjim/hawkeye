#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ChapterEndInteractable.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The chapter end is not examined with a fight on, nor while she is down (gameplay-semantics.md, "Narrative
 * plumbing"): an alerted living thug holds it; calm or dead, it offers itself again. Found by Hawkeye.Lap.Campaign: examining CH01's
 * arrow under the ArcherPair ran the end sequence and the scene, and the return put Kate back in their sight,
 * down inside four seconds of the "[Chapter complete]" toast.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterEndWaitsOutAFight, "Hawkeye.MissionFlow.ChapterEndWaitsOutAFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChapterEndWaitsOutAFight::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!Missions)
	{
		AddError(TEXT("The test world has no UMissionSubsystem."));
		return false;
	}
	UMissionDefinition* Mission = NewObject<UMissionDefinition>(Missions);
	UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
	Objective->ObjectiveId = TEXT("find_arrow");
	Mission->Objectives.Add(Objective);
	Missions->StartMission(Mission);

	AChapterEndInteractable* Arrow = Cast<AChapterEndInteractable>(
		TestWorld.SpawnActor(AChapterEndInteractable::StaticClass(), FVector(500.f, 0.f, 100.f), FRotator::ZeroRotator));
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	AHawkeyePauseTestController* PC = Cast<AHawkeyePauseTestController>(
		TestWorld.SpawnActor(AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(-2000.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestTrue(TEXT("Arrow, Kate, a controller and a thug spawned"), Arrow && Kate && PC && Thug))
	{
		return false;
	}
	Arrow->ObjectiveId = TEXT("find_arrow");
	PC->Possess(Kate);

	TestTrue(TEXT("Calm: the arrow offers itself"), IInteractable::Execute_CanInteract(Arrow, Kate));
	Thug->SetAlertState(EThugAlertState::Alerted);
	TestFalse(TEXT("A thug alerted: it waits"), IInteractable::Execute_CanInteract(Arrow, Kate));
	IInteractable::Execute_Interact(Arrow, Kate);
	TestFalse(TEXT("And a press does nothing"), Arrow->HasBeenUsed() || Objective->IsCompleted()
		|| (Missions->GetTracker() && Missions->GetTracker()->FindObjective(TEXT("find_arrow"))->IsCompleted()));
	Thug->GetHealthComponent()->ApplyDamage(100000.f, nullptr);
	TestTrue(TEXT("The thug dead: it offers itself again"), IInteractable::Execute_CanInteract(Arrow, Kate));

	// Down, she examines nothing (found by the same lap: E from the ground ran the sequence, its save refused).
	Kate->GetHealthComponent()->ApplyDamage(100000.f, nullptr);
	TestTrue(TEXT("At 0 health she is down"), Kate->IsDowned() || !Kate->GetHealthComponent()->IsAlive());
	TestFalse(TEXT("Down: the arrow does not offer itself"), IInteractable::Execute_CanInteract(Arrow, Kate));
	return true;
}

#endif
