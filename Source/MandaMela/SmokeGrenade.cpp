// d:/Unreal GameDev/MandaMela/Source/MandaMela/SmokeGrenade.cpp
#include "SmokeGrenade.h"

ASmokeGrenade::ASmokeGrenade()
{
}

void ASmokeGrenade::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	SetItemType(EMandaMelaItemType::SmokeBomb);
}
