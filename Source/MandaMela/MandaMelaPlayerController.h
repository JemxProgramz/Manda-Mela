// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UUserWidget;
class AActor;
class UMandaMelaGameplayWidget;
class AMandaMelaPickup;
class AMandaMelaCharacter;
class APlayerState;
class USoundBase;
class UPrimitiveComponent;
class UCameraComponent;
struct FInputActionValue;

UCLASS(abstract)
class AMandaMelaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMandaMelaPlayerController();

protected:
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	UPROPERTY(EditAnywhere, Config, Category="Input|Touch Controls")
	bool bForceTouchControls = false;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void SetupInputComponent() override;
	bool ShouldUseTouchControls() const;

public:
	void AddGuessDigit(int32 Digit);
	void BackspaceGuess();
	void ClearGuess();
	void SubmitGuess();
	void RequestMatchMode(EMandaMelaMatchMode Mode);
	void RequestStartMatch();
	int32 GetSelectedInventorySlot() const { return SelectedInventorySlot; }
	void SetSelectedInventorySlotFromServer(int32 Slot);
	void RefreshHeldTacticalItem(bool bSelectOtherIfEmpty);

	UFUNCTION(Exec)
	void MM_Suicide();

	UFUNCTION(Exec)
	void MM_TestFlash(float Intensity = 1.0f);

	UFUNCTION(Client, Reliable)
	void ClientWatchSpectatorTarget(AActor* Target);

	UFUNCTION(Client, Reliable)
	void ClientShowDeathNotice(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientAttackFeedback(bool bCorrect, const FString& Message);

	UFUNCTION(Client, Unreliable)
	void ClientGameplayFeedback(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientUseBinoculars(float Duration);

	UFUNCTION(Client, Reliable)
	void ClientStoreCameraRecord(int32 Number);

	UFUNCTION(Client, Reliable)
	void ClientApplyFlash(float Intensity, float Duration);

	UFUNCTION(Client, Unreliable)
	void ClientPlayWarning(USoundBase* Sound, FVector Location);

private:
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UMandaMelaGameplayWidget> GameplayWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category="Flashbang", meta=(ClampMin="0.0"))
	float FlashHoldTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Flashbang", meta=(ClampMin="0.0"))
	float FlashFadeTime = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category="Flashbang")
	FLinearColor FlashWarmTint = FLinearColor(1.0f, 0.9569f, 0.8392f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Flashbang", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FlashMinIntensity = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category="Flashbang", meta=(ClampMin="0.0"))
	float FlashBloomBoost = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Flashbang", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FlashDesaturation = 0.8f;

	UPROPERTY(EditAnywhere, Category="Flashbang")
	TObjectPtr<USoundBase> FlashRingSound;

	UPROPERTY(Transient)
	TObjectPtr<UMandaMelaGameplayWidget> GameplayWidget;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeMappingContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> RuntimeInputActions;
	TArray<TWeakObjectPtr<AMandaMelaCharacter>> SpectatorTargets;

	FString GuessDigits;
	FString LastFeedback;
	FString SpectatorDeathMessage;
	int32 SelectedInventorySlot = 0;
	int32 SpectatorTargetIndex = INDEX_NONE;
	bool bScoreboardOpen = false;
	bool bPauseMenuOpen = false;
	bool bSubmissionPending = false;
	bool bBinocularsActive = false;
	bool bFlashActive = false;
	bool bFlashPostProcessSaved = false;
	bool bDeathmatchSpectating = false;
	bool bBinocularSavedVisibility = false;
	float NormalCameraFOV = 90.0f;
	float NormalSpringArmLength = 400.0f;
	float NormalCameraLocationZ = 0.0f;
	float LastActionRequestTime = -1000.0f;
	float LastAttackRequestTime = -1000.0f;
	float FlashBaseIntensity = 0.0f;
	float FlashEffectStartTime = 0.0f;
	float FlashHoldDuration = 0.0f;
	float FlashFadeDuration = 0.0f;
	float SavedFlashPostProcessBlendWeight = 1.0f;
	FPostProcessSettings SavedFlashPostProcessSettings;
	FTimerHandle HudRefreshTimer;
	FTimerHandle FlashTimer;
	FTimerHandle BinocularTimer;
	FTimerHandle FeedbackTimer;
	FTimerHandle CameraRecordTimer;
	FTimerHandle DeathNoticeTimer;
	TWeakObjectPtr<AMandaMelaCharacter> BinocularPawn;
	TWeakObjectPtr<UCameraComponent> FlashCamera;
	TArray<bool> BinocularMeshVisibility;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> BinocularMeshComponents;

	UInputAction* AddRuntimeAction(FName Name, const TArray<FKey>& Keys);
	void SetupGameplayInput();
	void Interact();
	void SelectSlot(int32 Slot);
	void CycleSelectedSlot();
	void UseSlot(int32 Slot);
	void UseSelectedSlot();
	void DropSelectedItem();
	AMandaMelaCharacter* TraceTarget() const;
	AMandaMelaPickup* FindNearbyPickup() const;
	void RefreshHud();
	void StartFlashEffect(float Intensity, float Duration);
	void UpdateFlashEffect();
	void ClearFlashEffect();
	float GetCurrentFlashIntensity() const;
	void ApplyFlashPostProcess(float Intensity);
	void RestoreBinoculars();
	void ApplyBinocularView();
	void ClearBinocularView();
	void CaptureCameraPhoto(int32 Number);
	void ClearCameraRecord();
	void RefreshSpectatorTargets();
	void CycleSpectatorTarget(int32 Direction);

	UFUNCTION(Server, Reliable)
	void ServerSubmitGuess(const FString& Digits);

	UFUNCTION(Server, Reliable)
	void ServerSelectMatchMode(EMandaMelaMatchMode Mode);

	UFUNCTION(Server, Reliable)
	void ServerStartMatch();

	UFUNCTION(Server, Reliable)
	void ServerRequestPickup(AMandaMelaPickup* Pickup);

	UFUNCTION(Server, Reliable)
	void ServerRequestDrop(int32 Slot);

	UFUNCTION(Server, Reliable)
	void ServerRequestUseItem(int32 Slot, AMandaMelaCharacter* VisibleTarget);

	UFUNCTION(Server, Reliable)
	void ServerSelectInventorySlot(int32 Slot);

	UFUNCTION(Client, Reliable)
	void ClientSetSelectedInventorySlot(int32 Slot);

	UFUNCTION(Server, Reliable)
	void ServerDebugSelfEliminate();

	UFUNCTION()
	void InputSpectatorPrevious();

	UFUNCTION()
	void InputSpectatorNext();

	UFUNCTION() void InputScoreboardPressed();
	UFUNCTION() void InputScoreboardReleased();
	UFUNCTION() void InputCancel();
	UFUNCTION() void InputSubmit();
	UFUNCTION() void InputBackspace();
	UFUNCTION() void InputClear();
	UFUNCTION() void InputSelectDeathmatch();
	UFUNCTION() void InputSelectTeamDeathmatch();
	UFUNCTION() void InputSelectCaptureTheFlag();
	UFUNCTION() void InputInteract();
	UFUNCTION() void InputCycleSlot();
	UFUNCTION() void InputUseSelectedSlot();
	UFUNCTION() void InputDropItem();
	void InputDigit(const FInputActionValue& Value, int32 Digit);
};
