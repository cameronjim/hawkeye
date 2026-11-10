#include "World/ThugEnvQueryContext.h"

#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "GameFramework/Pawn.h"
#include "World/ThugAIController.h"

void UHawkeyeEnvQueryContext_ThugTarget::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const APawn* Querier = Cast<APawn>(QueryInstance.Owner.Get());
	const AThugAIController* Brain = Querier ? Cast<AThugAIController>(Querier->GetController()) : nullptr;
	if (const AActor* Target = Brain ? Brain->GetTarget() : nullptr)
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
	}
}
