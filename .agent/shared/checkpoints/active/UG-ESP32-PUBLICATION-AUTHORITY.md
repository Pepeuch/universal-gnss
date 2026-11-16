# ESP32 Release publication authority

Lifecycle: ACTIVE
Status: PARTIAL (software implemented; hosted RC path pending main deployment)
Scope: DEPLOYMENT
Canonical tracking: [TODO](../../../../TODO.md), publication-authority follow-up.
Contract/evidence: [Release publication](../../../../docs/releases/esp32-release-publication.md).
Baseline: dev `e496116672a06e5687e79e866b5861259f1efc2e`, main
`e632686c9c2e8dd5d4dc5b8d15f641dc1a633824` (identical trees).

## Current decision and preserved boundaries

The maintainer explicitly deferred tag rulesets during this task. Publishing a
canonical RC Release from an admin/maintain account authorizes its firmware;
no separate approval-register entry is needed per RC. Seven exact-SHA push/main
validations, main ancestry, frozen tag object/commit/tree/Release ID, bundle
qualification and immutable additive assets remain required. Legacy alpha
retains explicit approval; existing revocations are not overridden.

ADAPT the existing esp32-alpha workflow, not a second publication infrastructure.
The build commands and docker-release.yml remain unchanged. Native ESP32 now
compiles on every branch/tag push and every PR, without source approval or main
ancestry. Release publication is a separate authorized path. Release assets are
namespaced esp32-*, checked before writes and read back; a final completion marker
controls catalogue eligibility. Interrupted uploads resume the original verified
site archive without rebuild or overwrite. Web-only updates retain firmware history.

The historical missing-list/glob/source recovery remains established by the
[retained recovery evidence](../retained/UG-ESP32-PUBLICATION-RECOVERY.md).
No general automatic creation of historical tags is claimed or needed for RCs.

## Remote observation / qualification remaining

RC1 exists on e632, ID447677079, initially no assets. Seven source CI workflows
and Docker release 43966826035 succeeded. Rulesets API returned []; none added.
Pages deployment policy was relaxed once to permit tag pattern v*-RC* while
preserving main. No release/tag/firmware/board mutation or dispatch occurred.

Local deterministic tests cover trigger, trusted maintainer, strict identities,
seven CI results, failed/cancelled/skipped build recovery, immutable assets,
partial-upload resume and synthetic S3/C3 packaging through catalogue verification.
293/293 generator/CI tests, 44 maintenance tests, 94 local links, syntax,
checkpoint/dashboard and diff checks passed. This is not hosted publication evidence.

## Exact next step

Review and merge through dev, promote to main, wait for seven push/main successes,
then manually catch up the existing RC1 using its unchanged SHA e632686c9c2e8dd5d4dc5b8d15f641dc1a633824
and tag v0.7.5-RC1. Verify the 22 durable assets, completion marker and public
catalogue/firmware bytes. Do not recreate the Release, retag, flash, or claim this
qualification before that run. Future published RCs should then trigger directly.

Invalidation: publisher/source/tag/Release identity, build qualification, CI,
maintainer permission, catalogue integrity or Pages policy changes. Tags remain
unprotected until a separately requested hardening task.
