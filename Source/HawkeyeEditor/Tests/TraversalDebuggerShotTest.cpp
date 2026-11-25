#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "STraversalDebuggerPanel.h"
#include "Styling/AppStyle.h"
#include "TextureResource.h"
#include "TraversalDebuggerSubsystem.h"
#include "Traversal/GrappleReachWorld.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The traversal debugger's tab as an image, for the demo video (docs/demo/shot-list.md, shot 8). Opt-in: needs a real
 * renderer (no -nullrhi) and -HawkeyeDemo; with the district open as the editor world it runs the audit on it and a
 * "reach from here" from the first street, draws the tab's own widget (the one the Tools menu opens) off screen with the
 * widget renderer, and writes Saved/Demo/traversal_debugger/panel.png. No window is opened and nothing is faked: the rows
 * are the audit's.
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -HawkeyeDemo -unattended -nosplash -nop4 -log
 *       -ExecCmds="Automation RunTests Hawkeye.Editor.TraversalDebugger.Shot; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalDebuggerShotTest, "Hawkeye.Editor.TraversalDebugger.Shot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHawkeyeTraversalDebuggerShotTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the tab's picture needs a renderer; run without -nullrhi."));
		return true;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("HawkeyeDemo")))
	{
		AddWarning(TEXT("Skipped (demo capture is opt-in): pass -HawkeyeDemo."));
		return true;
	}
	UTraversalDebuggerSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UTraversalDebuggerSubsystem>() : nullptr;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!Subsystem || !World || !FGrappleReachWorld::FindSpawner(World))
	{
		AddError(TEXT("The editor world is not the district."));
		return true;
	}
	if (!Subsystem->RefreshScene())
	{
		AddError(Subsystem->GetStatus());
		return true;
	}
	if (!Subsystem->RunAudit(false))
	{
		AddError(Subsystem->GetStatus());
		return true;
	}
	const FGrappleReachScene& Scene = Subsystem->GetScene();
	const FVector2D Street = Scene.Streets.Num() > 0 && Scene.Streets[0].Points.Num() > 0 ? Scene.Streets[0].Points[0] : FVector2D::ZeroVector;
	Subsystem->ReachFrom(FVector(Street.X, Street.Y, 5000.f), TEXT("the first street"));
	AddInfo(FString::Printf(TEXT("TraversalDebugger shot: %s"), *Subsystem->GetStatus()));

	const FVector2D Size(1000.0, 1000.0);
	const TSharedRef<SWidget> Root = SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(STraversalDebuggerPanel)
			]
		];
	FWidgetRenderer* Renderer = new FWidgetRenderer(/*bUseGammaCorrection=*/true, /*bInClearTarget=*/true);
	UTextureRenderTarget2D* Target = nullptr;
	// The lists build their rows on the first pass; the second draws them.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		Target = Renderer->DrawWidget(Root, Size);
		FlushRenderingCommands();
	}
	if (!Target)
	{
		AddError(TEXT("The widget renderer made no target."));
		delete Renderer;
		return true;
	}
	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	const bool bRead = Resource && Resource->ReadPixels(Pixels);
	if (!bRead || Pixels.Num() == 0)
	{
		AddError(TEXT("Reading the target back failed."));
		delete Renderer;
		return true;
	}
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demo") / TEXT("traversal_debugger"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = Dir / TEXT("panel.png");
	const FImageView Image(Pixels.GetData(), Target->SizeX, Target->SizeY);
	if (!FImageUtils::SaveImageByExtension(*Path, Image))
	{
		AddError(FString::Printf(TEXT("Could not write %s."), *Path));
	}
	else
	{
		AddInfo(FString::Printf(TEXT("TraversalDebugger shot: wrote %s (%dx%d)."), *Path, Target->SizeX, Target->SizeY));
	}
	delete Renderer;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
