// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// See VisionProAlphaProbe.h for which surface this reads and why that surface is the decisive one.

#include "VisionProAlphaProbe.h"

#include "SceneViewExtension.h"
#include "SceneViewExtensionContext.h"
#include "SceneView.h"
#include "UnrealClient.h"            // FRenderTarget::GetRenderTargetTexture
#include "RenderGraphBuilder.h"      // FRDGBuilder::AddPostExecuteCallback (RenderGraphBuilder.h:197)
#include "RHICommandList.h"          // FRHICommandListImmediate::Get (RHICommandList.h:4409)
#include "RHIResources.h"            // FRHITextureDesc
#include "RHITypes.h"                // FReadSurfaceDataFlags (RHITypes.h:15)
#include "PixelFormat.h"             // GetPixelFormatString (PixelFormat.h:520)
#include "Math/Float16Color.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Misc/OutputDeviceRedirector.h"

#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogVisionProAlphaProbe, Warning, All);

// Every line is `[ALPHAPROBE] key=value`, one pair per line, so the device log can be grepped and
// parsed without a screenshot. Warning verbosity so it survives shipping log filters.
#define ALPHAPROBE_LOG(Fmt, ...) UE_LOG(LogVisionProAlphaProbe, Warning, TEXT("[ALPHAPROBE] ") Fmt, ##__VA_ARGS__)

namespace VisionProAlphaProbeInternal
{
	// Number of alpha buckets in the coarse distribution line. 16 buckets of 16 codes each is
	// enough to see "the whole frame sits at one sub-opaque alpha" at a glance, which is the exact
	// shape defect D1 would produce.
	static constexpr int32 NumAlphaBuckets = 16;

	// Tolerance for the premultiplication test, in normalized units. 8-bit channels quantize to
	// 1/255, and the sRGB decode of adjacent codes can move a value by a little more than that
	// near black, so 2/255 keeps quantization from manufacturing fake violations.
	static constexpr float PremulEpsilon = 2.0f / 255.0f;

	/**
	 * Standard sRGB EOTF (IEC 61966-2-1), spelled out rather than routed through a helper so the
	 * exact math that produced the numbers is visible next to them.
	 *
	 * WHY THIS MATTERS FOR THE PREMULTIPLICATION TEST:
	 * The OXRVisionOS color swapchain is created with TexCreate_SRGB
	 * (Engine/Platforms/VisionOS/Plugins/Runtime/OpenXRVisionOS/Source/OXRVisionOS/Private/
	 *  OXRVisionOSSwapchain.cpp:133), so for 8-bit formats the stored RGB bytes are sRGB-ENCODED
	 * while the alpha byte is plain linear coverage (sRGB never applies to the alpha channel).
	 * Comparing the raw bytes would be comparing an encoded value to an unencoded one: a correctly
	 * premultiplied linear pixel with A=0.25 and premultiplied linear R=0.25 stores byte 137
	 * (sRGB_encode(0.25) = 0.537), against alpha byte 64 - so a naive RGB<=A test would report a
	 * violation and "prove" the buffer is NOT premultiplied when it is. Every RGB value used in
	 * the premultiplication test below is decoded to linear first.
	 */
	static float SRGBToLinear(float Encoded)
	{
		if (Encoded <= 0.04045f)
		{
			return Encoded / 12.92f;
		}
		return FMath::Pow((Encoded + 0.055f) / 1.055f, 2.4f);
	}

	static const TCHAR* DimensionToString(ETextureDimension Dimension)
	{
		switch (Dimension)
		{
		case ETextureDimension::Texture2D:        return TEXT("Texture2D");
		case ETextureDimension::Texture2DArray:   return TEXT("Texture2DArray");
		case ETextureDimension::Texture3D:        return TEXT("Texture3D");
		case ETextureDimension::TextureCube:      return TEXT("TextureCube");
		case ETextureDimension::TextureCubeArray: return TEXT("TextureCubeArray");
		default:                                  return TEXT("Unknown");
		}
	}

