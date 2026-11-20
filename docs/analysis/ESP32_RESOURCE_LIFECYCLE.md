# ESP32 resource lifecycle — work in progress

Status: **PARTIAL / HARDWARE_REQUIRED**. Branch `perf/esp32-resource-lifecycle`,
based on `c48f06f2de38991b4c42eb807c9286001123bd93`. The maintainer explicitly
requested this base while monitoring P0 CI, superseding the original main-after-P0
prerequisite. The maintainer authorized checkpoint commits and a branch push for
this pause; this is not final qualification or publication. The candidate application has been installed
after private backup and verification; see the target evidence below.

## Pause authority — 20 November 2026

This summary supersedes operational statements in the chronological evidence below.
The exercise remains **PARTIAL / HARDWARE_REQUIRED**, paused at the maintainer's
request. No new trial, firmware publication or architectural study is started at
closeout. Historical completed tests are retained on their exact source snapshots.

- Final software source tree: `e31e2d96daf7e3c152826fbd5cb4807087a1c5b7`.
  Full pinned SDK validation passes: **112 native ASan/UBSan tests**, Python/JS
  suites, ten SDK/QEMU starts including cold-task recovery, and independent
  four-segment reproducibility. The normal application is 1592720 bytes, SHA-256
  `05ac13f69abd9200748f92fc305e226f8f34afdad71e6d5d524590af0c42cdbc`.
  Its normal and local 64 KiB fault images are archived, **not installed**.
- Actual installed normal S3 comparator: source tree
  `70af63288a2c215f30cf269b2a9590cf57d27e9b`, UART-in-IRAM, CPU A,
  PSRAM radio payload, Wi-Fi B. Application SHA-256
  `f0fa1fe25cbafeb6e6984a3d3fd48c543b4327d94fa2159863502d384c5180f6`.
  The final source promotes these settings to defaults and adds verification;
  do not claim that the final software image itself was flashed.
- All synthetic sources and qualification benches are stopped; both USB ports
  are free. Original logical network settings, radio association/channel,
  TLS identity, administrator and GNSS configuration are preserved. The radio
  was originally enabled and is restored enabled; seven network endpoints are
  disabled as originally configured. NVS journal bytes need not be identical.
- Latest nominal matched window: **902/902** fresh CRC-valid C3 USB frames,
  **60/60** HTTPS requests with 1/2/3 clients, no UART/MAC/queue errors.
  Minimum sampled internal free **126667 B**, largest **34816 B**; existing
  96/32 KiB admission thresholds are met. The indicative 150 KiB loaded target
  is **not achieved**. CPU0/1 busy 69.355%/54.824%, end-to-end p99 485.004 ms,
  maximum 513.527 ms. This is synthetic transport plus live receiver telemetry,
  not RTK field qualification.
- Remaining acceptance: strict 100-transition series, intermittent radio failure,
  cancellation/burst scenarios, and fault recovery on the final exact source.
  No full GO, endurance or physical GNSS/RTCM/RTK continuity claim.

Other software checks already passed: 309 generator/CI tests, 44 repository/policy
Python tests, touched-file clang-format-21 and Python syntax. No new C3 Relay,
TOE, Rust, HTTPS lifecycle or authentication-latency implementation is included.
Local report copies in `ESP/analyses/` are deliberately excluded from the commits.

## Existing evidence and boundaries

The [19 November architecture audit](ESP32_ARCHITECTURE_RESOURCE_AUDIT.md) is the
current ownership map; the 15 November local memory audit is an earlier baseline.
Console/JSON/satellite/routing PSRAM placement, lazy radio storage and compact
GNSS buffers are already present. Do not count them as new savings. Shared GNSS
core, settings schema, partitions and memory guards remain outside this implementation.
The separately authorized common NTRIP caster Basic-header/socket-admission fixes
are covered by 70 Linux tests; no shared GNSS algorithm is changed. A targeted C3 Rover USB radio correction is now authorized after
backup; C3 Relay remains excluded. Baseline firmware and exact artifact hashes are in
[the P0 inventory](ESP32_S3_FRESH_INSTALL_ARTIFACTS.json).

## Initial reference measurements — 20 November 2026

Read-only, certificate-pinned requests confirm clean `952eca0` HTTPS on the S3.
Initial sample: internal free 82,435 B, largest 32,768 B, boot minimum 42,652 B;
PSRAM free 8,277,880 B. Admission is reduced and satellite diagnostics are refused.
There is no observed RTCM flow in this reference sample.

Ten `/api/resources` requests per client, nominally one second apart, use separate
persistent HTTPS connections and the same trusted certificate. Phases run in order
1/2/3 clients with two seconds between them. Durations include request/response,
initial handshake and scheduling; they are not GNSS/RTCM latency measurements.
Raw measurements remain private. Minima below are sampled, not intra-operation
heap tracing or a reset of the global boot minimum.

