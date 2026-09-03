// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Capability-reporting probe for the M5 Apple Vision Pro bring-up week.
//
// WHY THIS EXISTS: `devicectl device capture screenshot` fails on this device with
// CoreDeviceError 1001, so there is no visual channel off the headset. The device log is the
// ONLY way to tell an M2 AVP apart from an M5 AVP, or an SM6 build apart from an ES3.1 build.
// Every value this class emits is MEASURED at runtime -- from MTLDevice, from UE's RHI globals,
// from IConsoleManager, from sysctl -- never assumed, never hardcoded, never inferred from an
// .ini. Anything that could not be measured is emitted as `key=unavailable` rather than a
// plausible-looking default, because a fabricated value that looks real would silently mislead
// the entire week.
//
// This is the wider sibling of VisionProGPUDetection (which answers only "Apple9 or not?").
// That class stays the fast, cached, Blueprint-facing tier check; this one is the one-shot
// full-fidelity dump.
//
// OUTPUT CONTRACT -- do not change casually, a pull-and-diff script parses it:
//   Every line is exactly `[M5CAP] key=value`. One key-value per line, no prose, no spaces
//   inside a value (whitespace in measured strings is replaced with '_'). The block is
//   bracketed by `[M5CAP] report_begin` / `[M5CAP] report_end`, and GLog->Flush() is called
//   after the last line -- device logs get truncated at process boundaries otherwise, the same
//   reason the existing pinch/gamepad cyclers flush.
//
// Pull and diff two devices with:
//   xcrun devicectl device console --device <id> | grep '^.*\[M5CAP\] ' | sed 's/.*\[M5CAP\] //'

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VisionProCapabilityReport.generated.h"

UCLASS()
class MY_PROJECT_API UVisionProCapabilityReport : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Emits the complete [M5CAP] block to the UE log and flushes GLog.
	//
	// Safe to call more than once (it caches nothing and mutates nothing), but it is intended to
	// run once, late in startup -- see INTEGRATION.md. Must run AFTER RHI init, or the Section B
	// values report the pre-init defaults instead of what the RHI actually selected. On non-Apple
	// platforms the Metal section is emitted entirely as `unavailable`; the UE section still works.
	UFUNCTION(BlueprintCallable, Category = "VisionOS|Diagnostics")
	static void LogFullReport();
};
