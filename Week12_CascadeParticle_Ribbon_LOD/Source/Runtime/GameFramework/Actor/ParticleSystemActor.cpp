#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "GameFramework/Actor/ParticleSystemActor.h"

#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"

AParticleSystemActor::AParticleSystemActor()
{
	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("ParticleSystemComponent");
	SetRootComponent(ParticleSystemComponent);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("ParticleBillboardComponent");
	BillboardComponent->SetupAttachment(ParticleSystemComponent);
	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(2.0f);
	// 게임에서는 보이지 않고 Details에서는 실제 파티클 컴포넌트를 편집한다
	BillboardComponent->SetHiddenInGame(true);
	BillboardComponent->SetHiddenInDetails(true);
	BillboardComponent->bCastShadow = false;
	BillboardComponent->bReceiveShadow = false;
	BillboardComponent->bGenerateOverlapEvents = false;
	// 파티클 충돌이 이 아이콘에 맞지 않게 한다
	BillboardComponent->bBlockComponent = false;

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture(ResourcePaths::ParticleIcon);
	if (!Base || !Icon)
	{
		HTR_LOG(Warning, "Particle billboard material or icon could not be loaded.");
		return;
	}
	// 공유 BillboardMaterial을 직접 수정하지 않는다
	UMaterial* IconMaterial = UMaterial::CreateInstance(Base);
	if (!IconMaterial) return;
	if (IconMaterial->Textures.IsEmpty()) IconMaterial->Textures.Add(Icon);
	else IconMaterial->Textures[0] = Icon;
	BillboardComponent->SetMaterial(0, IconMaterial);
}
