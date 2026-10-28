// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaMatchState.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaFlag.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

AMandaMelaPlayerState::AMandaMelaPlayerState()
{
	bReplicates = true;
}

EMandaMelaItemType AMandaMelaPlayerState::GetInventoryItem(int32 Slot) const
{
	return Slot >= 0 && Slot < UE_ARRAY_COUNT(InventorySlots) ? InventorySlots[Slot].ItemType : EMandaMelaItemType::None;
}

bool AMandaMelaPlayerState::TryAddItem(EMandaMelaItemType ItemType)
{
	if (!HasAuthority() || !IsAlive() || ItemType == EMandaMelaItemType::None)
	{
		return false;
	}

	for (FMandaMelaInventorySlot& Slot : InventorySlots)
	{
		if (Slot.ItemType == EMandaMelaItemType::None)
		{
			Slot.ItemType = ItemType;
			ForceNetUpdate();
			return true;
		}
	}
	return false;
}

bool AMandaMelaPlayerState::TryRemoveItem(int32 Slot, EMandaMelaItemType& OutItemType)
{
	if (!HasAuthority() || !IsAlive() || Slot < 0 || Slot >= UE_ARRAY_COUNT(InventorySlots))
	{
		return false;
	}

	OutItemType = InventorySlots[Slot].ItemType;
	if (OutItemType == EMandaMelaItemType::None)
	{
		return false;
	}
	InventorySlots[Slot].ItemType = EMandaMelaItemType::None;
	ForceNetUpdate();
	return true;
}

void AMandaMelaPlayerState::SetCooldownEndServerTime(float EndTime)
{
	if (HasAuthority())
	{
		CooldownEndServerTime = EndTime;
		ForceNetUpdate();
	}
}

void AMandaMelaPlayerState::SetTeam(EMandaMelaTeam NewTeam)
{
	if (HasAuthority())
	{
		Team = NewTeam;
		ForceNetUpdate();
	}
}

void AMandaMelaPlayerState::AddElimination()
{
	if (HasAuthority())
	{
		++EliminationCount;
		ForceNetUpdate();
	}
}

void AMandaMelaPlayerState::AddCapture()
{
	if (HasAuthority())
	{
		++CaptureCount;
		ForceNetUpdate();
	}
}

void AMandaMelaPlayerState::ResetMatchStats()
{
	if (HasAuthority())
	{
		EliminationCount = 0;
		CaptureCount = 0;
		ForceNetUpdate();
	}
}

void AMandaMelaPlayerState::InitializeForRound(int32 NewSecretNumber)
{
	if (!HasAuthority())
	{
		return;
	}

	SecretNumber = NewSecretNumber;
	bParticipating = true;
	bAlive = true;
	CooldownEndServerTime = 0.0f;
	for (FMandaMelaInventorySlot& Slot : InventorySlots)
	{
		Slot.ItemType = EMandaMelaItemType::None;
	}
	ForceNetUpdate();
}

void AMandaMelaPlayerState::SetAlive(bool bNewAlive)
{
	if (HasAuthority() && bParticipating && bAlive != bNewAlive)
	{
		bAlive = bNewAlive;
		ForceNetUpdate();
		OnRep_Alive();
	}
}

void AMandaMelaPlayerState::OnRep_Alive()
{
	if (AMandaMelaCharacter* Character = Cast<AMandaMelaCharacter>(GetPawn()))
	{
		Character->ApplyEliminatedState();
	}
	if (APlayerController* OwningController = Cast<APlayerController>(GetOwner()))
	{
		OwningController->SetIgnoreMoveInput(!IsAlive());
		OwningController->SetIgnoreLookInput(!IsAlive());
	}
}

void AMandaMelaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaPlayerState, bAlive);
	DOREPLIFETIME(AMandaMelaPlayerState, bParticipating);
	DOREPLIFETIME_CONDITION(AMandaMelaPlayerState, InventorySlots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMandaMelaPlayerState, CooldownEndServerTime, COND_OwnerOnly);
	DOREPLIFETIME(AMandaMelaPlayerState, Team);
	DOREPLIFETIME(AMandaMelaPlayerState, EliminationCount);
	DOREPLIFETIME(AMandaMelaPlayerState, CaptureCount);
}

int32 AMandaMelaGameState::GetTeamScore(EMandaMelaTeam Team) const
{
	return Team == EMandaMelaTeam::Red ? RedTeamScore : (Team == EMandaMelaTeam::Blue ? BlueTeamScore : 0);
}

