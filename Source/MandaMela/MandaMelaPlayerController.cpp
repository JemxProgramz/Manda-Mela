// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaPlayerController.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaGameMode.h"
#include "MandaMelaGameplayWidget.h"
#include "MandaMelaFlag.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaPickup.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TimerManager.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
	const TCHAR* GetInventoryItemName(EMandaMelaItemType Item)
	{
		switch (Item)
		{
		case EMandaMelaItemType::Binoculars: return TEXT("Binoculars");
		case EMandaMelaItemType::Camera: return TEXT("Camera");
		case EMandaMelaItemType::Flashbang: return TEXT("Flashbang");
		case EMandaMelaItemType::SmokeBomb: return TEXT("Smoke Bomb");
		case EMandaMelaItemType::SoundDecoy: return TEXT("Sound Decoy");
		default: return TEXT("Empty");
		}
	}
}

AMandaMelaPlayerController::AMandaMelaPlayerController()
{
	GameplayWidgetClass = UMandaMelaGameplayWidget::StaticClass();
}

void AMandaMelaPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!GameplayWidgetClass)
	{
		GameplayWidgetClass = UMandaMelaGameplayWidget::StaticClass();
	}
	UE_LOG(LogTemp, Display, TEXT("MandaMela PC BeginPlay: local=%d widgetClass=%s"), IsLocalController(), *GetNameSafe(GameplayWidgetClass));

	if (IsLocalController())
	{
		if (ShouldUseTouchControls())
		{
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
			if (MobileControlsWidget)
			{
				MobileControlsWidget->AddToPlayerScreen(0);
			}
		}

		if (GameplayWidgetClass)
		{
			GameplayWidget = CreateWidget<UMandaMelaGameplayWidget>(this, GameplayWidgetClass);
			if (GameplayWidget)
			{
				const bool bAddedToPlayerScreen = GameplayWidget->AddToPlayerScreen(10);
				if (!bAddedToPlayerScreen)
				{
					GameplayWidget->AddToViewport(10);
				}
				UE_LOG(LogTemp, Display, TEXT("MandaMela HUD widget added: playerScreen=%d viewportFallback=%d"), bAddedToPlayerScreen, !bAddedToPlayerScreen);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("MandaMela failed to create its HUD widget."));
			}
		}
		GetWorldTimerManager().SetTimer(HudRefreshTimer, this, &AMandaMelaPlayerController::RefreshHud, 0.25f, true);
	}
}

void AMandaMelaPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreCharacterAfterCamera();
	RestoreBinoculars();
	ClearFlashEffect();
	GetWorldTimerManager().ClearTimer(HudRefreshTimer);
	GetWorldTimerManager().ClearTimer(BinocularTimer);
	GetWorldTimerManager().ClearTimer(FeedbackTimer);
	GetWorldTimerManager().ClearTimer(DeathNoticeTimer);
	if (IsLocalController() && RuntimeMappingContext)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Subsystem->RemoveMappingContext(RuntimeMappingContext);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMandaMelaPlayerController::OnPossess(APawn* InPawn)
{
	if (bCameraModeActive) CloseCameraMode();
	if (bCameraPhotoEntryActive) CloseCameraPhoto();
	RestoreCharacterAfterCamera();
	RestoreBinoculars();
	ClearFlashEffect();
	Super::OnPossess(InPawn);
}

void AMandaMelaPlayerController::OnUnPossess()
{
	if (bCameraModeActive) CloseCameraMode();
	if (bCameraPhotoEntryActive) CloseCameraPhoto();
	RestoreCharacterAfterCamera();
	RestoreBinoculars();
	ClearFlashEffect();
	Super::OnUnPossess();
}

void AMandaMelaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
		if (!ShouldUseTouchControls())
		{
			for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
	SetupGameplayInput();
}

bool AMandaMelaPlayerController::ShouldUseTouchControls() const
{
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

UInputAction* AMandaMelaPlayerController::AddRuntimeAction(FName Name, const TArray<FKey>& Keys)
{
	if (!RuntimeMappingContext)
	{
		return nullptr;
	}
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	if (!Action)
	{
		return nullptr;
	}
	Action->ValueType = EInputActionValueType::Boolean;
	for (const FKey& Key : Keys)
	{
		RuntimeMappingContext->MapKey(Action, Key);
	}
	RuntimeInputActions.Add(Action);
	return Action;
}

void AMandaMelaPlayerController::SetupGameplayInput()
{
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!EnhancedInput || !Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("MandaMela gameplay input requires the project's Enhanced Input component and local player subsystem."));
		return;
	}

	RuntimeMappingContext = NewObject<UInputMappingContext>(this, TEXT("MandaMelaGameplayMapping"));
	const auto BindNoArg = [this, EnhancedInput](FName Name, const TArray<FKey>& Keys, auto Function)
	{
		if (UInputAction* Action = AddRuntimeAction(Name, Keys))
		{
			EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, Function);
		}
	};

	if (UInputAction* ScoreboardAction = AddRuntimeAction(TEXT("Scoreboard"), { EKeys::Tab }))
	{
		EnhancedInput->BindAction(ScoreboardAction, ETriggerEvent::Started, this, &AMandaMelaPlayerController::InputScoreboardPressed);
		EnhancedInput->BindAction(ScoreboardAction, ETriggerEvent::Completed, this, &AMandaMelaPlayerController::InputScoreboardReleased);
	}
	BindNoArg(TEXT("GuessCancel"), { EKeys::Escape }, &AMandaMelaPlayerController::InputCancel);
	// Windows reports the keypad Enter key through the same EKeys::Enter binding.
	BindNoArg(TEXT("GuessSubmit"), { EKeys::Enter }, &AMandaMelaPlayerController::InputSubmit);
	BindNoArg(TEXT("GuessBackspace"), { EKeys::BackSpace }, &AMandaMelaPlayerController::InputBackspace);
	BindNoArg(TEXT("GuessClear"), { EKeys::Delete }, &AMandaMelaPlayerController::InputClear);
	BindNoArg(TEXT("ModeDeathmatch"), { EKeys::Q }, &AMandaMelaPlayerController::InputSelectDeathmatch);
	BindNoArg(TEXT("ModeTeamDeathmatch"), { EKeys::R }, &AMandaMelaPlayerController::InputSelectTeamDeathmatch);
	BindNoArg(TEXT("ModeCaptureTheFlag"), { EKeys::T }, &AMandaMelaPlayerController::InputSelectCaptureTheFlag);
	BindNoArg(TEXT("Interact"), { EKeys::E }, &AMandaMelaPlayerController::InputInteract);
	BindNoArg(TEXT("CycleSlotUp"), { EKeys::MouseScrollUp }, &AMandaMelaPlayerController::InputCycleSlot);
	BindNoArg(TEXT("CycleSlotDown"), { EKeys::MouseScrollDown }, &AMandaMelaPlayerController::InputCycleSlot);
	BindNoArg(TEXT("UseSelectedSlot"), { EKeys::F }, &AMandaMelaPlayerController::InputUseSelectedSlot);
	if (UInputAction* CameraHoldAction = AddRuntimeAction(TEXT("UseCameraHold"), { EKeys::RightMouseButton }))
	{
		EnhancedInput->BindAction(CameraHoldAction, ETriggerEvent::Started, this, &AMandaMelaPlayerController::InputCameraHoldPressed);
		EnhancedInput->BindAction(CameraHoldAction, ETriggerEvent::Completed, this, &AMandaMelaPlayerController::InputCameraHoldReleased);
		EnhancedInput->BindAction(CameraHoldAction, ETriggerEvent::Canceled, this, &AMandaMelaPlayerController::InputCameraHoldReleased);
	}
	BindNoArg(TEXT("CaptureCameraPhoto"), { EKeys::LeftMouseButton }, &AMandaMelaPlayerController::InputCapturePhoto);
	BindNoArg(TEXT("DropItem"), { EKeys::G }, &AMandaMelaPlayerController::InputDropItem);
	BindNoArg(TEXT("SpectatorPrevious"), { EKeys::PageUp }, &AMandaMelaPlayerController::InputSpectatorPrevious);
	BindNoArg(TEXT("SpectatorNext"), { EKeys::PageDown }, &AMandaMelaPlayerController::InputSpectatorNext);

	const TArray<TArray<FKey>> DigitKeys = {
		{ EKeys::Zero, EKeys::NumPadZero }, { EKeys::One, EKeys::NumPadOne }, { EKeys::Two, EKeys::NumPadTwo },
		{ EKeys::Three, EKeys::NumPadThree }, { EKeys::Four, EKeys::NumPadFour }, { EKeys::Five, EKeys::NumPadFive },
		{ EKeys::Six, EKeys::NumPadSix }, { EKeys::Seven, EKeys::NumPadSeven }, { EKeys::Eight, EKeys::NumPadEight },
		{ EKeys::Nine, EKeys::NumPadNine }
	};
	for (int32 Digit = 0; Digit <= 9; ++Digit)
	{
		if (UInputAction* Action = AddRuntimeAction(FName(*FString::Printf(TEXT("TabletDigit%d"), Digit)), DigitKeys[Digit]))
		{
			EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, &AMandaMelaPlayerController::InputDigit, Digit);
		}
	}
	Subsystem->AddMappingContext(RuntimeMappingContext, 10);
}

