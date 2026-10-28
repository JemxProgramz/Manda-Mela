#include "MandaMelaPickup.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMandaMelaPickupTacticalModelsTest,
	"MandaMela.Pickups.TacticalModelsAreConfigured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMandaMelaPickupTacticalModelsTest::RunTest(const FString& Parameters)
{
	const AMandaMelaPickup* DefaultPickup = GetDefault<AMandaMelaPickup>();
	const UClass* PickupClass = AMandaMelaPickup::StaticClass();
	const TPair<const TCHAR*, const TCHAR*> Models[] =
	{
		{ TEXT("BinocularsModel"), TEXT("/Game/Tactics/binocular1.binocular1") },
		{ TEXT("FlashbangModel"), TEXT("/Game/Tactics/flashbang.flashbang") },
		{ TEXT("SmokeBombModel"), TEXT("/Game/Tactics/smoke.smoke") }
	};

	bool bPassed = true;
	for (const TPair<const TCHAR*, const TCHAR*>& Model : Models)
	{
		const FString PropertyName(Model.Key);
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(PickupClass, *PropertyName);
		if (!TestNotNull(*FString::Printf(TEXT("%s property exists"), Model.Key), Property))
		{
			bPassed = false;
			continue;
		}

		UObject* Asset = Property->GetObjectPropertyValue_InContainer(DefaultPickup);
		if (!TestNotNull(*FString::Printf(TEXT("%s has a default mesh"), Model.Key), Asset))
		{
			bPassed = false;
			continue;
		}

		bPassed &= TestEqual(
			*FString::Printf(TEXT("%s uses the matching Tactics mesh"), Model.Key),
			Asset->GetPathName(),
			FString(Model.Value));
	}

	return bPassed;
}

#endif
