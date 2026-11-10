#include "Challenge/ChallengeTracker.h"

#include "Hawkeye.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"

bool UChallengeTracker::StartRun(UChallengeDefinition* Definition)
{
	if (!Definition)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: StartRun with no challenge definition."), *GetNameSafe(this));
		return false;
	}
	if (Active)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s refused; %s is already running."), *GetNameSafe(this),
			*Definition->Id.ToString(), *Active->Id.ToString());
		return false;
	}
	if (Definition->GetStepCount() == 0)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s has no %s; refusing to start it."), *GetNameSafe(this), *Definition->Id.ToString(),
			Definition->Type == EChallengeType::Archery ? TEXT("targets") : TEXT("checkpoints"));
		return false;
	}

	Active = Definition;
	ElapsedSeconds = 0.f;
	Score = 0;
	Progress = 0;
	const int32 TargetCount = Definition->Type == EChallengeType::Archery ? Definition->Targets.Num() : 0;
	TargetsDown.Init(false, TargetCount);
	TargetPoints.Init(0, TargetCount);
	UE_LOG(LogHawkeye, Log, TEXT("%s: started %s (%s, %d %s, %.0f s)."), *GetNameSafe(this), *Definition->Id.ToString(),
		*UEnum::GetValueAsString(Definition->Type), Definition->GetStepCount(),
		Definition->Type == EChallengeType::Archery ? TEXT("targets") : TEXT("checkpoints"), Definition->TimeLimitSeconds);
	return true;
}

bool UChallengeTracker::Advance(float DeltaSeconds, const FVector& PlayerLocation)
{
	if (!Active)
	{
		return false;
	}
	ElapsedSeconds += FMath::Max(DeltaSeconds, 0.f);

	// The checkpoint first: going through the last ring on the frame the clock runs out still counts.
	const int32 Next = GetNextCheckpointIndex();
	if (Next != INDEX_NONE
		&& FVector::Dist(PlayerLocation, Active->Checkpoints[Next].GetLocation()) <= Active->CheckpointRadius)
	{
		++Progress;
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s checkpoint %d of %d at %.1f s."), *GetNameSafe(this), *Active->Id.ToString(),
			Progress, Active->Checkpoints.Num(), ElapsedSeconds);
		if (Progress >= Active->Checkpoints.Num())
		{
			EndRun(EChallengeEndReason::Completed);
			return true;
		}
	}

	EChallengeEndReason Reason;
	if (UChallengeRules::CheckFailure(ElapsedSeconds, Active->TimeLimitSeconds, PlayerLocation, Active->StartLocation,
		Active->AreaRadius, Reason))
	{
		EndRun(Reason);
		return true;
	}
	return false;
}

int32 UChallengeTracker::RegisterHit(int32 TargetIndex, float RadialCm)
{
	if (!Active || Active->Type != EChallengeType::Archery || !TargetsDown.IsValidIndex(TargetIndex) || TargetsDown[TargetIndex])
	{
		return 0;
	}
	const int32 Points = UChallengeRules::ScoreHit(Active, RadialCm);
	TargetsDown[TargetIndex] = true;
	TargetPoints[TargetIndex] = Points;
	Score += Points;
	++Progress;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s target %d hit %.1f cm from centre for %d (score %d, %d of %d down, %.1f s)."),
		*GetNameSafe(this), *Active->Id.ToString(), TargetIndex, RadialCm, Points, Score, Progress, TargetsDown.Num(),
		ElapsedSeconds);
	if (Progress >= TargetsDown.Num())
	{
		EndRun(EChallengeEndReason::Completed);
	}
	return Points;
}

void UChallengeTracker::AbortRun()
{
	if (Active)
	{
		EndRun(EChallengeEndReason::Aborted);
	}
}

float UChallengeTracker::GetRemainingSeconds() const
{
	return Active ? FMath::Max(Active->TimeLimitSeconds - ElapsedSeconds, 0.f) : 0.f;
}

int32 UChallengeTracker::GetTotal() const
{
	return Active ? Active->GetStepCount() : 0;
}

int32 UChallengeTracker::GetNextCheckpointIndex() const
{
	if (!Active || Active->Type != EChallengeType::Traversal || !Active->Checkpoints.IsValidIndex(Progress))
	{
		return INDEX_NONE;
	}
	return Progress;
}

