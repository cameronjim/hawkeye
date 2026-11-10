#include "UI/HawkeyeHints.h"

#include "Player/HawkeyeSprintToggle.h"

namespace HawkeyeHints
{
	static FHawkeyeHintRule Make(const TCHAR* Id, const FText& Text, EHawkeyeHintTrigger Trigger, EHawkeyeHintAction Dismiss,
		int32 Priority, float HoldSeconds = 0.f, bool bUrgent = false)
	{
		FHawkeyeHintRule Rule;
		Rule.Id = FName(Id);
		Rule.Text = Text;
		Rule.Trigger = Trigger;
		Rule.Dismiss = Dismiss;
		Rule.Priority = Priority;
		Rule.HoldSeconds = HoldSeconds;
		Rule.bUrgent = bUrgent;
		return Rule;
	}

	/** Token, key name, pad name. */
	struct FKeyName
	{
		const TCHAR* Token;
		const TCHAR* Keys;
		const TCHAR* Pad;
	};

	// The bindings in Tools/Editor/create_input_assets.py and the controller table in gameplay-semantics.md.
	static const FKeyName KeyNames[] = {
		{ TEXT("Sprint"), TEXT("Shift"), TEXT("L3") },
		{ TEXT("Jump"), TEXT("Space"), TEXT("A") },
		{ TEXT("Grapple"), TEXT("Q"), TEXT("RB") },
		{ TEXT("Draw"), TEXT("LMB"), TEXT("RT") },
		{ TEXT("Aim"), TEXT("RMB"), TEXT("LT") },
		{ TEXT("Strike"), TEXT("V"), TEXT("X") },
		{ TEXT("Dodge"), TEXT("Ctrl"), TEXT("B") },
		{ TEXT("Crouch"), TEXT("Ctrl"), TEXT("B") },
		{ TEXT("Interact"), TEXT("E"), TEXT("Y") },
		{ TEXT("Takedown"), TEXT("F"), TEXT("Y") },
		{ TEXT("Phone"), TEXT("P"), TEXT("Hold D-pad Down") },
		{ TEXT("Map"), TEXT("M"), TEXT("Hold D-pad Up") },
		{ TEXT("Switch"), TEXT("X"), TEXT("LB") },
		{ TEXT("Wheel"), TEXT("Tab"), TEXT("View") },
	};
}

