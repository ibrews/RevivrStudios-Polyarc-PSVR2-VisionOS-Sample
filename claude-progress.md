# ⭐ CURRENT — Pinchwork plugin spin-off (2026-06-06)

**Goal:** extract the owned hand-tracking system into a standalone, sellable UE plugin +
clean-room demo, leaving the Polyarc/Revivr (MIT) PSVR2 sample code untouched.

**Branch:** `spinoff/pinchwork-plugin` — NOT merged to `main`, NOT pushed. All reversible.

**Decisions (user-approved):** product = plugin + demo template; demo = clean-room (Epic +
own content only, zero Polyarc); engine fix = separate `.patch`. Attribution = **Alex Coulombe**
(ibrews rule), MIT.

**Done + VERIFIED this session:**
- `Plugins/Pinchwork/` — owned MIT plugin. Both components (`HandTrackingComponent`,
  `HandSkeletalDriverComponent`) moved in with git history; `MY_PROJECT_API`→`PINCHWORK_API`.
- Seam was already dead: removed vestigial `#include "GamepadInputSetup.h"` → plugin has ZERO
  Polyarc deps. (grab is self-contained; gun-fire injects via Enhanced Input.)
- Dev project kept working: plugin enabled in `.uproject`, `[CoreRedirects]` repoint
  `VRPawn.uasset`, `My_Project.Build.cs` depends on Pinchwork.
- ✅ **Compile verified** headless: `Module.Pinchwork.cpp` builds, `UnrealEditor-Pinchwork.dylib`
  links, `** BUILD SUCCEEDED **` (incremental `My_ProjectEditor`, ~92s).
- ✅ **Redirects verified** headless (linker load): classes resolve at `/Script/Pinchwork.*`,
  `vrpawn_loaded=True`, zero old-path class-resolution errors.
- ✅ Phase 5: `Plugins/Pinchwork/EnginePatches/0001-arm64-lightmass.patch` (+ EULA-carveout README),
  integrity-checked via `git apply --reverse --check`.

**⚠️ OPEN FLAGS:**
1. **Plugin not yet portable** — its constructor still hard-refs `/Game/...` content (the `IA_*`
   actions). Builds *inside the dev project*; a blank-project buyer won't get the input actions
   until **Phase 3** migrates content into the plugin's `Content/` (editor + redirector fixup).
2. **Engine commit `4f59b2fea5c6` is LOCAL-ONLY** — the arm64-Lightmass port is NOT pushed to
   `ibrews/UnrealEngine` (`branch -r --contains` empty). At risk until pushed. (The `.patch` in
   the plugin is a mitigation, but the engine fork itself should be pushed.)

**PENDING (need the UE editor, via ECABridge/ue5-mcp — user drives the editor, I script it):**
- Phase 3: migrate `IA_*` input actions + WA/glass materials into `Plugins/Pinchwork/Content/`,
  repoint the C++ `ConstructorHelpers` paths `/Game/...`→`/Pinchwork/...`, resave `VRPawn`
  (bakes new class path; lets `CoreRedirects` be dropped later).
- Phase 4: clean-room demo `.uproject` (Epic + own content only).
- Phase 6: create `ibrews/Pinchwork` + demo repos (README-first), after 3–4 verified.

---

# Progress — make the two travel-test levels distinct PLACES

**Date:** 2026-06-02
**Goal:** Make Level A (`VRTemplateMap`) and Level B (`TravelTestMap`) read as two different
*places*, not just different light colors. Headless-only (interactive editor + ECABridge are
flaky on this 5.6.1 fork). Do NOT touch level-travel C++ or the engine fork.

## Theme chosen (user-approved)
- **Level A = "Cobalt Lab"** — cool, metal/tech.
- **Level B = "Stone Courtyard"** — warm, sandstone/wood/greenery.

## What changed (content only)
| | Level A — Cobalt Lab (`VRTemplateMap`) | Level B — Stone Courtyard (`TravelTestMap`) |
|---|---|---|
| Floor (Floor/Floor2) | M_Tech_Panel | M_Rock_Sandstone |
| Walls | M_Metal_Brushed_Nickel | M_Brick_Cut_Stone |
| Accent cubes (×7) | M_Metal_Chrome | M_Wood_Walnut |
| Table | M_Metal_Brushed_Nickel | M_Wood_Walnut |
| Grab cubes (×21) | kept default blue grid (MI_Cube_01) | re-skinned M_Wood_Walnut |
| Sci-fi cube | kept (lab hero) | removed |
| Added props | 2× Pillar_50x500 (nickel) + chrome SM_Statue | SM_Statue + 3× SM_Rock + 4× SM_Bush + SM_TableRound + SM_Chair |
| Directional+Sky light | cool (0.66,0.76,1.0) int 2.5 | warm (1.0,0.74,0.50) int 3.0 |
| Campfire point lights | left warm in BOTH (distant ambient feature) | same |