| Clients | Successful requests | Minimum sampled internal free B | Minimum sampled largest block B | Median request ms | Maximum request ms |
|---|---:|---:|---:|---:|---:|
| 1 | 10/10 | 82,539 | 32,768 | 58.45 | 907.99 |
| 2 | 20/20 | 81,923 | 32,768 | 74.10 | 1,738.23 |
| 3 | 30/30 | 81,435 | 32,768 | 76.67 | 2,622.66 |

These are baseline samples, not an A/B gain or maximum-load qualification.

## First comparator under development

The S3 lifecycle manager creates independent workers for requested, available
endpoints. Stack reservations are initially unchanged. Idle configured capacity
does not imply an executing worker. A single manager prevents duplicate starts;
allocation/task errors are explicit and retried at a bounded cadence. Workers
close their sockets and invalidate sources before deliberate exit.

The router acknowledges destination withdrawal only after replacing its cached
callback, outside the lifetime of previous callback invocations. A timeout never
allows freeing/resetting a context. Reconfiguration also withdraws before queue
reset. A publication ticket alone does not prove retirement.

Logical connection owners are retained in explicitly allocated PSRAM after first
activation, preserving per-publisher filter cadence and cumulative metrics across
worker recreation. Queue controls remain internal, and queue payloads retain
existing bounded capacities. No automatic placement of DMA, ISR data or stacks in
PSRAM is introduced. External placement of the object does not prove all of its children are external.
Existing constructors may allocate strings/Pimpl children. S3 exceptions plus a
2,048-byte emergency reserve now permit clean recovery instead of libstdc++ abort.
Constructor failure releases the external placement block; execution failure clears
unwound trace pointers, closes/withdraws, and backs off. C3 configuration is unchanged.

## Evidence and remaining acceptance

Host ASan/UBSan: **81/81** native tests pass, including actual service compilation,
idle allocation checks, owner/queue/task failures, duplicate creation prevention,
100 simulated activation/stop cycles, seven-endpoint selection, network loss,
retirement retry, router acknowledgement and planned/unexpected worker exits.
This does not prove FreeRTOS scheduling, idle stack reclamation, DNS/TLS cancellation
latency, RF delivery, real network throughput or target heap fragmentation.

Latest tree `374f0535f36c2e43ef6df53e0ee317701f6804b6` passes full pinned-SDK
`ci.py`: 81 native ASan/UBSan cases; all five Python/JS suites; real SDK TLS/NVS
emulation; compilation/verification; independent four-segment reproduction. The
QEMU boots also exhaust the 8-bit heaps, catch a C++ allocation failure using the
emergency reserve, and return all held blocks before TLS tests. The native build
contract suite now has 14 cases. Exact BIN/ELF/MAP/sdkconfig are archived for the installed candidate.

| Build | App bytes | DRAM data B | DRAM BSS B |
|---|---:|---:|---:|
| Reference clean952 | 1,505,408 | 23,789 | 42,384 |
| On-demand, before nested-allocation recovery | 1,506,768 | 23,789 | 42,608 |
| Recovery-capable candidate 374f053 | 1,584,896 | 23,789 | 42,656 |

Exception safety costs 79,488 image bytes against the reference, plus dynamic
exception-runtime/reserve costs still to measure physically. Static BSS increases
272 bytes. None of these are measured savings. Both C3 Rover USB and UART builds
and independent reproductions passed tree944eafe6; the subsequent changes are
S3-only recovery configuration/code and tests, with C3 sources preserved.

## Physical comparator results — not final acceptance

The updated paired configuration was included in a new snapshot. Whole-device ROM
verification passed before writing only the application at `0x20000`; NVS, TLS,
bootloader, table and OTA prefix readback were byte-identical before first boot.
The original certificate and configured administrator were retained. The installed
application SHA-256 is `4f94d005822b74dfb0c3a9e98305a4269cc869e6c0253608b47da28b5228e4db`.

With GNSS, ESP-NOW and HTTPS active but all NTRIP endpoints idle:

| Clients | Successful requests | Minimum sampled internal free B | Minimum sampled largest block B | Median request ms | Maximum request ms |
|---|---:|---:|---:|---:|---:|
| 1 | 10/10 | 139,187 | 34,816 | 61.64 | 888.25 |
| 2 | 20/20 | 138,719 | 34,816 | 70.98 | 1,661.28 |
| 3 | 30/30 | 138,055 | 34,816 | 65.59 | 2,475.99 |

This proves an idle-endpoint benefit only. Enabling one incoming connection and
two independent outgoing publishers with controlled synthetic RTCM produced 398
source frames and 167/159 publisher frames, with the requested distinct message
filters and no fixture CRC errors. However, internal free memory fell to **76,463 B**
and the largest block to **31,744 B**. Admission was reduced and satellite reporting
was refused. **FAIL for the nominal active-load memory objective.** No guards or
stack reservations were lowered to hide this result.

A bounded heap trace around input-only reactivation measured 129,515 →119,003 B
internal free. A separate publisher-only activation measured 119,003 →110,415 B.
Both traces overflowed their 1,024 records; they are call-site clues, not complete
allocation/peak accounting. Symbolization of the exact candidate ELF confirms the
input task allocation and retained `std::vector<std::optional<int64_t>>` allocations;
shared protocol sources remain unchanged. Full attribution under traffic remains
open. The first comparator therefore is not the final architecture.

