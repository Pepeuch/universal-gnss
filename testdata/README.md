# Test Data Corpus

This directory holds small synthetic or sanitized GNSS vectors for regression
tests and offline tooling checks.

The files are intentionally tiny:
- `nmea/basic_fix.nmea`: basic text NMEA fix stream with `GGA`, `RMC`, `GSA`,
  and `GSV`.
- `ubx/nav_pvt_sat_monrf.ubx`: compact UBX binary stream with `NAV-PVT`,
  `NAV-SAT`, and `MON-RF`.
- `rtcm/basic_msm.rtcm`: small RTCM3 correction stream with `1005`, `1077`,
  and `1087`.
- `unicore/basic_ascii.log`: small Unicore ASCII log with `BESTNAVA`,
  `RTKSTATUSA`, `RTCMSTATUSA`, and `SATSINFOA`.
- `mixed/nmea_ubx_rtcm_unicore.bin`: mixed stream with noise, NMEA, UBX,
  Unicore ASCII, RTCM3, one checksum-invalid UBX frame, and one truncated RTCM
  tail.

Guidelines:
- No private field logs are stored here.
- Coordinates and payloads are synthetic or sanitized test fixtures.
- The mixed and binary files are sized for unit tests, not performance tests.
- `unicore/basic_ascii.log` intentionally keeps `CRLF` line endings to mirror a
  real serial text log. Regenerate it with the same `\r\n` framing used by the
  canonical Unicore ASCII test helpers rather than normalizing it to bare `LF`.

Regeneration:
- There is no dedicated generator script checked in yet.
- The current files mirror the synthetic helper payloads already embedded in
  `gnss_tools/tests/test_gnss_replay.cpp`,
  `gnss_tools/tests/test_gnss_stream_inspector.cpp`,
  `gnss_protocols/tests/test_ubx_nav_pvt.cpp`,
  `gnss_protocols/tests/test_ubx_nav_sat.cpp`, and
  `gnss_protocols/tests/test_ubx_mon_rf.cpp`.
- If one of those canonical synthetic payloads changes, regenerate the
  corresponding file with the same bytes and keep this README in sync.
