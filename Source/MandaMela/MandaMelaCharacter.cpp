// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandaMelaCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "MandaMela.h"
#include "MandaMelaMatchState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	USkeletalMesh* LoadCreativeMesh(const TCHAR* AssetPath)
	{
		return LoadObject<USkeletalMesh>(nullptr, AssetPath);
	}

	// Retargeted from /Game/Characters/Mannequins/Anims/Unarmed for the Creative character skeleton.
	UClass* GetCreativeUprightAnimationBlueprintClass()
	{
		return LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Creative_Characters_FREE/Animations/Retargeted_Mannequin/AnimBlueprint/ABP_Unarmed_Creative_UprightIdle.ABP_Unarmed_Creative_UprightIdle_C"));
	}
}

AMandaMelaCharacter::AMandaMelaCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.08f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 145.f;
	GetCharacterMovement()->SetCrouchedHalfHeight(60.f);
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->bCrouchMaintainsBaseLocation = true;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 2200.0f;
	GetCharacterMovement()->FallingLateralFriction = 2.0f;

	// Keep the gameplay camera in the usual third-person position.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = false;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	GetMesh()->SetOwnerNoSee(false);

	HeadNumberText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HeadNumberText"));
	HeadNumberText->SetupAttachment(RootComponent);
	HeadNumberText->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	HeadNumberText->SetRelativeRotation(FRotator::ZeroRotator);
	HeadNumberText->SetHorizontalAlignment(EHTA_Center);
	HeadNumberText->SetWorldSize(24.0f);
	HeadNumberText->SetTextRenderColor(FColor(255, 220, 90));
	HeadNumberText->SetText(FText::GetEmpty());
	HeadNumberText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadNumberText->SetOwnerNoSee(false);

	OutfitMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CreativeOutfit"));
	OutfitMesh->SetupAttachment(GetMesh());
	OutfitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OutfitMesh->SetGenerateOverlapEvents(false);
	OutfitMesh->SetOwnerNoSee(false);

	FaceMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CreativeFace"));
	FaceMesh->SetupAttachment(GetMesh());
	FaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FaceMesh->SetGenerateOverlapEvents(false);
	FaceMesh->SetOwnerNoSee(false);

	HeadAccessoryMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CreativeHeadAccessory"));
	HeadAccessoryMesh->SetupAttachment(GetMesh());
	HeadAccessoryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadAccessoryMesh->SetGenerateOverlapEvents(false);
	HeadAccessoryMesh->SetOwnerNoSee(false);

	HeldTacticMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeldTacticItem"));
	HeldTacticMesh->SetupAttachment(GetMesh(), TEXT("RightHand"));
	HeldTacticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldTacticMesh->SetGenerateOverlapEvents(false);
	HeldTacticMesh->SetVisibility(false);

	HeldCameraMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldCameraItem"));
	HeldCameraMesh->SetupAttachment(GetMesh(), TEXT("RightHand"));
	HeldCameraMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldCameraMesh->SetGenerateOverlapEvents(false);
	HeldCameraMesh->SetVisibility(false);

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AMandaMelaCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bCrouchQueuedForLanding)
	{
		if (IsEliminated())
		{
			bCrouchQueuedForLanding = false;
		}
		else if (!GetCharacterMovement()->IsFalling())
		{
			Crouch();
			bCrouchQueuedForLanding = GetCharacterMovement()->IsFalling() || !GetCharacterMovement()->IsCrouching();
		}
	}
	if (IsEliminated() && !bDeathAnimationPlaying)
	{
		ApplyEliminatedState();
	}
	if (bUsingCreativeCharacter && !bDeathAnimationPlaying)
	{
		UpdateTacticalAnimation();
	}
	UpdateTacticalMeshMotion();
	UpdateHeadNumberVisibility();
}

void AMandaMelaCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Enforce third-person visibility even when this character is spawned from a Blueprint.
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	GetMesh()->SetOwnerNoSee(false);
	HeadNumberText->SetOwnerNoSee(false);
	OutfitMesh->SetOwnerNoSee(false);
	FaceMesh->SetOwnerNoSee(false);
	HeadAccessoryMesh->SetOwnerNoSee(false);
	OriginalMeshCollisionProfile = GetMesh()->GetCollisionProfileName();
	OriginalCreativeMeshScale = GetMesh()->GetRelativeScale3D();
	HeadNumberText->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	HeadNumberText->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	HeadNumberText->SetRelativeRotation(FRotator::ZeroRotator);
	ApplyCreativeAppearance();
	// Keep first-use animation loading/compression out of pickup and throw actions.
	PreloadTacticalAnimations();
	UpdateHeldTacticalItem();
	RefreshHeadNumber();
	ApplyEliminatedState();
}

void AMandaMelaCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (HasAuthority())
	{
		if (const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>(); MatchPlayerState && MatchPlayerState->GetSecretNumber() != INDEX_NONE)
		{
			SetPublicPlayerNumber(MatchPlayerState->GetSecretNumber());
		}
	}
	RefreshHeadNumber();
}

void AMandaMelaCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshHeadNumber();
	ApplyEliminatedState();
}

void AMandaMelaCharacter::SetPublicPlayerNumber(int32 Number)
{
	if (HasAuthority())
	{
		PublicPlayerNumber = Number;
		ForceNetUpdate();
		RefreshHeadNumber();
	}
}

FString AMandaMelaCharacter::GetFormattedPublicPlayerNumber() const
{
	return PublicPlayerNumber == INDEX_NONE ? FString() : FString::Printf(TEXT("%04d"), PublicPlayerNumber);
}

FVector AMandaMelaCharacter::GetPublicPlayerNumberLocation() const
{
	return HeadNumberText ? HeadNumberText->GetComponentLocation() : GetActorLocation() + FVector::UpVector * 90.0f;
}

bool AMandaMelaCharacter::IsPublicPlayerNumberFacing(const FVector& ViewerLocation) const
{
	const FVector ToViewer = (ViewerLocation - GetActorLocation()).GetSafeNormal2D();
	return PublicPlayerNumber != INDEX_NONE && !IsEliminated()
		&& FVector::DotProduct(GetActorForwardVector(), ToViewer) > 0.1f;
}

void AMandaMelaCharacter::OnRep_PublicPlayerNumber()
{
	RefreshHeadNumber();
}

void AMandaMelaCharacter::OnRep_CharacterAppearance()
{
	if (HasActorBegunPlay())
	{
		ApplyCreativeAppearance();
	}
}

void AMandaMelaCharacter::OnRep_HeldTacticalItem()
{
	if (TacticalMeshMotion == 0)
	{
		UpdateHeldTacticalItem();
	}
}

void AMandaMelaCharacter::RefreshHeadNumber()
{
	if (!HeadNumberText)
	{
		return;
	}
	HeadNumberText->SetText(PublicPlayerNumber == INDEX_NONE
		? FText::GetEmpty()
		: FText::FromString(GetFormattedPublicPlayerNumber()));
	UpdateHeadNumberVisibility();
}

void AMandaMelaCharacter::UpdateHeadNumberVisibility()
{
	if (!HeadNumberText)
	{
		return;
	}

	bool bShowNumber = PublicPlayerNumber != INDEX_NONE && !IsEliminated();
	APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (bShowNumber && Viewer)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
		bShowNumber = IsPublicPlayerNumberFacing(ViewLocation);
	}
	else
	{
		bShowNumber = false;
	}

	if (HeadNumberText->IsVisible() != bShowNumber)
	{
		HeadNumberText->SetVisibility(bShowNumber);
	}
}

bool AMandaMelaCharacter::IsEliminated() const
{
	const AMandaMelaPlayerState* MatchPlayerState = GetPlayerState<AMandaMelaPlayerState>();
	return MatchPlayerState && MatchPlayerState->IsParticipating() && !MatchPlayerState->IsAlive();
}

