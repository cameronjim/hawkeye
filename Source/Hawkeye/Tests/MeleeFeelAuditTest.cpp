#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/HitContactRules.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Player/HawkeyeCharacter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Audit.MeleeFeel (standalone -game, about 4 minutes): the numbers behind "fighting isn't smooth - punches
 * don't land properly, ragdoll physics are off" (docs/playtests/2026-11-11-melee-feel-audit.md). On the district street
 * by the PlayerStart, AI off, every trial its own fresh thug:
 *
 *  - Kate's light and heavy at 60/100/150/200/250 cm and 0/30/60 degrees off her facing, her chain at 100 cm, the
 * thug's fists on her at 80 to 200 cm: at the frame the damage lands, the gap between the striking limb and the struck
 * body's surface (the drivers' bones against the physics asset, and the bodies people see), whether she faces him, the
 * time from the press, and what follows the damage frame and when (hit-stop, reaction clip and its side, physical
 * reaction, flash, sound).
 *  - Five knockouts (light, heavy, finisher, arrow, off a ledge): the throw, the inherited velocity, the path, stretch,
 *    tunnelling, jitter, slide and settle, and the visible body against the driver; frames in
 * Saved/Screenshots/MeleeAudit.
 *  - Magnetism through a wall and over a ledge, and the swing's slide, snap, buffering and clip blends.
 *
 * Writes Saved/Automation/melee_feel_audit.json. It fails only on the limits in gameplay-semantics.md, "Melee feel".
 */
