#include "EnginePCH.h"
#include "MovieSceneTracks/MovieScenePropertyBinding.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UObject/Class.h"

namespace
{
	const FFloatProperty* FindFloatProperty(const UObject* Object, const FString& Name)
	{
		const UClass* Class = Object ? Object->GetClass() : nullptr;
		return Class ? CastField<FFloatProperty>(Class->FindPropertyByName(Name)) : nullptr;
	}

	// 리플렉션에 없는 프로퍼티의 Get/Set 세터 (UE는 "Set" + 프로퍼티 이름 함수를 찾아 부른다)
	bool GetKnown(UObject* Object, const FString& Name, float& OutValue)
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Object))
		{
			if (Name == "FieldOfView") { OutValue = Camera->GetFieldOfView(); return true; }
			if (Name == "OrthoWidth")  { OutValue = Camera->GetOrthoWidth(); return true; }
		}
		return false;
	}

	bool SetKnown(UObject* Object, const FString& Name, float Value)
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Object))
		{
			if (Name == "FieldOfView") { Camera->SetFieldOfView(Value); return true; }
			if (Name == "OrthoWidth")  { Camera->SetOrthoWidth(Value); return true; }
		}
		return false;
	}

	// 액터 자신 → 컴포넌트 순서로 찾는다
	template <typename FunctionType>
	bool ForEachCandidate(UObject* BoundObject, FunctionType&& Function)
	{
		if (!BoundObject) return false;
		if (Function(BoundObject)) return true;
		if (AActor* Actor = Cast<AActor>(BoundObject))
			for (UActorComponent* Component : Actor->GetComponents())
				if (Component && Function(Component)) return true;
		return false;
	}
}

bool MovieScenePropertyBindings::GetFloat(UObject* BoundObject, const FString& PropertyName, float& OutValue, UObject** OutOwner)
{
	return ForEachCandidate(BoundObject, [&](UObject* Object)
	{
		bool bFound = GetKnown(Object, PropertyName, OutValue);
		if (!bFound)
		{
			if (const FFloatProperty* Property = FindFloatProperty(Object, PropertyName))
			{
				OutValue = *Property->ContainerPtrToValuePtr<float>(Object);
				bFound = true;
			}
		}
		if (bFound && OutOwner) *OutOwner = Object;
		return bFound;
	});
}

bool MovieScenePropertyBindings::SetFloat(UObject* BoundObject, const FString& PropertyName, float Value)
{
	return ForEachCandidate(BoundObject, [&](UObject* Object)
	{
		if (SetKnown(Object, PropertyName, Value)) return true;
		const FFloatProperty* Property = FindFloatProperty(Object, PropertyName);
		if (!Property) return false;
		*Property->ContainerPtrToValuePtr<float>(Object) = Value;
		return true;
	});
}

AActor* MovieSceneHelpers::ActorFromBoundObject(UObject* BoundObject)
{
	if (AActor* Actor = Cast<AActor>(BoundObject)) return Actor;
	if (UActorComponent* Component = Cast<UActorComponent>(BoundObject)) return Component->GetOwner();
	return nullptr;
}

USceneComponent* MovieSceneHelpers::SceneComponentFromBoundObject(UObject* BoundObject)
{
	if (USceneComponent* Component = Cast<USceneComponent>(BoundObject)) return Component;
	if (AActor* Actor = Cast<AActor>(BoundObject)) return Actor->GetRootComponent();
	return nullptr;
}

UCameraComponent* MovieSceneHelpers::CameraComponentFromRuntimeObject(UObject* RuntimeObject)
{
	if (UCameraComponent* Camera = Cast<UCameraComponent>(RuntimeObject)) return Camera;
	if (ACameraActor* CameraActor = Cast<ACameraActor>(RuntimeObject)) return CameraActor->GetCameraComponent();
	if (AActor* Actor = Cast<AActor>(RuntimeObject))
		for (UActorComponent* Component : Actor->GetComponents())
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component)) return Camera;
	return nullptr;
}