void AMandaMelaCharacter::GetBinocularVisualComponents(TArray<UPrimitiveComponent*>& OutComponents) const
{
	OutComponents.Reset();
	OutComponents.Reserve(7);
	OutComponents.Add(GetMesh());
	OutComponents.Add(FaceMesh);
	OutComponents.Add(OutfitMesh);
	OutComponents.Add(HeadAccessoryMesh);
	OutComponents.Add(HeadNumberText);
	OutComponents.Add(HeldTacticMesh);
	OutComponents.Add(HeldCameraMesh);
}

void AMandaMelaCharacter::RefreshBinocularVisuals()
{
	UpdateHeadNumberVisibility();
	UpdateHeldTacticalItem();
}

void AMandaMelaCharacter::SetCharacterAppearance(int32 AppearanceIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	CharacterAppearanceIndex = FMath::Clamp(AppearanceIndex, 0, 4);
	ApplyCreativeAppearance();
	ForceNetUpdate();
}

void AMandaMelaCharacter::ApplyCreativeAppearance()
{
	if (bDeathAnimationPlaying)
	{
		return;
	}

	USkeletalMesh* Body = LoadCreativeMesh(TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Body_010.SK_Body_010"));
	if (!Body)
	{
		UE_LOG(LogTemp, Warning, TEXT("Creative Characters body mesh could not be loaded; keeping the Blueprint mesh."));
		return;
	}

	static const TCHAR* OutfitPaths[] = {
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_T_Shirt_009.SK_T_Shirt_009"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Costume_10_001.SK_Costume_10_001"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Outerwear_029.SK_Outerwear_029"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Costume_6_001.SK_Costume_6_001"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Outerwear_036.SK_Outerwear_036")
	};
	static const TCHAR* AccessoryPaths[] = {
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Glasses_004.SK_Glasses_004"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Hairstyle_male_012.SK_Hairstyle_male_012"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Hat_049.SK_Hat_049"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Hairstyle_male_010.SK_Hairstyle_male_010"),
		TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Headphones_002.SK_Headphones_002")
	};

	const int32 Style = FMath::Clamp(CharacterAppearanceIndex, 0, 4);
	StopCrouchAnimation(0.2f);
	GetMesh()->SetSkeletalMeshAsset(Body);
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	if (UClass* CreativeAnimationClass = GetCreativeUprightAnimationBlueprintClass())
	{
		GetMesh()->SetAnimInstanceClass(CreativeAnimationClass);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Retargeted Creative character animation Blueprint could not be loaded."));
	}
	bUsingCreativeCharacter = true;
	if (OutfitMesh)
	{
		OutfitMesh->SetSkeletalMeshAsset(LoadCreativeMesh(OutfitPaths[Style]));
		OutfitMesh->SetLeaderPoseComponent(GetMesh());
		OutfitMesh->SetVisibility(OutfitMesh->GetSkeletalMeshAsset() != nullptr, true);
	}
	if (HeadAccessoryMesh)
	{
		HeadAccessoryMesh->SetSkeletalMeshAsset(LoadCreativeMesh(AccessoryPaths[Style]));
		HeadAccessoryMesh->SetLeaderPoseComponent(GetMesh());
		HeadAccessoryMesh->SetVisibility(HeadAccessoryMesh->GetSkeletalMeshAsset() != nullptr, true);
	}
	if (FaceMesh)
	{
		FaceMesh->SetSkeletalMeshAsset(LoadCreativeMesh(TEXT("/Game/Creative_Characters_FREE/Skeleton_Meshes/SK_Male_emotion_usual_001.SK_Male_emotion_usual_001")));
		FaceMesh->SetLeaderPoseComponent(GetMesh());
		FaceMesh->SetVisibility(FaceMesh->GetSkeletalMeshAsset() != nullptr, true);
	}
}

