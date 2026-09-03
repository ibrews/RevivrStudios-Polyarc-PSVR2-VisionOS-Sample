# Lumenwork

A UE 5.8.2 test project for **Lumen global illumination on standalone XR headsets** — Apple Vision
Pro (Metal SM5), Android XR (Vulkan SM5), and Pico. It exists to answer one question honestly: can
Lumen be made to look good and run acceptably on mobile-class XR GPUs, or do we fall back to
Lumen reflections only?

Derived from the Polyarc PSVR2/visionOS sample. Only the **config** was carried over from the
Windows-side tuning session; everything else comes from the known-good macOS tree.

## Engine

Builds against `UnrealEngineVisionOS` @ `.claude/worktrees/ue582-xr-main-m5` (UE 5.8.2), which
carries the visionOS translucent depth-fixup for the deferred renderer. The 5.6.1 trunk root is
retired — see its `RETIRED-DO-NOT-BUILD.md`.

## Things to Try

1. **Build and deploy to Apple Vision Pro.**
   `./ue-avp-build.sh device` then install with `xcrun devicectl device install app`.
   Expect the documented `Touch UBT generated tiles` finalize bug — the fix is to re-run the
   `xcodebuild` line from the log's `params:` output by hand.
2. **Confirm you actually got SM5, not ES3.1.** Read the shader platform out of the *runtime*
   device log, never the ini. `IsMobilePlatform()` means "is this ES3_1", which is the trap that
   made an earlier Lumen fix dead code on `SP_METAL_SM5_IOS`.
3. **Verify Lumen Lite is active.** Look for `r.Lumen.FinalGatherMethod=0` in the applied cvars.
   The cvar numbering is inverted relative to the internal enum — `0` is the irradiance-field
   gather, which is the cheaper path.
4. **Check the frame counter.** A working run advances past `][0]`; a stalled one sits there. This
   discriminates "crashed" from "rendered nothing" faster than any log grep.
5. **Build the same project for Android XR / Pico** and compare. An Android XR build that runs on
   the XREAL Aura is expected to run on a Galaxy XR too.

## Status

Early. The AVP path is the furthest along; Android XR and Pico follow.
