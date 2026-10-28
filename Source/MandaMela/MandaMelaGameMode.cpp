// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaGameMode.h"
#include "MandaMelaBotController.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaFlag.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaPickup.h"
#include "MandaMelaPlayerController.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void SendAttackFeedback(AController* Controller, bool bCorrect, const FString& Message)
	{
		if (AMandaMelaPlayerController* PlayerController = Cast<AMandaMelaPlayerController>(Controller))
		{
			PlayerController->ClientAttackFeedback(bCorrect, Message);
		}
	}
}

AMandaMelaGameMode::AMandaMelaGameMode()
{
	PlayerStateClass = AMandaMelaPlayerState::StaticClass();
	GameStateClass = AMandaMelaGameState::StaticClass();
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SmokeEffect(TEXT("/Game/Tactics/NS_SmokeBomb.NS_SmokeBomb"));
	if (SmokeEffect.Succeeded())
	{
		SmokeParticleEffect = SmokeEffect.Object;
	}
}

void AMandaMelaGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// The active GameMode Blueprint may retain empty or stale class references after redirects.
	// Restore the required match state classes before AGameModeBase creates the GameState.
	if (!PlayerStateClass || !PlayerStateClass->IsChildOf(AMandaMelaPlayerState::StaticClass()))
	{
		PlayerStateClass = AMandaMelaPlayerState::StaticClass();
	}
	if (!GameStateClass || !GameStateClass->IsChildOf(AMandaMelaGameState::StaticClass()))
	{
		GameStateClass = AMandaMelaGameState::StaticClass();
	}

	Super::InitGame(MapName, Options, ErrorMessage);
}

void AMandaMelaGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	if (GameState && GameState->PlayerArray.Num() >= MaxPlayers)
	{
		ErrorMessage = TEXT("This match is limited to 20 players.");
		return;
	}
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

void AMandaMelaGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	AMandaMelaPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!PlayerState || !MatchState)
	{
		return;
	}
	SpawnOfflineBots(NewPlayer);

	if (MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress && PlayerState->GetSecretNumber() == INDEX_NONE)
	{
		if (MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch)
		{
			AssignTeamToPlayer(PlayerState);
		}
		TSet<int32> LivingNumbers;
		for (APlayerState* State : MatchState->PlayerArray)
		{
			if (const AMandaMelaPlayerState* Existing = Cast<AMandaMelaPlayerState>(State); Existing && Existing != PlayerState && Existing->IsAlive())
			{
				LivingNumbers.Add(Existing->GetSecretNumber());
			}
		}
		const int32 Number = GenerateUnusedNumber(LivingNumbers);
		if (Number != INDEX_NONE)
		{
			PlayerState->InitializeForRound(Number);
			RoundUsedNumbers.Add(Number);
		}
		MatchState->SetLivingPlayerCount(MatchState->GetLivingPlayerCount() + 1);
		if (NewPlayer && !NewPlayer->GetPawn())
		{
			if (MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch)
			{
				if (APlayerStart* StartSpot = Cast<APlayerStart>(ChoosePlayerStart(NewPlayer)))
				{
					RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
				}
				else
				{
					RestartPlayer(NewPlayer);
				}
			}
			else
			{
				RestartPlayer(NewPlayer);
			}
		}
	}
}

void AMandaMelaGameMode::SpawnOfflineBots(APlayerController* LocalPlayer)
{
	if (bOfflineBotsSpawned || GetNetMode() != NM_Standalone || OfflineBotCount <= 0 || !LocalPlayer)
	{
		return;
	}
	bOfflineBotsSpawned = true;

	const FVector Center = LocalPlayer->GetPawn() ? LocalPlayer->GetPawn()->GetActorLocation() : LocalPlayer->GetFocalLocation();
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	const int32 ConnectedCount = MatchState ? MatchState->PlayerArray.Num() : 1;
	const int32 BotCount = FMath::Min(OfflineBotCount, MaxPlayers - ConnectedCount);
	for (int32 Index = 0; Index < BotCount; ++Index)
	{
		const float Angle = 2.0f * PI * Index / BotCount;
		const FVector SpawnLocation = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 500.0f;
		const FRotator SpawnRotation(0.0f, (Center - SpawnLocation).Rotation().Yaw, 0.0f);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMandaMelaBotController* Bot = GetWorld()->SpawnActor<AMandaMelaBotController>(AMandaMelaBotController::StaticClass(), SpawnLocation, SpawnRotation, SpawnParameters);
		if (!Bot || !Bot->GetPlayerState<AMandaMelaPlayerState>())
		{
			if (Bot) Bot->Destroy();
			continue;
		}

		Bot->GetPlayerState<AMandaMelaPlayerState>()->SetPlayerName(FString::Printf(TEXT("Bot %d"), Index + 1));
		RestartPlayerAtTransform(Bot, FTransform(SpawnRotation, SpawnLocation));
		if (AMandaMelaCharacter* BotCharacter = Cast<AMandaMelaCharacter>(Bot->GetPawn()))
		{
			BotCharacter->SetCharacterAppearance((Index % 4) + 1);
		}
	}
}

void AMandaMelaGameMode::Logout(AController* Exiting)
{
	AMandaMelaPlayerState* LeavingPlayer = Exiting ? Exiting->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	const bool bWasParticipating = LeavingPlayer && LeavingPlayer->IsParticipating();
	Super::Logout(Exiting);
	if (bWasParticipating)
	{
		UpdateLivingPlayersAndCheckWinner(LeavingPlayer);
	}
}

void AMandaMelaGameMode::SelectMatchMode(AMandaMelaPlayerController* Host, EMandaMelaMatchMode Mode)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!HasAuthority() || !Host || !Host->IsLocalController() || !MatchState || MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	switch (Mode)
	{
	case EMandaMelaMatchMode::Deathmatch:
	case EMandaMelaMatchMode::TeamDeathmatch:
	case EMandaMelaMatchMode::CaptureTheFlag:
		MatchState->SetMatchMode(Mode);
		break;
	default:
		break;
	}
}

void AMandaMelaGameMode::StartMatch(AMandaMelaPlayerController* Host)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!HasAuthority() || !Host || !Host->IsLocalController() || !MatchState || MatchState->PlayerArray.Num() < MinPlayers
		|| MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress)
	{
		if (Host)
		{
			Host->ClientGameplayFeedback(TEXT("The host can start when at least two players are connected."));
		}
		return;
	}
	StartMatchInternal(MatchState->GetMatchMode());
}