void AMandaMelaPlayerController::AddGuessDigit(int32 Digit)
{
	if (bCameraModeActive) return;
	if (bCameraPhotoEntryActive)
	{
		if (!bCameraPhotoTargetAvailable || bCameraPhotoAnswerPending || Digit < 0 || Digit > 9 || GuessDigits.Len() >= 4) return;
		GuessDigits.AppendChar(static_cast<TCHAR>('0' + Digit));
		if (GameplayWidget) GameplayWidget->SetGuessText(GuessDigits);
		return;
	}
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
	if (bPauseMenuOpen || Digit < 0 || Digit > 9 || GuessDigits.Len() >= 4 || bSubmissionPending
		|| !MatchPlayerState || !MatchPlayerState->IsAlive()
		|| !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	GuessDigits.AppendChar(static_cast<TCHAR>('0' + Digit));
	if (GameplayWidget) GameplayWidget->SetGuessText(GuessDigits);
}

void AMandaMelaPlayerController::BackspaceGuess()
{
	if (!bCameraModeActive && !bCameraPhotoAnswerPending && !bSubmissionPending && !GuessDigits.IsEmpty())
	{
		GuessDigits.LeftChopInline(1);
		if (GameplayWidget) GameplayWidget->SetGuessText(GuessDigits);
	}
}

void AMandaMelaPlayerController::ClearGuess()
{
	if (bCameraModeActive || bCameraPhotoAnswerPending) return;
	GuessDigits.Reset();
	if (GameplayWidget) GameplayWidget->SetGuessText(GuessDigits);
}

void AMandaMelaPlayerController::SubmitGuess()
{
	if (bCameraModeActive) return;
	if (bCameraPhotoEntryActive)
	{
		if (!bCameraPhotoTargetAvailable || bCameraPhotoAnswerPending) return;
		if (GuessDigits.Len() != 4)
		{
			if (GameplayWidget) GameplayWidget->SetCameraPhotoStatus(FText::FromString(TEXT("Enter exactly four digits.")));
			return;
		}
		bCameraPhotoAnswerPending = true;
		if (GameplayWidget) GameplayWidget->SetCameraPhotoStatus(FText::FromString(TEXT("Checking number...")));
		ServerSubmitCameraPhotoAnswer(GuessDigits);
		return;
	}
	if (bPauseMenuOpen)
	{
		return;
	}
	const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
	if (MatchState && MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		RequestStartMatch();
		return;
	}
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	if (bSubmissionPending || !MatchPlayerState || !MatchPlayerState->IsAlive()
		|| !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		return;
	}
	if (GuessDigits.Len() != 4)
	{
		LastFeedback = TEXT("Enter exactly four digits.");
		if (GameplayWidget) GameplayWidget->SetFeedback(FText::FromString(LastFeedback));
		return;
	}
	for (TCHAR Digit : GuessDigits)
	{
		if (Digit < TEXT('0') || Digit > TEXT('9'))
		{
			LastFeedback = TEXT("Enter exactly four digits.");
			if (GameplayWidget) GameplayWidget->SetFeedback(FText::FromString(LastFeedback));
			return;
		}
	}
	bSubmissionPending = true;
	LastFeedback = TEXT("Submitting...");
	if (GameplayWidget) GameplayWidget->SetFeedback(FText::FromString(LastFeedback));
	ServerSubmitGuess(GuessDigits);
}

void AMandaMelaPlayerController::MM_Suicide()
{
	if (GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogTemp, Warning, TEXT("MM_Suicide is available only in a standalone offline match."));
		return;
	}
	ServerDebugSelfEliminate();
}

void AMandaMelaPlayerController::MM_GiveCamera()
{
	if (GetNetMode() != NM_Standalone)
	{
		ClientGameplayFeedback(TEXT("MM_GiveCamera is available only in standalone testing."));
		return;
	}

	AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	const int32 EmptySlot = MatchPlayerState && MatchPlayerState->GetInventoryItem(0) != EMandaMelaItemType::None ? 1 : 0;
	if (!MatchPlayerState || MatchPlayerState->GetInventoryItem(EmptySlot) != EMandaMelaItemType::None || !MatchPlayerState->TryAddItem(EMandaMelaItemType::Camera))
	{
		ClientGameplayFeedback(TEXT("Could not add Camera; player must be alive with an empty slot."));
		return;
	}

	SetSelectedInventorySlotFromServer(EmptySlot);
	ClientGameplayFeedback(TEXT("Camera added for testing."));
}

void AMandaMelaPlayerController::MM_TestFlash(float Intensity)
{
	const float TestIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	const float TestDuration = FMath::Max(0.8f, (FlashHoldTime + FlashFadeTime) * TestIntensity);
	ClientApplyFlash_Implementation(TestIntensity, TestDuration);
}

void AMandaMelaPlayerController::ClientWatchSpectatorTarget_Implementation(AActor* Target)
{
	bDeathmatchSpectating = true;
	SpectatorTargets.Reset();
	SpectatorTargetIndex = INDEX_NONE;
	if (IsValid(Target))
	{
		SetViewTargetWithBlend(Target, 0.35f);
	}
	RefreshSpectatorTargets();
}

void AMandaMelaPlayerController::ClientShowDeathNotice_Implementation(const FString& Message)
{
	RestoreCharacterAfterCamera();
	RestoreBinoculars();
	ClearFlashEffect();
	SpectatorDeathMessage = Message;
	if (GameplayWidget)
	{
		GameplayWidget->SetDeathNotice(true, FText::FromString(Message));
		if (bDeathmatchSpectating)
		{
			GameplayWidget->SetSpectatorDeathMessage(FText::FromString(Message));
		}
	}
	GetWorldTimerManager().ClearTimer(DeathNoticeTimer);
	GetWorldTimerManager().SetTimer(DeathNoticeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (GameplayWidget)
		{
			GameplayWidget->SetDeathNotice(false);
		}
	}), 5.0f, false);
}

void AMandaMelaPlayerController::ServerDebugSelfEliminate_Implementation()
{
	if (GetNetMode() != NM_Standalone)
	{
		return;
	}
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->HandleDebugSelfElimination(this);
	}
}

void AMandaMelaPlayerController::RequestMatchMode(EMandaMelaMatchMode Mode)
{
	if (bCameraModeActive || bCameraPhotoEntryActive) return;
	const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
	if (MatchState && MatchState->GetMatchPhase() != EMandaMelaMatchPhase::InProgress)
	{
		ServerSelectMatchMode(Mode);
	}
}

void AMandaMelaPlayerController::RequestStartMatch()
{
	if (!bPauseMenuOpen && !bSubmissionPending && !bCameraPhotoEntryActive && !bCameraModeActive)
	{
		ServerStartMatch();
	}
}

