# Authoritative Lumenwork build guard

> **P0 quarantine (2026-09-07):** this launcher currently refuses every UBT
> invocation. Unreal's `-WriteOutdatedActions` path calls
> `DeleteOutdatedProducedItems` before exporting the graph; an attempted preview
> deleted 2,356 build products. Do not bypass this guard. A replacement preview
> mechanism must prove non-mutation before build admission can be restored.

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
