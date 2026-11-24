#include "EnginePCH.h"
#include "Level.h"
#include "World.h"

// delete Actor가 소멸자를 호출하려면 AActor의 완전한 정의가 필요하다.
// 전방 선언만으로는 C4150(소멸자 미호출)이 되어 액터가 GUObjectArray에 남는다.
#include "GameFramework/Actor.h"
#include "Components/ExponentialHeightFogComponent.h"

void ULevel::AddActor(AActor* Actor)
{
	if (!Actor)
		return;

	Actor->World = OwningWorld;
	Actor->Level = this;
	Actors.Add(Actor);
}

void ULevel::AdoptLoadedActors(ULevel& StagedLevel)
{
	for (AActor* Actor : StagedLevel.Actors)
	{
		Actor->SetOuter(this);
		AddActor(Actor);
	}
	StagedLevel.Actors.Reset();
}

void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (!Actor)
			continue;

		delete Actor;
	}
	Actors.Reset();
}

void ULevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Actors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		if (Ar.IsLoading())
			Actors.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Actors[i];
			ActorArray.EnterElement() << Obj;
			Actors[i] = static_cast<AActor*>(Obj);
		}

		UObject* WorldObj = OwningWorld;
		Record << SA_VALUE("OwningWorld", WorldObj);
		OwningWorld = static_cast<UWorld*>(WorldObj);
		return;
	}

	if (Ar.IsSaving())
	{
		// 개수를 먼저 알아야 EnterArray를 할 수 있으므로 저장할 액터부터 고른다
		TArray<AActor*> SavedActors;
		for (AActor* Actor : Actors)
		{
			if (Actor && !Actor->HasAnyFlags(EObjectFlags::RF_Transient))
				SavedActors.Add(Actor);
		}

		int32 Num = SavedActors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		for (AActor* Actor : SavedActors)
			SaveActorRecord(ActorArray.EnterElement().EnterRecord(), Actor);
		return;
	}

}

void ULevel::SaveActorRecord(FStructuredArchive::FRecord ActorRecord, AActor* Actor, bool bForClipboard)
{
	FString ClassName = Actor->GetClass()->Name;
	FString ActorName = Actor->GetName();
	ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);
	Actor->Serialize(ActorRecord);
	if (!bForClipboard) return;

	// 클립보드에 포함되지 않은 부모 컴포넌트는 현재 레벨에서 Outer 경로로 찾는다.
	USceneComponent* Root = Actor->GetRootComponent();
	USceneComponent* AttachParent = Root ? Root->GetAttachParent() : nullptr;
	AActor* ParentActor = Actor->GetAttachParentActor();
	if (TOptional<FStructuredArchive::FSlot> AttachSlot = ActorRecord.TryEnterField("AttachParent", ParentActor != nullptr))
	{
		FString ParentActorName = ParentActor->GetName();
		FStructuredArchive::FRecord AttachRecord = AttachSlot.GetValue().EnterRecord();
		AttachRecord << SA_VALUE("Actor", ParentActorName);
		TArray<FString> ComponentPath;
		for (const UObject* Object = AttachParent; Object && Object != ParentActor; Object = Object->GetOuter())
			ComponentPath.Add(Object->GetName());
		int32 Count = ComponentPath.Num();
		auto PathArray = AttachRecord.EnterArray("ComponentPath", Count);
		for (int32 Index = Count - 1; Index >= 0; --Index)
			PathArray.EnterElement() << ComponentPath[Index];
	}
}
