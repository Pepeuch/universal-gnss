# Example: Airseekers Tron `rtk_rover` correction gateway

> **Example configuration, not a code path.** Universal GNSS contains no
> Airseekers-specific logic. This page shows how the generic, off-by-default
> [RTCM gateway handshake](../../ros2.md#rtcm-gateway-handshake-optional)
> parameters describe one real correction-input gateway. Every string and
> timing below is configuration supplied by the operator.

## Hardware

The Airseekers Tron robotic mower carries a Unicore UM960 receiver behind an
AT32 microcontroller board (`rtk_rover`) on the same UART. The board owns a
LoRa radio that receives RTCM from the vendor's base station and multiplexes
corrections into the receiver:

- in **LoRa mode** (its default) it feeds the receiver from the radio and does
  **not** pass host-written bytes to the receiver;
- in **network RTK mode** it passes host RTCM (for example NTRIP) through to the
  receiver.

## Observed gateway protocol

| Behaviour | Value | Hardware check (2026-10-14) |
| --- | --- | --- |
| Enable network RTK mode | host writes the exact line `$GNRTK,ON*69` + CRLF | verified: the board reported `$GNRTK,ON` about 8 s after host writes resumed |
| Keep-alive | repeat the same line about every 4 s | the board's fallback timer is also reset by host RTCM itself: with RTCM flowing, 292 s without the line left the board in network mode and the fix RTK fixed. The line is load-bearing only before the first frame and after a gap in corrections; repeating it every 4 s is the cheap, safe choice |
| Fallback | after roughly 6 s without any host byte the board returns to LoRa mode | verified: `$GNRTK,OFF` 5.9 s after the last host write |
| Mode reports | the board interleaves `$GNRTK,ON` / `$GNRTK,OFF` sentences with the receiver's NMEA | reported on change (verified); whether the report repeats while in network mode is not verified |
| Vendor reset on drop-out | the vendor driver writes `RTKRESET` + CRLF on an ON->OFF edge before re-enabling | not needed: an `RTKRESET` sent in network mode with RTCM flowing had no visible effect |
| Hand back to LoRa | stop every host write (keep-alive and RTCM); there is no explicit disable command | verified. Switching back to network mode afterwards took about 8 s and the receiver produced no solution for about 10 s |

Not verified: whether host RTCM alone switches the board out of LoRa mode (the
vendor driver always writes the enable line first, so this could not be
isolated without a code change). The receiver also held RTK fixed on stale
corrections for 120 s (`diff_age`), not the vendor-configured 20 s, so judge
correction health by the differential age rather than the fix type alone.

## Minimal configuration (keep-alive only)

The keep-alive line doubles as the enable line, so no separate enable command
is needed; no disable command is configured because the board falls back to
LoRa on silence.

```yaml
universal_gnss_receiver:
  ros__parameters:
    receiver_family: unicore
    transport: serial
    serial_device: /dev/ttyS1   # board-specific; adjust
    serial_baud: 115200          # board-specific; adjust
    rtcm_forwarding_activity_timeout_s: 5.0
    rtcm_gateway_keepalive_command: "$GNRTK,ON*69"
    rtcm_gateway_keepalive_interval_s: 4.0
    rtcm_gateway_line_ending: crlf
```

Resulting behaviour:

1. the first NTRIP RTCM frame on `rtcm` writes `$GNRTK,ON*69\r\n`, then the
   frame;
2. while frames keep arriving, `$GNRTK,ON*69\r\n` is re-written every 4 s;
3. 5 s after the last frame (`rtcm_forwarding_activity_timeout_s`) the
   keep-alive stops; about 6 s later the board is back on LoRa. Total hand-back
   latency is therefore roughly 11 s after NTRIP corrections stop. Stopping the
   receiver node also stops the keep-alive.

The checksum `*69` is part of the operator-supplied string; Universal GNSS never
computes or validates it.

## Optional: acknowledgement gating and reset on drop-out

If the board reliably reports its mode, RTCM can be withheld until it confirms
network mode, and an unexpected fallback can be handled like the vendor driver
does:

```yaml
    rtcm_gateway_ack_pattern: "$GNRTK,ON"
    rtcm_gateway_ack_timeout_s: 6.0
    rtcm_gateway_nack_pattern: "$GNRTK,OFF"
    rtcm_gateway_reset_commands: ["RTKRESET"]
```

- frames are discarded (counted as `gateway_frames_withheld`) until a line
  containing `$GNRTK,ON` arrives; every 6 s without it the enable line is
  re-sent and `gateway=lost` is reported;
- once acknowledged, a line containing `$GNRTK,OFF` writes `RTKRESET\r\n` and
  then `$GNRTK,ON*69\r\n`, and frames are withheld until the next acknowledgement.

Only enable this variant once the board is known to emit the mode sentences
while in network mode; otherwise corrections would be withheld indefinitely.
The plain keep-alive configuration above does not depend on inbound reports.
On the hardware checked above `RTKRESET` made no difference, so
`rtcm_gateway_reset_commands` can stay empty; the nack pattern alone re-sends
the enable line.

## Diagnostics to check

`universal_gnss/rtcm_forwarding` then reports `gateway=active`, increasing
`gateway_keepalives_sent`, and, with the acknowledgement variant,
`gateway_acks_seen` and `gateway_last_ack_age_s`.