AMandaMelaPickup* AMandaMelaPlayerController::FindNearbyPickup() const
{
	APawn* PlayerPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!PlayerPawn || !World)
	{
		return nullptr;
	}

	AMandaMelaPickup* ClosestPickup = nullptr;
	float ClosestDistance = 500.0f;
	for (TActorIterator<AMandaMelaPickup> It(World); It; ++It)
	{
		AMandaMelaPickup* Pickup = *It;
		if (Pickup->GetItemType() == EMandaMelaItemType::None || Pickup->IsActorBeingDestroyed())
		{
			continue;
		}

		const float Distance = PlayerPawn->GetDistanceTo(Pickup);
		if (Distance <= ClosestDistance)
		{
			ClosestPickup = Pickup;
			ClosestDistance = Distance;
		}
	}
	return ClosestPickup;
}

void AMandaMelaPlayerController::Interact()
{
	if (bPauseMenuOpen || bCameraPhotoEntryActive || bCameraModeActive)
	{
		return;
	}
	if (const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>(); MatchPlayerState && !MatchPlayerState->IsAlive())
	{
		return;
	}
	if (AMandaMelaPickup* Pickup = FindNearbyPickup())
	{
		ServerRequestPickup(Pickup);
	}
}

void AMandaMelaPlayerController::SelectSlot(int32 Slot)
{
	if (!bCameraPhotoEntryActive && !bCameraModeActive && Slot >= 0 && Slot <= 1)
	{
		SelectedInventorySlot = Slot;
		RefreshHud();
		ServerSelectInventorySlot(Slot);
	}
}

void AMandaMelaPlayerController::SetSelectedInventorySlotFromServer(int32 Slot)
{
	if (!HasAuthority() || Slot < 0 || Slot > 1)
	{
		return;
	}
	SelectedInventorySlot = Slot;
	RefreshHeldTacticalItem(false);
	ClientSetSelectedInventorySlot(Slot);
	RefreshHud();
}

void AMandaMelaPlayerController::RefreshHeldTacticalItem(bool bSelectOtherIfEmpty)
{
	if (!HasAuthority())
	{
		return;
	}
	AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	if (!MatchPlayerState)
	{
		return;
	}
	if (bSelectOtherIfEmpty && MatchPlayerState->GetInventoryItem(SelectedInventorySlot) == EMandaMelaItemType::None)
	{
		const int32 OtherSlot = 1 - SelectedInventorySlot;
		if (MatchPlayerState->GetInventoryItem(OtherSlot) != EMandaMelaItemType::None)
		{
			SelectedInventorySlot = OtherSlot;
			ClientSetSelectedInventorySlot(OtherSlot);
		}
	}
	if (AMandaMelaCharacter* HeldCharacter = Cast<AMandaMelaCharacter>(GetPawn()))
	{
		HeldCharacter->SetHeldTacticalItem(MatchPlayerState->GetInventoryItem(SelectedInventorySlot));
	}
	RefreshHud();
}

void AMandaMelaPlayerController::CycleSelectedSlot()
{
	if (!bCameraPhotoEntryActive && !bCameraModeActive) SelectSlot((SelectedInventorySlot + 1) % 2);
}

void AMandaMelaPlayerController::UseSlot(int32 Slot)
{
	if (bPauseMenuOpen || bCameraPhotoEntryActive || bCameraModeActive || Slot < 0 || Slot > 1)
	{
		return;
	}
	if (const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>(); !MatchPlayerState || !MatchPlayerState->IsAlive())
	{
		return;
	}
	ServerRequestUseItem(Slot, nullptr);
}

void AMandaMelaPlayerController::UseSelectedSlot()
{
	UseSlot(SelectedInventorySlot);
}

void AMandaMelaPlayerController::DropSelectedItem()
{
	if (bPauseMenuOpen || bCameraPhotoEntryActive || bCameraModeActive)
	{
		return;
	}
	ServerRequestDrop(SelectedInventorySlot);
}