UHawkeyeHintRules::UHawkeyeHintRules()
{
	using namespace HawkeyeHints;
	using T = EHawkeyeHintTrigger;
	using A = EHawkeyeHintAction;
	// Priority: a fight's hints first (the parry and the dodge can't wait), then the moves in front of her, then
	// the ones about the city. Every line is short, present tense, the key first where it can be.
	FHawkeyeHintRule Sprint = Make(TEXT("sprint"), NSLOCTEXT("Hawkeye", "HintSprint", "Hold {Sprint} to sprint"),
		T::WalkingOpenGround, A::Sprinted, 40, 3.f);
	Sprint.ToggleText = NSLOCTEXT("Hawkeye", "HintSprintToggle", "Press {Sprint} to sprint");
	Rules = {
		Make(TEXT("parry"), NSLOCTEXT("Hawkeye", "HintParry", "{Strike} as the ! flashes to parry"), T::ParryTelegraph,
			A::Parried, 100, 0.f, true),
		Make(TEXT("dodge"), NSLOCTEXT("Hawkeye", "HintDodge", "Tap {Dodge} to dodge"), T::TookHit, A::Dodged, 95, 0.f, true),
		Make(TEXT("finisher"), NSLOCTEXT("Hawkeye", "HintFinisher", "{Takedown} to finish him"), T::FinisherOpen,
			A::Finished, 90),
		Make(TEXT("takedown"), NSLOCTEXT("Hawkeye", "HintTakedown", "{Takedown} from behind to take him down"),
			T::TakedownReady, A::TookDown, 85),
		Make(TEXT("strike"), NSLOCTEXT("Hawkeye", "HintStrike", "{Strike} to strike, hold for heavy"), T::ThugClose,
			A::Struck, 80),
		Make(TEXT("draw"), NSLOCTEXT("Hawkeye", "HintDraw", "{Aim} then {Draw}, release to fire"), T::ThugAtRange,
			A::FiredArrow, 70),
		Make(TEXT("switch"), NSLOCTEXT("Hawkeye", "HintSwitch", "{Switch} to play as {Partner}"), T::PartnerNearInFight,
			A::Switched, 60),
		Make(TEXT("chain"), NSLOCTEXT("Hawkeye", "HintChain", "{Grapple} again mid-zip to chain"), T::ChainAvailable,
			A::Chained, 55),
		Make(TEXT("grapple"), NSLOCTEXT("Hawkeye", "HintGrapple", "{Grapple} to zip"), T::GrappleReady, A::Zipped, 50),
		Make(TEXT("vault"), NSLOCTEXT("Hawkeye", "HintVault", "{Jump} or sprint into it to vault"), T::NearVaultable,
			A::Vaulted, 45),
		Make(TEXT("climb"), NSLOCTEXT("Hawkeye", "HintClimb", "{Jump} at the wall to climb it"), T::FacingClimbableWall,
			A::Climbed, 44),
		Make(TEXT("hang"), NSLOCTEXT("Hawkeye", "HintHang", "{Crouch} at the edge to drop and hang"), T::AtRoofEdge,
			A::DroppedToHang, 42),
		Sprint,
		Make(TEXT("crime"), NSLOCTEXT("Hawkeye", "HintCrime", "Follow the red ! to stop the crime"), T::CrimeActive,
			A::None, 35),
		Make(TEXT("wheel"), NSLOCTEXT("Hawkeye", "HintWheel", "Hold {Wheel} to pick a trick arrow"), T::TrickArrowGained,
			A::OpenedWheel, 34),
		Make(TEXT("phone"), NSLOCTEXT("Hawkeye", "HintPhone", "{Phone} to read the text"), T::UnreadText, A::OpenedPhone, 30),
		Make(TEXT("safehouse"), NSLOCTEXT("Hawkeye", "HintSafehouse", "{Interact} at the door to use the safehouse"),
			T::NearSafehouse, A::EnteredSafehouse, 28),
		Make(TEXT("challenge"), NSLOCTEXT("Hawkeye", "HintChallenge", "{Interact} at the pedestal to start a challenge"),
			T::NearPedestal, A::StartedChallenge, 26),
		Make(TEXT("fasttravel"), NSLOCTEXT("Hawkeye", "HintFastTravel", "Fast travel from either safehouse's menu"),
			T::BothSafehousesFound, A::FastTravelled, 24),
		Make(TEXT("map"), NSLOCTEXT("Hawkeye", "HintMap", "{Map} for the map"), T::FarWithoutMarker, A::OpenedMap, 20),
	};
}

const TArray<FHawkeyeHintRule>& UHawkeyeHintRules::GetDefaultRules()
{
	return GetDefault<UHawkeyeHintRules>()->Rules;
}

const FHawkeyeHintRule* UHawkeyeHintRules::FindRule(const TArray<FHawkeyeHintRule>& InRules, FName Id)
{
	return InRules.FindByPredicate([Id](const FHawkeyeHintRule& Rule) { return Rule.Id == Id; });
}

FString UHawkeyeHintRules::GetKeyName(const FString& Token, bool bGamepad)
{
	for (const HawkeyeHints::FKeyName& Name : HawkeyeHints::KeyNames)
	{
		if (Token.Equals(Name.Token, ESearchCase::IgnoreCase))
		{
			return bGamepad ? Name.Pad : Name.Keys;
		}
	}
	return FString();
}

