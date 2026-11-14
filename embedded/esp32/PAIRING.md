# Native ESP-NOW pairing and local USB control v1

Status: software implementation; physical S3/C3/CCMP/USB acceptance is pending.
One peer is supported per device. No C6, LoRa, RF discovery or remote association
protocol is enabled. A MAC read through local USB is manual identification, not
an RF discovery result. No firmware variant contains a user's peer or channel.
[Portable configuration v1](PORTABLE_CONFIG.md) adds private-free CONFIG_GET/CHECK/APPLY
commands in this same maintenance phase; USB/UART output is selected before data starts.

## Authorization and persistence

The gateway defaults to radio **off**, with no peer/key fallback. A fixed channel
(1–11) and unicast STA MAC require a private shared 16-byte LMK. Both ends configure
SDK encrypted unicast and install that LMK; each boot creates its own random local
PMK. See the [IDF 6.1 security contract](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/network/esp_now.html#security).
The receiver additionally checks the source peer and its own unicast destination.
This is not a claim that CCMP/RF acceptance has been physically qualified.

S3 changes use the existing authenticated HTTPS administration interface. The
operator opens a 120-second local window and explicitly confirms association.
The secret is never returned by status/export APIs. Removal has its own explicit
confirmation. Generic Web JSON edits cannot bypass the pairing window; settings
rollback preserves current radio identity, as well as admin identity/quarantine.
Console provisioning remains a trusted local operation. No radio packet can write
configuration. A mode window does not broadcast an invitation to unknown peers.

The S3 stores settings through the existing validated dual-bank store. C3 stores
one atomic NVS blob (`ug_rover/pairing`, 32 bytes): magic/version, paired flag,
channel, peer, LMK and existing UG CRC24Q. CRC detects corruption, not malicious
flash modification. These keys are not protected against physical flash access
unless a separate qualified flash-encryption policy is added. Unknown versions,
bad CRC, I/O and readback failures lock writes/startup; no namespace/flash erase
is automatic. Old experimental plaintext-radio configurations are rejected rather
than silently treated as authenticated pairs; they need explicit local migration.

## C3 control channel and future ROS2 adapter

USB Serial/JTAG maintenance runs **before** Wi-Fi, ESP-NOW and RTCM output, including
when RTCM output is UART1. After an ordinary paired reset, the first 8 seconds are
silent. A local host can send `UGCFG/1 OPEN <16-hex-random-nonce>\n`; the control
response echoes that unpredictable nonce. A tool must validate it, preventing a
normal RTCM payload from impersonating an unsolicited configuration handshake.
An unpaired/locked device stays silent and available for local maintenance.

The explicit maintenance window lasts 120 seconds; another local OPEN renews it.
Commands are ASCII, LF terminated, at most 128 bytes, with exact single spaces:

```text
UGCFG/1 GET
UGCFG/1 SET <BASE_STA_MAC> <CHANNEL> <32_HEX_LMK> CONFIRM_PAIR
UGCFG/1 CLEAR CONFIRM_UNPAIR
UGCFG/1 RUN CONFIRM_RTCM
```

Replies are one `UGCFG/1 ` prefix followed by bounded JSON and LF. GET returns
identity, pairing state, channel, key presence and storage status, never the LMK.
The `ttl_ms` field reports the configured 120-second control-window limit, not
a continuously updated remaining time.
SET/CLEAR commit and verify storage before acknowledgement. RUN requires a valid
pair; it emits `{"mode":"closed"}` and drains control TX before the data plane
starts. At timeout a paired valid device does the same; an unpaired device returns
to silent local waiting. A failed drain does not start RTCM. No application
commands/diagnostics are emitted in normal USB/UART RTCM mode. Inputs in that mode
are not interpreted as control. ROM boot bytes remain a separate hardware concern.

A future ROS2 adapter must enter an **explicit maintenance phase**, stop forwarding
RTCM, arrange an operator-approved reset, validate the fresh nonce, and process
control replies separately. It must only resume its RTCM parser after the closed
marker. Reset/re-enumeration, Linux grants and actual USB delivery remain physical
acceptance tasks. This protocol does not offer live unsolicited diagnostics in a
binary stream, nor does it implement a ROS2 node or automatic reset here.

## Reproducible operator procedure (not executed on hardware here)

1. Identify both boards and actual STA MACs. Read the S3 MAC from its authenticated
   pairing status and the C3 MAC through maintenance. USB IDs are not Wi-Fi MACs.
2. Reset the identified C3 manually and run the tool within its boot window:

```bash
python3 embedded/esp32/tools/rover_config.py /dev/serial/by-id/<identified-C3> \
  --set-peer <S3-STA-MAC> --channel 1 \
  --new-key-file /private/new-pair.key --s3-ticket /private/new-s3-ticket.json
```

The two files are created with mode 0600 and no overwrite. Keep them outside Git,
issues, public profiles, logs and captures. The tool performs no scan/reset/flash.
3. In S3 WebUI, load the private ticket locally, open the pairing window and
   confirm the association. The status distinguishes configured/effective channel,
   startup and MAC ACKs; an ACK is not proof of rover USB reception.
4. Restart the S3 explicitly. End C3 maintenance with the same tool and `--run`,
   or wait its documented timeout. Both ends must use the same channel/key and
   each other's STA MAC. Pair/channel changes need no compilation.
5. Capture RTCM with the existing Linux tools after maintenance ends. Never feed
   control JSON into an RTCM consumer. Qualify W5500 and GNSS UART wiring first.

S3 pair changes latch TX stopped and invalidate pending frames until reboot.
The receiver's new boot starts fresh reassembly state; configuration never edits
an active C3 codec. Already in-flight physical bytes cannot be recalled. Radio
configuration does not depend on Internet/NTRIP availability. No Wi-Fi STA join,
automatic channel selection or network/Zigbee configuration change is performed.