namespace HawkeyeMeleeAudit
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");

	enum class EKind : uint8
	{
		KateLight,
		KateHeavy,
		KateChain,
		ThugFists,
		KoLight,
		KoHeavy,
		KoFinisher,
		KoArrow,
		KoLedge,
		WallProbe,
		LedgeProbe,
		Smooth,
		Buffer,
	};

	static const TCHAR* KindName(EKind Kind)
	{
		switch (Kind)
		{
			case EKind::KateLight:
				return TEXT("kate_light");
			case EKind::KateHeavy:
				return TEXT("kate_heavy");
			case EKind::KateChain:
				return TEXT("kate_chain");
			case EKind::ThugFists:
				return TEXT("thug_fists");
			case EKind::KoLight:
				return TEXT("ko_light");
			case EKind::KoHeavy:
				return TEXT("ko_heavy");
			case EKind::KoFinisher:
				return TEXT("ko_finisher");
			case EKind::KoArrow:
				return TEXT("ko_arrow");
			case EKind::KoLedge:
				return TEXT("ko_ledge");
			case EKind::WallProbe:
				return TEXT("wall_probe");
			case EKind::LedgeProbe:
				return TEXT("ledge_probe");
			case EKind::Smooth:
				return TEXT("smooth");
			default:
				return TEXT("buffer");
		}
	}

	static bool IsKnockout(EKind Kind)
	{
		return Kind == EKind::KoLight || Kind == EKind::KoHeavy || Kind == EKind::KoFinisher ||
			Kind == EKind::KoArrow || Kind == EKind::KoLedge;
	}

	struct FTrial
	{
		EKind Kind = EKind::KateLight;
		float Distance = 150.f;
		float Angle = 0.f;
	};

	/** One landed (or whiffed) blow, measured at the frame its damage went in. */
	struct FBlow
	{
		FString Attack;
		bool bLanded = false;
		bool bWhiffCaps = false;
		double PressWorld = 0.0;
		uint64 PressFrame = 0;
		float StartGap = 0.f;
		float HitSeconds = -1.f;
		int32 HitFrames = -1;
		float CapsuleGap = 0.f;
		float DriverLimbGap = 1e9f;
		FString DriverLimb;
		float SeenLimbGap = 1e9f;
		FString SeenLimb;
		float FacingDegrees = 0.f;
		float MinDriverGapInWindow = 1e9f;
		FVector ImpactDirection = FVector::ZeroVector;
		float ImpactWeight = 0.f;
		// After the damage frame, in frames (-1: never within 0.6 s of world time).
		int32 HitStopFrame = -1;
		float HitStopRealMs = 0.f;
		int32 ClipFrame = -1;
		FString ClipSide;
		FString ExpectedSide;
		int32 PhysFrame = -1;
		int32 FlashFrame = -1;
		int32 SoundFrame = -1;
		double DamageReal = 0.0;
		double StopStartReal = 0.0;
		int32 VictimSimulatingBefore = 0;
		float AttackerSeenHandOffset = -1.f;
		float VictimSeenHeadOffset = -1.f;
	};

	struct FKnockout
	{
		FString Label;
		bool bDied = false;
		FString Struck;
		float ThrowBone = 0.f;
		float ThrowBody = 0.f;
		float AnimPelvisSpeed = 0.f;
		float FirstSimPelvisSpeed = 0.f;
		float PeakPelvisSpeed = 0.f;
		float Travel = 0.f;
		float SlideWhileDown = 0.f;
		float MaxStretch = 0.f;
		FString StretchBone;
		float DeepestBelowGround = 0.f;
		float MaxLookOffset = 0.f;
		float LateMaxSpeed = 0.f;
		int32 LateReversals = 0;
		float SettleSeconds = -1.f;
		float RestPelvisOverGround = -1.f;
		float DropHeight = 0.f;
		TArray<FString> Frames;
	};

	struct FState
	{
		TArray<FTrial> Trials;
		int32 Index = -1;
		int32 Step = 0;
		double StepWorld = 0.0;
		double StepReal = 0.0;
		TWeakObjectPtr<AThugCharacter> Foe;
		TArray<TWeakObjectPtr<AActor>> Props;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector StreetFeet = FVector::ZeroVector;
		FVector Along = FVector::ForwardVector;
		bool bHaveStreet = false;
		float VictimHealthBefore = 0.f;
		TArray<FBlow> Blows;
		FBlow Current;
		bool bWatching = false;
		bool bAfter = false;
		int32 FramesAfter = 0;
		int32 LastAudio = 0;
		int32 ChainPresses = 0;
		FKnockout Ko;
		bool bKoWatching = false;
		double KoStartWorld = 0.0;
		FVector KoFeet = FVector::ZeroVector;
		FVector LastPelvis = FVector::ZeroVector;
		FVector LastPelvisVelocity = FVector::ZeroVector;
		bool bHaveLastPelvis = false;
		int32 KoFrame = 0;
		TArray<float> CaptureAt;
		int32 NextCapture = 0;
		bool bCapturing = false;
		TArray<TSharedPtr<FJsonValue>> Rows;
		TArray<TSharedPtr<FJsonValue>> Knockouts;
		TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
		FDelegateHandle TickHook;
		// Smoothness sampling.
		TArray<FVector> KatePath;
		TArray<float> KateYaw;
		TArray<float> KateDt;
		TArray<uint8> KatePhase;
	};

	static FState S;

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

	static AHawkeyeCharacter* FindKate(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	}

	static bool Ground(UWorld* World, const FVector& XY, float FromZ, FVector& Out, const AActor* Ignore = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeAuditGround), false, Ignore);
		for (const TWeakObjectPtr<AThugCharacter>& Weak : {S.Foe})
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		if (AHawkeyeCharacter* Kate = FindKate(World))
		{
			Params.AddIgnoredActor(Kate);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
				Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, FromZ - 6000.f), ECC_Visibility, Params))
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}

	static void Stand(ACharacter* Character, const FVector& Feet, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	/** A flat street spot: the kerb is a step, so the spot is moved along the street until 4 m each way is level. */
	static bool FindFlatStreet(UWorld* World, const FVector& Near, const FVector& Along, FVector& Out)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		for (int32 Try = 0; Try < 40; ++Try)
		{
			const float Offset = (Try / 2) * 100.f * ((Try % 2) ? -1.f : 1.f);
			FVector Centre;
			if (!Ground(World, Near + Side * Offset, Near.Z + 3000.f, Centre))
			{
				continue;
			}
			bool bFlat = true;
			for (const FVector& Probe : {Along * 400.f, -Along * 400.f, Side * 300.f, -Side * 300.f,
					 Along * 250.f + Side * 250.f, Along * 250.f - Side * 250.f})
			{
				FVector At;
				if (!Ground(World, Centre + Probe, Centre.Z + 300.f, At) || FMath::Abs(At.Z - Centre.Z) > 4.f)
				{
					bFlat = false;
					break;
				}
			}
			if (bFlat)
			{
				Out = Centre;
				return true;
			}
		}
		return false;
	}

	static AThugCharacter* SpawnFoe(UWorld* World, const FVector& Feet, float Yaw)
	{
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		if (!ThugClass)
		{
			ThugClass = AThugCharacter::StaticClass();
		}
		const FTransform At(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, 100.f));
		AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(
			ThugClass, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Thug)
		{
			return nullptr;
		}
		Thug->Weapon = EThugWeapon::Fists;
		UGameplayStatics::FinishSpawningActor(Thug, At);
		Thug->RefreshHeldWeapon();
		if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
		Stand(Thug, Feet, Yaw);
		return Thug;
	}

	static AStaticMeshActor* SpawnBox(UWorld* World, const FVector& Centre, const FVector& HalfExtent, float Yaw)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Centre), Params);
		if (!Box || !Cube)
		{
			return Box;
		}
		UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetWorldScale3D(HalfExtent / 50.f);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		S.Props.Add(Box);
		return Box;
	}

	static int32 FindBoneCI(const USkeletalMeshComponent* Mesh, const TCHAR* Name)
	{
		if (!Mesh || !Mesh->GetSkeletalMeshAsset())
		{
			return INDEX_NONE;
		}
		const int32 Exact = Mesh->GetBoneIndex(Name);
		if (Exact != INDEX_NONE)
		{
			return Exact;
		}
		const int32 Count = Mesh->GetNumBones();
		for (int32 Bone = 0; Bone < Count; ++Bone)
		{
			if (Mesh->GetBoneName(Bone).ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Bone;
			}
		}
		return INDEX_NONE;
	}

	static bool BonePos(const USkeletalMeshComponent* Mesh, const TCHAR* Name, FVector& Out)
	{
		const int32 Bone = FindBoneCI(Mesh, Name);
		if (Bone == INDEX_NONE)
		{
			return false;
		}
		Out = Mesh->GetBoneTransform(Bone).GetLocation();
		return true;
	}

	/**
	 * The gap from Point to Body's surface: its physics asset when it has one; else its bone segments (pelvis to head,
	 * the limbs) less a rough flesh radius for each (head 11 cm, torso 15, upper limbs 7, legs 8).
	 */
	static float SurfaceGap(const USkeletalMeshComponent* Body, const FVector& Point)
	{
		if (!Body)
		{
			return 1e9f;
		}
		if (Body->GetPhysicsAsset())
		{
			FClosestPointOnPhysicsAsset Closest;
			if (Body->GetClosestPointOnPhysicsAsset(Point, Closest, false))
			{
				return Closest.Distance;
			}
		}
		struct FSeg
		{
			const TCHAR* A;
			const TCHAR* B;
			float R;
		};
		static const FSeg Segs[] = {
			{TEXT("pelvis"), TEXT("spine_01"), 15.f},
			{TEXT("spine_01"), TEXT("spine_03"), 15.f},
			{TEXT("spine_03"), TEXT("neck_01"), 14.f},
			{TEXT("neck_01"), TEXT("head"), 11.f},
			{TEXT("upperarm_l"), TEXT("lowerarm_l"), 7.f},
			{TEXT("lowerarm_l"), TEXT("hand_l"), 6.f},
			{TEXT("upperarm_r"), TEXT("lowerarm_r"), 7.f},
			{TEXT("lowerarm_r"), TEXT("hand_r"), 6.f},
			{TEXT("thigh_l"), TEXT("calf_l"), 9.f},
			{TEXT("calf_l"), TEXT("foot_l"), 7.f},
			{TEXT("thigh_r"), TEXT("calf_r"), 9.f},
			{TEXT("calf_r"), TEXT("foot_r"), 7.f},
		};
		float Best = 1e9f;
		for (const FSeg& Seg : Segs)
		{
			FVector A;
			FVector B;
			if (!BonePos(Body, Seg.A, A) || !BonePos(Body, Seg.B, B))
			{
				continue;
			}
			const FVector OnSeg = FMath::ClosestPointOnSegment(Point, A, B);
			Best = FMath::Min(Best, FVector::Dist(Point, OnSeg) - Seg.R);
		}
		// The head is a ball above its bone, not a point.
		FVector Head;
		if (BonePos(Body, TEXT("head"), Head))
		{
			Best = FMath::Min(Best, FVector::Dist(Point, Head + FVector(0.f, 0.f, 8.f)) - 11.f);
		}
		return Best;
	}

	static const TCHAR* Limbs[] = {TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l")};

	/** The nearest of the attacker's fists and feet on Attacker to Victim's surface; OutLimb names it. */
	static float NearestLimbGap(
		const USkeletalMeshComponent* Attacker, const USkeletalMeshComponent* Victim, FString& OutLimb)
	{
		float Best = 1e9f;
		for (const TCHAR* Limb : Limbs)
		{
			FVector At;
			if (!BonePos(Attacker, Limb, At))
			{
				continue;
			}
			const float Gap = SurfaceGap(Victim, At);
			if (Gap < Best)
			{
				Best = Gap;
				OutLimb = Limb;
			}
		}
		return Best;
	}

	static USkeletalMeshComponent* SeenMesh(AActor* Actor)
	{
		if (AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Actor))
		{
			return Kate->GetBodyMesh();
		}
		if (AThugCharacter* Thug = Cast<AThugCharacter>(Actor))
		{
			return Thug->GetBodyMesh();
		}
		return nullptr;
	}

	static USkeletalMeshComponent* DriverBody(AActor* Actor)
	{
		ACharacter* Character = Cast<ACharacter>(Actor);
		USkeletalMeshComponent* Driver = Character ? Character->GetMesh() : nullptr;
		if (Driver && Driver->GetPhysicsAsset())
		{
			return Driver;
		}
		// Kate's driver (the UEFN mannequin) carries no bodies; her look's physics asset is her body.
		return SeenMesh(Actor);
	}

	static FString SideName(EHawkeyeHitDirection Side)
	{
		return UEnum::GetValueAsString(Side).RightChop(FString(TEXT("EHawkeyeHitDirection::")).Len());
	}

	static FString MontageSide(const AActor* Victim, const UAnimMontage* Montage)
	{
		const UCombatAnimSet* Set = nullptr;
		if (const AThugCharacter* Thug = Cast<AThugCharacter>(Victim))
		{
			Set = Thug->GetMeleeComponent() ? Thug->GetMeleeComponent()->GetAnimSet() : nullptr;
		}
		else if (const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Victim))
		{
			Set = Kate->GetMeleeComponent() ? Kate->GetMeleeComponent()->GetAnimSet() : nullptr;
		}
		if (!Set || !Montage)
		{
			return FString();
		}
		const TPair<ECombatAnimRole, const TCHAR*> Roles[] = {{ECombatAnimRole::HitFront, TEXT("Front")},
			{ECombatAnimRole::HitBack, TEXT("Back")}, {ECombatAnimRole::HitLeft, TEXT("Left")},
			{ECombatAnimRole::HitRight, TEXT("Right")}, {ECombatAnimRole::Knockdown, TEXT("Knockdown")}};
		for (const TPair<ECombatAnimRole, const TCHAR*>& Role : Roles)
		{
			if (Set->ResolveMontage(Role.Key) == Montage)
			{
				return Role.Value;
			}
		}
		return FString();
	}

	/** The hit-reaction side a montage now playing on Actor (its main or post-process instance) stands for, or empty.
	 */
	static FString PlayingReactionSide(AActor* Actor)
	{
		ACharacter* Character = Cast<ACharacter>(Actor);
		USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		if (!Mesh)
		{
			return FString();
		}
		for (UAnimInstance* Anim : {Mesh->GetAnimInstance(), Mesh->GetPostProcessInstance()})
		{
			if (!Anim)
			{
				continue;
			}
			for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
			{
				if (Instance && Instance->IsActive())
				{
					const FString Side = MontageSide(Actor, Instance->Montage);
					if (!Side.IsEmpty())
					{
						return Side;
					}
				}
			}
		}
		return FString();
	}

	/** Bodies simulating on Actor's seen mesh now (a reaction left running shows here). */
	static int32 SimulatingBodies(AActor* Actor)
	{
		const USkeletalMeshComponent* Mesh = SeenMesh(Actor);
		int32 Count = 0;
		if (Mesh)
		{
			for (const FBodyInstance* Body : Mesh->Bodies)
			{
				Count +=
					Body && Body->BodySetup.IsValid() && Mesh->IsSimulatingPhysics(Body->BodySetup->BoneName) ? 1 : 0;
			}
		}
		return Count;
	}

	static int32 AudioCount(UWorld* World)
	{
		const UHawkeyeAudioSubsystem* Audio = World ? World->GetSubsystem<UHawkeyeAudioSubsystem>() : nullptr;
		return Audio ? Audio->GetTriggerCount() : 0;
	}

	static UHealthComponent* VictimHealth(AHawkeyeCharacter* Kate, AThugCharacter* Thug, EKind Kind)
	{
		return Kind == EKind::ThugFists ? Kate->GetHealthComponent() : (Thug ? Thug->GetHealthComponent() : nullptr);
	}

	static void BeginBlow(UWorld* World, AActor* Attacker, AActor* Victim, const FString& Attack)
	{
		S.Current = FBlow();
		S.Current.Attack = Attack;
		S.Current.PressWorld = World->GetTimeSeconds();
		S.Current.PressFrame = GFrameCounter;
		S.Current.StartGap = FVector::Dist2D(Attacker->GetActorLocation(), Victim->GetActorLocation());
		S.Current.VictimSimulatingBefore = SimulatingBodies(Victim);
		S.bWatching = true;
		S.bAfter = false;
		S.FramesAfter = 0;
		S.LastAudio = AudioCount(World);
	}

	static TSharedPtr<FJsonObject> BlowJson(const FTrial& Trial, const FBlow& B)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("kind"), KindName(Trial.Kind));
		Row->SetStringField(TEXT("attack"), B.Attack);
		Row->SetNumberField(TEXT("start_cm"), Trial.Distance);
		Row->SetNumberField(TEXT("angle_deg"), Trial.Angle);
		Row->SetNumberField(TEXT("start_gap_cm"), B.StartGap);
		Row->SetBoolField(TEXT("landed"), B.bLanded);
		Row->SetBoolField(TEXT("whiff_caps"), B.bWhiffCaps);
		Row->SetNumberField(TEXT("hit_s"), B.HitSeconds);
		Row->SetNumberField(TEXT("hit_frames"), B.HitFrames);
		Row->SetNumberField(TEXT("capsule_gap_cm"), B.CapsuleGap);
		Row->SetNumberField(TEXT("driver_limb_gap_cm"), B.DriverLimbGap);
		Row->SetStringField(TEXT("driver_limb"), B.DriverLimb);
		Row->SetNumberField(TEXT("seen_limb_gap_cm"), B.SeenLimbGap);
		Row->SetStringField(TEXT("seen_limb"), B.SeenLimb);
		Row->SetNumberField(TEXT("facing_deg"), B.FacingDegrees);
		Row->SetNumberField(TEXT("min_driver_gap_in_window_cm"), B.MinDriverGapInWindow);
		Row->SetNumberField(TEXT("weight"), B.ImpactWeight);
		Row->SetNumberField(TEXT("hitstop_frame"), B.HitStopFrame);
		Row->SetNumberField(TEXT("hitstop_real_ms"), B.HitStopRealMs);
		Row->SetNumberField(TEXT("clip_frame"), B.ClipFrame);
		Row->SetStringField(TEXT("clip_side"), B.ClipSide);
		Row->SetStringField(TEXT("expected_side"), B.ExpectedSide);
		Row->SetNumberField(TEXT("physical_frame"), B.PhysFrame);
		Row->SetNumberField(TEXT("flash_frame"), B.FlashFrame);
		Row->SetNumberField(TEXT("sound_frame"), B.SoundFrame);
		Row->SetNumberField(TEXT("victim_bodies_simulating_before"), B.VictimSimulatingBefore);
		Row->SetNumberField(TEXT("attacker_seen_hand_off_driver_cm"), B.AttackerSeenHandOffset);
		Row->SetNumberField(TEXT("victim_seen_head_off_driver_cm"), B.VictimSeenHeadOffset);
		return Row;
	}

	/** Per frame, after every actor ticked: the blow being watched, the knockout being followed, Kate's path. */
	static void OnPostActorTick(UWorld* World, ELevelTick, float)
	{
		if (World != FindWorld() || !S.Trials.IsValidIndex(S.Index))
		{
			return;
		}
		AHawkeyeCharacter* Kate = FindKate(World);
		AThugCharacter* Thug = S.Foe.Get();
		if (!Kate)
		{
			return;
		}
		const FTrial& Trial = S.Trials[S.Index];
		const double Now = World->GetTimeSeconds();

		if (Trial.Kind == EKind::Smooth || Trial.Kind == EKind::Buffer)
		{
			S.KatePath.Add(Kate->GetActorLocation());
			S.KateYaw.Add(Kate->GetActorRotation().Yaw);
			S.KateDt.Add(World->GetDeltaSeconds());
			S.KatePhase.Add(static_cast<uint8>(Kate->GetMeleeComponent()->GetPhase()));
		}

		if (S.bWatching && Thug)
		{
			const bool bThugSwings = Trial.Kind == EKind::ThugFists;
			AActor* Attacker = bThugSwings ? static_cast<AActor*>(Thug) : Kate;
			AActor* Victim = bThugSwings ? static_cast<AActor*>(Kate) : Thug;
			UHealthComponent* Health = VictimHealth(Kate, Thug, Trial.Kind);
			const UMeleeComponent* Melee = bThugSwings ? Thug->GetMeleeComponent() : Kate->GetMeleeComponent();
			FBlow& B = S.Current;
			if (!S.bAfter)
			{
				if (Melee && (Melee->IsHitWindowOpen() || Melee->GetPhase() == EMeleePhase::Recover))
				{
					FString Limb;
					B.MinDriverGapInWindow = FMath::Min(B.MinDriverGapInWindow,
						NearestLimbGap(Cast<ACharacter>(Attacker)->GetMesh(), DriverBody(Victim), Limb));
				}
				B.bWhiffCaps = Melee && Melee->IsContactWhiff();
				const bool bLanded =
					Health && (Health->GetCurrentHealth() < S.VictimHealthBefore - 0.01f || !Health->IsAlive());
				if (bLanded)
				{
					B.bLanded = true;
					B.HitSeconds = static_cast<float>(Now - B.PressWorld);
					B.HitFrames = static_cast<int32>(GFrameCounter - B.PressFrame);
					B.CapsuleGap = FVector::Dist2D(Attacker->GetActorLocation(), Victim->GetActorLocation());
					B.DriverLimbGap =
						NearestLimbGap(Cast<ACharacter>(Attacker)->GetMesh(), DriverBody(Victim), B.DriverLimb);
					B.SeenLimbGap = NearestLimbGap(SeenMesh(Attacker), SeenMesh(Victim), B.SeenLimb);
					// How far each seen body's bones sit off its driver's: a look that has come apart from its driver.
					auto Offset = [](AActor* Actor, const TCHAR* Bone)
					{
						FVector Seen;
						FVector Driver;
						const ACharacter* Character = Cast<ACharacter>(Actor);
						return Character && BonePos(SeenMesh(Actor), Bone, Seen) &&
								BonePos(Character->GetMesh(), Bone, Driver)
							? static_cast<float>(FVector::Dist(Seen, Driver))
							: -1.f;
					};
					B.AttackerSeenHandOffset = Offset(Attacker, TEXT("hand_r"));
					B.VictimSeenHeadOffset = Offset(Victim, TEXT("head"));
					const FVector To = (Victim->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
					B.FacingDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector::DotProduct(Attacker->GetActorForwardVector().GetSafeNormal2D(), To), -1.f, 1.f)));
					if (Melee && Melee->GetLastImpact().bValid)
					{
						B.ImpactDirection = Melee->GetLastImpact().Direction;
						B.ImpactWeight = Melee->GetLastImpact().Weight;
					}
					B.ExpectedSide = SideName(UHawkeyeMeleeRules::ClassifyHitDirection(
						Victim->GetActorForwardVector(), Attacker->GetActorLocation() - Victim->GetActorLocation()));
					B.DamageReal = FPlatformTime::Seconds();
					S.bAfter = true;
					S.FramesAfter = 0;
				}
			}
			if (S.bAfter)
			{
				const int32 Frame = S.FramesAfter;
				const float Dilation = UGameplayStatics::GetGlobalTimeDilation(World);
				if (B.HitStopFrame < 0 && Dilation < 0.99f)
				{
					B.HitStopFrame = Frame;
					B.StopStartReal = FPlatformTime::Seconds();
				}
				else if (B.HitStopFrame >= 0 && B.HitStopRealMs <= 0.f && Dilation >= 0.99f)
				{
					B.HitStopRealMs = static_cast<float>((FPlatformTime::Seconds() - B.StopStartReal) * 1000.0);
				}
				if (B.ClipFrame < 0)
				{
					const FString Side = PlayingReactionSide(Victim);
					if (!Side.IsEmpty())
					{
						B.ClipFrame = Frame;
						B.ClipSide = Side;
					}
				}
				const UHitReactionComponent* React = Victim->FindComponentByClass<UHitReactionComponent>();
				if (B.PhysFrame < 0 && React && React->GetReactionBlend() > 0.f)
				{
					B.PhysFrame = Frame;
				}
				if (B.FlashFrame < 0 && !bThugSwings && Thug->GetHitFlashAlpha() > 0.f)
				{
					B.FlashFrame = Frame;
				}
				const int32 Audio = AudioCount(World);
				if (B.SoundFrame < 0 && Audio > S.LastAudio)
				{
					B.SoundFrame = Frame;
				}
				S.LastAudio = Audio;
				++S.FramesAfter;
			}
			else
			{
				S.LastAudio = AudioCount(World);
			}
		}

		if (S.bKoWatching && Thug)
		{
			USkeletalMeshComponent* Driver = Thug->GetMesh();
			const int32 Pelvis = FindBoneCI(Driver, TEXT("pelvis"));
			if (Pelvis == INDEX_NONE)
			{
				return;
			}
			const FVector P = Driver->GetBoneTransform(Pelvis).GetLocation();
			const float Dt = FMath::Max(World->GetDeltaSeconds(), 1e-4f);
			const FVector V = S.bHaveLastPelvis ? (P - S.LastPelvis) / Dt : FVector::ZeroVector;
			FKnockout& K = S.Ko;
			if (!K.bDied)
			{
				if (Thug->IsLimp())
				{
					K.bDied = true;
					S.KoStartWorld = Now;
					S.KoFrame = 0;
					K.AnimPelvisSpeed = S.LastPelvisVelocity.Size();
					if (const UHitReactionComponent* His = Thug->GetHitReactionComponent())
					{
						K.ThrowBone = His->GetKnockoutBoneVelocity().Size();
						K.ThrowBody = His->GetKnockoutBodyVelocity().Size();
						K.Struck = His->GetLastImpact().Bone.ToString();
					}
					S.KoFeet = Thug->GetActorLocation() -
						FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				}
			}
			else
			{
				++S.KoFrame;
				const float Elapsed = static_cast<float>(Now - S.KoStartWorld);
				const float PhysSpeed = Driver->IsSimulatingPhysics()
					? Driver->GetPhysicsLinearVelocity(Driver->GetBoneName(Pelvis)).Size()
					: V.Size();
				if (S.KoFrame == 1)
				{
					K.FirstSimPelvisSpeed = PhysSpeed;
				}
				K.PeakPelvisSpeed = FMath::Max(K.PeakPelvisSpeed, PhysSpeed);
				K.Travel = FVector::Dist2D(P, S.KoFeet);
				FVector Floor;
				const bool bFloor = Ground(World, P, P.Z + 40.f, Floor, Thug);
				if (bFloor && P.Z - Floor.Z < 35.f && S.bHaveLastPelvis)
				{
					K.SlideWhileDown += FVector::Dist2D(P, S.LastPelvis);
				}
				// Every body below the ground under it (a body through the floor).
				for (const FBodyInstance* Body : Driver->Bodies)
				{
					if (!Body || !Body->BodySetup.IsValid())
					{
						continue;
					}
					const int32 Bone = Driver->GetBoneIndex(Body->BodySetup->BoneName);
					const FVector At = Driver->GetBoneTransform(Bone).GetLocation();
					FVector Under;
					if (Ground(World, At, At.Z + 150.f, Under, Thug))
					{
						K.DeepestBelowGround = FMath::Max(K.DeepestBelowGround, Under.Z - At.Z);
					}
					// Stretch: the joint's length now against the reference pose, between two bodies only (the pelvis
					// hangs off the root, which has none).
					const FName ParentName = Driver->GetParentBone(Body->BodySetup->BoneName);
					const int32 Parent =
						Driver->GetBodyInstance(ParentName) ? Driver->GetBoneIndex(ParentName) : INDEX_NONE;
					if (Parent != INDEX_NONE)
					{
						const float Now2 = FVector::Dist(At, Driver->GetBoneTransform(Parent).GetLocation());
						const float Ref = Driver->GetSkeletalMeshAsset()
											  ->GetRefSkeleton()
											  .GetRefBonePose()[Bone]
											  .GetLocation()
											  .Size();
						if (Ref > 2.f)
						{
							const float Stretch = Now2 - Ref;
							if (FMath::Abs(Stretch) > FMath::Abs(K.MaxStretch))
							{
								K.MaxStretch = Stretch;
								K.StretchBone = Body->BodySetup->BoneName.ToString();
							}
						}
					}
				}
				// The body people see against the one that falls.
				if (USkeletalMeshComponent* Seen = Thug->GetBodyMesh(); Seen && Seen != Driver)
				{
					FVector SeenPelvis;
					if (BonePos(Seen, TEXT("pelvis"), SeenPelvis))
					{
						K.MaxLookOffset = FMath::Max(K.MaxLookOffset, FVector::Dist(SeenPelvis, P));
					}
				}
				if (Elapsed > 1.5f &&
					!(Thug->GetHitReactionComponent() && Thug->GetHitReactionComponent()->IsKnockoutSettled()))
				{
					K.LateMaxSpeed = FMath::Max(K.LateMaxSpeed, PhysSpeed);
					if (S.LastPelvisVelocity.Z * V.Z < 0.f && FMath::Abs(V.Z) > 5.f)
					{
						++K.LateReversals;
					}
				}
				if (K.SettleSeconds < 0.f && Thug->GetHitReactionComponent() &&
					Thug->GetHitReactionComponent()->IsKnockoutSettled())
				{
					K.SettleSeconds = Elapsed;
					K.RestPelvisOverGround = bFloor ? P.Z - Floor.Z : -1.f;
				}
			}
			S.LastPelvisVelocity = V;
			S.LastPelvis = P;
			S.bHaveLastPelvis = true;
		}
	}

	static void ClearTrial(UWorld* World)
	{
		if (AThugCharacter* Thug = S.Foe.Get())
		{
			Thug->Destroy();
		}
		S.Foe = nullptr;
		for (const TWeakObjectPtr<AActor>& Prop : S.Props)
		{
			if (AActor* Actor = Prop.Get())
			{
				Actor->Destroy();
			}
		}
		S.Props.Reset();
		S.bWatching = false;
		S.bAfter = false;
		S.bKoWatching = false;
		S.bHaveLastPelvis = false;
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	}

	static void FrameKnockout(UWorld* World, APlayerController* PC, const FVector& Centre, const FVector& Along)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = S.Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
			S.Camera = Camera;
		}
		if (!Camera)
		{
			return;
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		const FVector Eye = Centre + Side * 560.f + FVector(0.f, 0.f, 120.f);
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(60.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
	}

	static FString ShotPath(const FString& File)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MeleeAudit") / File);
	}
}

