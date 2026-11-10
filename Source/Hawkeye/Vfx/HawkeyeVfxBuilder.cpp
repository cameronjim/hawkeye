#include "Vfx/HawkeyeVfxBuilder.h"

#include "Hawkeye.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraTypes.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

namespace HawkeyeVfxBuild
{
	/** One system's editing state, reused across calls until a structural edit drops it. */
	struct FSession
	{
		TWeakObjectPtr<UNiagaraSystem> System;
		TSharedPtr<FNiagaraSystemViewModel> ViewModel;
	};
	static FSession Session;

	static void DropViewModel()
	{
		Session.ViewModel.Reset();
		Session.System.Reset();
	}

	static TSharedPtr<FNiagaraSystemViewModel> GetViewModel(UNiagaraSystem& System)
	{
		if (Session.ViewModel.IsValid() && Session.System.Get() == &System)
		{
			return Session.ViewModel;
		}
		DropViewModel();
		// The same options the engine's own headless users (the audit commandlet, script upgrades) pass.
		TSharedRef<FNiagaraSystemViewModel> ViewModel = MakeShared<FNiagaraSystemViewModel>();
		FNiagaraSystemViewModelOptions Options;
		Options.bCanAutoCompile = false;
		Options.bCanModifyEmittersFromTimeline = false;
		Options.bCanSimulate = false;
		Options.bCompileForEdit = false;
		Options.bIsForDataProcessingOnly = true;
		Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
		// The stack subscribes its messages under this key and asserts it is set.
		const FGuid AssetGuid = System.GetAssetGuid();
		Options.MessageLogGuid.Emplace(AssetGuid.IsValid() ? AssetGuid : FGuid::NewGuid());
		ViewModel->Initialize(System, Options);
		Session.ViewModel = ViewModel;
		Session.System = &System;
		return ViewModel;
	}

