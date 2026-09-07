# Authoritative Lumenwork build guard

All native UBT invocations remain quarantined: the old action preview deleted
engine products before exporting its graph. The guard's only admitted mode is
`--identity-only`, which checks the registered checkout, authoritative ref,
ancestry, tracked cleanliness, exact engine root and exact engine commit without
invoking UBT. No recovery token bypasses this quarantine.

## M5 cook, stage and sign

`package_lumen_lite_m5.sh` requires the engine release receipt, a current
`OWNER_SESSION`, and an existing source-matched VisionOS target receipt before
acquiring the host build lease. Missing native metadata must be recovered with
provenance; another executable's receipt is not a substitute.

The package route uses `-skipbuild`, disables AutomationTool compilation, passes
`-NoEngineChanges`, and routes temporary files, local DDC, UAT logs and Xcode
DerivedData into the unique run directory. Set `ENTITLEMENTS` to preserved
Agile Lens signing entitlements and run signing in a context that can access the
development identity. The preserved provisioning profile is checked for the
Agile Lens team, intended bundle, M5 UDID, certificate fingerprint and expiration.
Xcode receives the intended bundle ID explicitly; the verified profile is embedded
before signing. CMS decoding does not alter the keychain search list.

Xcode receives `UE_OVERRIDE_STAGE_DIR=<run>/stage/VisionOS`. The final candidate's
complete .utoc/.ucas/.pak inventory must match this stage by path, size and SHA-256
before signing. The script also verifies the map/pawn in IoStore and requires
fresh shader statistics with positive 1/2/4 Lumen Lite shader counts. It preserves
a unique 0.0.13 candidate and does not install it.

`prepare_lumen_lite_m5_arm.sh` derives A/B/C/D/E candidates. Installation requires
a current device-activity check. Every arm retains HMDRenderTarget=100; B selects
FinalGatherMethod=0. Nanite is outside this delivery.

## Things to Try

1. Run `python3 Build/Delivery/guarded_ubt.py --identity-only` on the M5 host.
2. Run `python3 -m unittest discover -s Build/Delivery -p 'test_*.py'`.
3. Inspect the target receipt and exact binary provenance before packaging.
4. Check `staged-payload.json` and the package signature before device install.