/** Runs the audit's trials one after another, a step per frame; true once every trial is done and the JSON written. */
class FHawkeyeMeleeAuditRun : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeMeleeAuditRun(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override;

private:
	void Next(UWorld* World);
	bool Setup(UWorld* World, AHawkeyeCharacter* Kate);
	bool Run(UWorld* World, AHawkeyeCharacter* Kate);
	void FinishBlow();
	void FinishKnockout();
	void FinishSmooth(UWorld* World, AHawkeyeCharacter* Kate);
	void Write(UWorld* World, AHawkeyeCharacter* Kate);

	FAutomationTestBase* Test;
	bool bStarted = false;
	double StartReal = 0.0;
};

void FHawkeyeMeleeAuditRun::Next(UWorld* World)
{
	using namespace HawkeyeMeleeAudit;
	ClearTrial(World);
	++S.Index;
	S.Step = 0;
	S.StepWorld = World->GetTimeSeconds();
	S.StepReal = FPlatformTime::Seconds();
}

bool FHawkeyeMeleeAuditRun::Setup(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeMeleeAudit;
	const FTrial& Trial = S.Trials[S.Index];
	APlayerController* PC = World->GetFirstPlayerController();
	PC->SetViewTarget(Kate);
	Kate->GetHealthComponent()->Revive(Kate->GetHealthComponent()->GetMaxHealth());
	Kate->GetHealthComponent()->SetInvulnerable(Trial.Kind != EKind::ThugFists);
	FVector KateFeet = S.StreetFeet;
	const FVector Facing = S.Along;
	FVector FoeFeet;
	const FVector Toward = FRotator(0.f, Facing.Rotation().Yaw + Trial.Angle, 0.f).Vector();

	if (Trial.Kind == EKind::KoLedge || Trial.Kind == EKind::LedgeProbe)
	{
		// A 150 cm block in the street, its far edge across her path: the thug stands at the edge (the knockout) or on
		// the street beyond it (the probe: Kate on the block, him below). The block spans 0 to 250 cm along the street;
		// she stands at 100 on it.
		const FVector BlockCentre = S.StreetFeet + Facing * 125.f + FVector(0.f, 0.f, 75.f);
		SpawnBox(World, BlockCentre, FVector(125.f, 200.f, 75.f), Facing.Rotation().Yaw);
		KateFeet = S.StreetFeet + Facing * 100.f + FVector(0.f, 0.f, 150.f);
		FoeFeet = Trial.Kind == EKind::KoLedge ? S.StreetFeet + Facing * 225.f + FVector(0.f, 0.f, 150.f)
											   : S.StreetFeet + Facing * 340.f;
	}
	else
	{
		FoeFeet = S.StreetFeet + Toward * Trial.Distance;
		Ground(World, FoeFeet, S.StreetFeet.Z + 200.f, FoeFeet);
	}
	Stand(Kate, KateFeet, Facing.Rotation().Yaw);
	PC->SetControlRotation(FRotator(-10.f, Facing.Rotation().Yaw, 0.f));
	AThugCharacter* Thug = SpawnFoe(World, FoeFeet, (KateFeet - FoeFeet).Rotation().Yaw);
	S.Foe = Thug;
	if (!Thug)
	{
		return false;
	}
	if (Trial.Kind == EKind::WallProbe)
	{
		// A wall between them, 30 cm thick and taller than both.
		SpawnBox(World, S.StreetFeet + Facing * (Trial.Distance * 0.5f) + FVector(0.f, 0.f, 150.f),
			FVector(15.f, 150.f, 150.f), Facing.Rotation().Yaw);
	}
	UHealthComponent* His = Thug->GetHealthComponent();
	His->SetMaxHealth(IsKnockout(Trial.Kind) ? 1.f : 5000.f, true);
	// Everything in the camera's view: the knockouts side on.
	if (IsKnockout(Trial.Kind))
	{
		FrameKnockout(World, PC, (KateFeet + FoeFeet) * 0.5f + Facing * 120.f, Facing);
	}
	return true;
}