	static int32 GetCVarInt(const TCHAR* Name, bool& bOutFound)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			bOutFound = true;
			return CVar->GetInt();
		}
		bOutFound = false;
		return -1;
	}

	static void LogCVar(const TCHAR* Name)
	{
		bool bFound = false;
		const int32 Value = GetCVarInt(Name, bFound);
		if (bFound)
		{
			ALPHAPROBE_LOG(TEXT("cvar.%s=%d"), Name, Value);
		}
		else
		{
			ALPHAPROBE_LOG(TEXT("cvar.%s=NOT_REGISTERED"), Name);
		}
	}

	/** One sampled pixel, in whatever the stored channel order resolves to after readback (RGBA). */
	struct FSamplePoint
	{
		const TCHAR* Name = nullptr;
		int32 X = 0;
		int32 Y = 0;
		int32 R = 0;
		int32 G = 0;
		int32 B = 0;
		int32 A = 0;
		float Rf = 0.0f;
		float Gf = 0.0f;
		float Bf = 0.0f;
		float Af = 0.0f;
	};

	/** Everything a single full-frame scan produces. */
	struct FProbeAccum
	{
		int64 Total = 0;
		int64 CountAZero = 0;   // A exactly 0
		int64 CountAMid = 0;    // 0 < A < 1
		int64 CountAOpaque = 0; // A exactly 1

		float MinA = 1.0f;
		float MaxA = 0.0f;
		double SumA = 0.0;

		int64 Buckets[NumAlphaBuckets] = {};
		int64 ValueCounts[256] = {};   // quantized alpha code histogram, used for the mode

		// Premultiplication evidence, computed only over pixels with 0 < A < 1.
		int64 PremulSampled = 0;
		int64 PremulViolationsLinear = 0;
		int64 PremulViolationsRaw = 0;

		void Accumulate(float R, float G, float B, float A, bool bRGBIsSRGBEncoded)
		{
			++Total;

			MinA = FMath::Min(MinA, A);
			MaxA = FMath::Max(MaxA, A);
			SumA += (double)A;

			const int32 Code = FMath::Clamp(FMath::RoundToInt(A * 255.0f), 0, 255);
			++ValueCounts[Code];
			++Buckets[FMath::Clamp(Code / (256 / NumAlphaBuckets), 0, NumAlphaBuckets - 1)];

			if (A <= 0.0f)
			{
				++CountAZero;
				return;
			}
			if (A >= 1.0f)
			{
				++CountAOpaque;
				return;
			}
			++CountAMid;

			// PREMULTIPLICATION TEST.
			// Premultiplied alpha means the stored color has ALREADY been multiplied by coverage,
			// so for every pixel linear_RGB <= A holds by construction: you cannot emit more light
			// than your coverage allows. Straight (unpremultiplied) alpha has no such bound - a
			// fully saturated white surface at 25% coverage stores RGB=1.0 with A=0.25, which
			// violates RGB<=A. So a NONZERO linear violation count is hard evidence the buffer is
			// NOT premultiplied; a violation count of zero across a large mid-alpha population is
			// evidence it IS (strictly: consistent with premultiplied - a dark unpremultiplied
			// image also satisfies the bound, which is why the sampled count is reported alongside).
			//
			// Both a linear-space and a raw-byte-space count are reported. Only the LINEAR one is
			// meaningful when the surface is sRGB (see SRGBToLinear above); the raw one is emitted
			// so the difference between the two is visible in the log rather than assumed.
			++PremulSampled;

			const float RawMax = FMath::Max3(R, G, B);
			if (RawMax > A + PremulEpsilon)
			{
				++PremulViolationsRaw;
			}

			const float LinR = bRGBIsSRGBEncoded ? SRGBToLinear(R) : R;
			const float LinG = bRGBIsSRGBEncoded ? SRGBToLinear(G) : G;
			const float LinB = bRGBIsSRGBEncoded ? SRGBToLinear(B) : B;
			if (FMath::Max3(LinR, LinG, LinB) > A + PremulEpsilon)
			{
				++PremulViolationsLinear;
			}
		}
	};

	static void EmitResults(const FProbeAccum& Accum, const TArray<FSamplePoint>& Samples, bool bFloatPath)
	{
		if (Accum.Total <= 0)
		{
			ALPHAPROBE_LOG(TEXT("error=NO_PIXELS_SCANNED"));
			return;
		}

		const double MeanA = Accum.SumA / (double)Accum.Total;

		ALPHAPROBE_LOG(TEXT("pixels_total=%lld"), Accum.Total);
		ALPHAPROBE_LOG(TEXT("a_eq_0=%lld"), Accum.CountAZero);
		ALPHAPROBE_LOG(TEXT("a_gt0_lt255=%lld"), Accum.CountAMid);
		ALPHAPROBE_LOG(TEXT("a_eq_255=%lld"), Accum.CountAOpaque);
		ALPHAPROBE_LOG(TEXT("a_frac_zero=%.6f"), (double)Accum.CountAZero / (double)Accum.Total);
		ALPHAPROBE_LOG(TEXT("a_frac_mid=%.6f"), (double)Accum.CountAMid / (double)Accum.Total);
		ALPHAPROBE_LOG(TEXT("a_frac_opaque=%.6f"), (double)Accum.CountAOpaque / (double)Accum.Total);

		// Byte-scale figures (the convention the defect is discussed in) and, on the float path,
		// the exact normalized values as well, since quantizing them would hide sub-1/255 detail.
		ALPHAPROBE_LOG(TEXT("a_min_u8=%d"), FMath::Clamp(FMath::RoundToInt(Accum.MinA * 255.0f), 0, 255));
		ALPHAPROBE_LOG(TEXT("a_max_u8=%d"), FMath::Clamp(FMath::RoundToInt(Accum.MaxA * 255.0f), 0, 255));
		ALPHAPROBE_LOG(TEXT("a_mean_u8=%.4f"), MeanA * 255.0);
		if (bFloatPath)
		{
			ALPHAPROBE_LOG(TEXT("a_min_f=%.6f"), Accum.MinA);
			ALPHAPROBE_LOG(TEXT("a_max_f=%.6f"), Accum.MaxA);
			ALPHAPROBE_LOG(TEXT("a_mean_f=%.6f"), MeanA);
		}

		// The mode. If one sub-255 alpha value dominates the frame, defect D1 is literally visible
		// here as a single number - that is the "opaque geometry is being written with A<1" case.
		int32 ModeCode = 0;
		int64 ModeCount = -1;
		for (int32 Code = 0; Code < 256; ++Code)
		{
			if (Accum.ValueCounts[Code] > ModeCount)
			{
				ModeCount = Accum.ValueCounts[Code];
				ModeCode = Code;
			}
		}
		ALPHAPROBE_LOG(TEXT("a_mode_u8=%d"), ModeCode);
		ALPHAPROBE_LOG(TEXT("a_mode_count=%lld"), ModeCount);
		ALPHAPROBE_LOG(TEXT("a_mode_share=%.6f"), (double)ModeCount / (double)Accum.Total);

		FString BucketLine;
		for (int32 Bucket = 0; Bucket < NumAlphaBuckets; ++Bucket)
		{
			BucketLine += FString::Printf(TEXT("%s%lld"), (Bucket == 0 ? TEXT("") : TEXT(",")), Accum.Buckets[Bucket]);
		}
		ALPHAPROBE_LOG(TEXT("a_hist16_bucketwidth=16"));
		ALPHAPROBE_LOG(TEXT("a_hist16=%s"), *BucketLine);

		for (const FSamplePoint& Sample : Samples)
		{
			// Raw integers, as stored in the surface, so nothing is inferred from a float rounding.
			ALPHAPROBE_LOG(TEXT("probe.%s=x:%d,y:%d,R:%d,G:%d,B:%d,A:%d"),
				Sample.Name, Sample.X, Sample.Y, Sample.R, Sample.G, Sample.B, Sample.A);
			if (bFloatPath)
			{
				ALPHAPROBE_LOG(TEXT("probef.%s=x:%d,y:%d,R:%.6f,G:%.6f,B:%.6f,A:%.6f"),
					Sample.Name, Sample.X, Sample.Y, Sample.Rf, Sample.Gf, Sample.Bf, Sample.Af);
			}
		}

		ALPHAPROBE_LOG(TEXT("premul_sampled=%lld"), Accum.PremulSampled);
		ALPHAPROBE_LOG(TEXT("premul_violations_linear=%lld"), Accum.PremulViolationsLinear);
		ALPHAPROBE_LOG(TEXT("premul_violations_raw=%lld"), Accum.PremulViolationsRaw);
		ALPHAPROBE_LOG(TEXT("premul_epsilon=%.6f"), PremulEpsilon);
		if (Accum.PremulSampled == 0)
		{
			ALPHAPROBE_LOG(TEXT("premul_verdict=NO_MID_ALPHA_PIXELS_TEST_INCONCLUSIVE"));
		}
		else if (Accum.PremulViolationsLinear > 0)
		{
			ALPHAPROBE_LOG(TEXT("premul_verdict=NOT_PREMULTIPLIED"));
		}
		else
		{
			ALPHAPROBE_LOG(TEXT("premul_verdict=CONSISTENT_WITH_PREMULTIPLIED"));
		}

		// Classification of the alpha channel itself. Deliberately coarse and named, so a log
		// reader does not have to re-derive the thresholds.
		const double FracOpaque = (double)Accum.CountAOpaque / (double)Accum.Total;
		const double FracZero   = (double)Accum.CountAZero   / (double)Accum.Total;
		const double ModeShare  = (double)ModeCount          / (double)Accum.Total;
		const TCHAR* Verdict = TEXT("MIXED");
		if (FracOpaque > 0.999)
		{
			Verdict = TEXT("ALL_OPAQUE");
		}
		else if (FracZero > 0.900)
		{
			Verdict = TEXT("MOSTLY_ZERO");
		}
		else if (ModeCode > 0 && ModeCode < 255 && ModeShare > 0.900)
		{
			Verdict = TEXT("UNIFORM_SUB_OPAQUE");
		}
		ALPHAPROBE_LOG(TEXT("alpha_verdict=%s"), Verdict);
		ALPHAPROBE_LOG(TEXT("alpha_verdict_legend=ALL_OPAQUE:alpha_is_correct_look_at_compositor_blend|UNIFORM_SUB_OPAQUE:opaque_geometry_written_with_A_lt_1|MOSTLY_ZERO:alpha_inverted_or_never_written|MIXED:inspect_hist"));
	}
}