int32 AMandaMelaGameState::GetCaptureCount(EMandaMelaTeam Team) const
{
	return Team == EMandaMelaTeam::Red ? RedTeamCaptures : (Team == EMandaMelaTeam::Blue ? BlueTeamCaptures : 0);
}

AMandaMelaFlag* AMandaMelaGameState::GetFlagForTeam(EMandaMelaTeam Team) const
{
	return Team == EMandaMelaTeam::Red ? RedFlag.Get() : (Team == EMandaMelaTeam::Blue ? BlueFlag.Get() : nullptr);
}

void AMandaMelaGameState::SetMatchPhase(EMandaMelaMatchPhase NewPhase)
{
	if (HasAuthority())
	{
		MatchPhase = NewPhase;
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::SetMatchMode(EMandaMelaMatchMode NewMode)
{
	if (HasAuthority() && MatchPhase != EMandaMelaMatchPhase::InProgress)
	{
		MatchMode = NewMode;
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::SetMatchEndServerTime(float EndTime)
{
	if (HasAuthority())
	{
		MatchEndServerTime = EndTime;
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::SetTeamScore(EMandaMelaTeam Team, int32 Score)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Team == EMandaMelaTeam::Red)
	{
		RedTeamScore = FMath::Max(0, Score);
	}
	else if (Team == EMandaMelaTeam::Blue)
	{
		BlueTeamScore = FMath::Max(0, Score);
	}
	ForceNetUpdate();
	OnRep_MatchState();
}

void AMandaMelaGameState::AddTeamCapture(EMandaMelaTeam Team)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Team == EMandaMelaTeam::Red)
	{
		++RedTeamCaptures;
	}
	else if (Team == EMandaMelaTeam::Blue)
	{
		++BlueTeamCaptures;
	}
	ForceNetUpdate();
	OnRep_MatchState();
}

void AMandaMelaGameState::SetTeamFlags(AMandaMelaFlag* NewRedFlag, AMandaMelaFlag* NewBlueFlag)
{
	if (HasAuthority())
	{
		RedFlag = NewRedFlag;
		BlueFlag = NewBlueFlag;
		ForceNetUpdate();
	}
}

void AMandaMelaGameState::ResetForRound(EMandaMelaMatchMode NewMode)
{
	if (HasAuthority())
	{
		MatchPhase = EMandaMelaMatchPhase::InProgress;
		LivingPlayerCount = 0;
		Winner = nullptr;
		bDraw = false;
		WinningTeam = EMandaMelaTeam::None;
		MatchMode = NewMode;
		MatchEndServerTime = 0.0f;
		RedTeamScore = 0;
		BlueTeamScore = 0;
		RedTeamCaptures = 0;
		BlueTeamCaptures = 0;
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::SetLivingPlayerCount(int32 Count)
{
	if (HasAuthority())
	{
		LivingPlayerCount = FMath::Max(0, Count);
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::SetResult(APlayerState* NewWinner, bool bIsDraw, EMandaMelaTeam NewWinningTeam)
{
	if (HasAuthority())
	{
		Winner = NewWinner;
		bDraw = bIsDraw;
		WinningTeam = NewWinningTeam;
		MatchPhase = EMandaMelaMatchPhase::Ended;
		MatchEndServerTime = 0.0f;
		ForceNetUpdate();
		OnRep_MatchState();
	}
}

void AMandaMelaGameState::OnRep_MatchState()
{
	// UI reads replicated match state from its owning PlayerController.
}

void AMandaMelaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaGameState, MatchPhase);
	DOREPLIFETIME(AMandaMelaGameState, LivingPlayerCount);
	DOREPLIFETIME(AMandaMelaGameState, Winner);
	DOREPLIFETIME(AMandaMelaGameState, bDraw);
	DOREPLIFETIME(AMandaMelaGameState, MatchMode);
	DOREPLIFETIME(AMandaMelaGameState, WinningTeam);
	DOREPLIFETIME(AMandaMelaGameState, MatchEndServerTime);
	DOREPLIFETIME(AMandaMelaGameState, RedTeamScore);
	DOREPLIFETIME(AMandaMelaGameState, BlueTeamScore);
	DOREPLIFETIME(AMandaMelaGameState, RedTeamCaptures);
	DOREPLIFETIME(AMandaMelaGameState, BlueTeamCaptures);
	DOREPLIFETIME(AMandaMelaGameState, RedFlag);
	DOREPLIFETIME(AMandaMelaGameState, BlueFlag);
}
