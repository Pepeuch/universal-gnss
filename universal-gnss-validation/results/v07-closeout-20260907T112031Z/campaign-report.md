# v0.7 non-endurance closeout campaign

Target: mower RPi `192.168.10.35`, current Kilted arm64 sidecar image
`sha256:e7e648049cd8aa2e1ff2ede4598a9453e8e48374d96684dafd89faefc50110ab`,
Universal GNSS revision `0bcfdff9135992978769a6573ec62ae8e3d9a30f`, and the
u-blox stable by-id path recorded in `metadata.json`.

No mower motion, navigation, motor/blade action, receiver firmware operation,
persistent receiver write, USB action, or endurance test occurred.

## Receiver-child recovery — PASS

At `2026-09-07T11:22:10.854Z`, only the receiver child was sent `SIGKILL`.
The NTRIP child stopped as the launch group exited. Docker retained container ID
`5b397933ffec55eea3e1a04f233060067fe009ca870da33460389b657ed55b58`,
incremented restart count `0 -> 1`, and restarted under `unless-stopped`.
Receiver PID changed `12908 -> 33919`; NTRIP PID changed `12935 -> 33978`;
source incarnation changed
`45909fcb0ec0ef205ace22420e05fc30 -> a244a4285d7d3d32250c9461e8edd1ae`.
The first captured new epoch was sequence 33 with a post-restart observation
timestamp. Sequence then advanced, correction state re-established, stale_data
remained false, and both health services plus Docker health returned healthy.

This proves the receiver-process recovery gate. It does not prove a
test-correlated retired-byte cutoff or close UGA-126.

## Low receiver rate / high publication rate — PASS

The physical u-blox profile remained at 7 Hz. A least-privilege test container,
selected by the host stable by-id and without running the startup configurator,
set only `publish_rate_hz=20.0`. Raw `low-high-status.csv` contains 84
publications representing 30 unique position sequences: 54 same-sequence
transitions, comprising 52 exact cached republications and two accepted NAV-SAT
aggregate updates whose runtime stamp advanced without inventing a position.
There was no sequence decrease and the maximum sequence jump was one. Collector
diagnostics reported no stale/no-data samples, restart, process/incarnation
change, parser transition, or corruption. See `low-high-analysis.json` and the
`low-input-high-publication/` collector directory.

## High receiver rate / low publication rate — PASS

The physical u-blox profile again remained at 7 Hz while the isolated
least-privilege receiver container set only `publish_rate_hz=1.0`.
`high-low-topic-hz.txt` reports a bounded 1.000 Hz publication rate. Eighteen
published observations advanced sequence 245 -> 364, exactly seven positions
per publication with no decrease; observation timestamp gaps were
0.980024..1.019985 seconds. The collector reported no stale/no-data sample,
restart, process/incarnation change, diagnostic transition, or corruption. See
`high-low-analysis.json` and the `high-input-low-publication/` collector
directory.

## Rollback / final state

Each isolated rate container was stopped and removed before the original
`mowgli-gps` container was restarted. Final evidence at
`2026-09-07T11:34:27.131Z` records the original container ID and image digest,
Docker healthy, receiver PID 48224, NTRIP PID 48359, stable by-id source,
advancing sequence, valid fix, active corrections, and both health services
responsive. Restart count is zero after the explicit clean stop/start restore.

## Gates deliberately left open

- Strong incarnation/stale-state gate and UGA-126: no safe existing seam
  injected a correlated retired-incarnation byte/response for explicit
  exclusion.
- Persistent receiver/profile configuration: no persistent action was taken.
- Long-run container validation: explicitly excluded.
- Multiarchitecture publication: no registry action occurred.
