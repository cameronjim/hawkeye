#include "World/HawkeyeCapture.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Hawkeye.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace HawkeyeCapture
{
	/** GFrameCounter of the last frame whose draw took one of these captures; 0 before any. */
	static uint64 LastCaptureFrame = 0;

	enum class EState : uint8
	{
		Queued,
		Capturing,
		Writing,
		Done,
		Failed,
	};

	struct FShot
	{
		const void* Owner = nullptr;
		FString Path;
		bool bShowUI = true;
		FDateTime RequestedUtc;
		double IssuedSeconds = 0.0;
		EState State = EState::Queued;
		FOnDone OnDone;
		/** Set by the writer thread: 0 running, 1 written, -1 failed. */
		TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe> WriteResult;
	};

	/** Ticked by the core ticker, so nothing that asked for a shot has to tick it. */
	class FQueue
	{
	public:
		static FQueue& Get()
		{
			static FQueue Queue;
			return Queue;
		}

		void Add(const void* Owner, const FString& Path, bool bShowUI, FOnDone OnDone)
		{
			const int32 Busy = Current;
			FShot& Shot = Shots.AddDefaulted_GetRef();
			Shot.Owner = Owner;
			Shot.Path = Path;
			Shot.bShowUI = bShowUI;
			Shot.RequestedUtc = FDateTime::UtcNow();
			Shot.OnDone = MoveTemp(OnDone);
			if (Busy != INDEX_NONE || FScreenshotRequest::IsScreenshotRequested())
			{
				UE_LOG(LogHawkeye, Log, TEXT("Shots: %s waits for %s."), *FPaths::GetCleanFilename(Path),
					Busy != INDEX_NONE ? *FPaths::GetCleanFilename(Shots[Busy].Path) : TEXT("another capture"));
			}
			if (!TickHandle.IsValid())
			{
				// The PNG writer lives in a module: load it here on the game thread, not on the worker.
				FModuleManager::Get().LoadModule(TEXT("ImageWrapper"));
				TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FQueue::Tick));
			}
			// With nothing in flight it goes out now, so the frame that asked is the one captured.
			Tick(0.f);
		}

		int32 CountOutstanding(const void* Owner) const
		{
			int32 N = 0;
			for (const FShot& Shot : Shots)
			{
				N += Shot.Owner == Owner && (Shot.State == EState::Queued || Shot.State == EState::Capturing
					|| Shot.State == EState::Writing) ? 1 : 0;
			}
			return N;
		}

		int32 CountFailed(const void* Owner) const
		{
			int32 N = 0;
			for (const FShot& Shot : Shots)
			{
				N += Shot.Owner == Owner && Shot.State == EState::Failed ? 1 : 0;
			}
			return N;
		}

		void Forget(const void* Owner)
		{
			// Indices into Shots stay valid while one is in flight.
			if (Current != INDEX_NONE)
			{
				return;
			}
			Shots.RemoveAll([Owner](const FShot& Shot)
			{
				return Shot.Owner == Owner && (Shot.State == EState::Done || Shot.State == EState::Failed);
			});
		}

	private:
		bool Tick(float /*DeltaSeconds*/)
		{
			const double Now = FPlatformTime::Seconds();
			if (Current != INDEX_NONE)
			{
				FShot& Shot = Shots[Current];
				if (Shot.State == EState::Capturing && Now - Shot.IssuedSeconds > CaptureTimeoutSeconds)
				{
					Unbind();
					// Take the stale request back, or it would capture the next frame asked for under this name.
					FScreenshotRequest::Reset();
					const int32 Index = Current;
					Current = INDEX_NONE;
					Fail(Index, FString::Printf(TEXT("the viewport never drew it in %.0f s (%s)"), CaptureTimeoutSeconds,
						*DescribeViewport()));
				}
				else if (Shot.State == EState::Writing && Shot.WriteResult.IsValid() && Shot.WriteResult->Load() != 0)
				{
					const int32 Index = Current;
					Current = INDEX_NONE;
					Check(Index, Shot.WriteResult->Load() > 0);
				}
				else if (Shot.State == EState::Writing && Now - Shot.IssuedSeconds > CaptureTimeoutSeconds * 3.0)
				{
					const int32 Index = Current;
					Current = INDEX_NONE;
					Fail(Index, TEXT("the PNG writer never finished"));
				}
			}
			if (Current == INDEX_NONE)
			{
				Issue(Now);
			}
			return true;
		}

		void Issue(double Now)
		{
			// Someone else's capture (a console HighResShot) owns the single request slot: wait for it.
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				return;
			}
			const int32 Next = Shots.IndexOfByPredicate([](const FShot& Shot) { return Shot.State == EState::Queued; });
			if (Next == INDEX_NONE)
			{
				return;
			}
			FShot& Shot = Shots[Next];
			Current = Next;
			Shot.IssuedSeconds = Now;
			Shot.State = EState::Capturing;
			CapturedHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FQueue::HandleCaptured);
			FScreenshotRequest::RequestScreenshot(Shot.Path, Shot.bShowUI, /*bAddUniqueSuffix=*/false);
		}

		/** Inside the viewport's draw: copy the pixels and hand the PNG to a worker. */
		void HandleCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
		{
			Unbind();
			LastCaptureFrame = GFrameCounter;
			if (Current == INDEX_NONE || Shots[Current].State != EState::Capturing)
			{
				return;
			}
			FShot& Shot = Shots[Current];
			Shot.State = EState::Writing;
			Shot.WriteResult = MakeShared<TAtomic<int32>, ESPMode::ThreadSafe>(0);
			TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe> Result = Shot.WriteResult;
			const FString Path = Shot.Path;
			Async(EAsyncExecution::ThreadPool, [Width, Height, Pixels = Colors, Path, Result]()
			{
				const FImageView Image(Pixels.GetData(), Width, Height);
				const bool bSaved = FImageUtils::SaveImageByExtension(*Path, Image);
				Result->Store(bSaved ? 1 : -1);
			});
		}

		void Check(int32 Index, bool bSaved)
		{
			FShot& Shot = Shots[Index];
			IFileManager& Files = IFileManager::Get();
			const bool bExists = Files.FileExists(*Shot.Path);
			const int64 Size = bExists ? Files.FileSize(*Shot.Path) : -1;
			const FDateTime Stamp = bExists ? Files.GetTimeStamp(*Shot.Path) : FDateTime::MinValue();
			if (bSaved && IsFreshCapture(bExists, Size, Stamp, Shot.RequestedUtc))
			{
				Shot.State = EState::Done;
				Finish(Index, true, FString());
				return;
			}
			FString Why;
			if (!bSaved)
			{
				Why = TEXT("the PNG could not be written");
			}
			else if (!bExists)
			{
				Why = TEXT("there is no file after the capture");
			}
			else if (Size <= 0)
			{
				Why = TEXT("the file is empty");
			}
			else
			{
				Why = FString::Printf(TEXT("the file is from %s, before the request at %s"), *Stamp.ToString(),
					*Shot.RequestedUtc.ToString());
			}
			Fail(Index, Why);
		}

		void Fail(int32 Index, const FString& Why)
		{
			FShot& Shot = Shots[Index];
			Shot.State = EState::Failed;
			const FString Message = FString::Printf(TEXT("Screenshot %s was not captured: %s."), *Shot.Path, *Why);
			UE_LOG(LogHawkeye, Error, TEXT("Shots: %s"), *Message);
			Finish(Index, false, Message);
		}

		/** Runs the shot's callback once. The callback may queue another shot, so it is moved out first. */
		void Finish(int32 Index, bool bOk, const FString& Why)
		{
			FOnDone OnDone = MoveTemp(Shots[Index].OnDone);
			Shots[Index].OnDone = FOnDone();
			if (OnDone)
			{
				OnDone(bOk, Why);
			}
		}

		void Unbind()
		{
			if (CapturedHandle.IsValid())
			{
				UGameViewportClient::OnScreenshotCaptured().Remove(CapturedHandle);
				CapturedHandle.Reset();
			}
		}

		static FString DescribeViewport()
		{
			UGameViewportClient* Client = GEngine ? GEngine->GameViewport.Get() : nullptr;
			const FViewport* Viewport = Client ? Client->Viewport : nullptr;
			const TSharedPtr<SWindow> Window = Client ? Client->GetWindow() : nullptr;
			const FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
			const bool bActive = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive();
			return FString::Printf(TEXT("viewport %dx%d, window %s%s, application %s"), Size.X, Size.Y,
				Window.IsValid() ? TEXT("open") : TEXT("missing"),
				Window.IsValid() && Window->IsWindowMinimized() ? TEXT(" and minimised") : TEXT(""),
				bActive ? TEXT("in front") : TEXT("in the background"));
		}

		TArray<FShot> Shots;
		int32 Current = INDEX_NONE;
		FDelegateHandle CapturedHandle;
		FTSTicker::FDelegateHandle TickHandle;
	};

	void Request(const FString& FullPath, bool bShowUI, const void* Owner, FOnDone OnDone)
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(FullPath));
		FQueue::Get().Add(Owner, FullPath, bShowUI, MoveTemp(OnDone));
	}

	int32 GetOutstanding(const void* Owner)
	{
		return FQueue::Get().CountOutstanding(Owner);
	}

	int32 GetFailures(const void* Owner)
	{
		return FQueue::Get().CountFailed(Owner);
	}

	void ForgetFinished(const void* Owner)
	{
		FQueue::Get().Forget(Owner);
	}

	bool DidCaptureLastFrame()
	{
		return LastCaptureFrame != 0 && GFrameCounter - LastCaptureFrame <= 1;
	}

	bool IsFreshCapture(bool bExists, int64 FileSize, const FDateTime& FileTimeUtc, const FDateTime& RequestedUtc,
		double SlackSeconds)
	{
		return bExists && FileSize > 0 && FileTimeUtc >= RequestedUtc - FTimespan::FromSeconds(SlackSeconds);
	}
}
