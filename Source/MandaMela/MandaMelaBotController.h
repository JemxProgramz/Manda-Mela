// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MandaMelaBotController.generated.h"

UCLASS()
class AMandaMelaBotController : public AAIController
{
	GENERATED_BODY()

public:
	AMandaMelaBotController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const override;

private:
	static constexpr float AttackInterval = 2.5f;
	static constexpr float AttackRange = 900.0f;
	float NextAttackTime = 0.0f;
};