void AMandaMelaPlayerController::RefreshHud()
{
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
	const EMandaMelaMatchPhase Phase = MatchState ? MatchState->GetMatchPhase() : EMandaMelaMatchPhase::Waiting;
	if (bBinocularsActive && (!MatchPlayerState || !MatchPlayerState->IsAlive() || Phase != EMandaMelaMatchPhase::InProgress))
	{
		RestoreBinoculars();
	}
	if (bFlashActive && ((MatchPlayerState && !MatchPlayerState->IsAlive())
		|| (MatchState && Phase == EMandaMelaMatchPhase::Ended)))
	{
		ClearFlashEffect();
	}
	const bool bRoundAllowsMovement = !MatchState || MatchState->GetMatchPhase() != EMandaMelaMatchPhase::Ended;
	const bool bPlayerAlive = !MatchPlayerState || !MatchPlayerState->IsParticipating() || MatchPlayerState->IsAlive();
	SetIgnoreMoveInput(bPauseMenuOpen || !bRoundAllowsMovement || !bPlayerAlive);
	SetIgnoreLookInput(bPauseMenuOpen || !bRoundAllowsMovement || !bPlayerAlive);
	if (!GameplayWidget)
	{
		return;
	}
	FString Status = TEXT("Waiting for host to start");
	if (Phase == EMandaMelaMatchPhase::Waiting)
	{
		const TCHAR* ModeName = MatchState && MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch ? TEXT("TEAM DEATHMATCH")
			: MatchState && MatchState->GetMatchMode() == EMandaMelaMatchMode::CaptureTheFlag ? TEXT("CAPTURE THE FLAG") : TEXT("DEATHMATCH");
		Status = FString::Printf(TEXT("%s  |  %d connected"), ModeName, MatchState ? MatchState->PlayerArray.Num() : 0);
	}
	else if (Phase == EMandaMelaMatchPhase::InProgress && MatchState)
	{
		const bool bTeamMode = MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch;
		if (!bTeamMode)
		{
			Status = MatchPlayerState && MatchPlayerState->IsParticipating() && !MatchPlayerState->IsAlive()
				? FString::Printf(TEXT("ELIMINATED  |  %d players remain"), MatchState->GetLivingPlayerCount())
				: FString::Printf(TEXT("DEATHMATCH  |  %d remain  |  ELIMS %d"), MatchState->GetLivingPlayerCount(), MatchPlayerState ? MatchPlayerState->GetEliminationCount() : 0);
		}
		else
		{
			const float RemainingTime = FMath::Max(0.0f, MatchState->GetMatchEndServerTime() - MatchState->GetServerWorldTimeSeconds());
			const int32 Minutes = FMath::FloorToInt(RemainingTime / 60.0f);
			const int32 Seconds = FMath::FloorToInt(RemainingTime) % 60;
			const EMandaMelaTeam PlayerTeam = MatchPlayerState ? MatchPlayerState->GetTeam() : EMandaMelaTeam::None;
			const TCHAR* TeamName = PlayerTeam == EMandaMelaTeam::Red ? TEXT("RED") : (PlayerTeam == EMandaMelaTeam::Blue ? TEXT("BLUE") : TEXT("SPECTATOR"));
			if (MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch)
			{
				const float Cooldown = MatchPlayerState ? MatchPlayerState->GetCooldownEndServerTime() - MatchState->GetServerWorldTimeSeconds() : 0.0f;
				Status = FString::Printf(TEXT("TDM  |  RED %d - %d BLUE  |  TO 10  |  %02d:%02d  |  YOU %s  |  ELIMS %d%s"),
					MatchState->GetTeamScore(EMandaMelaTeam::Red), MatchState->GetTeamScore(EMandaMelaTeam::Blue), Minutes, Seconds, TeamName,
					MatchPlayerState ? MatchPlayerState->GetEliminationCount() : 0,
					Cooldown > 0.0f ? *FString::Printf(TEXT("  |  COOLDOWN %d"), FMath::CeilToInt(Cooldown)) : TEXT(""));
			}
			else
			{
				const AMandaMelaFlag* OwnFlag = MatchState->GetFlagForTeam(PlayerTeam);
				const EMandaMelaTeam EnemyTeam = PlayerTeam == EMandaMelaTeam::Red ? EMandaMelaTeam::Blue : EMandaMelaTeam::Red;
				const AMandaMelaFlag* EnemyFlag = MatchState->GetFlagForTeam(EnemyTeam);
				Status = FString::Printf(TEXT("CTF  |  RED %d - %d BLUE  |  %02d:%02d  |  YOUR FLAG %s  |  ENEMY %s"),
					MatchState->GetCaptureCount(EMandaMelaTeam::Red), MatchState->GetCaptureCount(EMandaMelaTeam::Blue), Minutes, Seconds,
					OwnFlag ? OwnFlag->GetStateText() : TEXT("UNAVAILABLE"), EnemyFlag ? EnemyFlag->GetStateText() : TEXT("UNAVAILABLE"));
			}
			if (MatchPlayerState && !MatchPlayerState->IsAlive())
			{
				Status += TEXT("  |  RESPAWNING");
			}
		}
	}
	else if (MatchState)
	{
		if (MatchState->GetMatchMode() != EMandaMelaMatchMode::Deathmatch && MatchState->GetWinningTeam() != EMandaMelaTeam::None)
		{
			const bool bTdm = MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch;
			const int32 RedScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Red) : MatchState->GetCaptureCount(EMandaMelaTeam::Red);
			const int32 BlueScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Blue) : MatchState->GetCaptureCount(EMandaMelaTeam::Blue);
			const TCHAR* WinningTeamName = MatchState->GetWinningTeam() == EMandaMelaTeam::Red ? TEXT("RED") : TEXT("BLUE");
			if (bTdm && MatchPlayerState)
			{
				const TCHAR* Result = MatchPlayerState->GetTeam() == MatchState->GetWinningTeam() ? TEXT("VICTORY") : TEXT("DEFEAT");
				Status = FString::Printf(TEXT("%s! %s TEAM WINS  |  RED %d - %d BLUE"), Result, WinningTeamName, RedScore, BlueScore);
			}
			else
			{
				Status = FString::Printf(TEXT("%s WINS  |  RED %d - %d BLUE  |  %s"), WinningTeamName,
					RedScore, BlueScore, bTdm ? TEXT("POINTS") : TEXT("CAPTURES"));
			}
		}
		else if (MatchState->IsDraw())
		{
			if (MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch)
			{
				Status = TEXT("ROUND ENDED IN A DRAW");
			}
			else
			{
				const bool bTdm = MatchState->GetMatchMode() == EMandaMelaMatchMode::TeamDeathmatch;
				const int32 RedScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Red) : MatchState->GetCaptureCount(EMandaMelaTeam::Red);
				const int32 BlueScore = bTdm ? MatchState->GetTeamScore(EMandaMelaTeam::Blue) : MatchState->GetCaptureCount(EMandaMelaTeam::Blue);
				Status = FString::Printf(TEXT("DRAW  |  RED %d - %d BLUE"), RedScore, BlueScore);
			}
		}
		else if (MatchPlayerState && MatchState->GetWinner() == MatchPlayerState)
		{
			Status = FString::Printf(TEXT("VICTORY  |  ELIMS %d"), MatchPlayerState->GetEliminationCount());
		}
		else if (MatchState->GetWinner())
		{
			const AMandaMelaPlayerState* WinnerState = Cast<AMandaMelaPlayerState>(MatchState->GetWinner());
			Status = FString::Printf(TEXT("WINNER: %s  |  ELIMS %d"), *MatchState->GetWinner()->GetPlayerName(), WinnerState ? WinnerState->GetEliminationCount() : 0);
		}
	}
	const EMandaMelaItemType Slot1 = MatchPlayerState ? MatchPlayerState->GetInventoryItem(0) : EMandaMelaItemType::None;
	const EMandaMelaItemType Slot2 = MatchPlayerState ? MatchPlayerState->GetInventoryItem(1) : EMandaMelaItemType::None;
	GameplayWidget->SetInventorySlots(
		FText::FromString(FString(GetInventoryItemName(Slot1))),
		FText::FromString(FString(GetInventoryItemName(Slot2))),
		SelectedInventorySlot);
	if (bScoreboardOpen)
	{
		GameplayWidget->RefreshScoreboard();
	}
	if (bDeathmatchSpectating && MatchPlayerState && MatchPlayerState->IsAlive())
	{
		bDeathmatchSpectating = false;
		SpectatorTargets.Reset();
		SpectatorTargetIndex = INDEX_NONE;
		SpectatorDeathMessage.Reset();
		GameplayWidget->SetSpectatorInfo(false, FText::GetEmpty(), INDEX_NONE, 0);
		GameplayWidget->SetSpectatorDeathMessage(FText::GetEmpty());
	}
	if (bDeathmatchSpectating)
	{
		RefreshSpectatorTargets();
	}
	GameplayWidget->SetFeedback(FText::FromString(LastFeedback));
	if (!bCameraPhotoEntryActive && !bCameraModeActive)
	{
		GameplayWidget->SetKeyHint(FText::FromString(TEXT("TYPE 0-9   |   ENTER SUBMIT   |   BACKSPACE DELETE   |   TAB SCOREBOARD")));
	}
	FString Prompt;
	if (!bCameraModeActive && MatchPlayerState && MatchPlayerState->IsAlive()
		&& MatchState && MatchState->GetMatchPhase() == EMandaMelaMatchPhase::InProgress)
	{
		if (AMandaMelaPickup* Pickup = FindNearbyPickup())
		{
			const bool bInventoryFull = MatchPlayerState->GetInventoryItem(0) != EMandaMelaItemType::None
				&& MatchPlayerState->GetInventoryItem(1) != EMandaMelaItemType::None;
			Prompt = bInventoryFull
				? TEXT("Inventory full  |  G drop an item")
				: FString::Printf(TEXT("E   PICK UP   /   %s"), GetInventoryItemName(Pickup->GetItemType()));
		}
		else
		{
			if (MatchState->GetMatchMode() == EMandaMelaMatchMode::CaptureTheFlag)
			{
				Prompt = TEXT("Touch enemy flag  |  Return dropped flag  |  Bring it home to capture  |  0-9 guess  |  Enter submit  |  Tab scoreboard");
			}
			else if (MatchPlayerState->GetInventoryItem(SelectedInventorySlot) == EMandaMelaItemType::Camera)
			{
				Prompt = TEXT("Hold right click to use camera  |  E pick up  |  Scroll slots  |  G drop");
			}
			else
			{
				Prompt = TEXT("Gear spawns near players  |  E pick up  |  Scroll slots  |  F use/throw  |  G drop");
			}
		}
	}
	GameplayWidget->SetMatchHud(FText::FromString(Status), FText::FromString(Prompt));
	GameplayWidget->SetMatchSetup(Phase, MatchState ? MatchState->GetMatchMode() : EMandaMelaMatchMode::Deathmatch,
		MatchState ? MatchState->PlayerArray.Num() : 0, IsLocalController() && HasAuthority());
}

void AMandaMelaPlayerController::ClearBinocularView()
{
	if (AMandaMelaCharacter* ControlledPawn = BinocularPawn.Get())
	{
		if (USpringArmComponent* CameraBoom = ControlledPawn->GetCameraBoom())
		{
			CameraBoom->TargetArmLength = NormalSpringArmLength;
		}
		if (UCameraComponent* Camera = ControlledPawn->GetFollowCamera())
		{
			Camera->SetFieldOfView(NormalCameraFOV);
			Camera->SetRelativeLocation(FVector(0.0f, 0.0f, NormalCameraLocationZ));
		}
	}
	for (int32 i = 0; i < BinocularMeshComponents.Num() && i < BinocularMeshVisibility.Num(); ++i)
	{
		if (UPrimitiveComponent* Component = BinocularMeshComponents[i].Get())
		{
			Component->SetOwnerNoSee(BinocularMeshVisibility[i]);
		}
	}
	if (AMandaMelaCharacter* ControlledPawn = BinocularPawn.Get())
	{
		ControlledPawn->RefreshBinocularVisuals();
	}
	BinocularPawn.Reset();
	BinocularMeshComponents.Empty();
	BinocularMeshVisibility.Empty();
}

