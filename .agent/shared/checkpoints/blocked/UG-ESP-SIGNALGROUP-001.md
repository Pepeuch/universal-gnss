# UG-ESP-SIGNALGROUP-001 — native signal-group reconfiguration

Lifecycle: BLOCKED
Status: BLOCKED
Scope: RECEIVER (ESP32 native)
Validation: HARDWARE_REQUIRED, with unfinished software/design prerequisites.
Canonical status: [TODO.md](../../../../TODO.md).
Baseline: `d59ba1e972780fe1562ba4e2b221ef7775af162f`; the native rejection guard
is committed alongside this record. Source/tests remain authoritative.

## Contract and evidence

[Unicore N4 R1.6, §4.23 p.40 (PDF page 58)](https://en.unicore.com/uploads/file/Unicore%20Reference%20Commands%20Manual%20For%20N4%20High%20Precision%20Products_V2_EN_R1.6.pdf#page=58)
documents automatic persistence and module reset when SIGNALGROUP changes.
Reviewed PDF SHA256:
`4dcecd02e696fa3cfe030e362fcbe4308c18df0793cd5c67df8067b366a33a49`.

The native `BuildTelemetryPlan` rejects every nonempty signal-group selection,
including explicitly confirmed persistence, before constructing a prefix.
Capabilities report `signal_groups_configurable:false`; the model catalogue is
read-only. Ordinary telemetry/F9P constellation planning remains available.
Decision: ADAPT the legacy feature through explicit safe lifecycle ownership;
do not port its implicit persistent/reset behavior as runtime telemetry.

The shared `UnicoreConfigProfileBuilder` still uses `kRuntime` for this command.
Its public safety metadata needs separate core review; the native guard does not
change or qualify Linux/ROS2 direct callers. No generic recovery fence was found
or added. Reuse the [existing response-fence boundary](UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md).

## Required delta / unblock condition

1. Design and review explicit approval of both persistence and reset, including
   full-plan preflight, prior setting saves and interrupted-operation journaling.
2. Define and implement a provider whose receiver/transport-incarnation contract
   establishes the required exclusion of earlier bytes/responses. No reset,
   delay, model query or reopen is accepted as that guarantee by itself.
3. Validate on exact UM980/UM982 firmware, board/UART/port and buffer topology:
   changed/unchanged groups, reset timing, partial writes, interruption, late
   replies and failed recovery. Only a proven new incarnation may permit later
   configuration; failures keep quarantine and invalidated observations.
4. Revisit the shared safety classification under an explicitly reviewed public
   compatibility change before enabling a native application path.

This is not a hardware-ready gate: software prerequisites are still open.
No device operation or flash was performed in this work.

## Validation and resumption

`embedded_telemetry_operation_contract` rejects model/persistence variants and
proves empty output before dispatch. The 37 native sanitizer CTests and browser
mock pass; S3/relay SDK builds, role/partition checks and independent reconstruction
pass. Durable details: [migration guide](../../../../embedded/esp32/migration/README.md#signalgroup-native-dispatch-safety-boundary).

Exact next action: perform the explicit persistent/reset lifecycle design and
shared-metadata review; keep the native gate closed. Do not repeat protocol
archaeology or an expensive build merely because the session/model changed.
Invalidate this evidence only if the command contract, planner/owner/journal,
recovery provider, relevant model/firmware or validation environment changes.
