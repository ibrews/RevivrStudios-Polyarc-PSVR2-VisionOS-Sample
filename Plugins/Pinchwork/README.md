# Pinchwork

**An OpenXR hand-tracking and gesture system for Unreal Engine.** Drop it into any UE project to get per-finger pinch detection, curl-pattern gesture recognition with per-user calibration, pinch-to-grab with throw physics, and gesture → Enhanced Input injection — built and tuned on Apple Vision Pro, and portable to any OpenXR hand-tracking runtime.

Pinchwork is a self-contained plugin: it depends only on Epic's OpenXR / Enhanced Input modules and ships none of your project's code. Add the components to a pawn and bind the events.

## Features

- **Per-finger pinches** — independent index-thumb, middle-thumb, ring-thumb, and pinky-thumb pinch events, each with its own threshold and release hysteresis (no jitter).
- **Curl-pattern gestures** — open palm, fist, peace, finger guns, rock-on, call-me, thumbs-up, and more, classified by per-finger extension signature with confidence + margin gating.
- **Per-user calibration** — walks the user through fist → open-hand and sets per-finger thresholds at the midpoint, adapting to hand size and joint flexibility instead of a hard-coded threshold.
- **Pinch-to-grab** — index-thumb pinch grabs the nearest actor with a `GrabComponent` *at the pinch point*, rides it on the hand each frame, and throws it on release scaled by hand velocity.
- **Gesture → Enhanced Input** — recognized gestures inject `IA_*` input actions each active frame, so existing Enhanced Input bindings consume hand gestures exactly like controller input.
- **Floating gesture label** — optional billboarded text above the hand showing the live gesture / finger-extension state, for on-device tuning.

## Requirements

- Unreal Engine 5.x with the **OpenXR**, **OpenXRHandTracking**, **XRBase**, and **EnhancedInput** plugins (Pinchwork enables them as dependencies).
- An OpenXR runtime that provides hand tracking (Apple Vision Pro via the visionOS OpenXR path, Quest, etc.).

## Quickstart

1. Copy the `Pinchwork` folder into your project's `Plugins/` directory and regenerate project files.
2. Enable **Pinchwork** in *Edit → Plugins* (it auto-enables its OpenXR / Enhanced Input dependencies). Restart the editor.
3. On your VR pawn, add two **Hand Tracking (OpenXR)** components — set **Is Right Hand** on one of them.
4. (Optional) Add a **Hand Skeletal Driver** component per hand if you want a rigged hand mesh driven by the tracked joints.
5. Bind the pinch / gesture delegates (`On Pinch Started`, `On Gesture Started`, …) in your pawn Blueprint and react.

## Things to Try

1. **See your gestures live.** Leave `Show Gesture Label` on, build to your headset, and make a fist / peace sign / finger guns — the detected gesture name floats above your hand in real time.
2. **Calibrate to your hand.** Keep `Auto Calibrate On Start` enabled; on launch, make a fist when prompted, then open your hand. Pinch detection now adapts to *your* finger geometry.
3. **Grab and throw.** Add a `GrabComponent` to any actor, then index-thumb pinch near it — it snaps to your pinch point. Flick and release to throw it with your hand's velocity.
4. **Drive Enhanced Input from gestures.** Bind `IA_IndexThumbPinch` (or any gesture action) in your Input Mapping Context and watch a pinch trigger it exactly like a controller button.
5. **Tune the pinch feel.** Adjust `Pinch Threshold Cm` and `Pinch Release Hysteresis Cm` on the component and feel how it changes the grab/release responsiveness on-device.

## License

MIT © 2026 Alex Coulombe. See [LICENSE](LICENSE).