Material note: big floor/wall pieces are stretched `SM_Cube`s (0–1 UVs), so structured textures
(hex, cobblestone) would smear into one giant tile. Uniform materials went on the large floors
(Tech_Panel / Rock_Sandstone); detailed textures (chrome, brick, wood) on walls/accents/props
where UVs read ~1:1. Same approved palette, just surface-assigned to look clean.

Spawned props are labeled `LVLGEN_*` so the design script is idempotent (it deletes prior
`LVLGEN_*` actors before re-spawning).

## BuiltData fix (was the key risk)
TravelTestMap was a byte-copy of VRTemplateMap → it shared `VRTemplateMap_BuiltData`
(identical MapBuildDataId). Fixed by deleting it and re-creating via
`EditorAssetLibrary.duplicate_asset` (re-keys MapBuildDataId), then baking each level
separately (`build_light_maps`, QUALITY_PREVIEW).
**Verified:** `TravelTestMap_BuiltData.uasset` now exists as its OWN file; md5 differs from
`VRTemplateMap_BuiltData.uasset` (746,957 differing bytes) → genuinely independent baked lighting.

## Status — ALL DONE
1. ✅ Re-duplicate TravelTestMap (re-key MapBuildDataId)
2. ✅ Design Level A (Cobalt Lab) — verified on disk
3. ✅ Design Level B (Stone Courtyard) — verified on disk
4. ✅ Bake both levels separately — distinct BuiltData confirmed (md5 differ)
5. ✅ Cook + package VisionOS — `BUILD SUCCESSFUL`, both maps cooked WITH their own
   `_BuiltData` (`.uasset`+`.uexp` each), fresh `my_project-visionos.pak` (38.6 MB) →
   `Archive/VisionOS/My_Project.app` (built 18:03)
6. ✅ Install to AVP — `App installed: com.alex.MyProject` on device 2642855C
   - GOTCHA: first `devicectl install` failed with CoreDeviceError 3002 "Connection
     interrupted" (transient tunnel drop, common when AVP asleep/not worn). **Retrying the
     exact same command succeeded.** No code/asset change needed.

## Git (uncommitted — left for user review)
Working tree has: `VRTemplateMap.umap` (M), `TravelTestMap.umap` (M),
`VRTemplateMap_BuiltData.uasset` (M, rebaked), `TravelTestMap_BuiltData.uasset` (?? new).
NOT committed (no commit was requested). Ready to commit when desired.

## Round 2 (2026-06-02) — sound, gesture input actions, pinch-grab, texture fix

User feedback: levels look good; add a scene-load sound; floor/walls look stretched;
add pinch-to-grab. Clarified: codify gestures as Enhanced Input actions so the VRPawn
grab can consume `IA_IndexThumbPinch` alongside the grip.

1. **Scene-load sound** — `HandTrackingComponent::BeginPlay` (left instance only) plays a
   one-shot per level: Level A = `Light01` (electric), Level B = `Starter_Birds01` (birds).
   Editable via `SceneLoadSoundLevelA/B`; defaults loaded by path if unset.
2. **Gestures → Enhanced Input actions** — created 6 per-hand IA assets in
   `/Game/VRTemplate/Input/Actions/Hands/` (`IA_IndexThumbPinch/MiddleThumbPinch/PinkyThumbPinch`
   × `_Left/_Right`, duplicated from `IA_Grab_Right`). `HandTrackingComponent` injects them via
   `InjectInputForAction` each frame the gesture is active (`bInjectGestureInputActions`).
   These are now bindable in the VRPawn BP ("add IndexThumbPinch alongside the grip inputs").
3. **Pinch-to-grab** — `HandTrackingComponent::NotifyPinchGrab(bool)` (on index-pinch start/end,
   incl. tracking-loss release) calls new `UGamepadInputSetup::HandlePinchGrab(bRight, bPressed)`,
   which reuses the existing `TryGrab`/`ReleaseGrab` with HOLD semantics (grab on pinch, drop on
   release). CVar `SpatialAccessory.PinchGrab` (default 1) toggles it (turn off once the VRPawn BP
   binds `IA_IndexThumbPinch` itself). Grabs any actor with a `GrabComponent` (IsGrabbableActor).
