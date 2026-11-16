# Common GNSS buffer optimization — review pending

Lifecycle: ACTIVE
Status: PARTIAL (implementation/software evidence complete; integration review open)
Scope: DRIVER
Canonical tracking: [TODO](../../../../TODO.md), compact-buffer proposal paragraph.
Baseline HEAD: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`; branch `fix/esp32-single-login`.

## Contract and current state

`ReceiverSession`, `UbloxSession` and `UnicoreSession` now use one internal contiguous
byte buffer plus exact optional int64 timestamp runs. No timestamp sentinel, Feed
rebatching, parser/CRC/freshness/selection change. Auto replay remains byte by byte.
Old public BufferedByte types remain; private layouts change, so consumers rebuild.
Decision: ADAPT the storage, preserve the authoritative session semantics.
Implementation commit: `8b1b446b71bec60f24e1e2426282033cf901a9f6` (authorized Git review).
Validation tree `2275b09f614a0894f396811b7c9f92c4a608e692` records the identical
production/test inputs before commit; it is not a firmware publication.

## Evidence and tradeoff

[Report](../../../../docs/analysis/GNSS_BUFFER_MEMORY_OPTIMIZATION.md),
[222-row CSV](../../../../docs/analysis/GNSS_BUFFER_BENCHMARK.csv),
[provenance](../../../../docs/analysis/GNSS_BUFFER_BENCHMARK.json) are committed measurement records.
4096 pending bytes/common timestamp: retained storage 98304→4112 B; measured host
usable heap 98312→4128 B. Exact state/metrics/timestamp traces match on 222 cases.
Linux 70 and ESP32 host 68 sanitizer tests, 265 policy tests, Lyrical 166 tests pass.
S3 HTTP/HTTPS each build twice reproducibly; static RAM +32 B, Flash +576/+544 B.
Dense per-byte timestamps slow u-blox/Unicore ~27%; full stress-harness CPU +9.8%.
79/222 cases allocate more. No universal CPU or physical S3 SRAM gain claimed.
No-exception IDF defers optional live shrink until empty/reset; normal UART batches
are 512 B. Full caller Feed remains inspected before receiver selection.

## Exact next step / boundaries

User review of the memory/CPU tradeoff precedes integration. Commit/push for GitHub
review is authorized; merge, firmware publication and hardware writes are not. No new optimization,
flash or hardware inference. Source, tests, benchmark runner and reports comprise
16 files at this handoff, before checkpoint-maintenance changes. Existing private
checkpoints and four unrelated user files remain excluded. Local operational state:
`.agent/checkpoints/UG-COMPACT-GNSS-BUFFER.md` (ignored, not required by CI).
Physical GNSS/RTCM latency and target heap placement remain unmeasured; a separate
explicit authorization and fixed physical baseline are required for any flash.
Invalidate only if implementation, affecting dependencies, fixtures or SDK changes;
a new session is not a reason to repeat the successful suites.