void AMandaMelaGameMode::StartMatchInternal(EMandaMelaMatchMode Mode)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!HasAuthority() || !MatchState || MatchState->PlayerArray.Num() < MinPlayers || MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(MatchClockTimer);
	MatchState->ResetForRound(Mode);
	AssignTeamsForMatch();
	FindTeamBaseLocations(RedBaseLocation, BlueBaseLocation);
	if (Mode != EMandaMelaMatchMode::Deathmatch)
	{
		SetupTeamSpawnPoints();
	}
	if (Mode == EMandaMelaMatchMode::CaptureTheFlag)
	{
		SetupTeamFlags();
	}
	else
	{
		if (RedFlag) RedFlag->Destroy();
		if (BlueFlag) BlueFlag->Destroy();
		RedFlag = nullptr;
		BlueFlag = nullptr;
		MatchState->SetTeamFlags(nullptr, nullptr);
	}

	RoundUsedNumbers.Reset();
	int32 LivingCount = 0;
	for (APlayerState* State : MatchState->PlayerArray)
	{
		AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State);
		if (!PlayerState)
		{
			continue;
		}
		PlayerState->ResetMatchStats();
		AController* PlayerController = Cast<AController>(PlayerState->GetOwner());
		if (PlayerController && Mode != EMandaMelaMatchMode::Deathmatch)
		{
			AActor* StartSpot = ChoosePlayerStart(PlayerController);
			if (StartSpot)
			{
				if (APawn* Pawn = PlayerController->GetPawn())
				{
					Pawn->TeleportTo(StartSpot->GetActorLocation(), StartSpot->GetActorRotation());
				}
			}
			if (!PlayerController->GetPawn())
			{
				if (APlayerStart* PlayerStart = Cast<APlayerStart>(StartSpot))
				{
					RestartPlayerAtPlayerStart(PlayerController, PlayerStart);
				}
				else
				{
					RestartPlayer(PlayerController);
				}
			}
		}
		if (PlayerController)
		{
			PlayerController->SetIgnoreMoveInput(false);
			PlayerController->SetIgnoreLookInput(false);
		}
		AssignPlayerNumber(PlayerState, RoundUsedNumbers);
		if (PlayerState->GetSecretNumber() != INDEX_NONE)
		{
			RoundUsedNumbers.Add(PlayerState->GetSecretNumber());
			++LivingCount;
		}
	}
	MatchState->SetLivingPlayerCount(LivingCount);
	if (Mode == EMandaMelaMatchMode::TeamDeathmatch || Mode == EMandaMelaMatchMode::CaptureTheFlag)
	{
		const float Duration = Mode == EMandaMelaMatchMode::TeamDeathmatch ? TeamDeathmatchDuration : CaptureTheFlagDuration;
		MatchState->SetMatchEndServerTime(MatchState->GetServerWorldTimeSeconds() + Duration);
		GetWorldTimerManager().SetTimer(MatchClockTimer, this, &AMandaMelaGameMode::OnMatchClockTick, 0.25f, true);
	}
	else
	{
		MatchState->SetMatchEndServerTime(0.0f);
	}
	StartLootSpawning();
}

void AMandaMelaGameMode::AssignTeamsForMatch()
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState)
	{
		return;
	}
	for (APlayerState* State : MatchState->PlayerArray)
	{
		if (AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State))
		{
			PlayerState->SetTeam(EMandaMelaTeam::None);
		}
	}
	if (MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch)
	{
		return;
	}
	for (APlayerState* State : MatchState->PlayerArray)
	{
		AssignTeamToPlayer(Cast<AMandaMelaPlayerState>(State));
	}
}

void AMandaMelaGameMode::AssignTeamToPlayer(AMandaMelaPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}
	const int32 RedCount = CountTeamPlayers(EMandaMelaTeam::Red, false);
	const int32 BlueCount = CountTeamPlayers(EMandaMelaTeam::Blue, false);
	PlayerState->SetTeam(RedCount <= BlueCount ? EMandaMelaTeam::Red : EMandaMelaTeam::Blue);
}

int32 AMandaMelaGameMode::CountTeamPlayers(EMandaMelaTeam Team, bool bLivingOnly) const
{
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState || Team == EMandaMelaTeam::None)
	{
		return 0;
	}
	int32 Count = 0;
	for (APlayerState* State : MatchState->PlayerArray)
	{
		const AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State);
		if (PlayerState && PlayerState->GetTeam() == Team && (!bLivingOnly || PlayerState->IsAlive()))
		{
			++Count;
		}
	}
	return Count;
}

AActor* AMandaMelaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	const AMandaMelaPlayerState* PlayerState = Player ? Player->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	if (MatchState && MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch && PlayerState && PlayerState->GetTeam() != EMandaMelaTeam::None)
	{
		TArray<APlayerStart*> Starts;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			Starts.Add(*It);
		}
		if (APlayerStart* TeamStart = FindTeamPlayerStart(PlayerState->GetTeam(), Starts))
		{
			return TeamStart;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

APlayerStart* AMandaMelaGameMode::FindTeamPlayerStart(EMandaMelaTeam Team, const TArray<APlayerStart*>& Starts) const
{
	const FName DesiredTag = Team == EMandaMelaTeam::Red ? FName(TEXT("TeamRed")) : FName(TEXT("TeamBlue"));
	APlayerStart* BestStart = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const FVector TargetLocation = Team == EMandaMelaTeam::Red ? RedBaseLocation : BlueBaseLocation;
	bool bHasExplicitTag = false;
	for (APlayerStart* Start : Starts)
	{
		if (Start && Start->PlayerStartTag == DesiredTag)
		{
			bHasExplicitTag = true;
			int32 NearbyPawns = 0;
			for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
			{
				const APawn* Pawn = *PawnIt;
				if (Pawn && Pawn->GetDistanceTo(Start) < 250.0f)
				{
					++NearbyPawns;
				}
			}
			const float Score = NearbyPawns * 10000.0f + FVector::DistSquared(Start->GetActorLocation(), TargetLocation) * 0.001f;
			if (Score < BestScore)
			{
				BestScore = Score;
				BestStart = Start;
			}
		}
	}
	if (bHasExplicitTag)
	{
		return BestStart;
	}

	BestScore = TNumericLimits<float>::Max();
	for (APlayerStart* Start : Starts)
	{
		if (!Start || Start->PlayerStartTag == FName(TEXT("TeamRed")) || Start->PlayerStartTag == FName(TEXT("TeamBlue")))
		{
			continue;
		}
		int32 NearbyPawns = 0;
		for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
		{
			const APawn* Pawn = *PawnIt;
			if (Pawn && Pawn->GetDistanceTo(Start) < 250.0f)
			{
				++NearbyPawns;
			}
		}
		const float Score = NearbyPawns * 10000.0f + FVector::DistSquared(Start->GetActorLocation(), TargetLocation) * 0.001f;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestStart = Start;
		}
	}
	return BestStart;
}

void AMandaMelaGameMode::FindTeamBaseLocations(FVector& OutRedLocation, FVector& OutBlueLocation)
{
	TArray<APlayerStart*> Starts;
	TArray<APlayerStart*> RedStarts;
	TArray<APlayerStart*> BlueStarts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		APlayerStart* Start = *It;
		Starts.Add(Start);
		if (Start->PlayerStartTag == FName(TEXT("TeamRed"))) RedStarts.Add(Start);
		if (Start->PlayerStartTag == FName(TEXT("TeamBlue"))) BlueStarts.Add(Start);
	}
	const auto AverageStartLocations = [](const TArray<APlayerStart*>& TeamStarts)
	{
		FVector Total = FVector::ZeroVector;
		for (const APlayerStart* Start : TeamStarts) Total += Start->GetActorLocation();
		return TeamStarts.IsEmpty() ? FVector::ZeroVector : Total / TeamStarts.Num();
	};
	if (!RedStarts.IsEmpty() && !BlueStarts.IsEmpty())
	{
		OutRedLocation = AverageStartLocations(RedStarts);
		OutBlueLocation = AverageStartLocations(BlueStarts);
		return;
	}
	if (Starts.Num() >= 2)
	{
		APlayerStart* First = nullptr;
		APlayerStart* Second = nullptr;
		float FarthestDistance = -1.0f;
		for (int32 FirstIndex = 0; FirstIndex < Starts.Num(); ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Starts.Num(); ++SecondIndex)
			{
				const float Distance = FVector::DistSquared(Starts[FirstIndex]->GetActorLocation(), Starts[SecondIndex]->GetActorLocation());
				if (Distance > FarthestDistance)
				{
					FarthestDistance = Distance;
					First = Starts[FirstIndex];
					Second = Starts[SecondIndex];
				}
			}
		}
		if (First && Second)
		{
			OutRedLocation = First->GetActorLocation();
			OutBlueLocation = Second->GetActorLocation();
			return;
		}
	}
	if (Starts.Num() == 1)
	{
		OutRedLocation = Starts[0]->GetActorLocation() - FVector(700.0f, 0.0f, 0.0f);
		OutBlueLocation = Starts[0]->GetActorLocation() + FVector(700.0f, 0.0f, 0.0f);
		return;
	}
	OutRedLocation = FVector(-700.0f, 0.0f, 100.0f);
	OutBlueLocation = FVector(700.0f, 0.0f, 100.0f);
}

