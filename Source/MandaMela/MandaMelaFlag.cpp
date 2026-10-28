// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaFlag.h"
#include "MandaMelaCharacter.h"
#include "MandaMelaGameMode.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMandaMelaFlag::AMandaMelaFlag()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TouchZone = CreateDefaultSubobject<USphereComponent>(TEXT("TouchZone"));
	TouchZone->SetupAttachment(SceneRoot);
	TouchZone->InitSphereRadius(135.0f);
	TouchZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TouchZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	TouchZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TouchZone->SetGenerateOverlapEvents(true);
	TouchZone->OnComponentBeginOverlap.AddDynamic(this, &AMandaMelaFlag::OnTouchBegin);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	PoleMesh->SetupAttachment(SceneRoot);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoleMesh->SetGenerateOverlapEvents(false);
	if (CubeMesh.Succeeded())
	{
		PoleMesh->SetStaticMesh(CubeMesh.Object);
	}
	PoleMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	PoleMesh->SetRelativeScale3D(FVector(0.07f, 0.07f, 1.4f));

	BannerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BannerMesh"));
	BannerMesh->SetupAttachment(SceneRoot);
	BannerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BannerMesh->SetGenerateOverlapEvents(false);
	if (CubeMesh.Succeeded())
	{
		BannerMesh->SetStaticMesh(CubeMesh.Object);
	}
	BannerMesh->SetRelativeLocation(FVector(34.0f, 0.0f, 112.0f));
	BannerMesh->SetRelativeScale3D(FVector(0.72f, 0.055f, 0.42f));

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FlagLabel"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 190.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(24.0f);
	Label->SetTextRenderColor(FColor::White);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetGenerateOverlapEvents(false);
}

void AMandaMelaFlag::BeginPlay()
{
	Super::BeginPlay();
	if (Team == EMandaMelaTeam::None)
	{
		HomeTransform = GetActorTransform();
		return;
	}
	RefreshPresentation();
}

const TCHAR* AMandaMelaFlag::GetStateText() const
{
	switch (FlagStatus)
	{
	case EMandaMelaFlagStatus::Home: return TEXT("HOME");
	case EMandaMelaFlagStatus::Carried: return TEXT("CARRIED");
	case EMandaMelaFlagStatus::Dropped: return TEXT("DROPPED");
	default: return TEXT("UNKNOWN");
	}
}

void AMandaMelaFlag::InitializeFlag(EMandaMelaTeam NewTeam, const FTransform& Home)
{
	if (!HasAuthority())
	{
		return;
	}
	Team = NewTeam;
	HomeTransform = Home;
	FlagStatus = EMandaMelaFlagStatus::Home;
	Carrier = nullptr;
	SetActorTransform(HomeTransform);
	RefreshPresentation();
	ForceNetUpdate();
}

void AMandaMelaFlag::SetCarriedBy(AMandaMelaCharacter* NewCarrier)
{
	if (!HasAuthority() || !NewCarrier)
	{
		return;
	}
	Carrier = NewCarrier;
	FlagStatus = EMandaMelaFlagStatus::Carried;
	RefreshPresentation();
	ForceNetUpdate();
}

void AMandaMelaFlag::DropAt(const FVector& Location)
{
	if (!HasAuthority())
	{
		return;
	}
	Carrier = nullptr;
	FlagStatus = EMandaMelaFlagStatus::Dropped;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocation(Location);
	RefreshPresentation();
	ForceNetUpdate();
}

void AMandaMelaFlag::ReturnHome()
{
	if (!HasAuthority())
	{
		return;
	}
	Carrier = nullptr;
	FlagStatus = EMandaMelaFlagStatus::Home;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(HomeTransform);
	RefreshPresentation();
	ForceNetUpdate();
}

void AMandaMelaFlag::OnRep_FlagState()
{
	RefreshPresentation();
}

void AMandaMelaFlag::OnTouchBegin(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!HasAuthority())
	{
		return;
	}
	if (AMandaMelaCharacter* Character = Cast<AMandaMelaCharacter>(OtherActor))
	{
		if (AMandaMelaGameMode* GameMode = GetWorld()->GetAuthGameMode<AMandaMelaGameMode>())
		{
			GameMode->HandleFlagTouch(this, Character);
		}
	}
}

void AMandaMelaFlag::RefreshPresentation()
{
	const bool bCarried = FlagStatus == EMandaMelaFlagStatus::Carried && Carrier;
	if (bCarried)
	{
		AttachToComponent(Carrier->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SetActorRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
		SetActorRelativeRotation(FRotator::ZeroRotator);
		TouchZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		TouchZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (FlagStatus == EMandaMelaFlagStatus::Home && HasAuthority())
		{
			SetActorTransform(HomeTransform);
		}
	}

	const bool bRed = Team == EMandaMelaTeam::Red;
	const FColor TeamColor = bRed ? FColor(235, 75, 65) : FColor(65, 175, 245);
	const TCHAR* TeamName = bRed ? TEXT("RED") : TEXT("BLUE");
	const TCHAR* StateName = GetStateText();
	Label->SetText(FText::FromString(FString::Printf(TEXT("%s FLAG  /  %s"), TeamName, StateName)));
	Label->SetTextRenderColor(TeamColor);
	ForceNetUpdate();
}

void AMandaMelaFlag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaFlag, Team);
	DOREPLIFETIME(AMandaMelaFlag, FlagStatus);
	DOREPLIFETIME(AMandaMelaFlag, Carrier);
}