Two C3 USB captures received **zero frames**, including a capture after read-only
UGCFG identification and explicit `RUN CONFIRM_RTCM`. Both pairing records were
independently decoded and CRC-checked in private: LMK, channel and crossed STA MACs
match. No key/configuration or C3 firmware was rewritten. MAC acknowledgements on
the S3 do not prove decryption or USB delivery. This part remains **FAIL / unresolved**.

The operator confirmed fresh F9P HPG 1.32 observations and zero UART faults. Supplied
position coordinates are deliberately excluded from this record. General GNSS JSON
is not evidence of detailed satellite rows/C/N0, which remain unverified.

Remaining acceptance: measured cooperative alternative, active-load heap attribution,
all seven endpoints/five simultaneous publishers, slow clients, cancellation budgets,
100 physical transitions, C3 RTCM delivery, CPU/latency and nominal satellite access.
Temporary controlled test endpoint configuration must be restored to the newest
paired logical snapshot at the end. Raw traces/backups remain private.

## First-comparator restoration (historical sample)

The original logical input, upstream, five publishers and caster settings were
restored from the newest verified paired snapshot. GNSS, radio association,
authentication and TLS were not changed by this restoration. The controlled source
and USB helpers are stopped; both serial ports are free. Pinned HTTPS returns 200
with the original certificate; GNSS/radio/HTTPS report running without error.
Final idle sample: internal127,491 B, largest34,816 B, global minimum74,384 B,
PSRAM8,268,704 B, normal admission. This is logical restoration, not a claim that
NVS journal bytes are unchanged. Candidate firmware remains installed.


## Cooperative comparator and active-load failure

The second comparator uses one 12 KiB I/O task and a temporary 10 KiB blocking
helper. Exclusive atomic handoff prevents access to a helper-owned endpoint.
DNS/connect/request/GGA cannot block sibling progression. Publisher writes retain
the original receipt time and commit filter cadence after the complete write.
Router acknowledgement remains mandatory before retirement.

Tree `241741d1c028a5e44c16bac50bf62a8d982f3871` passes the complete pinned SDK
chain, 84 native sanitizer tests, SDK TLS/NVS emulation and independent reproduction.
It includes a reconnect deadline race fix. Earlier tree
`666208286e9a192da26351966aa431bf7bfc9da6` is installed on the S3 with exact
BIN/ELF/MAP retained; newer source is not installed yet.

Input plus two publishers at 5 frames/s retains **119,359 B internal free /
34,816 B largest**, correct independent filters and no observed allocation failures.
A seven-endpoint large-frame attempt **fails acceptance**: two nominal HTTPS clients
reach 65,555 B free; three complete 29/30 requests. Global minimum is 41,472 B and
largest 31,744 B. An operator browser was also active, so the total client count
was not controlled. The fixture aborts on a one-second source drain timeout;
requested rate is not proven achieved rate. Caster slow-client readiness also
failed. These results are not PASS.

C3 runtime inspection confirms configured key, crossed MACs, channel, started
state and worker. Its receive callback is not entered during a controlled pulse,
despite S3 MAC ACKs. A domain-separated PMK derived from the existing LMK is under
software validation on both endpoints: a hypothesis, not a proven physical fix.
There is no new persistent key; a coordinated endpoint update would be required.

## Capacity-first placement under validation

The target is service coexistence and bounded interference, accepting a small
PSRAM latency cost when overall capacity and stability improve. Internal SRAM is
reserved for necessary controls, DMA, ISR and incompatible stacks. Existing
96/32 KiB normal and 48/16 KiB critical guards stay unchanged. A 150 KiB nominal
free target is aspirational, not measured acceptance.

A new S3-only C++ allocation domain explicitly sends task-owned network children
to PSRAM without fallback. Outside that scope, allocation retains SDK malloc and
alignment policy. A PSRAM provenance registry supports deletion by another task
and erases exactly the requested extent. Its internal critical-section control
admits at most 4,096 live blocks and eight links per hash bucket; earlier bucket
exhaustion is explicitly counted and refused. Allocation, wiping and freeing are
outside the lock. Scoped data never belongs to ISR/cache-off paths. Lock duration
still needs target measurement. Host sanitizer validation is **88/88**; new SDK
and QEMU validation is pending.

An external 12 KiB I/O stack is a separate default-off comparator. The blocking
helper stays internal for credential/NVS/TLS setup. This does not qualify arbitrary
Flash/NVS/OTA calls from an external stack. SDK TCB remains internal; matching
caps-aware deletion is required if task retirement is later introduced.

Astra reviewed three affinity comparisons with the same workload and heap policy:
A current application affinity; B GNSS/UART owner plus router on CPU1; C B plus
application network/radio/Web workers on CPU0. Keep SDK affinities fixed. UART ISR
placement follows its installing task; synchronous router callbacks must remain
bounded. Sustained load below 70% per core, no nominal processing drops and service
saturation isolation remain experimental criteria. No affinity is qualified yet.
Future SD/SMB/MQTT/RINEX services remain planning, not implementations in this task.