void AMandaMelaGameMode::SetupTeamFlags()
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState)
	{
		return;
	}
	FVector Center = (RedBaseLocation + BlueBaseLocation) * 0.5f;
	const auto HomeTransform = [&Center](const FVector& Location)
	{
		FVector GroundLocation = Location;
		GroundLocation.Z -= 88.0f;
		return FTransform((Center - GroundLocation).Rotation(), GroundLocation);
	};
	const FTransform RedTransform = HomeTransform(RedBaseLocation);
	const FTransform BlueTransform = HomeTransform(BlueBaseLocation);
	if (!IsValid(RedFlag))
	{
		RedFlag = GetWorld()->SpawnActor<AMandaMelaFlag>(AMandaMelaFlag::StaticClass(), RedTransform);
	}
	if (!IsValid(BlueFlag))
	{
		BlueFlag = GetWorld()->SpawnActor<AMandaMelaFlag>(AMandaMelaFlag::StaticClass(), BlueTransform);
	}
	if (RedFlag) RedFlag->InitializeFlag(EMandaMelaTeam::Red, RedTransform);
	if (BlueFlag) BlueFlag->InitializeFlag(EMandaMelaTeam::Blue, BlueTransform);
	MatchState->SetTeamFlags(RedFlag, BlueFlag);
}

void AMandaMelaGameMode::SetupTeamSpawnPoints()
{
	const FVector Center = (RedBaseLocation + BlueBaseLocation) * 0.5f;
	const auto EnsureTeamStart = [this, &Center](TObjectPtr<APlayerStart>& TeamStart, const FName TeamTag, const FVector& BaseLocation)
	{
		if (!IsValid(TeamStart))
		{
			const FRotator Rotation = (Center - BaseLocation).Rotation();
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TeamStart = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), BaseLocation, Rotation, SpawnParameters);
		}
		if (TeamStart)
		{
			TeamStart->PlayerStartTag = TeamTag;
		}
	};
	EnsureTeamStart(RedTeamStart, FName(TEXT("TeamRed")), RedBaseLocation);
	EnsureTeamStart(BlueTeamStart, FName(TEXT("TeamBlue")), BlueBaseLocation);
}

int32 AMandaMelaGameMode::GenerateUnusedNumber(const TSet<int32>& AlreadyUsedNumbers) const
{
	if (AlreadyUsedNumbers.Num() >= 10000)
	{
		return INDEX_NONE;
	}
	int32 Number = FMath::RandRange(0, 9999);
	while (AlreadyUsedNumbers.Contains(Number))
	{
		Number = (Number + 1) % 10000;
	}
	return Number;
}

void AMandaMelaGameMode::AssignPlayerNumber(AMandaMelaPlayerState* PlayerState, const TSet<int32>& AlreadyUsedNumbers)
{
	if (!PlayerState || !HasAuthority())
	{
		return;
	}
	const int32 Number = GenerateUnusedNumber(AlreadyUsedNumbers);
	if (Number != INDEX_NONE)
	{
		PlayerState->InitializeForRound(Number);
		RefreshPlayerNumberDisplay(PlayerState);
	}
}

void AMandaMelaGameMode::RefreshPlayerNumberDisplay(AMandaMelaPlayerState* PlayerState)
{
	if (PlayerState)
	{
		if (AMandaMelaCharacter* Character = Cast<AMandaMelaCharacter>(PlayerState->GetPawn()))
		{
			Character->ResetForRound(PlayerState->GetSecretNumber());
		}
	}
}

void AMandaMelaGameMode::HandleDebugSelfElimination(AController* Player)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerState* PlayerState = Player ? Player->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	AMandaMelaCharacter* Character = Player ? Cast<AMandaMelaCharacter>(Player->GetPawn()) : nullptr;
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !MatchState
		|| MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch
		|| MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress
		|| !PlayerState || !PlayerState->IsAlive() || !Character)
	{
		return;
	}

	PlayerState->SetAlive(false);
	Character->ApplyEliminatedState();
	Character->MulticastPlayEliminationEffect();
	if (AMandaMelaPlayerController* PlayerController = Cast<AMandaMelaPlayerController>(Player))
	{
		PlayerController->ClientShowDeathNotice(TEXT("TEST ELIMINATION"));
	}
	BeginDeathmatchSpectating(PlayerState);
	UpdateLivingPlayersAndCheckWinner();
}

void AMandaMelaGameMode::BeginDeathmatchSpectating(AMandaMelaPlayerState* EliminatedPlayer)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerController* PlayerController = EliminatedPlayer
		? Cast<AMandaMelaPlayerController>(EliminatedPlayer->GetPlayerController())
		: nullptr;
	if (!MatchState || MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch || !PlayerController)
	{
		return;
	}

	AMandaMelaCharacter* SpectatorTarget = nullptr;
	for (APlayerState* State : MatchState->PlayerArray)
	{
		AMandaMelaPlayerState* Candidate = Cast<AMandaMelaPlayerState>(State);
		if (Candidate && Candidate != EliminatedPlayer && Candidate->IsAlive())
		{
			SpectatorTarget = Cast<AMandaMelaCharacter>(Candidate->GetPawn());
			if (SpectatorTarget)
			{
				break;
			}
		}
	}

	PlayerController->ClientWatchSpectatorTarget(SpectatorTarget);
}

