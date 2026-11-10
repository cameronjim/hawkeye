#include "Flashback/FlashbackDefinition.h"

FPrimaryAssetId UFlashbackDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Flashback"), GetFName());
}