4. **Texture stretch fix** — big `SM_Cube` floor/walls had 0-1 UVs (textures smeared ~20x). Built
   a triplanar world-aligned master `M_WorldAligned_v2` (WorldAlignedTexture MF → BaseColor) +
   instances `MI_WA_TechPanel/Steel/Sandstone/CutStone`; assigned A floor=TechPanel walls=Steel,
   B floor=Sandstone walls=CutStone. Accent cubes/props keep StarterContent mats (already ~1:1).
   No re-bake needed (material swap doesn't invalidate lightmaps).

Cook-time gotcha handled: IA assets + the two sounds are loaded by **string path**
(StaticLoadObject), not hard refs, so added `+DirectoriesToAlwaysCook` for
`/Game/VRTemplate/Input/Actions/Hands` and `/Game/StarterContent/Audio` in DefaultGame.ini.

Headless gotcha (round 2): material-graph `connect_material_property` BaseColor output name —
`"XYZTexture"` FAILS; use `""` (first output of the function call). `delete_asset` on a
referenced/loaded material silently no-ops then `create_asset` crashes ("already exists") — so
build a new master under a fresh name and reparent instances instead of delete+recreate.
Orphan: the first (broken) `M_WorldAligned` is now unreferenced (harmless, won't cook).

Build: editor Mac target compiled clean (`BUILD SUCCEEDED`). Cook+install in progress.

## Round 3 (2026-06-02) — stretch/bakes/ball/transparency/hands

User feedback after testing round-2 build: floor+ceiling still stretched; scenes look
unbaked; courtyard still has the ball; transparency (glass statue + Level A/B text) renders
badly over passthrough; swap hand mesh to walnut in the wooded level.

1. **Texture stretch (real fix)** — the `WorldAlignedTexture` MF (round 2) wired up clean
   (chain verified) but visually still stretched, so it wasn't world-projecting. Replaced with
   a **manual triplanar** master `M_TriPlanar` (WorldPosition / TileSize → 3 axis-masked
   `TextureSampleParameter2D "BaseTex"` samples, blended by `abs(VertexNormalWS)` components;
   exact for axis-aligned box surfaces, no normalization needed). 22/22 connects OK, BaseColor
   wired. Reparented `MI_WA_*` to it (params carry by name). `M_WorldAligned`/`_v2` now orphaned.
2. **Glass statue** — B's `LVLGEN_Statue_Center` was `M_StatueGlass` [TRANSLUCENT] (the SM_Statue
   default) → poor passthrough compositing. Set to opaque `M_Rock_Marble_Polished`.
3. **Ball** — destroyed `SM_Ball_01` in TravelTestMap (B). Still present in A (lab) intentionally.
4. **Level A/B text** — was `DrawDebugString` (debug canvas → ghosts over MR). Replaced with an
   opaque `UTextRenderComponent` (`DefaultTextMaterialOpaque`), billboarded to face the camera,
   left-instance-only. `DrawLevelLabel()` is now non-const. NOTE: billboard orientation
   (`(CamLoc-Anchor).Rotation()`) unverified on-device — may need a 180° yaw flip if mirrored.
   The per-hand gesture label (`UpdateGestureLabel`, still DrawDebugString) was NOT converted.
5. **Hand mesh walnut (B)** — `HandSkeletalDriverComponent`: new `WoodedLevelHandMaterial`
   (M_Wood_Walnut via CDO finder) + `IsWoodedLevel()` (map name == TravelTestMap); applied to all
   poseable slots in `EnsurePoseablesInitialized`. CVar-free; toggle `bSwapHandMaterialInWoodedLevel`.
6. **Lighting** — re-baked BOTH at `QUALITY_MEDIUM` as the FINAL save (after all content edits),
   so valid bakes reach the cook. (Round-2 "no bakes" likely: PREVIEW + flat material, and/or
   re-saving maps after the bake.) Lesson: **bake LAST, never re-save a level after baking.**

Editor target compiled clean. Bake(MEDIUM)+cook chained in background → install next.
→ INSTALLED (cook4, exit 0, com.alex.MyProject). M_TriPlanar cooked.

### Diagnosis: passthrough transparency + lighting (rendering-config findings)
- **`r.AllowStaticLighting=False`** (+ `r.ForwardShading=True`, `r.MobileHDR=False`) → lighting is
  FULLY DYNAMIC; lightmaps are ignored at runtime. So `build_light_maps` (all 3 rounds) had NO
  runtime effect — "scenes look unbaked" is BY DESIGN. Baked GI/shadows would need
  `AllowStaticLighting=True` + Stationary/Static lights + shader recompile + bake (project-wide).
- **Passthrough transparency**: visionOS mixed immersion composites over passthrough via per-pixel
  premultiplied alpha (Apple WWDC24). Opaque→alpha1→solid (works); translucent + DrawDebug text
  don't emit correct compositing alpha (worsened by MobileHDR=False/forward, no alpha propagation;
  fork is full-immersion-tuned, no passthrough alpha handling) → ghosting. True smooth glass over
  passthrough ≈ engine-level work (out of scope). Achievable: opaque reflective "glass-look"
  (Fresnel+reflections) or BLEND_Masked; opaque TextRender already = proper text.
  Full writeup: KB intelligence/techniques/visionos-ue-passthrough-translucency.md.

