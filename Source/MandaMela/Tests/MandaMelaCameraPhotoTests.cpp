#include "MandaMelaGameMode.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMandaMelaCameraPhotoAnswerTest,
	"MandaMela.CameraPhoto.AnswerValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMandaMelaCameraPhotoAnswerTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Correct four-digit answer matches the captured number"),
		AMandaMelaGameMode::IsCameraPhotoAnswerCorrect(TEXT("0015"), 15));
	TestFalse(TEXT("Incorrect answer does not match the captured number"),
		AMandaMelaGameMode::IsCameraPhotoAnswerCorrect(TEXT("0016"), 15));
	TestFalse(TEXT("Short answer is rejected"),
		AMandaMelaGameMode::IsCameraPhotoAnswerCorrect(TEXT("015"), 15));
	TestFalse(TEXT("Non-digit answer is rejected"),
		AMandaMelaGameMode::IsCameraPhotoAnswerCorrect(TEXT("0A15"), 15));
	TestFalse(TEXT("Out-of-range target number is rejected"),
		AMandaMelaGameMode::IsCameraPhotoAnswerCorrect(TEXT("9999"), INDEX_NONE));
	return true;
}

#endif