void AMandaMelaCharacter::UpdateCrouchAnimation()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh)
	{
		return;
	}

	if (!CharacterMesh->GetRelativeScale3D().Equals(OriginalCreativeMeshScale, KINDA_SMALL_NUMBER))
	{
		CharacterMesh->SetRelativeScale3D(OriginalCreativeMeshScale);
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement->IsFalling())
	{
		if (Movement->IsCrouching())
		{
			UnCrouch();
		}
		StopCrouchAnimation(0.2f);
		return;
	}
	if (!Movement->IsCrouching())
	{
		StopCrouchAnimation(0.2f);
		return;
	}

	// Separate enter/exit speeds keep the crouch gait from flickering around zero speed.
	constexpr float CrouchWalkStartSpeed = 18.0f;
	constexpr float CrouchWalkStopSpeed = 8.0f;
	const bool bWasPlayingWalk = ActiveCrouchAnimation && ActiveCrouchAnimation == CrouchWalkAnimation;
	const float WalkSpeedThreshold = bWasPlayingWalk ? CrouchWalkStopSpeed : CrouchWalkStartSpeed;
	const bool bShouldPlayWalk = GetVelocity().Size2D() > WalkSpeedThreshold;
	TObjectPtr<UAnimSequence>& Animation = bShouldPlayWalk ? CrouchWalkAnimation : CrouchIdleAnimation;
	if (!Animation)
	{
		const TCHAR* AssetPath = bShouldPlayWalk
			? TEXT("/Game/Creative_Characters_FREE/Animations/Retargeted_Mannequin/Crouch/MOB1_CrouchWalk_F_IPC_Creative.MOB1_CrouchWalk_F_IPC_Creative")
			: TEXT("/Game/Creative_Characters_FREE/Animations/Retargeted_Mannequin/Crouch/MOB1_Crouch_Idle_V2_IPC_Creative.MOB1_Crouch_Idle_V2_IPC_Creative");
		Animation = LoadObject<UAnimSequence>(nullptr, AssetPath);
	}
	if (!Animation)
	{
		return;
	}

	UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}
	if (ActiveCrouchAnimation == Animation && ActiveCrouchMontage && AnimInstance->Montage_IsActive(ActiveCrouchMontage))
	{
		return;
	}

	UAnimMontage* CrouchMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Animation,
		TEXT("DefaultSlot"),
		0.22f, // Blend into crouch or crossfade between idle and walk.
		0.2f,
		1.0f,
		1);
	if (CrouchMontage)
	{
		AnimInstance->Montage_SetNextSection(TEXT("Default"), TEXT("Default"), CrouchMontage);
		ActiveCrouchAnimation = Animation;
		ActiveCrouchMontage = CrouchMontage;
	}
}

void AMandaMelaCharacter::StopCrouchAnimation(float BlendOutTime)
{
	if (ActiveCrouchMontage)
	{
		if (USkeletalMeshComponent* CharacterMesh = GetMesh())
		{
			if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance(); AnimInstance && AnimInstance->Montage_IsActive(ActiveCrouchMontage))
			{
				AnimInstance->Montage_Stop(FMath::Max(BlendOutTime, 0.0f), ActiveCrouchMontage);
			}
		}
	}
	ActiveCrouchMontage = nullptr;
	ActiveCrouchAnimation = nullptr;
}

void AMandaMelaCharacter::SetHeldTacticalItem(EMandaMelaItemType ItemType)
{
	if (!HasAuthority())
	{
		return;
	}
	HeldTacticalItemType = ItemType;
	UpdateHeldTacticalItem();
	ForceNetUpdate();
}

void AMandaMelaCharacter::UpdateHeldTacticalItem()
{
	if (!HeldTacticMesh || !HeldCameraMesh || TacticalMeshMotion != 0)
	{
		return;
	}
	SetHeldTacticalMesh(HeldTacticalItemType);
	const bool bShowItem = HeldTacticalItemType != EMandaMelaItemType::None && !IsEliminated();
	if (bShowItem)
	{
		AttachHeldTacticalMesh(HeldTacticalItemType);
	}
	const bool bIsCamera = HeldTacticalItemType == EMandaMelaItemType::Camera;
	HeldTacticMesh->SetVisibility(bShowItem && !bIsCamera, true);
	HeldCameraMesh->SetVisibility(bShowItem && bIsCamera && HeldCameraMesh->GetStaticMesh() != nullptr, true);
}

