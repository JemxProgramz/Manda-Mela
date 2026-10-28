// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaGameMode.generated.h"

class AMandaMelaPickup;
class AMandaMelaFlag;
class AMandaMelaBotController;
class AMandaMelaPlayerController;
class AMandaMelaCharacter;
class APlayerState;
class APlayerStart;
class UNiagaraSystem;
class USoundBase;

struct FMandaMelaPendingCameraPhoto
{
	TWeakObjectPtr<AMandaMelaCharacter> Target;
	TWeakObjectPtr<AMandaMelaPlayerState> TargetState;
	int32 Number = INDEX_NONE;
	FString TargetName;
	bool bCompleted = false;
};

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AMandaMelaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AMandaMelaGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	void HandleAttack(AController* Attacker, const FString& Digits);
	void HandleDebugSelfElimination(AController* Player);
	void HandlePickup(AMandaMelaPlayerController* Player, AMandaMelaPickup* Pickup);
	void HandleDrop(AMandaMelaPlayerController* Player, int32 Slot);
	void HandleUseItem(AMandaMelaPlayerController* Player, int32 Slot, class AMandaMelaCharacter* VisibleTarget);
	void HandleCameraPhotoCapture(AMandaMelaPlayerController* Player);
	void HandleCameraPhotoAnswer(AMandaMelaPlayerController* Player, const FString& Digits);
	void CancelCameraPhoto(AMandaMelaPlayerController* Player);
	static bool IsCameraPhotoAnswerCorrect(const FString& Digits, int32 CapturedNumber);
	void HandleFlagTouch(AMandaMelaFlag* TouchedFlag, AMandaMelaCharacter* Character);
	void SelectMatchMode(AMandaMelaPlayerController* Host, EMandaMelaMatchMode Mode);
	void StartMatch(AMandaMelaPlayerController* Host);
	bool IsPublicNumberVisible(AController* Viewer, AMandaMelaCharacter* Target, float MaxRange) const;

	UFUNCTION(BlueprintCallable, Category="Match")
	void StartNewRound();

protected:
	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Testing", meta=(ClampMin="0", ClampMax="19"))
	int32 OfflineBotCount = 3;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Feedback")
	TObjectPtr<USoundBase> IncorrectGuessWarningSound;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items")
	TObjectPtr<UNiagaraSystem> SmokeParticleEffect;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items")
	TObjectPtr<USoundBase> SoundDecoyEffect;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items", meta=(ClampMin="0.1", ClampMax="3.0"))
	float FlashFuseTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items", meta=(ClampMin="0", ClampMax="30"))
	int32 InitialLootCount = 6;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items", meta=(ClampMin="1", ClampMax="40"))
	int32 MaxWorldLootCount = 12;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items", meta=(ClampMin="2.0"))
	float LootSpawnInterval = 15.0f;

	UPROPERTY(EditDefaultsOnly, Category="Gameplay|Items", meta=(ClampMin="200.0"))
	float LootSpawnRadius = 900.0f;

private:
	static constexpr int32 MinPlayers = 2;
	static constexpr int32 MaxPlayers = 20;
	static constexpr int32 TeamDeathmatchScoreLimit = 10;
	static constexpr int32 CaptureTheFlagScoreLimit = 3;
	static constexpr float TeamDeathmatchDuration = 300.0f;
	static constexpr float CaptureTheFlagDuration = 600.0f;
	static constexpr float TeamRespawnDelay = 3.0f;
	static constexpr float IncorrectGuessCooldown = 3.0f;
	static constexpr float InteractionRange = 500.0f;
	static constexpr float ItemEffectRange = 2000.0f;
	static constexpr float WarningAudibleRange = 1500.0f;
	static constexpr float FlashRadius = 700.0f;

	void SpawnOfflineBots(APlayerController* LocalPlayer);
	void StartMatchInternal(EMandaMelaMatchMode Mode);
	void AssignTeamsForMatch();
	void AssignTeamToPlayer(AMandaMelaPlayerState* PlayerState);
	int32 CountTeamPlayers(EMandaMelaTeam Team, bool bLivingOnly) const;
	void AssignPlayerNumber(AMandaMelaPlayerState* PlayerState, const TSet<int32>& AlreadyUsedNumbers);
	int32 GenerateUnusedNumber(const TSet<int32>& AlreadyUsedNumbers) const;
	void RefreshPlayerNumberDisplay(AMandaMelaPlayerState* PlayerState);
	void UpdateLivingPlayersAndCheckWinner(APlayerState* ExcludedPlayer = nullptr);
	void BeginDeathmatchSpectating(AMandaMelaPlayerState* EliminatedPlayer);
	void UpdateTeamPopulationResult();
	void HandleTeamTimeout();
	void EndMatchForTeam(EMandaMelaTeam WinningTeam);
	void RespawnTeamPlayer(TWeakObjectPtr<AMandaMelaPlayerState> PlayerState);
	void DropCarriedFlag(AMandaMelaPlayerState* PlayerState);
	void EliminateTarget(AMandaMelaPlayerState* AttackerState, AMandaMelaPlayerState* TargetState, AMandaMelaCharacter* TargetCharacter);
	AMandaMelaFlag* FindFlagCarriedBy(const AMandaMelaPlayerState* PlayerState) const;
	void SetupTeamFlags();
	void SetupTeamSpawnPoints();
	void FindTeamBaseLocations(FVector& OutRedLocation, FVector& OutBlueLocation);
	APlayerStart* FindTeamPlayerStart(EMandaMelaTeam Team, const TArray<APlayerStart*>& Starts) const;
	void OnMatchClockTick();
	bool IsCurrentPlayer(const APlayerState* PlayerState) const;
	bool IsVisibleTarget(AController* Attacker, AMandaMelaCharacter* Target, float MaxRange) const;
	bool IsPointInCameraView(AController* Viewer, const FVector& Point, float MaxRange, float* OutCenterScore = nullptr, float AspectRatioOverride = 0.0f) const;
	AMandaMelaCharacter* FindCameraPhotoTarget(AMandaMelaPlayerController* Player, float MaxRange) const;
	TMap<TWeakObjectPtr<AMandaMelaPlayerController>, int32> ActiveCameraSessions;
	void DetonateFlashbang(TWeakObjectPtr<AMandaMelaPlayerState> ThrowerState, TWeakObjectPtr<AMandaMelaCharacter> ThrowerCharacter, const FVector& FlashLocation);
	void StartLootSpawning();
	void StopLootSpawning();
	void SpawnRandomLoot();
	bool FindRandomLootLocation(FVector& OutLocation) const;
	TMap<TWeakObjectPtr<AMandaMelaPlayerController>, FMandaMelaPendingCameraPhoto> PendingCameraPhotos;
	TSet<int32> RoundUsedNumbers;
	bool bOfflineBotsSpawned = false;
	bool bSpawnCameraNext = false;
	FTimerHandle LootSpawnTimer;
	FTimerHandle MatchClockTimer;
	UPROPERTY(Transient)
	TObjectPtr<AMandaMelaFlag> RedFlag = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<AMandaMelaFlag> BlueFlag = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<APlayerStart> RedTeamStart = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<APlayerStart> BlueTeamStart = nullptr;
	FVector RedBaseLocation = FVector::ZeroVector;
	FVector BlueBaseLocation = FVector::ZeroVector;
};