> ⚠️ SUPERSEDED 2026-06-02 (Round 5): "no alpha propagation / fork has no passthrough alpha
> handling / engine-level work out of scope" is WRONG. The fork ALREADY ships the full
> passthrough alpha pipeline (r.Mobile.PropagateAlpha=1 + FInlineAlphaInvert) and it is
> mathematically correct for BLEND_Translucent ON PAPER (verified vs stock UE 5.8 — identical,
> and the active on-device path is METAL_ES3_1_IOS mobile-forward with PropagateAlpha=1 +
> inline invert running). BUT on-device it STILL ghosts (user: "faint ghost/see-through" =
> translucent coverage≈0). Root cause under active investigation: real visionOS multiview-path
> bug vs. M_StatueGlass simply being low-opacity. Decisive 0.85-opacity unlit-translucent test
> (/Game/M_PassthroughTest on the statue) cooking now. See KB technique doc for the live trace.

## Round 4 (2026-06-02) — pinch-grab feel fixes + engine-transparency spun off

User feedback on the round-3 build: (1) grabbed cube snaps its center to the WRIST, should be at
the index-thumb pinch point; (2) released cube has no throw velocity, drops straight down.
Root cause: the pinch routed through `UGamepadInputSetup::TryGrab`, which attaches to the
MotionController GRIP component (≈wrist) and imparts no velocity.

**Fix — real pinch-grab in `HandTrackingComponent`** (it has the live thumb/index data each tick):
- New `PinchAnchor` USceneComponent kept on the thumb-index midpoint every Tick.
- `TryPinchGrab(pinchMidpoint)`: sphere-overlap (`GrabRadiusCm`=15) for the nearest actor with a
  GrabComponent, disable physics, attach with **Location=SnapToTarget** (object origin → pinch
  point) + Rotation/Scale=KeepWorld. So the object rides the pinch point, not the wrist.
- `UpdateHeldActorFollow` (Tick): moves the anchor to the pinch point + tracks smoothed hand
  velocity.
- `ReleasePinchGrab`: detach, re-enable physics, `SetPhysicsLinearVelocity(handVel * ThrowVelocityScale)`
  (`ThrowVelocityScale`=1.5) → the cube throws.
- `NotifyPinchGrab` now calls these directly (no longer routes to `UGamepadInputSetup`).
- `UGamepadInputSetup::HandlePinchGrab` is now UNUSED (dead) — the gamepad R1/L1 grip-grab there
  is unchanged (grip attach is correct for actual controllers). Remove HandlePinchGrab in cleanup.
- Editor target compiled clean; C++-only change (no content/bake change) → cook5 + install (bg).

## Engine-level passthrough transparency → spun off to its own chip/session
`r.Mobile.PropagateAlpha` (MobileBasePassRendering.cpp) / `IsMobilePropagateAlphaEnabled`
(SceneUtils.cpp:47) is the lever; visionOS present is in MetalRHI (MetalViewport.cpp /
MetalRHIVisionOSBridge.h, cp_drawable). Compositor wants PREMULTIPLIED alpha (WWDC24). Full
brief in the spawned chip + KB `intelligence/techniques/visionos-ue-passthrough-translucency.md`.
Engine changes are now ALLOWED (team lands fork PRs).

## Final verification still owed to the USER (needs the headset)
Wear the AVP, launch My_Project, and pinky-pinch to travel A↔B: confirm A reads as a cool
metal lab and B as a warm stone courtyard. Adjust any prop height/overlap on-device if needed
(re-run `/tmp/design2.py` then `/tmp/bake.py` then re-cook/install).

## HEADLESS WORKFLOW THAT WORKS (gotchas)
- Drive edits with: `UnrealEditor-Cmd <uproject> -run=pythonscript -script=<py> -stdout -unattended -nopause -nosplash -NoLogTimes`
- **`EditorActorSubsystem.spawn_actor_from_object` CRASHES** natively in this pythonscript
  commandlet (no traceback; process dies right at the call, with/without `-AllowCommandletRendering`).
  **WORKAROUND: `spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)` then
  `actor.static_mesh_component.set_static_mesh(mesh)`** — works cleanly.
- `EditorAssetLibrary.does_asset_exist` returns False early in a commandlet until the registry is
  scanned. Call `AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([path], True)`
  before delete/duplicate, or the duplicate fails with "already exists".
- Bake (`build_light_maps`) DOES need `-AllowCommandletRendering`; spawning does NOT. So design
  (spawn/reskin) runs in one commandlet WITHOUT the flag, bake runs in a separate one WITH it.
- `unreal.log_flush()` after each log line → the last line printed pinpoints a native crash.
- "Unable to locate CrashReporterClient" is benign shutdown noise on this fork (prints on
  successful runs too) — NOT a crash indicator by itself.
- Reusable scripts: `/tmp/design2.py` (duplicate+design A+B), `/tmp/bake.py` (bake both),
  `/tmp/verify.py` (inventory check), `/tmp/inventory_levels.py` (full actor dump).

## Failed approaches (don't retry)
- `/tmp/design_levels.py` v1 used `spawn_actor_from_object` → native crash mid-run. Superseded by
  `/tmp/design2.py` using `spawn_actor_from_class`.
