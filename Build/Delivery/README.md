# Authoritative Lumenwork build guard

> **P0 quarantine (2026-09-07):** this launcher currently refuses every UBT
> invocation. Unreal's `-WriteOutdatedActions` path calls
> `DeleteOutdatedProducedItems` before exporting the graph; an attempted preview
> deleted 2,356 build products. Do not bypass this guard. A replacement preview
> mechanism must prove non-mutation before build admission can be restored.

`--identity-only` validates the exact project root, remote, authoritative ref,
ancestry, clean tracked state, engine path, engine commit, and clean engine state
without invoking UBT. Use it before a `-skipbuild` cook/package. It accepts the
canonical HTTPS and SSH forms of the `AgileLens/Lumenwork` remote.

`guarded_ubt.py` is the required fail-closed entry point for ordinary M5 project
builds from the authoritative delivery checkout. It validates the exact checkout,
remote, ref, ancestry, source and engine commits, and tracked cleanliness before
asking UBT to write (not execute) the exact combined target action graph.

Ordinary builds are rejected when the graph exceeds 200 actions or would write
under the engine's `Engine/Binaries`, `Engine/Intermediate`, or `Engine/Saved`
trees. Builds that pass are launched with `-NoEngineChanges`.

Use `--execute-target-set editor` when the admitted operation is only the project
editor module. The safety preview still evaluates all four combined targets; only
the post-admission execution is narrowed.

Large or engine-mutating recovery graphs require a
`--manager-recovery-receipt PATH` and a one-use token supplied through
`LUMENWORK_MANAGER_RECOVERY_TOKEN` (preferred) or
`--manager-recovery-token TOKEN`. The JSON receipt must contain:

```json
{
  "schema_version": 1,
  "authorization": "engine-recovery",
  "token_sha256": "SHA256_OF_ONE_USE_TOKEN",
  "project_head": "CURRENT_REQUIRED_DELIVERY_REF_HEAD",
  "engine_head": "eb47362c29cc1cde71afeb6bd885c86ff7e8127a",
  "allow_engine_outputs": true,
  "max_actions": 3438
}
```

Prefer setting `LUMENWORK_MANAGER_RECOVERY_TOKEN` instead of putting the token on
the command line. Identity checks are never bypassed by a recovery receipt.

Run tests with `python3 -m unittest Build/Delivery/test_guarded_ubt.py`.

## M5 Lumen Lite delivery

`package_lumen_lite_m5.sh` is the post-recovery 0.0.13 cook/package/sign path.
It refuses to start until the exact `engine-recovery-eb473.done` receipt names
the accepted engine hash, reruns the identity guard, executes the protected
pipeline inside `ue-build-lane.sh run`, and arms the descriptor-driven overnight
watchdog. It preserves a unique run directory and requires fresh
`ShaderTypeStats-METAL_SM5_IOS.csv` evidence with global-shader counts of at
least 1/2/4 for the three Lumen Lite shader types. It does not install.

`prepare_lumen_lite_m5_arm.sh` derives signed A/B/C/D/E candidates from that
preserved baseline. Device installation remains off by default and requires an
explicit current wearer/capture activity check. Every arm pins
`xr.SecondaryScreenPercentage.HMDRenderTarget=100`; arm B is the primary demo
candidate with `r.Lumen.FinalGatherMethod=0`. Nanite is outside this delivery.

Run all delivery unit tests with:

```bash
python3 -m unittest Build/Delivery/test_guarded_ubt.py \
  Build/Delivery/test_verify_lumen_lite_cook.py
```
