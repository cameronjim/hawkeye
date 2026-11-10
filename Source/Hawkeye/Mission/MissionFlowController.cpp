#include "Mission/MissionFlowController.h"

#include "Hawkeye.h"

EMissionFlowStep UMissionFlowController::Begin(bool bInHasFlashback, bool bInHasNextLevel)
{
	FMissionFlowRoute InRoute;
	InRoute.bHasFlashback = bInHasFlashback;
	InRoute.bHasNextLevel = bInHasNextLevel;
	return BeginRoute(InRoute);
}

EMissionFlowStep UMissionFlowController::BeginRoute(const FMissionFlowRoute& InRoute)
{
	Route = InRoute;
	Step = Route.bHasCloseUp ? EMissionFlowStep::CloseUp : EMissionFlowStep::EndCard;

	UE_LOG(LogHawkeye, Log,
		TEXT("Mission flow: %s (flashback: %s, scene: %s, next level: %s, return to roaming: %s)."),
		Route.bHasCloseUp ? TEXT("close-up") : TEXT("end card"), Route.bHasFlashback ? TEXT("yes") : TEXT("no"),
		Route.bHasFlashback && Route.bHasPlayableScene ? TEXT("yes") : TEXT("no"),
		Route.bHasNextLevel ? TEXT("yes") : TEXT("no"), Route.bReturnToRoaming ? TEXT("yes") : TEXT("no"));

	return Step;
}

void UMissionFlowController::ResumeAt(const FMissionFlowRoute& InRoute, EMissionFlowStep InStep)
{
	Route = InRoute;
	Step = InStep;
	UE_LOG(LogHawkeye, Log, TEXT("Mission flow: resumed at %s."), *UEnum::GetValueAsString(InStep));
}

void UMissionFlowController::Reset()
{
	Step = EMissionFlowStep::Idle;
	Route = FMissionFlowRoute();
}

EMissionFlowStep UMissionFlowController::AfterStory() const
{
	if (Route.bHasNextLevel)
	{
		return EMissionFlowStep::OpenNextLevel;
	}
	return Route.bReturnToRoaming ? EMissionFlowStep::ReturnToRoaming : EMissionFlowStep::FinalCard;
}

EMissionFlowStep UMissionFlowController::NextAfter(EMissionFlowStep Current) const
{
	switch (Current)
	{
	case EMissionFlowStep::CloseUp:
		return EMissionFlowStep::EndCard;

	case EMissionFlowStep::EndCard:
		return Route.bHasFlashback ? EMissionFlowStep::Flashback : AfterStory();

	case EMissionFlowStep::Flashback:
		return Route.bHasPlayableScene ? EMissionFlowStep::PlayableScene : AfterStory();

	case EMissionFlowStep::PlayableScene:
		return AfterStory();

	case EMissionFlowStep::FinalCard:
		// The last mission of the campaign hands the player back to the menu.
		return EMissionFlowStep::OpenMenuLevel;

	case EMissionFlowStep::OpenNextLevel:
	case EMissionFlowStep::OpenMenuLevel:
	case EMissionFlowStep::ReturnToRoaming:
		return EMissionFlowStep::Done;

	default:
		// Idle and Done are both terminal: Advance on them is a deliberate no-op.
		return Current;
	}
}

EMissionFlowStep UMissionFlowController::Advance()
{
	Step = NextAfter(Step);
	return Step;
}
