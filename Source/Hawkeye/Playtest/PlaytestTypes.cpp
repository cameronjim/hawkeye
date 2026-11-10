#include "Playtest/PlaytestTypes.h"

#include "JsonObjectConverter.h"
#include "Misc/ScopeLock.h"

void FPlaytestSummary::AddMovement(const FString& Key, float Seconds)
{
	if (Seconds > 0.f)
	{
		MovementSeconds.FindOrAdd(Key) += Seconds;
	}
}

bool FPlaytestSummary::AddStep(const FVector& From, const FVector& To)
{
	const double Step = FVector::Dist(From, To);
	if (Step > MaxStepCm)
	{
		return false;
	}
	DistanceMetres += float(Step / 100.0);
	return true;
}

void FPlaytestSummary::AddChallengeEnd(uint8 Medal)
{
	++ChallengesRun;
	switch (Medal)
	{
	case 1: ++BronzeMedals; break;
	case 2: ++SilverMedals; break;
	case 3: ++GoldMedals; break;
	default: break;
	}
}

void FPlaytestSummary::AddCrimeEnd(bool bStopped)
{
	CrimesStopped += bStopped ? 1 : 0;
}

void FPlaytestSummary::AddFightEnd(bool bWon)
{
	++Fights;
	FightsWon += bWon ? 1 : 0;
}

void FPlaytestSummary::AddMap(const FString& MapName)
{
	if (!MapName.IsEmpty())
	{
		Maps.AddUnique(MapName);
	}
}

FPlaytestFightWatch::EEnd FPlaytestFightWatch::Update(int32 AlertedNow, int32 SeenTotal, int32 SeenStanding,
	bool bPlayerStanding, double NowSeconds)
{
	if (AlertedNow > 0)
	{
		bInFight = true;
		LastAlertedSeconds = NowSeconds;
		return EEnd::None;
	}
	if (!bInFight || NowSeconds - LastAlertedSeconds < QuietSeconds)
	{
		return EEnd::None;
	}
	bInFight = false;
	return HawkeyePlaytest::IsFightWon(SeenTotal, SeenStanding, bPlayerStanding) ? EEnd::Won : EEnd::Other;
}

void FPlaytestLogRing::Add(const FString& Line)
{
	FScopeLock Guard(&Lock);
	if (Lines.Num() >= Capacity)
	{
		Lines.RemoveAt(0, Lines.Num() - Capacity + 1, EAllowShrinking::No);
	}
	Lines.Add(Line);
}

TArray<FString> FPlaytestLogRing::Snapshot() const
{
	FScopeLock Guard(&Lock);
	return Lines;
}

namespace HawkeyePlaytest
{
	FString MakeSessionName(const FDateTime& LocalTime)
	{
		return LocalTime.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	}

	FString SanitizeSessionName(const FString& Raw)
	{
		FString Out;
		for (const TCHAR C : Raw)
		{
			if (FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_'))
			{
				Out.AppendChar(C);
			}
		}
		return Out.Left(64);
	}

	static const TCHAR* YesNo(bool b)
	{
		return b ? TEXT("yes") : TEXT("no");
	}

	static FString OrDash(const FString& S)
	{
		return S.IsEmpty() ? FString(TEXT("-")) : S.Replace(TEXT(" "), TEXT("_"));
	}

	FString DescribeState(const FPlaytestState& S)
	{
		return FString::Printf(
			TEXT("move=%s speed=%.0f sprint=%s crouch=%s parkour=%s zip=%s aim=%s downed=%s health=%.0f fight=%d ")
			TEXT("crime=%s challenge=%s interior=%s map=%s tod=%s difficulty=%s paused=%s"),
			*OrDash(S.Movement), S.Speed, YesNo(S.bSprinting), YesNo(S.bCrouching), *OrDash(S.Parkour), YesNo(S.bZipping),
			YesNo(S.bAiming), YesNo(S.bDowned), S.Health, S.AlertedThugs, *OrDash(S.Crime), *OrDash(S.Challenge),
			YesNo(S.bInterior), *OrDash(S.Map), *OrDash(S.TimeOfDay), *OrDash(S.Difficulty), YesNo(S.bPaused));
	}

	FString FormatNoteLine(const FPlaytestNote& Note)
	{
		return FString::Printf(TEXT("NOTE #%d at %.1fs: pos=(%.0f,%.0f,%.0f) yaw=%.0f state=%s"), Note.Index,
			Note.SessionSeconds, Note.Position.X, Note.Position.Y, Note.Position.Z, Note.Yaw, *DescribeState(Note.State));
	}

	FString MovementKey(const FPlaytestState& S)
	{
		if (S.bPaused)
		{
			return TEXT("Paused");
		}
		if (S.bDowned)
		{
			return TEXT("Downed");
		}
		if (S.bZipping)
		{
			return TEXT("Zip");
		}
		if (!S.Parkour.IsEmpty() && S.Parkour != TEXT("None"))
		{
			return S.Parkour;
		}
		if (S.Movement == TEXT("Falling"))
		{
			return TEXT("Air");
		}
		if (S.bCrouching)
		{
			return TEXT("Crouch");
		}
		if (S.bSprinting)
		{
			return TEXT("Sprint");
		}
		if (S.Speed > 300.f)
		{
			return TEXT("Run");
		}
		if (S.Speed > 20.f)
		{
			return TEXT("Walk");
		}
		return TEXT("Idle");
	}

	bool IsFightWon(int32 SeenTotal, int32 SeenStanding, bool bPlayerStanding)
	{
		return SeenTotal > 0 && SeenStanding == 0 && bPlayerStanding;
	}

	FString NotesToJson(const FPlaytestNotesFile& File)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(File, Json);
		return Json;
	}

	bool NotesFromJson(const FString& Json, FPlaytestNotesFile& OutFile)
	{
		return FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutFile);
	}

	FString SummaryToJson(const FPlaytestSummary& Summary)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(Summary, Json);
		return Json;
	}

	bool SummaryFromJson(const FString& Json, FPlaytestSummary& OutSummary)
	{
		return FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutSummary);
	}
}
