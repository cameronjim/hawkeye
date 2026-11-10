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
#include "Mission/ObjectiveTriggerVolume.h"
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
#include "World/ChapterEndInteractable.h"
#include "World/InteractionComponent.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeNarrativeShots
{
	static const FName ReturnPoint(TEXT("City_SceneReturn_FB00"));
	static const TCHAR* SceneMapName = TEXT("L_Scene_Placeholder");

	/** The order the chapter end reached each beat in, filled as the pass watches it. */
	struct FState
	{
		TArray<FString> Beats;
		TWeakObjectPtr<UWorld> DistrictWorld;
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

	static AActor* FindReturnPoint(UWorld* World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(ReturnPoint))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static AChapterEndInteractable* FindChapterEnd(UWorld* World)
	{
		TActorIterator<AChapterEndInteractable> It(World);
		return It ? *It : nullptr;
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

	// The phone: complete the first three objectives so their texts arrive, then open it.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		FreezeThugs(World);
		if (AHawkeyePlayerController* PC = FindController(World))
		{
			PC->HideMainMenu();
		}
		UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		for (const TCHAR* Id : { TEXT("reach_roof"), TEXT("cross_block"), TEXT("clear_roof") })
		{
			Missions->CompleteObjective(Id);
		}
		const APlayerStart* Start = TActorIterator<APlayerStart>(World) ? *TActorIterator<APlayerStart>(World) : nullptr;
		if (Start)
		{
			Place(World, Start->GetActorLocation(), Start->GetActorRotation().Yaw, -8.f);
		}
		Test->TestEqual(TEXT("find_arrow is now the current objective"),
			Missions->GetCurrentObjective() ? Missions->GetCurrentObjective()->ObjectiveId : NAME_None, FName(TEXT("find_arrow")));
		return true;
	}));
	// msg_ch01_04 waits 8 s after find_arrow becomes current.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(9.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(World);
		Test->TestTrue(TEXT("Every CH01 text has arrived"), Phone && Phone->GetInbox()->GetArrivedCount() == 4);
		Test->TestTrue(TEXT("And some are unread"), Phone && Phone->GetUnreadCount() > 0);
		Test->AddInfo(FString::Printf(TEXT("phone: %d arrived, %d unread before opening."),
			Phone ? Phone->GetInbox()->GetArrivedCount() : 0, Phone ? Phone->GetUnreadCount() : 0));
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
		Test->AddInfo(FString::Printf(TEXT("phone: %d contacts, %s selected, %d unread after opening."),
			Phone ? Phone->GetContactCount() : 0, Phone ? *Phone->GetSelectedContact() : TEXT("-"),
			UPhoneSubsystem::Get(World) ? UPhoneSubsystem::Get(World)->GetUnreadCount() : -1));
		if (PC)
		{
			PC->SetPhoneOpen(false);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

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

	// A scripted line: the subtitle with its speaker.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(FindWorld());
		Test->TestTrue(TEXT("A DT_Dialogue line plays by name"), Dialogue && Dialogue->PlayLine(TEXT("seq_ch01_open_02")));
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
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.5f));

	// The chapter end: at the return point in front of the tower, looking up at the arrow; examine it.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
	{
		UWorld* World = FindWorld();
		const AActor* Point = FindReturnPoint(World);
		const AChapterEndInteractable* Arrow = FindChapterEnd(World);
		if (!Point || !Arrow)
		{
			Test->AddError(TEXT("narrative shots: no City_SceneReturn_FB00 or City_ChapterEnd."));
			return true;
		}
		const FVector ToArrow = Arrow->GetActorLocation() - Point->GetActorLocation();
		Place(World, Point->GetActorLocation(), ToArrow.Rotation().Yaw, 20.f);
		State->DistrictWorld = World;
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
	{
		UWorld* World = FindWorld();
		AHawkeyeCharacter* Kate = FindPlayer(World);
		AChapterEndInteractable* Arrow = FindChapterEnd(World);
		UInteractionComponent* Interaction = Kate ? Kate->GetInteractionComponent() : nullptr;
		Test->TestTrue(TEXT("The arrow offers itself to Kate by the tower"), Arrow && Kate && IInteractable::Execute_CanInteract(Arrow, Kate));
		const bool bInteracted = Interaction && Interaction->TryInteract() && Interaction->GetFocusedActor() == nullptr;
		Test->AddInfo(FString::Printf(TEXT("chapter end: interaction component %s."), bInteracted ? TEXT("examined it") : TEXT("missed it; examining directly")));
		if (Arrow && !Arrow->HasBeenUsed())
		{
			IInteractable::Execute_Interact(Arrow, Kate);
		}
		Note(*State, TEXT("Interact"));
		Note(*State, CurrentBeat(World));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
	{
		const AHawkeyePlayerController* PC = FindController(FindWorld());
		Test->TestTrue(TEXT("The close-up is running"), PC && PC->IsCloseUpActive());
		TakeShot(Test, TEXT("chapter_end_closeup.png"));
		return true;
	}));

	// The rest of the chapter end plays itself: watch the beats until the scene map is up.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
	{
		UWorld* World = FindWorld();
		const FString Beat = CurrentBeat(World);
		if (!Beat.IsEmpty())
		{
			Note(*State, Beat);
		}
		return IsSceneWorld(World) && FindPlayer(World) != nullptr;
	}, [Test]() { Test->AddError(TEXT("narrative shots: the playable scene never opened.")); return true; }, 60.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		Test->TestTrue(TEXT("In the scene, saves are refused"), Save && Save->IsInPlayableScene());
		Test->TestTrue(TEXT("The scene runs its own mission"), Missions && Missions->GetCurrentMission()
			&& Missions->GetCurrentMission()->GetName() == TEXT("DA_Scene_Placeholder"));
		if (AHawkeyeCharacter* Kate = FindPlayer(World))
		{
			if (AHawkeyePlayerController* PC = FindController(World))
			{
				PC->SetControlRotation(FRotator(-6.f, Kate->GetActorRotation().Yaw, 0.f));
			}
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]() { TakeShot(Test, TEXT("scene_placeholder.png")); return true; }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	// Walk into the scene's objective: its mission completes and the district comes back.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UWorld* World = FindWorld();
		TActorIterator<AObjectiveTriggerVolume> Volume(World);
		if (AHawkeyeCharacter* Kate = FindPlayer(World); Kate && Volume)
		{
			Kate->TeleportTo(Volume->GetActorLocation(), Kate->GetActorRotation());
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
	{
		UWorld* World = FindWorld();
		const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(World);
		return World && !IsSceneWorld(World) && World != State->DistrictWorld.Get() && Save && !Save->IsLoading()
			&& !Save->GetSceneReturn().bReturnPending && FindPlayer(World);
	}, [Test]() { Test->AddError(TEXT("narrative shots: the district never came back from the scene.")); return true; }, 60.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
	{
		UWorld* World = FindWorld();
		const AHawkeyeCharacter* Kate = FindPlayer(World);
		const AActor* Point = FindReturnPoint(World);
		const float Off = Kate && Point ? FVector::Dist2D(Kate->GetActorLocation(), Point->GetActorLocation()) : -1.f;
		Test->TestTrue(FString::Printf(TEXT("Kate is back at City_SceneReturn_FB00 (%.0f cm off)"), Off), Off >= 0.f && Off < 100.f);
		const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(World);
		const UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr;
		const bool bToast = Toasts && Toasts->IsToastVisible() && Toasts->GetToastHeading().ToString() == TEXT("[Chapter complete]");
		if (bToast)
		{
			Note(*State, TEXT("Toast"));
		}
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
		Test->TestTrue(TEXT("Every objective is complete in the restored district"), Missions && !Missions->GetCurrentObjective());
		const FString Order = FString::Join(State->Beats, TEXT(" -> "));
		Test->AddInfo(TEXT("chapter end order: ") + Order);
		Test->TestEqual(TEXT("Interact, close-up, end card, flashback, scene, then the toast"), Order,
			FString(TEXT("Interact -> CloseUp -> EndCard -> Flashback -> Scene -> Toast")));
		TakeShot(Test, TEXT("chapter_complete.png"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
}

#endif
