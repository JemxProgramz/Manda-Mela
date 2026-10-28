// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"

// ===========================================================================
//  AMandaMelaPickup
// ===========================================================================

AMandaMelaPickup::AMandaMelaPickup()
{
	bReplicates = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->SetSphereRadius(40.0f);
	Collision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TacticMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TacticMesh"));
	TacticMesh->SetupAttachment(RootComponent);
	TacticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TacticMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BinocularsMeshAsset(TEXT("/Game/Tactics/binocular1.binocular1"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FlashbangMeshAsset(TEXT("/Game/Tactics/flashbang.flashbang"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SmokeBombMeshAsset(TEXT("/Game/Tactics/smoke.smoke"));
	if (BinocularsMeshAsset.Succeeded())
	{
		BinocularsModel = BinocularsMeshAsset.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could not load tactical pickup mesh: /Game/Tactics/binocular1.binocular1"));
	}
	if (FlashbangMeshAsset.Succeeded())
	{
		FlashbangModel = FlashbangMeshAsset.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could not load tactical pickup mesh: /Game/Tactics/flashbang.flashbang"));
	}
	if (SmokeBombMeshAsset.Succeeded())
	{
		SmokeBombModel = SmokeBombMeshAsset.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could not load tactical pickup mesh: /Game/Tactics/smoke.smoke"));
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->InitialSpeed = 0.0f;
	ProjectileMovement->MaxSpeed = 1000.0f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.3f;
}

void AMandaMelaPickup::BeginPlay()
{
	Super::BeginPlay();
	UpdateItemPresentation();
}

void AMandaMelaPickup::SetItemType(EMandaMelaItemType NewItemType)
{
	ItemType = NewItemType;
	UpdateItemPresentation();
}

void AMandaMelaPickup::LaunchThrownItem(const FVector& LaunchVelocity)
{
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = LaunchVelocity;
		ProjectileMovement->Activate();
	}
}

bool AMandaMelaPickup::TryClaim()
{
	if (bClaimed) return false;
	bClaimed = true;
	return true;
}

void AMandaMelaPickup::ReleaseClaim()
{
	bClaimed = false;
}

void AMandaMelaPickup::OnRep_ItemType()
{
	UpdateItemPresentation();
}

void AMandaMelaPickup::UpdateItemPresentation()
{
	// Hide the static mesh and show/swap the skeletal mesh based on the item type.
	bool bShowTactic = false;
	USkeletalMesh* DesiredMesh = nullptr;

	switch (ItemType)
	{
	case EMandaMelaItemType::Binoculars:
		DesiredMesh = BinocularsModel;
		bShowTactic = true;
		break;
	case EMandaMelaItemType::Flashbang:
		DesiredMesh = FlashbangModel;
		bShowTactic = true;
		break;
	case EMandaMelaItemType::SmokeBomb:
		DesiredMesh = SmokeBombModel;
		bShowTactic = true;
		break;
	default:
		break;
	}

	if (TacticMesh)
	{
		TacticMesh->SetVisibility(bShowTactic);
		if (bShowTactic && DesiredMesh)
		{
			TacticMesh->SetSkeletalMesh(DesiredMesh);
		}
	}

	if (Mesh)
	{
		Mesh->SetVisibility(!bShowTactic);
	}
}

void AMandaMelaPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaPickup, ItemType);
}



// ===========================================================================
//  AMandaMelaSmokeCloud
// ===========================================================================

AMandaMelaSmokeCloud::AMandaMelaSmokeCloud()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	VisibilityBlocker = CreateDefaultSubobject<USphereComponent>(TEXT("VisibilityBlocker"));
	VisibilityBlocker->SetSphereRadius(300.0f);
	VisibilityBlocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisibilityBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisibilityBlocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	VisibilityBlocker->SetGenerateOverlapEvents(false);
	RootComponent = VisibilityBlocker;

	ParticleEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ParticleEffect"));
	ParticleEffect->SetupAttachment(RootComponent);
	ParticleEffect->SetAutoActivate(false);
}

void AMandaMelaSmokeCloud::Initialize(UNiagaraSystem* Effect, float LifetimeSeconds)
{
	if (!Effect) return;

	EffectAsset = Effect;
	Lifetime    = LifetimeSeconds;

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			LifetimeTimer, [this]() { Destroy(); },
			Lifetime, false);
		ActivateEffect();
	}
}

void AMandaMelaSmokeCloud::BeginPlay()
{
	Super::BeginPlay();

	if (EffectAsset && !ParticleEffect->IsActive())
	{
		ActivateEffect();
	}
}

void AMandaMelaSmokeCloud::ActivateEffect()
{
	if (!EffectAsset) return;

	ParticleEffect->SetAsset(EffectAsset);
	ParticleEffect->Activate(true);
}

void AMandaMelaSmokeCloud::OnRep_Effect()
{
	ActivateEffect();

	if (!HasAuthority())
	{
		const float Remaining = Lifetime - (GetWorld()->GetTimeSeconds() - CreationTime);
		if (Remaining > 0.f)
		{
			GetWorldTimerManager().SetTimer(
				LifetimeTimer, [this]() { Destroy(); },
				Remaining, false);
		}
	}
}

void AMandaMelaSmokeCloud::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaSmokeCloud, EffectAsset);
	DOREPLIFETIME(AMandaMelaSmokeCloud, Lifetime);
}

// ===========================================================================
//  AMandaMelaSoundDecoy
// ===========================================================================

AMandaMelaSoundDecoy::AMandaMelaSoundDecoy()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void AMandaMelaSoundDecoy::Initialize(USoundBase* Sound, float LifetimeSeconds)
{
	if (!Sound) return;

	DecoySound = Sound;
	Lifetime   = LifetimeSeconds;

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			LifetimeTimer, [this]() { Destroy(); },
			Lifetime, false);
		StartSoundPlayback();
	}
}

void AMandaMelaSoundDecoy::BeginPlay()
{
	Super::BeginPlay();

	if (DecoySound && !bSoundPlaybackStarted)
	{
		StartSoundPlayback();
	}
}

void AMandaMelaSoundDecoy::OnRep_DecoySound()
{
	if (!bSoundPlaybackStarted)
	{
		StartSoundPlayback();
	}
}

void AMandaMelaSoundDecoy::StartSoundPlayback()
{
	if (!DecoySound || bSoundPlaybackStarted) return;
	bSoundPlaybackStarted = true;

	RuntimeAttenuation = NewObject<USoundAttenuation>(this);

	PlaySoundPulse();
	GetWorldTimerManager().SetTimer(
		SoundPulseTimer, this, &AMandaMelaSoundDecoy::PlaySoundPulse,
		1.5f, true);
}

void AMandaMelaSoundDecoy::PlaySoundPulse()
{
	if (!DecoySound) return;
	UGameplayStatics::PlaySoundAtLocation(this, DecoySound, GetActorLocation(), 1.0f, 1.0f, 0.0f, RuntimeAttenuation);
}

void AMandaMelaSoundDecoy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaSoundDecoy, DecoySound);
	DOREPLIFETIME(AMandaMelaSoundDecoy, Lifetime);
}