/**
 * Private scene view extension. It exists only to get a render-thread hook at a point where
 * (a) the frame's rendering is complete and (b) the correct swapchain image for THIS frame is
 * still the render thread's current viewport target.
 *
 * Both properties matter. The OXRVisionOS color swapchain is 2 images deep
 * (OXRVisionOSSession.cpp:55, OXRVisionOSBackbufferLength = 2) and FSceneViewport hands the render
 * thread a different one each frame via SetRenderTargetTextureRenderThread
 * (SceneViewport.cpp:2300), so a readback fired from a bare game-thread ENQUEUE_RENDER_COMMAND can
 * land after the NEXT frame has already acquired a different image and would silently measure the
 * wrong surface.
 */
class FVisionProAlphaProbeViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FVisionProAlphaProbeViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	/** Set from the game thread to request exactly one capture. */
	void Arm(uint32 InSequence)
	{
		PendingSequence.store((int32)InSequence);
		bArmed.store(true);
	}

	bool IsArmed() const { return bArmed.load(); }

	//~ ISceneViewExtension
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& /*Context*/) const override
	{
		// Costs nothing when disarmed - the extension is skipped entirely.
		return bArmed.load();
	}

private:
	// std::atomic rather than TAtomic: Core/Public/Templates/Atomic.h:528 states TAtomic is
	// unmaintained and new code should use std::atomic.
	std::atomic<bool> bArmed{ false };
	std::atomic<int32> PendingSequence{ 0 };
};

void FVisionProAlphaProbeViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	using namespace VisionProAlphaProbeInternal;

	check(IsInRenderingThread());

	// Consume the arm exactly once, even if several view families render this frame.
	bool bExpected = true;
	if (!bArmed.compare_exchange_strong(bExpected, false))
	{
		return;
	}
	const int32 Sequence = PendingSequence.load();

	if (InViewFamily.RenderTarget == nullptr)
	{
		ALPHAPROBE_LOG(TEXT("error=VIEWFAMILY_HAS_NO_RENDERTARGET seq=%d"), Sequence);
		if (GLog) { GLog->Flush(); }
		return;
	}

	// FSceneViewport::GetRenderTargetTexture() is thread-aware (SceneViewport.cpp:2094-2102): on the
	// render thread it returns RenderTargetTextureRenderThreadRHI, i.e. THIS frame's acquired
	// OXRVisionOS color swapchain image. That is exactly the texture XrEndFrame will blit into the
	// cp_drawable. Capturing the reference here pins it for the deferred readback below.
	FTextureRHIRef Target = InViewFamily.RenderTarget->GetRenderTargetTexture();
	if (!Target.IsValid())
	{
		ALPHAPROBE_LOG(TEXT("error=RENDERTARGET_TEXTURE_NULL seq=%d"), Sequence);
		if (GLog) { GLog->Flush(); }
		return;
	}

	// Defer to after RDG execution. Doing the readback inside a pass is illegal (ReadSurfaceData
	// needs the immediate command list and flushes the RHI thread); doing it here, before Execute,
	// would read a texture the GPU has not written yet. AddPostExecuteCallback
	// (RenderGraphBuilder.h:197) runs on the render thread once every pass in this graph - base
	// pass, inline alpha invert, MSAA resolve, translucent depth fixup - has been recorded and
	// submitted, which is the state the compositor will consume.
	GraphBuilder.AddPostExecuteCallback([Target = MoveTemp(Target), Sequence]() mutable
	{
		// ONE-SHOT ONLY. ReadSurfaceData below does ImmediateFlush(FlushRHIThread) and the Metal
		// backend then calls SubmitAndBlockUntilGPUIdle (MetalRenderTarget.cpp:159/:225) plus a
		// full-frame staging copy. Expect a visible multi-frame hitch. Never call this per frame.
		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

		const FRHITextureDesc& Desc = Target->GetDesc();
		const EPixelFormat Format = Desc.Format;
		const int32 Width  = Desc.Extent.X;
		const int32 Height = Desc.Extent.Y;
		const bool bSRGB = EnumHasAnyFlags(Desc.Flags, TexCreate_SRGB);

		ALPHAPROBE_LOG(TEXT("---- begin seq=%d frame=%llu ----"), Sequence, (unsigned long long)GFrameCounterRenderThread);
		ALPHAPROBE_LOG(TEXT("surface=OXRVisionOS_ColorSwapchainImage"));
		ALPHAPROBE_LOG(TEXT("surface_how=FSceneViewFamily.RenderTarget.GetRenderTargetTexture_on_render_thread"));
		ALPHAPROBE_LOG(TEXT("surface_note=blitted_verbatim_to_cp_drawable_by_MetalViewport_PresentImmersive_no_conversion"));
		ALPHAPROBE_LOG(TEXT("texture_ptr=0x%llx"), (unsigned long long)(UPTRINT)Target.GetReference());
		ALPHAPROBE_LOG(TEXT("native_metal_texture=0x%llx"), (unsigned long long)(UPTRINT)Target->GetNativeResource());
		ALPHAPROBE_LOG(TEXT("format=%s"), GetPixelFormatString(Format));
		ALPHAPROBE_LOG(TEXT("format_enum=%d"), (int32)Format);
		ALPHAPROBE_LOG(TEXT("width=%d"), Width);
		ALPHAPROBE_LOG(TEXT("height=%d"), Height);
		ALPHAPROBE_LOG(TEXT("array_size=%d"), (int32)Desc.ArraySize);
		ALPHAPROBE_LOG(TEXT("num_samples=%d"), (int32)Desc.NumSamples);
		ALPHAPROBE_LOG(TEXT("dimension=%s"), DimensionToString(Desc.Dimension));
		ALPHAPROBE_LOG(TEXT("srgb=%d"), bSRGB ? 1 : 0);

		if (Width <= 0 || Height <= 0)
		{
			ALPHAPROBE_LOG(TEXT("error=ZERO_SIZED_SURFACE"));
			ALPHAPROBE_LOG(TEXT("---- end seq=%d ----"), Sequence);
			if (GLog) { GLog->Flush(); }
			return;
		}

		const FIntRect Rect(0, 0, Width, Height);

		// Fixed, named probe points. Frame center plus the four quadrant centers.
		const int32 CX = Width / 2;
		const int32 CY = Height / 2;
		const int32 QX0 = Width / 4;
		const int32 QX1 = (Width * 3) / 4;
		const int32 QY0 = Height / 4;
		const int32 QY1 = (Height * 3) / 4;

		TArray<FSamplePoint> Samples;
		Samples.Add({ TEXT("center"),      CX,  CY  });
		Samples.Add({ TEXT("quad_tl"),     QX0, QY0 });
		Samples.Add({ TEXT("quad_tr"),     QX1, QY0 });
		Samples.Add({ TEXT("quad_bl"),     QX0, QY1 });
		Samples.Add({ TEXT("quad_br"),     QX1, QY1 });

		FProbeAccum Accum;
		bool bFloatPath = false;

		// FORMAT DISPATCH. Choosing the wrong readback path here is the single most likely way this
		// probe reports a confident wrong answer, so each branch names what it verified.
		if (Format == PF_FloatRGBA)
		{
			// MUST NOT use ReadSurfaceData->FColor for this format. ConvertRawR16G16B16A16FDataToFColor
			// (Engine/Source/Runtime/RHI/Public/RHISurfaceDataConversion.h:166) first scans the whole
			// frame for per-channel min/max and then RENORMALIZES every channel into that range before
			// calling ToFColor() - it would rescale the alpha channel by the frame's own alpha range
			// and destroy the exact quantity being measured. ReadSurfaceFloatData is a raw blit plus
			// memcpy on Metal (MetalRenderTarget.cpp:400) with no conversion at all.
			bFloatPath = true;
			ALPHAPROBE_LOG(TEXT("readback_path=ReadSurfaceFloatData_FFloat16Color"));
			ALPHAPROBE_LOG(TEXT("readback_exact=1"));
			ALPHAPROBE_LOG(TEXT("readback_note=FColor_path_would_renormalize_alpha_by_frame_minmax_and_was_avoided"));
			// rgba16Float has no sRGB variant in Metal, so the stored RGB is already linear even
			// though the swapchain was requested with TexCreate_SRGB.
			ALPHAPROBE_LOG(TEXT("rgb_encoding=linear"));

			// ReadSurfaceFloatData honors FReadSurfaceDataFlags::SetArrayIndex on Metal
			// (MetalRenderTarget.cpp:400 passes ArrayIndex through to CopyFromTextureToBuffer),
			// unlike ReadSurfaceData. Slice 0 is the left eye.
			FReadSurfaceDataFlags Flags(RCM_MinMax);
			Flags.SetArrayIndex(0);
			ALPHAPROBE_LOG(TEXT("array_slice_read=0"));
			ALPHAPROBE_LOG(TEXT("array_slice_note=ReadSurfaceFloatData_honors_SetArrayIndex_on_Metal"));

			TArray<FFloat16Color> Pixels;
			RHICmdList.Transition(FRHITransitionInfo(Target.GetReference(), ERHIAccess::SRVMask, ERHIAccess::CopySrc));
			RHICmdList.ReadSurfaceFloatData(Target.GetReference(), Rect, Pixels, Flags);
			RHICmdList.Transition(FRHITransitionInfo(Target.GetReference(), ERHIAccess::CopySrc, ERHIAccess::SRVMask));

			if (Pixels.Num() != Width * Height)
			{
				ALPHAPROBE_LOG(TEXT("error=READBACK_SIZE_MISMATCH got=%d expected=%d"), Pixels.Num(), Width * Height);
				ALPHAPROBE_LOG(TEXT("---- end seq=%d ----"), Sequence);
				if (GLog) { GLog->Flush(); }
				return;
			}

			for (const FFloat16Color& P : Pixels)
			{
				Accum.Accumulate((float)P.R, (float)P.G, (float)P.B, (float)P.A, /*bRGBIsSRGBEncoded*/ false);
			}

			for (FSamplePoint& S : Samples)
			{
				const FFloat16Color& P = Pixels[S.Y * Width + S.X];
				S.Rf = (float)P.R; S.Gf = (float)P.G; S.Bf = (float)P.B; S.Af = (float)P.A;
				S.R = FMath::Clamp(FMath::RoundToInt(S.Rf * 255.0f), 0, 255);
				S.G = FMath::Clamp(FMath::RoundToInt(S.Gf * 255.0f), 0, 255);
				S.B = FMath::Clamp(FMath::RoundToInt(S.Bf * 255.0f), 0, 255);
				S.A = FMath::Clamp(FMath::RoundToInt(S.Af * 255.0f), 0, 255);
			}
		}
		else if (Format == PF_B8G8R8A8 || Format == PF_R8G8B8A8 || Format == PF_A2B10G10R10)
		{
			// These three reach FColor without any rescaling:
			//   PF_B8G8R8A8   -> ConvertRawB8G8R8A8DataToFColor  (RHISurfaceDataConversion.h:80)  - pure memcpy
			//   PF_R8G8B8A8   -> ConvertRawR8G8B8A8DataToFColor  (RHISurfaceDataConversion.h:65)  - channel swizzle only
			//   PF_A2B10G10R10-> ConvertRawR10G10B10A2DataToFColor(RHISurfaceDataConversion.h:126) - requantize 10/2 -> 8
			// Dispatch is RHISurfaceDataConversion.h:919-945.
			ALPHAPROBE_LOG(TEXT("readback_path=ReadSurfaceData_FColor"));
			ALPHAPROBE_LOG(TEXT("readback_exact=%d"), (Format == PF_A2B10G10R10) ? 0 : 1);
			if (Format == PF_A2B10G10R10)
			{
				// 2-bit alpha: only four distinguishable coverage levels exist in the surface, so
				// the mid-alpha band and the premultiplication test are coarse by construction.
				ALPHAPROBE_LOG(TEXT("readback_note=alpha_is_2_bit_only_4_levels_0_85_170_255_after_requantize"));
			}
			ALPHAPROBE_LOG(TEXT("rgb_encoding=%s"), bSRGB ? TEXT("srgb_encoded") : TEXT("linear"));

			// Metal's RHIReadSurfaceData IGNORES FReadSurfaceDataFlags::SetArrayIndex - both of its
			// branches hardcode source slice 0 (MetalRenderTarget.cpp:144 and :209). Stating the
			// slice honestly rather than passing an index that silently does nothing.
			FReadSurfaceDataFlags Flags(RCM_MinMax);
			Flags.SetLinearToGamma(false);
			ALPHAPROBE_LOG(TEXT("array_slice_read=0"));
			ALPHAPROBE_LOG(TEXT("array_slice_note=MetalRHI_ReadSurfaceData_hardcodes_slice0_SetArrayIndex_is_ignored"));

			TArray<FColor> Pixels;
			RHICmdList.ReadSurfaceData(Target.GetReference(), Rect, Pixels, Flags);

			if (Pixels.Num() != Width * Height)
			{
				ALPHAPROBE_LOG(TEXT("error=READBACK_SIZE_MISMATCH got=%d expected=%d"), Pixels.Num(), Width * Height);
				ALPHAPROBE_LOG(TEXT("---- end seq=%d ----"), Sequence);
				if (GLog) { GLog->Flush(); }
				return;
			}

			const float Inv255 = 1.0f / 255.0f;
			for (const FColor& P : Pixels)
			{
				Accum.Accumulate(P.R * Inv255, P.G * Inv255, P.B * Inv255, P.A * Inv255, bSRGB);
			}

			for (FSamplePoint& S : Samples)
			{
				const FColor& P = Pixels[S.Y * Width + S.X];
				S.R = P.R; S.G = P.G; S.B = P.B; S.A = P.A;
			}
		}
		else
		{
			// Refusing beats guessing. An unhandled format routed through the FColor path could be
			// rescaled or gamma-converted on the way out and would produce a confident wrong number.
			ALPHAPROBE_LOG(TEXT("readback_path=NONE"));
			ALPHAPROBE_LOG(TEXT("error=UNHANDLED_PIXEL_FORMAT format=%s"), GetPixelFormatString(Format));
			ALPHAPROBE_LOG(TEXT("error_note=add_an_explicit_branch_after_checking_its_ConvertRAWSurfaceData_path_do_not_fall_through"));
			ALPHAPROBE_LOG(TEXT("---- end seq=%d ----"), Sequence);
			if (GLog) { GLog->Flush(); }
			return;
		}

		EmitResults(Accum, Samples, bFloatPath);

		// Config that produced these numbers, read at capture time so every block is self-describing
		// and can be correlated with whichever mode the on-device cycler was sitting in.
		LogCVar(TEXT("xr.OpenXRInvertAlpha"));
		LogCVar(TEXT("OpenXR.AlphaInvertPass"));
		LogCVar(TEXT("r.AlphaInvertPass"));
		LogCVar(TEXT("r.Mobile.VisionOS.InlineAlphaInvert"));
		LogCVar(TEXT("r.Mobile.PropagateAlpha"));

		ALPHAPROBE_LOG(TEXT("---- end seq=%d ----"), Sequence);

		// Device logs truncate without this. Same forced flush as PinchworkShowcaseSubsystem's
		// [ALPHAMODE] lines - this block IS the measurement, so losing it loses the run.
		if (GLog) { GLog->Flush(); }
	});
}

