#include "EnginePCH.h"
#include "UObject/Property.h"

#include "UObject/Class.h"
#include "Asset/AssetManager.h"

// ---- FColorProperty ----

void FColorProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    Slot << GetPropertyValue(Value);
}

// ---- FLinearColorProperty ----

FVector4 FLinearColorProperty::GetChannels(const void* Value) const
{
    static_assert(sizeof(FVector4) == sizeof(FLinearColor));
    FVector4 Channels;
    memcpy(&Channels, Value, sizeof(Channels));
    return Channels;
}

void FLinearColorProperty::SetChannels(void* Value, const FVector4& Channels) const
{
    memcpy(Value, &Channels, sizeof(Channels));
}

void FLinearColorProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    // Week12 scene files store these channels as X/Y/Z/W.
    FVector4 Channels = GetChannels(Value);
    Slot << Channels;
    if (Slot.GetUnderlyingArchive().IsLoading()) SetChannels(Value, Channels);
}

// ---- FObjectProperty ----

void FObjectProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    UObject*& Ref = GetPropertyValue(Value);
    UObject* Previous = Ref;
    Slot << Ref;

    // 불러온 객체가 프로퍼티가 기대하는 클래스가 아니면 되돌린다
    if (Slot.GetUnderlyingArchive().IsLoading() && Ref && PropertyClass && !Ref->IsA(PropertyClass))
    {
        HTR_LOG(Warning, "Load: '{}' is not a {}, keeping default", Name, PropertyClass->Name);
        Ref = Previous;
    }
}

// ---- FEnumProperty ----

void FEnumProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    // UENUM은 이름으로 저장한다. enum 항목 순서를 바꿔도 파일이 깨지지 않는다.
    FString EntryName = Enum ? Enum->ToName(GetValue(Value)) : FString();
    Slot << EntryName;

    int64 Loaded = 0;
    if (Slot.GetUnderlyingArchive().IsLoading() && Enum)
    {
        if (Enum->FromName(EntryName, Loaded))
            SetValue(Value, Loaded);
        else
            HTR_LOG(Warning, "Load: '{}' has no enum entry '{}', keeping default", Name, EntryName);
    }
}

// ---- FStructProperty ----

void FStructProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    Struct->SerializeTaggedProperties(Slot, Value);
}

void FColorProperty::SerializeJsonItem(json& Field, void* Value, bool bIsLoading) const
{
    uint32& Packed = GetPropertyValue(Value).DWColor();
    if (bIsLoading) Field.get_to(Packed);
    else Field = Packed;
}

void FLinearColorProperty::SerializeJsonItem(json& Field, void* Value, bool bIsLoading) const
{
    if (bIsLoading) SetChannels(Value, Field.get<FVector4>());
    else Field = GetChannels(Value);
}

void FObjectProperty::SerializeJsonItem(json& Field, void* Value, bool bIsLoading) const
{
    UObject*& Ref = GetPropertyValue(Value);
    if (bIsLoading)
    {
        if (!Field.is_string()) return;
        const FString AssetPath = Field.get<FString>();
        URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(AssetPath);
        if (Asset && (!PropertyClass || Asset->IsA(PropertyClass))) Ref = Asset;
        else HTR_LOG(Warning, "Load: asset '{}' not found or incompatible for {}, keeping default", AssetPath, Name);
    }
    else
    {
        URenderAsset* Asset = Cast<URenderAsset>(Ref);
        if (Asset && !Asset->GetPath().empty()) Field = Asset->GetPath();
        else Field = nullptr;
    }
}

void FEnumProperty::SerializeJsonItem(json& Field, void* Value, bool bIsLoading) const
{
    if (bIsLoading)
    {
        int64 Loaded = 0;
        if (Enum && Field.is_string() && Enum->FromName(Field.get<FString>(), Loaded)) SetValue(Value, Loaded);
    }
    else Field = Enum ? Enum->ToName(GetValue(Value)) : FString();
}

void FStructProperty::SerializeJsonItem(json& Field, void* Value, bool bIsLoading) const
{
    if (!bIsLoading) Field = json::object();
    Struct->SerializeTaggedProperties(Field, Value, bIsLoading);
}