Current cleanup: original logical input/upstream/five publishers/caster restored;
no synthetic source is active. The S3 persistent maintenance console owns its USB
port; C3 USB is free. Original certificate, administrator, GNSS and pairing are
retained. The failed cooperative load left a 31,744 B largest block, so the earlier
restored idle sample must not be presented as the current cooperative result.

## Latest qualification — private arena and shared caster correction

The scoped hash-registry comparator is **rejected** as a final architecture. Its
4,096-block cap (and earlier per-bucket refusal) can be exhausted by legitimate
60-second timestamp histories while substantial PSRAM remains free. At 20 valid
MSM frames/s, four timestamp indexes alone can retain approximately 4,800 nodes.
A real S3 abort was captured and symbolized against the exact installed ELF:
allocation → timestamp multiset → RTCM monitor → NTRIP read → cooperative owner.
The reason the throw reaches `std::terminate` instead of the owner catch is still
open. Changing allocators does not by itself resolve this recovery defect.

The replacement uses a **1 MiB explicitly reserved PSRAM arena**, SDK TLSF and a
separate internal recursive heap lock. Address-range ownership replaces hash
lookup; per-block headers retain requested extent and owner. Ordinary allocation
still uses the SDK heap. The private arena never borrows SRAM. Its free/minimum/
largest values are reported separately: the global heap counts the full reservation,
not the live payload usage. Full arena walks are diagnostic-only and require
latency measurement; they are not real-time-path operations. The 1 MiB budget is
an initial comparator, not a proven optimal reservation.

Tree `d3ad9b34c594629dd87b3172fb76cbd80e573388` passes the full pinned SDK chain,
88 native sanitizer cases and independent reproduction. QEMU checks 6,000
simultaneous nodes, real SDK arena alignment/poisoning, inter-core release,
suspended-scheduler deletion, arena-only exhaustion and a caught exception in the
production RTCM monitor, plus private-heap integrity and recovery. This still does
not prove the complete physical `NtripClient::Read` → owner-catch recovery path.
A separately labelled **64 KiB fault-injection image**, never publishable, is built
from that same source for the required physical refusal experiment. Normal memory
guards remain unchanged. Neither arena image is hardware-qualified yet.

The maintainer explicitly authorized two targeted shared-caster fixes. The Basic
authorization builder already includes `Basic `, so the extra prefix caused all
standard authenticated clients to be closed before `ICY`. A stack admission guard
also closes a newly accepted descriptor if vector growth throws. Linux regression
**70/70**, including standard authentication, malformed/wrong credentials and forced
admission-allocation failure, passes. The native policy test also passes after
correcting its mirrored doubled-prefix fixture. Shared GNSS/protocol engines and
ROSIDL remain unchanged; no downstream-specific implementation is introduced.

After coordinated PMK updates preserving both association prefixes, the C3 received
**367 consecutive synthetic RTCM frames**, sequences 0–366, valid CRC24Q, no gaps,
duplicates or reordering. Host-monotonic end-to-end latency: p50 64.10 ms, p95
108.46 ms, p99 111.67 ms, max114.85 ms at 5 frames/s / 256-byte payload. This
proves that scenario only. The subsequent loaded attempt received only eight USB
frames before the S3 reset; it is not a successful load qualification. HTTPS
requests after that reset cannot be counted as successful coexistence measurements.

Original logical network settings were restored after both failed load attempts;
all private keys, credentials, backups and captures remain outside Git. Rust is
explicitly deferred by the maintainer: a future monitor experiment would require
its own compatibility decision, preserving unordered observations and exact
retention semantics. No language migration or common-monitor rewrite is included.


## Xtensa cold-task exception recovery — 20 November 2026

The physical probe `7d5fb1cca3c2ff6f57cb35836da0baedfc8c1348` reports
`_URC_END_OF_STACK` before termination. Independent ELF review identifies the
mechanism: the SDK FreeRTOS Xtensa port sets the task entry's return address to
zero; this toolchain's frame-state search returns end-of-stack before calling
that final frame's C++ personality. A valid FDE/LSDA in the task entry does not
make its handler reachable. `_Unwind_Backtrace` calls its callback before checking
that same end condition, explaining why the diagnostic trace includes the final
`NetworkLifecycle` frame. This is not proof of a missing table or insufficient
exception-pool capacity.

Two isolated cold QEMU boots reproduce the difference on a 12,288-byte internal
stack: the first refused vector-copy allocation reaches the terminate marker when
the handler is in the task entry; a non-inlined called handler catches it and
allows reallocation and heap-integrity checks. The native cooperative owner,
blocking helper and independent-worker comparator now keep their handlers in
such callees. Work after each call prevents a tail call. A deterministic test-only
allocation failure checks the cold called-handler path before the existing warmed
main-task tests; shipping builds do not expose that injection.