void AMandaMelaGameMode::HandleAttack(AController* Attacker, const FString& Digits)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerState* AttackerState = Attacker ? Attacker->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();

	if (!Attacker || !AttackerState || !AttackerState->IsAlive() || !IsCurrentPlayer(AttackerState) || !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		SendAttackFeedback(Attacker, false, TEXT("You cannot attack right now."));
		return;
	}
	if (Now < AttackerState->GetCooldownEndServerTime())
	{
		SendAttackFeedback(Attacker, false, FString::Printf(TEXT("Cooldown: %d seconds"), FMath::CeilToInt(AttackerState->GetCooldownEndServerTime() - Now)));
		return;
	}
	if (Digits.Len() != 4 || !Digits.IsValidIndex(0) || !Digits.IsNumeric())
	{
		SendAttackFeedback(Attacker, false, TEXT("Enter exactly four digits."));
		return;
	}
	for (TCHAR Character : Digits)
	{
		if (Character < TEXT('0') || Character > TEXT('9'))
		{
			SendAttackFeedback(Attacker, false, TEXT("Enter exactly four digits."));
			return;
		}
	}

	const int32 Guess = FCString::Atoi(*Digits);
	AMandaMelaPlayerState* TargetState = nullptr;
	AMandaMelaCharacter* TargetCharacter = nullptr;
	for (APlayerState* Candidate : MatchState->PlayerArray)
	{
		AMandaMelaPlayerState* CandidateState = Cast<AMandaMelaPlayerState>(Candidate);
		if (!CandidateState || CandidateState == AttackerState || !CandidateState->IsAlive() || CandidateState->GetSecretNumber() != Guess)
		{
			continue;
		}
		if (AMandaMelaCharacter* CandidateCharacter = Cast<AMandaMelaCharacter>(CandidateState->GetPawn()))
		{
			TargetState = CandidateState;
			TargetCharacter = CandidateCharacter;
			break;
		}
	}

	if (TargetState && TargetCharacter)
	{
		const bool bTeamMode = MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch;
		if (bTeamMode && AttackerState->GetTeam() == TargetState->GetTeam())
		{
			SendAttackFeedback(Attacker, false, TEXT("You cannot eliminate a teammate."));
			return;
		}
		DropCarriedFlag(TargetState);
		TargetState->SetAlive(false);
		TargetCharacter->ApplyEliminatedState();
		TargetCharacter->MulticastPlayEliminationEffect();
		if (AMandaMelaPlayerController* TargetController = Cast<AMandaMelaPlayerController>(TargetState->GetPlayerController()))
		{
			TargetController->ClientShowDeathNotice(FString::Printf(TEXT("ELIMINATED BY %s"), *AttackerState->GetPlayerName()));
		}
		// UI feedback for attacker is handled via client RPC (ClientAttackFeedback) above; no direct widget manipulation from GameMode.
		if (MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch)
		{
			BeginDeathmatchSpectating(TargetState);
		}
		AttackerState->AddElimination();
		if (MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch)
		{
			const EMandaMelaTeam Team = AttackerState->GetTeam();
			MatchState->SetTeamScore(Team, MatchState->GetTeamScore(Team) + 1);
		}
		SendAttackFeedback(Attacker, true, FString::Printf(TEXT("You killed %s"), *TargetState->GetPlayerName()));
		for (APlayerState* State : MatchState->PlayerArray)
		{
			AMandaMelaPlayerState* OtherState = Cast<AMandaMelaPlayerState>(State);
			if (OtherState && OtherState != AttackerState && OtherState != TargetState && OtherState->IsParticipating())
			{
				if (AMandaMelaPlayerController* OtherController = Cast<AMandaMelaPlayerController>(OtherState->GetPlayerController()))
				{
					OtherController->ClientGameplayFeedback(TEXT("An opponent was eliminated."));
				}
			}
		}
		UpdateLivingPlayersAndCheckWinner();
		if (MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress && bTeamMode)
		{
			if (MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch
				&& MatchState->GetTeamScore(AttackerState->GetTeam()) >= TeamDeathmatchScoreLimit)
			{
				EndMatchForTeam(AttackerState->GetTeam());
			}
			else
			{
				FTimerHandle RespawnTimer;
				GetWorldTimerManager().SetTimer(RespawnTimer, FTimerDelegate::CreateWeakLambda(this,
					[this, TargetStateWeak = TWeakObjectPtr<AMandaMelaPlayerState>(TargetState)]()
					{
						RespawnTeamPlayer(TargetStateWeak);
					}), TeamRespawnDelay, false);
			}
		}
		return;
	}

	AttackerState->SetCooldownEndServerTime(Now + IncorrectGuessCooldown);
	if (AMandaMelaCharacter* AttackerCharacter = Cast<AMandaMelaCharacter>(Attacker->GetPawn()))
	{
		for (APlayerState* State : MatchState->PlayerArray)
		{
			AMandaMelaPlayerState* NearbyState = Cast<AMandaMelaPlayerState>(State);
			APawn* NearbyPawn = NearbyState ? NearbyState->GetPawn() : nullptr;
			if (NearbyState && NearbyState->IsParticipating() && NearbyPawn && FVector::DistSquared(AttackerCharacter->GetActorLocation(), NearbyPawn->GetActorLocation()) <= FMath::Square(WarningAudibleRange))
			{
				if (AMandaMelaPlayerController* NearbyController = Cast<AMandaMelaPlayerController>(NearbyPawn->GetController()))
				{
					NearbyController->ClientPlayWarning(IncorrectGuessWarningSound, AttackerCharacter->GetActorLocation());
				}
			}
		}
	}
	SendAttackFeedback(Attacker, false, TEXT("Wrong number. Attack cooldown: 3 seconds."));
}

bool AMandaMelaGameMode::IsCurrentPlayer(const APlayerState* PlayerState) const
{
	return GameState && PlayerState && GameState->PlayerArray.Contains(const_cast<APlayerState*>(PlayerState));
}

bool AMandaMelaGameMode::IsVisibleTarget(AController* Attacker, AMandaMelaCharacter* Target, float MaxRange) const
{
	AMandaMelaCharacter* AttackerCharacter = Attacker ? Cast<AMandaMelaCharacter>(Attacker->GetPawn()) : nullptr;
	if (!AttackerCharacter || !Target || AttackerCharacter == Target || AttackerCharacter->GetDistanceTo(Target) > MaxRange)
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Attacker->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MandaMelaTargetVisibility), false, AttackerCharacter);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * MaxRange, ECC_Visibility, QueryParams) && Hit.GetActor() == Target;
}

bool AMandaMelaGameMode::IsPublicNumberVisible(AController* Viewer, AMandaMelaCharacter* Target, float MaxRange) const
{
	if (!Viewer || !Target || !IsVisibleTarget(Viewer, Target, MaxRange))
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector NumberLocation = Target->GetPublicPlayerNumberLocation();
	if (!Target->IsPublicPlayerNumberFacing(ViewLocation)
		|| FVector::DistSquared(ViewLocation, NumberLocation) > FMath::Square(MaxRange))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MandaMelaPublicNumberVisibility), false, Viewer->GetPawn());
	QueryParams.AddIgnoredActor(Target);
	FHitResult Hit;
	const bool bHitOccluder = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, NumberLocation, ECC_Visibility, QueryParams);
	if (bHitOccluder && Hit.GetActor() == Target)
	{
		// Should not happen because Target is ignored, but keep defensive behavior
		return false;
	}
	return !bHitOccluder;
}

