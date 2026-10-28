// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "MandaMelaMatchState.generated.h"

class AMandaMelaGameMode;
class AMandaMelaFlag;

UENUM(BlueprintType)
enum class EMandaMelaMatchMode : uint8
{
	Deathmatch,
	TeamDeathmatch,
	CaptureTheFlag
};

UENUM(BlueprintType)
enum class EMandaMelaTeam : uint8
{
	None,
	Red,
	Blue
};

UENUM(BlueprintType)
enum class EMandaMelaMatchPhase : uint8
{
	Waiting,
	InProgress,
	Ended
};

UENUM(BlueprintType)
enum class EMandaMelaItemType : uint8
{
	None,
	Binoculars,
	Camera,
	Flashbang,
	SmokeBomb,
	SoundDecoy
};

USTRUCT(BlueprintType)
struct FMandaMelaInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EMandaMelaItemType ItemType = EMandaMelaItemType::None;
};

UCLASS()
class AMandaMelaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AMandaMelaPlayerState();

	UFUNCTION(BlueprintPure, Category="Match")
	bool IsAlive() const { return bAlive && bParticipating; }

	UFUNCTION(BlueprintPure, Category="Match")
	bool IsParticipating() const { return bParticipating; }

	UFUNCTION(BlueprintPure, Category="Match")
	float GetCooldownEndServerTime() const { return CooldownEndServerTime; }

	UFUNCTION(BlueprintPure, Category="Inventory")
	EMandaMelaItemType GetInventoryItem(int32 Slot) const;

	bool TryAddItem(EMandaMelaItemType ItemType);
	bool TryRemoveItem(int32 Slot, EMandaMelaItemType& OutItemType);
	void SetCooldownEndServerTime(float EndTime);
	int32 GetSecretNumber() const { return SecretNumber; }
	EMandaMelaTeam GetTeam() const { return Team; }
	int32 GetEliminationCount() const { return EliminationCount; }
	int32 GetCaptureCount() const { return CaptureCount; }

private:
	friend class AMandaMelaGameMode;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeForRound(int32 NewSecretNumber);
	void SetAlive(bool bNewAlive);
	void SetTeam(EMandaMelaTeam NewTeam);
	void AddElimination();
	void AddCapture();
	void ResetMatchStats();

	UPROPERTY(ReplicatedUsing=OnRep_Alive, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	bool bAlive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	bool bParticipating = false;

	UPROPERTY(Replicated)
	FMandaMelaInventorySlot InventorySlots[2];

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	float CooldownEndServerTime = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	EMandaMelaTeam Team = EMandaMelaTeam::None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 EliminationCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 CaptureCount = 0;

	// Server-only. This value is never registered for replication.
	int32 SecretNumber = INDEX_NONE;

	UFUNCTION()
	void OnRep_Alive();
};

UCLASS()
class AMandaMelaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Match")
	EMandaMelaMatchPhase GetMatchPhase() const { return MatchPhase; }

	UFUNCTION(BlueprintPure, Category="Match")
	int32 GetLivingPlayerCount() const { return LivingPlayerCount; }

	UFUNCTION(BlueprintPure, Category="Match")
	APlayerState* GetWinner() const { return Winner; }

	UFUNCTION(BlueprintPure, Category="Match")
	bool IsDraw() const { return bDraw; }
	EMandaMelaMatchMode GetMatchMode() const { return MatchMode; }
	EMandaMelaTeam GetWinningTeam() const { return WinningTeam; }
	float GetMatchEndServerTime() const { return MatchEndServerTime; }
	int32 GetTeamScore(EMandaMelaTeam Team) const;
	int32 GetCaptureCount(EMandaMelaTeam Team) const;
	AMandaMelaFlag* GetFlagForTeam(EMandaMelaTeam Team) const;

private:
	friend class AMandaMelaGameMode;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetMatchPhase(EMandaMelaMatchPhase NewPhase);
	void ResetForRound(EMandaMelaMatchMode NewMode);
	void SetLivingPlayerCount(int32 Count);
	void SetResult(APlayerState* NewWinner, bool bIsDraw, EMandaMelaTeam NewWinningTeam = EMandaMelaTeam::None);
	void SetMatchMode(EMandaMelaMatchMode NewMode);
	void SetMatchEndServerTime(float EndTime);
	void SetTeamScore(EMandaMelaTeam Team, int32 Score);
	void AddTeamCapture(EMandaMelaTeam Team);
	void SetTeamFlags(AMandaMelaFlag* RedFlag, AMandaMelaFlag* BlueFlag);

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	EMandaMelaMatchPhase MatchPhase = EMandaMelaMatchPhase::Waiting;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 LivingPlayerCount = 0;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	TObjectPtr<APlayerState> Winner = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	bool bDraw = false;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	EMandaMelaMatchMode MatchMode = EMandaMelaMatchMode::Deathmatch;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	EMandaMelaTeam WinningTeam = EMandaMelaTeam::None;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	float MatchEndServerTime = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 RedTeamScore = 0;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 BlueTeamScore = 0;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 RedTeamCaptures = 0;

	UPROPERTY(ReplicatedUsing=OnRep_MatchState, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	int32 BlueTeamCaptures = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AMandaMelaFlag> RedFlag = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AMandaMelaFlag> BlueFlag = nullptr;

	UFUNCTION()
	void OnRep_MatchState();
};
