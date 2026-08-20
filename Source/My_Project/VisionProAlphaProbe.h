// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// VisionProAlphaProbe - numeric pixel/alpha readback of the surface the visionOS compositor
// actually receives, so defect D1 ("everything renders semi-transparent over passthrough in
// mixed immersion") can be settled by MEASUREMENT rather than by a human describing what they see.
//
// `devicectl device capture screenshot` does not work on the target device (CoreDeviceError 1001),
// so the UE log is the only channel off the headset. Every line this emits is prefixed
// [ALPHAPROBE] and is one machine-parseable key=value pair.
//
// WHICH SURFACE IS READ, AND WHY IT IS THE DECISIVE ONE
// -----------------------------------------------------
// This fork does NOT reach the visionOS compositor through Epic's stock OpenXR runtime path. It
// ships its own OpenXR runtime, OXRVisionOS, and the submit chain on device is:
//
//   mobile forward render (render-to-backbuffer)
//     -> FSceneViewport render target == the OXRVisionOS color swapchain image
//        (Engine/Source/Runtime/Engine/Private/Slate/SceneViewport.cpp:2410 allocates the
//         viewport's buffered frames from IStereoRenderTargetManager::AllocateRenderTargetTextures;
//         :2094-2102 FSceneViewport::GetRenderTargetTexture() returns the render-thread-current one)
//     -> FOXRVisionOSSession::XrEndFrame takes View0.subImage.swapchain and pulls
//        GetLastWaitedImage().Image
//        (Engine/Platforms/VisionOS/Plugins/Runtime/OpenXRVisionOS/Source/OXRVisionOS/Private/
//         OXRVisionOSSession.cpp:1313 / :1385 / :1399)
//     -> MetalRHIVisionOS::PresentImmersive -> FMetalViewport::PresentImmersive
//        (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalViewport.cpp:1181)
//     -> Context.CopyFromTextureToTexture(swapchain slice -> cp_drawable color texture slice)
//        (MetalViewport.cpp:1272) -> cp_drawable_encode_present
//
// That last step is a raw MTLBlit: no format conversion, no blend, no shader. The swapchain image
// is therefore BYTE-IDENTICAL to what the visionOS compositor receives, and it is the last surface
// in the chain that UE code can reach. That is what this probe reads.
//
// What each candidate surface could and could not prove:
//   * Scene color after post-processing - proves what the renderer produced. Does NOT prove what
//     the compositor received: on this path it is upstream of the inline alpha invert
//     (Renderer/Private/MobileShadingRenderer.cpp:2358), the MSAA resolve, and the visionOS
//     translucent-depth fixup. Reading it cannot distinguish "the renderer wrote bad alpha" from
//     "a later pass rewrote it".
//   * The viewport backbuffer (RHIGetViewportBackBuffer) - on visionOS immersive this surface is
//     NOT in the presented path at all; PresentImmersive bypasses it and copies swapchain ->
//     drawable directly. Reading it would yield a confident number about a surface nobody sees.
//   * The cp_drawable color texture immediately before cp_drawable_encode_present - this IS the
//     final surface, but it is only valid inside FMetalViewport::PresentImmersive on the RHI
//     thread, is unreachable from project code, and differs from the swapchain image only by the
//     raw blit above. See INTEGRATION.md for the exact engine patch if it is ever wanted.
//
// COST
// ----
// A full-frame readback flushes the RHI thread and blocks until the GPU is idle
// (FRHICommandListImmediate::ReadSurfaceData -> ImmediateFlush(FlushRHIThread), and the Metal
// backend additionally calls SubmitAndBlockUntilGPUIdle - see
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRenderTarget.cpp:159). Expect a multi-frame
// hitch. This is strictly ONE-SHOT / on demand - never per frame.

#pragma once

#include "CoreMinimal.h"

/**
 * One-shot numeric readback of the visionOS compositor-facing color surface.
 *
 * Call CaptureAndLog() from the game thread (a gesture handler, a subsystem, or the
 * `vos.AlphaProbe` console command registered in the .cpp). It arms a single capture; the actual
 * readback runs on the render thread at the end of the next rendered view family, and the whole
 * [ALPHAPROBE] block is emitted with a forced GLog->Flush() so nothing is lost to device log
 * truncation.
 *
 * Safe to call again after the previous capture has completed; a second call while one is already
 * armed is ignored (and logged as such) rather than queueing.
 */
class FVisionProAlphaProbe
{
public:
	/** Arm a single capture. Game thread only. */
	static void CaptureAndLog();
};
