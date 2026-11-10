#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformFileManager.h"
#include "HawkeyePlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

/**
 * What the interior AI tests and the interior laps share (InteriorAITest.cpp, InteriorLapTest.cpp): finding
 * the world, Kate and the sample's enemies (tagged enemy:<id> by generate_interior.py), injecting input,
 * and the sample layout's own numbers (Tools/Interiors/Sample.json; change them with it).
 */
namespace HawkeyeInteriorKit
{
	inline const TCHAR* MapPath = TEXT("/Game/Maps/L_Int_Sample");
	inline const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	inline const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	inline const TCHAR* CrouchPath = TEXT("/Game/Input/IA_Crouch.IA_Crouch");
	inline const TCHAR* TakedownPath = TEXT("/Game/Input/IA_Takedown.IA_Takedown");
	inline const TCHAR* MeleePath = TEXT("/Game/Input/IA_Melee.IA_Melee");
	inline const TCHAR* FirePath = TEXT("/Game/Input/IA_Fire.IA_Fire");
	inline const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	inline constexpr float FloorHeight = 330.f;

	/** The sample's rooms, flat (Sample.json). The gallery is the hall's west strip on floor 1. */
	inline const FBox2D Lobby(FVector2D(0.f, 0.f), FVector2D(800.f, 600.f));
	inline const FBox2D Office(FVector2D(0.f, 600.f), FVector2D(500.f, 1400.f));
	inline const FBox2D Stair(FVector2D(500.f, 600.f), FVector2D(800.f, 1220.f));
	inline const FBox2D Hall(FVector2D(800.f, 0.f), FVector2D(2200.f, 1400.f));
	inline const FBox2D Gallery(FVector2D(800.f, 0.f), FVector2D(1200.f, 1400.f));
	inline const FBox2D Vault(FVector2D(2200.f, 900.f), FVector2D(2600.f, 1400.f));

	inline UWorld* FindWorld()
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

	inline AHawkeyePlayerController* FindController(UWorld* World)
	{
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	inline AHawkeyeCharacter* FindPlayer(UWorld* World)
	{
		const AHawkeyePlayerController* PC = FindController(World);
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	inline bool IsMap(const UWorld* World, const TCHAR* Path)
	{
		return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Path;
	}

	template <typename T>
	TArray<T*> All(UWorld* World)
	{
		TArray<T*> Out;
		for (TActorIterator<T> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		return Out;
	}

	/** The sample's enemy with layout id Id: lobby, hall, gallery or vault. */
	inline AThugCharacter* Enemy(UWorld* World, const TCHAR* Id)
	{
		const FName Tag(*FString::Printf(TEXT("enemy:%s"), Id));
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	inline AThugAIController* BrainOf(const AThugCharacter* Thug)
	{
		return Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr;
	}

	inline FVector Feet(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	inline bool IsDown(const AThugCharacter* Thug)
	{
		return !Thug || Thug->IsLimp() || !Thug->GetHealthComponent() || !Thug->GetHealthComponent()->IsAlive();
	}

	/** Stands Thug with his feet at Where facing Yaw, stopped. */
	inline void PlaceThug(AThugCharacter* Thug, const FVector& Where, float Yaw)
	{
		if (!Thug)
		{
			return;
		}
		const float Half = Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Thug->TeleportTo(Where + FVector(0.f, 0.f, Half + 2.f), FRotator(0.f, Yaw, 0.f));
		Thug->GetCharacterMovement()->StopMovementImmediately();
		if (AThugAIController* Brain = BrainOf(Thug))
		{
			Brain->StopMovement();
		}
	}

	/** Every thug stops thinking where he stands: nothing moves, sees or hears until let go. */
	inline void FreezeThugs(UWorld* World)
	{
		for (TActorIterator<AThugAIController> It(World); It; ++It)
		{
			It->SetThinkingEnabled(false);
		}
	}

	inline UEnhancedInputLocalPlayerSubsystem* InputOf(APlayerController* PC)
	{
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		return Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	}

	inline void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* Action = LoadObject<UInputAction>(nullptr, Path);
		if (Input && Action)
		{
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
		}
	}

	inline void Hold(APlayerController* PC, const TCHAR* Path, bool bHold, const FInputActionValue& Value = FInputActionValue(true))
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* Action = LoadObject<UInputAction>(nullptr, Path);
		if (!Input || !Action)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(Action, Value, {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Action);
		}
	}

	/** Puts Kate's feet at Where facing Yaw, the camera behind her. */
	inline void Place(UWorld* World, const FVector& Where, float Yaw, float Pitch = -8.f)
	{
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!PC || !Kate)
		{
			return;
		}
		const float Half = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(Where + FVector(0.f, 0.f, Half + 2.f), FRotator(0.f, Yaw, 0.f));
		Kate->GetCharacterMovement()->StopMovementImmediately();
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		PC->SetViewTarget(Kate);
	}

	inline void WriteText(const FString& FileName, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / FileName);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		FFileHelper::SaveStringToFile(Text, *Path);
	}
}

#endif
