#include "EnginePCH.h"
#include "Class.h"

#include "Object.h"

// ---- UStruct ----

UStruct::UStruct()
	:UObject(false)
{
	//HTR_LOG(Info, "Create RTTI UClass");
}

UStruct::~UStruct()
{
	for (FProperty* Property : Properties)
	{
		delete Property;
	}
}

FProperty* UStruct::FindPropertyByName(const FString& InName) const
{
	for (const UStruct* s = this; s; s = s->GetSuperStruct())
	{
		for (FProperty* Property : s->Properties)
		{
			if (Property->Name == InName)
			{
				return Property;
			}
		}
	}
	return nullptr;
}

// 타입별 처리는 각 FProperty 서브클래스가 한다 (Property.h)
void UStruct::SerializeTaggedProperties(FStructuredArchive::FSlot Slot, void* Data) const
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	for (const UStruct* s = this; s; s = s->GetSuperStruct())
	{
		for (const FProperty* Property : s->Properties)
		{
			if (!Property->ShouldSerializeValue(Ar))
				continue;

			TOptional<FStructuredArchive::FSlot> Field = Record.TryEnterField(Property->Name.c_str(), true);
			if (!Field.IsSet()) continue;

			Property->SerializeItem(Field.GetValue(), Property->ContainerPtrToValuePtr(Data));
		}
	}
}

void UStruct::SerializeTaggedProperties(json& Record, void* Data, bool bIsLoading) const
{
	for (const UStruct* Struct = this; Struct; Struct = Struct->GetSuperStruct())
	{
		for (const FProperty* Property : Struct->GetProperties())
		{
			if (Property->HasAnyFlags(CPF_Transient) || (bIsLoading && !Record.contains(Property->Name)))
				continue;

			Property->SerializeJsonItem(Record[Property->Name], Property->ContainerPtrToValuePtr(Data), bIsLoading);
		}
	}
}

// ---- UScriptStruct ----

bool UScriptStruct::IsChildOf(const UScriptStruct* BaseStruct) const
{
	if (!BaseStruct) return false;
	for (const UScriptStruct* s = this; s; s = s->SuperStruct)
	{
		if (s == BaseStruct) return true;
	}
	return false;
}

// ---- UClass ----

const FFunction* UClass::FindFunction(const FString& InName) const
{
	for (const UClass* c = this; c; c = c->Super)
	{
		for (const FFunction& Function : c->Functions)
		{
			if (Function.Name == InName)
			{
				return &Function;
			}
		}
	}
	return nullptr;
}

bool UClass::IsChildOf(const UClass* BaseClass) const
{
	if (!BaseClass) return false;
	for (const UClass* c = this; c; c = c->Super)
	{
		if (c == BaseClass) return true;
	}
	return false;
}

UObject* UClass::GetDefaultObject() 
{
	if (!DefaultObject && Constructor)
	{
		FStaticConstructObjectParameters Params(this);
		Params.Name = FName(FString("Default__") + Name);
		Params.SetFlags = EObjectFlags::RF_ClassDefaultObject;
		DefaultObject = StaticConstructObject_Internal(Params);
	}
	return DefaultObject;
}