	static FNiagaraEmitterHandle* FindHandle(UNiagaraSystem& System, FName Emitter)
	{
		for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			if (Handle.GetName() == Emitter)
			{
				return &Handle;
			}
		}
		return nullptr;
	}

	static TSharedPtr<FNiagaraEmitterHandleViewModel> FindHandleViewModel(UNiagaraSystem& System, FName Emitter)
	{
		TSharedPtr<FNiagaraSystemViewModel> ViewModel = GetViewModel(System);
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& Handle : ViewModel->GetEmitterHandleViewModels())
		{
			if (Handle->GetName() == Emitter)
			{
				return Handle;
			}
		}
		return nullptr;
	}

	static const TCHAR* StageName(ENiagaraScriptUsage Usage)
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::EmitterSpawnScript:
			return TEXT("EmitterSpawn");
		case ENiagaraScriptUsage::EmitterUpdateScript:
			return TEXT("EmitterUpdate");
		case ENiagaraScriptUsage::ParticleSpawnScript:
			return TEXT("ParticleSpawn");
		case ENiagaraScriptUsage::ParticleUpdateScript:
			return TEXT("ParticleUpdate");
		case ENiagaraScriptUsage::SystemSpawnScript:
			return TEXT("SystemSpawn");
		case ENiagaraScriptUsage::SystemUpdateScript:
			return TEXT("SystemUpdate");
		default:
			return TEXT("Other");
		}
	}

	static bool ParseStage(const FString& Stage, ENiagaraScriptUsage& OutUsage)
	{
		static const TPair<const TCHAR*, ENiagaraScriptUsage> Stages[] = {
			{ TEXT("EmitterSpawn"), ENiagaraScriptUsage::EmitterSpawnScript },
			{ TEXT("EmitterUpdate"), ENiagaraScriptUsage::EmitterUpdateScript },
			{ TEXT("ParticleSpawn"), ENiagaraScriptUsage::ParticleSpawnScript },
			{ TEXT("ParticleUpdate"), ENiagaraScriptUsage::ParticleUpdateScript },
		};
		for (const TPair<const TCHAR*, ENiagaraScriptUsage>& Entry : Stages)
		{
			if (Stage.Equals(Entry.Key, ESearchCase::IgnoreCase))
			{
				OutUsage = Entry.Value;
				return true;
			}
		}
		return false;
	}

	static void CollectModules(UNiagaraStackEntry& Entry, TArray<UNiagaraStackModuleItem*>& OutModules)
	{
		TArray<UNiagaraStackEntry*> Children;
		Entry.GetUnfilteredChildren(Children);
		for (UNiagaraStackEntry* Child : Children)
		{
			if (UNiagaraStackModuleItem* Module = Cast<UNiagaraStackModuleItem>(Child))
			{
				OutModules.Add(Module);
			}
			else if (Child && !Cast<UNiagaraStackFunctionInput>(Child))
			{
				CollectModules(*Child, OutModules);
			}
		}
	}

	static TArray<UNiagaraStackModuleItem*> GetModules(UNiagaraSystem& System, FName Emitter)
	{
		TArray<UNiagaraStackModuleItem*> Modules;
		TSharedPtr<FNiagaraEmitterHandleViewModel> Handle = FindHandleViewModel(System, Emitter);
		UNiagaraStackViewModel* Stack = Handle.IsValid() ? Handle->GetEmitterStackViewModel() : nullptr;
		if (UNiagaraStackEntry* Root = Stack ? Stack->GetRootEntry() : nullptr)
		{
			Root->RefreshChildren();
			CollectModules(*Root, Modules);
		}
		return Modules;
	}

	static FString ModuleStage(const UNiagaraStackModuleItem& Module)
	{
		const UNiagaraNodeOutput* Output = Module.GetOutputNode();
		return Output ? StageName(Output->GetUsage()) : TEXT("Other");
	}

	/** "ParticleSpawn:Initialize Particle#2" or "Initialize Particle". */
	static UNiagaraStackModuleItem* FindModule(UNiagaraSystem& System, FName Emitter, const FString& Spec)
	{
		FString Stage;
		FString Name = Spec;
		Spec.Split(TEXT(":"), &Stage, &Name);
		if (!Spec.Contains(TEXT(":")))
		{
			Stage.Reset();
			Name = Spec;
		}
		int32 Wanted = 1;
		FString Base = Name;
		FString Count;
		if (Name.Split(TEXT("#"), &Base, &Count, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Wanted = FMath::Max(1, FCString::Atoi(*Count));
		}
		int32 Seen = 0;
		for (UNiagaraStackModuleItem* Module : GetModules(System, Emitter))
		{
			if (!Module->GetDisplayName().ToString().Equals(Base.TrimStartAndEnd(), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!Stage.IsEmpty() && !ModuleStage(*Module).Equals(Stage, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (++Seen == Wanted)
			{
				return Module;
			}
		}
		return nullptr;
	}

	/** Inputs directly on Owner's function call, looking through categories but not into other inputs. */
	static void CollectInputs(UNiagaraStackEntry& Entry, const UNiagaraNodeFunctionCall* Owner,
		TArray<UNiagaraStackFunctionInput*>& OutInputs)
	{
		TArray<UNiagaraStackEntry*> Children;
		Entry.GetUnfilteredChildren(Children);
		for (UNiagaraStackEntry* Child : Children)
		{
			if (UNiagaraStackFunctionInput* Input = Cast<UNiagaraStackFunctionInput>(Child))
			{
				if (Input->GetInputFunctionCallNodePtr() == Owner)
				{
					OutInputs.Add(Input);
				}
				// The module's own inputs also nest under one another (Lifetime Mode holds Lifetime Min/Max);
				// a dynamic input's children have another owner and are left out.
				CollectInputs(*Input, Owner, OutInputs);
			}
			else if (Child && !Cast<UNiagaraStackModuleItem>(Child))
			{
				CollectInputs(*Child, Owner, OutInputs);
			}
		}
	}

	static UNiagaraStackFunctionInput* FindInput(UNiagaraStackModuleItem& Module, const FString& Path)
	{
		TArray<FString> Parts;
		// ">" and not "/": input names have slashes of their own ("Near / Far Distance").
		Path.ParseIntoArray(Parts, TEXT(">"));
		UNiagaraStackEntry* Scope = &Module;
		const UNiagaraNodeFunctionCall* Owner = &Module.GetModuleNode();
		UNiagaraStackFunctionInput* Found = nullptr;
		for (const FString& Part : Parts)
		{
			if (!Scope || !Owner)
			{
				return nullptr;
			}
			Scope->RefreshChildren();
			TArray<UNiagaraStackFunctionInput*> Inputs;
			CollectInputs(*Scope, Owner, Inputs);
			Found = nullptr;
			for (UNiagaraStackFunctionInput* Input : Inputs)
			{
				if (Input->GetDisplayName().ToString().Equals(Part.TrimStartAndEnd(), ESearchCase::IgnoreCase))
				{
					Found = Input;
					break;
				}
			}
			if (!Found)
			{
				return nullptr;
			}
			Scope = Found;
			Owner = Found->GetDynamicInputNode();
		}
		return Found;
	}

	/** Enum inputs accept the entry's display or authored name as well as its value. */
	static bool ResolveEnum(const UEnum& Enum, const FString& Text, int32& OutValue)
	{
		if (Text.IsNumeric())
		{
			OutValue = FCString::Atoi(*Text);
			return true;
		}
		for (int32 Index = 0; Index < Enum.NumEnums(); ++Index)
		{
			if (Enum.GetDisplayNameTextByIndex(Index).ToString().Equals(Text, ESearchCase::IgnoreCase)
				|| Enum.GetAuthoredNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase)
				|| Enum.GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				OutValue = static_cast<int32>(Enum.GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	/** Fills Memory (of Type's struct) from Text. */
	static bool ParseValue(const FNiagaraTypeDefinition& Type, const FString& Text, uint8* Memory, FString& OutError)
	{
		const FString Value = Text.TrimStartAndEnd();
		if (Type.IsEnum() && Type.GetEnum())
		{
			int32 EnumValue = 0;
			if (!ResolveEnum(*Type.GetEnum(), Value, EnumValue))
			{
				FString Entries;
				for (int32 Index = 0; Index + 1 < Type.GetEnum()->NumEnums(); ++Index)
				{
					Entries += FString::Printf(TEXT("%s'%s'=%lld"), Entries.IsEmpty() ? TEXT("") : TEXT(", "),
						*Type.GetEnum()->GetDisplayNameTextByIndex(Index).ToString(), Type.GetEnum()->GetValueByIndex(Index));
				}
				OutError = FString::Printf(TEXT("no entry '%s' in %s (%s)"), *Value, *Type.GetEnum()->GetName(), *Entries);
				return false;
			}
			reinterpret_cast<FNiagaraInt32*>(Memory)->Value = EnumValue;
			return true;
		}
		const UScriptStruct* Struct = Type.GetScriptStruct();
		if (Struct == FNiagaraTypeDefinition::GetFloatStruct())
		{
			reinterpret_cast<FNiagaraFloat*>(Memory)->Value = FCString::Atof(*Value);
			return true;
		}
		if (Struct == FNiagaraTypeDefinition::GetIntStruct())
		{
			reinterpret_cast<FNiagaraInt32*>(Memory)->Value = FCString::Atoi(*Value);
			return true;
		}
		if (Struct == FNiagaraTypeDefinition::GetBoolStruct())
		{
			reinterpret_cast<FNiagaraBool*>(Memory)->SetValue(Value.ToBool());
			return true;
		}
		if (!Struct)
		{
			OutError = TEXT("input has no value struct");
			return false;
		}
		FStringOutputDevice Errors;
		const TCHAR* End = Struct->ImportText(*Value, Memory, nullptr, PPF_None, &Errors, Struct->GetName());
		if (!End || !Errors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' is not a %s (%s)"), *Value, *Struct->GetName(), *Errors);
			return false;
		}
		return true;
	}

	static bool EnsureUserParameter(UNiagaraSystem& System, const FNiagaraTypeDefinition& Type, FName Name)
	{
		FNiagaraUserRedirectionParameterStore& Store = System.GetExposedParameters();
		const FNiagaraVariable Variable(Type, Name);
		if (Store.IndexOf(Variable) != INDEX_NONE)
		{
			return true;
		}
		FNiagaraVariable Initial(Type, Name);
		Initial.AllocateData();
		return Store.AddParameter(Initial, /*bInitialize=*/true, /*bTriggerRebind=*/true);
	}

	/** "Prop=Text|Prop2=Text" onto a data interface object. */
	static bool ApplyObjectProperties(UObject& Object, const FString& Assignments, FString& OutError)
	{
		TArray<FString> Items;
		Assignments.ParseIntoArray(Items, TEXT("|"));
		for (const FString& Item : Items)
		{
			FString Name;
			FString Text;
			if (!Item.Split(TEXT("="), &Name, &Text))
			{
				OutError = FString::Printf(TEXT("'%s' is not Prop=Value"), *Item);
				return false;
			}
			FProperty* Property = FindFProperty<FProperty>(Object.GetClass(), *Name.TrimStartAndEnd());
			if (!Property)
			{
				OutError = FString::Printf(TEXT("%s has no property %s"), *Object.GetClass()->GetName(), *Name);
				return false;
			}
			Object.Modify();
			if (!Property->ImportText_InContainer(*Text.TrimStartAndEnd(), &Object, &Object, PPF_None))
			{
				OutError = FString::Printf(TEXT("could not read '%s' into %s"), *Text, *Name);
				return false;
			}
			FPropertyChangedEvent Changed(Property);
			Object.PostEditChangeProperty(Changed);
		}
		return true;
	}

	static bool ApplyInput(UNiagaraSystem& System, UNiagaraStackFunctionInput& Input, const FString& Value, FString& OutError)
	{
		const FNiagaraTypeDefinition& Type = Input.GetInputType();
		if (Value.StartsWith(TEXT("=")))
		{
			const FName Parameter(*Value.Mid(1).TrimStartAndEnd());
			if (Parameter.ToString().StartsWith(TEXT("User.")) && !EnsureUserParameter(System, Type, Parameter))
			{
				OutError = FString::Printf(TEXT("could not add %s"), *Parameter.ToString());
				return false;
			}
			Input.SetLinkedParameterValue(FNiagaraVariableBase(Type, Parameter));
			return true;
		}
		if (Value.StartsWith(TEXT("dyn:")))
		{
			UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *Value.Mid(4).TrimStartAndEnd());
			if (!Script)
			{
				OutError = FString::Printf(TEXT("no dynamic input script %s"), *Value.Mid(4));
				return false;
			}
			Input.SetDynamicInput(Script);
			return true;
		}
		if (Value.StartsWith(TEXT("di:")))
		{
			UNiagaraDataInterface* DataInterface = Input.GetDataValueObject();
			if (!DataInterface)
			{
				OutError = TEXT("the input has no data interface");
				return false;
			}
			return ApplyObjectProperties(*DataInterface, Value.Mid(3), OutError);
		}
		const UScriptStruct* Struct = Type.GetScriptStruct();
		if (!Struct)
		{
			OutError = TEXT("the input takes no plain value");
			return false;
		}
		TSharedRef<FStructOnScope> Local = MakeShared<FStructOnScope>(Struct);
		if (!ParseValue(Type, Value, Local->GetStructMemory(), OutError))
		{
			return false;
		}
		Input.SetLocalValue(Local);
		return true;
	}

	static FString DescribeInput(UNiagaraStackFunctionInput& Input, int32 Depth)
	{
		const FString Indent = FString::ChrN(4 + Depth * 2, TEXT(' '));
		const FNiagaraTypeDefinition& Type = Input.GetInputType();
		FString Mode;
		switch (Input.GetValueMode())
		{
		case UNiagaraStackFunctionInput::EValueMode::Local:
			Mode = TEXT("local");
			break;
		case UNiagaraStackFunctionInput::EValueMode::Linked:
			Mode = TEXT("linked ") + Input.GetLinkedParameterValue().GetName().ToString();
			break;
		case UNiagaraStackFunctionInput::EValueMode::Dynamic:
			Mode = TEXT("dynamic ") + GetNameSafe(Input.GetDynamicInputNode() ? Input.GetDynamicInputNode()->FunctionScript : nullptr);
			break;
		case UNiagaraStackFunctionInput::EValueMode::Data:
			Mode = TEXT("data ") + GetNameSafe(Input.GetDataValueObject());
			break;
		default:
			Mode = TEXT("other");
			break;
		}
		FString Text;
		if (TSharedPtr<const FStructOnScope> Local = Input.GetLocalValueStruct())
		{
			if (const UScriptStruct* Struct = Cast<UScriptStruct>(Local->GetStruct()))
			{
				Struct->ExportText(Text, Local->GetStructMemory(), nullptr, nullptr, PPF_None, nullptr);
			}
		}
		FString Line = FString::Printf(TEXT("%s- %s [%s] %s %s\n"), *Indent, *Input.GetDisplayName().ToString(),
			*Type.GetName(), *Mode, *Text);
		if (UNiagaraNodeFunctionCall* Dynamic = Input.GetDynamicInputNode())
		{
			TArray<UNiagaraStackFunctionInput*> Children;
			CollectInputs(Input, Dynamic, Children);
			for (UNiagaraStackFunctionInput* Child : Children)
			{
				Line += DescribeInput(*Child, Depth + 1);
			}
		}
		return Line;
	}

	static bool Fail(const TCHAR* What, const UNiagaraSystem* System, const FString& Detail)
	{
		UE_LOG(LogHawkeye, Error, TEXT("HawkeyeVfxBuilder.%s(%s): %s."), What, *GetNameSafe(System), *Detail);
		return false;
	}
}
#endif

UNiagaraSystem* UHawkeyeVfxBuilder::CreateSystem(const FString& PackageName)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	DropViewModel();
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package || AssetName.IsEmpty())
	{
		Fail(TEXT("CreateSystem"), nullptr, PackageName + TEXT(" is not a package name"));
		return nullptr;
	}
	Package->FullyLoad();
	UNiagaraSystem* System = FindObject<UNiagaraSystem>(Package, *AssetName);
	if (System)
	{
		// Rebuilt in place so every soft reference to it stays good.
		System->Modify();
		TSet<FGuid> Handles;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			Handles.Add(Handle.GetId());
		}
		System->RemoveEmitterHandlesById(Handles);
		System->GetExposedParameters().Empty(true);
	}
	else
	{
		System = NewObject<UNiagaraSystem>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UNiagaraSystemFactoryNew::InitializeSystem(System, /*bCreateDefaultNodes=*/true);
		FAssetRegistryModule::AssetCreated(System);
	}
	Package->MarkPackageDirty();
	return System;
#else
	UE_LOG(LogHawkeye, Error, TEXT("HawkeyeVfxBuilder.CreateSystem(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

bool UHawkeyeVfxBuilder::AddEmitter(UNiagaraSystem* System, const FString& EmitterPath, FName Name)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	if (!System)
	{
		return Fail(TEXT("AddEmitter"), System, TEXT("no system"));
	}
	UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
	if (!Template)
	{
		return Fail(TEXT("AddEmitter"), System, TEXT("no emitter at ") + EmitterPath);
	}
	DropViewModel();
	const FGuid Id = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetId() == Id)
		{
			Handle.SetName(Name, *System);
			System->MarkPackageDirty();
			return true;
		}
	}
	return Fail(TEXT("AddEmitter"), System, TEXT("the copy of ") + EmitterPath + TEXT(" did not appear"));
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::AddUserParameter(UNiagaraSystem* System, FName Name, const FString& Type, const FString& DefaultValue)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	if (!System)
	{
		return Fail(TEXT("AddUserParameter"), System, TEXT("no system"));
	}
	static const TPair<const TCHAR*, FNiagaraTypeDefinition (*)()> Types[] = {
		{ TEXT("float"), [] { return FNiagaraTypeDefinition::GetFloatDef(); } },
		{ TEXT("int"), [] { return FNiagaraTypeDefinition::GetIntDef(); } },
		{ TEXT("bool"), [] { return FNiagaraTypeDefinition::GetBoolDef(); } },
		{ TEXT("vector"), [] { return FNiagaraTypeDefinition::GetVec3Def(); } },
		{ TEXT("position"), [] { return FNiagaraTypeDefinition::GetPositionDef(); } },
		{ TEXT("color"), [] { return FNiagaraTypeDefinition::GetColorDef(); } },
		{ TEXT("vec2"), [] { return FNiagaraTypeDefinition::GetVec2Def(); } },
	};
	FNiagaraTypeDefinition Definition;
	bool bKnown = false;
	for (const auto& Entry : Types)
	{
		if (Type.Equals(Entry.Key, ESearchCase::IgnoreCase))
		{
			Definition = Entry.Value();
			bKnown = true;
		}
	}
	if (!bKnown)
	{
		return Fail(TEXT("AddUserParameter"), System, TEXT("unknown type ") + Type);
	}
	const FName FullName(*(TEXT("User.") + Name.ToString()));
	FNiagaraVariable Variable(Definition, FullName);
	Variable.AllocateData();
	FString Error;
	if (!DefaultValue.IsEmpty() && !ParseValue(Definition, DefaultValue, Variable.GetData(), Error))
	{
		return Fail(TEXT("AddUserParameter"), System, Error);
	}
	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	if (Store.IndexOf(Variable) == INDEX_NONE)
	{
		Store.AddParameter(Variable, /*bInitialize=*/true, /*bTriggerRebind=*/true);
	}
	Store.SetParameterData(Variable.GetData(), Variable, /*bAdd=*/true);
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::AddModule(UNiagaraSystem* System, FName Emitter, const FString& Stage, const FString& ScriptPath)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter) : nullptr;
	FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
	if (!Data)
	{
		return Fail(TEXT("AddModule"), System, TEXT("no emitter ") + Emitter.ToString());
	}
	ENiagaraScriptUsage Usage;
	if (!ParseStage(Stage, Usage))
	{
		return Fail(TEXT("AddModule"), System, TEXT("unknown stage ") + Stage);
	}
	UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath);
	const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
	UNiagaraNodeOutput* Output = (Source && Source->NodeGraph) ? Source->NodeGraph->FindEquivalentOutputNode(Usage) : nullptr;
	if (!Script || !Output)
	{
		return Fail(TEXT("AddModule"), System, FString::Printf(TEXT("no module %s or no %s stage"), *ScriptPath, *Stage));
	}
	DropViewModel();
	const UNiagaraNodeFunctionCall* Added = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output);
	System->MarkPackageDirty();
	return Added != nullptr || Fail(TEXT("AddModule"), System, TEXT("could not add ") + ScriptPath);
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::SetModuleEnabled(UNiagaraSystem* System, FName Emitter, const FString& Module, bool bEnabled)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	UNiagaraStackModuleItem* Item = System ? FindModule(*System, Emitter, Module) : nullptr;
	if (!Item)
	{
		return Fail(TEXT("SetModuleEnabled"), System, FString::Printf(TEXT("no module '%s' on %s"), *Module, *Emitter.ToString()));
	}
	Item->SetEnabled(bEnabled);
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::SetInput(UNiagaraSystem* System, FName Emitter, const FString& Module, const FString& Input,
	const FString& Value)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	UNiagaraStackModuleItem* Item = System ? FindModule(*System, Emitter, Module) : nullptr;
	if (!Item)
	{
		return Fail(TEXT("SetInput"), System, FString::Printf(TEXT("no module '%s' on %s"), *Module, *Emitter.ToString()));
	}
	UNiagaraStackFunctionInput* Found = FindInput(*Item, Input);
	if (!Found)
	{
		return Fail(TEXT("SetInput"), System, FString::Printf(TEXT("no input '%s' on %s/%s"), *Input, *Emitter.ToString(), *Module));
	}
	FString Error;
	if (!ApplyInput(*System, *Found, Value, Error))
	{
		return Fail(TEXT("SetInput"), System, FString::Printf(TEXT("%s/%s/%s = %s: %s"), *Emitter.ToString(), *Module, *Input, *Value, *Error));
	}
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::SetEmitterProperty(UNiagaraSystem* System, FName Emitter, const FString& Property, const FString& Value)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter) : nullptr;
	const FVersionedNiagaraEmitter Versioned = Handle ? Handle->GetInstance() : FVersionedNiagaraEmitter();
	FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
	if (!Data || !Versioned.Emitter)
	{
		return Fail(TEXT("SetEmitterProperty"), System, TEXT("no emitter ") + Emitter.ToString());
	}
	FProperty* Found = FindFProperty<FProperty>(FVersionedNiagaraEmitterData::StaticStruct(), *Property);
	if (!Found)
	{
		return Fail(TEXT("SetEmitterProperty"), System, TEXT("no emitter property ") + Property);
	}
	Versioned.Emitter->Modify();
	if (!Found->ImportText_InContainer(*Value, Data, Versioned.Emitter, PPF_None))
	{
		return Fail(TEXT("SetEmitterProperty"), System, FString::Printf(TEXT("could not read '%s' into %s"), *Value, *Property));
	}
	FPropertyChangedEvent Changed(Found);
	Versioned.Emitter->PostEditChangeVersionedProperty(Changed, Versioned.Version);
	DropViewModel();
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::SetRendererProperty(UNiagaraSystem* System, FName Emitter, int32 RendererIndex,
	const FString& Property, const FString& Value)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter) : nullptr;
	FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
	if (!Data || !Data->GetRenderers().IsValidIndex(RendererIndex) || !Data->GetRenderers()[RendererIndex])
	{
		return Fail(TEXT("SetRendererProperty"), System, FString::Printf(TEXT("no renderer %d on %s"), RendererIndex, *Emitter.ToString()));
	}
	FString Error;
	if (!ApplyObjectProperties(*Data->GetRenderers()[RendererIndex], Property + TEXT("=") + Value, Error))
	{
		return Fail(TEXT("SetRendererProperty"), System, Error);
	}
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UHawkeyeVfxBuilder::SetSystemProperty(UNiagaraSystem* System, const FString& Property, const FString& Value)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	if (!System)
	{
		return Fail(TEXT("SetSystemProperty"), System, TEXT("no system"));
	}
	FString Error;
	if (!ApplyObjectProperties(*System, Property + TEXT("=") + Value, Error))
	{
		return Fail(TEXT("SetSystemProperty"), System, Error);
	}
	DropViewModel();
	return true;