bool FHawkeyeMeleeAuditRun::Run(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeMeleeAudit;
	const FTrial& Trial = S.Trials[S.Index];
	AThugCharacter* Thug = S.Foe.Get();
	const double Since = World->GetTimeSeconds() - S.StepWorld;
	const double SinceReal = FPlatformTime::Seconds() - S.StepReal;
	auto Advance = [World]()
	{
		++S.Step;
		S.StepWorld = World->GetTimeSeconds();
		S.StepReal = FPlatformTime::Seconds();
	};

	switch (S.Step)
	{
		case 0:
			if (!Setup(World, Kate))
			{
				Test->AddWarning(FString::Printf(
					TEXT("Melee audit: %s at %.0f cm could not be set up."), KindName(Trial.Kind), Trial.Distance));
				return true;
			}
			Advance();
			return false;

		case 1:
			// Settle: both on their feet, the last trial's chain and counter gone.
			if (Since < 1.2)
			{
				return false;
			}
			if (!Thug)
			{
				return true;
			}
			S.VictimHealthBefore = VictimHealth(Kate, Thug, Trial.Kind)->GetCurrentHealth();
			S.ChainPresses = 0;
			S.KatePath.Reset();
			S.KateYaw.Reset();
			S.KateDt.Reset();
			S.KatePhase.Reset();
			switch (Trial.Kind)
			{
				case EKind::KateLight:
				case EKind::KateChain:
				case EKind::KoLight:
				case EKind::WallProbe:
				case EKind::LedgeProbe:
				case EKind::Smooth:
				case EKind::Buffer:
					BeginBlow(World, Kate, Thug, TEXT("light1"));
					if (!Kate->StartLightAttack())
					{
						Test->AddWarning(TEXT("Melee audit: her light did not start."));
					}
					S.ChainPresses = 1;
					S.Extra->SetNumberField(
						FString::Printf(TEXT("%s_start_x"), KindName(Trial.Kind)), Kate->GetActorLocation().X);
					break;
				case EKind::KateHeavy:
				case EKind::KoHeavy:
				case EKind::KoLedge:
					BeginBlow(World, Kate, Thug, TEXT("heavy"));
					if (!Kate->StartHeavyAttack())
					{
						Test->AddWarning(TEXT("Melee audit: her heavy did not start."));
					}
					break;
				case EKind::KoFinisher:
					Thug->Parried(Kate, 1.5f);
					BeginBlow(World, Kate, Thug, TEXT("finisher"));
					if (!Kate->TryFinisher())
					{
						Test->AddWarning(TEXT("Melee audit: her finisher did not start."));
					}
					break;
				case EKind::KoArrow:
				{
					BeginBlow(World, Kate, Thug, TEXT("arrow"));
					const FVector From = Kate->GetActorLocation() + FVector(0.f, 0.f, 50.f) + S.Along * 60.f;
					const FVector Chest = Thug->GetActorLocation() + FVector(0.f, 0.f, 40.f);
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AArrowProjectile* Arrow = World->SpawnActor<AArrowProjectile>(
						AArrowProjectile::StaticClass(), From, (Chest - From).Rotation(), Params);
					if (Arrow)
					{
						Arrow->InitArrow(nullptr, nullptr, 50.f, Kate, nullptr);
						Arrow->LaunchWithVelocity((Chest - From).GetSafeNormal() * 6000.f);
						S.Props.Add(Arrow);
					}
					break;
				}
				case EKind::ThugFists:
				{
					BeginBlow(World, Thug, Kate, TEXT("fists"));
					UMeleeComponent* His = Thug->GetMeleeComponent();
					His->SetNextAttackTarget(Kate);
					if (!His->StartAttack(Thug->GetMeleeAttack()))
					{
						Test->AddWarning(TEXT("Melee audit: his swing did not start."));
					}
					break;
				}
			}
			S.bKoWatching = IsKnockout(Trial.Kind);
			if (S.bKoWatching)
			{
				S.Ko = FKnockout();
				S.Ko.Label = KindName(Trial.Kind);
				S.Ko.DropHeight = Trial.Kind == EKind::KoLedge ? 150.f : 0.f;
				S.CaptureAt = {0.1f, 0.3f, 0.7f, 1.5f};
				S.NextCapture = 0;
			}
			Advance();
			return false;

		case 2:
		{
			// Watch the blow: 0.6 s of world time after its damage frame, or 2.5 s with no damage (a whiff).
			FBlow& B = S.Current;
			if (Trial.Kind == EKind::Buffer && S.ChainPresses == 1 &&
				Kate->GetMeleeComponent()->GetPhase() == EMeleePhase::Recover &&
				!Kate->GetMeleeComponent()->IsComboWindowOpen())
			{
				// Pressed in the recovery, before the combo window: is it buffered, and when does it go?
				S.Extra->SetBoolField(TEXT("buffer_accepted"), Kate->StartLightAttack());
				S.Extra->SetNumberField(TEXT("buffer_press_world"), World->GetTimeSeconds());
				S.ChainPresses = 2;
			}
			if (Trial.Kind == EKind::Buffer && S.ChainPresses == 2 &&
				Kate->GetMeleeComponent()->GetCurrentAttack().Name == FName(TEXT("light2")) &&
				!S.Extra->HasField(TEXT("buffer_went_after_s")))
			{
				S.Extra->SetNumberField(TEXT("buffer_went_after_s"),
					World->GetTimeSeconds() - S.Extra->GetNumberField(TEXT("buffer_press_world")));
			}
			if (S.bAfter)
			{
				const double AfterWorld = World->GetTimeSeconds() - (B.PressWorld + B.HitSeconds);
				if (AfterWorld < 0.6)
				{
					return false;
				}
				FinishBlow();
				if (Trial.Kind == EKind::KateChain && S.ChainPresses < 3)
				{
					// The next light, pressed as the last one's watch ends (it buffers or goes).
					const FString Name = S.ChainPresses == 1 ? TEXT("light2") : TEXT("light3");
					S.VictimHealthBefore = Thug->GetHealthComponent()->GetCurrentHealth();
					BeginBlow(World, Kate, Thug, Name);
					Kate->StartLightAttack();
					++S.ChainPresses;
					S.StepWorld = World->GetTimeSeconds();
					return false;
				}
				Advance();
				return false;
			}
			if (Since < 2.5 && SinceReal < 20.0)
			{
				return false;
			}
			FinishBlow();
			Advance();
			return false;
		}

		case 3:
		{
			if (!S.bKoWatching)
			{
				if (Trial.Kind == EKind::WallProbe || Trial.Kind == EKind::LedgeProbe)
				{
					const UMeleeComponent* Hers = Kate->GetMeleeComponent();
					TSharedPtr<FJsonObject> Probe = MakeShared<FJsonObject>();
					Probe->SetBoolField(TEXT("picked"), Hers->GetSwingTarget() == Thug);
					Probe->SetNumberField(TEXT("kate_moved_cm"),
						FVector::Dist2D(Kate->GetActorLocation(),
							S.StreetFeet + (Trial.Kind == EKind::LedgeProbe ? S.Along * 100.f : FVector::ZeroVector)));
					Probe->SetNumberField(TEXT("kate_feet_z_change_cm"),
						Kate->GetActorLocation().Z - Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() -
							(S.StreetFeet.Z + (Trial.Kind == EKind::LedgeProbe ? 150.f : 0.f)));
					Probe->SetNumberField(TEXT("lunge_cm"), Hers->GetLastLungeDistance());
					Probe->SetBoolField(TEXT("landed"), S.Blows.Num() > 0 && S.Blows.Last().bLanded);
					Probe->SetBoolField(TEXT("whiff_caps"), Hers->IsContactWhiff());
					S.Extra->SetObjectField(KindName(Trial.Kind), Probe);
				}
				if (Trial.Kind == EKind::Smooth || Trial.Kind == EKind::Buffer)
				{
					FinishSmooth(World, Kate);
				}
				if (Trial.Kind == EKind::ThugFists && Trial.Angle > 0.f)
				{
					// After his punches: is her look still on her (it once drifted a metre off per punch)?
					FrameKnockout(
						World, World->GetFirstPlayerController(), Kate->GetActorLocation() - S.Along * 120.f, S.Along);
					HawkeyeShots::Request(Test, ShotPath(TEXT("kate_after_his_punches.png")), false);
					Advance();
					return false;
				}
				return true;
			}
			// The knockout: frames at 0.1, 0.3, 0.7, 1.5 s after he went limp, then until settled (6 s at most).
			const FKnockout& K = S.Ko;
			if (!K.bDied)
			{
				if (Since > 3.0)
				{
					Test->AddWarning(FString::Printf(TEXT("Melee audit: %s never went limp."), *K.Label));
					FinishKnockout();
					return true;
				}
				return false;
			}
			const float Elapsed = static_cast<float>(World->GetTimeSeconds() - S.KoStartWorld);
			if (S.bCapturing)
			{
				if (HawkeyeShots::GetOutstanding(Test) > 0 && SinceReal < 12.0)
				{
					return false;
				}
				S.bCapturing = false;
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				return false;
			}
			if (S.CaptureAt.IsValidIndex(S.NextCapture) && Elapsed >= S.CaptureAt[S.NextCapture])
			{
				// Captured at full speed: a crawl (0.02) disturbed the ragdolls it caught (the solver's corrections
				// over tiny steps).
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				const FString File = FString::Printf(TEXT("%s_%02d_%.1fs.png"), *K.Label, S.NextCapture, Elapsed);
				HawkeyeShots::Request(Test, ShotPath(File), false);
				S.Ko.Frames.Add(File);
				++S.NextCapture;
				S.bCapturing = true;
				S.StepReal = FPlatformTime::Seconds();
				return false;
			}
			const bool bSettled =
				Thug && Thug->GetHitReactionComponent() && Thug->GetHitReactionComponent()->IsKnockoutSettled();
			if ((!bSettled || Elapsed < 2.0f) && Elapsed < 6.f)
			{
				return false;
			}
			const FString File = FString::Printf(TEXT("%s_%02d_settled.png"), *K.Label, S.NextCapture);
			// Captured at full speed: a crawl (0.02) disturbed the ragdolls it caught (the solver's corrections over
			// tiny steps).
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			HawkeyeShots::Request(Test, ShotPath(File), false);
			S.Ko.Frames.Add(File);
			S.bKoWatching = false;
			Advance();
			return false;
		}

		default:
			if (HawkeyeShots::GetOutstanding(Test) > 0 && SinceReal < 12.0)
			{
				return false;
			}
			if (IsKnockout(Trial.Kind))
			{
				FinishKnockout();
			}
			return true;
	}
}