void AMandaMelaCharacter::PreloadTacticalAnimations()
{
	CameraUseAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Tactics/camera/Camera_GameReady/Camera_Use_Animation_Anim.Camera_Use_Animation_Anim"));
	CameraUseSkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Tactics/camera/Camera_GameReady/Camera_Use_Animation.Camera_Use_Animation"));
	if (bUsingCreativeCharacter && !TacticalThrowAnimation)
	{
		TacticalThrowAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Creative_Characters_FREE/Animations/Retargeted_Mannequin/ItemActions/Tactic_MM_Pistol_Fire.Tactic_MM_Pistol_Fire"));
	}
}

void AMandaMelaCharacter::SetHeldTacticalMesh(EMandaMelaItemType ItemType)
{
	if (!HeldTacticMesh)
	{
		return;
	}
	if (ItemType == EMandaMelaItemType::Camera)
	{
		HeldTacticMesh->SetSkeletalMesh(nullptr);
		HeldTacticMesh->SetVisibility(false, true);
		if (!HeldCameraMesh)
		{
			return;
		}

		UStaticMesh* Model = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Tactics/camera/Camera_GameReady/Camera_GameReady.Camera_GameReady"));
		HeldCameraMesh->SetStaticMesh(Model);
		if (!Model)
		{
			HeldCameraMesh->SetVisibility(false, true);
			return;
		}

		const FBoxSphereBounds Bounds = Model->GetBounds();
		const float LongestDimension = Bounds.BoxExtent.GetAbs().GetMax() * 2.0f;
		const float Scale = LongestDimension > KINDA_SMALL_NUMBER ? 24.0f / LongestDimension : 1.0f;
		HeldCameraMesh->SetRelativeScale3D(FVector(Scale));
		HeldCameraMesh->SetRelativeRotation(FRotator::ZeroRotator);
		HeldCameraMesh->SetRelativeLocation(FVector(5.0f, 0.0f, 0.0f) - Bounds.Origin * Scale);
		HeldCameraMesh->SetVisibility(false, true);
		return;
	}
	if (HeldCameraMesh)
	{
		HeldCameraMesh->SetVisibility(false, true);
	}
	const TCHAR* AssetPath = nullptr;
	float TargetSize = 0.0f;
	switch (ItemType)
	{
	case EMandaMelaItemType::Binoculars:
		AssetPath = TEXT("/Game/Tactics/binocular1.binocular1");
		TargetSize = 24.0f;
		break;
	case EMandaMelaItemType::Flashbang:
		AssetPath = TEXT("/Game/Tactics/flashbang.flashbang");
		TargetSize = 18.0f;
		break;
	case EMandaMelaItemType::SmokeBomb:
		AssetPath = TEXT("/Game/Tactics/smoke.smoke");
		TargetSize = 21.0f;
		break;
	default:
		HeldTacticMesh->SetSkeletalMesh(nullptr);
		HeldTacticMesh->SetVisibility(false, true);
		return;
	}

	USkeletalMesh* Model = LoadObject<USkeletalMesh>(nullptr, AssetPath);
	HeldTacticMesh->SetSkeletalMesh(Model);
	if (!Model)
	{
		HeldTacticMesh->SetVisibility(false, true);
		return;
	}
	HeldTacticMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const float LongestDimension = Bounds.BoxExtent.GetAbs().GetMax() * 2.0f;
	const float Scale = LongestDimension > KINDA_SMALL_NUMBER ? TargetSize / LongestDimension : 1.0f;
	HeldTacticMesh->SetRelativeScale3D(FVector(Scale));
	HeldTacticMesh->SetRelativeRotation(FRotator::ZeroRotator);
	HeldTacticMesh->SetRelativeLocation(FVector(5.0f, 0.0f, 0.0f) - Bounds.Origin * Scale);
}

void AMandaMelaCharacter::AttachHeldTacticalMesh(EMandaMelaItemType ItemType)
{
	if (!GetMesh())
	{
		return;
	}
	if (ItemType == EMandaMelaItemType::Camera)
	{
		if (HeldCameraMesh)
		{
			HeldCameraMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("RightHand"));
			SetHeldTacticalMesh(ItemType);
		}
		return;
	}
	if (!HeldTacticMesh)
	{
		return;
	}
	if (HeldCameraMesh)
	{
		HeldCameraMesh->SetVisibility(false, true);
	}
	HeldTacticMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("RightHand"));
	SetHeldTacticalMesh(ItemType);
}