#else
	return false;
#endif
}

int32 UHawkeyeVfxBuilder::AddRenderer(UNiagaraSystem* System, FName Emitter, const FString& RendererClassPath)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	UClass* RendererClass = LoadClass<UNiagaraRendererProperties>(nullptr, *RendererClassPath);
	FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter) : nullptr;
	const FVersionedNiagaraEmitter Versioned = Handle ? Handle->GetInstance() : FVersionedNiagaraEmitter();
	FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
	if (!Data || !Versioned.Emitter || !RendererClass || !RendererClass->IsChildOf(UNiagaraRendererProperties::StaticClass()))
	{
		Fail(TEXT("AddRenderer"), System, TEXT("no emitter ") + Emitter.ToString() + TEXT(" or not a renderer class"));
		return -1;
	}
	DropViewModel();
	UNiagaraRendererProperties* Renderer = NewObject<UNiagaraRendererProperties>(Versioned.Emitter, RendererClass,
		NAME_None, RF_Transactional);
	Versioned.Emitter->AddRenderer(Renderer, Versioned.Version);
	System->MarkPackageDirty();
	return Data->GetRenderers().Num() - 1;
#else
	return -1;
#endif
}

bool UHawkeyeVfxBuilder::FinishSystem(UNiagaraSystem* System)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	if (!System)
	{
		return Fail(TEXT("FinishSystem"), System, TEXT("no system"));
	}
	DropViewModel();
	System->RequestCompile(/*bForce=*/true);
	System->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false, /*bShowProgress=*/false);

	bool bClean = true;
	auto Check = [&bClean, System](const UNiagaraScript* Script, const FString& Owner)
	{
		if (Script && Script->GetLastCompileStatus() == ENiagaraScriptCompileStatus::NCS_Error)
		{
			bClean = false;
			UE_LOG(LogHawkeye, Error, TEXT("HawkeyeVfxBuilder.FinishSystem(%s): %s %s failed: %s"), *GetNameSafe(System),
				*Owner, *Script->GetName(), *Script->GetVMExecutableData().ErrorMsg);
		}
	};
	Check(System->GetSystemSpawnScript(), TEXT("system"));
	Check(System->GetSystemUpdateScript(), TEXT("system"));
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
		{
			TArray<UNiagaraScript*> Scripts;
			Data->GetScripts(Scripts, /*bCompilableOnly=*/true, /*bEnabledOnly=*/true);
			for (const UNiagaraScript* Script : Scripts)
			{
				Check(Script, Handle.GetName().ToString());
			}
		}
	}
	System->MarkPackageDirty();
	return bClean;