void FHawkeyeMeleeAuditRun::FinishBlow()
{
	using namespace HawkeyeMeleeAudit;
	S.bWatching = false;
	S.bAfter = false;
	S.Blows.Add(S.Current);
	const FTrial& Trial = S.Trials[S.Index];
	S.Rows.Add(MakeShared<FJsonValueObject>(BlowJson(Trial, S.Current)));
	const FBlow& B = S.Current;
	if (UWorld* World = FindWorld(); World && FindKate(World))
	{
		// Where her look sits against her driver: it is attached at the identity.
		AHawkeyeCharacter* Kate = FindKate(World);
		const USkeletalMeshComponent* Look = Kate->GetBodyMesh();
		const USkeletalMeshComponent* Driver = Kate->GetMesh();
		if (Look && Driver && Look != Driver)
		{
			int32 Simulating = 0;
			for (const FBodyInstance* Body : Look->Bodies)
			{
				Simulating +=
					Body && Body->BodySetup.IsValid() && Look->IsSimulatingPhysics(Body->BodySetup->BoneName) ? 1 : 0;
			}
			Test->AddInfo(FString::Printf(
				TEXT("Melee audit: Kate's look: relative %s, %.1f cm off the driver component, attached to %s, "
					 "%d bodies simulating, root body %s, collision %d."),
				*Look->GetRelativeLocation().ToCompactString(),
				FVector::Dist(Look->GetComponentLocation(), Driver->GetComponentLocation()),
				*GetNameSafe(Look->GetAttachParent()), Simulating,
				Look->Bodies.Num() > 0 ? *FString::FromInt(Look->Bodies.Num()) : TEXT("no bodies"),
				static_cast<int32>(Look->GetCollisionEnabled())));
		}
	}
	Test->AddInfo(FString::Printf(
		TEXT("Melee audit: %s %s from %.0f cm at %.0f deg: %s at %.2f s (%d frames), capsules %.0f cm, "
			 "driver %s %.0f cm off his body, seen %s %.0f cm, facing %.0f deg; after: stop f%d %.0f ms, clip f%d %s "
			 "(expected %s), "
			 "physics f%d, flash f%d, sound f%d."),
		KindName(Trial.Kind), *B.Attack, Trial.Distance, Trial.Angle,
		B.bLanded ? TEXT("landed") : (B.bWhiffCaps ? TEXT("WHIFF (caps)") : TEXT("WHIFF")), B.HitSeconds, B.HitFrames,
		B.CapsuleGap, *B.DriverLimb, B.DriverLimbGap, *B.SeenLimb, B.SeenLimbGap, B.FacingDegrees, B.HitStopFrame,
		B.HitStopRealMs, B.ClipFrame, *B.ClipSide, *B.ExpectedSide, B.PhysFrame, B.FlashFrame, B.SoundFrame));
}

