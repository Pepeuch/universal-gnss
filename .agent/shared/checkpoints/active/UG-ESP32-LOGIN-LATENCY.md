# S3 login latency — still open

Lifecycle: ACTIVE
Status: PARTIAL
Scope: DEPLOYMENT (ESP32 authentication)
Canonical tracking: [TODO](../../../../TODO.md), acceptable login latency remains open.
Code baseline: `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`.

## Contract and proven state

Single login/session/logout and password persistence work on the recorded S3.
[Latency and freeze report](../../../../docs/analysis/ESP32_LOGIN_LATENCY.md) is the
measurement authority. Compatible SDK SHA/HMAC optimization reduces public-vector
median to 36.844149 s; one negative HTTP login is 38.579958 s. Owner confirms
positive login with the existing password after reboot. This is NOT acceptable
latency completion, and the negative-request duration is not a positive-login metric.
PBKDF2-HMAC-SHA256 remains 600000 iterations, 16-byte salt, 32-byte result, NVS v1
56-byte record. Contexts/pads/results are wiped, no credential-proof cache added.
Source: `ug_platform/src/ug_web_auth_platform.cpp` and
`ug_platform/include/ug_password_hmac.hpp` under `embedded/esp32/components/`.
Tests: `embedded/esp32/native/tests/test_password_hmac.cpp`, injected failures and
OpenSSL equivalence; 68 sanitizer tests and reproducible HTTP/HTTPS builds passed.

## Exact next step

Profile the remaining KDF cost/yield schedule with public test vectors and decide
on a compatible bounded optimization before any new hardware operation. SDK private
SHA-driver use must be requalified if the pinned SDK changes. Do not lower the work
factor, introduce shared secrets, reset credentials, or assume a browser-proof design
has been approved. No implementation is authorized by checkpoint maintenance.

## Baseline and pending evidence

[Last qualified baseline](../retained/UG-ESP32-QUALIFIED-BASELINE.md) records the
installed image, HTTP/radio state and protected data; it is a prior physical
observation, not a fresh probe. Concurrent authentication, endurance, and latency
under real GNSS/RTCM are pending. Reuse existing tests unless their inputs change.