void AMandaMelaPlayerController::HideCharacterForCamera()
{
	if (bCameraModeHidesCharacter) return;
	AMandaMelaCharacter* ControlledCharacter = Cast<AMandaMelaCharacter>(GetPawn());
	if (!ControlledCharacter) return;
	bCameraModePreviousActorHidden = ControlledCharacter->IsHidden();
	ControlledCharacter->SetActorHiddenInGame(true);
	if (UCameraComponent* Camera = ControlledCharacter->GetFollowCamera())
	{
		CameraModeOriginalFOV = Camera->FieldOfView;
		Camera->SetFieldOfView(40.0f);
	}

	TArray<UPrimitiveComponent*> Components;
	ControlledCharacter->GetBinocularVisualComponents(Components);
	CameraModePawn = ControlledCharacter;
	CameraModeHiddenComponents.Empty();
	CameraModePreviousVisibility.Empty();
	for (UPrimitiveComponent* Component : Components)
	{
		if (!Component) continue;
		CameraModeHiddenComponents.Add(Component);
		CameraModePreviousVisibility.Add(Component->IsVisible());
	}
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : CameraModeHiddenComponents)
	{
		if (UPrimitiveComponent* VisibleComponent = Component.Get())
		{
			VisibleComponent->SetVisibility(false, true);
		}
	}
	bCameraModeHidesCharacter = true;
}

void AMandaMelaPlayerController::RestoreCharacterAfterCamera()
{
	if (!bCameraModeHidesCharacter) return;
	for (int32 Index = 0; Index < CameraModeHiddenComponents.Num() && Index < CameraModePreviousVisibility.Num(); ++Index)
	{
		if (UPrimitiveComponent* Component = CameraModeHiddenComponents[Index].Get())
		{
			Component->SetVisibility(CameraModePreviousVisibility[Index], true);
		}
	}
	if (AMandaMelaCharacter* CameraModeCharacter = CameraModePawn.Get())
	{
		CameraModeCharacter->SetActorHiddenInGame(bCameraModePreviousActorHidden);
		if (UCameraComponent* Camera = CameraModeCharacter->GetFollowCamera())
		{
			Camera->SetFieldOfView(CameraModeOriginalFOV);
		}
		CameraModeCharacter->RefreshBinocularVisuals();
	}
	CameraModePawn.Reset();
	CameraModeHiddenComponents.Empty();
	CameraModePreviousVisibility.Empty();
	bCameraModeHidesCharacter = false;
}

void AMandaMelaPlayerController::RestoreBinoculars()
{
	if (!bBinocularsActive)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(BinocularTimer);
	ClearBinocularView();
	if (GameplayWidget)
	{
		if (GameplayWidget->GetClass()->FindFunctionByName(TEXT("StartBinocularsFade")))
		{
			GameplayWidget->StartBinocularsFade(false, 0.4f);
		}
		else
		{
			GameplayWidget->SetBinocularsVisible(false);
		}
	}
	bBinocularsActive = false;
}

UTextureRenderTarget2D* AMandaMelaPlayerController::CaptureCameraPhotoScene()
{
	AMandaMelaCharacter* ControlledPawn = Cast<AMandaMelaCharacter>(GetPawn());
	UCameraComponent* Camera = ControlledPawn ? ControlledPawn->GetFollowCamera() : nullptr;
	if (!Camera)
	{
		return nullptr;
	}

	USceneCaptureComponent2D* CameraPhotoCapture = ControlledPawn->FindComponentByClass<USceneCaptureComponent2D>();
	if (!IsValid(CameraPhotoCapture))
	{
		CameraPhotoCapture = NewObject<USceneCaptureComponent2D>(ControlledPawn, TEXT("CameraPhotoCapture"), RF_Transient);
		CameraPhotoCapture->SetupAttachment(Camera);
		CameraPhotoCapture->RegisterComponent();
	}
	CameraPhotoCapture->bCaptureEveryFrame = false;
	CameraPhotoCapture->bCaptureOnMovement = false;
	CameraPhotoCapture->bAlwaysPersistRenderingState = true;
	CameraPhotoCapture->bMainViewFamily = true;
	CameraPhotoCapture->bInheritMainViewCameraPostProcessSettings = true;
	CameraPhotoCapture->ShowFlags.SetDynamicShadows(false);
	CameraPhotoCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	constexpr float CameraFrameWidthFraction = 0.72f;
	constexpr float CameraFrameHeightFraction = 0.84f;
	float ViewAspect = FMath::Max(Camera->AspectRatio, 0.01f);
	if (Camera->bConstrainAspectRatio)
	{
		ViewAspect = Camera->AspectRatio;
	}
	else
	{
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		GetViewportSize(ViewportWidth, ViewportHeight);
		if (ViewportHeight > 0)
		{
			ViewAspect = static_cast<float>(ViewportWidth) / ViewportHeight;
		}
	}
	const float PhotoAspect = ViewAspect * CameraFrameWidthFraction / CameraFrameHeightFraction;
	const int32 PhotoWidth = PhotoAspect >= 1.0f ? 640 : FMath::Max(1, FMath::RoundToInt(640.0f * PhotoAspect));
	const int32 PhotoHeight = PhotoAspect >= 1.0f ? FMath::Max(1, FMath::RoundToInt(640.0f / PhotoAspect)) : 640;
	CameraPhotoRenderTarget = CameraPhotoCapture->TextureTarget;
	if (!CameraPhotoRenderTarget)
	{
		CameraPhotoRenderTarget = NewObject<UTextureRenderTarget2D>(CameraPhotoCapture, TEXT("CameraPhotoRenderTarget"), RF_Transient);
	}
	if (!CameraPhotoRenderTarget) return nullptr;
	if (CameraPhotoRenderTarget->SizeX != PhotoWidth || CameraPhotoRenderTarget->SizeY != PhotoHeight)
	{
		CameraPhotoRenderTarget->InitAutoFormat(PhotoWidth, PhotoHeight);
		CameraPhotoRenderTarget->ClearColor = FLinearColor::Black;
	}
	CameraPhotoCapture->TextureTarget = CameraPhotoRenderTarget;
	CameraPhotoCapture->HiddenActors.AddUnique(ControlledPawn);

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float ViewFOV = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : Camera->FieldOfView;
	const float HalfViewFOV = FMath::DegreesToRadians(ViewFOV * 0.5f);
	const float PhotoHorizontalScale = (static_cast<float>(PhotoWidth) / PhotoHeight) * CameraFrameHeightFraction / ViewAspect;
	CameraPhotoCapture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfViewFOV) * PhotoHorizontalScale));
	CameraPhotoCapture->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	CameraPhotoCapture->SetWorldLocationAndRotation(ViewLocation, ViewRotation);
	CameraPhotoCapture->CaptureSceneDeferred();
	return CameraPhotoRenderTarget;
}

void AMandaMelaPlayerController::CloseCameraMode()
{
	bCameraModeActive = false;
	bCameraPhotoCapturePending = false;
	bCameraRightMouseHeld = false;
	RestoreCharacterAfterCamera();
	if (GameplayWidget)
	{
		GameplayWidget->SetCameraModeVisible(false);
		GameplayWidget->SetKeyHint(FText::FromString(TEXT("TYPE 0-9   |   ENTER SUBMIT   |   BACKSPACE DELETE   |   TAB SCOREBOARD")));
	}
	ServerCancelCameraPhoto();
	RefreshHud();
}

void AMandaMelaPlayerController::CloseCameraPhoto()
{
	bCameraPhotoEntryActive = false;
	bCameraPhotoTargetAvailable = false;
	bCameraPhotoAnswerPending = false;
	bCameraRightMouseHeld = false;
	RestoreCharacterAfterCamera();
	GuessDigits.Reset();
	if (GameplayWidget)
	{
		GameplayWidget->SetGuessText(GuessDigits);
		GameplayWidget->HideCameraPhotoCard();
		GameplayWidget->SetKeyHint(FText::FromString(TEXT("TYPE 0-9   |   ENTER SUBMIT   |   BACKSPACE DELETE   |   TAB SCOREBOARD")));
	}
	ServerCancelCameraPhoto();
	RefreshHud();
}