FVector AMandaMelaCharacter::GetTacticalHandLocation() const
{
	if (!GetMesh())
	{
		return GetActorLocation();
	}
	if (!HeldTacticMesh || !HeldTacticMesh->GetSkeletalMeshAsset())
	{
		return GetMesh()->GetSocketLocation(TEXT("RightHand"));
	}
	const FBoxSphereBounds Bounds = HeldTacticMesh->GetSkeletalMeshAsset()->GetBounds();
	const FVector Scale = HeldTacticMesh->GetRelativeScale3D();
	const FVector LocalMeshOrigin = FVector(5.0f, 0.0f, 0.0f) - Bounds.Origin * Scale;
	return GetMesh()->GetSocketTransform(TEXT("RightHand")).TransformPosition(LocalMeshOrigin);
}

void AMandaMelaCharacter::MulticastPlayTacticalPickup_Implementation(EMandaMelaItemType ItemType, FVector PickupLocation)
{
	SetHeldTacticalMesh(ItemType);
	StopCrouchAnimation(0.12f);
	if (ItemType == EMandaMelaItemType::Camera)
	{
		AttachHeldTacticalMesh(ItemType);
		if (HeldCameraMesh)
		{
			HeldCameraMesh->SetVisibility(HeldCameraMesh->GetStaticMesh() != nullptr, true);
		}
		return;
	}
	if (!HeldTacticMesh || !HeldTacticMesh->GetSkeletalMeshAsset())
	{
		return;
	}
	HeldTacticMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	HeldTacticMesh->SetWorldLocation(PickupLocation);
	HeldTacticMesh->SetVisibility(true, true);
	TacticalMeshMotion = 1;
	TacticalMeshMotionItemType = ItemType;
	TacticalMeshMotionStartTime = GetWorld()->GetTimeSeconds();
	TacticalMeshMotionDuration = 0.34f;
	TacticalMeshMotionStart = PickupLocation;
}

void AMandaMelaCharacter::MulticastPlayTacticalThrow_Implementation(EMandaMelaItemType ItemType, FVector TargetLocation, bool bAnimateItemFlight)
{
	StopCrouchAnimation(0.12f);
	PlayTacticalThrowAnimation();
	if (!bAnimateItemFlight)
	{
		return;
	}
	SetHeldTacticalMesh(ItemType);
	if (!HeldTacticMesh || !HeldTacticMesh->GetSkeletalMeshAsset())
	{
		return;
	}
	TacticalMeshMotion = 2;
	TacticalMeshMotionItemType = ItemType;
	TacticalMeshMotionStartTime = GetWorld()->GetTimeSeconds();
	TacticalMeshMotionDuration = 0.38f;
	TacticalMeshMotionStart = GetTacticalHandLocation();
	TacticalMeshMotionTarget = TargetLocation;
	HeldTacticMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	HeldTacticMesh->SetWorldLocation(TacticalMeshMotionStart);
	HeldTacticMesh->SetVisibility(true, true);
}

void AMandaMelaCharacter::PlayTacticalThrowAnimation()
{
	if (!bUsingCreativeCharacter || !GetMesh())
	{
		return;
	}
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}
	if (ActiveTacticalActionMontage && AnimInstance->Montage_IsActive(ActiveTacticalActionMontage))
	{
		AnimInstance->Montage_Stop(0.1f, ActiveTacticalActionMontage);
	}
	if (TacticalThrowAnimation)
	{
		ActiveTacticalActionMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(TacticalThrowAnimation, TEXT("DefaultSlot"), 0.18f, 0.18f, 1.0f, 1);
	}
}

