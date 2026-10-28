// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaGameplayWidget.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaFlag.h"
#include "MandaMelaMatchState.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace
{
	const FLinearColor MandaNavy(0.015f, 0.055f, 0.12f, 0.96f);
	const FLinearColor MandaCard(0.025f, 0.105f, 0.22f, 0.96f);
	const FLinearColor MandaBlue(0.09f, 0.30f, 0.55f, 1.0f);
	const FLinearColor MandaCyan(0.20f, 0.72f, 0.98f, 1.0f);
	const FLinearColor MandaGold(1.0f, 0.68f, 0.08f, 1.0f);
	const FLinearColor MandaWhite(0.91f, 0.95f, 1.0f, 1.0f);
	const FLinearColor MandaMuted(0.48f, 0.62f, 0.76f, 1.0f);

	FSlateChildSize FillSize(float Weight = 1.0f)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Weight;
		return Size;
	}

	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Text, int32 FontSize,
		const FLinearColor& Color = MandaWhite, FName FontStyle = FName("Regular"), FName WidgetName = NAME_None)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
		Label->SetText(FText::FromString(Text));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(FontStyle, FontSize));
		Label->SetColorAndOpacity(FSlateColor(Color));
		return Label;
	}

	UTexture2D* CreateBinocularsMaskTexture()
	{
		constexpr int32 TextureSize = 512;
		UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize, TextureSize, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		if (!Pixels)
		{
			Mip.BulkData.Unlock();
			return nullptr;
		}

		constexpr float ScreenAspect = 16.0f / 9.0f;
		constexpr float LensRadius = 0.43f;
		constexpr float LensCenterOffset = 0.16f;
		constexpr float EdgeFeather = 0.035f;
		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X)
			{
				const FVector2D Point(
					(static_cast<float>(X) / (TextureSize - 1) - 0.5f) * ScreenAspect,
					static_cast<float>(Y) / (TextureSize - 1) - 0.5f);
				const float LeftLensDistance = FVector2D::Distance(Point, FVector2D(-LensCenterOffset, 0.0f));
				const float RightLensDistance = FVector2D::Distance(Point, FVector2D(LensCenterOffset, 0.0f));
				const float Edge = FMath::Min(LeftLensDistance, RightLensDistance) - (LensRadius - EdgeFeather);
				const float Blend = FMath::Clamp(Edge / EdgeFeather, 0.0f, 1.0f);
				const float SmoothBlend = Blend * Blend * (3.0f - 2.0f * Blend);
				Pixels[Y * TextureSize + X] = FColor(0, 0, 0, FMath::RoundToInt(SmoothBlend * 255.0f));
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* CreateFlashOverlayTexture(const FLinearColor& WarmTint)
	{
		constexpr int32 TextureSize = 512;
		UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize, TextureSize, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		if (!Pixels)
		{
			Mip.BulkData.Unlock();
			return nullptr;
		}

		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X)
			{
				const FVector2D Point(
					static_cast<float>(X) / (TextureSize - 1) - 0.5f,
					static_cast<float>(Y) / (TextureSize - 1) - 0.5f);
				const float EdgeBlend = FMath::Clamp(Point.Size() / 0.7071f, 0.0f, 1.0f);
				const FLinearColor PixelColor = FMath::Lerp(FLinearColor::White, WarmTint, EdgeBlend * EdgeBlend);
				Pixels[Y * TextureSize + X] = PixelColor.ToFColorSRGB();
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UBorder* MakeCard(UWidgetTree* Tree, const FLinearColor& Color = MandaCard, FName WidgetName = NAME_None)
	{
		UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), WidgetName);
		Card->SetBrushColor(Color);
		Card->SetPadding(FMargin(10.0f));
		return Card;
	}
}

void UMandaMelaGameplayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree)
	{
		return;
	}

	WidgetTree->RootWidget = nullptr;
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	BinocularsOverlay = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BinocularsOverlay"));
	BinocularsMaskTexture = CreateBinocularsMaskTexture();
	if (BinocularsMaskTexture)
	{
		FSlateBrush MaskBrush;
		MaskBrush.SetResourceObject(BinocularsMaskTexture);
		MaskBrush.ImageSize = FVector2D(512.0f, 512.0f);
		BinocularsOverlay->SetBrush(MaskBrush);
	}
	BinocularsOverlay->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* BinocularsSlot = Root->AddChildToCanvas(BinocularsOverlay))
	{
		BinocularsSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BinocularsSlot->SetOffsets(FMargin(0.0f));
	}

	UVerticalBox* Header = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MatchHeader"));
	UTextBlock* Brand = MakeLabel(WidgetTree, TEXT("MANDA MELA"), 17, MandaGold, FName("Bold"));
	Header->AddChildToVerticalBox(Brand);
	HudText = MakeLabel(WidgetTree, TEXT("Waiting for players"), 20, MandaWhite, FName("Bold"));
	Header->AddChildToVerticalBox(HudText);
	if (UCanvasPanelSlot* HeaderSlot = Root->AddChildToCanvas(Header))
	{
		HeaderSlot->SetPosition(FVector2D(24.0f, 20.0f));
		HeaderSlot->SetSize(FVector2D(600.0f, 78.0f));
	}

	FeedbackText = MakeLabel(WidgetTree, TEXT(""), 16, MandaCyan, FName("Bold"));
	FeedbackText->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* FeedbackSlot = Root->AddChildToCanvas(FeedbackText))
	{
		FeedbackSlot->SetAnchors(FAnchors(0.5f, 0.16f));
		FeedbackSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		FeedbackSlot->SetPosition(FVector2D(0.0f, 0.0f));
		FeedbackSlot->SetSize(FVector2D(580.0f, 36.0f));
	}

	SpectatorPanel = MakeCard(WidgetTree, MandaNavy);
	SpectatorPanel->SetPadding(FMargin(14.0f, 8.0f));
	UVerticalBox* SpectatorLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SpectatorLayout"));
	SpectatorPanel->SetContent(SpectatorLayout);
	UTextBlock* SpectatorTitle = MakeLabel(WidgetTree, TEXT("SPECTATING"), 11, MandaCyan, FName("Bold"));
	SpectatorTitle->SetJustification(ETextJustify::Center);
	SpectatorLayout->AddChildToVerticalBox(SpectatorTitle);
	SpectatorTargetText = MakeLabel(WidgetTree, TEXT("Player"), 17, MandaWhite, FName("Bold"));
	SpectatorTargetText->SetJustification(ETextJustify::Center);
	SpectatorLayout->AddChildToVerticalBox(SpectatorTargetText);
	SpectatorDeathText = MakeLabel(WidgetTree, TEXT(""), 11, FLinearColor(1.0f, 0.40f, 0.32f), FName("Bold"));
	SpectatorDeathText->SetJustification(ETextJustify::Center);
	SpectatorDeathText->SetVisibility(ESlateVisibility::Collapsed);
	SpectatorLayout->AddChildToVerticalBox(SpectatorDeathText);
	UTextBlock* SpectatorHint = MakeLabel(WidgetTree, TEXT("PAGE UP  PREVIOUS     /     PAGE DOWN  NEXT"), 10, MandaMuted, FName("Bold"));
	SpectatorHint->SetJustification(ETextJustify::Center);
	SpectatorLayout->AddChildToVerticalBox(SpectatorHint);
	SpectatorPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* SpectatorSlot = Root->AddChildToCanvas(SpectatorPanel))
	{
		SpectatorSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		SpectatorSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		SpectatorSlot->SetPosition(FVector2D(0.0f, 108.0f));
		SpectatorSlot->SetSize(FVector2D(420.0f, 104.0f));
	}

	DeathNoticePanel = MakeCard(WidgetTree, FLinearColor(0.12f, 0.025f, 0.025f, 0.94f));
	DeathNoticePanel->SetPadding(FMargin(20.0f, 14.0f));
	DeathNoticeText = MakeLabel(WidgetTree, TEXT("YOU WERE ELIMINATED"), 23, FLinearColor(1.0f, 0.36f, 0.28f), FName("Bold"));
	DeathNoticeText->SetJustification(ETextJustify::Center);
	DeathNoticePanel->SetContent(DeathNoticeText);
	DeathNoticePanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* DeathNoticeSlot = Root->AddChildToCanvas(DeathNoticePanel))
	{
		DeathNoticeSlot->SetAnchors(FAnchors(0.5f, 0.34f));
		DeathNoticeSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		DeathNoticeSlot->SetSize(FVector2D(620.0f, 76.0f));
	}

	PromptPanel = MakeCard(WidgetTree, MandaNavy);
	PromptPanel->SetPadding(FMargin(12.0f, 6.0f));
	PromptText = MakeLabel(WidgetTree, TEXT("E pick up   |   Scroll select   |   F use   |   G drop"), 14, MandaWhite, FName("Bold"));
	PromptPanel->SetContent(PromptText);
	PromptPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* PromptSlot = Root->AddChildToCanvas(PromptPanel))
	{
		PromptSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		PromptSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		PromptSlot->SetPosition(FVector2D(24.0f, -24.0f));
		PromptSlot->SetSize(FVector2D(620.0f, 48.0f));
	}

	GuessPanel = MakeCard(WidgetTree, MandaNavy);
	GuessPanel->SetPadding(FMargin(12.0f, 8.0f));
	UVerticalBox* GuessLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuessLayout"));
	GuessPanel->SetContent(GuessLayout);
	UTextBlock* GuessTitle = MakeLabel(WidgetTree, TEXT("NUMBER ENTRY"), 12, MandaCyan, FName("Bold"));
	GuessTitle->SetJustification(ETextJustify::Center);
	GuessLayout->AddChildToVerticalBox(GuessTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	UHorizontalBox* GuessDigits = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GuessDigits"));
	GuessLayout->AddChildToVerticalBox(GuessDigits);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBorder* DigitCard = MakeCard(WidgetTree, MandaBlue);
		DigitCard->SetPadding(FMargin(4.0f, 2.0f));
		UTextBlock* DigitText = MakeLabel(WidgetTree, TEXT("_"), 26, MandaWhite, FName("Bold"));
		DigitText->SetJustification(ETextJustify::Center);
		DigitCard->SetContent(DigitText);
		UHorizontalBoxSlot* DigitSlot = GuessDigits->AddChildToHorizontalBox(DigitCard);
		DigitSlot->SetSize(FillSize());
		DigitSlot->SetPadding(FMargin(3.0f, 0.0f));
		GuessDigitTexts.Add(DigitText);
	}
	UTextBlock* GuessHint = MakeLabel(WidgetTree, TEXT("0-9 TYPE   /   ENTER SUBMIT   /   BACKSPACE EDIT"), 10, MandaMuted, FName("Bold"));
	GuessHint->SetJustification(ETextJustify::Center);
	GuessLayout->AddChildToVerticalBox(GuessHint)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	if (UCanvasPanelSlot* GuessSlot = Root->AddChildToCanvas(GuessPanel))
	{
		GuessSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		GuessSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		GuessSlot->SetPosition(FVector2D(0.0f, -112.0f));
		GuessSlot->SetSize(FVector2D(360.0f, 118.0f));
	}

	QuickbarPanel = MakeCard(WidgetTree, MandaNavy);
	QuickbarPanel->SetPadding(FMargin(5.0f));
	QuickbarPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	UHorizontalBox* Quickbar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Quickbar"));
	QuickbarPanel->SetContent(Quickbar);
	if (UCanvasPanelSlot* QuickbarSlot = Root->AddChildToCanvas(QuickbarPanel))
	{
		QuickbarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		QuickbarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		QuickbarSlot->SetPosition(FVector2D(0.0f, -12.0f));
		QuickbarSlot->SetSize(FVector2D(340.0f, 72.0f));
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FName SlotBorderName(*FString::Printf(TEXT("InventorySlotBorder%d"), Index));
		UBorder* SlotBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), SlotBorderName);
		SlotBorder->SetBrushColor(MandaBlue);
		SlotBorder->SetPadding(FMargin(2.0f));
		UBorder* SlotSurface = MakeCard(WidgetTree, MandaCard);
		SlotSurface->SetPadding(FMargin(8.0f, 3.0f));
		UVerticalBox* SlotContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		SlotSurface->SetContent(SlotContent);
		const FString SlotHeader = FString::Printf(TEXT("WHEEL  /  SLOT %d"), Index + 1);
		SlotContent->AddChildToVerticalBox(MakeLabel(WidgetTree, *SlotHeader, 10, MandaCyan, FName("Bold")));
		const FName ItemTextName(*FString::Printf(TEXT("InventorySlotText%d"), Index));
		UTextBlock* ItemText = MakeLabel(WidgetTree, TEXT("Empty"), 15, MandaMuted, FName("Bold"), ItemTextName);
		ItemText->SetJustification(ETextJustify::Center);
		SlotContent->AddChildToVerticalBox(ItemText);
		SlotBorder->SetContent(SlotSurface);
		UHorizontalBoxSlot* SlotEntry = Quickbar->AddChildToHorizontalBox(SlotBorder);
		SlotEntry->SetSize(FillSize());
		SlotEntry->SetPadding(FMargin(3.0f, 0.0f));
	}

	MatchSetupPanel = MakeCard(WidgetTree, MandaNavy);
	MatchSetupPanel->SetPadding(FMargin(20.0f, 16.0f));
	if (UCanvasPanelSlot* SetupSlot = Root->AddChildToCanvas(MatchSetupPanel))
	{
		SetupSlot->SetAnchors(FAnchors(0.5f, 0.30f));
		SetupSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		SetupSlot->SetSize(FVector2D(620.0f, 180.0f));
	}
	UVerticalBox* SetupLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MatchSetupLayout"));
	MatchSetupPanel->SetContent(SetupLayout);
	MatchSetupTitle = MakeLabel(WidgetTree, TEXT("MATCH SETUP"), 22, MandaGold, FName("Bold"));
	MatchSetupTitle->SetJustification(ETextJustify::Center);
	SetupLayout->AddChildToVerticalBox(MatchSetupTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	MatchSetupOptions = MakeLabel(WidgetTree, TEXT("[Q] DEATHMATCH     [R] TEAM DEATHMATCH     [T] CAPTURE THE FLAG"), 13, MandaWhite, FName("Bold"));
	MatchSetupOptions->SetJustification(ETextJustify::Center);
	SetupLayout->AddChildToVerticalBox(MatchSetupOptions)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	MatchSetupHint = MakeLabel(WidgetTree, TEXT("Waiting for the host"), 14, MandaCyan, FName("Bold"));
	MatchSetupHint->SetJustification(ETextJustify::Center);
	SetupLayout->AddChildToVerticalBox(MatchSetupHint);

	PausePanel = MakeCard(WidgetTree, MandaNavy);
	PausePanel->SetPadding(FMargin(24.0f));
	if (UCanvasPanelSlot* PauseSlot = Root->AddChildToCanvas(PausePanel))
	{
		PauseSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PauseSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PauseSlot->SetSize(FVector2D(400.0f, 160.0f));
	}
	UVerticalBox* PauseLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseLayout"));
	PausePanel->SetContent(PauseLayout);
	UTextBlock* PauseTitle = MakeLabel(WidgetTree, TEXT("PAUSED"), 24, MandaGold, FName("Bold"));
	PauseTitle->SetJustification(ETextJustify::Center);
	PauseLayout->AddChildToVerticalBox(PauseTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	UTextBlock* PauseHint = MakeLabel(WidgetTree, TEXT("Press ESCAPE to resume\nThe match continues while this menu is open."), 13, MandaWhite, FName("Bold"));
	PauseHint->SetJustification(ETextJustify::Center);
	PauseLayout->AddChildToVerticalBox(PauseHint);
	PausePanel->SetVisibility(ESlateVisibility::Collapsed);

	FlashOverlay = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FlashOverlay"));
	FlashOverlay->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* FlashSlot = Root->AddChildToCanvas(FlashOverlay))
	{
		FlashSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		FlashSlot->SetOffsets(FMargin(0.0f));
		FlashSlot->SetZOrder(1000);
	}

	ScoreboardPanel = MakeCard(WidgetTree, MandaNavy);
	ScoreboardPanel->SetPadding(FMargin(18.0f));
	if (UCanvasPanelSlot* ScoreboardSlot = Root->AddChildToCanvas(ScoreboardPanel))
	{
		ScoreboardSlot->SetAnchors(FAnchors(0.28f, 0.12f, 0.72f, 0.88f));
		ScoreboardSlot->SetOffsets(FMargin(0.0f));
	}
	UVerticalBox* ScoreboardLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ScoreboardLayout"));
	ScoreboardPanel->SetContent(ScoreboardLayout);
	UTextBlock* ScoreboardTitle = MakeLabel(WidgetTree, TEXT("MANDA MELA  /  SCOREBOARD"), 22, MandaGold, FName("Bold"));
	ScoreboardTitle->SetJustification(ETextJustify::Center);
	ScoreboardLayout->AddChildToVerticalBox(ScoreboardTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	UTextBlock* ScoreboardHint = MakeLabel(WidgetTree, TEXT("HOLD TAB TO VIEW   /   PLAYER NUMBERS ARE NEVER LISTED"), 10, MandaMuted, FName("Bold"));
	ScoreboardHint->SetJustification(ETextJustify::Center);
	ScoreboardLayout->AddChildToVerticalBox(ScoreboardHint)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	UScrollBox* ScoreboardScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ScoreboardScroll"));
	ScoreboardPlayerList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ScoreboardPlayers"));
	ScoreboardScroll->AddChild(ScoreboardPlayerList);
	ScoreboardLayout->AddChildToVerticalBox(ScoreboardScroll)->SetSize(FillSize());
	ScoreboardPanel->SetVisibility(ESlateVisibility::Collapsed);

	CameraPhotoPanel = MakeCard(WidgetTree, FLinearColor(0.94f, 0.90f, 0.78f, 1.0f), TEXT("CameraPhotoPanel"));
	CameraPhotoPanel->SetPadding(FMargin(12.0f, 12.0f, 12.0f, 10.0f));
	CameraPhotoPanel->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBox* CameraPhotoLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CameraPhotoLayout"));
	CameraPhotoPanel->SetContent(CameraPhotoLayout);
	USizeBox* CameraPhotoFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CameraPhotoFrame"));
	CameraPhotoFrame->SetWidthOverride(320.0f);
	CameraPhotoFrame->SetHeightOverride(250.0f);
	CameraPhotoImageWidget = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CameraPhotoImage"));
	CameraPhotoImageWidget->SetBrushTintColor(FLinearColor::White);
	CameraPhotoFrame->SetContent(CameraPhotoImageWidget);
	CameraPhotoLayout->AddChildToVerticalBox(CameraPhotoFrame)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	UTextBlock* CameraLabel = MakeLabel(WidgetTree, TEXT("TARGET NUMBER"), 11, MandaNavy, FName("Bold"));
	CameraLabel->SetJustification(ETextJustify::Center);
	CameraPhotoLayout->AddChildToVerticalBox(CameraLabel)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	CameraPhotoNumberText = MakeLabel(WidgetTree, TEXT("----"), 32, MandaNavy, FName("Bold"), TEXT("CameraPhotoNumber"));
	CameraPhotoNumberText->SetJustification(ETextJustify::Center);
	CameraPhotoLayout->AddChildToVerticalBox(CameraPhotoNumberText);
	if (UCanvasPanelSlot* CameraSlot = Root->AddChildToCanvas(CameraPhotoPanel))
	{
		// Lower right-centre of the screen
		CameraSlot->SetAnchors(FAnchors(0.7f, 0.7f));
		CameraSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CameraSlot->SetSize(FVector2D(344.0f, 334.0f));
	}

	// Key hint panel bottom-right
	KeyHintPanel = MakeCard(WidgetTree, MandaNavy);
	KeyHintPanel->SetPadding(FMargin(12.0f, 8.0f));
	KeyHintText = MakeLabel(WidgetTree, TEXT("TYPE 0-9   |   ENTER SUBMIT   |   BACKSPACE DELETE   |   TAB SCOREBOARD"), 11, MandaWhite, FName("Bold"));
	KeyHintText->SetJustification(ETextJustify::Right);
	KeyHintPanel->SetContent(KeyHintText);
	KeyHintPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* KeyHintSlot = Root->AddChildToCanvas(KeyHintPanel))
	{
		KeyHintSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		KeyHintSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		KeyHintSlot->SetPosition(FVector2D(-24.0f, -24.0f));
		KeyHintSlot->SetSize(FVector2D(520.0f, 44.0f));
	}

	// Kill feed centred at bottom
	for (int32 KillIndex = 0; KillIndex < 5; ++KillIndex)
	{
		UTextBlock* KillText = MakeLabel(WidgetTree, TEXT(""), 16, MandaWhite, FName("Bold"));
		KillText->SetJustification(ETextJustify::Center);
		KillText->SetVisibility(ESlateVisibility::Collapsed);
		KillFeedTexts.Add(KillText);
		if (UCanvasPanelSlot* KillSlot = Root->AddChildToCanvas(KillText))
		{
			KillSlot->SetAnchors(FAnchors(0.5f, 1.0f));
			KillSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			KillSlot->SetPosition(FVector2D(0.0f, -200.0f - KillIndex * 22.0f));
			KillSlot->SetSize(FVector2D(600.0f, 28.0f));
		}
	}
}

