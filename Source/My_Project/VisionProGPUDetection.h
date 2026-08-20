// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Runtime GPU-tier detection for Apple Vision Pro, independent of which shader platform this
// binary was cooked for. Matches the engine's own SM6 hardware gate (MetalRHI.cpp:258-265,
// MTLDevice->supportsFamily(MTL::GPUFamilyApple9) on iOS/visionOS) rather than a device-model-
// string lookup table, so it stays correct as new Vision Pro hardware ships (M5+) without needing
// an update. Polyarc's public docs mention "Proper device detection for Vision Pro M5" as a fix
// but publish no implementation detail (checked home/getting-started/performance/foveated_rendering
// pages, 2026-08-18) -- this is our own implementation of the same idea, same underlying Metal API.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VisionProGPUDetection.generated.h"

UENUM(BlueprintType)
enum class EVisionProGPUTier : uint8
{
	Unknown      UMETA(DisplayName = "Unknown"),
	M2OrEarlier  UMETA(DisplayName = "M2 or earlier (Apple8 or below -- no SM6)"),
	M5OrLater    UMETA(DisplayName = "M5 or later (Apple9+ -- SM6-capable)"),
};

UCLASS()
class MY_PROJECT_API UVisionProGPUDetection : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Queries the system default Metal device's GPU family. Cached after the first call (GPU
	// tier cannot change at runtime). Returns Unknown on non-Apple platforms or if Metal is
	// unavailable (e.g. some CI/headless configurations).
	UFUNCTION(BlueprintPure, Category = "VisionPro|Hardware")
	static EVisionProGPUTier GetGPUTier();

	// Convenience: short display string for HUD/debug use, e.g. "M5+ (Apple9)" / "M2 (Apple8-)".
	UFUNCTION(BlueprintPure, Category = "VisionPro|Hardware")
	static FString GetGPUTierDisplayString();
};
