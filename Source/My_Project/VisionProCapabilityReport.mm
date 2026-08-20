// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Obj-C++ so it can talk to MTLDevice directly rather than through UE's MTL:: C++ wrapper --
// that wrapper lives in MetalRHI/Private and is not reachable from a game module. My_Project's
// Build.cs already compiles .mm (GamepadInputSetup / VisionProGPUDetection are both .mm and UBT
// picks up every .cpp/.mm/.cc under Source/My_Project automatically), so no Build.cs change is
// needed for the file itself -- see INTEGRATION.md for the one module dependency worth adding.

#include "VisionProCapabilityReport.h"

#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/OutputDeviceRedirector.h"

// Section B needs the RHI globals. Every symbol used below was grepped out of this engine fork
// before being written here (paths are the ue58-port worktree):
//   GMaxRHIShaderPlatform          Runtime/RHI/Public/RHIShaderPlatform.h:82
//   GMaxRHIFeatureLevel            Runtime/RHI/Public/RHIFeatureLevel.h:109
//   GRHIGlobals                    Runtime/RHI/Public/RHIGlobals.h:805
//   GRHIGlobals.bSupportsBindless  Runtime/RHI/Public/RHIGlobals.h:712
//   GRHISupportsRayTracing         Runtime/RHI/Public/RHIGlobals.h:907  (macro over GRHIGlobals)
//   GRHISupportsMeshShadersTier0/1 Runtime/RHI/Public/RHIGlobals.h:961-962
//   GMaxTextureDimensions          Runtime/RHI/Public/RHIGlobals.h:882
//   LexToString(EShaderPlatform)   Runtime/RHI/Public/RHIStrings.h:51
//   LexToString(ERHIFeatureLevel)  Runtime/RHI/Public/RHIStrings.h:44
//   LegacyShaderPlatformToShaderFormat / ShaderPlatformToPlatformName  RHIStrings.h:46,48
#include "RHI.h"
#include "RHIDefinitions.h"
#include "RHIFeatureLevel.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"

#if PLATFORM_APPLE
	#import <Foundation/Foundation.h>
	#import <Metal/Metal.h>
	#include <TargetConditionals.h>
	#include <sys/sysctl.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogM5Cap, Log, All);