void UMandaMelaGameplayWidget::SetMatchSetup(EMandaMelaMatchPhase Phase, EMandaMelaMatchMode Mode, int32 ConnectedPlayers, bool bIsHost)
{
	if (!MatchSetupPanel || !MatchSetupTitle || !MatchSetupOptions || !MatchSetupHint)
	{
		return;
	}
	const bool bShowSetup = Phase != EMandaMelaMatchPhase::InProgress;
	MatchSetupPanel->SetVisibility(bShowSetup ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShowSetup)
	{
		return;
	}

	MatchSetupTitle->SetText(Phase == EMandaMelaMatchPhase::Ended
		? FText::FromString(TEXT("ROUND RESULTS  /  READY AGAIN?"))
		: FText::FromString(TEXT("CHOOSE A MODE")));
	const FString ModeChoices = FString::Printf(
		TEXT("[Q] DEATHMATCH%s     [R] TEAM DEATHMATCH%s     [T] CAPTURE THE FLAG%s"),
		Mode == EMandaMelaMatchMode::Deathmatch ? TEXT("  *") : TEXT(""),
		Mode == EMandaMelaMatchMode::TeamDeathmatch ? TEXT("  *") : TEXT(""),
		Mode == EMandaMelaMatchMode::CaptureTheFlag ? TEXT("  *") : TEXT(""));
	MatchSetupOptions->SetText(FText::FromString(ModeChoices));
	if (!bIsHost)
	{
		MatchSetupHint->SetText(FText::FromString(FString::Printf(TEXT("%d PLAYERS CONNECTED  /  WAITING FOR THE HOST"), ConnectedPlayers)));
		MatchSetupHint->SetColorAndOpacity(FSlateColor(MandaMuted));
	}
	else if (ConnectedPlayers < 2)
	{
		MatchSetupHint->SetText(FText::FromString(FString::Printf(TEXT("%d / 2 PLAYERS  /  SELECT WITH Q/R/T"), ConnectedPlayers)));
		MatchSetupHint->SetColorAndOpacity(FSlateColor(MandaMuted));
	}
	else
	{
		MatchSetupHint->SetText(FText::FromString(FString::Printf(TEXT("%d PLAYERS  /  Q/R/T SELECT  /  ENTER START"), ConnectedPlayers)));
		MatchSetupHint->SetColorAndOpacity(FSlateColor(MandaCyan));
	}
}