void AMandaMelaCharacter::UpdateTacticalAnimation()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Movement || !AnimInstance)
	{
		return;
	}
	if (ActiveTacticalActionMontage && !AnimInstance->Montage_IsActive(ActiveTacticalActionMontage))
	{
		ActiveTacticalActionMontage = nullptr;
	}
	if (ActiveTacticalActionMontage)
	{
		return;
	}
	if (HeldTacticalItemType == EMandaMelaItemType::None && !Movement->IsCrouching()
		&& !ActiveCrouchMontage && !ActiveCrouchAnimation)
	{
		return;
	}

	const bool bGrounded = !Movement->IsFalling();
	if (!bGrounded && Movement->IsCrouching())
	{
		UnCrouch();
	}
	const bool bCrouched = Movement->IsCrouching() && bGrounded;
	if (bCrouched)
	{
		UpdateCrouchAnimation();
	}
	else
	{
		StopCrouchAnimation(0.16f);
	}
}

void AMandaMelaCharacter::UpdateTacticalMeshMotion()
{
	if (TacticalMeshMotion == 0 || !HeldTacticMesh || !GetWorld())
	{
		return;
	}
	const float Alpha = FMath::Clamp((GetWorld()->GetTimeSeconds() - TacticalMeshMotionStartTime) / TacticalMeshMotionDuration, 0.0f, 1.0f);
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	if (TacticalMeshMotion == 1)
	{
		const FVector HandLocation = GetTacticalHandLocation();
		HeldTacticMesh->SetWorldLocation(FMath::Lerp(TacticalMeshMotionStart, HandLocation, SmoothAlpha));
		if (Alpha >= 1.0f)
		{
			TacticalMeshMotion = 0;
			AttachHeldTacticalMesh(TacticalMeshMotionItemType);
			HeldTacticMesh->SetVisibility(true, true);
		}
		return;
	}

	FVector FlightLocation = FMath::Lerp(TacticalMeshMotionStart, TacticalMeshMotionTarget, SmoothAlpha);
	FlightLocation.Z += 70.0f * 4.0f * Alpha * (1.0f - Alpha);
	HeldTacticMesh->SetWorldLocation(FlightLocation);
	if (Alpha >= 1.0f)
	{
		TacticalMeshMotion = 0;
		UpdateHeldTacticalItem();
	}
}

void AMandaMelaCharacter::ToggleCrouch()
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()); PlayerController && PlayerController->IsMoveInputIgnored())
	{
		return;
	}
	if (IsEliminated() || !bUsingCreativeCharacter)
	{
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement->IsFalling())
	{
		bCrouchQueuedForLanding = !bCrouchQueuedForLanding;
		return;
	}

	bCrouchQueuedForLanding = false;
	if (Movement->IsCrouching())
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void AMandaMelaCharacter::ApplyEliminatedState()
{
	if (IsEliminated())
	{
		GetCharacterMovement()->DisableMovement();
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (HeadNumberText)
		{
			HeadNumberText->SetVisibility(false);
		}

		if (!bDeathAnimationPlaying)
		{
			StopCrouchAnimation(0.15f);
			if (ActiveTacticalActionMontage && GetMesh())
			{
				if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance(); AnimInstance && AnimInstance->Montage_IsActive(ActiveTacticalActionMontage))
				{
					AnimInstance->Montage_Stop(0.15f, ActiveTacticalActionMontage);
				}
			}
			ActiveTacticalActionMontage = nullptr;
			TacticalMeshMotion = 0;
			if (HeldTacticMesh)
			{
				HeldTacticMesh->SetVisibility(false, true);
			}
			if (HeldCameraMesh)
			{
				HeldCameraMesh->SetVisibility(false, true);
			}
			GetMesh()->SetRelativeScale3D(OriginalCreativeMeshScale);
			GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
			GetMesh()->SetAllBodiesSimulatePhysics(true);
			GetMesh()->SetSimulatePhysics(true);
			GetMesh()->WakeAllRigidBodies();
			bDeathAnimationPlaying = true;
		}

		if (HasAuthority() && GetLifeSpan() <= 0.0f)
		{
			SetLifeSpan(4.0f);
		}
	}
	else if (bDeathAnimationPlaying)
	{
		RestoreFromElimination();
	}
}

