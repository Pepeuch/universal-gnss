# Native firmware host contracts

These tests compile the actual portable UG engine and native adapters with
SDK boundary stubs where necessary. They use synthetic data, loopback TCP and
PTYs; they do not open physical ports or qualify RF, electrical behaviour,
SDK task scheduling, TLS peak heap or NVS power-loss recovery.

Run as the normal project user, from the repository root, with the pinned
managed cJSON source (`espressif/cjson 1.7.19~2`):

```bash
cmake -S embedded/esp32/native/tests -B /tmp/ug-native-host -DUG_CJSON_SOURCE=/path/to/cJSON -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build /tmp/ug-native-host -j4
ctest --test-dir /tmp/ug-native-host --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 embedded/esp32/native/tests/test_capture_tools.py
```

The separate browser mock requires host-only `quickjs==1.19.4`; it is not a
firmware dependency. None of these commands downloads an SDK or flashes a board.

`embedded_web_identity_contract` compiles the production identity loader with
NVS/PSA boundary stubs and allocation fault injection. It checks missing,
unreadable and invalid records, memory failures, server-error classification,
unchanged NVS bytes and zeroization of released identity storage. These synthetic
tests do not validate cryptography or physical NVS persistence; those require the
pinned SDK and a separately identified/provisioned board.

## Bounded wire mutation corpus

`embedded_wire_corpus_contract` exercises the actual shared ESP-NOW codec and
RTCM stream adapter with seed `0x47554e53` and a 15-second CTest limit:

- 2400 fragmented-frame scenarios: nominal, missing fragment, bad CRC16,
  corrupted RTCM with repaired radio integrity, duplicates and expiry.
- 800 positive radio frame completions checked byte-for-byte against the source.
- 12000 stateful structured mutations, including repaired outer CRC/header
  integrity and truncated packet lengths.
- 2000 arbitrary-byte stream chunks and known valid containers spanning payload
  sizes 0, 1, 2, 20, 206, 207, 208 and 1023, with random input splits.

Every emitted container must pass the actual UG CRC24Q/length check. Mutated
radio headers cannot bypass reassembly bounds. Loss/duplicate/expiry cases have
explicit completion expectations; valid output cannot be replaced by padding or
duplicated prefixes. CRC16 remains the existing radio integrity algorithm.

This is a reproducible property corpus, not exhaustive or coverage-guided fuzzing,
cryptographic authentication, or a hardware-delivery proof. It runs against the
existing wire format without enabling or qualifying a LoRa radio.

La [refonte WebUI](../../WEBUI.md) ajoute des contrôles QuickJS de navigation,
file API, sessions, réponses tardives et suspension en arrière-plan. Le test
optionnel `test_webui_browser.py` utilise Chromium/Playwright 1.56.0 sur des API
synthétiques, sans appareil ni flash ; voir la procédure et les limites dans ce guide.
