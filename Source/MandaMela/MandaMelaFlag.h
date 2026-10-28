// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaFlag.generated.h"

class AMandaMelaCharacter;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMandaMelaFlagStatus : uint8
{
	Home,
	Carried,
	Dropped
};

UCLASS()
class AMandaMelaFlag : public AActor
{
	GENERATED_BODY()

public:
	AMandaMelaFlag();

	EMandaMelaTeam GetTeam() const { return Team; }
	EMandaMelaFlagStatus GetFlagStatus() const { return FlagStatus; }
	AMandaMelaCharacter* GetCarrier() const { return Carrier; }
	const TCHAR* GetStateText() const;

	void InitializeFlag(EMandaMelaTeam NewTeam, const FTransform& Home);
	void SetCarriedBy(AMandaMelaCharacter* NewCarrier);
	void DropAt(const FVector& Location);
	void ReturnHome();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> TouchZone;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PoleMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BannerMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(ReplicatedUsing=OnRep_FlagState)
	EMandaMelaTeam Team = EMandaMelaTeam::None;

	UPROPERTY(ReplicatedUsing=OnRep_FlagState)
	EMandaMelaFlagStatus FlagStatus = EMandaMelaFlagStatus::Home;

	UPROPERTY(ReplicatedUsing=OnRep_FlagState)
	TObjectPtr<AMandaMelaCharacter> Carrier = nullptr;

	FTransform HomeTransform;

	UFUNCTION()
	void OnRep_FlagState();

	UFUNCTION()
	void OnTouchBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void RefreshPresentation();
};
