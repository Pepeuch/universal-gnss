# ESP32 NTRIP filters and real-source qualification

Lifecycle: ACTIVE
Status: PARTIAL (filters implemented; remaining software/hardware paths explicit)
Scope: NTRIP
Validation: HARDWARE_REQUIRED for receiver output and timing; GGA return software unfinished.
Canonical tracking: [TODO](../../../../TODO.md), NTRIP qualification/GGA remains open.
Code baseline: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`.

## Implemented contract / evidence

[Session/NTRIP report](../../../../docs/analysis/ESP32_WEBUI_SESSION_NTRIP.md) records
five independently persisted publisher lists and individual monotonic interval
state. Types 1..4095, <=32 rules, <=512 characters; integer intervals 0..86400 s.
Bare type/zero sends every available frame; empty list passes all for compatibility.
Commit interval only on successful full-frame send; no synthesis, sleeping or queue
held merely to satisfy an interval. Other RTCM outputs remain unchanged.
`embedded/esp32/components/rtcm_filter/include/ug_rtcm_rules.hpp` and native
`test_publisher_filters.cpp` own the policy. Two real TCP loopback casters receive
CRC-valid bytes with different lists, using simulated interval clocks. Logical
Store/reboot persistence is tested; this is not a physical two-caster GNSS campaign.
Authenticated on-demand NTRIP password visibility and owner-confirmed WebUI journey
are retained without publishing any private credential.

## Unfinished boundaries / exact next step

NEAR requires rover-position GGA. No fixed GGA is configured; rover-to-S3 return
transport is not implemented. Do not synthesize a position or present a local S3
GGA as the requested rover source. Review that software contract separately first.
The current Unicore BASE plan emits MSM4; requested publisher lists include MSM7.
Filters cannot create absent message types and must not silently reconfigure GNSS.
Before declaring real output qualified, verify exact receiver/firmware/UART profile,
requested vs produced types, two destinations, timestamps/CRC, reboot persistence,
CPU/stack and routing latency, with private configuration backed up and restored.
No connected GNSS receiver was used in this campaign; no RTK FIX/quality claim.
No physical or real-caster configuration action is authorized by this maintenance.