The first physical corrected probe, source tree
`ffd4df833c67f12522aaf24bb67e658c524615d8`, still has a deliberately reduced
64 KiB arena. It catches three actual receive-path allocation failures without
termination/reboot. The controlled source emits 853 frames across four connections
with zero scheduling misses; this supports automatic reconnect in this scenario,
not loss-free delivery across an exhausted domain. Network settings are restored
and the source stopped afterward. The post-test owner retains 363 allocations /
18,215 requested bytes, confirming that ordinary Stop retains monitor history.
To guarantee recovery headroom after a terminal OOM, the native input path now
invalidates/closes the session, destroys that exclusive input owner, and recreates
it on retry. Normal reconnect retention is unchanged. This last cleanup delta
passes focused host tests; its new SDK and physical qualification are pending.

No reduced-arena probe is a publishable or operator-qualified firmware. Restore
the normal image after fault qualification. All private backups and raw captures
remain outside the repository.


The corrected source `ea269bf7d844f9fa13f424b353334ce659b66bde` passes the full
pinned SDK chain, 88 native sanitizer tests, Python/JavaScript suites, ten SDK/QEMU
starts including the first cold-task throw, and independent four-segment
reproduction. An initial cold test was optimized away and failed; address escape
plus exact refusal-count assertions correct the test rather than suppress it.

On the physical S3, its 64 KiB fault image catches the first allocation refusal
after 13.11 s of controlled input. The failed input owner drops to **zero live
allocations / zero requested bytes**; after retry only five allocations / 302 bytes
remain before input resumes. Restarting the source produces **41 new frames**;
owner state grows with those observations and uptime increases continuously.
Original logical network configuration is restored afterward. This qualifies
that input recovery scenario, not every endpoint, full load or all TLS paths.
The normal 1 MiB image must replace the fault image before operator use.

The repository agent policy now requires called, non-inlined recovery boundaries
for the affected Xtensa task runtime and explicit requalification on SDK/toolchain
changes. No common GNSS/monitor semantics or guard thresholds were changed.


## Aligned normal-image coexistence — 20 November 2026

The normal 1 MiB image, source tree
`e815606f57777bd7ed3130a06dae142fd9999750`, replaced the fault probe with its
application and protected prefix verified before boot. An aligned passive C3 USB
capture drains and tags pre-existing bytes before opening the source window.
At 5 frames/s with a 256-byte payload it receives **101/101 fresh frames**, no
sequence gaps, duplicates, CRC failures or MAC failures. Host-monotonic latency is
p50 14.023 ms, p95 16.500 ms, p99 17.331 ms and maximum18.235 ms. This raw-select
capture differs from the earlier buffered-read method; its lower latency must not
be attributed to a firmware improvement across those incomparable procedures.

A separate 45-second window simultaneously operates the input, five publishers,
local caster with two fast and one slow client, GNSS, encrypted ESP-NOW and the
1/2/3-client HTTPS workload. At 20 frames/s / 1023-byte payload, **902/902** source
frames arrive fresh on the C3 USB output, without sequence gaps, duplicates, CRC
failures, radio queue drops or MAC failures. The S3 records902 transmitted frames
and4510 MAC acknowledgements (five fragments per frame). End-to-end latency is
p50 108.691 ms, p95 410.940 ms, p99 462.969 ms and maximum485.818 ms in this workload.
HTTPS succeeds **60/60** times; its sampled internal minimum is **116751 B** and
largest contiguous block **34816 B**. The three-client median is111.548 ms and
maximum4465.389 ms; those request measurements include initial TLS handshakes.

These exact windows pass, but do not explain earlier variable radio losses or
prove endurance, field RTK, the CPU/Wi-Fi comparators, or a PSRAM-placement gain.
Ambient interference is a possible contributor, not an established cause. The
original logical network settings are restored and the synthetic source stopped.
The radio association, channel1, TLS, administrator and GNSS settings are preserved.
NVS journal bytes need not be identical after a logically restored configuration.

The subsequent immutable software snapshot
`bc45c4c72c019b6f942ebb96aec6f417de20a4f6` passes **111 native ASan/UBSan tests**,
the Python/JavaScript suites, ten real-SDK QEMU starts including cold-task recovery,
and independent four-segment reproduction. It adds radio storage/queue diagnostics
and sensitive-owner memory erasure; this is software evidence, not the installed
hardware reference. A later bounded numeric GNSS diagnostic getter and checked
JSON insertions remain a separate compile/physical delta until verified.

## HTTPS lifecycle alternatives — read-only study

