#include "Combat/HealthComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Phone/PhoneInbox.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/NarrativeScreenshots.h"
#include "Tests/PartnerScreenshots.h"
#include "UI/ChapterTitleWidget.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UI/PhoneWidget.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeNarrativeShots
{
	static const TCHAR* SceneMapName = TEXT("L_Scene_Placeholder");

	/** The order the chapter end reached each beat in, filled as the pass watches it. */
	struct FState
	{
		TArray<FString> Beats;
		TWeakObjectPtr<UWorld> DistrictWorld;
		bool bEndCardShot = false;
	};

	static UWorld* FindWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	static AHawkeyePlayerController* FindController(UWorld* World)
	{
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static AHawkeyeCharacter* FindPlayer(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	static bool IsSceneWorld(const UWorld* World)
	{
		return World && World->GetMapName().Contains(SceneMapName);
	}

	static void TakeShot(FAutomationTestBase* Test, const FString& FileName)
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
		HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	}

	/** Kate at Location facing Yaw with the camera pitched Pitch, safe from everything. */
	static void Place(UWorld* World, const FVector& Location, float Yaw, float Pitch)
	{
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Kate)
		{
			return;
		}
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Kate->TeleportTo(Location, FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
	}

	static void FreezeThugs(UWorld* World)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		HawkeyeFreezePartner(World);
	}

	/** Records Beat once, the first frame it is seen. */
	static void Note(FState& State, const FString& Beat)
	{
		if (State.Beats.IsEmpty() || State.Beats.Last() != Beat)
		{
			State.Beats.Add(Beat);
		}
	}

	/** What the chapter end is doing right now, as a beat name (empty when nothing). */
	static FString CurrentBeat(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		if (IsSceneWorld(World))
		{
			return TEXT("Scene");
		}
		if (!PC)
		{
			return FString();
		}
		if (PC->IsCloseUpActive())
		{
			return TEXT("CloseUp");
		}
		const UMissionFlowController* Flow = PC->GetMissionFlow();
		if (Flow && Flow->GetStep() == EMissionFlowStep::EndCard)
		{
			return TEXT("EndCard");
		}
		if (PC->IsFlashbackActive())
		{
			return TEXT("Flashback");
		}
		return FString();
	}
}

void HawkeyeAddNarrativeShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeNarrativeShots;
	TSharedRef<FState> State = MakeShared<FState>();

	// After the save pass's death the district has just reloaded: wait for it to settle.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = FindWorld();
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		return World && Save && !Save->IsLoading() && FindPlayer(World);
	}, [Test]() { Test->AddError(TEXT("narrative shots: no district to start in.")); return true; }, 30.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		FreezeThugs(World);
		if (AHawkeyePlayerController* PC = FindController(World))
		{
			PC->HideMainMenu();
		}
		const APlayerStart* Start = TActorIterator<APlayerStart>(World) ? *TActorIterator<APlayerStart>(World) : nullptr;
		if (Start)
		{
			Place(World, Start->GetActorLocation(), Start->GetActorRotation().Yaw, -8.f);
		}
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		Test->TestEqual(TEXT("Chapter 1 is Night Shift, its first objective chase_thief"),
			Missions && Missions->GetCurrentObjective() ? Missions->GetCurrentObjective()->ObjectiveId : NAME_None, FName(TEXT("chase_thief")));
		return true;
	}));

	// The chapter title card, 1.5 s in: faded up and holding.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		const UChapterTitleWidget* Title = PC && Missions ? PC->ShowChapterTitle(Missions->GetCurrentMission()) : nullptr;
		Test->TestTrue(TEXT("The title card shows"), Title && Title->IsPlaying());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]() { TakeShot(Test, TEXT("chapter_title.png")); return true; }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		const AHawkeyePlayerController* PC = FindController(FindWorld());
		Test->TestFalse(TEXT("The title card is gone after 4.5 s"), PC && PC->IsChapterTitleShowing());
		return true;
	}));

	// A scripted line, Night Shift's opening (a draft): the subtitle with its speaker.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(FindWorld());
		Test->TestTrue(TEXT("A DT_Dialogue line plays by name"), Dialogue && Dialogue->PlayLine(TEXT("seq_ch01_open_01")));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(FindWorld());
		Test->TestTrue(TEXT("The subtitle is up"), Hud && Hud->IsSubtitleVisible());
		Test->AddInfo(FString::Printf(TEXT("subtitle: \"%s\""), Hud ? *Hud->GetSubtitleShown().ToString() : TEXT("-")));
		TakeShot(Test, TEXT("dialogue_subtitle.png"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.5f));

	// The chapter's end, as the chase's catch completes it (the catch itself is the campaign lap's): the end card
	// with Kate's last line, then back to roaming with the toast. No close-up, flashback or scene any more.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
	{
		UWorld* World = FindWorld();
		State->DistrictWorld = World;
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(World))
		{
			for (const TCHAR* Id : { TEXT("chase_thief"), TEXT("cross_gap"), TEXT("catch_thief") })
			{
				Missions->CompleteObjective(Id);
			}
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Test, State]()
	{
		UWorld* World = FindWorld();
		const AHawkeyePlayerController* PC = FindController(World);
		const FString Beat = CurrentBeat(World);
		if (!Beat.IsEmpty())
		{
			Note(*State, Beat);
		}
		if (Beat == TEXT("EndCard") && !State->bEndCardShot)
		{
			State->bEndCardShot = true;
			TakeShot(Test, TEXT("chapter_end_card.png"));
		}
		const UMissionFlowController* Flow = PC ? PC->GetMissionFlow() : nullptr;
		return State->bEndCardShot && Flow && !Flow->IsRunning();
	}, [Test]() { Test->AddError(TEXT("narrative shots: the chapter end never came back to roaming.")); return true; }, 30.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
	{
		UWorld* World = FindWorld();
		const AHawkeyePlayerController* PC = FindController(World);
		const UHawkeyeObjectiveWidget* Toasts = PC && PC->GetHawkeyeHud() ? PC->GetHawkeyeHud()->GetObjectiveMarker() : nullptr;
		if (Toasts && Toasts->IsToastVisible() && Toasts->GetToastHeading().ToString() == TEXT("[Chapter complete]"))
		{
			Note(*State, TEXT("Toast"));
		}
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		Test->TestTrue(TEXT("Every objective is complete"), Missions && !Missions->GetCurrentObjective());
		const FString Order = FString::Join(State->Beats, TEXT(" -> "));
		Test->AddInfo(TEXT("chapter end order: ") + Order);
		Test->TestEqual(TEXT("End card, then the toast"), Order, FString(TEXT("EndCard -> Toast")));
		TakeShot(Test, TEXT("chapter_complete.png"));
		return true;
	}));

	// The phone after the chapter: Clint's texts and Kate's replies in his thread, the landlord's.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(FindWorld());
		return Phone && Phone->GetInbox() && Phone->GetInbox()->GetArrivedCount() >= 7;
	}, [Test]() { Test->AddError(TEXT("narrative shots: Chapter 1's seven texts did not arrive in 40 s.")); return true; }, 40.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
		Test->TestTrue(TEXT("Some are unread"), Phone && Phone->GetUnreadCount() > 0);
		const TArray<FString> Contacts = Phone && Phone->GetInbox() ? Phone->GetInbox()->GetContacts() : TArray<FString>();
		Test->TestFalse(TEXT("Kate's replies sit in the threads they answer"), Contacts.Contains(TEXT("Kate")));
		Test->AddInfo(FString::Printf(TEXT("phone: contacts %s, %d unread before opening."), *FString::Join(Contacts, TEXT(", ")),
			Phone ? Phone->GetUnreadCount() : -1));
		if (AHawkeyePlayerController* PC = FindController(World))
		{
			PC->SetPhoneOpen(true);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]() { TakeShot(Test, TEXT("phone_open.png")); return true; }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		const UPhoneWidget* Phone = PC ? PC->GetPhoneWidget() : nullptr;
		Test->TestTrue(TEXT("The phone is open with a thread showing"), PC && PC->IsPhoneOpen() && Phone && Phone->GetThreadLength() > 0);
		if (PC)
		{
			PC->SetPhoneOpen(false);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
}

#endif
