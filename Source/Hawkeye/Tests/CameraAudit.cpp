#include "Tests/CameraAudit.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Hawkeye.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace HawkeyeCameraAudit
{
	const TCHAR* RuleName(ERule Rule)
	{
		switch (Rule)
		{
		case ERule::FloorUnderKate: return TEXT("FloorUnderKate");
		case ERule::FloorUnderLens: return TEXT("FloorUnderLens");
		case ERule::LensOverlap: return TEXT("LensOverlap");
		case ERule::NearPlane: return TEXT("NearPlane");
		case ERule::LensJump: return TEXT("LensJump");
		case ERule::AngularSpeed: return TEXT("AngularSpeed");
		case ERule::BodyToggle: return TEXT("BodyToggle");
		default: return TEXT("?");
		}
	}

	// --- The rules ------------------------------------------------------------------------------

	void FRuleBook::Reset()
	{
		bHasPrevious = false;
		ToggleTimes.Reset();
	}

	void FRuleBook::Evaluate(const FFrame& Frame, TArray<FViolation>& Out)
	{
		auto Add = [&](ERule Rule, float Value, float Limit, float Severity)
		{
			FViolation Violation;
			Violation.Rule = Rule;
			Violation.Time = Frame.Time;
			Violation.Location = Frame.Lens;
			Violation.Mode = Frame.Mode;
			Violation.Speed = Frame.KateSpeed;
			Violation.Value = Value;
			Violation.Limit = Limit;
			Violation.Severity = Severity;
			Out.Add(MoveTemp(Violation));
		};

		// Where the lens is, whatever the last frame was.
		const float NoFloorHalf = HawkeyeCameraRules::NoFloor * 0.5f;
		if (Frame.FloorUnderKate > NoFloorHalf)
		{
			const float Clearance = static_cast<float>(Frame.Lens.Z) - Frame.FloorUnderKate;
			if (Clearance < Limits.MinFloorClearance)
			{
				Add(ERule::FloorUnderKate, Clearance, Limits.MinFloorClearance, Limits.MinFloorClearance - Clearance);
			}
		}
		if (Frame.FloorUnderLens > NoFloorHalf)
		{
			const float Clearance = static_cast<float>(Frame.Lens.Z) - Frame.FloorUnderLens;
			if (Clearance < Limits.MinFloorClearance)
			{
				Add(ERule::FloorUnderLens, Clearance, Limits.MinFloorClearance, Limits.MinFloorClearance - Clearance);
			}
		}
		if (Frame.bLensOverlap)
		{
			Add(ERule::LensOverlap, 1.f, 0.f, 1.f);
		}
		if (Frame.NearPlaneHits > 0)
		{
			Add(ERule::NearPlane, static_cast<float>(Frame.NearPlaneHits), 0.f, static_cast<float>(Frame.NearPlaneHits));
		}

		// The body's hide and show: counted in the second before now.
		if (bHasPrevious && Previous.bBodyHidden != Frame.bBodyHidden)
		{
			ToggleTimes.Add(Frame.Time);
		}
		ToggleTimes.RemoveAll([&Frame](double Time) { return Frame.Time - Time > 1.0; });
		if (ToggleTimes.Num() >= Limits.BodyToggleCount && bHasPrevious && Previous.bBodyHidden != Frame.bBodyHidden)
		{
			Add(ERule::BodyToggle, static_cast<float>(ToggleTimes.Num()), static_cast<float>(Limits.BodyToggleCount - 1),
				static_cast<float>(ToggleTimes.Num()));
		}

		// What changed since the last frame: not on a cut, which is the one frame it may.
		if (bHasPrevious && !Frame.bCut && Frame.Delta > KINDA_SMALL_NUMBER)
		{
			const float Seconds = Frame.Delta;
			// The lens relative to her, in a frame that turns with the yaw (the view orbiting her is the player's to do).
			const FVector Before = Previous.Lens - Previous.Pivot;
			const FVector Now = Frame.Lens - Frame.Pivot;
			const FVector BeforeLocal = FRotator(0.f, -Previous.View.Yaw, 0.f).RotateVector(Before);
			const FVector NowLocal = FRotator(0.f, -Frame.View.Yaw, 0.f).RotateVector(Now);
			// The pitch's orbit too: a view that swings through an angle carries a lens that far from her through that arc.
			const float Turned = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Previous.View.Vector(), Frame.View.Vector())), -1.f, 1.f));
			const float Orbit = static_cast<float>(Before.Size()) * Turned;
			const float Moved = static_cast<float>(FVector::Dist(BeforeLocal, NowLocal));
			const float Speed = FMath::Max(Moved - Orbit, 0.f) / Seconds;
			if (Speed > Limits.MaxLensSpeed)
			{
				Add(ERule::LensJump, Speed, Limits.MaxLensSpeed, Speed / Limits.MaxLensSpeed);
			}
			if (!Frame.bScriptedBeat)
			{
				const float Degrees = FMath::RadiansToDegrees(Turned) / Seconds;
				if (Degrees > Limits.MaxAngularSpeed)
				{
					Add(ERule::AngularSpeed, Degrees, Limits.MaxAngularSpeed, Degrees / Limits.MaxAngularSpeed);
				}
			}
		}
		Previous = Frame;
		bHasPrevious = true;
	}

	// --- The session: one lap under audit ---------------------------------------------------------

	namespace
	{
		struct FSession
		{
			FAutomationTestBase* Test = nullptr;
			FString Lap;
			FRuleBook Rules;
			TArray<FViolation> All;
			int32 Counts[static_cast<int32>(ERule::Count)] = {};
			bool bReported[static_cast<int32>(ERule::Count)] = {};
			int32 Frames = 0;
			int32 Cuts = 0;
			int32 ScriptedFrames = 0;
			int32 LastCutCount = 0;
			bool bHasLastCut = false;
			double ScriptedUntil = -1.0;
			FString ScriptedWhy;
		};

		TUniquePtr<FSession> GSession;
		bool bInstalled = false;

		/** The highest floor under Origin within Distance: the lens's own channel, what is seen, and the world's static geometry. */
		float FloorBelow(UWorld* World, const FVector& Origin, float Distance, const FCollisionQueryParams& Params)
		{
			const FVector End = Origin - FVector(0.f, 0.f, Distance);
			float Floor = HawkeyeCameraRules::NoFloor;
			FHitResult Hit;
			for (const ECollisionChannel Channel : { ECC_Camera, ECC_Visibility })
			{
				if (World->LineTraceSingleByChannel(Hit, Origin, End, Channel, Params) && !Hit.bStartPenetrating)
				{
					Floor = FMath::Max(Floor, static_cast<float>(Hit.ImpactPoint.Z));
				}
			}
			if (World->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
				&& !Hit.bStartPenetrating)
			{
				Floor = FMath::Max(Floor, static_cast<float>(Hit.ImpactPoint.Z));
			}
			return Floor;
		}

		/** Where the player's screen is looking from, measured. False when there is nothing to measure (no pawn, a cutscene). */
		bool SampleFrame(UWorld* World, float DeltaSeconds, FSession& Session, FFrame& Out)
		{
			APlayerController* PC = World->GetFirstPlayerController();
			AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
			APlayerCameraManager* Manager = PC ? PC->PlayerCameraManager : nullptr;
			if (!Kate || !Manager || DeltaSeconds <= 0.f)
			{
				return false;
			}
			// A view target that is not her, or one being blended to: a cutscene or a photo mode; not the gameplay camera.
			const bool bBlending = Manager->GetViewTarget() != Kate || Manager->PendingViewTarget.Target != nullptr;
			if (bBlending)
			{
				return false;
			}

			const AWorldSettings* Settings = World->GetWorldSettings();
			Out.Time = World->GetTimeSeconds();
			Out.Delta = HawkeyeCameraRules::UnscaledDelta(DeltaSeconds, Settings ? Settings->GetEffectiveTimeDilation() : 1.f);
			Out.Lens = Manager->GetCameraLocation();
			Out.View = Manager->GetCameraRotation();
			Out.Pivot = Kate->GetActorLocation();
			Out.KateSpeed = static_cast<float>(Kate->GetVelocity().Size());
			const UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
			Out.Mode = FString::Printf(TEXT("%s%s"), Movement ? *Movement->GetMovementName() : TEXT("?"),
				Kate->GetAimBlendAlpha() > 0.5f ? TEXT("+aim") : TEXT(""));

			const int32 CutCount = Kate->GetCameraCutCount();
			Out.bCut = !Session.bHasLastCut || CutCount != Session.LastCutCount;
			Session.bHasLastCut = true;
			Session.LastCutCount = CutCount;
			Out.bScriptedBeat = Out.Time < Session.ScriptedUntil || Kate->IsLockedOutByTakedown();
			const USkeletalMeshComponent* Body = Kate->GetBodyMesh();
			Out.bBodyHidden = Body && Body->bOwnerNoSee;

			FCollisionQueryParams Params(SCENE_QUERY_STAT(CameraAudit), false, Kate);
			const float HalfHeight = Kate->GetCapsuleComponent() ? Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			Out.FloorUnderKate = FloorBelow(World, Out.Pivot, HalfHeight + 250.f, Params);

			// Under the lens, from just above it but not from above a ceiling it is under.
			FVector Start = Out.Lens + FVector(0.f, 0.f, 50.f);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Out.Lens, Start, ECC_Camera, Params))
			{
				Start.Z = Hit.ImpactPoint.Z - 2.0;
			}
			Out.FloorUnderLens = FloorBelow(World, Start, 600.f + static_cast<float>(Start.Z - Out.Lens.Z), Params);

			Out.bLensOverlap = World->OverlapBlockingTestByChannel(Out.Lens, FQuat::Identity, ECC_Camera,
				FCollisionShape::MakeSphere(Session.Rules.GetLimits().OverlapRadius), Params);

			// The four corners of the near plane, from the lens.
			const FMinimalViewInfo& View = Manager->GetCameraCacheView();
			const float Aspect = View.AspectRatio > 0.1f ? View.AspectRatio : 16.f / 9.f;
			const float Near = GNearClippingPlane;
			const float HalfWidth = Near * FMath::Tan(FMath::DegreesToRadians(Manager->GetFOVAngle() * 0.5f));
			const FRotationMatrix Axes(Out.View);
			for (const float Across : { -1.f, 1.f })
			{
				for (const float Up : { -1.f, 1.f })
				{
					const FVector Corner = Out.Lens + Axes.GetUnitAxis(EAxis::X) * Near + Axes.GetUnitAxis(EAxis::Y) * (Across * HalfWidth)
						+ Axes.GetUnitAxis(EAxis::Z) * (Up * HalfWidth / Aspect);
					if (World->LineTraceTestByChannel(Out.Lens, Corner, ECC_Camera, Params))
					{
						++Out.NearPlaneHits;
					}
				}
			}
			return true;
		}

		void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
		{
			FSession* Session = GSession.Get();
			if (!Session || !World || !World->IsGameWorld() || TickType != LEVELTICK_All)
			{
				return;
			}
			FFrame Frame;
			if (!SampleFrame(World, DeltaSeconds, *Session, Frame))
			{
				Session->Rules.Reset();
				Session->bHasLastCut = false;
				return;
			}
			++Session->Frames;
			Session->Cuts += Frame.bCut ? 1 : 0;
			Session->ScriptedFrames += Frame.bScriptedBeat ? 1 : 0;

			TArray<FViolation> Found;
			Session->Rules.Evaluate(Frame, Found);
			for (FViolation& Violation : Found)
			{
				const int32 Index = static_cast<int32>(Violation.Rule);
				++Session->Counts[Index];
				if (!Session->bReported[Index])
				{
					// The first of each rule fails the lap now: the test's errors are counted when it ends.
					Session->bReported[Index] = true;
					if (Session->Test)
					{
						Session->Test->AddError(FString::Printf(TEXT("Camera audit, %s: %.1f against %.1f at %.2f s, %s, Kate at %.0f cm/s, lens %s."),
							RuleName(Violation.Rule), Violation.Value, Violation.Limit, Violation.Time, *Violation.Mode, Violation.Speed,
							*Violation.Location.ToCompactString()));
					}
				}
				if (Session->All.Num() < 5000)
				{
					Session->All.Add(MoveTemp(Violation));
				}
			}
		}

		TSharedRef<FJsonObject> ToJson(const FViolation& Violation)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("rule"), RuleName(Violation.Rule));
			Object->SetNumberField(TEXT("time"), Violation.Time);
			Object->SetStringField(TEXT("location"), Violation.Location.ToCompactString());
			Object->SetStringField(TEXT("mode"), Violation.Mode);
			Object->SetNumberField(TEXT("kate_speed"), Violation.Speed);
			Object->SetNumberField(TEXT("value"), Violation.Value);
			Object->SetNumberField(TEXT("limit"), Violation.Limit);
			return Object;
		}

		void WriteReport(const FSession& Session)
		{
			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("lap"), Session.Lap);
			Root->SetNumberField(TEXT("frames_audited"), Session.Frames);
			Root->SetNumberField(TEXT("cuts"), Session.Cuts);
			Root->SetNumberField(TEXT("scripted_beat_frames"), Session.ScriptedFrames);
			int32 Total = 0;
			TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < static_cast<int32>(ERule::Count); ++Index)
			{
				const ERule Rule = static_cast<ERule>(Index);
				Total += Session.Counts[Index];
				TArray<FViolation> Mine = Session.All.FilterByPredicate([Rule](const FViolation& V) { return V.Rule == Rule; });
				Mine.Sort([](const FViolation& A, const FViolation& B) { return A.Severity > B.Severity; });
				TArray<TSharedPtr<FJsonValue>> Worst;
				for (int32 Worse = 0; Worse < FMath::Min(Mine.Num(), 5); ++Worse)
				{
					Worst.Add(MakeShared<FJsonValueObject>(ToJson(Mine[Worse])));
				}
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("frames"), Session.Counts[Index]);
				Entry->SetArrayField(TEXT("worst"), Worst);
				Rules->SetObjectField(RuleName(Rule), Entry);
			}
			Root->SetNumberField(TEXT("violations"), Total);
			Root->SetObjectField(TEXT("rules"), Rules);

			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Root, Writer);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("CameraAudit")
				/ (Session.Lap + TEXT(".json")));
			FFileHelper::SaveStringToFile(Text, *Path);
			UE_LOG(LogHawkeye, Log, TEXT("Camera audit %s: %d frames, %d cuts, %d violations; %s"), *Session.Lap, Session.Frames, Session.Cuts, Total, *Path);
		}

		void OnTestStart(FAutomationTestBase* Test)
		{
			if (!Test || !Test->GetBeautifiedTestName().StartsWith(TEXT("Hawkeye.Lap.")))
			{
				return;
			}
			GSession = MakeUnique<FSession>();
			GSession->Test = Test;
			GSession->Lap = Test->GetBeautifiedTestName().RightChop(FString(TEXT("Hawkeye.Lap.")).Len());
		}

		void OnTestEnd(FAutomationTestBase* Test)
		{
			if (GSession && GSession->Test == Test)
			{
				WriteReport(*GSession);
				GSession.Reset();
			}
		}
	}

	void Install()
	{
		if (bInstalled)
		{
			return;
		}
		bInstalled = true;
		FAutomationTestFramework::Get().OnTestStartEvent.AddStatic(&OnTestStart);
		FAutomationTestFramework::Get().OnTestEndEvent.AddStatic(&OnTestEnd);
		FWorldDelegates::OnWorldPostActorTick.AddStatic(&OnWorldPostActorTick);
	}

	void NoteScriptedBeat(float Seconds, const TCHAR* Why)
	{
		if (!GSession)
		{
			return;
		}
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				World = Context.World();
			}
		}
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		GSession->ScriptedUntil = FMath::Max(GSession->ScriptedUntil, Now + Seconds);
		GSession->ScriptedWhy = Why;
	}

	int32 GetViolationCount()
	{
		if (!GSession)
		{
			return 0;
		}
		int32 Total = 0;
		for (const int32 Count : GSession->Counts)
		{
			Total += Count;
		}
		return Total;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