namespace
{
	// Created lazily on the first capture and kept alive for the process. Debug-only lifetime: it is
	// never explicitly unregistered, so it relies on FSceneViewExtensionBase's destructor at static
	// teardown. Fine for a diagnostic; do not copy this pattern into shipping code.
	TSharedPtr<FVisionProAlphaProbeViewExtension, ESPMode::ThreadSafe> GAlphaProbeViewExtension;
	uint32 GAlphaProbeSequence = 0;
}

void FVisionProAlphaProbe::CaptureAndLog()
{
	if (!IsInGameThread())
	{
		ALPHAPROBE_LOG(TEXT("error=CAPTURE_MUST_BE_CALLED_FROM_GAME_THREAD"));
		if (GLog) { GLog->Flush(); }
		return;
	}

	if (GEngine == nullptr)
	{
		ALPHAPROBE_LOG(TEXT("error=NO_ENGINE_YET"));
		if (GLog) { GLog->Flush(); }
		return;
	}

	if (!GAlphaProbeViewExtension.IsValid())
	{
		GAlphaProbeViewExtension = FSceneViewExtensions::NewExtension<FVisionProAlphaProbeViewExtension>();
	}

	if (GAlphaProbeViewExtension->IsArmed())
	{
		ALPHAPROBE_LOG(TEXT("note=ALREADY_ARMED_IGNORING_DUPLICATE_REQUEST"));
		if (GLog) { GLog->Flush(); }
		return;
	}

	++GAlphaProbeSequence;
	GAlphaProbeViewExtension->Arm(GAlphaProbeSequence);

	ALPHAPROBE_LOG(TEXT("armed seq=%u frame=%llu"), GAlphaProbeSequence, (unsigned long long)GFrameCounter);
	if (GLog) { GLog->Flush(); }
}

// Convenience trigger so a capture can be fired without a rebuild wherever a console/exec path
// exists (PIE, an exec binding, an in-game console). On device the practical trigger is a gesture
// calling FVisionProAlphaProbe::CaptureAndLog() directly - see INTEGRATION.md.
static FAutoConsoleCommand GVisionProAlphaProbeCmd(
	TEXT("vos.AlphaProbe"),
	TEXT("One-shot numeric readback of the visionOS compositor-facing color surface. Logs an [ALPHAPROBE] block. Stalls the GPU; do not spam."),
	FConsoleCommandDelegate::CreateStatic(&FVisionProAlphaProbe::CaptureAndLog));
