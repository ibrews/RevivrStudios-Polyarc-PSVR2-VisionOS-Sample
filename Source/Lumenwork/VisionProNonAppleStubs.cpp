// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Non-Apple implementations of the Blueprint libraries whose real bodies live in
// Apple/*.mm (Objective-C++, Metal). UBT skips the Apple/ folder on other platforms, but UHT
// still generates the UCLASS glue from the headers, so these symbols must exist to link
// (Android Vulkan SM5 builds hit "Objective-C was disabled in PCH file" before this split).
#include "VisionProGPUDetection.h"
#include "VisionProCapabilityReport.h"

#if !PLATFORM_APPLE

EVisionProGPUTier UVisionProGPUDetection::GetGPUTier()
{
	return EVisionProGPUTier::Unknown;
}

FString UVisionProGPUDetection::GetGPUTierDisplayString()
{
	return TEXT("n/a (non-Apple)");
}

void UVisionProCapabilityReport::LogFullReport()
{
	UE_LOG(LogTemp, Log, TEXT("[M5CAP] report_begin"));
	UE_LOG(LogTemp, Log, TEXT("[M5CAP] metal=unavailable"));
	UE_LOG(LogTemp, Log, TEXT("[M5CAP] report_end"));
	GLog->Flush();
}

#endif // !PLATFORM_APPLE
