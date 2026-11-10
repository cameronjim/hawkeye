#include "Player/GaspTraversal.h"

#include "Hawkeye.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace HawkeyeGaspTraversal
{
	static const FName TryTraversalName(TEXT("TryTraversalAction"));
	static const FName CheckInputsName(TEXT("GetTraversalCheckInputs"));
	static const FName DoingTraversalName(TEXT("DoingTraversalAction"));
	static const TCHAR* InputsStructName = TEXT("S_TraversalCheckInputs");
	static const TCHAR* CheckFailedName = TEXT("TraversalCheckFailed");
	static const TCHAR* MontageFailedName = TEXT("MontageSelectionFailed");

	/** A UFunction's parameter block, initialised and destroyed the way ProcessEvent expects. */
	class FParams
	{
	public:
		explicit FParams(UFunction* InFunction)
			: Function(InFunction)
		{
			Memory = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(1, Function->ParmsSize),
				Function->GetMinAlignment()));
			FMemory::Memzero(Memory, FMath::Max<int32>(1, Function->ParmsSize));
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Memory);
			}
		}

		~FParams()
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Memory);
			}
			FMemory::Free(Memory);
		}

		FParams(const FParams&) = delete;
		FParams& operator=(const FParams&) = delete;

		uint8* Get() const { return Memory; }

	private:
		UFunction* Function;
		uint8* Memory = nullptr;
	};

	static bool IsInputsStruct(const FProperty* Property)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return Struct && Struct->Struct && Struct->Struct->GetName().Contains(InputsStructName);
	}

	/** The S_TraversalCheckInputs parameter: the output of GetTraversalCheckInputs, the input of TryTraversalAction. */
	static FStructProperty* FindInputsParam(UFunction* Function, bool bOutput)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const bool bIsOutput = It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)
				&& !It->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (IsInputsStruct(*It) && bIsOutput == bOutput)
			{
				return CastField<FStructProperty>(*It);
			}
		}
		return nullptr;
	}

	static FBoolProperty* FindBoolParam(UFunction* Function, const TCHAR* AuthoredName)
	{
		for (TFieldIterator<FBoolProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->GetAuthoredName().Equals(AuthoredName))
			{
				return *It;
			}
		}
		return nullptr;
	}

	UActorComponent* FindTraversalLogic(const AActor* Owner)
	{
		if (!IsValid(Owner))
		{
			return nullptr;
		}
		TInlineComponentArray<UActorComponent*> Components(Owner);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->FindFunction(TryTraversalName))
			{
				return Component;
			}
		}
		return nullptr;
	}

	bool IsDoingTraversal(const UActorComponent* TraversalLogic)
	{
		if (!IsValid(TraversalLogic))
		{
			return false;
		}
		const FBoolProperty* Flag = FindFProperty<FBoolProperty>(TraversalLogic->GetClass(), DoingTraversalName);
		return Flag && Flag->GetPropertyValue_InContainer(TraversalLogic);
	}

	FResult TryTraversal(ACharacter* Character, UActorComponent* TraversalLogic)
	{
		FResult Result;
		UFunction* GetInputs = IsValid(Character) ? Character->FindFunction(CheckInputsName) : nullptr;
		UFunction* Try = IsValid(TraversalLogic) ? TraversalLogic->FindFunction(TryTraversalName) : nullptr;
		FStructProperty* InputsOut = GetInputs ? FindInputsParam(GetInputs, true) : nullptr;
		FStructProperty* InputsIn = Try ? FindInputsParam(Try, false) : nullptr;
		FBoolProperty* CheckFailed = Try ? FindBoolParam(Try, CheckFailedName) : nullptr;
		FBoolProperty* MontageFailed = Try ? FindBoolParam(Try, MontageFailedName) : nullptr;
		if (!InputsOut || !InputsIn || !CheckFailed || !MontageFailed || InputsOut->Struct != InputsIn->Struct)
		{
			return Result;
		}

		FParams InputsParams(GetInputs);
		Character->ProcessEvent(GetInputs, InputsParams.Get());

		FParams TryParams(Try);
		InputsIn->CopyCompleteValue(InputsIn->ContainerPtrToValuePtr<void>(TryParams.Get()),
			InputsOut->ContainerPtrToValuePtr<void>(InputsParams.Get()));
		TraversalLogic->ProcessEvent(Try, TryParams.Get());

		Result.bCalled = true;
		Result.bCheckFailed = CheckFailed->GetPropertyValue_InContainer(TryParams.Get());
		Result.bMontageFailed = MontageFailed->GetPropertyValue_InContainer(TryParams.Get());
		return Result;
	}

	bool HasTraversalApi(const UClass* CharacterClass, const UClass* TraversalLogicClass, FString& OutReport)
	{
		UFunction* GetInputs = CharacterClass ? CharacterClass->FindFunctionByName(CheckInputsName) : nullptr;
		UFunction* Try = TraversalLogicClass ? TraversalLogicClass->FindFunctionByName(TryTraversalName) : nullptr;
		const bool bInputsOut = GetInputs && FindInputsParam(GetInputs, true);
		const bool bInputsIn = Try && FindInputsParam(Try, false);
		const bool bCheck = Try && FindBoolParam(Try, CheckFailedName);
		const bool bMontage = Try && FindBoolParam(Try, MontageFailedName);
		const bool bFlag = TraversalLogicClass && FindFProperty<FBoolProperty>(TraversalLogicClass, DoingTraversalName);
		OutReport = FString::Printf(
			TEXT("%s.GetTraversalCheckInputs=%d (returns S_TraversalCheckInputs=%d); %s.TryTraversalAction=%d "
				 "(inputs=%d, TraversalCheckFailed=%d, MontageSelectionFailed=%d); DoingTraversalAction=%d"),
			*GetNameSafe(CharacterClass), GetInputs ? 1 : 0, bInputsOut ? 1 : 0, *GetNameSafe(TraversalLogicClass),
			Try ? 1 : 0, bInputsIn ? 1 : 0, bCheck ? 1 : 0, bMontage ? 1 : 0, bFlag ? 1 : 0);
		return bInputsOut && bInputsIn && bCheck && bMontage && bFlag;
	}
}