void UMandaMelaGameplayWidget::SetPauseVisible(bool bVisible)
{
	if (PausePanel)
	{
		PausePanel->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMandaMelaGameplayWidget::SetMatchHud(const FText& Status, const FText& Prompt)
{
	if (HudText)
	{
		HudText->SetText(Status);
	}
	if (PromptText)
	{
		PromptText->SetText(Prompt);
	}
	if (PromptPanel)
	{
		PromptPanel->SetVisibility(Prompt.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UMandaMelaGameplayWidget::SetInventorySlots(const FText& Slot1, const FText& Slot2, int32 SelectedSlot)
{
	const FText SlotTexts[] = { Slot1, Slot2 };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FString ItemName = SlotTexts[Index].ToString();
		const FName ItemTextName(*FString::Printf(TEXT("InventorySlotText%d"), Index));
		if (UTextBlock* ItemText = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(ItemTextName)) : nullptr)
		{
			ItemText->SetText(SlotTexts[Index]);
			ItemText->SetColorAndOpacity(FSlateColor(ItemName == TEXT("Empty") ? MandaMuted : MandaWhite));
		}
		const FName SlotBorderName(*FString::Printf(TEXT("InventorySlotBorder%d"), Index));
		if (UBorder* SlotBorder = WidgetTree ? Cast<UBorder>(WidgetTree->FindWidget(SlotBorderName)) : nullptr)
		{
			SlotBorder->SetBrushColor(Index == SelectedSlot ? MandaGold : MandaBlue);
		}
	}
}

void UMandaMelaGameplayWidget::SetGuessText(const FString& Digits)
{
	for (int32 Index = 0; Index < GuessDigitTexts.Num(); ++Index)
	{
		const FString Digit = Digits.IsValidIndex(Index) ? Digits.Mid(Index, 1) : TEXT("_");
		GuessDigitTexts[Index]->SetText(FText::FromString(Digit));
	}
}

void UMandaMelaGameplayWidget::SetFeedback(const FText& Feedback)
{
	if (FeedbackText)
	{
		FeedbackText->SetText(Feedback);
	}
}

void UMandaMelaGameplayWidget::SetSpectatorInfo(bool bVisible, const FText& TargetName, int32 CurrentTarget, int32 TargetCount)
{
	const ESlateVisibility GameplayVisibility = bVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (GuessPanel)
	{
		GuessPanel->SetVisibility(GameplayVisibility);
	}
	if (QuickbarPanel)
	{
		QuickbarPanel->SetVisibility(bVisible ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (PromptText)
	{
		PromptText->SetVisibility(GameplayVisibility);
	}
	if (SpectatorPanel)
	{
		SpectatorPanel->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (SpectatorTargetText)
	{
		const FString Name = TargetName.IsEmpty() ? TEXT("No players alive") : TargetName.ToString();
		const FString Position = TargetCount > 0 ? FString::Printf(TEXT("%s  /  %d OF %d"), *Name, CurrentTarget + 1, TargetCount) : Name;
		SpectatorTargetText->SetText(FText::FromString(Position));
	}
}

void UMandaMelaGameplayWidget::SetSpectatorDeathMessage(const FText& Message)
{
	if (SpectatorDeathText)
	{
		SpectatorDeathText->SetText(Message);
		SpectatorDeathText->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UMandaMelaGameplayWidget::SetDeathNotice(bool bVisible, const FText& Message)
{
	if (DeathNoticeText && !Message.IsEmpty())
	{
		DeathNoticeText->SetText(Message);
	}
	if (DeathNoticePanel)
	{
		DeathNoticePanel->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMandaMelaGameplayWidget::SetCameraPhoto(UTextureRenderTarget2D* Photo, const FText& TargetNumber)
{
	if (CameraPhotoImageWidget && Photo)
	{
		CameraPhotoImageWidget->SetBrushResourceObject(Photo);
	}
	if (CameraPhotoNumberText)
	{
		CameraPhotoNumberText->SetText(TargetNumber);
	}
	if (CameraPhotoPanel)
	{
		CameraPhotoPanel->SetVisibility(Photo ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (CameraPhotoPanel->GetParent())
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(CameraPhotoPanel->Slot))
			{
				// Position near lower right-centre
				CanvasSlot->SetAnchors(FAnchors(0.7f, 0.7f));
				CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			}
		}
	}
}

void UMandaMelaGameplayWidget::ShowCameraPhotoCard(UTextureRenderTarget2D* Photo, const FText& TargetNumber, float DisplayDuration)
{
	if (CameraPhotoImageWidget && Photo)
	{
		CameraPhotoImageWidget->SetBrushResourceObject(Photo);
	}
	if (CameraPhotoNumberText)
	{
		CameraPhotoNumberText->SetText(TargetNumber);
	}
	if (CameraPhotoPanel)
	{
		CameraPhotoPanel->SetRenderOpacity(0.0f);
		CameraPhotoPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(CameraPhotoPanel->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(0.7f, 0.7f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		}
		// Simple fade-in effect
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (CameraPhotoPanel)
			{
				CameraPhotoPanel->SetRenderOpacity(1.0f);
			}
		}));
		FTimerHandle HideHandle;
		GetWorld()->GetTimerManager().SetTimer(HideHandle, this, &UMandaMelaGameplayWidget::HideCameraPhotoCard, FMath::Max(1.0f, DisplayDuration), false);
	}
}

void UMandaMelaGameplayWidget::HideCameraPhotoCard()
{
	if (CameraPhotoPanel)
	{
		CameraPhotoPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMandaMelaGameplayWidget::StartFlashEffect(float Intensity, const FLinearColor& WarmTint)
{
	if (!FlashOverlay)
	{
		return;
	}
	FlashOverlayTexture = CreateFlashOverlayTexture(WarmTint);
	if (FlashOverlayTexture)
	{
		FSlateBrush FlashBrush;
		FlashBrush.SetResourceObject(FlashOverlayTexture);
		FlashBrush.ImageSize = FVector2D(512.0f, 512.0f);
		FlashOverlay->SetBrush(FlashBrush);
	}
	FlashOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	UpdateFlashEffect(Intensity);
}

void UMandaMelaGameplayWidget::UpdateFlashEffect(float Intensity)
{
	if (FlashOverlay)
	{
		FlashOverlay->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, FMath::Clamp(Intensity, 0.0f, 1.0f)));
	}
}

void UMandaMelaGameplayWidget::ClearFlashEffect()
{
	if (FlashOverlay)
	{
		FlashOverlay->SetVisibility(ESlateVisibility::Collapsed);
		FlashOverlay->SetColorAndOpacity(FLinearColor::White);
	}
}

void UMandaMelaGameplayWidget::SetBinocularsVisible(bool bVisible)
{
	if (BinocularsOverlay)
	{
		BinocularsOverlay->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		BinocularsOverlay->SetRenderOpacity(bVisible ? 1.0f : 0.0f);
	}
}

void UMandaMelaGameplayWidget::StartBinocularsFade(bool bFadeIn, float Duration)
{
	if (BinocularsOverlay)
	{
		if (bFadeIn)
		{
			BinocularsOverlay->SetRenderOpacity(0.0f);
			BinocularsOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
			GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (BinocularsOverlay) BinocularsOverlay->SetRenderOpacity(1.0f);
			}));
		}
		else
		{
			BinocularsOverlay->SetRenderOpacity(0.0f);
			FTimerHandle HideHandle;
			GetWorld()->GetTimerManager().SetTimer(HideHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (BinocularsOverlay) BinocularsOverlay->SetVisibility(ESlateVisibility::Collapsed);
			}), FMath::Max(0.1f, Duration), false);
		}
	}
}

void UMandaMelaGameplayWidget::AddKillFeedMessage(const FString& Message)
{
	for (int32 i = 0; i < KillFeedTexts.Num(); ++i)
	{
		if (KillFeedTexts[i])
		{
			KillFeedTexts[i]->SetText(FText::FromString(Message));
			KillFeedTexts[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
			FTimerHandle KillTimer;
			GetWorld()->GetTimerManager().SetTimer(KillTimer, FTimerDelegate::CreateWeakLambda(this, [this, i]()
			{
				if (i < KillFeedTexts.Num() && KillFeedTexts[i])
				{
					KillFeedTexts[i]->SetVisibility(ESlateVisibility::Collapsed);
				}
			}), 4.0f, false);
			break;
		}
	}
}

void UMandaMelaGameplayWidget::SetScoreboardVisible(bool bVisible)
{
	if (ScoreboardPanel)
	{
		ScoreboardPanel->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bVisible)
	{
		RefreshScoreboard();
	}
}

void UMandaMelaGameplayWidget::RefreshScoreboard()
{
	TArray<APlayerState*> Players;
	if (const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr)
	{
		for (APlayerState* Player : MatchState->PlayerArray)
		{
			if (Player)
			{
				Players.Add(Player);
			}
		}
	}

	bool bNeedsRebuild = Players.Num() != ScoreboardPlayers.Num();
	for (int32 Index = 0; !bNeedsRebuild && Index < Players.Num(); ++Index)
	{
		bNeedsRebuild = Players[Index] != ScoreboardPlayers[Index];
	}
	if (bNeedsRebuild)
	{
		RebuildScoreboard(Players);
	}

	for (int32 Index = 0; Index < ScoreboardPlayers.Num(); ++Index)
	{
		APlayerState* Player = ScoreboardPlayers[Index];
		const AMandaMelaPlayerState* MatchPlayer = Cast<AMandaMelaPlayerState>(Player);
		const bool bAlive = MatchPlayer && MatchPlayer->IsAlive();
		const bool bWaiting = MatchPlayer && !MatchPlayer->IsParticipating();
		const FString Name = Player && !Player->GetPlayerName().IsEmpty() ? Player->GetPlayerName() : TEXT("Player");
		const EMandaMelaTeam Team = MatchPlayer ? MatchPlayer->GetTeam() : EMandaMelaTeam::None;
		const FString TeamName = Team == EMandaMelaTeam::Red ? TEXT("RED") : (Team == EMandaMelaTeam::Blue ? TEXT("BLUE") : TEXT(""));
		const AMandaMelaGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AMandaMelaGameState>() : nullptr;
		const EMandaMelaMatchMode Mode = MatchState ? MatchState->GetMatchMode() : EMandaMelaMatchMode::Deathmatch;
		bool bCarryingFlag = false;
		if (MatchState && MatchPlayer && Mode == EMandaMelaMatchMode::CaptureTheFlag)
		{
			const EMandaMelaTeam EnemyTeam = Team == EMandaMelaTeam::Red ? EMandaMelaTeam::Blue : EMandaMelaTeam::Red;
			const AMandaMelaFlag* EnemyFlag = MatchState->GetFlagForTeam(EnemyTeam);
			bCarryingFlag = EnemyFlag && EnemyFlag->GetCarrier() == Cast<AMandaMelaCharacter>(MatchPlayer->GetPawn());
		}
		const FString Status = bWaiting ? TEXT("WAITING") : (!bAlive ? TEXT("ELIMINATED") : (bCarryingFlag ? TEXT("FLAG CARRIER") : TEXT("ALIVE")));
		const FString Score = !MatchPlayer ? TEXT("")
			: Mode == EMandaMelaMatchMode::CaptureTheFlag
				? FString::Printf(TEXT("CAPS %d"), MatchPlayer->GetCaptureCount())
				: FString::Printf(TEXT("ELIMS %d"), MatchPlayer->GetEliminationCount());
		ScoreboardPlayerNames[Index]->SetText(FText::FromString(Name));
		ScoreboardPlayerTeams[Index]->SetText(FText::FromString(TeamName));
		ScoreboardPlayerScores[Index]->SetText(FText::FromString(Score));
		ScoreboardPlayerStatuses[Index]->SetText(FText::FromString(Status));
		const FLinearColor RowColor = bAlive ? MandaWhite : MandaMuted;
		ScoreboardPlayerNames[Index]->SetColorAndOpacity(FSlateColor(RowColor));
		ScoreboardPlayerTeams[Index]->SetColorAndOpacity(FSlateColor(Team == EMandaMelaTeam::Red ? FLinearColor(0.95f, 0.30f, 0.25f) : (Team == EMandaMelaTeam::Blue ? MandaCyan : MandaMuted)));
		ScoreboardPlayerScores[Index]->SetColorAndOpacity(FSlateColor(MandaGold));
		ScoreboardPlayerStatuses[Index]->SetColorAndOpacity(FSlateColor(bAlive ? MandaCyan : MandaMuted));
		ScoreboardPlayerRows[Index]->SetRenderOpacity(bAlive || bWaiting ? 1.0f : 0.6f);
	}
}

void UMandaMelaGameplayWidget::RebuildScoreboard(const TArray<APlayerState*>& Players)
{
	if (!ScoreboardPlayerList)
	{
		return;
	}

	ScoreboardPlayerList->ClearChildren();
	ScoreboardPlayers.Reset();
	ScoreboardPlayerRows.Reset();
	ScoreboardPlayerNames.Reset();
	ScoreboardPlayerStatuses.Reset();
	ScoreboardPlayerTeams.Reset();
	ScoreboardPlayerScores.Reset();
	for (APlayerState* Player : Players)
	{
		if (!Player)
		{
			continue;
		}
		UBorder* Row = MakeCard(WidgetTree, FLinearColor(0.035f, 0.15f, 0.30f, 0.94f));
		Row->SetPadding(FMargin(12.0f, 8.0f));
		UHorizontalBox* RowContent = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Row->SetContent(RowContent);
		UTextBlock* Name = MakeLabel(WidgetTree, TEXT("Player"), 16, MandaWhite, FName("Bold"));
		UHorizontalBoxSlot* NameSlot = RowContent->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(FillSize());
		UTextBlock* Team = MakeLabel(WidgetTree, TEXT(""), 12, MandaMuted, FName("Bold"));
		UHorizontalBoxSlot* TeamSlot = RowContent->AddChildToHorizontalBox(Team);
		TeamSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		TeamSlot->SetPadding(FMargin(10.0f, 0.0f));
		UTextBlock* Score = MakeLabel(WidgetTree, TEXT(""), 12, MandaGold, FName("Bold"));
		UHorizontalBoxSlot* ScoreSlot = RowContent->AddChildToHorizontalBox(Score);
		ScoreSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ScoreSlot->SetPadding(FMargin(10.0f, 0.0f));
		UTextBlock* Status = MakeLabel(WidgetTree, TEXT("WAITING"), 13, MandaCyan, FName("Bold"));
		Status->SetJustification(ETextJustify::Right);
		RowContent->AddChildToHorizontalBox(Status);
		ScoreboardPlayerList->AddChild(Row);
		ScoreboardPlayers.Add(Player);
		ScoreboardPlayerRows.Add(Row);
		ScoreboardPlayerNames.Add(Name);
		ScoreboardPlayerStatuses.Add(Status);
		ScoreboardPlayerTeams.Add(Team);
		ScoreboardPlayerScores.Add(Score);
	}
}
