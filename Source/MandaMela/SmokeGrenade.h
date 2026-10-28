// d:/Unreal GameDev/MandaMela/Source/MandaMela/SmokeGrenade.h
#pragma once

#include "CoreMinimal.h"
#include "MandaMelaPickup.h"
#include "SmokeGrenade.generated.h"

/**
 * Smoke-grenade pickup subclass.
 * When picked up, its ItemType is set to SmokeBomb so the GameMode
 * spawns the correct AMandaMelaSmokeCloud via the existing tactical-use flow.
 */
UCLASS()
class MANDAMELA_API ASmokeGrenade : public AMandaMelaPickup
{
	GENERATED_BODY()

public:
	ASmokeGrenade();

protected:
	virtual void PostInitializeComponents() override;
};
