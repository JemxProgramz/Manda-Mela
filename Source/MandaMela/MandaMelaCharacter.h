// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UTextRenderComponent;
class UInputAction;
class UAnimSequence;
class UAnimMontage;
class USkeletalMesh;
class USkeletalMeshComponent;
class UPrimitiveComponent;
class AMandaMelaPlayerState;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/** Player character with a local first-person camera and a visible world character for other players. */
UCLASS(abstract)
class AMandaMelaCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	AMandaMelaCharacter();	

	UFUNCTION(BlueprintPure, Category="Match")
	int32 GetPublicPlayerNumber() const { return PublicPlayerNumber; }

	UFUNCTION(BlueprintPure, Category="Match")
	FString GetFormattedPublicPlayerNumber() const;
	FVector GetPublicPlayerNumberLocation() const;
	bool IsPublicPlayerNumberFacing(const FVector& ViewerLocation) const;

	void SetPublicPlayerNumber(int32 Number);
	void SetCharacterAppearance(int32 AppearanceIndex);
	void ApplyEliminatedState();
	void ResetForRound(int32 Number);
	bool IsEliminated() const;
	void GetBinocularVisualComponents(TArray<UPrimitiveComponent*>& OutComponents) const;
	void RefreshBinocularVisuals();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayEliminationEffect();

	UFUNCTION(BlueprintImplementableEvent, Category="Match", meta=(DisplayName="On Elimination Effect"))
	void BP_OnEliminationEffect();

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UTextRenderComponent> HeadNumberText;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkeletalMeshComponent> OutfitMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkeletalMeshComponent> FaceMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkeletalMeshComponent> HeadAccessoryMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkeletalMeshComponent> HeldTacticMesh;

	UPROPERTY(ReplicatedUsing=OnRep_HeldTacticalItem)
	EMandaMelaItemType HeldTacticalItemType = EMandaMelaItemType::None;

	UPROPERTY(ReplicatedUsing=OnRep_PublicPlayerNumber, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 PublicPlayerNumber = INDEX_NONE;

	UPROPERTY(ReplicatedUsing=OnRep_CharacterAppearance, BlueprintReadOnly, Category="Character", meta=(AllowPrivateAccess="true", ClampMin="0", ClampMax="4"))
	int32 CharacterAppearanceIndex = 0;

	UPROPERTY(Transient)
	FVector OriginalCreativeMeshScale = FVector::OneVector;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CrouchIdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CrouchWalkAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveCrouchAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCrouchMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> TacticalThrowAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveTacticalActionMontage;

	FName OriginalMeshCollisionProfile;

	bool bUsingCreativeCharacter = false;
	bool bDeathAnimationPlaying = false;
	bool bCrouchQueuedForLanding = false;
	uint8 TacticalMeshMotion = 0;
	float TacticalMeshMotionStartTime = 0.0f;
	float TacticalMeshMotionDuration = 0.0f;
	FVector TacticalMeshMotionStart = FVector::ZeroVector;
	FVector TacticalMeshMotionTarget = FVector::ZeroVector;
	EMandaMelaItemType TacticalMeshMotionItemType = EMandaMelaItemType::None;

	UFUNCTION()
	void OnRep_PublicPlayerNumber();
	UFUNCTION()
	void OnRep_CharacterAppearance();
	UFUNCTION()
	void OnRep_HeldTacticalItem();
	void RefreshHeadNumber();
	void UpdateHeadNumberVisibility();
	void ApplyCreativeAppearance();
	void UpdateCrouchAnimation();
	void StopCrouchAnimation(float BlendOutTime);
	void UpdateTacticalAnimation();
	void UpdateTacticalMeshMotion();
	void UpdateHeldTacticalItem();
	void PreloadTacticalAnimations();
	void PlayTacticalThrowAnimation();
	void SetHeldTacticalMesh(EMandaMelaItemType ItemType);
	void AttachHeldTacticalMesh(EMandaMelaItemType ItemType);
	FVector GetTacticalHandLocation() const;
	void ToggleCrouch();
	void RestoreFromElimination();

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	void SetHeldTacticalItem(EMandaMelaItemType ItemType);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayTacticalPickup(EMandaMelaItemType ItemType, FVector PickupLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayTacticalThrow(EMandaMelaItemType ItemType, FVector TargetLocation, bool bAnimateItemFlight);

	UFUNCTION(BlueprintPure, Category="Inventory")
	EMandaMelaItemType GetHeldTacticalItem() const { return HeldTacticalItemType; }

	FVector GetTacticalItemHandLocation() const { return GetTacticalHandLocation(); }

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