void AMandaMelaGameMode::HandlePickup(AMandaMelaPlayerController* Player, AMandaMelaPickup* Pickup)
{
	AMandaMelaPlayerState* PlayerState = Player ? Player->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	AMandaMelaCharacter* Character = Player ? Cast<AMandaMelaCharacter>(Player->GetPawn()) : nullptr;
	if (!PlayerState || !PlayerState->IsAlive() || !GetGameState<AMandaMelaGameState>() || GetGameState<AMandaMelaGameState>()->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		if (Player) Player->ClientGameplayFeedback(TEXT("You cannot pick up items right now."));
		return;
	}
	if (!Pickup || Pickup->IsActorBeingDestroyed() || !Character || Character->GetDistanceTo(Pickup) > InteractionRange)
	{
		Player->ClientGameplayFeedback(TEXT("That item is out of reach."));
		return;
	}
	if (PlayerState->GetInventoryItem(0) != EMandaMelaItemType::None && PlayerState->GetInventoryItem(1) != EMandaMelaItemType::None)
	{
		Player->ClientGameplayFeedback(TEXT("Inventory full. Drop an item first."));
		return;
	}
	const EMandaMelaItemType ItemType = Pickup->GetItemType();
	if (Pickup->TryClaim())
	{
		if (PlayerState->TryAddItem(ItemType))
		{
			const int32 FirstInventorySlot = PlayerState->GetInventoryItem(0) == EMandaMelaItemType::None ? 0 : 1;
			Player->SetSelectedInventorySlotFromServer(FirstInventorySlot);
			Character->MulticastPlayTacticalPickup(ItemType, Pickup->GetActorLocation());
			Player->ClientGameplayFeedback(TEXT("Item picked up."));
			Pickup->Destroy();
		}
		else
		{
			Pickup->ReleaseClaim();
			Player->ClientGameplayFeedback(TEXT("Inventory full. Drop an item first."));
		}
	}
}

void AMandaMelaGameMode::HandleDrop(AMandaMelaPlayerController* Player, int32 Slot)
{
	AMandaMelaPlayerState* PlayerState = Player ? Player->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	AMandaMelaCharacter* Character = Player ? Cast<AMandaMelaCharacter>(Player->GetPawn()) : nullptr;
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!PlayerState || !PlayerState->IsAlive() || !Character || !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress || Slot < 0 || Slot > 1)
	{
		if (Player) Player->ClientGameplayFeedback(TEXT("You cannot drop that item."));
		return;
	}
	const FVector SpawnLocation = Character->GetTacticalItemHandLocation();
	const FTransform SpawnTransform(Character->GetActorRotation(), SpawnLocation);
	AMandaMelaPickup* Pickup = GetWorld()->SpawnActor<AMandaMelaPickup>(AMandaMelaPickup::StaticClass(), SpawnTransform);
	if (!Pickup)
	{
		Player->ClientGameplayFeedback(TEXT("Could not drop item."));
		return;
	}
	EMandaMelaItemType DroppedType = EMandaMelaItemType::None;
	if (!PlayerState->TryRemoveItem(Slot, DroppedType))
	{
		Pickup->Destroy();
		Player->ClientGameplayFeedback(TEXT("That inventory slot is empty."));
		return;
	}
	Pickup->SetItemType(DroppedType);
	Player->RefreshHeldTacticalItem(true);
	FVector TossTarget = Character->GetActorLocation() + Character->GetActorForwardVector() * 240.0f;
	TossTarget.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 25.0f;
	Character->MulticastPlayTacticalThrow(DroppedType, TossTarget, false);
	Pickup->LaunchThrownItem(Character->GetActorForwardVector() * 430.0f + FVector::UpVector * 260.0f);
	Player->ClientGameplayFeedback(TEXT("Item dropped."));
}

void AMandaMelaGameMode::HandleUseItem(AMandaMelaPlayerController* Player, int32 Slot, AMandaMelaCharacter* VisibleTarget)
{
	AMandaMelaPlayerState* PlayerState = Player ? Player->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	AMandaMelaCharacter* Character = Player ? Cast<AMandaMelaCharacter>(Player->GetPawn()) : nullptr;
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!PlayerState || !PlayerState->IsAlive() || !Character || !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress || Slot < 0 || Slot > 1)
	{
		if (Player) Player->ClientGameplayFeedback(TEXT("You cannot use an item right now."));
		return;
	}
	const EMandaMelaItemType ItemType = PlayerState->GetInventoryItem(Slot);
	if (ItemType == EMandaMelaItemType::None)
	{
		Player->ClientGameplayFeedback(TEXT("That inventory slot is empty."));
		return;
	}
	AMandaMelaPlayerState* TargetState = VisibleTarget ? Cast<AMandaMelaPlayerState>(VisibleTarget->GetPlayerState()) : nullptr;
	if (ItemType == EMandaMelaItemType::Camera)
	{
		if (!VisibleTarget || !TargetState)
		{
			UE_LOG(LogTemp, Warning, TEXT("HandleUseItem(Camera): missing target or target playerstate (Player=%s)"), *GetNameSafe(Player));
			Player->ClientGameplayFeedback(TEXT("Aim at a visible opponent to record their number."));
			return;
		}
		if (!TargetState->IsAlive())
		{
			UE_LOG(LogTemp, Warning, TEXT("HandleUseItem(Camera): target not alive (Target=%s, Player=%s)"), *GetNameSafe(VisibleTarget), *GetNameSafe(Player));
			Player->ClientGameplayFeedback(TEXT("Aim at a visible opponent to record their number."));
			return;
		}
		if (!IsCurrentPlayer(TargetState))
		{
			UE_LOG(LogTemp, Warning, TEXT("HandleUseItem(Camera): target not a current player (Target=%s, Player=%s)"), *GetNameSafe(VisibleTarget), *GetNameSafe(Player));
			Player->ClientGameplayFeedback(TEXT("Aim at a visible opponent to record their number."));
			return;
		}
		if (!IsVisibleTarget(Player, VisibleTarget, ItemEffectRange))
		{
			UE_LOG(LogTemp, Warning, TEXT("HandleUseItem(Camera): IsVisibleTarget failed (Target=%s, Player=%s)"), *GetNameSafe(VisibleTarget), *GetNameSafe(Player));
			Player->ClientGameplayFeedback(TEXT("Aim at a visible opponent to record their number."));
			return;
		}
		if (!IsPublicNumberVisible(Player, VisibleTarget, ItemEffectRange))
		{
			UE_LOG(LogTemp, Warning, TEXT("HandleUseItem(Camera): IsPublicNumberVisible failed (Target=%s, Player=%s)"), *GetNameSafe(VisibleTarget), *GetNameSafe(Player));
			Player->ClientGameplayFeedback(TEXT("Aim at a visible opponent to record their number."));
			return;
		}
	}
	if (ItemType == EMandaMelaItemType::SmokeBomb && !SmokeParticleEffect)
	{
		Player->ClientGameplayFeedback(TEXT("Smoke Bomb needs a particle effect assigned in the GameMode."));
		return;
	}
	if (ItemType == EMandaMelaItemType::SoundDecoy && !SoundDecoyEffect)
	{
		Player->ClientGameplayFeedback(TEXT("Sound Decoy needs a sound assigned in the GameMode."));
		return;
	}
	EMandaMelaItemType ConsumedType = EMandaMelaItemType::None;
	if (!PlayerState->TryRemoveItem(Slot, ConsumedType))
	{
		return;
	}
	Player->RefreshHeldTacticalItem(true);
	// ponytail: use the existing animated ground target; add a collision-backed projectile only when level geometry needs it.
	const float ThrowDistance = ConsumedType == EMandaMelaItemType::Flashbang ? FlashRadius + 100.0f : 240.0f;
	FVector ThrowTarget = Character->GetActorLocation() + Character->GetActorForwardVector() * ThrowDistance;
	ThrowTarget.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 25.0f;
	const bool bAnimatedTacticalThrow = ConsumedType == EMandaMelaItemType::Flashbang || ConsumedType == EMandaMelaItemType::SmokeBomb;
	if (bAnimatedTacticalThrow)
	{
		Character->MulticastPlayTacticalThrow(ConsumedType, ThrowTarget, true);
	}

	switch (ConsumedType)
	{
	case EMandaMelaItemType::Binoculars:
		Player->ClientUseBinoculars(4.0f);
		Player->ClientGameplayFeedback(TEXT("Binoculars used."));
		break;
	case EMandaMelaItemType::Camera:
		Player->ClientStoreCameraRecord(VisibleTarget->GetPublicPlayerNumber());
		Player->ClientGameplayFeedback(TEXT("Number recorded."));
		break;
	case EMandaMelaItemType::Flashbang:
		{
			const TWeakObjectPtr<AMandaMelaPlayerState> WeakThrowerState = PlayerState;
			const TWeakObjectPtr<AMandaMelaCharacter> WeakThrowerCharacter = Character;
			FTimerHandle FlashFuseTimer;
			GetWorldTimerManager().SetTimer(FlashFuseTimer, FTimerDelegate::CreateWeakLambda(this,
				[this, WeakThrowerState, WeakThrowerCharacter, ThrowTarget]()
				{
					DetonateFlashbang(WeakThrowerState, WeakThrowerCharacter, ThrowTarget);
				}), FlashFuseTime, false);
		}
		Player->ClientGameplayFeedback(TEXT("Flashbang used."));
		break;
	case EMandaMelaItemType::SmokeBomb:
	{
		if (AMandaMelaSmokeCloud* Smoke = GetWorld()->SpawnActor<AMandaMelaSmokeCloud>(AMandaMelaSmokeCloud::StaticClass(), ThrowTarget, FRotator::ZeroRotator))
		{
			Smoke->Initialize(SmokeParticleEffect, 6.0f);
		}
		Player->ClientGameplayFeedback(TEXT("Smoke bomb used."));
		break;
	}
	case EMandaMelaItemType::SoundDecoy:
	{
		const FVector Location = Character->GetActorLocation() + Character->GetActorForwardVector() * 180.0f;
		if (AMandaMelaSoundDecoy* Decoy = GetWorld()->SpawnActor<AMandaMelaSoundDecoy>(AMandaMelaSoundDecoy::StaticClass(), Location, FRotator::ZeroRotator))
		{
			Decoy->Initialize(SoundDecoyEffect, 6.0f);
		}
		Player->ClientGameplayFeedback(TEXT("Sound decoy used."));
		break;
	}
	default:
		break;
	}
}