- Guarding the duplicate behind `does_asset_exist` without a registry scan → skipped the delete →
  `duplicate_asset` failed "already exists".

## TODO / on-device check
- Verify spawned StarterContent prop heights (placed at z=0 assuming base pivots; rocks/bushes
  half-sunk would still look natural). Fine-tune positions on-device if any float/clip.
- Confirm grab-cube wood re-skin survives the Grabbable BP at runtime (override stuck in editor).

## Round 6 (2026-06-03) — passthrough translucency: diagnosis RESOLVED + interim deferred
On-device proof: a 0.85-opacity translucent material composites **NEAR-SOLID** over passthrough →
the fork's alpha pipeline WORKS. Earlier "ghosts / out-of-scope / fork has no alpha handling" was
WRONG; M_StatueGlass is just low-Fresnel-opacity (correctly faint). Remaining defect = "blocky
alpha" on translucent geometry = visionOS depth/alpha invariant (translucency writes alpha but NOT
scene depth; Apple WWDC24 #10092 "zero depth for zero alpha"; Godot #109975). Engine fix spec'd →
**draft fork PR github-polyarc/UnrealEngineVisionOS#4** (build next session).
Full diagnosis + fix + build gotchas in KB:
`intelligence/techniques/visionos-ue-passthrough-translucency.md`, `ue-visionos-cook-build-gotchas.md`,
`intelligence/decisions/2026-06-03-visionos-passthrough-translucency.md`.

State for next session:
- Statue (`LVLGEN_Statue_Center` in TravelTestMap) is currently **M_PassthroughTest** (0.85 cyan
  translucent — shows the working pipeline + the blocky edge artifact).
