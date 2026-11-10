#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HawkeyeVfxBuilder.generated.h"

class UNiagaraSystem;

/**
 * Builds the game's Niagara systems headless, so they come from a script like every other asset
 * (Tools/Editor/create_vfx.py drives it). Niagara exposes nothing to Python for editing emitters, so
 * this wraps the editor's own stack view model, the same one the Niagara editor's panels use:
 *
 *   CreateSystem      an empty system asset (or the existing one emptied in place)
 *   AddEmitter        copies one of the Niagara plugin's template emitters (/Niagara/DefaultAssets/
 *                     Templates/Emitters/...) into it under a name
 *   AddModule         appends a module script (/Niagara/Modules/...) to a stage
 *   SetInput          sets a module input by its display name: a literal, "=User.X" to link a user
 *                     parameter (created with the input's type when missing), "di:Prop=Text|..." to
 *                     edit the data interface behind it, or "dyn:/Niagara/DynamicInputs/..." to put a
 *                     dynamic input on it. "A>B" reaches input B of the dynamic input on A.
 *   SetEmitterProperty, SetRendererProperty, SetSystemProperty   reflection, by property name, text values
 *   AddRenderer       a renderer of a class (the light renderer for flashes)
 *   FinishSystem      compiles and reports script errors
 *
 * Modules are named "Stage:Display Name" or just "Display Name"; "#2" picks the second of two.
 * Stages: EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate. Editor builds only; in a
 * cooked game every function logs an error and fails.
 */
UCLASS()
class HAWKEYE_API UHawkeyeVfxBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The system at PackageName (e.g. "/Game/VFX/NS_Smoke"), created, or emptied of emitters and user parameters. Unsaved. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static UNiagaraSystem* CreateSystem(const FString& PackageName);

	/** Copies the emitter asset at EmitterPath into System as Name. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool AddEmitter(UNiagaraSystem* System, const FString& EmitterPath, FName Name);

	/** Adds User.<Name> of Type (float, int, bool, vector, position, color, vec2) with DefaultValue as text. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool AddUserParameter(UNiagaraSystem* System, FName Name, const FString& Type, const FString& DefaultValue);

	/** Appends the module script at ScriptPath to Stage of Emitter. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool AddModule(UNiagaraSystem* System, FName Emitter, const FString& Stage, const FString& ScriptPath);

	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool SetModuleEnabled(UNiagaraSystem* System, FName Emitter, const FString& Module, bool bEnabled);

	/** Sets a module input (see the class comment for the value forms). */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool SetInput(UNiagaraSystem* System, FName Emitter, const FString& Module, const FString& Input,
		const FString& Value);

	/** A property of the emitter's data (SimTarget, bLocalSpace, CalculateBoundsMode, FixedBounds...). */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool SetEmitterProperty(UNiagaraSystem* System, FName Emitter, const FString& Property, const FString& Value);

	/** A property of the emitter's RendererIndex-th renderer (Material, Alignment, FacingMode...). */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool SetRendererProperty(UNiagaraSystem* System, FName Emitter, int32 RendererIndex, const FString& Property,
		const FString& Value);

	/** A property of the system itself (bFixedBounds, FixedBounds, WarmupTime...). */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool SetSystemProperty(UNiagaraSystem* System, const FString& Property, const FString& Value);

	/** Adds a renderer of the class at RendererClassPath ("/Script/Niagara.NiagaraRibbonRendererProperties"). Its index, or -1. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static int32 AddRenderer(UNiagaraSystem* System, FName Emitter, const FString& RendererClassPath);

	/** Compiles the system and waits. False (and the messages logged) when any script failed. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static bool FinishSystem(UNiagaraSystem* System);

	/** Every emitter, module and input as text, for finding names and for verify scripts. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static FString DescribeSystem(UNiagaraSystem* System);

	/** The emitters' names, in order. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static TArray<FName> GetEmitterNames(UNiagaraSystem* System);

	/** Renderers on the emitter, -1 when there is no such emitter. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static int32 GetRendererCount(UNiagaraSystem* System, FName Emitter);

	/** Drops the cached editing state; call once a script is done with the systems. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|VFX")
	static void EndEditing();
};
