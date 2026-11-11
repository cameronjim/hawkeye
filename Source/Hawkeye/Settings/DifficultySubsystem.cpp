#include "Settings/DifficultySubsystem.h"

#include "Hawkeye.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Settings/HawkeyeSettingsSubsystem.h"

namespace HawkeyeDifficulty
{
	/** One row per stat: Story, Normal, Hard. */
	struct FRow
	{
		EDifficultyStat Stat;
		float Values[3];
	};

	static constexpr FRow Table[] = {
		{ EDifficultyStat::ThugDamage, { 0.6f, 1.0f, 1.4f } },
		{ EDifficultyStat::ThugHealth, { 0.8f, 1.0f, 1.2f } },
		{ EDifficultyStat::ArcherDrawSeconds, { 1.5f, 1.2f, 1.0f } },
		{ EDifficultyStat::ParryWindowSeconds, { 0.15f, 0.f, -0.1f } },
		{ EDifficultyStat::RegenDelaySeconds, { 3.f, 5.f, 8.f } },
		{ EDifficultyStat::FallDamage, { 0.5f, 1.0f, 1.0f } },
		{ EDifficultyStat::TrickArrowCapBonus, { 2.f, 0.f, 0.f } },
		{ EDifficultyStat::RangedTrackingLagSeconds, { 0.4f, 0.32f, 0.25f } },
		{ EDifficultyStat::RangedConeScale, { 1.4f, 1.0f, 0.7f } },
		{ EDifficultyStat::ArcherShotGapMinSeconds, { 3.0f, 1.5f, 1.2f } },
		{ EDifficultyStat::ArcherShotGapMaxSeconds, { 4.0f, 2.5f, 1.8f } },
		{ EDifficultyStat::AimAssistScale, { 1.25f, 1.0f, 0.5f } },
		{ EDifficultyStat::MeleeTokens, { 1.f, 2.f, 3.f } },
		{ EDifficultyStat::RangedTokens, { 1.f, 1.f, 2.f } },
		{ EDifficultyStat::TokenGapSeconds, { 0.9f, 0.45f, 0.3f } },
		{ EDifficultyStat::OffScreenTelegraphSeconds, { 1.2f, 1.0f, 0.8f } },
		{ EDifficultyStat::TokenGroupShare, { 0.5f, 0.5f, 0.5f } },
		{ EDifficultyStat::MeleeSeparationDegrees, { 90.f, 60.f, 45.f } },
		{ EDifficultyStat::ImpactIntervalSeconds, { 0.6f, 0.45f, 0.35f } },
		{ EDifficultyStat::HitGraceSeconds, { 0.8f, 0.6f, 0.45f } },
	};

	/** The command line's -Difficulty=, read once. */
	static const TOptional<EHawkeyeDifficulty>& CommandLineOverride()
	{
		static const TOptional<EHawkeyeDifficulty> Parsed = []() -> TOptional<EHawkeyeDifficulty>
		{
			FString Value;
			EHawkeyeDifficulty Difficulty = EHawkeyeDifficulty::Normal;
			if (FParse::Value(FCommandLine::Get(), TEXT("Difficulty="), Value)
				&& UDifficultySubsystem::ParseDifficulty(Value, Difficulty))
			{
				return Difficulty;
			}
			return {};
		}();
		return Parsed;
	}
}

UDifficultySubsystem* UDifficultySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDifficultySubsystem>() : nullptr;
}

void UDifficultySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UHawkeyeSettingsSubsystem>();
	Super::Initialize(Collection);

	if (HawkeyeDifficulty::CommandLineOverride().IsSet())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: difficulty %s from the command line (not saved)."), *GetName(),
			*GetDifficultyName(GetDifficulty()).ToString());
	}
}

EHawkeyeDifficulty UDifficultySubsystem::GetDifficulty() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UHawkeyeSettingsSubsystem* Settings = GameInstance ? GameInstance->GetSubsystem<UHawkeyeSettingsSubsystem>() : nullptr;
	return Settings ? Settings->GetDifficulty() : UHawkeyeSettingsSubsystem::GetCurrentSettings(nullptr).Difficulty;
}

float UDifficultySubsystem::GetScalar(EDifficultyStat Stat) const
{
	return GetTableValue(GetDifficulty(), Stat);
}

float UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty Difficulty, EDifficultyStat Stat)
{
	const int32 Column = FMath::Clamp(static_cast<int32>(Difficulty), 0, 2);
	for (const HawkeyeDifficulty::FRow& Row : HawkeyeDifficulty::Table)
	{
		if (Row.Stat == Stat)
		{
			return Row.Values[Column];
		}
	}
	ensureMsgf(false, TEXT("Difficulty stat %d has no row in the table."), static_cast<int32>(Stat));
	return 1.f;
}

float UDifficultySubsystem::GetScalarFor(const UObject* WorldContextObject, EDifficultyStat Stat)
{
	return GetTableValue(UHawkeyeSettingsSubsystem::GetCurrentSettings(WorldContextObject).Difficulty, Stat);
}

EHawkeyeDifficulty UDifficultySubsystem::ResolveDifficulty(EHawkeyeDifficulty Stored)
{
	const TOptional<EHawkeyeDifficulty>& Override = HawkeyeDifficulty::CommandLineOverride();
	return Override.IsSet() ? Override.GetValue() : Stored;
}

bool UDifficultySubsystem::ParseDifficulty(const FString& Text, EHawkeyeDifficulty& OutDifficulty)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("Story"), ESearchCase::IgnoreCase))
	{
		OutDifficulty = EHawkeyeDifficulty::Story;
		return true;
	}
	if (Trimmed.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
	{
		OutDifficulty = EHawkeyeDifficulty::Normal;
		return true;
	}
	if (Trimmed.Equals(TEXT("Hard"), ESearchCase::IgnoreCase))
	{
		OutDifficulty = EHawkeyeDifficulty::Hard;
		return true;
	}
	return false;
}

FText UDifficultySubsystem::GetDifficultyName(EHawkeyeDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case EHawkeyeDifficulty::Story:
		return NSLOCTEXT("Hawkeye", "DifficultyStory", "Story");
	case EHawkeyeDifficulty::Hard:
		return NSLOCTEXT("Hawkeye", "DifficultyHard", "Hard");
	default:
		return NSLOCTEXT("Hawkeye", "DifficultyNormal", "Normal");
	}
}

FText UDifficultySubsystem::GetDifficultyBlurb(EHawkeyeDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case EHawkeyeDifficulty::Story:
		return NSLOCTEXT("Hawkeye", "DifficultyStoryBlurb", "For the story. Thugs hit softer, parries forgive, health returns fast.");
	case EHawkeyeDifficulty::Hard:
		return NSLOCTEXT("Hawkeye", "DifficultyHardBlurb", "Thugs hit harder and last longer. Archers draw faster. Tighter parries.");
	default:
		return NSLOCTEXT("Hawkeye", "DifficultyNormalBlurb", "The fights as they were tuned.");
	}
}