// MTLGPUFamilyApple10 and MTLGPUFamilyMetal4 only appear in the Xcode 26 SDKs. Referencing a
// missing enum constant is a hard compile error, not a warning, so both are gated on the SDK
// version macro: __VISION_OS_VERSION_MAX_ALLOWED is defined by the visionOS SDK's
// AvailabilityInternal.h:117 and __VISIONOS_26_0 == 260000 (AvailabilityVersions.h:416). The
// __IPHONE_ and __MAC_ arms cover an iOS or Mac build of the same file -- each platform defines
// only its own MAX_ALLOWED macro, so all three must be tested or a Mac build silently reports
// `unavailable_sdk_too_old` on an SDK that does have the symbols (__MAC_26_0 == 260000,
// macOS SDK AvailabilityVersions.h:99). Found by running this probe on a Mac.
#if (defined(__VISION_OS_VERSION_MAX_ALLOWED) && __VISION_OS_VERSION_MAX_ALLOWED >= 260000) \
	|| (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) \
	|| (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
	#define M5CAP_SDK_VERSION_HAS_26 1
#else
	#define M5CAP_SDK_VERSION_HAS_26 0
#endif

// Apple10 is present in every 26.x SDK, device and simulator alike, and carries no availability
// annotation -- SDK version alone is a sufficient gate.
#define M5CAP_SDK_HAS_APPLE10 (M5CAP_SDK_VERSION_HAS_26)

// Metal4 needs a SECOND condition. The SDK version is NOT sufficient: the simulator SDKs omit the
// symbol entirely at the same version number. Verified by grepping MTLDevice.h in all four 26.5
// SDKs -- MTLGPUFamilyMetal4 is present in XROS26.5 and iPhoneOS26.5, absent from XRSimulator26.5
// and iPhoneSimulator26.5 (Metal 4 is not offered in the simulator). Caught by compiling this
// exact file against the xrsimulator SDK, where a version-only gate failed with
// "use of undeclared identifier 'MTLGPUFamilyMetal4'". Metal4 also carries
// API_AVAILABLE(macos(26.0), ios(26.0)) (MTLDevice.h:213), so it needs a RUNTIME @available check
// on top of this compile-time one.
#if defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
	#define M5CAP_SDK_HAS_METAL4 0
	#define M5CAP_METAL4_ABSENT_REASON TEXT("unavailable_simulator_sdk_omits_metal4")
#else
	#define M5CAP_SDK_HAS_METAL4 (M5CAP_SDK_VERSION_HAS_26)
	#define M5CAP_METAL4_ABSENT_REASON TEXT("unavailable_sdk_too_old")
#endif

namespace
{
	const TCHAR* const kUnavailable = TEXT("unavailable");

	// The parse contract says values never contain spaces. Measured strings (device name
	// "Apple M5", OS version "Version 26.0 (Build 23A123)") do contain them, so collapse any
	// whitespace to '_' rather than dropping the measurement or quoting it.
	FString Sanitize(const FString& In)
	{
		FString Out = In;
		for (TCHAR& Ch : Out)
		{
			if (Ch == TEXT(' ') || Ch == TEXT('\t') || Ch == TEXT('\r') || Ch == TEXT('\n'))
			{
				Ch = TEXT('_');
			}
		}
		return Out.IsEmpty() ? FString(kUnavailable) : Out;
	}

	void Emit(const TCHAR* Key, const FString& Value)
	{
		UE_LOG(LogM5Cap, Log, TEXT("[M5CAP] %s=%s"), Key, *Sanitize(Value));
	}

	void Emit(const TCHAR* Key, const TCHAR* Value)
	{
		Emit(Key, FString(Value));
	}

	void EmitUnavailable(const TCHAR* Key)
	{
		Emit(Key, kUnavailable);
	}

	// Booleans are emitted as 1/0 so a diff script can subtract two reports without a lookup table.
	void EmitBool(const TCHAR* Key, bool bValue)
	{
		Emit(Key, bValue ? TEXT("1") : TEXT("0"));
	}

	void EmitInt(const TCHAR* Key, int64 Value)
	{
		Emit(Key, FString::Printf(TEXT("%lld"), (long long)Value));
	}

	void EmitUInt(const TCHAR* Key, uint64 Value)
	{
		Emit(Key, FString::Printf(TEXT("%llu"), (unsigned long long)Value));
	}

	// Reads the LIVE cvar value, not the .ini. A cvar can be overridden by DeviceProfiles, by a
	// scalability bucket, by the platform's own startup code, or by -ExecCmds -- the whole point of
	// reading it here is to catch the case where the shipped .ini says one thing and the running
	// build does another. A cvar that does not exist in this build is `unavailable`, never 0.
	FString GetLiveCVarInt(const TCHAR* Name)
	{
		if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return FString::Printf(TEXT("%d"), CVar->GetInt());
		}
		return FString(kUnavailable);
	}

#if PLATFORM_APPLE
	// UE's FString has NO constructor taking NSString* -- grepped Runtime/Core/Public for one and
	// there is none. The engine's own idiom is FString(UTF8_TO_TCHAR([Str UTF8String])), e.g.
	// ApplePlatformHttp.cpp:61. Doing it in one helper also gives every call site nil-safety:
	// -bundleIdentifier and -objectForKey: both legitimately return nil, and UTF8_TO_TCHAR(nullptr)
	// is undefined behavior, not an empty string.
	FString NSStringToFString(NSString* In)
	{
		if (In == nil)
		{
			return FString(kUnavailable);
		}
		const char* Utf8 = [In UTF8String];
		return Utf8 ? FString(UTF8_TO_TCHAR(Utf8)) : FString(kUnavailable);
	}

	// sysctlbyname returns a NUL-terminated C string for hw.machine / hw.model.
	FString SysctlString(const char* Name)
	{
		size_t Size = 0;
		if (sysctlbyname(Name, nullptr, &Size, nullptr, 0) != 0 || Size == 0)
		{
			return FString(kUnavailable);
		}

		TArray<char> Buffer;
		Buffer.SetNumZeroed((int32)Size + 1);
		if (sysctlbyname(Name, Buffer.GetData(), &Size, nullptr, 0) != 0)
		{
			return FString(kUnavailable);
		}
		return FString(ANSI_TO_TCHAR(Buffer.GetData()));
	}

	bool SysctlUInt64(const char* Name, uint64& OutValue)
	{
		uint64 Value = 0;
		size_t Size = sizeof(Value);
		if (sysctlbyname(Name, &Value, &Size, nullptr, 0) != 0 || Size != sizeof(Value))
		{
			return false;
		}
		OutValue = Value;
		return true;
	}
#endif // PLATFORM_APPLE

	void EmitDeviceAndOSSection()
	{
#if PLATFORM_APPLE
		@autoreleasepool
		{
			NSProcessInfo* ProcessInfo = [NSProcessInfo processInfo];

			const NSOperatingSystemVersion OSVersion = [ProcessInfo operatingSystemVersion];
			Emit(TEXT("os_version"), FString::Printf(TEXT("%ld.%ld.%ld"),
				(long)OSVersion.majorVersion, (long)OSVersion.minorVersion, (long)OSVersion.patchVersion));
			Emit(TEXT("os_version_string"), NSStringToFString([ProcessInfo operatingSystemVersionString]));

			// hw.machine is the marketing-adjacent device identifier ("RealityDevice14,1" on the
			// first-gen AVP); hw.model is the board id. Both are emitted because it is not yet
			// known which one the M5 unit differentiates on -- one of them will.
			Emit(TEXT("device_hw_machine"), SysctlString("hw.machine"));
			Emit(TEXT("device_hw_model"), SysctlString("hw.model"));

			uint64 MemSizeBytes = 0;
			if (SysctlUInt64("hw.memsize", MemSizeBytes))
			{
				EmitUInt(TEXT("physical_memory_bytes"), MemSizeBytes);
			}
			else
			{
				EmitUnavailable(TEXT("physical_memory_bytes"));
			}

			NSBundle* MainBundle = [NSBundle mainBundle];
			NSString* ShortVersion = [[MainBundle infoDictionary] objectForKey:@"CFBundleShortVersionString"];
			NSString* BundleVersion = [[MainBundle infoDictionary] objectForKey:@"CFBundleVersion"];
			Emit(TEXT("app_short_version"), NSStringToFString(ShortVersion));
			Emit(TEXT("app_bundle_version"), NSStringToFString(BundleVersion));
			Emit(TEXT("app_bundle_id"), NSStringToFString([MainBundle bundleIdentifier]));
		}
#else
		EmitUnavailable(TEXT("os_version"));
		EmitUnavailable(TEXT("os_version_string"));
		EmitUnavailable(TEXT("device_hw_machine"));
		EmitUnavailable(TEXT("device_hw_model"));
		EmitUnavailable(TEXT("physical_memory_bytes"));
		EmitUnavailable(TEXT("app_short_version"));
		EmitUnavailable(TEXT("app_bundle_version"));
		EmitUnavailable(TEXT("app_bundle_id"));
#endif // PLATFORM_APPLE

		// Cross-check the sysctl number against UE's own view. A mismatch is itself a finding.
		EmitUInt(TEXT("ue_physical_gb_ram"), (uint64)FPlatformMemory::GetPhysicalGBRam());
		Emit(TEXT("ue_build_version"), FString(FApp::GetBuildVersion()));
	}

#if PLATFORM_APPLE
	struct FFamilyProbe
	{
		const TCHAR* Key;
		NSInteger    Family;
		bool         bIsAppleTier;   // participates in the "highest Apple family" reduction
		int32        AppleTierIndex; // 1..10 for Apple families, 0 otherwise
	};

	void EmitMetalSection()
	{
		@autoreleasepool
		{
			id<MTLDevice> Device = MTLCreateSystemDefaultDevice();
			if (Device == nil)
			{
				// Metal genuinely unavailable (headless/CI). Emit the whole section as unavailable
				// rather than letting a caller assume the keys were simply omitted.
				Emit(TEXT("metal_device_available"), TEXT("0"));
				EmitUnavailable(TEXT("metal_device_name"));
				EmitUnavailable(TEXT("metal_highest_apple_family"));
				return;
			}

			Emit(TEXT("metal_device_available"), TEXT("1"));
			Emit(TEXT("metal_device_name"), NSStringToFString([Device name]));
			EmitUInt(TEXT("metal_device_registry_id"), (uint64)[Device registryID]);
			EmitBool(TEXT("metal_has_unified_memory"), [Device hasUnifiedMemory] != NO);

			// MTLDevice.location / MTLDeviceLocation does NOT exist in the visionOS SDK -- grepped
			// XROS26.5.sdk .../Metal.framework/Headers/ for "MTLDeviceLocation" and for a `location`
			// @property and both came back empty. It is a macOS-only discrete/external-GPU concept.
			// Emitted as unavailable rather than omitted so the parse script sees it was considered.
			EmitUnavailable(TEXT("metal_device_location"));

			// ---- Full GPU family ladder -------------------------------------------------------
			// This is the measurement that distinguishes an M2 AVP (tops out at Apple8) from an M5
			// AVP (Apple9+), and it is exactly the predicate the engine's own SM6 gate uses --
			// MetalRHI.cpp:258-265 does `MTLDevice->supportsFamily(MTL::GPUFamilyApple9)` on
			// iOS/visionOS. Probing the whole ladder rather than just Apple9 means a surprise
			// (e.g. Apple9 true but Apple7 false, or Apple10 true) is visible instead of silent.
			TArray<FFamilyProbe> Probes;
			Probes.Add({ TEXT("metal_family_apple1"),  MTLGPUFamilyApple1,  true, 1  });
			Probes.Add({ TEXT("metal_family_apple2"),  MTLGPUFamilyApple2,  true, 2  });
			Probes.Add({ TEXT("metal_family_apple3"),  MTLGPUFamilyApple3,  true, 3  });
			Probes.Add({ TEXT("metal_family_apple4"),  MTLGPUFamilyApple4,  true, 4  });
			Probes.Add({ TEXT("metal_family_apple5"),  MTLGPUFamilyApple5,  true, 5  });
			Probes.Add({ TEXT("metal_family_apple6"),  MTLGPUFamilyApple6,  true, 6  });
			Probes.Add({ TEXT("metal_family_apple7"),  MTLGPUFamilyApple7,  true, 7  });
			Probes.Add({ TEXT("metal_family_apple8"),  MTLGPUFamilyApple8,  true, 8  });
			Probes.Add({ TEXT("metal_family_apple9"),  MTLGPUFamilyApple9,  true, 9  });
#if M5CAP_SDK_HAS_APPLE10
			Probes.Add({ TEXT("metal_family_apple10"), MTLGPUFamilyApple10, true, 10 });
#endif
			Probes.Add({ TEXT("metal_family_common1"), MTLGPUFamilyCommon1, false, 0 });
			Probes.Add({ TEXT("metal_family_common2"), MTLGPUFamilyCommon2, false, 0 });
			Probes.Add({ TEXT("metal_family_common3"), MTLGPUFamilyCommon3, false, 0 });
			Probes.Add({ TEXT("metal_family_metal3"),  MTLGPUFamilyMetal3,  false, 0 });

			int32 HighestAppleTier = 0;
			bool bSupportsApple3OrLater = false;
			bool bSupportsApple1 = false;

			for (const FFamilyProbe& Probe : Probes)
			{
				const bool bSupported = [Device supportsFamily:(MTLGPUFamily)Probe.Family] != NO;
				EmitBool(Probe.Key, bSupported);

				if (bSupported && Probe.bIsAppleTier)
				{
					HighestAppleTier = FMath::Max(HighestAppleTier, Probe.AppleTierIndex);
					bSupportsApple1 = bSupportsApple1 || (Probe.AppleTierIndex >= 1);
					bSupportsApple3OrLater = bSupportsApple3OrLater || (Probe.AppleTierIndex >= 3);
				}
			}

#if !M5CAP_SDK_HAS_APPLE10
			Emit(TEXT("metal_family_apple10"), TEXT("unavailable_sdk_too_old"));
#endif

#if M5CAP_SDK_HAS_METAL4
			if (@available(visionOS 26.0, iOS 26.0, macOS 26.0, *))
			{
				EmitBool(TEXT("metal_family_metal4"), [Device supportsFamily:MTLGPUFamilyMetal4] != NO);
			}
			else
			{
				// SDK knows the symbol but the running OS predates it. Not a measurement failure,
				// but not a measured `0` either -- say which it is.
				Emit(TEXT("metal_family_metal4"), TEXT("unavailable_os_too_old"));
			}
#else
			Emit(TEXT("metal_family_metal4"), M5CAP_METAL4_ABSENT_REASON);
#endif

			if (HighestAppleTier > 0)
			{
				EmitInt(TEXT("metal_highest_apple_family"), HighestAppleTier);
			}
			else
			{
				EmitUnavailable(TEXT("metal_highest_apple_family"));
			}

			// Replicate the engine's SM6 gate verbatim so the log proves whether the gate branch
			// even executed. MetalRHI.cpp:259 wraps the family test in
			// `if (@available(macOS 15.0, iOS 18.0, *))` -- on visionOS the iOS 18.0 clause maps to
			// visionOS 2.0, so on a visionOS 1.x device the gate never runs and bSupportsSM6 stays
			// false REGARDLESS of the GPU. If SM6 is unexpectedly off on M5 hardware, this line and
			// metal_family_apple9 together say which of the two reasons it was.
			if (@available(macOS 15.0, iOS 18.0, *))
			{
				EmitBool(TEXT("engine_sm6_gate_os_available"), true);
				EmitBool(TEXT("engine_sm6_gate_result"), [Device supportsFamily:MTLGPUFamilyApple9] != NO);
				EmitBool(TEXT("engine_bindless_gate_result"), [Device supportsFamily:MTLGPUFamilyApple7] != NO);
			}
			else
			{
				EmitBool(TEXT("engine_sm6_gate_os_available"), false);
				Emit(TEXT("engine_sm6_gate_result"), TEXT("unavailable_os_too_old"));
				Emit(TEXT("engine_bindless_gate_result"), TEXT("unavailable_os_too_old"));
			}

			// ---- Limits -----------------------------------------------------------------------
			EmitUInt(TEXT("metal_recommended_max_working_set_bytes"), (uint64)[Device recommendedMaxWorkingSetSize]);
			EmitUInt(TEXT("metal_max_buffer_length_bytes"), (uint64)[Device maxBufferLength]);
			EmitUInt(TEXT("metal_max_threadgroup_memory_bytes"), (uint64)[Device maxThreadgroupMemoryLength]);

			const MTLSize MaxThreads = [Device maxThreadsPerThreadgroup];
			EmitUInt(TEXT("metal_max_threads_per_threadgroup_w"), (uint64)MaxThreads.width);
			EmitUInt(TEXT("metal_max_threads_per_threadgroup_h"), (uint64)MaxThreads.height);
			EmitUInt(TEXT("metal_max_threads_per_threadgroup_d"), (uint64)MaxThreads.depth);

			// MTLArgumentBuffersTier1 == 0, Tier2 == 1 (MTLDevice.h:245-249). Emit the human tier
			// number, not the raw enum, so the log does not read "tier=0" for a supported tier.
			EmitInt(TEXT("metal_argument_buffers_tier"), (int64)[Device argumentBuffersSupport] + 1);

			// ---- Feature flags ----------------------------------------------------------------
			// Every selector below is annotated no later than ios(16.4) in the visionOS SDK
			// (checked in .../Metal.framework/Headers/MTLDevice.h), and visionOS 1.0 maps above
			// iOS 17, so none of them needs an @available guard on this platform. They are also
			// all plain @property reads -- none can fail, so none can legitimately be `unavailable`.
			EmitBool(TEXT("metal_supports_raytracing"), [Device supportsRaytracing] != NO);
			EmitBool(TEXT("metal_supports_raytracing_from_render"), [Device supportsRaytracingFromRender] != NO);
			EmitBool(TEXT("metal_supports_function_pointers"), [Device supportsFunctionPointers] != NO);
			EmitBool(TEXT("metal_supports_dynamic_libraries"), [Device supportsDynamicLibraries] != NO);
			EmitBool(TEXT("metal_supports_primitive_motion_blur"), [Device supportsPrimitiveMotionBlur] != NO);
			EmitBool(TEXT("metal_supports_shader_barycentric_coords"), [Device supportsShaderBarycentricCoordinates] != NO);
			EmitBool(TEXT("metal_supports_32bit_float_filtering"), [Device supports32BitFloatFiltering] != NO);
			EmitBool(TEXT("metal_supports_bc_texture_compression"), [Device supportsBCTextureCompression] != NO);
			EmitBool(TEXT("metal_supports_pull_model_interpolation"), [Device supportsPullModelInterpolation] != NO);

			// ---- MSAA -------------------------------------------------------------------------
			// MSAA is known-broken on this visionOS port, so what the HARDWARE claims is the useful
			// half of the picture -- if the device reports 4x here and the port still renders
			// wrong, the bug is ours, not the silicon's.
			const NSUInteger SampleCounts[] = { 1, 2, 4, 8 };
			for (NSUInteger Count : SampleCounts)
			{
				const FString Key = FString::Printf(TEXT("metal_supports_sample_count_%llu"), (unsigned long long)Count);
				Emit(*Key, [Device supportsTextureSampleCount:Count] != NO ? TEXT("1") : TEXT("0"));
			}

			// ---- Max 2D texture dimension -----------------------------------------------------
			// DERIVATION: Metal exposes no MTLDevice property for this, so it must come from the
			// family ladder we just probed. Apple's Metal Feature Set Tables give "Maximum 2D
			// texture width and height" as 8192 for Apple1-Apple2 and 16384 for Apple3 and later.
			// Because it is derived rather than read, the derivation is emitted alongside the
			// number so a future reader can tell it apart from a measured value -- and
			// rhi_max_texture_dimensions below is UE's independently-set number for the same thing,
			// so a mismatch between the two is visible instead of silent.
			if (bSupportsApple3OrLater)
			{
				EmitInt(TEXT("metal_max_2d_texture_dim"), 16384);
				Emit(TEXT("metal_max_2d_texture_dim_source"), TEXT("derived_family_apple3_or_later"));
			}
			else if (bSupportsApple1)
			{
				EmitInt(TEXT("metal_max_2d_texture_dim"), 8192);
				Emit(TEXT("metal_max_2d_texture_dim_source"), TEXT("derived_family_apple1_or_apple2"));
			}
			else
			{
				EmitUnavailable(TEXT("metal_max_2d_texture_dim"));
				Emit(TEXT("metal_max_2d_texture_dim_source"), TEXT("no_apple_family_reported"));
			}

			// MTLCreateSystemDefaultDevice is NS_RETURNS_RETAINED (MTLDevice.h:130). Under ARC the
			// compiler balances it; without ARC we own the +1 and must release it.
#if !__has_feature(objc_arc)
			[Device release];
#endif
		}
	}
#else // !PLATFORM_APPLE
	void EmitMetalSection()
	{
		Emit(TEXT("metal_device_available"), TEXT("0"));
		Emit(TEXT("metal_section_skipped_reason"), TEXT("not_an_apple_platform"));
	}
#endif // PLATFORM_APPLE

	void EmitRHISection()
	{
		// Numeric AND string for both, because the numeric value survives a LexToString that does
		// not know a fork-added platform, and the string survives an enum renumbering between
		// engine versions. Either one alone can lie across a version bump; together they cannot.
		EmitInt(TEXT("rhi_max_shader_platform_numeric"), (int64)GMaxRHIShaderPlatform);
		Emit(TEXT("rhi_max_shader_platform_name"), LexToString(GMaxRHIShaderPlatform));

		// NOTE: `ShaderPlatformToShaderFormatName` does NOT exist anywhere in this 5.8 fork --
		// grepped all of Engine/Source and got zero hits. The two functions that do exist in
		// RHIStrings.h are LegacyShaderPlatformToShaderFormat (:46) and ShaderPlatformToPlatformName
		// (:48), so those are what is emitted. Both return FName.
		Emit(TEXT("rhi_max_shader_platform_shader_format"), LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
		Emit(TEXT("rhi_max_shader_platform_platform_name"), ShaderPlatformToPlatformName(GMaxRHIShaderPlatform).ToString());

		EmitInt(TEXT("rhi_max_feature_level_numeric"), (int64)GMaxRHIFeatureLevel);
		Emit(TEXT("rhi_max_feature_level_name"), LexToString(GMaxRHIFeatureLevel));

		// Runtime bindless. MetalRHI.cpp:521 sets this from
		// UE::RHICore::GetBindlessConfigurationOnStartup(GMaxRHIShaderPlatform) != Disabled, so it
		// is the runtime-derived answer. The richer ERHIBindlessConfiguration would need a
		// dependency on the RHICore module (RHICore.h:44) that this game module does not have, and
		// every RHIGetRuntimeBindless* helper in RHI.h:628-644 is UE_DEPRECATED in 5.8 and now
		// returns Disabled unconditionally -- calling one of those would have produced a
		// confident, permanently-wrong `0`.
		EmitBool(TEXT("rhi_supports_bindless"), GRHIGlobals.bSupportsBindless);

		EmitBool(TEXT("rhi_supports_raytracing"), GRHISupportsRayTracing);
		EmitBool(TEXT("rhi_supports_mesh_shaders_tier0"), GRHISupportsMeshShadersTier0);
		EmitBool(TEXT("rhi_supports_mesh_shaders_tier1"), GRHISupportsMeshShadersTier1);
		EmitInt(TEXT("rhi_max_texture_dimensions"), (int64)(int32)GMaxTextureDimensions);

		// ---- Live renderer feature state ------------------------------------------------------
		// Read through IConsoleManager, i.e. the values the running process actually holds, not
		// whatever the packaged .ini said. Every cvar name below was confirmed registered in this
		// fork before being written here.
		Emit(TEXT("cvar_r_Nanite_ProjectEnabled"), GetLiveCVarInt(TEXT("r.Nanite.ProjectEnabled")));
		Emit(TEXT("cvar_r_DynamicGlobalIlluminationMethod"), GetLiveCVarInt(TEXT("r.DynamicGlobalIlluminationMethod")));
		Emit(TEXT("cvar_r_ReflectionMethod"), GetLiveCVarInt(TEXT("r.ReflectionMethod")));
		Emit(TEXT("cvar_r_Shadow_Virtual_Enable"), GetLiveCVarInt(TEXT("r.Shadow.Virtual.Enable")));
		Emit(TEXT("cvar_r_ForwardShading"), GetLiveCVarInt(TEXT("r.ForwardShading")));
		Emit(TEXT("cvar_vr_MobileMultiView"), GetLiveCVarInt(TEXT("vr.MobileMultiView")));
		Emit(TEXT("cvar_vr_InstancedStereo"), GetLiveCVarInt(TEXT("vr.InstancedStereo")));
		Emit(TEXT("cvar_r_Mobile_ShadingPath"), GetLiveCVarInt(TEXT("r.Mobile.ShadingPath")));
		Emit(TEXT("cvar_r_MobileHDR"), GetLiveCVarInt(TEXT("r.MobileHDR")));
	}

	void EmitCompileTimeDefinesSection()
	{
		// These make a build's configuration provable from its own log. Each is emitted only if the
		// preprocessor can actually see it from THIS module -- a game module's visibility differs
		// per define, and guessing would be worse than saying `unavailable`.

		// GLOBAL CompileEnvironment definitions -> visible from every module, so these values are
		// authoritative for the whole binary:
		//   PLATFORM_VISIONOS=1                  Engine/Platforms/VisionOS/.../UEBuildVisionOS.cs:97
		//   PLATFORM_SUPPORTS_BINDLESS_RENDERING Platform/IOS/UEBuildIOS.cs:1257 (set to 1 only when
		//                                        ShouldIncludeMetalShaderConverterForIOS(Target))
		//   RHI_RAYTRACING=1                     Platform/IOS/UEBuildIOS.cs:1251
		//   WITH_IOS_SIMULATOR=0|1               Platform/IOS/UEBuildIOS.cs:1189/1193
		// All four also have `#ifndef`/0 fallbacks in public Core/RHI headers, so they are always
		// defined by the time this file compiles.
		EmitBool(TEXT("define_PLATFORM_VISIONOS"), PLATFORM_VISIONOS != 0);
		EmitBool(TEXT("define_PLATFORM_SUPPORTS_BINDLESS_RENDERING"), PLATFORM_SUPPORTS_BINDLESS_RENDERING != 0);
		EmitBool(TEXT("define_RHI_RAYTRACING"), RHI_RAYTRACING != 0);
		EmitBool(TEXT("define_WITH_IOS_SIMULATOR"), WITH_IOS_SIMULATOR != 0);

		// METAL_USE_METAL_SHADER_CONVERTER is a PublicDefinitions entry on the MetalShaderConverter
		// ThirdParty module (MetalShaderConverter.build.cs:134), NOT a global definition, so it
		// only reaches modules that depend on that module -- a game module does not, and this will
		// virtually always report unavailable. That is the honest answer; the useful one is the
		// line above it. UEBuildIOS.cs sets PLATFORM_SUPPORTS_BINDLESS_RENDERING=1 under the SAME
		// ShouldIncludeMetalShaderConverterForIOS(Target) predicate that gates
		// METAL_USE_METAL_SHADER_CONVERTER=1, which makes it an exact proxy from out here.
#if defined(METAL_USE_METAL_SHADER_CONVERTER)
		EmitBool(TEXT("define_METAL_USE_METAL_SHADER_CONVERTER"), METAL_USE_METAL_SHADER_CONVERTER != 0);
#else
		Emit(TEXT("define_METAL_USE_METAL_SHADER_CONVERTER"), TEXT("unavailable_not_visible_to_game_module"));
#endif
		Emit(TEXT("define_METAL_USE_METAL_SHADER_CONVERTER_proxy"), TEXT("PLATFORM_SUPPORTS_BINDLESS_RENDERING"));

		// METAL_RHI_RAYTRACING is defined in MetalRHI/Public/MetalRHI.h:38-39, which this module
		// does not include (that would mean depending on MetalRHI). Same reasoning as above.
#if defined(METAL_RHI_RAYTRACING)
		EmitBool(TEXT("define_METAL_RHI_RAYTRACING"), METAL_RHI_RAYTRACING != 0);
#else
		Emit(TEXT("define_METAL_RHI_RAYTRACING"), TEXT("unavailable_not_visible_to_game_module"));
#endif

		// Which SDK this translation unit was compiled against -- needed to interpret any
		// `unavailable_sdk_too_old` above.
#if defined(__VISION_OS_VERSION_MAX_ALLOWED)
		EmitInt(TEXT("compiled_against_visionos_sdk"), (int64)__VISION_OS_VERSION_MAX_ALLOWED);
#else
		EmitUnavailable(TEXT("compiled_against_visionos_sdk"));
#endif
		EmitBool(TEXT("compiled_with_objc_arc"), __has_feature(objc_arc) != 0);
	}
} // namespace

void UVisionProCapabilityReport::LogFullReport()
{
	Emit(TEXT("report_begin"), TEXT("1"));
	// Bump whenever a key is renamed or removed, so a diff script can refuse a report it cannot parse.
	Emit(TEXT("report_schema_version"), TEXT("1"));
	Emit(TEXT("report_timestamp_utc"), FDateTime::UtcNow().ToIso8601());

	EmitDeviceAndOSSection();
	EmitMetalSection();
	EmitRHISection();
	EmitCompileTimeDefinesSection();

	Emit(TEXT("report_end"), TEXT("1"));

	// Device logs get truncated at process/stream boundaries without this -- the same reason the
	// existing showcase cyclers flush. Logs are the only channel off this device, so a truncated
	// report is a lost report.
	if (GLog)
	{
		GLog->Flush();
	}
}

// Re-fireable from the console at any time, which matters because the interesting comparison is
// often "before vs. after I changed a cvar", not just what startup happened to pick.
//
// FAutoConsoleCommand only exists inside `#if !NO_CVARS` (IConsoleManager.h:2274-2278). NO_CVARS
// defaults to 0 (CoreDefines.h:51) but can be forced on in a stripped shipping config, so guard
// the registration -- LogFullReport() itself stays callable from C++ and Blueprint either way.
#if !NO_CVARS
static FAutoConsoleCommand GVisionProCapabilityReportCmd(
	TEXT("visionos.CapabilityReport"),
	TEXT("Emit the full [M5CAP] capability report block to the log and flush."),
	FConsoleCommandDelegate::CreateStatic(&UVisionProCapabilityReport::LogFullReport));
#endif

// Fire once automatically at startup. Set to 0 (or delete this block) to make the probe
// console-only -- LogFullReport() and the console command keep working either way.
#ifndef M5CAP_AUTO_REPORT_ON_STARTUP
	#define M5CAP_AUTO_REPORT_ON_STARTUP 1
#endif

#if M5CAP_AUTO_REPORT_ON_STARTUP
// EndOfEngineInit is the correct phase and the reason is load-bearing: Section B reads
// GMaxRHIShaderPlatform / GMaxRHIFeatureLevel / GRHIGlobals, all of which MetalRHI assigns during
// RHI init (MetalRHI.cpp:355-390 and :521). Anything that runs at PreRHIInit or from a game
// module's StartupModule risks reading the pre-init defaults and reporting ES3_1 on a build that
// actually selected SM6 -- a wrong value that looks completely plausible. EndOfEngineInit is also
// after DeviceProfileManagerReady, so the cvars read via IConsoleManager are the post-device-
// profile values rather than the raw .ini ones.
//
// FDelayedAutoRegisterHelper is used rather than FCoreDelegates::GetOnPostEngineInit() only
// because it needs no edit to My_Project.cpp -- both fire at an equivalent point. (Note if you
// switch: plain FCoreDelegates::OnPostEngineInit is UE_DEPRECATED in 5.8,
// CoreDelegates.h:240-241; the accessor GetOnPostEngineInit() is the supported spelling.)
static FDelayedAutoRegisterHelper GVisionProCapabilityReportAutoRun(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]() { UVisionProCapabilityReport::LogFullReport(); });
#endif