Astra reviewed the actual ESP-IDF6.1 source
[`fff9895c82d744c7237be8847347bdd1b07c6643`](https://github.com/espressif/esp-idf/tree/fff9895c82d744c7237be8847347bdd1b07c6643)
and the current implementation. No HTTPS lifecycle change is implemented or
qualified by this study. `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` is already acquired.

| Alternative | Quantifiable resource effect | Qualification and architectural limit |
| --- | --- | --- |
| Permanent server, current reference |24576 B internal task stack plus TCB; TCP listener and two control sockets remain at idle. |Measured coexistence above; no new saving. |
| One / two / three client slots |Fixed session table192/384/576 B; at full occupancy20/40/60 KiB TLS content buffers, already mainly PSRAM. |Three to one saves384 B table payload at idle. One slow client monopolizes a one-slot server. Compare two for transfer plus administration, retain three as reference. |
| Release duplicate application PEM after SDK copy |At most6150 B raw bytes for the bounded certificate/key copies. |Ordinary allocator placement and measured SRAM gain unknown; wipe private copies and preserve restart ownership. |
| Server stack in PSRAM |Potential relocation of24576 B stack, independent of Mbed TLS allocation. |Web POST writes NVS: use an internal-stack owner and qualify transitive Flash/cache/ROM/DMA accesses and borrowed-buffer lifetime. Not implemented. |
| Server activated on demand |May release the same stack and other server resources during sleep. |No public HTTP API adopts an already accepted socket. ESP-TLS alone can accept a descriptor. Transparent wake therefore needs SDK/dispatcher work; stop/rebind can lose the first connection. No measured saving. |
| Dynamic TLS buffers |May shorten buffer lifetime between records; existing sizes already asymmetric16 KiB RX /4 KiB TX. |Global configuration also affects NTRIP TLS; record-size negotiation and renegotiation/mTLS interoperability required. RTCM frame size does not bound a TLS record. OOM can move into an active transfer. |

Moving and removing the same server stack are alternatives: do not add both
24576-byte figures. Internal8-bit, internal-DMA and PSRAM8-bit heaps must be measured
separately; the two internal capability sets overlap. After connection close,
client TLS contexts/keys/record buffers are freed, while listener, server task,
controls and server PEM remain. Fragmentation/TIME_WAIT can outlive a connection.
The impact of repeated server destruction/recreation is **not yet measured**.

The SDK user HTTPS-open callback runs after the handshake, too late for pre-TLS
memory admission. A future policy needs a real consumable reservation, bounded
handshakes, progress/lifetime deadlines, cooldown and cleanup without involving
RTCM workers. Preserve existing application sessions, expiration/revocation,
PBKDF2, certificates, CSP and ESP-NOW encryption. No HTTP fallback is proposed.
[HTTPS transport implementation](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_https_server/src/https_server.c).

The JavaScript is already grouped at build time. Flash holds three gzip assets:
HTML5034 B, JavaScript15269 B and CSS1276 B, total21579 B; three GETs can share one
connection. Keeping external scripts/styles preserves the current CSP. If useful,
compare immutable, content-addressed public assets while HTML/API/auth remain
`no-store`; no permanent SRAM gain is established by grouping alone.

Large synchronous sends block the HTTP worker even when chunked. A bounded
asynchronous download worker and at least two connections could preserve concurrent
administration, but require progress deadlines, cancellation and completion of all
borrowed requests before server stop. The SDK async API allocates request/auxiliary
copies and retains the socket until completion.
[Async request lifecycle](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_http_server/src/httpd_txrx.c).

Prefer the simplest measured architecture: optimize the permanent reference first,
then compare alternatives using repeated lifecycle cycles,1/2/3 clients, slow and
large transfers, NTRIP TLS and ongoing RTCM delivery. SMB and microSD remain future
candidates to measure with bounded workers, DMA/SPI/cache contention and per-domain
budgets; generic footprint estimates do not reject them. Their implementation,
W5500 TOE and Rust remain outside this exercise.


## Radio payload PSRAM comparator — first matched physical pair

Both images use source tree`1d87ebad932bfd7238f6ea8b9943ece7318e03f6`, the same
CPU/Wi-Fi defaults, seven endpoints, two fast plus one slow caster client,
20 frames/s /1023-byte payload for45 seconds and the same1/2/3-client HTTPS
procedure. The only selected radio storage difference is the explicit allocation
capability. Ring cursors, synchronization and SDK send buffers remain internal;
the13792-byte payload is retained for the boot and never falls back to SRAM.

| Measurement | Internal radio payload | PSRAM radio payload |
| --- | ---: | ---: |
| Actual source / fresh C3 USB frames |902 /902 |901 /901 |
| Sequence gaps / duplicates / CRC errors / MAC failures |0 /0 /0 /0 |0 /0 /0 /0 |
| HTTPS successful requests |60 /60 |60 /60 |
| Three-client sampled internal free minimum |116367 B |129787 B |
| Sampled largest internal block |34816 B |38912 B |
| Post-window internal free |126927 B |140551 B |
| Post-window internal DMA-capable free |119139 B |132763 B |
| Post-window PSRAM free |7201068 B |7187240 B |
| Historical boot internal free minimum |66248 B |79944 B |
| Failed allocation counter |0 |0 |
| Queue peak / maximum dequeue age |1 /97 ms |1 /86 ms |
| Maximum measured push / pop lock hold |56 /56 microseconds |142 /183 microseconds |
| Worker stack remaining high-water mark |2128 B |2112 B |
| CPU0 /CPU1 busy, sustained window |69.896% /54.887% |69.177% /55.641% |
| End-to-end p50 /p95 /p99 /maximum |115.467 /401.042 /451.896 /478.480 ms |103.202 /430.058 /491.204 /510.950 ms |
| Application binary size |1591344 B |1591344 B |

The post-window internal gain is**13624 B**, with**4096 B** more contiguous space
and**13828 B** extra PSRAM usage. DMA-capable memory is a subset of internal memory;
its13624-byte gain must not be added to the internal gain. Runtime heap block
metadata and concurrent allocations mean these observed differences are not an
exact assertion that every13792 payload byte becomes free internal heap. Startup
minima remain distinct from nominal in-use admission thresholds.

Queue lock timings include measurement overhead and cache activity, exclude the
final unlock, and are maxima since boot. Both workers are observed on both cores;
that mask is not a CPU-duration distribution. The CPU figures are50.820398-second
and50.740727-second idle-counter windows, not instantaneous maxima. UART bytes
continue progressing, without increases in any of the five error counters.
The longer PSRAM p99/maximum is recorded; one pair cannot attribute ambient
network/scheduling variability to placement. The measured critical sections stay
below0.2 ms on this pair. Further profile/burst/lifecycle comparisons are pending;
no final default switch or complete-qualification claim follows from this pair.
Original logical network configuration is restored after each window.


### CPU-B failed window retained

The same-source CPU-B comparator (GNSS/router pinned to CPU1, radio and network
workers otherwise unpinned, PSRAM radio payload and Wi-Fi A) fails its first full
window:901 source/routed frames,270 fresh C3 frames,339 transmit failures and292
radio drops, with631 sequence gaps and no CRC errors/duplicates. Queue peak8 and
maximum dequeue age835 ms show backpressure; UART counters do not increase. The
captured first failures are SDK MAC-delivery callbacks, not Send API rejections.
The counters aggregate failure paths, and detailed logs are bounded per boot.
Neither those diagnostics nor average CPU utilization prove the underlying cause.

Without resetting or changing keys, the same CPU-B image then passes the reduced
input-only5fps/256-byte scenario:101/101 C3 frames, no new MAC failures, latency
p50 13.029 ms and maximum24.174 ms. Other physical profile comparisons pause while
an A–B–A control at identical PSRAM placement and full workload is performed.
The unsuccessful window remains part of the evidence. No channel, association,
priority or guard workaround is applied; network settings are restored after it.


The second A control on the same PSRAM placement passes902/902 frames, zero MAC
failures/queue drops, and60/60 HTTPS requests; p99 445.174 ms and maximum468.445 ms.
This A–B–A sequence does not qualify B as a default. It associates the failed
window with that comparator but does not prove a CPU-affinity cause. A retains
the reference role; other comparators remain experiments, without any production
default switch or attempt to hide the failed trial.


The CPU-C comparator delivers903/903 frames and60/60 HTTPS responses, without
UART/radio errors, but is unsuitable as a default: CPU0 averages89.175% busy and
CPU1 22.261%; end-to-end latency p50 1274.846 ms, p95 3148.465 ms, p99 3197.403 ms,
maximum3215.218 ms. Its radio queue peaks at one and maximum dequeue age138 ms;
those diagnostics do not locate all upstream TCP/scheduling delay. The data
integrity check passes while the performance objective fails. CPU-A automatic
affinity is retained as the reference for subsequent Wi-Fi comparisons.


### Same-source comparator summary

All rows refer to the recorded1d87 source and their exact build flags. External
I/O is plaintext NTRIP only; the HTTPS server stack remains internal. These
single-window results do not qualify future bidirectional radio traffic.

| Comparator | Fresh C3 / source | Minimum sampled internal | Largest | CPU0 /CPU1 | p99 /maximum latency |
| --- | ---: | ---: | ---: | ---: | ---: |
| A internal |902 /902 |116367 B |34816 B |69.90% /54.89% |451.896 /478.480 ms |
| A PSRAM |901 /901 |129787 B |38912 B |69.18% /55.64% |491.204 /510.950 ms |
| CPU B |270 /901 |129931 B |38912 B |70.52% /54.49% |642.351 /881.549 ms |
| CPU C |903 /903 |129523 B |38912 B |89.17% /22.26% |3197.403 /3215.218 ms |
| Wi-Fi B |903 /903 |137619 B |45056 B |68.41% /55.57% |455.661 /477.220 ms |
| Wi-Fi B2 |902 /902 |137379 B |45056 B |68.75% /55.21% |469.299 /489.618 ms |
| AMPDU-off control |902 /902 |136179 B |45056 B |69.09% /54.80% |513.061 /551.185 ms |
| Wi-Fi C |902 /902 |140127 B |49152 B |69.06% /55.51% |573.610 /594.999 ms |
| External I/O stack |902 /902 |142551 B |51200 B |68.70% /55.81% |540.657 /553.996 ms |

B2 shows no meaningful permanent saving over B in the comparable post-window
snapshots (147459 versus147455 B free). AMPDU-off at equal RX6 also shows no
saving (147383 B). RX4 gains3424 B over B but lowers the RX reserve and disables
aggregation without bidirectional qualification. B retains32/32 dynamic maxima
and AMPDU while obtaining its measured RX6 saving. Dynamic maxima are capacities,
not permanent allocations. Keep the conservative B candidate for lifecycle tests.

The external I/O stack moves about12 KiB out of internal memory on the tested
plain-TCP path. TLS renegotiation, mTLS, ROM/DMA and transitive Flash-sensitive
paths remain unqualified; leave that option disabled. Do not count its gain as
an implemented production saving. CPU-B is not selected after its failed window;
CPU-C passes integrity but fails the latency/CPU objective. CPU-A is retained.


### Transition series stopped on UART discontinuity

The first executable bench attempt stops before any configuration change because
of a local Python name collision; it proves no transition. After correcting that
bench, the real series proves32 complete cycles plus activation33 (**65**
transitions). The following deactivation fails the UART contract: the aggregate
overflow counter rises0→1 and received-byte accounting resets after invalidation.
Line errors, event saturation, read failures and drain-limit counters stay zero.
All**99/99** synthetic frames nevertheless arrive fresh on C3 with valid CRC and
correct per-cycle sequences. Quiescent internal free remains at least148067 B,
largest45056 B. **The100-transition acceptance has not passed.** The source is
stopped, logical original network settings restored and capture closed.

The pinned SDK explicitly documents reception-loss risk during Flash operations
when`CONFIG_UART_ISR_IN_IRAM` is disabled, as on that image. The aggregate cannot
distinguish hardware FIFO overflow from a full software RX ring; no retrospective
GC attribution is justified. Bounded diagnostics now distinguish both types,
record dequeue-observation timestamps/order and active poll gaps, and retain eight
non-sensitive timing records of`nvs_set_blob` and`nvs_commit`. They preserve the
invalidation/flush path and stored configuration format. The simple NVS backend
performs writes/page management during set, while commit checks the handle.
Paired IRQ-in-Flash/IRAM builds and physical correlation remain pending. Internal
memory required by an IRAM-safe UART driver must be measured and justified;
no protection threshold is reduced to conceal its cost.


## UART Flash/IRAM correction — physical paired evidence

Paired source `70af63288a2c215f30cf269b2a9590cf57d27e9b` uses identical
CPU A / PSRAM radio / Wi-Fi B settings, differing only in the UART ISR setting.
With the ISR in Flash, five configuration writes reproduce **one hardware FIFO
overflow**, no software-ring overflow. Its dequeue observation at 14.620647 s
falls within write 2's `nvs_set_blob` interval [14.561866, 14.643597] s.
Set operations take 39–94 ms; commit takes 23–26 microseconds. This correlates
with set activity; it does not identify a particular erase/GC operation or give
an ISR timestamp. The maximum active poll gap is 50.821 ms.

With the ISR in IRAM, **81 writes / 20 batches** pass with no FIFO/ring/line/event/
read/drain errors; maximum poll gap 51.025 ms. Executable IRAM grows **2560 B**.
The matching DRAM dummy-section increase aliases that memory, not a second cost.
After the first stress window internal free changes 150799→140963 B, largest
45056→34816 B, PSRAM free 7248988→7256280 B. DMA-capable free is an overlapping
subset, not additive savings. IRAM-safe SDK driver buffers must remain internal;
this measured safety cost is accepted without lowering memory guards.

The final source requires and verifies `CONFIG_UART_ISR_IN_IRAM=y` for the S3
GNSS build. Radio payload PSRAM and conservative Wi-Fi B (6 static RX, 32/32
RX/TX dynamic maxima, AMPDU retained) become S3 radio-owner defaults. CPU A stays
automatic. External I/O-stack placement stays **off**: plain-TCP measurements do
not qualify TLS/Flash/ROM/DMA paths. No receiver rate, pins or baud is changed.
Observed receiver traffic is about 625 B/s; UART remains 460800 8N1 (46080 B/s
line capacity). Actual receiver navigation frequency has not been queried;
synthetic RTCM frames/s must not be confused with receiver epochs/s.

## Latest transition failure and safe stop

The IRAM series proves **42 transitions / 21 complete cycles**. Activation 22
routes three further frames, but all three fail MAC delivery: **63/66** fresh USB
frames across that series, no CRC errors/duplicates, no UART errors, no queue
drops or quarantine. The strict bench stops; the 100-transition criterion has
**not passed**. Quiescent free stays at least137731 B, largest34816 B.
Possible ambient interference is a hypothesis, not an established cause.

Without reboot, rekey or association change, a separate input-only 1 frame/s
recovery window delivers **11/11**, maximum latency21.180 ms, no new MAC failure.
This proves bounded recovery in that window, not resolution of intermittent loss.
Original logical settings are then restored and independently checked; pinned
HTTPS responds, synthetic source is stopped and both USB ports are released.
The source-header cancellation, TLS-helper cancellation and burst benches are
prepared but **not executed**. Preserve failed windows alongside successful ones.

The generic ESP32 knowledge-preservation checkpoint indexes reusable results,
rejected alternatives and private evidence locations. Extraction into another
repository, including ArduPilot/DroneCAN adaptations, is deferred.

Saved implementation commit: `b8a8d57f7f120522fb3b999b517dfc6e97e51bbc` (caster dependency `1c1dc9f71fd8da3ebf853c7fc9bf1491ab26197f`).