void AMandaMelaGameMode::DetonateFlashbang(TWeakObjectPtr<AMandaMelaPlayerState> ThrowerState, TWeakObjectPtr<AMandaMelaCharacter> ThrowerCharacter, const FVector& FlashLocation)
{
	AMandaMelaGameState* CurrentMatchState = GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerState* CurrentThrowerState = ThrowerState.Get();
	if (!CurrentMatchState || !CurrentThrowerState)
	{
		return;
	}

	for (APlayerState* State : CurrentMatchState->PlayerArray)
	{
		AMandaMelaPlayerState* OtherState = Cast<AMandaMelaPlayerState>(State);
		APawn* OtherPawn = OtherState ? OtherState->GetPawn() : nullptr;
		if (!OtherState || !OtherState->IsAlive() || !OtherPawn
			|| FVector::DistSquared(FlashLocation, OtherPawn->GetActorLocation()) > FMath::Square(FlashRadius))
		{
			continue;
		}

		if (OtherState != CurrentThrowerState && CurrentMatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch
			&& OtherState->GetTeam() == CurrentThrowerState->GetTeam())
		{
			continue;
		}

		AMandaMelaPlayerController* OtherController = Cast<AMandaMelaPlayerController>(OtherPawn->GetController());
		if (!OtherController)
		{
			continue;
		}

		FVector ViewLocation;
		FRotator ViewRotation;
		OtherController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector DirectionToFlash = (FlashLocation - ViewLocation).GetSafeNormal();
		const float ViewDot = FVector::DotProduct(ViewRotation.Vector(), DirectionToFlash);
		if (ViewDot <= 0.0f)
		{
			continue;
		}

		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MandaMelaFlashbangLineOfSight), false, ThrowerCharacter.Get());
		if (GetWorld()->LineTraceSingleByChannel(Hit, FlashLocation, ViewLocation, ECC_Visibility, QueryParams)
			&& Hit.GetActor() != OtherPawn)
		{
			continue;
		}

		const float Distance = FVector::Distance(FlashLocation, OtherPawn->GetActorLocation());
		const float FullIntensityRadius = FlashRadius * 0.4f;
		const float DistanceAlpha = FMath::Clamp((Distance - FullIntensityRadius) / (FlashRadius - FullIntensityRadius), 0.0f, 1.0f);
		const float DistanceFactor = FMath::Lerp(1.0f, 0.4f, DistanceAlpha);
		const float ViewFactor = FMath::Lerp(0.5f, 1.0f, ViewDot);
		const float Intensity = FMath::Clamp(DistanceFactor * ViewFactor, 0.0f, 1.0f);
		const float Duration = FMath::Max(0.8f, 3.0f * Intensity);
		OtherController->ClientApplyFlash(Intensity, Duration);
	}
}

AMandaMelaFlag* AMandaMelaGameMode::FindFlagCarriedBy(const AMandaMelaPlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return nullptr;
	}
	AMandaMelaFlag* Flags[] = { RedFlag.Get(), BlueFlag.Get() };
	for (AMandaMelaFlag* Flag : Flags)
	{
		if (Flag && Flag->GetFlagStatus() == EMandaMelaFlagStatus::Carried
			&& Flag->GetCarrier() && Flag->GetCarrier()->GetPlayerState<AMandaMelaPlayerState>() == PlayerState)
		{
			return Flag;
		}
	}
	return nullptr;
}

void AMandaMelaGameMode::DropCarriedFlag(AMandaMelaPlayerState* PlayerState)
{
	if (AMandaMelaFlag* Flag = FindFlagCarriedBy(PlayerState))
	{
		const FVector DropLocation = PlayerState->GetPawn() ? PlayerState->GetPawn()->GetActorLocation() : Flag->GetActorLocation();
		Flag->DropAt(DropLocation);
	}
}