void FHawkeyeMeleeAuditRun::FinishKnockout()
{
	using namespace HawkeyeMeleeAudit;
	const FKnockout& K = S.Ko;
	TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("kind"), K.Label);
	Row->SetBoolField(TEXT("died"), K.bDied);
	Row->SetStringField(TEXT("struck_bone"), K.Struck);
	Row->SetNumberField(TEXT("throw_bone_cms"), K.ThrowBone);
	Row->SetNumberField(TEXT("throw_body_cms"), K.ThrowBody);
	Row->SetNumberField(TEXT("anim_pelvis_speed_before_cms"), K.AnimPelvisSpeed);
	Row->SetNumberField(TEXT("first_sim_pelvis_speed_cms"), K.FirstSimPelvisSpeed);
	Row->SetNumberField(TEXT("peak_pelvis_speed_cms"), K.PeakPelvisSpeed);
	Row->SetNumberField(TEXT("travel_cm"), K.Travel);
	Row->SetNumberField(TEXT("slide_while_down_cm"), K.SlideWhileDown);
	Row->SetNumberField(TEXT("max_joint_stretch_cm"), K.MaxStretch);
	Row->SetStringField(TEXT("stretch_bone"), K.StretchBone);
	Row->SetNumberField(TEXT("deepest_below_ground_cm"), K.DeepestBelowGround);
	Row->SetNumberField(TEXT("max_seen_vs_driver_pelvis_cm"), K.MaxLookOffset);
	Row->SetNumberField(TEXT("late_max_speed_cms"), K.LateMaxSpeed);
	Row->SetNumberField(TEXT("late_reversals"), K.LateReversals);
	Row->SetNumberField(TEXT("settle_s"), K.SettleSeconds);
	Row->SetNumberField(TEXT("rest_pelvis_over_ground_cm"), K.RestPelvisOverGround);
	Row->SetNumberField(TEXT("drop_cm"), K.DropHeight);
	TArray<TSharedPtr<FJsonValue>> Frames;
	for (const FString& Frame : K.Frames)
	{
		Frames.Add(MakeShared<FJsonValueString>(Frame));
	}
	Row->SetArrayField(TEXT("frames"), Frames);
	S.Knockouts.Add(MakeShared<FJsonValueObject>(Row));
	Test->AddInfo(FString::Printf(
		TEXT("Melee audit: %s: died %d on %s, thrown bone %.0f / body %.0f cm/s, pelvis before %.0f, first sim "
			 "%.0f, peak %.0f cm/s; travelled %.0f cm (%.0f sliding on the ground); stretch %.1f cm (%s); %.1f cm "
			 "under the ground; seen "
			 "body off the driver by %.1f cm; after 1.5 s %.0f cm/s, %d reversals; settled %.2f s, pelvis %.0f cm up."),
		*K.Label, K.bDied ? 1 : 0, *K.Struck, K.ThrowBone, K.ThrowBody, K.AnimPelvisSpeed, K.FirstSimPelvisSpeed,
		K.PeakPelvisSpeed, K.Travel, K.SlideWhileDown, K.MaxStretch, *K.StretchBone, K.DeepestBelowGround,
		K.MaxLookOffset, K.LateMaxSpeed, K.LateReversals, K.SettleSeconds, K.RestPelvisOverGround));
}

