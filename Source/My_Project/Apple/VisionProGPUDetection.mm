// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "VisionProGPUDetection.h"

#if PLATFORM_MAC || PLATFORM_IOS || PLATFORM_VISIONOS
#import <Metal/Metal.h>
#endif

EVisionProGPUTier UVisionProGPUDetection::GetGPUTier()
{
	static EVisionProGPUTier CachedTier = EVisionProGPUTier::Unknown;
	static bool bQueried = false;
	if (bQueried)
	{
		return CachedTier;
	}
	bQueried = true;

#if PLATFORM_MAC || PLATFORM_IOS || PLATFORM_VISIONOS
	id<MTLDevice> Device = MTLCreateSystemDefaultDevice();
	if (Device != nil)
	{
		// Matches MetalRHI.cpp's own SM6 gate: Apple9+ on iOS/visionOS. Capability-based, not a
		// device-model-string lookup, so it doesn't need updating for future hardware.
		if ([Device supportsFamily:MTLGPUFamilyApple9])
		{
			CachedTier = EVisionProGPUTier::M5OrLater;
		}
		else
		{
			CachedTier = EVisionProGPUTier::M2OrEarlier;
		}
	}
#endif

	return CachedTier;
}

FString UVisionProGPUDetection::GetGPUTierDisplayString()
{
	switch (GetGPUTier())
	{
		case EVisionProGPUTier::M5OrLater:   return TEXT("M5+ (Apple9)");
		case EVisionProGPUTier::M2OrEarlier: return TEXT("M2 (Apple8-)");
		default:                             return TEXT("Unknown");
	}
}