void AMandaMelaPlayerController::ServerSubmitGuess_Implementation(const FString& Digits)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAttackRequestTime < 0.5f)
	{
		return;
	}
	LastAttackRequestTime = Now;
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->HandleAttack(this, Digits);
	}
}

void AMandaMelaPlayerController::ServerSubmitCameraPhotoAnswer_Implementation(const FString& Digits)
{
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->HandleCameraPhotoAnswer(this, Digits);
	}
}

void AMandaMelaPlayerController::ServerCaptureCameraPhoto_Implementation()
{
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->HandleCameraPhotoCapture(this);
	}
}

void AMandaMelaPlayerController::ServerCancelCameraPhoto_Implementation()
{
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->CancelCameraPhoto(this);
	}
}

void AMandaMelaPlayerController::ServerSelectMatchMode_Implementation(EMandaMelaMatchMode Mode)
{
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->SelectMatchMode(this, Mode);
	}
}

void AMandaMelaPlayerController::ServerStartMatch_Implementation()
{
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
	{
		GameMode->StartMatch(this);
	}
}

void AMandaMelaPlayerController::ServerRequestPickup_Implementation(AMandaMelaPickup* Pickup)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastActionRequestTime < 0.1f) return;
	LastActionRequestTime = Now;
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>()) GameMode->HandlePickup(this, Pickup);
}

void AMandaMelaPlayerController::ServerRequestDrop_Implementation(int32 Slot)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastActionRequestTime < 0.1f) return;
	LastActionRequestTime = Now;
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>()) GameMode->HandleDrop(this, Slot);
}

void AMandaMelaPlayerController::ServerRequestUseItem_Implementation(int32 Slot, AMandaMelaCharacter* VisibleTarget)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastActionRequestTime < 0.1f) return;
	LastActionRequestTime = Now;
	if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>()) GameMode->HandleUseItem(this, Slot, VisibleTarget);
}

void AMandaMelaPlayerController::ServerSelectInventorySlot_Implementation(int32 Slot)
{
	SetSelectedInventorySlotFromServer(Slot);
}

void AMandaMelaPlayerController::ClientSetSelectedInventorySlot_Implementation(int32 Slot)
{
	if (Slot >= 0 && Slot <= 1)
	{
		SelectedInventorySlot = Slot;
		RefreshHud();
	}
}

void AMandaMelaPlayerController::ClientAttackFeedback_Implementation(bool bCorrect, const FString& Message)
{
	bSubmissionPending = false;
	GuessDigits.Reset();
	LastFeedback = Message;
	if (GameplayWidget)
	{
		GameplayWidget->SetGuessText(GuessDigits);
		GameplayWidget->SetFeedback(FText::FromString(Message));
		if (bCorrect && GameplayWidget->GetClass()->FindFunctionByName(TEXT("AddKillFeedMessage")))
		{
			GameplayWidget->AddKillFeedMessage(Message);
		}
	}
	GetWorldTimerManager().SetTimer(FeedbackTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		LastFeedback.Reset();
		if (GameplayWidget) GameplayWidget->SetFeedback(FText::GetEmpty());
	}), 3.0f, false);
}

void AMandaMelaPlayerController::ClientGameplayFeedback_Implementation(const FString& Message)
{
	LastFeedback = Message;
	if (GameplayWidget)
	{
		GameplayWidget->SetFeedback(FText::FromString(Message));
	}
	GetWorldTimerManager().SetTimer(FeedbackTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		LastFeedback.Reset();
		if (GameplayWidget) GameplayWidget->SetFeedback(FText::GetEmpty());
	}), 2.5f, false);
}

void AMandaMelaPlayerController::ApplyBinocularView()
{
	if (bBinocularsActive)
	{
		return;
	}
	AMandaMelaCharacter* ControlledPawn = Cast<AMandaMelaCharacter>(GetPawn());
	if (!ControlledPawn)
	{
		return;
	}
	if (UCameraComponent* Camera = ControlledPawn->GetFollowCamera())
	{
		NormalCameraFOV = Camera->FieldOfView;
		NormalCameraLocationZ = Camera->GetRelativeLocation().Z;
	}
	if (USpringArmComponent* CameraBoom = ControlledPawn->GetCameraBoom())
	{
		NormalSpringArmLength = CameraBoom->TargetArmLength;
	}
	BinocularPawn = ControlledPawn;
	BinocularMeshComponents.Empty();
	BinocularMeshVisibility.Empty();
	TArray<UPrimitiveComponent*> Components;
	ControlledPawn->GetBinocularVisualComponents(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		if (Component)
		{
			BinocularMeshComponents.Add(Component);
			BinocularMeshVisibility.Add(Component->bOwnerNoSee);
			Component->SetOwnerNoSee(true);
		}
	}
	if (UCameraComponent* Camera = ControlledPawn->GetFollowCamera())
	{
		Camera->SetFieldOfView(40.0f);
	}
	if (USpringArmComponent* CameraBoom = ControlledPawn->GetCameraBoom())
	{
		CameraBoom->TargetArmLength = 0.0f;
		if (UCameraComponent* Cam2 = ControlledPawn->GetFollowCamera()) Cam2->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	}
	bBinocularsActive = true;
}

void AMandaMelaPlayerController::ClientUseBinoculars_Implementation(float Duration)
{
	ApplyBinocularView();
	if (GameplayWidget)
	{
		if (GameplayWidget->GetClass()->FindFunctionByName(TEXT("StartBinocularsFade")))
		{
			GameplayWidget->StartBinocularsFade(true, 0.3f);
		}
		else
		{
			GameplayWidget->SetBinocularsVisible(true);
		}
	}
	GetWorldTimerManager().SetTimer(BinocularTimer, this, &AMandaMelaPlayerController::RestoreBinoculars, FMath::Clamp(Duration, 0.5f, 8.0f), false);
}

void AMandaMelaPlayerController::ClientStoreCameraRecord_Implementation()
{
	if (!bCameraModeActive || !GameplayWidget || !CameraPhotoRenderTarget)
	{
		ServerCancelCameraPhoto();
		return;
	}

	if (bBinocularsActive)
	{
		RestoreBinoculars();
	}
	bCameraModeActive = false;
	bCameraPhotoCapturePending = false;
	bCameraRightMouseHeld = false;
	bCameraPhotoEntryActive = true;
	bCameraPhotoTargetAvailable = true;
	bCameraPhotoAnswerPending = false;
	GuessDigits.Reset();
	GameplayWidget->SetGuessText(GuessDigits);
	GameplayWidget->SetKeyHint(FText::FromString(TEXT("PHOTO INPUT  |  TYPE 0-9   |   ENTER SUBMIT   |   ESC CANCEL")));
	GameplayWidget->SetCameraModeVisible(false);
	GameplayWidget->ShowCameraPhotoCard(CameraPhotoRenderTarget,
		FText::FromString(TEXT("Enter the number in the photo. Press Enter to submit.")));
	RefreshHud();
}

void AMandaMelaPlayerController::ClientEnterCameraMode_Implementation()
{
	if (!bCameraRightMouseHeld)
	{
		CloseCameraMode();
		return;
	}
	if (!GameplayWidget)
	{
		ServerCancelCameraPhoto();
		return;
	}
	if (bBinocularsActive)
	{
		RestoreBinoculars();
	}
	HideCharacterForCamera();
	bCameraModeActive = true;
	bCameraPhotoCapturePending = false;
	GuessDigits.Reset();
	GameplayWidget->SetGuessText(GuessDigits);
	GameplayWidget->SetCameraModeVisible(true);
	GameplayWidget->SetCameraModeStatus(FText::FromString(TEXT("CAMERA READY  /  LEFT CLICK TO TAKE PHOTO")));
	GameplayWidget->SetKeyHint(FText::FromString(TEXT("MOVE FREELY  |  LEFT CLICK TAKE PHOTO  |  RELEASE RIGHT CLICK TO CLOSE")));
	RefreshHud();
}

