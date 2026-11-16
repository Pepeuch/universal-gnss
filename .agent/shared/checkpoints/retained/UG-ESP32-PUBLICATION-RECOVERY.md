# Web Flasher listing and actual approved publication recovery

Lifecycle: RETAINED
Status: IMPLEMENTED (recorded recovery, not unrestricted future publication authority)
Scope: DEPLOYMENT / VALIDATION
Canonical tracking: [TODO](../../../../TODO.md), recorded Web Flasher P0 publication.
Fix commit: `464b7939653838b06380ef70bbe0962b2712e69a`, merged by PR48 into
`d3c9e0688bc6756bc0f493397d03c7bd3c2f1b40` (ancestor of current e5cb849).

## Defects and bounded resolutions

Optional publications metadata 503 hid a valid catalogue. Catalogue-first listing
now tolerates metadata 503/404/malformed JSON for every release channel, while
actual flash still verifies bytes/provenance/partitions/hardware.
The `C/C++ Format` workflow-run name needed escaped plus signs in its glob; the
invalid trigger blocked automatic publication startup. Actionlint had not caught it.
Later tag creation returned 403 on an old workflow-changing source. An explicitly
authorized normal tag push recovered that one source; no protection bypass or
main rewrite. General future tag authority remains [OPEN](../active/UG-ESP32-PUBLICATION-AUTHORITY.md).

## Recorded actual publication — not rerun during maintenance

Source `52aea50a098a1839b6028a247229d273a92ee302`, tree
`cafb82ba7c5f5b3982fab436387b1df6a5573147`; publisher `d3c9e06…`.
[Immutable Release](https://github.com/Pepeuch/universal-gnss/releases/tag/esp32-alpha-52aea50a098a).
Run43870662689 first failed publication; recovery publish job133318544238 succeeded,
Pages deployment/HTTP verification passed. Four catalogue images retained old+new
S3/C3; eight new segments downloaded and SHA-checked, both manifests verified.
New S3 app SHA256:
`a87ff178abf66e00fe62da7830bfa7df8696e6236d7e6b904bf873ffebb09dbc`;
C3 USB app:
`d0de2b8908a12007a581d62ab55d5aeff8a819d625c3ccafab023cd781a256b9`.
These are published binaries, distinct from later locally installed 9df S3 firmware.
Local restored site `/tmp/ug-publication-recovered-site` is supplemental disposable
evidence, not the publication authority. The durable Release is the recovery source.
265 policy/12 portal JS/6 portable JS tests passed; six desktop/mobile Chromium
metadata-failure cases verified real restored S3 bytes and latest/default selection.

## Conservation and exact resume

[Publication contracts](../../../../docs/flasher/README.md) and current workflow/code
remain authoritative. Preserve prior releases, nonempty durable catalogue, hashes,
shared Web/alpha deployment locking and full-install vs unqualified NVS-update labels.
No tag/release/deploy/build/CI polling was performed in checkpoint reconciliation.
On a future reported failure, inspect exact source/publisher/run and durable assets;
do not republish automatically or assume a merge is publication approval.
