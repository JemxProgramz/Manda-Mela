// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MandaMelaMatchState.h"
#include "MandaMelaGameplayWidget.generated.h"

class UBorder;
class UBackgroundBlur;
class UCanvasPanel;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UTextureRenderTarget2D;
class UVerticalBox;
class APlayerState;

UCLASS()
class UMandaMelaGameplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	void SetMatchHud(const FText& Status, const FText& Prompt);
	void SetInventorySlots(const FText& Slot1, const FText& Slot2, int32 SelectedSlot);
	void SetGuessText(const FString& Digits);
	void SetFeedback(const FText& Feedback);
	void SetSpectatorInfo(bool bVisible, const FText& TargetName, int32 CurrentTarget, int32 TargetCount);
	void SetSpectatorDeathMessage(const FText& Message);
	void SetDeathNotice(bool bVisible, const FText& Message = FText::GetEmpty());
	void ShowCameraPhotoCard(UTextureRenderTarget2D* Photo, const FText& Status);
	void SetCameraPhotoStatus(const FText& Status);
	void SetCameraModeVisible(bool bVisible);
	void SetCameraModeStatus(const FText& Status);
	void SetKeyHint(const FText& Hint);
	void HideCameraPhotoCard();
	void StartFlashEffect(float Intensity, const FLinearColor& WarmTint);
	void UpdateFlashEffect(float Intensity);
	void ClearFlashEffect();
	void SetBinocularsVisible(bool bVisible);
	void StartBinocularsFade(bool bFadeIn, float Duration);
	void SetMatchSetup(EMandaMelaMatchPhase Phase, EMandaMelaMatchMode Mode, int32 ConnectedPlayers, bool bIsHost);
	void SetPauseVisible(bool bVisible);
	void SetScoreboardVisible(bool bVisible);
	void AddKillFeedMessage(const FString& Message);
	void RefreshScoreboard();

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HudText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PromptText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PromptPanel;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> GuessDigitTexts;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> SpectatorPanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SpectatorTargetText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SpectatorDeathText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> DeathNoticePanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DeathNoticeText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> ScoreboardPanel;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> MatchSetupPanel;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> GuessPanel;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> QuickbarPanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MatchSetupTitle;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MatchSetupOptions;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MatchSetupHint;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PausePanel;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ScoreboardPlayerList;
	UPROPERTY(Transient)
	TArray<TObjectPtr<APlayerState>> ScoreboardPlayers;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ScoreboardPlayerRows;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ScoreboardPlayerNames;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ScoreboardPlayerStatuses;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ScoreboardPlayerTeams;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ScoreboardPlayerScores;
	UPROPERTY(Transient)
	TObjectPtr<UImage> FlashOverlay;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FlashOverlayTexture;
	UPROPERTY(Transient)
	TObjectPtr<UImage> BinocularsOverlay;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BinocularsMaskTexture;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> CameraPhotoPanel;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> CameraPhotoFrame;
	UPROPERTY(Transient)
	TObjectPtr<UImage> CameraPhotoImageWidget;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CameraPhotoStatusText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> CameraPhotoBackdrop;
	UPROPERTY(Transient)
	TObjectPtr<UBackgroundBlur> CameraPhotoBlur;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> CameraPhotoDigitTexts;
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> CameraModePanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CameraModeStatusText;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> KeyHintPanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KeyHintText;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> KillFeedTexts;

	void RebuildScoreboard(const TArray<APlayerState*>& Players);
};