void AMandaMelaPlayerController::ClientCameraPhotoCaptureFailed_Implementation(const FString& Message)
{
	if (!bCameraModeActive) return;
	bCameraPhotoCapturePending = false;
	LastFeedback = Message;
	if (GameplayWidget)
	{
		GameplayWidget->SetCameraModeStatus(FText::FromString(Message));
	}
	if (!bCameraRightMouseHeld)
	{
		CloseCameraMode();
		return;
	}
	RefreshHud();
}

void AMandaMelaPlayerController::ClientEndCameraMode_Implementation(const FString& Message)
{
	bCameraModeActive = false;
	bCameraPhotoCapturePending = false;
	bCameraRightMouseHeld = false;
	RestoreCharacterAfterCamera();
	LastFeedback = Message;
	if (GameplayWidget)
	{
		GameplayWidget->SetCameraModeVisible(false);
		GameplayWidget->SetKeyHint(FText::FromString(TEXT("TYPE 0-9   |   ENTER SUBMIT   |   BACKSPACE DELETE   |   TAB SCOREBOARD")));
	}
	RefreshHud();
}

void AMandaMelaPlayerController::ClientCameraPhotoAnswerResult_Implementation(bool bCorrect, bool bTargetAvailable, const FString& Message)
{
	if (!bCameraPhotoEntryActive)
	{
		return;
	}
	bCameraPhotoAnswerPending = false;
	bCameraPhotoTargetAvailable = bTargetAvailable;
	GuessDigits.Reset();
	if (GameplayWidget) GameplayWidget->SetGuessText(GuessDigits);
	if (GameplayWidget)
	{
		GameplayWidget->SetCameraPhotoStatus(FText::FromString(Message));
		if (!bTargetAvailable)
		{
			GameplayWidget->SetKeyHint(FText::FromString(TEXT("PHOTO COMPLETE   |   ESC CLOSE")));
		}
		if (bCorrect) GameplayWidget->AddKillFeedMessage(Message);
	}
	if (bCorrect)
	{
		LastFeedback = Message;
		CloseCameraPhoto();
	}
}

void AMandaMelaPlayerController::RefreshSpectatorTargets()
{
	if (!bDeathmatchSpectating || !GameplayWidget)
	{
		return;
	}

	const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
	TArray<TWeakObjectPtr<AMandaMelaCharacter>> NewTargets;
	if (MatchState && MatchState->GetMatchMode() == EMandaMelaMatchMode::Deathmatch)
	{
		for (APlayerState* State : MatchState->PlayerArray)
		{
			const AMandaMelaPlayerState* MatchPlayer = Cast<AMandaMelaPlayerState>(State);
			if (MatchPlayer && MatchPlayer->IsAlive())
			{
				if (AMandaMelaCharacter* TargetCharacter = Cast<AMandaMelaCharacter>(MatchPlayer->GetPawn()))
				{
					NewTargets.Add(TargetCharacter);
				}
			}
		}
	}

	AActor* CurrentViewTarget = GetViewTarget();
	int32 NewIndex = INDEX_NONE;
	for (int32 Index = 0; Index < NewTargets.Num(); ++Index)
	{
		if (NewTargets[Index].Get() == CurrentViewTarget)
		{
			NewIndex = Index;
			break;
		}
	}
	if (NewIndex == INDEX_NONE && NewTargets.Num() > 0)
	{
		NewIndex = FMath::Clamp(SpectatorTargetIndex, 0, NewTargets.Num() - 1);
	}

	SpectatorTargets = MoveTemp(NewTargets);
	SpectatorTargetIndex = NewIndex;
	AMandaMelaCharacter* Target = SpectatorTargets.IsValidIndex(SpectatorTargetIndex)
		? SpectatorTargets[SpectatorTargetIndex].Get()
		: nullptr;
	if (Target && CurrentViewTarget != Target)
	{
		SetViewTargetWithBlend(Target, 0.35f);
	}
	const AMandaMelaPlayerState* TargetPlayerState = Target ? Target->GetPlayerState<AMandaMelaPlayerState>() : nullptr;
	const FString TargetName = TargetPlayerState && !TargetPlayerState->GetPlayerName().IsEmpty()
		? TargetPlayerState->GetPlayerName()
		: (Target ? FString(TEXT("Player")) : FString());
	GameplayWidget->SetSpectatorInfo(true, FText::FromString(TargetName), SpectatorTargetIndex, SpectatorTargets.Num());
	GameplayWidget->SetSpectatorDeathMessage(FText::FromString(SpectatorDeathMessage));
}

void AMandaMelaPlayerController::CycleSpectatorTarget(int32 Direction)
{
	if (!bDeathmatchSpectating)
	{
		return;
	}
	RefreshSpectatorTargets();
	if (SpectatorTargets.Num() < 2)
	{
		return;
	}

	SpectatorTargetIndex = (SpectatorTargetIndex + Direction + SpectatorTargets.Num()) % SpectatorTargets.Num();
	if (AMandaMelaCharacter* Target = SpectatorTargets.IsValidIndex(SpectatorTargetIndex)
		? SpectatorTargets[SpectatorTargetIndex].Get()
		: nullptr)
	{
		SetViewTargetWithBlend(Target, 0.35f);
	}
	RefreshSpectatorTargets();
}

void AMandaMelaPlayerController::ClientApplyFlash_Implementation(float Intensity, float Duration)
{
	StartFlashEffect(Intensity, Duration);
}

void AMandaMelaPlayerController::StartFlashEffect(float Intensity, float Duration)
{
	const float RequestedIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	if (RequestedIntensity <= 0.0f)
	{
		return;
	}
	const float CurrentIntensity = GetCurrentFlashIntensity();
	const float NewIntensity = FMath::Clamp(FMath::Max(CurrentIntensity, FMath::Max(FlashMinIntensity, RequestedIntensity)), 0.0f, 1.0f);
	AMandaMelaCharacter* ControlledPawn = Cast<AMandaMelaCharacter>(GetPawn());
	UCameraComponent* Camera = ControlledPawn ? ControlledPawn->GetFollowCamera() : nullptr;
	if (!bFlashPostProcessSaved || !FlashCamera.IsValid())
	{
		if (bFlashPostProcessSaved)
		{
			ClearFlashEffect();
		}
		if (Camera)
		{
			FlashCamera = Camera;
			SavedFlashPostProcessSettings = Camera->PostProcessSettings;
			SavedFlashPostProcessBlendWeight = Camera->PostProcessBlendWeight;
			bFlashPostProcessSaved = true;
		}
	}

	const float BaseDuration = FMath::Max(FlashHoldTime + FlashFadeTime, KINDA_SMALL_NUMBER);
	const float DurationScale = FMath::Max(0.8f, Duration) / BaseDuration;
	FlashBaseIntensity = NewIntensity;
	FlashHoldDuration = FlashHoldTime * DurationScale;
	FlashFadeDuration = FlashFadeTime * DurationScale;
	FlashEffectStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	bFlashActive = true;
	if (GameplayWidget)
	{
		GameplayWidget->StartFlashEffect(FlashBaseIntensity, FlashWarmTint);
	}
	ApplyFlashPostProcess(FlashBaseIntensity);
	if (FlashRingSound)
	{
		UGameplayStatics::PlaySound2D(this, FlashRingSound, FlashBaseIntensity);
	}
	GetWorldTimerManager().ClearTimer(FlashTimer);
	GetWorldTimerManager().SetTimer(FlashTimer, this, &AMandaMelaPlayerController::UpdateFlashEffect, 1.0f / 60.0f, true);
}