void AMandaMelaCharacter::RestoreFromElimination()
{
	SetLifeSpan(0.0f);
	GetMesh()->SetAllBodiesSimulatePhysics(false);
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetCollisionProfileName(OriginalMeshCollisionProfile.IsNone() ? FName(TEXT("CharacterMesh")) : OriginalMeshCollisionProfile);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetVisibility(true, true);
	if (OutfitMesh) OutfitMesh->SetVisibility(true, true);
	if (FaceMesh) FaceMesh->SetVisibility(true, true);
	if (HeadAccessoryMesh) HeadAccessoryMesh->SetVisibility(true, true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UnCrouch();
	GetMesh()->SetRelativeScale3D(OriginalCreativeMeshScale);
	bDeathAnimationPlaying = false;
	ApplyCreativeAppearance();
	UpdateHeldTacticalItem();
	RefreshHeadNumber();
}

void AMandaMelaCharacter::ResetForRound(int32 Number)
{
	if (!HasAuthority())
	{
		return;
	}
	RestoreFromElimination();
	SetPublicPlayerNumber(Number);
	RefreshHeadNumber();
}

void AMandaMelaCharacter::MulticastPlayEliminationEffect_Implementation()
{
	BP_OnEliminationEffect();
}

float AMandaMelaCharacter::GetCameraUseAnimationDuration() const
{
	return CameraUseAnimation && CameraUseSkeletalMesh ? CameraUseAnimation->GetPlayLength() : 0.0f;
}

void AMandaMelaCharacter::MulticastPlayCameraUseAnimation_Implementation()
{
	if (!HeldTacticMesh || !HeldCameraMesh || !CameraUseAnimation || !CameraUseSkeletalMesh)
	{
		return;
	}

	HeldCameraMesh->SetVisibility(false, true);
	HeldTacticMesh->SetSkeletalMesh(CameraUseSkeletalMesh);
	const FBoxSphereBounds Bounds = CameraUseSkeletalMesh->GetBounds();
	const float LongestDimension = Bounds.BoxExtent.GetAbs().GetMax() * 2.0f;
	const float Scale = LongestDimension > KINDA_SMALL_NUMBER ? 24.0f / LongestDimension : 1.0f;
	HeldTacticMesh->SetRelativeScale3D(FVector(Scale));
	HeldTacticMesh->SetRelativeRotation(FRotator::ZeroRotator);
	HeldTacticMesh->SetRelativeLocation(FVector(5.0f, 0.0f, 0.0f) - Bounds.Origin * Scale);
	HeldTacticMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	HeldTacticMesh->SetAnimation(CameraUseAnimation);
	HeldTacticMesh->SetPosition(0.0f, false);
	HeldTacticMesh->SetVisibility(true, true);
	HeldTacticMesh->Play(false);
}

void AMandaMelaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMandaMelaCharacter, PublicPlayerNumber);
	DOREPLIFETIME(AMandaMelaCharacter, CharacterAppearanceIndex);
	DOREPLIFETIME(AMandaMelaCharacter, HeldTacticalItemType);
}

void AMandaMelaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMandaMelaCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMandaMelaCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMandaMelaCharacter::Look);
	}
	else
	{
		UE_LOG(LogMandaMela, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AMandaMelaCharacter::ToggleCrouch);
}

void AMandaMelaCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMandaMelaCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMandaMelaCharacter::DoMove(float Right, float Forward)
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()); PlayerController && PlayerController->IsMoveInputIgnored()) return;
	if (GetController() != nullptr && !IsEliminated())
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AMandaMelaCharacter::DoLook(float Yaw, float Pitch)
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()); PlayerController && PlayerController->IsLookInputIgnored()) return;
	if (GetController() != nullptr && !IsEliminated())
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMandaMelaCharacter::DoJumpStart()
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()); PlayerController && PlayerController->IsMoveInputIgnored()) return;
	// signal the character to jump
	if (!IsEliminated())
	{
		bCrouchQueuedForLanding = false;
		if (GetCharacterMovement()->IsCrouching())
		{
			UnCrouch();
		}
		StopCrouchAnimation(0.2f);
		Jump();
	}
}

void AMandaMelaCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