#else
	return false;
#endif
}

FString UHawkeyeVfxBuilder::DescribeSystem(UNiagaraSystem* System)
{
#if WITH_EDITOR
	using namespace HawkeyeVfxBuild;
	if (!System)
	{
		return TEXT("(no system)");
	}
	FString Text = FString::Printf(TEXT("%s: %d emitter(s)\n"), *System->GetPathName(), System->GetEmitterHandles().Num());
	TArray<FNiagaraVariable> UserParameters;
	System->GetExposedParameters().GetParameters(UserParameters);
	for (const FNiagaraVariable& Parameter : UserParameters)
	{
		Text += FString::Printf(TEXT("  user %s [%s]\n"), *Parameter.GetName().ToString(), *Parameter.GetType().GetName());
	}
	const TArray<FName> Names = GetEmitterNames(System);
	for (const FName& Name : Names)
	{
		const FNiagaraEmitterHandle* Handle = FindHandle(*System, Name);
		const FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
		Text += FString::Printf(TEXT("  emitter %s: %s, %s space\n"), *Name.ToString(),
			Data && Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"),
			Data && Data->bLocalSpace ? TEXT("local") : TEXT("world"));
		for (UNiagaraStackModuleItem* Module : GetModules(*System, Name))
		{
			Text += FString::Printf(TEXT("   %s: %s%s\n"), *ModuleStage(*Module), *Module->GetDisplayName().ToString(),
				Module->GetIsEnabled() ? TEXT("") : TEXT(" (disabled)"));
			TArray<UNiagaraStackFunctionInput*> Inputs;
			CollectInputs(*Module, &Module->GetModuleNode(), Inputs);
			for (UNiagaraStackFunctionInput* Input : Inputs)
			{
				Text += DescribeInput(*Input, 0);
			}
		}
		if (Data)
		{
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				Text += FString::Printf(TEXT("   renderer %s\n"), *GetNameSafe(Renderer ? Renderer->GetClass() : nullptr));
			}
		}
	}
	return Text;
#else
	return FString();
#endif
}

TArray<FName> UHawkeyeVfxBuilder::GetEmitterNames(UNiagaraSystem* System)
{
	TArray<FName> Names;
	if (System)
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			Names.Add(Handle.GetName());
		}
	}
	return Names;
}

int32 UHawkeyeVfxBuilder::GetRendererCount(UNiagaraSystem* System, FName Emitter)
{
	if (System)
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (Handle.GetName() == Emitter && Data)
			{
				return Data->GetRenderers().Num();
			}
		}
	}
	return -1;
}

void UHawkeyeVfxBuilder::EndEditing()
{
#if WITH_EDITOR
	HawkeyeVfxBuild::DropViewModel();
#endif
}