void UChallengeTracker::EndRun(EChallengeEndReason Reason)
{
	UChallengeDefinition* Definition = Active;
	Active = nullptr;
	if (!Definition)
	{
		return;
	}

	FChallengeResult Result;
	Result.ChallengeId = Definition->Id;
	Result.Type = Definition->Type;
	Result.Name = Definition->GetDisplayName();
	Result.Reason = Reason;
	Result.Seconds = ElapsedSeconds;
	Result.Score = Score;
	Result.Progress = Progress;
	Result.Total = Definition->GetStepCount();

	FChallengeRecord Record = GetRecord(Definition->Id);
	if (Reason == EChallengeEndReason::Completed)
	{
		Result.Medal = UChallengeRules::MedalForRun(Definition, Score, ElapsedSeconds);
		Result.bNewBest = UChallengeRules::IsNewBest(Definition->Type, Score, ElapsedSeconds, Record);
		if (Result.bNewBest)
		{
			Record.BestScore = Definition->Type == EChallengeType::Archery ? Score : 0;
			Record.BestSeconds = Definition->Type == EChallengeType::Traversal ? ElapsedSeconds : Record.BestSeconds;
		}
		Record.ChallengeId = Definition->Id;
		Record.BestMedal = FMath::Max(Record.BestMedal, Result.Medal);
		++Record.Completions;
		Records.Add(Definition->Id, Record);
	}
	Result.Record = Record;
	LastResult = Result;
	bHasLastResult = true;

	UE_LOG(LogHawkeye, Log, TEXT("%s: %s ended: %s after %.2f s, score %d, %d of %d, medal %s%s (best %d / %.2f s, %s)."),
		*GetNameSafe(this), *Definition->Id.ToString(), *UChallengeRules::EndReasonText(Reason).ToString(), ElapsedSeconds, Score,
		Progress, Result.Total, *UChallengeRules::MedalText(Result.Medal).ToString(), Result.bNewBest ? TEXT(", new best") : TEXT(""),
		Record.BestScore, Record.BestSeconds, *UChallengeRules::MedalText(Record.BestMedal).ToString());
	OnRunEnded.Broadcast(Result);
}

FChallengeRecord UChallengeTracker::GetRecord(FName ChallengeId) const
{
	if (const FChallengeRecord* Found = Records.Find(ChallengeId))
	{
		return *Found;
	}
	FChallengeRecord Empty;
	Empty.ChallengeId = ChallengeId;
	return Empty;
}

void UChallengeTracker::ExportRecords(TArray<FName>& OutIds, TArray<int32>& OutScores, TArray<float>& OutSeconds,
	TArray<int32>& OutMedals, TArray<int32>& OutCompletions) const
{
	OutIds.Reset();
	OutScores.Reset();
	OutSeconds.Reset();
	OutMedals.Reset();
	OutCompletions.Reset();
	TArray<FName> Ids;
	Records.GetKeys(Ids);
	Ids.Sort(FNameLexicalLess());
	for (const FName Id : Ids)
	{
		const FChallengeRecord& Record = Records[Id];
		OutIds.Add(Id);
		OutScores.Add(Record.BestScore);
		OutSeconds.Add(Record.BestSeconds);
		OutMedals.Add(static_cast<int32>(Record.BestMedal));
		OutCompletions.Add(Record.Completions);
	}
}

void UChallengeTracker::ImportRecords(const TArray<FName>& Ids, const TArray<int32>& Scores, const TArray<float>& Seconds,
	const TArray<int32>& Medals, const TArray<int32>& Completions)
{
	Records.Reset();
	const int32 Rows = FMath::Min(FMath::Min(Ids.Num(), Scores.Num()), FMath::Min(FMath::Min(Seconds.Num(), Medals.Num()), Completions.Num()));
	if (Rows != Ids.Num())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: saved challenge records are ragged (%d ids, %d kept)."), *GetNameSafe(this), Ids.Num(), Rows);
	}
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		if (Ids[Row].IsNone() || Completions[Row] <= 0)
		{
			continue;
		}
		FChallengeRecord Record;
		Record.ChallengeId = Ids[Row];
		Record.BestScore = Scores[Row];
		Record.BestSeconds = Seconds[Row];
		Record.BestMedal = static_cast<EChallengeMedal>(FMath::Clamp(Medals[Row], 0, static_cast<int32>(EChallengeMedal::Gold)));
		Record.Completions = Completions[Row];
		Records.Add(Record.ChallengeId, Record);
	}
}

void UChallengeTracker::Reset()
{
	OnRunEnded.Clear();
	Active = nullptr;
	ElapsedSeconds = 0.f;
	Score = 0;
	Progress = 0;
	TargetsDown.Reset();
	TargetPoints.Reset();
	Records.Reset();
	bHasLastResult = false;
}
