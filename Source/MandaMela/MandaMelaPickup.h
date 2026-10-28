// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaPickup.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USphereComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USoundAttenuation;
class UProjectileMovementComponent;

UCLASS()
class AMandaMelaPickup : public AActor
{
	GENERATED_BODY()

public:
	AMandaMelaPickup();

	EMandaMelaItemType GetItemType() const { return ItemType; }
	void SetItemType(EMandaMelaItemType NewItemType);
	void LaunchThrownItem(const FVector& LaunchVelocity);
	bool TryClaim();
	void ReleaseClaim();

private:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> TacticMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditDefaultsOnly, Category="Pickup|Models")
	TObjectPtr<USkeletalMesh> BinocularsModel;

	UPROPERTY(EditDefaultsOnly, Category="Pickup|Models")
	TObjectPtr<USkeletalMesh> FlashbangModel;

	UPROPERTY(EditDefaultsOnly, Category="Pickup|Models")
	TObjectPtr<USkeletalMesh> SmokeBombModel;

	UPROPERTY(EditInstanceOnly, ReplicatedUsing=OnRep_ItemType, BlueprintReadOnly, Category="Item", meta=(AllowPrivateAccess="true"))
	EMandaMelaItemType ItemType = EMandaMelaItemType::None;

	bool bClaimed = false;

	UFUNCTION()
	void OnRep_ItemType();

	void UpdateItemPresentation();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class AMandaMelaSmokeCloud : public AActor
{
	GENERATED_BODY()

public:
	AMandaMelaSmokeCloud();
	void Initialize(UNiagaraSystem* Effect, float LifetimeSeconds);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> ParticleEffect;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> VisibilityBlocker;

	UPROPERTY(ReplicatedUsing=OnRep_Effect)
	TObjectPtr<UNiagaraSystem> EffectAsset = nullptr;

	UPROPERTY(Replicated)
	float Lifetime = 6.0f;

	UFUNCTION()
	void OnRep_Effect();

	void ActivateEffect();
	FTimerHandle LifetimeTimer;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class AMandaMelaSoundDecoy : public AActor
{
	GENERATED_BODY()

public:
	AMandaMelaSoundDecoy();
	void Initialize(USoundBase* Sound, float LifetimeSeconds);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_DecoySound)
	TObjectPtr<USoundBase> DecoySound = nullptr;

	UPROPERTY(Replicated)
	float Lifetime = 6.0f;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeAttenuation;
	bool bSoundPlaybackStarted = false;

	UFUNCTION()
	void OnRep_DecoySound();

	void StartSoundPlayback();
	void PlaySoundPulse();
	FTimerHandle LifetimeTimer;
	FTimerHandle SoundPulseTimer;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
