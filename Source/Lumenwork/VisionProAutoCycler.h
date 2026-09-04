// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Timer-driven render-config cycler with MEASURED per-mode statistics.
//
// WHY THIS EXISTS: the existing pinch-driven cyclers in PinchworkShowcaseSubsystem require the
// wearer to perform a gesture and then REPORT what they saw. That makes a human the measurement
// instrument, which is both slow and unreliable -- and on this hardware there is no fallback,
// because `devicectl device capture screenshot` does not work on these Vision Pros (CoreDeviceError
// 1001). So the log has to carry the whole verdict.
//
// This class removes the human from the loop entirely: it advances the render configuration on a
// TIMER and emits measured statistics for each mode. A wearer only has to put the headset on and
// look at the scene; a two-minute wear session yields a full mode-by-mode performance matrix that
// can be diffed between an M2 and an M5 device without anyone describing anything.
//
// Every number here is measured from engine counters at runtime. Anything that cannot be measured
// is emitted as `unavailable` rather than a plausible-looking default -- a fabricated value would
// silently corrupt an M2-vs-M5 comparison, which is the entire point of the exercise.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "GPUProfiler.h"

// One measurement window: everything accumulated while a single render mode was active.
struct FVisionProModeSample
{
	FString ModeName;
	int32   ModeIndex        = -1;
	FString CyclerName;              // which cycler owns this mode ("quality" / "alpha")

	int32   FrameCount       = 0;
	double  TotalFrameMs     = 0.0;
	double  MinFrameMs       = TNumericLimits<double>::Max();
	double  MaxFrameMs       = 0.0;
	TArray<double> FrameMsSamples;   // retained so a percentile can be computed, not just a mean

	double  TotalGameMs      = 0.0;
	double  TotalRenderMs    = 0.0;

	int32   GpuSampleCount   = 0;    // GPU timings arrive on their own cadence, so count separately
	double  TotalGpuMs       = 0.0;
	double  MaxGpuMs         = 0.0;

	int64   TotalDrawCalls   = 0;
	int64   TotalPrimitives  = 0;

	// Thermal is sampled rather than accumulated -- the interesting facts are where it started,
	// where it ended, and the worst it ever got during the window.
	int32   ThermalStart     = -1;
	int32   ThermalEnd       = -1;
	int32   ThermalWorst     = -1;
	float   TempStartC       = -1.0f;
	float   TempEndC         = -1.0f;

	void Reset()
	{
		*this = FVisionProModeSample();
	}
};

// Drives one mode list on a timer and reports each window. The owner supplies the mode count and an
// apply callback, so this class stays independent of which cvars any particular cycler drives.
class FVisionProAutoCycler
{
public:
	DECLARE_DELEGATE_TwoParams(FApplyModeDelegate, int32 /*ModeIndex*/, FString& /*OutModeName*/);

	void Initialize(const TCHAR* InCyclerName, int32 InNumModes, FApplyModeDelegate InApplyMode);

	// Call once per frame from the owning subsystem's Tick.
	void Tick(float DeltaSeconds);

	bool IsRunning() const { return bRunning; }

private:
	void BeginMode(int32 ModeIndex);
	void EndModeAndReport();
	void AccumulateFrame(float DeltaSeconds);
	void DrainGpuTimings();
	static int32 ReadThermalState();
	static float ReadDeviceTemperature();

	FString              CyclerName;
	int32                NumModes    = 0;
	FApplyModeDelegate   ApplyMode;

	bool    bRunning        = false;
	bool    bInitialized    = false;
	int32   CurrentMode     = -1;
	int32   CompletedCycles = 0;
	double  ModeElapsed     = 0.0;   // seconds since this mode began (including warmup)
	double  SampleElapsed   = 0.0;   // seconds actually accumulated (excludes warmup)

	FVisionProModeSample Sample;

	// Persistent per-client cursor into the RHI's GPU timing ring buffer. Must persist across
	// frames: FState carries the read index, and a fresh one each frame would re-read or miss
	// entries (see the usage comment on FRHIGPUFrameTimeHistory in GPUProfiler.h).
	FRHIGPUFrameTimeHistory::FState GpuTimeState;
};
