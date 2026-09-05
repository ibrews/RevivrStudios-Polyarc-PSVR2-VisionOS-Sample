# Scripted hand testing and recording

Use the private [xrsim hand runbook](https://github.com/ibrews/xrsim/blob/main/docs/runbooks/unreal-hands.md)
for setup and recovery, the [JSON interface](https://github.com/ibrews/xrsim/blob/main/docs/unreal-hand-agent.md)
for scenario details, and the [recording recipe](https://github.com/ibrews/xrsim/blob/main/docs/unreal-hand-demo.md)
for a visible demonstration. Both this repo and xrsim have GitHub wikis disabled;
these versioned docs are the maintained instructions.

`Plugins/XrsimAgent` is editor-only and registers simulated input only when
`XRSIM_AGENT_DIR` is set before launching Unreal. Select a simulated HMD runtime
for that editor process with `XR_RUNTIME_JSON`; the baseline uses Meta XR Simulator
v205 and UE 5.8.2. The adapter injects joints and matching palm/aim motion poses.
It never attaches, moves, or applies forces to the test cube.

The map's default pawn is `VRPawn_HandWorldControl_C`, whose existing Blueprint
gesture logic reads hand keypoints. This test does not require switching to the
Pinchwork C++ pawn. Discover the actual play-world target name with `hand scene`;
the baseline cube is `GrabActor_StaticMesh_Physics9_1`.

ECA Bridge provides direct editor and play-world inspection alongside XrsimAgent.
Enable it and follow its [installation guide](https://github.com/ibrews/ECABridge/blob/main/docs/wiki/Installation.md).
On Windows, inspect `Plugins/ECABridge`: a Git checkout of the Mac symlink can be
a text file, so a real local plugin directory is required. The hand runbook covers
the verified additional-directory setup and the ECA unity-build fix (`9316bba`).
Use ECA to inspect live component transforms and visibility; keep cube interaction
in the existing Blueprint gesture and physics path.

From an xrsim source checkout, after installing dependencies and building:

```powershell
node bin/xrsim.js hand contract
node bin/xrsim.js hand preview --bridge C:\tests\lumenwork-hands
node tools/test-unreal-hands.mjs --bridge C:\tests\lumenwork-hands `
  --target GrabActor_StaticMesh_Physics9_1 --out C:\tests\hand-results
node tools/demo-unreal-hands.mjs --bridge C:\tests\lumenwork-hands `
  --target GrabActor_StaticMesh_Physics9_1 --out C:\tests\hand-demo --reset
```

The acceptance runner requires a positive pinch/grab/throw, an open-finger
negative, and a genuine grab/lift/stationary-release negative. Gravity alone
cannot pass its directional throw assertions. Results include actual attachment,
held movement, forward release speed and flight, a run UUID, the input timeline,
and per-frame physics observations. This validates gameplay with synthetic input;
it does not verify native OpenXR/CloudXR hand transport or target-headset performance.

## Original shared hand assets

The default pair in `Content/XRMannequins/Meshes` is restored from the original
UE 5.8 template. It matches the pair in the local training project at
`F:\ACP_TRAINING\260819_IntermediateVR\204.10_UET_58\uProject\Content\XRMannequins\Meshes`.
Pinchwork's default asset references now point here. Keep Vision Pro rotation
compensation in the engine fork instead of baking it into a second mesh variant.

`hand view` with `meshes:"bones"` creates temporary collision-free cylinders and
joint markers directly on the sampled joints, without mesh skinning. It creates no
grab components. The optional fixed spectator camera and fill light exist only
in Preview. To use the project's existing VRSpectator camera, run the demo helper
with `--existing-camera` and position that camera before the action starts.

Do not edit or save Blueprint assets merely to produce a successful test.
Do not reset the cube with actor setters between takes: end and restart Preview.
Keep external recording separate from timing/performance acceptance, and save
the matching scenario/result JSON alongside the video.