void AMandaMelaGameMode::HandleFlagTouch(AMandaMelaFlag* TouchedFlag, AMandaMelaCharacter* Character)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	AMandaMelaPlayerState* PlayerState = Character ? Character->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	if (!HasAuthority() || !TouchedFlag || !Character || !PlayerState || !PlayerState->IsAlive() || !MatchState
		|| MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress
		|| MatchState->GetMatchMode() != EMandaMelaMatchMode::CaptureTheFlag
		|| TouchedFlag->GetTeam() == EMandaMelaTeam::None)
	{
		return;
	}
	const EMandaMelaTeam PlayerTeam = PlayerState->GetTeam();
	if (PlayerTeam == EMandaMelaTeam::None)
	{
		return;
	}

	if (TouchedFlag->GetTeam() == PlayerTeam)
	{
		if (TouchedFlag->GetFlagStatus() == EMandaMelaFlagStatus::Dropped)
		{
			TouchedFlag->ReturnHome();
			if (AMandaMelaPlayerController* Controller = Cast<AMandaMelaPlayerController>(Character->GetController()))
			{
				Controller->ClientGameplayFeedback(TEXT("Your flag returned."));
			}
			if (AMandaMelaFlag* CarriedEnemyFlag = FindFlagCarriedBy(PlayerState); CarriedEnemyFlag && CarriedEnemyFlag->GetTeam() != PlayerTeam)
			{
				HandleFlagTouch(TouchedFlag, Character);
			}
			return;
		}
		if (TouchedFlag->GetFlagStatus() != EMandaMelaFlagStatus::Home)
		{
			return;
		}
		AMandaMelaFlag* CarriedEnemyFlag = FindFlagCarriedBy(PlayerState);
		if (!CarriedEnemyFlag || CarriedEnemyFlag->GetTeam() == PlayerTeam)
		{
			return;
		}

		MatchState->AddTeamCapture(PlayerTeam);
		PlayerState->AddCapture();
		if (RedFlag) RedFlag->ReturnHome();
		if (BlueFlag) BlueFlag->ReturnHome();
		for (APlayerState* State : MatchState->PlayerArray)
		{
			if (const AMandaMelaPlayerState* Teammate = Cast<AMandaMelaPlayerState>(State); Teammate && Teammate->GetTeam() == PlayerTeam)
			{
				if (AMandaMelaPlayerController* Controller = Cast<AMandaMelaPlayerController>(Teammate->GetPlayerController()))
				{
					Controller->ClientGameplayFeedback(TEXT("Flag captured!"));
				}
			}
		}
		if (MatchState->GetCaptureCount(PlayerTeam) >= CaptureTheFlagScoreLimit)
		{
			EndMatchForTeam(PlayerTeam);
		}
		return;
	}

	if (TouchedFlag->GetFlagStatus() == EMandaMelaFlagStatus::Carried)
	{
		return;
	}
	if (FindFlagCarriedBy(PlayerState))
	{
		if (AMandaMelaPlayerController* Controller = Cast<AMandaMelaPlayerController>(Character->GetController()))
		{
			Controller->ClientGameplayFeedback(TEXT("You can carry one flag at a time."));
		}
		return;
	}
	TouchedFlag->SetCarriedBy(Character);
	if (AMandaMelaPlayerController* Controller = Cast<AMandaMelaPlayerController>(Character->GetController()))
	{
		Controller->ClientGameplayFeedback(TEXT("Enemy flag taken. Bring it to your home flag."));
	}
}

void AMandaMelaGameMode::RespawnTeamPlayer(TWeakObjectPtr<AMandaMelaPlayerState> WeakPlayerState)
{
	AMandaMelaPlayerState* PlayerState = WeakPlayerState.Get();
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!PlayerState || !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress
		|| MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch || PlayerState->IsAlive()
		|| !MatchState->PlayerArray.Contains(PlayerState))
	{
		return;
	}
	TSet<int32> LivingNumbers;
	for (APlayerState* State : MatchState->PlayerArray)
	{
		if (const AMandaMelaPlayerState* Existing = Cast<AMandaMelaPlayerState>(State); Existing && Existing != PlayerState && Existing->IsAlive())
		{
			LivingNumbers.Add(Existing->GetSecretNumber());
		}
	}
	LivingNumbers.Add(PlayerState->GetSecretNumber());
	const int32 Number = GenerateUnusedNumber(LivingNumbers);
	if (Number == INDEX_NONE)
	{
		return;
	}
	AController* Controller = Cast<AController>(PlayerState->GetOwner());
	if (!Controller)
	{
		return;
	}
	if (APawn* EliminatedPawn = Controller->GetPawn())
	{
		Controller->UnPossess();
		EliminatedPawn->Destroy();
	}
	PlayerState->InitializeForRound(Number);
	RoundUsedNumbers = LivingNumbers;
	RoundUsedNumbers.Add(Number);
	APlayerStart* StartSpot = Cast<APlayerStart>(ChoosePlayerStart(Controller));
	if (StartSpot)
	{
		RestartPlayerAtPlayerStart(Controller, StartSpot);
	}
	else
	{
		RestartPlayer(Controller);
	}
	Controller->SetIgnoreMoveInput(false);
	Controller->SetIgnoreLookInput(false);
	UpdateLivingPlayersAndCheckWinner();
}

void AMandaMelaGameMode::UpdateLivingPlayersAndCheckWinner(APlayerState* ExcludedPlayer)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	int32 LivingCount = 0;
	AMandaMelaPlayerState* LastLivingPlayer = nullptr;
	for (APlayerState* State : MatchState->PlayerArray)
	{
		if (State != ExcludedPlayer)
		{
			if (AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State); PlayerState && PlayerState->IsAlive())
			{
				++LivingCount;
				LastLivingPlayer = PlayerState;
			}
		}
	}
	MatchState->SetLivingPlayerCount(LivingCount);
	if (MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch)
	{
		UpdateTeamPopulationResult();
		return;
	}
	if (LivingCount <= 1)
	{
		MatchState->SetResult(LivingCount == 1 ? LastLivingPlayer : nullptr, LivingCount == 0);
		GetWorldTimerManager().ClearTimer(MatchClockTimer);
		StopLootSpawning();
		for (APlayerState* State : MatchState->PlayerArray)
		{
			if (AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State))
			{
				if (AMandaMelaCharacter* Character = Cast<AMandaMelaCharacter>(PlayerState->GetPawn()))
				{
					Character->GetCharacterMovement()->DisableMovement();
				}
			}
		}
	}
}

void AMandaMelaGameMode::UpdateTeamPopulationResult()
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	const int32 RedPlayers = CountTeamPlayers(EMandaMelaTeam::Red, false);
	const int32 BluePlayers = CountTeamPlayers(EMandaMelaTeam::Blue, false);
	if (RedPlayers == 0 && BluePlayers == 0)
	{
		MatchState->SetResult(nullptr, true);
		GetWorldTimerManager().ClearTimer(MatchClockTimer);
		StopLootSpawning();
	}
	else if (RedPlayers == 0)
	{
		EndMatchForTeam(EMandaMelaTeam::Blue);
	}
	else if (BluePlayers == 0)
	{
		EndMatchForTeam(EMandaMelaTeam::Red);
	}
}

void AMandaMelaGameMode::EndMatchForTeam(EMandaMelaTeam WinningTeam)
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress || WinningTeam == EMandaMelaTeam::None)
	{
		return;
	}
	MatchState->SetResult(nullptr, false, WinningTeam);
	if (MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch)
	{
		const TCHAR* WinningTeamName = WinningTeam == EMandaMelaTeam::Red ? TEXT("RED") : TEXT("BLUE");
		const int32 RedScore = MatchState->GetTeamScore(EMandaMelaTeam::Red);
		const int32 BlueScore = MatchState->GetTeamScore(EMandaMelaTeam::Blue);
		for (APlayerState* State : MatchState->PlayerArray)
		{
			AMandaMelaPlayerState* TeamPlayer = Cast<AMandaMelaPlayerState>(State);
			AMandaMelaPlayerController* PlayerController = TeamPlayer
				? Cast<AMandaMelaPlayerController>(TeamPlayer->GetPlayerController())
				: nullptr;
			if (PlayerController)
			{
				const bool bWon = TeamPlayer->GetTeam() == WinningTeam;
				PlayerController->ClientGameplayFeedback(FString::Printf(
					TEXT("%s! %s TEAM WINS  |  RED %d - %d BLUE"), bWon ? TEXT("VICTORY") : TEXT("DEFEAT"),
					WinningTeamName, RedScore, BlueScore));
			}
		}
	}
	GetWorldTimerManager().ClearTimer(MatchClockTimer);
	StopLootSpawning();
}