- TODO (interim opaque glass-look — env flakiness blocked it tonight; editor pythonscript
  commandlets kept getting SIGTERM'd ~1 min in): recreate `M_GlassLookOpaque` — new Material,
  BLEND_Opaque DefaultLit; BaseColor (0.02,0.06,0.08); Roughness 0.06; Specular 1; Fresnel(exp 4)
  → Multiply by (0.25,0.70,1.0) → Emissive. Assign to the statue, save, then clean full cook +
  `-skipbuild` + install. (Script was at /tmp/glasslook.py.)
- Then implement the engine fix on PR #4's branch `fix/visionos-passthrough-translucent-depth` +
  build (Renderer module rebuild). See the PR design doc + KB technique doc for code anchors.

## Round 5 (2026-06-03) — rename + grab feel v2 + keep-in-RAM + forest hands (MY work, uncommitted)
This is the PROJECT-side work done in parallel with the chips (passthrough/icon/Lightmass). All
**uncommitted in the working tree** — see the inventory + pending batch below.
- **Rename → "Pinchwork".** `Config/DefaultGame.ini` `ProjectName=Pinchwork`; `Config/DefaultEngine.ini`
  `BundleDisplayName=Pinchwork` + `BundleName=Pinchwork` (was My_Project/MyProject). Bundle ID stays
  `com.alex.MyProject`. Layered visionOS app icon delivered by the icon chip →
  `Build/VisionOS/AppIconSource/` + `Build/VisionOS/Resources/Assets.xcassets/` (untracked).
  KB: `visionos-ue-layered-app-icon.md`.
- **Grab feel v2 (extends round 4)** in `HandTrackingComponent`:
  - **Rotate-with-hand:** Tick captures `CurrentPinchRotation = GetKeypointWorldTransform(Palm).GetRotation()`
    (guarded on non-zero); the held actor's anchor now applies that rotation, so a grabbed object
    turns with the hand instead of staying axis-locked.
  - **Hide-on-grab:** `SetHeldHandMeshHidden(true)` on grab finds the sibling `UHandSkeletalDriverComponent`
    (matching `bIsRight`) and calls its `SetHandMeshHidden` so the hand mesh doesn't z-fight the held
    object; restored on release.
  - Still uses round-4 pinch-point attach (`SnapToTarget`) + throw (`HeldPinchVelocity*ThrowVelocityScale`).
- **Gesture → Enhanced Input Actions** (round 3 follow-through, complete): 6 new IAs
  `Content/VRTemplate/Input/Actions/Hands/IA_{Index,Middle,Pinky}ThumbPinch_{Left,Right}.uasset`
  (untracked). `HandTrackingComponent` injects the active per-hand IA each Tick via
  `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` (gated on `bInjectGestureInputActions`).
- **Opaque level label** (passthrough-safe): `DrawLevelLabel()` replaced `DrawDebugString` with a
  `UTextRenderComponent` using `/Engine/EngineMaterials/DefaultTextMaterialOpaque`, billboarded to the
  camera. ⚠️ orientation unverified on-device — may need a 180° yaw flip.
- **Forest-level hand = walnut wood:** `HandSkeletalDriverComponent` gained `WoodedLevelHandMaterial`
  (default `/Game/StarterContent/Materials/M_Wood_Walnut`), `bSwapHandMaterialInWoodedLevel`,
  `WoodedLevelName="TravelTestMap"`, `IsWoodedLevel()`. Applies walnut to all poseable slots in B.
  **⚠️ BUG → needs `fix_hand_material.py` (see pending batch):** `M_Wood_Walnut` lacks
  `bUsedWithSkeletalMesh`, so on the skinned hand it falls back to **WorldGridMaterial** (the grid the
  user saw in the forest). Setting that usage flag cooks the skeletal permutation and fixes it.
  KB: `ue5-material-sampler-mismatch.md` § "WorldGridMaterial from a missing usage flag".
- **Keep textures/materials in RAM across level travel** (kills the ~1s pop-in): `UGamepadInputSetup`
  (a `GameInstanceSubsystem` → persists across `OpenLevel`) gained
  `UPROPERTY() TArray<TObjectPtr<UObject>> KeepAliveAssets` + `PreloadPersistentAssets()` (called from
  `Initialize()`). It `StaticLoadObject`s 19 shared assets and holds hard refs so they stay resident
  between the two maps (no unload/reload): the triplanar master + `MI_WA_*` + grid MIs, walnut/chrome/
  nickel/marble, the StarterContent props (Statue/Rock/Bush/TableRound/Chair), Pillar, and the VREditor
  cube/ball MIs. **Written but not yet compiled/cooked.**
- `UGamepadInputSetup::HandlePinchGrab` is now **dead code** (grab moved fully into HandTrackingComponent
  in round 4). The gamepad R1/L1 grip-grab is unchanged. Remove HandlePinchGrab in a cleanup pass.
- **Sci-fi cube WorldGrid (observed, not yet fixed):** the user reported the big floating cuboid with the
  metal shader rendered as WorldGridMaterial *in the transparency chip's build*. It's a STATIC mesh, so
  not a usage-flag issue — its Fab `Default` material fails to compile for mobile `METAL_ES3_1_IOS`. I
  never touched the cube and it rendered fine in my cook5, so my pending build likely renders it fine →
  **verify in my build; if it grids, reskin to a clean mobile-safe metal** (brushed nickel / steel).
  KB: `ue5-material-sampler-mismatch.md` § "WorldGridMaterial from a mobile compile failure".

## ⛔ CURRENT STATE & PENDING BATCH — READ THIS FIRST (handoff to the rebuild session, 2026-06-03)

> ⚠️ SUPERSEDED 2026-06-03 PM by **Round 7** (bottom of this file). The cook+install in the PENDING BATCH
> below is DONE: WorldGrid on device is FIXED (Cause E — `bUsedWithStaticLighting`) + VERIFIED on the AVP,
> and the M_Wood_Walnut skeletal flag was already set. What's still open is now the **sci-fi cube**
> (chrome+Movable+rotator, `Tools/headless/fix_scifi_cube.py`) and **real translucency** (PR #4) — both
> handed to the engine session, AND the shared engine editor is currently BuildId-inconsistent (needs a
> clean rebuild first). **Read the Round-7 section at the bottom before acting on anything below.**

### Working tree is LOADED — do NOT discard it
**599 lines of uncommitted C++ + new content assets live ONLY in this working tree.** A clean rebuild
that resets/cleans the tree (e.g. `git checkout .`, `git clean`, `git stash` then forget) **destroys
unrecovered feature work.** `git status` before any tree-touching op. Inventory:
- **Modified C++ (uncommitted):** `Source/My_Project/{HandTrackingComponent,HandSkeletalDriverComponent,GamepadInputSetup}.{h,cpp}` (+599/−17).
- **Modified config:** `Config/DefaultGame.ini`, `Config/DefaultEngine.ini`, `Build/IOS/UBTGenerated/Info.Template.plist`.
- **Modified maps:** `Content/VRTemplate/Maps/{VRTemplateMap,TravelTestMap}.umap` + `VRTemplateMap_BuiltData.uasset`.
- **Untracked content:** the 6 `IA_*ThumbPinch_*` IAs; `M_TriPlanar` + `MI_WA_{TechPanel,Steel,Sandstone,CutStone}`
  + orphaned `M_WorldAligned`/`_v2`; `M_PassthroughTest` (chip's translucent test); `TravelTestMap_BuiltData`;
  `Build/VisionOS/AppIconSource/` + `Build/VisionOS/Resources/Assets.xcassets/` (icon chip).
- These are intentionally uncommitted ("left for user review"). The rebuild can recompile/recook against
  them as-is — first cook after this state is normal-speed (shaders cached at `AllowStaticLighting=False`).

### Engine fork state (NOT broken — updated from earlier notes)
- Fork on branch **`fix/visionos-lightmass-arm64`** with a freshly-built **native arm64 `UnrealLightmass`**
  (`Engine/Binaries/Mac/UnrealLightmass` = `Mach-O arm64`, links arm64 Embree). The earlier "deleted
  x86_64 binary / half-broken" worry is **RESOLVED** — the arm64-Lightmass chip rebuilt it and it runs
  natively. Baking is now *possible* on this M1 Max. KB: `apple-silicon-ue-lightmass-arm64.md` (full saga:
  why x86_64 Lightmass crashes under Rosetta/Embree-AVX, the arm64 SSE→NEON port, build commands).
- `r.AllowStaticLighting=False` (dynamic lighting) is the **current working runtime state**. To actually
  ship baked lighting you must flip it True AND bake with the arm64 Lightmass AND the maps already have
  Stationary lights + `force_no_precomputed_lighting=false` (run `Tools/headless/static_bake2.py` with
  `-AllowCommandletRendering`). The chips also have an open passthrough-depth PR (`#4`, branch
  `fix/visionos-passthrough-translucent-depth`) for real glass.

### PENDING BATCH I was about to run (was HELD because the transparency chip was cooking the same project)
Only one cook of this project at a time (UBT/AutomationTool single-instance mutex). When the editor/cook
is free, run in order:
1. **`Tools/headless/fix_hand_material.py`** (no `-AllowCommandletRendering`) → sets
   `used_with_skeletal_mesh=True` on `M_Wood_Walnut`, recompiles, saves. Fixes the forest-hand grid.
2. **Compile editor target** (fast C++ check):
   `Engine/Build/BatchFiles/Mac/Build.sh My_ProjectEditor Mac Development -project=<uproject> -WaitMutex`.
3. **One cook + install** picking up the whole uncommitted batch (rename + grab v2 + keep-in-RAM + walnut fix):
   `RunUAT.sh BuildCookRun -project=<uproject> -platform=VisionOS -build -cook -stage -pak -package -archive
   -archivedirectory=<proj>/Archive -clientconfig=Development`
   then `xcrun devicectl device install app --device 2642855C-6B73-5D5B-9387-6B110E7A7CF3
   <proj>/Archive/VisionOS/Pinchwork.app` (CoreDeviceError 3002 = transient, retry).
4. **On-device verify:** sci-fi cuboid material (reskin to brushed-nickel/steel if it grids); grab
   hide-on-grab + rotate-with-hand; level label orientation (180° yaw flip if mirrored); travel pop-in gone.
- **Last fully-good installed build = cook5** (round-4: pinch-point grab + throw, dynamic lighting, pre-rename).

### Durable headless scripts
Moved out of ephemeral `/tmp` → **`Tools/headless/`** (mirrored to KB `projects/pinchwork/scripts/`).
See `Tools/headless/README.md` for run commands + per-script purpose. The old "`/tmp/*.py`" references
elsewhere in this doc are superseded by `Tools/headless/*.py`.

### KB coverage (all pushed)
`headless-ue-python-commandlet-gotchas.md`, `apple-silicon-ue-lightmass-arm64.md`,
`visionos-ue-passthrough-translucency.md`, `visionos-ue-layered-app-icon.md`, `visionos-ue-hand-pinch-grab.md`,
`ue-visionos-cook-build-gotchas.md`, `ue-visionos-openlevel-frame-lifecycle-fix.md`,
`ue5-material-sampler-mismatch.md` (extended with the two WorldGridMaterial fallback causes),
decisions `2026-06-03-visionos-passthrough-translucency.md`, project entry `projects/pinchwork/pinchwork.md`.

## Round 7 (2026-06-03 PM) — WorldGrid SOLVED (static-lighting usage flag) + VERIFIED on-device
**Symptom:** on-device, lots of geometry incl. the center statue rendered as WorldGridMaterial; editor audit
showed 0 WorldGrid. **Root cause (NOT the stale-DDC guess in the Round-6 handoff):** Round-5/6 enabled static
lighting (`r.AllowStaticLighting=False→True`, DefaultEngine.ini:56). Materials created headlessly never had
`bUsedWithStaticLighting=True` saved → cook omitted the static-lit (lightmap) shader permutation → Default
Material (WorldGrid) on device. The editor masks it (sets the flag on the fly + recompiles). Device log was
decisive: `LogMaterial: Material … missing bUsedWithStaticLighting=True! Default Material will be used in game`.
Shader libraries loaded cleanly → ruled out DDC/library mismatch.
**Fix:** `Tools/headless/fix_static_lighting_usage.py` set `used_with_static_lighting=True` on every /Game
BASE material in both maps (instances inherit). Changed 3: `M_GlassLookOpaque`, `M_TriPlanar` (→ all MI_WA_*),
`M_FPPistol`. StarterContent mats already had it (why they never gridded). Folded in the M_Wood_Walnut
skeletal flag (already True). Did NOT re-save the levels (protect bakes). Incremental recook **2m10s**,
BUILD SUCCESSFUL, installed to AVP 2642855C.
**VERIFIED on-device (user, headset):** statue = dark glassy crystal ✅, floors/walls textured ✅.
KB: "Cause E" added to `intelligence/techniques/ue5-material-sampler-mismatch.md`; script mirrored to
`projects/pinchwork/scripts/`.

### Still WorldGrid: the sci-fi cube (Cause D, not E)
Its material base resolves to `/InterchangeAssets/gltf/M_Default` (un-resaveable plugin material) — the
usage-flag fix can't reach it. Fix = reskin the cube mesh in `VRTemplateMap` to a mobile-safe metal (e.g.
`M_Metal_Brushed_Nickel`), needs a VRTemplateMap re-save (mind the bake) + recook. **Deferred to the engine
session to batch** with the statue/translucency recook.

### Statue is OPAQUE by design — translucency is NOT in this build
`M_GlassLookOpaque` = `BLEND_Opaque` fake-glass (Fresnel + low roughness + reflections). Real see-through glass
needs the PR #4 Renderer depth-write fix, which is **only a design doc right now**: branch
`fix/visionos-passthrough-translucent-depth` = common ancestor (ec3faf4) + ONE docs-only commit `af8f196`
(`PassthroughTranslucentDepth.md`, 78 lines, ZERO code/shaders). The current engine is
`fix/visionos-lightmass-arm64` (has arm64 Lightmass + Slate-log fix + OpenLevel-safe — which the translucency
branch LACKS). **Engine-session handoff:** build FROM `fix/visionos-lightmass-arm64` (keep those 3 fixes),
IMPLEMENT the depth fix per `af8f196` + KB `visionos-ue-passthrough-translucency.md`, rebuild Renderer, then
project-side swap the statue (`LVLGEN_Statue_Center` in TravelTestMap) to a translucent material
(`/Game/M_PassthroughTest` exists) AND **re-run `fix_static_lighting_usage.py`** on it (AllowStaticLighting=True
⇒ even the translucent material needs the flag or it WorldGrids), + recook.

### Signing / launch (today's other rabbit-hole — RESOLVED, not a signing bug)
App is signed `Apple Development: Alex Coulombe` under team **Agile Lens LLC** (C624J4S2F8, PAID; AVP UDID
`00008112-000608AA34E8A01E` IS in the 16-device dev profile; valid to Apr 2027; get-task-allow). **The cert
NAMES are inverted:** the `alex@agilelens.com` cert is actually the PERSONAL "Alex Coulombe" team
(824P5BRS5J) — do NOT "switch to agilelens", that moves you onto the individual account. The "untrusted
developer / needs internet" Home-screen gate is visionOS's online dev-cert verification (fails if the headset
can't reach Apple). **Bypass it by launching via the dev tunnel:**
`xcrun devicectl device process launch --device 2642855C-6B73-5D5B-9387-6B110E7A7CF3 --terminate-existing com.alex.MyProject`.

### Sci-fi cube fix — DEFERRED to the engine session (engine editor is BuildId-inconsistent, ~16:35)
User asked to fix the still-gridding sci-fi cube: chrome + mobility=Movable + a spin. Script written & ready:
`Tools/headless/fix_scifi_cube.py` (matches the cube by mesh path "sci-fi_cube"; sets M_Metal_Chrome on all
slots; mobility=Movable — **which kills the WorldGrid outright**: Movable meshes need no static-lighting
permutation, so the un-resaveable glTF base `/InterchangeAssets/gltf/M_Default` no longer matters; adds a
RotatingMovementComponent yaw 30°/s; saves VRTemplateMap; reload-verifies). Mirrored to KB
`projects/pinchwork/scripts/`.
**BLOCKED — couldn't run it. The shared engine editor is BuildId-INCONSISTENT:** this afternoon's ACCVR24
`-Werror` rebuild relinked `UnrealEditor-Cmd` + **853 dylibs at 14:57** but left
`Engine/Plugins/PCG/Binaries/Mac/UnrealEditor-PCGCompute.dylib` at **02:54** → the new editor rejects PCG
("Plugin 'PCG' failed to load because module 'PCGCompute' could not be found") → ANY `-run=pythonscript`
commandlet dies at startup. `-DisablePlugins=PCG` does NOT override it. **Cooks (`-run=Cook`) still work**
(ACCVR24 cooked fine; the 12:25 static-lighting commandlet worked because the editor was still consistent
then). This is the daily-log's "piecemeal-relink → cross-invocation BuildId → editor unusable" trap.
**Resolution (user chose): fold into the engine-rebuild session.** ⚠️ That session MUST do a CONSISTENT
editor build (clean recompile so PCG relinks too — NOT a piecemeal relink) BEFORE running any pythonscript
commandlet (the cube fix AND the post-translucency re-run of fix_static_lighting_usage.py both need it),
then run `fix_scifi_cube.py` + recook, batched with the translucency work.