void FHawkeyeMeleeAuditRun::FinishSmooth(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeMeleeAudit;
	const FString Prefix = KindName(S.Trials[S.Index].Kind);
	// The biggest one-frame move against the speeds either side (a snap), and her speed the frame the swing ends (a
	// slide).
	float MaxStep = 0.f;
	float MaxSpeed = 0.f;
	float EndSpeed = -1.f;
	float MaxYawRate = 0.f;
	float WorstJerk = 0.f;
	for (int32 I = 1; I < S.KatePath.Num(); ++I)
	{
		const float Dt = FMath::Max(S.KateDt[I], 1e-4f);
		const float Step = FVector::Dist2D(S.KatePath[I], S.KatePath[I - 1]);
		const float Speed = Step / Dt;
		MaxStep = FMath::Max(MaxStep, Step);
		MaxSpeed = FMath::Max(MaxSpeed, Speed);
		MaxYawRate = FMath::Max(MaxYawRate, FMath::Abs(FRotator::NormalizeAxis(S.KateYaw[I] - S.KateYaw[I - 1])) / Dt);
		if (I >= 2)
		{
			const float Before =
				FVector::Dist2D(S.KatePath[I - 1], S.KatePath[I - 2]) / FMath::Max(S.KateDt[I - 1], 1e-4f);
			WorstJerk = FMath::Max(WorstJerk, FMath::Abs(Speed - Before));
		}
		if (EndSpeed < 0.f && S.KatePhase[I] == static_cast<uint8>(EMeleePhase::Idle) &&
			S.KatePhase[I - 1] != static_cast<uint8>(EMeleePhase::Idle))
		{
			EndSpeed = Speed;
		}
	}
	S.Extra->SetNumberField(Prefix + TEXT("_max_frame_step_cm"), MaxStep);
	S.Extra->SetNumberField(Prefix + TEXT("_max_speed_cms"), MaxSpeed);
	S.Extra->SetNumberField(Prefix + TEXT("_speed_at_swing_end_cms"), EndSpeed);
	S.Extra->SetNumberField(Prefix + TEXT("_max_yaw_rate_dps"), MaxYawRate);
	S.Extra->SetNumberField(Prefix + TEXT("_worst_speed_change_per_frame_cms"), WorstJerk);
	// Her clips' blends.
	if (const UCombatAnimSet* Set = Kate->GetMeleeComponent()->GetAnimSet())
	{
		for (const ECombatAnimRole Role :
			{ECombatAnimRole::Light1, ECombatAnimRole::Light2, ECombatAnimRole::Light3, ECombatAnimRole::Heavy})
		{
			if (const UAnimMontage* Montage = Set->ResolveMontage(Role))
			{
				S.Extra->SetStringField(FString::Printf(TEXT("blend_%s"), *UEnum::GetValueAsString(Role)),
					FString::Printf(TEXT("%s in %.2f out %.2f, %.2f s long"), *Montage->GetName(),
						Montage->BlendIn.GetBlendTime(), Montage->BlendOut.GetBlendTime(), Montage->GetPlayLength()));
			}
		}
	}
	Test->AddInfo(FString::Printf(
		TEXT("Melee audit: %s: biggest frame step %.1f cm, top speed %.0f cm/s, %.0f cm/s as the swing ended, "
			 "turn up to %.0f deg/s, worst speed change in a frame %.0f cm/s."),
		*Prefix, MaxStep, MaxSpeed, EndSpeed, MaxYawRate, WorstJerk));
}