float AMandaMelaPlayerController::GetCurrentFlashIntensity() const
{
	if (!bFlashActive)
	{
		return 0.0f;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const float ElapsedTime = World->GetTimeSeconds() - FlashEffectStartTime;
	if (ElapsedTime <= FlashHoldDuration)
	{
		return FlashBaseIntensity;
	}
	if (FlashFadeDuration <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const float FadeAlpha = FMath::Clamp((ElapsedTime - FlashHoldDuration) / FlashFadeDuration, 0.0f, 1.0f);
	return FlashBaseIntensity * (1.0f - FMath::InterpEaseOut(0.0f, 1.0f, FadeAlpha, 2.0f));
}

void AMandaMelaPlayerController::ApplyFlashPostProcess(float Intensity)
{
	UCameraComponent* Camera = FlashCamera.Get();
	if (!Camera || !bFlashPostProcessSaved)
	{
		return;
	}
	const float BlendIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	FPostProcessSettings FlashPostProcessSettings = SavedFlashPostProcessSettings;
	const FVector4 BaseSaturation = SavedFlashPostProcessSettings.bOverride_ColorSaturation
		? SavedFlashPostProcessSettings.ColorSaturation : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	const float BaseBloom = SavedFlashPostProcessSettings.bOverride_BloomIntensity
		? SavedFlashPostProcessSettings.BloomIntensity : 1.0f;
	const float BaseFringe = SavedFlashPostProcessSettings.bOverride_SceneFringeIntensity
		? SavedFlashPostProcessSettings.SceneFringeIntensity : 0.0f;
	const float BaseVignette = SavedFlashPostProcessSettings.bOverride_VignetteIntensity
		? SavedFlashPostProcessSettings.VignetteIntensity : 0.0f;
	const float DesaturatedValue = FMath::Clamp(1.0f - FlashDesaturation, 0.0f, 1.0f);
	FlashPostProcessSettings.bOverride_ColorSaturation = true;
	FlashPostProcessSettings.ColorSaturation = FMath::Lerp(BaseSaturation, FVector4(DesaturatedValue, DesaturatedValue, DesaturatedValue, 1.0f), BlendIntensity);
	FlashPostProcessSettings.bOverride_BloomIntensity = true;
	FlashPostProcessSettings.BloomIntensity = BaseBloom + FlashBloomBoost * BlendIntensity;
	FlashPostProcessSettings.bOverride_SceneFringeIntensity = true;
	FlashPostProcessSettings.SceneFringeIntensity = BaseFringe + 0.6f * BlendIntensity;
	FlashPostProcessSettings.bOverride_VignetteIntensity = true;
	FlashPostProcessSettings.VignetteIntensity = FMath::Lerp(BaseVignette, FMath::Max(BaseVignette, 0.25f), BlendIntensity);
	Camera->PostProcessSettings = FlashPostProcessSettings;
	Camera->PostProcessBlendWeight = 1.0f;
}

void AMandaMelaPlayerController::UpdateFlashEffect()
{
	const float CurrentIntensity = GetCurrentFlashIntensity();
	if (CurrentIntensity <= KINDA_SMALL_NUMBER)
	{
		ClearFlashEffect();
		return;
	}
	if (GameplayWidget)
	{
		GameplayWidget->UpdateFlashEffect(CurrentIntensity);
	}
	ApplyFlashPostProcess(CurrentIntensity);
}

void AMandaMelaPlayerController::ClearFlashEffect()
{
	GetWorldTimerManager().ClearTimer(FlashTimer);
	if (GameplayWidget)
	{
		GameplayWidget->ClearFlashEffect();
	}
	if (bFlashPostProcessSaved)
	{
		if (UCameraComponent* Camera = FlashCamera.Get())
		{
			Camera->PostProcessSettings = SavedFlashPostProcessSettings;
			Camera->PostProcessBlendWeight = SavedFlashPostProcessBlendWeight;
		}
	}
	FlashCamera.Reset();
	bFlashPostProcessSaved = false;
	bFlashActive = false;
	FlashBaseIntensity = 0.0f;
	FlashHoldDuration = 0.0f;
	FlashFadeDuration = 0.0f;
}

void AMandaMelaPlayerController::ClientPlayWarning_Implementation(USoundBase* Sound, FVector Location)
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
	}
}

void AMandaMelaPlayerController::InputScoreboardPressed()
{
	if (bCameraPhotoEntryActive || bCameraModeActive) return;
	bScoreboardOpen = true;
	if (GameplayWidget) GameplayWidget->SetScoreboardVisible(true);
}

void AMandaMelaPlayerController::InputScoreboardReleased()
{
	bScoreboardOpen = false;
	if (GameplayWidget) GameplayWidget->SetScoreboardVisible(false);
}

void AMandaMelaPlayerController::InputCancel()
{
	if (bCameraModeActive)
	{
		if (!bCameraPhotoCapturePending) CloseCameraMode();
		return;
	}
	if (bCameraPhotoEntryActive)
	{
		CloseCameraPhoto();
		return;
	}
	if (!GuessDigits.IsEmpty())
	{
		ClearGuess();
		return;
	}
	bPauseMenuOpen = !bPauseMenuOpen;
	if (GameplayWidget)
	{
		GameplayWidget->SetPauseVisible(bPauseMenuOpen);
	}
	RefreshHud();
}
void AMandaMelaPlayerController::InputSubmit() { SubmitGuess(); }
void AMandaMelaPlayerController::InputBackspace() { BackspaceGuess(); }
void AMandaMelaPlayerController::InputClear() { ClearGuess(); }
void AMandaMelaPlayerController::InputSelectDeathmatch() { RequestMatchMode(EMandaMelaMatchMode::Deathmatch); }
void AMandaMelaPlayerController::InputSelectTeamDeathmatch() { RequestMatchMode(EMandaMelaMatchMode::TeamDeathmatch); }
void AMandaMelaPlayerController::InputSelectCaptureTheFlag() { RequestMatchMode(EMandaMelaMatchMode::CaptureTheFlag); }
void AMandaMelaPlayerController::InputInteract() { Interact(); }
void AMandaMelaPlayerController::InputCycleSlot() { CycleSelectedSlot(); }
void AMandaMelaPlayerController::InputUseSelectedSlot()
{
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	if (MatchPlayerState && MatchPlayerState->GetInventoryItem(SelectedInventorySlot) == EMandaMelaItemType::Camera)
	{
		return;
	}
	UseSelectedSlot();
}
void AMandaMelaPlayerController::InputCameraHoldPressed()
{
	if (bCameraRightMouseHeld || bCameraModeActive || bCameraPhotoEntryActive || bPauseMenuOpen)
	{
		return;
	}
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	if (!MatchPlayerState || MatchPlayerState->GetInventoryItem(SelectedInventorySlot) != EMandaMelaItemType::Camera)
	{
		return;
	}
	bCameraRightMouseHeld = true;
	UseSlot(SelectedInventorySlot);
}
void AMandaMelaPlayerController::InputCameraHoldReleased()
{
	bCameraRightMouseHeld = false;
	if (bCameraModeActive && !bCameraPhotoCapturePending)
	{
		CloseCameraMode();
	}
}
void AMandaMelaPlayerController::InputDropItem() { DropSelectedItem(); }
void AMandaMelaPlayerController::InputCapturePhoto()
{
	if (!bCameraModeActive || bCameraPhotoCapturePending) return;
	CameraPhotoRenderTarget = CaptureCameraPhotoScene();
	if (!CameraPhotoRenderTarget)
	{
		if (GameplayWidget)
		{
			GameplayWidget->SetCameraModeStatus(FText::FromString(TEXT("CAMERA VIEW UNAVAILABLE  /  PRESS ESC")));
		}
		return;
	}
	bCameraPhotoCapturePending = true;
	if (GameplayWidget)
	{
		GameplayWidget->SetCameraModeStatus(FText::FromString(TEXT("CAPTURING PHOTO...")));
	}
	ServerCaptureCameraPhoto();
}
void AMandaMelaPlayerController::InputSpectatorPrevious() { CycleSpectatorTarget(-1); }
void AMandaMelaPlayerController::InputSpectatorNext() { CycleSpectatorTarget(1); }
void AMandaMelaPlayerController::InputDigit(const FInputActionValue&, int32 Digit) { AddGuessDigit(Digit); }