TArray<FHawkeyeHintSegment> UHawkeyeHintRules::ResolveLine(const FString& Line, const FHawkeyeHintContext& Context)
{
	TArray<FHawkeyeHintSegment> Segments;
	auto AddText = [&Segments](const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		if (Segments.Num() > 0 && !Segments.Last().bKey)
		{
			Segments.Last().Text += Text;
		}
		else
		{
			Segments.Add({ Text, false });
		}
	};

	int32 Cursor = 0;
	while (Cursor < Line.Len())
	{
		const int32 Open = Line.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
		const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Line.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
		if (Open == INDEX_NONE || Close == INDEX_NONE)
		{
			AddText(Line.Mid(Cursor));
			break;
		}
		AddText(Line.Mid(Cursor, Open - Cursor));
		const FString Token = Line.Mid(Open + 1, Close - Open - 1);
		const FString Key = GetKeyName(Token, Context.bGamepad);
		if (!Key.IsEmpty())
		{
			Segments.Add({ Key, true });
		}
		else if (Token.Equals(TEXT("Partner"), ESearchCase::IgnoreCase))
		{
			AddText(Context.PartnerName);
		}
		else
		{
			AddText(Line.Mid(Open, Close - Open + 1));
		}
		Cursor = Close + 1;
	}
	return Segments;
}

TArray<FHawkeyeHintSegment> UHawkeyeHintRules::Resolve(const FHawkeyeHintRule& Rule, const FHawkeyeHintContext& Context)
{
	const bool bToggle = !Rule.ToggleText.IsEmpty() && FHawkeyeSprintToggle::UsesToggle(Context.SprintMode, Context.bGamepad);
	return ResolveLine((bToggle ? Rule.ToggleText : Rule.Text).ToString(), Context);
}

FString UHawkeyeHintRules::ResolveToString(const FHawkeyeHintRule& Rule, const FHawkeyeHintContext& Context)
{
	FString Out;
	for (const FHawkeyeHintSegment& Segment : Resolve(Rule, Context))
	{
		Out += Segment.bKey ? FString::Printf(TEXT("[%s]"), *Segment.Text) : Segment.Text;
	}
	return Out;
}

// --- Queue ------------------------------------------------------------------------------------------

void FHawkeyeHintQueue::Reset(const TArray<FHawkeyeHintRule>& InRules)
{
	Rules = &InRules;
	States.Reset();
	States.SetNum(InRules.Num());
	CurrentIndex = INDEX_NONE;
	CurrentAge = 0.f;
	bVisible = false;
}

const FHawkeyeHintRule* FHawkeyeHintQueue::GetCurrent() const
{
	return Rules && Rules->IsValidIndex(CurrentIndex) ? &(*Rules)[CurrentIndex] : nullptr;
}

TArray<FName> FHawkeyeHintQueue::GetWaiting() const
{
	TArray<int32> Waiting;
	for (int32 Index = 0; Rules && Index < States.Num(); ++Index)
	{
		if (States[Index].bArmed && Index != CurrentIndex)
		{
			Waiting.Add(Index);
		}
	}
	Waiting.StableSort([this](int32 A, int32 B) { return (*Rules)[A].Priority > (*Rules)[B].Priority; });
	TArray<FName> Ids;
	for (const int32 Index : Waiting)
	{
		Ids.Add((*Rules)[Index].Id);
	}
	return Ids;
}

void FHawkeyeHintQueue::EndCurrent(FHawkeyeHintStep& Step, TFunctionRef<int32(FName)> GetShows)
{
	if (!Rules || !Rules->IsValidIndex(CurrentIndex))
	{
		CurrentIndex = INDEX_NONE;
		return;
	}
	const FHawkeyeHintRule& Rule = (*Rules)[CurrentIndex];
	FRuleState& State = States[CurrentIndex];
	Step.Ended = Rule.Id;
	State.bArmed = false;
	// Its show was counted when it came up (Started): the saved count says whether any are left.
	if (GetShows(Rule.Id) >= Rule.MaxShows)
	{
		State.bDone = true;
	}
	else
	{
		State.bNeedsRearm = true;
	}
	CurrentIndex = INDEX_NONE;
	CurrentAge = 0.f;
}