void FHawkeyeMeleeAuditRun::Write(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeMeleeAudit;
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("blows"), S.Rows);
	Root->SetArrayField(TEXT("knockouts"), S.Knockouts);
	// The physics the ragdoll runs under.
	const UPhysicsSettings* Physics = UPhysicsSettings::Get();
	S.Extra->SetBoolField(TEXT("phys_substepping"), Physics->bSubstepping);
	S.Extra->SetNumberField(TEXT("phys_max_depenetration"), Physics->MaxDepenetrationVelocity);
	S.Extra->SetNumberField(TEXT("phys_max_delta"), Physics->MaxPhysicsDeltaTime);
	S.Extra->SetNumberField(TEXT("phys_position_iterations"), Physics->SolverOptions.PositionIterations);
	S.Extra->SetNumberField(TEXT("phys_velocity_iterations"), Physics->SolverOptions.VelocityIterations);
	S.Extra->SetNumberField(TEXT("phys_projection_iterations"), Physics->SolverOptions.ProjectionIterations);
	if (AThugCharacter* Probe = HawkeyeMeleeAudit::SpawnFoe(World, S.StreetFeet + S.Along * 900.f, 0.f))
	{
		USkeletalMeshComponent* Body = Probe->GetMesh();
		float Mass = 0.f;
		float Damping = 0.f;
		int32 Count = 0;
		int32 Overrides = 0;
		for (const FBodyInstance* Instance : Body->Bodies)
		{
			if (Instance)
			{
				Mass += Instance->GetBodyMass();
				Damping += Instance->LinearDamping;
				Overrides += Instance->GetOverrideMaxDepenetrationVelocity() ? 1 : 0;
				++Count;
			}
		}
		S.Extra->SetStringField(TEXT("thug_physics_asset"), GetNameSafe(Body->GetPhysicsAsset()));
		S.Extra->SetNumberField(TEXT("thug_bodies"), Count);
		S.Extra->SetNumberField(TEXT("thug_asset_mass_kg"), Mass);
		S.Extra->SetNumberField(TEXT("thug_ragdoll_mass_kg"),
			Probe->GetHitReactionComponent() ? Probe->GetHitReactionComponent()->RagdollMassKg : 0.f);
		S.Extra->SetNumberField(TEXT("thug_mean_linear_damping"), Count ? Damping / Count : 0.f);
		S.Extra->SetNumberField(TEXT("thug_depenetration_overrides"), Overrides);
		S.Extra->SetStringField(TEXT("thug_ragdoll_vs_pawn"),
			Body->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore ? TEXT("ignore") : TEXT("blocks"));
		S.Extra->SetStringField(TEXT("thug_seen_mesh"),
			GetNameSafe(Probe->GetBodyMesh() ? Probe->GetBodyMesh()->GetSkeletalMeshAsset() : nullptr));
		S.Extra->SetStringField(TEXT("kate_seen_mesh"),
			GetNameSafe(Kate->GetBodyMesh() ? Kate->GetBodyMesh()->GetSkeletalMeshAsset() : nullptr));
		Probe->Destroy();
	}
	Root->SetObjectField(TEXT("extra"), S.Extra);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("melee_feel_audit.json"));
	FFileHelper::SaveStringToFile(Text, *Path);
	Test->AddInfo(FString::Printf(
		TEXT("Melee audit: wrote %s (%d blows, %d knockouts)."), *Path, S.Rows.Num(), S.Knockouts.Num()));

	// The limits (gameplay-semantics.md, "Melee feel"): a landed blow's limb on the body, a hit in the reach band that
	// lands, a fall that stays a fall.
	for (const FBlow& B : S.Blows)
	{
		if (B.AttackerSeenHandOffset > 30.f || B.VictimSeenHeadOffset > 30.f)
		{
			Test->AddError(FString::Printf(
				TEXT("Melee audit: %s: a seen body has come off its driver (hand %.0f cm, head %.0f cm)."), *B.Attack,
				B.AttackerSeenHandOffset, B.VictimSeenHeadOffset));
		}
		if (B.bLanded && B.Attack != TEXT("arrow") && B.Attack != TEXT("finisher") && B.DriverLimbGap > 25.f)
		{
			Test->AddError(
				FString::Printf(TEXT("Melee audit: %s landed with the limb %.0f cm off the body (25 is the limit)."),
					*B.Attack, B.DriverLimbGap));
		}
	}
}

bool FHawkeyeMeleeAuditRun::Update()
{
	using namespace HawkeyeMeleeAudit;
	UWorld* World = FindWorld();
	AHawkeyeCharacter* Kate = FindKate(World);
	if (!World || !Kate)
	{
		Test->AddError(TEXT("Melee audit: no world or no Kate."));
		return true;
	}
	if (!bStarted)
	{
		bStarted = true;
		StartReal = FPlatformTime::Seconds();
		S = FState();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		HawkeyeFreezePartner(World);
		TActorIterator<APlayerStart> Start(World);
		if (Start)
		{
			const FVector Away = Start->GetActorForwardVector().GetSafeNormal2D();
			S.Along = FVector::CrossProduct(FVector::UpVector, Away);
			S.bHaveStreet = FindFlatStreet(World, Start->GetActorLocation() - Away * 400.f, S.Along, S.StreetFeet);
		}
		if (!S.bHaveStreet)
		{
			Test->AddError(TEXT("Melee audit: no flat street by the PlayerStart."));
			return true;
		}
		for (const EKind Kind : {EKind::KateLight, EKind::KateHeavy})
		{
			for (const float Distance : {60.f, 100.f, 150.f, 200.f, 250.f})
			{
				for (const float Angle : {0.f, 30.f, 60.f})
				{
					S.Trials.Add({Kind, Distance, Angle});
				}
			}
		}
		S.Trials.Add({EKind::KateChain, 100.f, 0.f});
		for (const float Distance : {80.f, 120.f, 160.f, 200.f})
		{
			S.Trials.Add({EKind::ThugFists, Distance, 0.f});
		}
		S.Trials.Add({EKind::ThugFists, 120.f, 45.f});
		// Each knockout three times: one fall is noise (the heavy's travelled 87 and 269 cm on the same code).
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			S.Trials.Add({EKind::KoLight, 150.f, 0.f});
			S.Trials.Add({EKind::KoHeavy, 150.f, 0.f});
			S.Trials.Add({EKind::KoFinisher, 120.f, 0.f});
			S.Trials.Add({EKind::KoArrow, 600.f, 0.f});
			S.Trials.Add({EKind::KoLedge, 260.f, 0.f});
		}
		S.Trials.Add({EKind::WallProbe, 220.f, 0.f});
		S.Trials.Add({EKind::LedgeProbe, 380.f, 0.f});
		S.Trials.Add({EKind::Smooth, 200.f, 0.f});
		S.Trials.Add({EKind::Buffer, 100.f, 0.f});
		// -MeleeAuditOnly=thug_fists,ko_heavy runs only those kinds (tuning runs).
		FString Only;
		if (FParse::Value(FCommandLine::Get(), TEXT("MeleeAuditOnly="), Only, false))
		{
			TArray<FString> Kinds;
			Only.ParseIntoArray(Kinds, TEXT(","));
			S.Trials.RemoveAll([&Kinds](const FTrial& Trial) { return !Kinds.Contains(KindName(Trial.Kind)); });
		}
		S.TickHook = FWorldDelegates::OnWorldPostActorTick.AddStatic(&OnPostActorTick);
		Next(World);
		return false;
	}
	if (FPlatformTime::Seconds() - StartReal > 900.0)
	{
		Test->AddError(TEXT("Melee audit: over 15 minutes; stopped."));
	}
	else if (S.Trials.IsValidIndex(S.Index))
	{
		if (!Run(World, Kate))
		{
			return false;
		}
		Next(World);
		if (S.Trials.IsValidIndex(S.Index))
		{
			return false;
		}
	}
	FWorldDelegates::OnWorldPostActorTick.Remove(S.TickHook);
	ClearTrial(World);
	if (ACameraActor* Camera = S.Camera.Get())
	{
		Camera->Destroy();
	}
	World->GetFirstPlayerController()->SetViewTarget(Kate);
	Kate->GetHealthComponent()->Revive(Kate->GetHealthComponent()->GetMaxHealth());
	Write(World, Kate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAuditMeleeFeel, "Hawkeye.Audit.MeleeFeel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAuditMeleeFeel::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the melee feel audit."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeAuditRun(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
