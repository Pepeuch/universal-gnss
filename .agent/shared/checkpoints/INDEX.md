# Shared checkpoint index

For the consolidated rc6 campaign diagnostics and current documentation review,
see [the retained campaign handoff](../RC6-20261108-REVIEW.md). Main-only stable
preparation and publication gates are [ACTIVE here](../RELEASE-V0.7.4.md).
Formal backlog status remains
in TODO/manifest; the durable report and figures are linked from that handoff.

- `active/UG-PRE-RELEASE-001_CHECKPOINT.md` — release-blocker audit at
  `5c50f6c`: F01–F07 are committed through `d70bee1`; F08 was implemented and
  regression-tested in that historical audit worktree. Includes source, PTY,
  stalled-TLS, indeterminate-apply, and Docker shell evidence. The audit stays
  active for independent release and hardware qualification.
- `retained/REPOSITORY_BASELINE_AUDIT_CHECKPOINT.md` and
  `retained/REPOSITORY_BASELINE_AUDIT_LEDGER.md` — frozen audit baseline,
  stable IDs, duplicate graph, and reusable evidence; current classifications
  and counts come from the manifest/TODO.
- `closed/UG-DOCS-STATUS-001_CHECKPOINT.md` — completed generated UGA status
  dashboard and CI validation contract.
- `blocked/UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md` — hard quarantine is
  implemented; a qualified cutoff/recovery contract is still required before
  any automatic release.
- `blocked/UG-DRIVER-TRANSPORT-INCARNATION-001_CHECKPOINT.md` — POSIX serial
  cannot prove an end-to-end prior-byte cutoff; implemented quarantine remains
  blocked from automatic recovery pending a qualified provider.
- `active/UG-MOWGLI-694-RC2-POSITION-FREEZE.md` — RC2 field trace proves no
  differing valid coordinate reached the post-mapping freshness boundary;
  the supplied JSONL lacks freshness diagnostics, so a narrow real-Unicore
  diagnostic capture is next before deciding whether raw bytes are required.
- `active/UG-PLAN-DEPLOYMENT-001_CHECKPOINT.md` — portable runtime/API/GUI/Docker
  and BlueOS dependency planning; `TODO.md` remains the source of truth.
- `active/UG-PLAN-005_ROBOT_SECOND_RPI_VALIDATION.md` — bounded robot and
  second-Raspberry-Pi campaign evidence through Phase F plus the 2026-09-07 RTK,
  receiver-child recovery, and rate-mismatch closeout; retains the remaining
  v0.7 hardware-validation plan, with a separate 2026-11-08 rc6 passive-campaign
  addendum; earlier physical matrices keep their exact historical baselines.
- `retained/UG-PLAN-005_EXTERNAL_LAN_DDS.md` — completed external-LAN DDS
  matrix: unmodified bridge multicast failed; explicit bridge/unicast locators
  passed both directions and different-domain non-delivery.

- `blocked/UG-ESP-SIGNALGROUP-001.md` — native Unicore signal-group reconfiguration:
  automatic persistence/reset is documented; native mutation is refused pending
  explicit reset/persistence approval, qualified incarnation recovery and hardware
  evidence. Shared driver safety classification needs separate review.

## ESP32 and compact-buffer handoff — reconciled 2026-11-16

ESP32 qualification baseline is `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`; the
compact-buffer review commit is `8b1b446b71bec60f24e1e2426282033cf901a9f6`. These entries cache evidence and next actions;
TODO/reports/source remain authoritative. No new physical/remote qualification was
performed for this documentation reconciliation.

- `active/UG-GNSS-COMPACT-BUFFERS.md` — exact timestamp/storage proposal, benchmark
  provenance, CPU regressions and pending review before integration.
- `active/UG-ESP32-LOGIN-LATENCY.md` — compatible login works but ~38.6 s remains
  excessive; no credential reset or lowered work factor.
- `active/UG-ESP32-RADIO-MEMORY.md` — active-radio largest block 31744 B fails
  32768 B acceptance; inactive memory gains are distinct.
- `active/UG-ESP32-USB-PAIRING.md` — HTTPS Web Flasher USB integration, partial
  pairing failure handling, requested multiple peers/broadcast still unfinished.
- `active/UG-ESP32-NTRIP-QUALIFICATION.md` — independent filters proven in software;
  rover GGA return, real receiver output and physical routing budgets unqualified.
- `active/UG-ESP32-PUBLICATION-AUTHORITY.md` — one-off historical tag recovery is
  complete; maintainer-authorized RC Release path is implemented locally, with
  hosted qualification pending main deployment. Tag hardening is explicitly deferred.
- `retained/UG-ESP32-QUALIFIED-BASELINE.md` — bounded HTTPS/HTTP/WebUI/memory
  evidence and last recorded installed S3 state; prevents obsolete recovery steps.
- `retained/UG-ESP32-PUBLICATION-RECOVERY.md` — listing/glob defects and actual
  approved S3/C3 Release/Pages recovery, distinct from later local firmware.
