// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "VisionProAutoCycler.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "RenderTimer.h"
#include "RHIStats.h"

DEFINE_LOG_CATEGORY_STATIC(LogVisionProAutoCycle, Log, All);

// Dwell defaults chosen so a ~2 minute wear session covers a full pass of a 6-mode cycler with
// enough frames per mode for the mean to be stable: 6 modes x (1.5s warmup + 10s sample) = ~69s.
static TAutoConsoleVariable<int32> CVarAutoCycleEnabled(
	TEXT("r.VisionOS.AutoCycle"),
	0,
	TEXT("Timer-driven render-mode cycler with measured per-mode stats. 0=off, 1=on.\n")
	TEXT("Removes the wearer from the measurement loop -- no gesture needed, results go to the log."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAutoCycleDwell(
	TEXT("r.VisionOS.AutoCycle.Dwell"),
	10.0f,
	TEXT("Seconds to MEASURE each mode, after warmup."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAutoCycleWarmup(
	TEXT("r.VisionOS.AutoCycle.Warmup"),
	1.5f,
	TEXT("Seconds to discard after switching modes, before measuring.\n")
	TEXT("A cvar change can trigger shader/PSO work and a render-target realloc; those frames are\n")
	TEXT("not representative of the mode's steady-state cost and would inflate the max and the mean."),
	ECVF_Default);

void FVisionProAutoCycler::Initialize(const TCHAR* InCyclerName, int32 InNumModes, FApplyModeDelegate InApplyMode)
{
	CyclerName   = InCyclerName;
	NumModes     = InNumModes;
	ApplyMode    = InApplyMode;
	bInitialized = true;
}

int32 FVisionProAutoCycler::ReadThermalState()
{
	// EDeviceThermalState: Unsupported=-1, None=0, Light=1, Moderate=2, Severe=3+ (throttling).
	// On Apple platforms this maps NSProcessInfoThermalState onto the first four values, which is
	// exactly the signal needed for the "does it hold up over minutes" question -- a mode that is
	// fast for 10s and Severe by minute 3 is not a shippable mode.
	return static_cast<int32>(FPlatformMisc::GetDeviceThermalState());
}

float FVisionProAutoCycler::ReadDeviceTemperature()
{
	// VERIFIED 2026-08-20: FIOSPlatformMisc overrides GetDeviceThermalState() (IOSPlatformMisc.h:62)
	// but does NOT override GetDeviceTemperature(), so on visionOS this falls through to the generic
	// implementation and will almost certainly read -1.0 ("unsupported", GenericPlatformMisc.h:1567-1575).
	// That is reported VERBATIM rather than substituted or hidden: -1.0 must read as "no probe", never
	// as a cold device. The thermal STATE above is the signal that actually works on this platform.
	return FPlatformMisc::GetDeviceTemperature();
}

void FVisionProAutoCycler::BeginMode(int32 ModeIndex)
{
	Sample.Reset();
	Sample.ModeIndex    = ModeIndex;
	Sample.CyclerName   = CyclerName;
	Sample.ThermalStart = ReadThermalState();
	Sample.ThermalWorst = Sample.ThermalStart;
	Sample.TempStartC   = ReadDeviceTemperature();

	FString AppliedName;
	ApplyMode.ExecuteIfBound(ModeIndex, AppliedName);
	Sample.ModeName = AppliedName.IsEmpty() ? FString::Printf(TEXT("mode%d"), ModeIndex) : AppliedName;

	CurrentMode   = ModeIndex;
	ModeElapsed   = 0.0;
	SampleElapsed = 0.0;

	// Discard any GPU timings queued from the PREVIOUS mode, so they are not attributed here.
	uint64 Discard = 0;
	while (GpuTimeState.PopFrameCycles(Discard) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
		// intentionally empty -- draining only
	}

	UE_LOG(LogVisionProAutoCycle, Warning,
		TEXT("[AUTOCYCLE] mode_begin cycler=%s index=%d name=%s"),
		*CyclerName, ModeIndex, *Sample.ModeName);
	if (GLog) { GLog->Flush(); }
}

void FVisionProAutoCycler::DrainGpuTimings()
{
	uint64 Cycles64 = 0;
	FRHIGPUFrameTimeHistory::EResult Result;
	while ((Result = GpuTimeState.PopFrameCycles(Cycles64)) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
		const double GpuMs = FPlatformTime::ToMilliseconds64(Cycles64);
		Sample.TotalGpuMs += GpuMs;
		Sample.MaxGpuMs    = FMath::Max(Sample.MaxGpuMs, GpuMs);
		++Sample.GpuSampleCount;
		// EResult::Disjoint means frames were dropped from the ring before we read them. The
		// samples we DID get are still valid, so they are kept; the count reflects reality and the
		// mean stays honest because it divides by the real sample count, not the frame count.
	}
}

void FVisionProAutoCycler::AccumulateFrame(float DeltaSeconds)
{
	const double FrameMs = static_cast<double>(DeltaSeconds) * 1000.0;
	Sample.FrameMsSamples.Add(FrameMs);
	Sample.TotalFrameMs += FrameMs;
	Sample.MinFrameMs    = FMath::Min(Sample.MinFrameMs, FrameMs);
	Sample.MaxFrameMs    = FMath::Max(Sample.MaxFrameMs, FrameMs);
	++Sample.FrameCount;

	// GGameThreadTime / GRenderThreadTime are cycle counts (RenderTimer.h), not milliseconds.
	Sample.TotalGameMs   += FPlatformTime::ToMilliseconds(GGameThreadTime);
	Sample.TotalRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);

	// Index 0: single-GPU. These are per-frame counters reset by the RHI each frame.
	Sample.TotalDrawCalls  += GNumDrawCallsRHI[0];
	Sample.TotalPrimitives += GNumPrimitivesDrawnRHI[0];

	DrainGpuTimings();

	const int32 Thermal = ReadThermalState();
	Sample.ThermalWorst = FMath::Max(Sample.ThermalWorst, Thermal);
}

void FVisionProAutoCycler::EndModeAndReport()
{
	Sample.ThermalEnd = ReadThermalState();
	Sample.TempEndC   = ReadDeviceTemperature();

	if (Sample.FrameCount <= 0)
	{
		// Report the empty window rather than skipping it. A silently missing mode in the log looks
		// identical to a mode that was never reached, and the diff script would show neither.
		UE_LOG(LogVisionProAutoCycle, Warning,
			TEXT("[AUTOCYCLE] mode_end cycler=%s index=%d name=%s frames=0 result=no_frames_sampled"),
			*CyclerName, Sample.ModeIndex, *Sample.ModeName);
		if (GLog) { GLog->Flush(); }
		return;
	}

	// p95 as a jank indicator: a good mean with a bad tail is still an uncomfortable headset, and
	// on a head-mounted display the tail is what the wearer actually notices.
	TArray<double> Sorted = Sample.FrameMsSamples;
	Sorted.Sort();
	const int32 P95Index = FMath::Clamp(FMath::FloorToInt(Sorted.Num() * 0.95f), 0, Sorted.Num() - 1);
	const double P95Ms   = Sorted[P95Index];

	const double AvgFrameMs  = Sample.TotalFrameMs  / Sample.FrameCount;
	const double AvgGameMs   = Sample.TotalGameMs   / Sample.FrameCount;
	const double AvgRenderMs = Sample.TotalRenderMs / Sample.FrameCount;
	const double AvgFps      = AvgFrameMs > 0.0 ? 1000.0 / AvgFrameMs : 0.0;

	// GPU timing is only present if the RHI actually pushed samples. Emit `unavailable` rather than
	// 0.0 when it did not -- a zero would read as "the GPU cost nothing", which is the exact kind of
	// fabricated-looking value that would silently corrupt an M2-vs-M5 comparison.
	const FString AvgGpuStr = Sample.GpuSampleCount > 0
		? FString::Printf(TEXT("%.3f"), Sample.TotalGpuMs / Sample.GpuSampleCount)
		: FString(TEXT("unavailable"));
	const FString MaxGpuStr = Sample.GpuSampleCount > 0
		? FString::Printf(TEXT("%.3f"), Sample.MaxGpuMs)
		: FString(TEXT("unavailable"));

	UE_LOG(LogVisionProAutoCycle, Warning,
		TEXT("[AUTOCYCLE] mode_end cycler=%s index=%d name=%s frames=%d ")
		TEXT("frame_ms_avg=%.3f frame_ms_min=%.3f frame_ms_max=%.3f frame_ms_p95=%.3f fps_avg=%.2f ")
		TEXT("game_ms_avg=%.3f render_ms_avg=%.3f gpu_ms_avg=%s gpu_ms_max=%s gpu_samples=%d ")
		TEXT("drawcalls_avg=%lld prims_avg=%lld ")
		TEXT("thermal_start=%d thermal_end=%d thermal_worst=%d temp_start_c=%.1f temp_end_c=%.1f"),
		*CyclerName, Sample.ModeIndex, *Sample.ModeName, Sample.FrameCount,
		AvgFrameMs, Sample.MinFrameMs, Sample.MaxFrameMs, P95Ms, AvgFps,
		AvgGameMs, AvgRenderMs, *AvgGpuStr, *MaxGpuStr, Sample.GpuSampleCount,
		static_cast<long long>(Sample.TotalDrawCalls  / Sample.FrameCount),
		static_cast<long long>(Sample.TotalPrimitives / Sample.FrameCount),
		Sample.ThermalStart, Sample.ThermalEnd, Sample.ThermalWorst,
		Sample.TempStartC, Sample.TempEndC);

	// Forced flush: on device this log IS the result. Without it the tail is lost when the app is
	// backgrounded or the headset is removed, which is precisely when a run ends.
	if (GLog) { GLog->Flush(); }
}

void FVisionProAutoCycler::Tick(float DeltaSeconds)
{
	if (!bInitialized || NumModes <= 0)
	{
		return;
	}

	const bool bWantRunning = CVarAutoCycleEnabled.GetValueOnGameThread() != 0;

	if (bWantRunning && !bRunning)
	{
		bRunning        = true;
		CompletedCycles = 0;
		UE_LOG(LogVisionProAutoCycle, Warning,
			TEXT("[AUTOCYCLE] cycle_start cycler=%s modes=%d dwell_s=%.2f warmup_s=%.2f"),
			*CyclerName, NumModes, CVarAutoCycleDwell.GetValueOnGameThread(),
			CVarAutoCycleWarmup.GetValueOnGameThread());
		if (GLog) { GLog->Flush(); }
		BeginMode(0);
		return;
	}

	if (!bWantRunning)
	{
		if (bRunning)
		{
			// Report the in-flight window instead of discarding it -- a partial measurement that
			// says how many frames it covers is still usable data.
			EndModeAndReport();
			bRunning    = false;
			CurrentMode = -1;
			UE_LOG(LogVisionProAutoCycle, Warning, TEXT("[AUTOCYCLE] cycle_stop cycler=%s"), *CyclerName);
			if (GLog) { GLog->Flush(); }
		}
		return;
	}

	ModeElapsed += DeltaSeconds;

	const float Warmup = FMath::Max(0.0f, CVarAutoCycleWarmup.GetValueOnGameThread());
	const float Dwell  = FMath::Max(0.1f, CVarAutoCycleDwell.GetValueOnGameThread());

	if (ModeElapsed < Warmup)
	{
		return;   // still settling after the mode switch; deliberately not measured
	}

	AccumulateFrame(DeltaSeconds);
	SampleElapsed += DeltaSeconds;

	if (SampleElapsed >= Dwell)
	{
		EndModeAndReport();

		const int32 NextMode = (CurrentMode + 1) % NumModes;
		if (NextMode == 0)
		{
			++CompletedCycles;
			// A completed pass is the natural unit for thermal comparison: pass 1 vs pass 4 of the
			// same mode list answers "does it hold up" without anyone timing anything by hand.
			UE_LOG(LogVisionProAutoCycle, Warning,
				TEXT("[AUTOCYCLE] cycle_complete cycler=%s pass=%d thermal_now=%d"),
				*CyclerName, CompletedCycles, ReadThermalState());
			if (GLog) { GLog->Flush(); }
		}
		BeginMode(NextMode);
	}
}