void AMandaMelaGameMode::HandleTeamTimeout()
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress
		|| MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch)
	{
		return;
	}
	const bool bTdm = MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch;
	const int32 RedScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Red) : MatchState->GetCaptureCount(EMandaMelaTeam::Red);
	const int32 BlueScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Blue) : MatchState->GetCaptureCount(EMandaMelaTeam::Blue);
	if (RedScore == BlueScore)
	{
		MatchState->SetResult(nullptr, true);
		GetWorldTimerManager().ClearTimer(MatchClockTimer);
		StopLootSpawning();
	}
	else
	{
		EndMatchForTeam(RedScore > BlueScore ? EMandaMelaTeam::Red : EMandaMelaTeam::Blue);
	}
}

void AMandaMelaGameMode::OnMatchClockTick()
{
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (MatchState && MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress
		&& MatchState->GetMatchEndServerTime() > 0.0f
		&& MatchState->GetServerWorldTimeSeconds() >= MatchState->GetMatchEndServerTime())
	{
		HandleTeamTimeout();
	}
}

void AMandaMelaGameMode::StartNewRound()
{
	AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!HasAuthority() || !MatchState || MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress || MatchState->PlayerArray.Num() < MinPlayers)
	{
		return;
	}
	StartMatchInternal(MatchState->GetMatchMode());
}

void AMandaMelaGameMode::StartLootSpawning()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(LootSpawnTimer);
	for (TActorIterator<AMandaMelaPickup> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	const int32 MaxItems = FMath::Max(1, MaxWorldLootCount);
	for (int32 Index = 0; Index < FMath::Min(InitialLootCount, MaxItems); ++Index)
	{
		SpawnRandomLoot();
	}

	GetWorldTimerManager().SetTimer(LootSpawnTimer, this, &AMandaMelaGameMode::SpawnRandomLoot,
		FMath::Max(2.0f, LootSpawnInterval), true);
}

void AMandaMelaGameMode::StopLootSpawning()
{
	GetWorldTimerManager().ClearTimer(LootSpawnTimer);
}

void AMandaMelaGameMode::SpawnRandomLoot()
{
	const AMandaMelaGameState* MatchState = GetGameState<AMandaMelaGameState>();
	if (!HasAuthority() || !GetWorld() || !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		StopLootSpawning();
		return;
	}

	int32 ExistingItems = 0;
	for (TActorIterator<AMandaMelaPickup> It(GetWorld()); It; ++It)
	{
		if (It->GetItemType() != EMandaMelaItemType::None)
		{
			++ExistingItems;
		}
	}
	if (ExistingItems >= FMath::Max(1, MaxWorldLootCount))
	{
		return;
	}

	FVector SpawnLocation;
	if (!FindRandomLootLocation(SpawnLocation))
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMandaMelaPickup* Pickup = GetWorld()->SpawnActor<AMandaMelaPickup>(AMandaMelaPickup::StaticClass(),
		SpawnLocation, FRotator::ZeroRotator, SpawnParameters))
	{
		const int32 FirstItem = static_cast<int32>(EMandaMelaItemType::Binoculars);
		const int32 LastItem = static_cast<int32>(EMandaMelaItemType::SoundDecoy);
		Pickup->SetItemType(static_cast<EMandaMelaItemType>(FMath::RandRange(FirstItem, LastItem)));
	}
}

bool AMandaMelaGameMode::FindRandomLootLocation(FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	TArray<FVector> SpawnOrigins;
	TArray<FVector> BotSpawnOrigins;
	if (const AMandaMelaGameState* MatchState = World->GetGameState<AMandaMelaGameState>())
	{
		for (APlayerState* State : MatchState->PlayerArray)
		{
			const AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State);
			const APawn* Pawn = PlayerState && PlayerState->IsAlive() ? PlayerState->GetPawn() : nullptr;
			if (Pawn)
			{
				if (PlayerState->IsABot())
				{
					BotSpawnOrigins.Add(Pawn->GetActorLocation());
				}
				else
				{
					SpawnOrigins.Add(Pawn->GetActorLocation());
				}
			}
		}
	}
	if (SpawnOrigins.IsEmpty())
	{
		SpawnOrigins = MoveTemp(BotSpawnOrigins);
	}
	if (SpawnOrigins.IsEmpty())
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			SpawnOrigins.Add(It->GetActorLocation());
		}
	}
	if (SpawnOrigins.IsEmpty() && World->GetWorldSettings())
	{
		SpawnOrigins.Add(FVector::ZeroVector);
	}
	if (SpawnOrigins.IsEmpty())
	{
		return false;
	}

	const float Radius = FMath::Max(200.0f, LootSpawnRadius);
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		const FVector Origin = SpawnOrigins[FMath::RandRange(0, SpawnOrigins.Num() - 1)];
		FVector Candidate;
		bool bFoundLocation = false;
		if (Navigation)
		{
			FNavLocation NavLocation;
			if (Navigation->GetRandomReachablePointInRadius(Origin, Radius, NavLocation))
			{
				Candidate = NavLocation.Location;
				bFoundLocation = true;
			}
		}

		if (!bFoundLocation)
		{
			FVector Direction = FMath::VRand();
			Direction.Z = 0.0f;
			Direction = Direction.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				Direction = FVector::ForwardVector;
			}
			const FVector Probe = Origin + Direction * FMath::FRandRange(150.0f, Radius);
			FHitResult FloorHit;
			const FVector TraceStart = Probe + FVector::UpVector * 1500.0f;
			const FVector TraceEnd = Probe - FVector::UpVector * 5000.0f;
			if (World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_WorldStatic)
				&& FloorHit.ImpactNormal.Z > 0.5f)
			{
				Candidate = FloorHit.ImpactPoint;
				bFoundLocation = true;
			}
		}
		if (!bFoundLocation)
		{
			continue;
		}

		bool bTooClose = false;
		if (const AMandaMelaGameState* MatchState = World->GetGameState<AMandaMelaGameState>())
		{
			for (APlayerState* State : MatchState->PlayerArray)
			{
				const AMandaMelaPlayerState* PlayerState = Cast<AMandaMelaPlayerState>(State);
				const APawn* Pawn = PlayerState && PlayerState->IsAlive() ? PlayerState->GetPawn() : nullptr;
				if (Pawn && FVector::DistSquared2D(Candidate, Pawn->GetActorLocation()) < FMath::Square(250.0f))
				{
					bTooClose = true;
					break;
				}
			}
		}
		if (bTooClose)
		{
			continue;
		}
		for (TActorIterator<AMandaMelaPickup> It(World); It; ++It)
		{
			if (FVector::DistSquared2D(Candidate, It->GetActorLocation()) < FMath::Square(150.0f))
			{
				bTooClose = true;
				break;
			}
		}
		if (!bTooClose)
		{
			OutLocation = Candidate + FVector::UpVector * 25.0f;
			return true;
		}
	}
	return false;
}