FHawkeyeHintStep FHawkeyeHintQueue::Tick(float DeltaSeconds, uint64 Triggers, uint64 Actions, bool bSuppressed, bool bEnabled,
	TFunctionRef<int32(FName)> GetShows)
{
	FHawkeyeHintStep Step;
	if (!Rules || States.Num() != Rules->Num())
	{
		return Step;
	}
	const float Dt = FMath::Max(DeltaSeconds, 0.f);

	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		const FHawkeyeHintRule& Rule = (*Rules)[Index];
		FRuleState& State = States[Index];
		if (State.bDone)
		{
			continue;
		}

		// Doing the thing is the lesson, shown or not.
		if (Rule.Dismiss != EHawkeyeHintAction::None && (Actions & Bit(Rule.Dismiss)) != 0)
		{
			if (Index == CurrentIndex)
			{
				Step.Ended = Rule.Id;
				CurrentIndex = INDEX_NONE;
				CurrentAge = 0.f;
			}
			Step.Learned.Add(Rule.Id);
			State = FRuleState();
			State.bDone = true;
			continue;
		}

		const bool bHolds = Rule.Trigger != EHawkeyeHintTrigger::None && (Triggers & Bit(Rule.Trigger)) != 0;
		if (bSuppressed)
		{
			// Every clock stops; a trigger that fires under a menu still arms, and waits for it to close.
			if (bHolds)
			{
				State.SinceClearSeconds = 0.f;
			}
		}
		else if (bHolds)
		{
			State.HeldSeconds += Dt;
			State.SinceClearSeconds = 0.f;
		}
		else
		{
			State.HeldSeconds = 0.f;
			State.SinceClearSeconds += Dt;
		}

		if (!bHolds)
		{
			State.bNeedsRearm = false;
		}
		const bool bShowsLeft = Index == CurrentIndex || GetShows(Rule.Id) < Rule.MaxShows;
		if (!bShowsLeft)
		{
			State.bDone = true;
			State.bArmed = false;
			continue;
		}
		if (bHolds && bEnabled && !State.bNeedsRearm && State.HeldSeconds >= Rule.HoldSeconds)
		{
			State.bArmed = true;
		}
		if (State.bArmed && !bHolds && State.SinceClearSeconds > Rule.LingerSeconds)
		{
			if (Index == CurrentIndex)
			{
				EndCurrent(Step, GetShows);
			}
			State.bArmed = false;
		}
	}

	if (!bEnabled)
	{
		// Off: nothing waits either, so turning them back on starts clean.
		if (CurrentIndex != INDEX_NONE)
		{
			EndCurrent(Step, GetShows);
		}
		for (FRuleState& State : States)
		{
			State.bArmed = false;
		}
		bVisible = false;
		return Step;
	}
	if (bSuppressed)
	{
		bVisible = false;
		return Step;
	}

	// The most relevant armed rule; ties go to the table's order.
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		if (States[Index].bArmed && Index != CurrentIndex && (Best == INDEX_NONE || (*Rules)[Index].Priority > (*Rules)[Best].Priority))
		{
			Best = Index;
		}
	}
	if (CurrentIndex != INDEX_NONE && Best != INDEX_NONE)
	{
		const FHawkeyeHintRule& Challenger = (*Rules)[Best];
		if (Challenger.Priority > (*Rules)[CurrentIndex].Priority && (Challenger.bUrgent || CurrentAge >= MinShowSeconds))
		{
			EndCurrent(Step, GetShows);
		}
	}
	if (CurrentIndex == INDEX_NONE && Best != INDEX_NONE)
	{
		CurrentIndex = Best;
		CurrentAge = 0.f;
		Step.Started = (*Rules)[Best].Id;
	}
	else if (CurrentIndex != INDEX_NONE)
	{
		CurrentAge += Dt;
	}
	bVisible = CurrentIndex != INDEX_NONE;
	return Step;
}
