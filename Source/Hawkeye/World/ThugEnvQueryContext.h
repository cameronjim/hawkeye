#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "ThugEnvQueryContext.generated.h"

/**
 * EQS context: the actor the querying thug is fighting (AThugAIController::GetTarget). EQS_CoverPoints
 * traces from it to each candidate and keeps the blocked ones.
 */
UCLASS()
class HAWKEYE_API UHawkeyeEnvQueryContext_ThugTarget : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
