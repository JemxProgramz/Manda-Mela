// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaBotController.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaGameMode.h"
#include "MandaMelaMatchState.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMandaMelaBotsPaused(
		TEXT("MandaMela.BotsPaused"),
		0,
		TEXT("Pause all offline bot movement. Set to 0 to resume."),
		ECVF_Cheat);
}

AMandaMelaBotController::AMandaMelaBotController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;
}

void AMandaMelaBotController::GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->GetActorEyesViewPoint(Location, Rotation);
		Rotation = GetControlRotation();
		return;
	}
	Super::GetPlayerViewPoint(Location, Rotation);
}

void AMandaMelaBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}

	AMandaMelaGameState* MatchState = GetWorld()->GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerState* OwnState = GetPlayerState<AMandaMelaPlayerState>();
	ClearFocus(EAIFocusPriority::Gameplay);
	if (!MatchState || !OwnState || !OwnState->IsAlive() || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		return;
	}

	if (APawn* BotPawn = GetPawn())
	{
		if (CVarMandaMelaBotsPaused.GetValueOnGameThread() != 0)
		{
			if (UPawnMovementComponent* Movement = BotPawn->GetMovementComponent())
			{
				Movement->StopMovementImmediately();
			}
			return;
		}

		if (MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch && OwnState->GetTeam() != EMandaMelaTeam::None)
		{
			AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>();
			AMandaMelaCharacter* TargetCharacter = nullptr;
			float BestDistanceSquared = FMath::Square(AttackRange);
			for (APlayerState* State : MatchState->PlayerArray)
			{
				AMandaMelaPlayerState* Candidate = Cast<AMandaMelaPlayerState>(State);
				AMandaMelaCharacter* CandidateCharacter = Candidate ? Cast<AMandaMelaCharacter>(Candidate->GetPawn()) : nullptr;
				if (!Candidate || !CandidateCharacter || Candidate == OwnState || !Candidate->IsAlive()
					|| Candidate->GetTeam() == EMandaMelaTeam::None || Candidate->GetTeam() == OwnState->GetTeam())
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(BotPawn->GetActorLocation(), CandidateCharacter->GetActorLocation());
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					TargetCharacter = CandidateCharacter;
				}
			}

			if (GameMode && TargetCharacter)
			{
				FVector ViewLocation;
				FRotator ViewRotation;
				GetPlayerViewPoint(ViewLocation, ViewRotation);
				const FRotator DesiredRotation = (TargetCharacter->GetPublicPlayerNumberLocation() - ViewLocation).Rotation();
				SetControlRotation(FMath::RInterpTo(GetControlRotation(), DesiredRotation, DeltaSeconds, 2.0f));

				const float Now = GetWorld()->GetTimeSeconds();
				if (TargetCharacter->GetPublicPlayerNumber() != INDEX_NONE
					&& Now >= NextAttackTime && GameMode->IsPublicNumberVisible(this, TargetCharacter, AttackRange))
				{
					NextAttackTime = Now + AttackInterval;
					GameMode->HandleAttack(this, FString::Printf(TEXT("%04d"), TargetCharacter->GetPublicPlayerNumber()));
				}
			}
		}

		const int32 WanderStep = FMath::FloorToInt(GetWorld()->GetTimeSeconds() / 3.0f);
		const uint32 DirectionSeed = HashCombine(static_cast<uint32>(GetUniqueID()), GetTypeHash(WanderStep));
		const float Angle = FMath::DegreesToRadians(static_cast<float>(DirectionSeed % 36000u) / 100.0f);
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		BotPawn->AddMovementInput(Direction, 0.65f);
	}
}
