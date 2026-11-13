// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_admin.hpp"
#include "ug_board.hpp"
#include "ug_json.hpp"
#include "ug_settings.hpp"
#include <cassert>
#include <iostream>
using namespace ug::embedded;
int main()
{
  Capabilities base{true, true, false, 5, "waveshare-s3-eth", "gateway"};
  base.gnss_pin_mask = GnssPinsForBoard(base.board);
  for (int pin : {0,  3,  9,  10, 11, 12, 13, 14, 19, 20, 26, 27,
                  28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 45, 46})
    assert(!(base.gnss_pin_mask & (UINT64_C(1) << pin)));
  assert(GnssPinsForBoard("unknown") == 0);
  Capabilities relay{false, false, false, 1, "supermini-c3", "relay-ntrip"};
  std::string error, json;
  Settings original = DefaultSettings(base), out;
  assert(ValidateSettings(original, base, error));
  assert(EncodeSettings(original, json, true));
  assert(DecodeSettings(json, original, base, out, error));
  assert(!out.local_input && !out.gnss_enabled && out.revision == 2);
  assert(!DecodeSettings(R"({"gnss":{"enabled":true}})", original, base, out, error));
  assert(DecodeSettings(R"({"input":"local","gnss":{"enabled":true,"tx_pin":17,"rx_pin":18}})",
                        original,
                        base,
                        out,
                        error));
  assert(out.gnss_enabled && out.gnss_tx_pin == 17 && out.gnss_rx_pin == 18);
  assert(!DecodeSettings(R"({"input":"local","gnss":{"enabled":true,"tx_pin":17,"rx_pin":37}})",
                         original,
                         base,
                         out,
                         error));
  assert(!DecodeSettings(R"({"gnss":{"tx_pin":17,"rx_pin":17}})", original, base, out, error));
  auto configured = original;
  configured.upstream.password = "upstream-test-secret";
  configured.wifi_password = "wifi-test-secret";
  configured.ap_password = "ap-test-secret";
  configured.caster_password = "caster-test-secret";
  configured.caster_user = "tester";
  configured.admin_token_sha256 = std::string(64, 'a');
  assert(ValidateSettings(configured, base, error));
  assert(EncodeSettings(configured, json));
  assert(json.find("test-secret") == std::string::npos);
  assert(json.find(std::string(64, 'a')) == std::string::npos);
  assert(DecodeSettings(json, configured, base, out, error));
  assert(out.upstream.password == configured.upstream.password);
  assert(!DecodeSettings(json, original, base, out, error));  // Redaction cannot restore secrets.
  assert(EncodeSettings(configured, json, true));
  assert(DecodeSettings(json, original, base, out, error));
  assert(out.admin_token_sha256 == configured.admin_token_sha256);
  assert(out.upstream.password == configured.upstream.password);
  assert(DecodeSettings(
      R"({"name":"unit","upstream":{"endpoint":{"port":2102}}})", configured, base, out, error));
  assert(out.device_name == "unit" && out.upstream.password == configured.upstream.password);
  assert(out.upstream.port == 2102 && configured.upstream.port != 2102);
  for (const std::string bad :
       {"",
        "[]",
        "null",
        "{}x",
        R"({"schema":2})",
        R"({"schema":1.5})",
        R"({"name":"one","name":"two"})",
        R"({"gnss":{"baud":115200,"baud":9600}})",
        R"({"new":1})",
        R"({"gnss":{"new":true}})",
        R"({"gnss":{"baud":9600.5}})",
        R"({"gnss":{"baud":1e200}})",
        R"({"name":"\u0000bad"})",
        R"({"name":"bad\nline"})",
        R"({"upstream":{"endpoint":{"host":"evil\r\nheader"}}})",
        R"({"upstream":{"enabled":true}})",
        R"({"upstream":{"endpoint":{"port":0}}})",
        R"({"upstream":{"endpoint":{"ca_file":"/spiffs/certs/../secret"}}})",
        R"({"upstream":{"endpoint":{"client_cert_file":"/spiffs/certs/client.pem"}}})",
        R"({"publishers":[{"slot":0},{"slot":0}]})",
        R"({"publishers":[{"slot":5}]})",
        R"({"publishers":[{"enabled":true}]})",
        R"({"gnss":{"enabled":"yes"}})",
        R"({"admin_configured":"true"})",
        R"({"admin_token_sha256":"abcd"})",
        R"({"radio":{"channel":0}})",
        R"({"radio":{"channel":12}})",
        R"({"radio":{"peer":"ff:ff:ff:ff:ff:ff"}})",
        R"({"radio":{"peer":"00:00:00:00:00:00"}})",
        R"({"wifi":{"sta":true,"ssid":"test"}})",
        R"({"wifi":{"ap":true,"ap_password":"short"}})",
        R"({"lora":{"enabled":true}})",
        R"({"lora":{"sf":13}})",
        R"({"input":"both"})"})
  {
    out.device_name = "untouched";
    assert(!DecodeSettings(bad, configured, base, out, error));
    assert(out.device_name == "untouched" && !error.empty());
  }
  assert(!DecodeSettings(std::string(8193, ' '), original, base, out, error));
  assert(!DecodeSettings(std::string(20, '[') + std::string(20, ']'), original, base, out, error));
  auto minimal = DefaultSettings(relay);
  assert(ValidateSettings(minimal, relay, error));
  assert(!DecodeSettings(R"({"gnss":{"enabled":true}})", minimal, relay, out, error));
  assert(!DecodeSettings(R"({"input":"local"})", minimal, relay, out, error));
  assert(!DecodeSettings(R"({"caster":{"enabled":true}})", minimal, relay, out, error));
  assert(DecodeSettings(
      R"({"upstream":{"enabled":true,"endpoint":{"host":"caster.test","password":"sample"}}})",
      minimal,
      relay,
      out,
      error));
  assert(!out.gnss_enabled && out.upstream_enabled);
  assert(DecodeSettings(R"({"radio":{"enabled":false},"wifi":{"sta":true,"ssid":"test"}})",
                        minimal,
                        relay,
                        out,
                        error));
  assert(DecodeSettings(R"({"radio":{"peer":"02:00:00:00:00:02"}})", minimal, relay, out, error));
  minimal.revision = UINT64_MAX;
  assert(!DecodeSettings("{}", minimal, relay, out, error));
  // Deterministic malformed-input corpus under sanitizers; no partial output mutation.
  std::uint32_t seed = 0x12345678;
  for (unsigned i = 0; i < 10000; ++i)
  {
    std::string bytes(i % 257, ' ');
    for (auto& b : bytes)
    {
      seed = 1664525 * seed + 1013904223;
      b = static_cast<char>(seed >> 24);
    }
    (void)DecodeSettings(bytes, original, base, out, error);
  }
  std::cout << "settings roundtrip, credentials, role bounds and 10000 malformed inputs passed\n";
  auto digest = [](std::string_view in, std::array<std::uint8_t, 32>& out) {
    if (in != std::string(64, 'z'))
      return false;
    out.fill(0xab);
    return true;
  };
  std::string expected;
  for (unsigned i = 0; i < 32; ++i)
    expected += "ab";
  assert(AuthorizedBearer("Bearer " + std::string(64, 'z'), expected, digest));
  assert(AuthorizedBearer("bEaReR " + std::string(64, 'z'), expected, digest));
  assert(!AuthorizedBearer("Bearer " + std::string(64, 'z'), std::string(64, 'a'), digest));
  assert(!AuthorizedBearer("Bearer " + std::string(64, 'z'), "", digest));
  assert(!AuthorizedBearer("Bearer short", expected, digest));
  assert(!AuthorizedBearer("Bearer " + std::string(64, 'z') + "\r\n", expected, digest));
  assert(!AuthorizedBearer("Basic " + std::string(64, 'z'), expected, digest));
  assert(!AuthorizedBearer("Bearer " + std::string(129, 'z'), expected, digest));
  assert(!ParseBoundedObject(R"({"x":1,"x":2})", error));
}
