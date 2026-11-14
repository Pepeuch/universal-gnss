// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_board.hpp"
#include "ug_portable.h"
#include "ug_portable_settings.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace ug::embedded;
int main(int argc, char** argv)
{
  if (argc == 2)
  {
    ug_portable_t p;
    char out[513];
    if (!ug_portable_parse(argv[1], strlen(argv[1]), &p) || !ug_portable_json(&p, out, sizeof(out)))
      return 1;
    std::cout << out;
    return 0;
  }
  for (auto board : {UG_BOARD_S3, UG_BOARD_C3})
  {
    auto p = ug_portable_default(board);
    char json[513];
    assert(ug_portable_json(&p, json, sizeof(json)));
    ug_portable_t decoded{};
    assert(ug_portable_parse(json, strlen(json), &decoded));
    uint8_t record[UG_PORTABLE_RECORD_SIZE];
    assert(ug_portable_record(&decoded, record));
    assert(ug_portable_restore(record, sizeof(record), &decoded));
    for (unsigned i = 0; i < sizeof(record); ++i)
    {
      record[i] ^= 1;
      assert(!ug_portable_restore(record, sizeof(record), &decoded));
      record[i] ^= 1;
    }
    auto with_secret = std::string(json);
    with_secret.insert(1, "\"password\":\"private\",");
    assert(!ug_portable_parse(with_secret.data(), with_secret.size(), &decoded));
    with_secret = std::string(json);
    with_secret.insert(1, "\"schema\":1,");
    assert(!ug_portable_parse(with_secret.data(), with_secret.size(), &decoded));
    auto original = decoded;
    assert(!ug_portable_parse("{}", 2, &decoded));
    assert(!memcmp(&original, &decoded, sizeof(decoded)));
  }
  Capabilities caps{true, true, false, 5, "waveshare-s3-eth", "gateway"};
  caps.gnss_pin_mask = GnssPinsForBoard(caps.board);
  auto before = DefaultSettings(caps);
  before.radio_peer = "02:00:00:00:00:02";
  before.radio_key = "0123456789abcdef0123456789abcdef";
  before.radio_enabled = true;
  before.device_name = "PERSONAL_NAME";
  before.wifi_ssid = "PERSONAL_SSID";
  before.upstream.host = "PERSONAL_HOST";
  before.upstream.password = "PERSONAL_PASSWORD";
  before.admin_token_sha256 = std::string(64, 'a');
  std::string json, error;
  assert(ExportPortableSettings(before, caps, json, error));
  assert(json.find("PERSONAL") == std::string::npos &&
         json.find(before.radio_key) == std::string::npos);
  Settings after;
  assert(ImportPortableSettings(json, before, caps, after, error));
  assert(after.radio_key == before.radio_key && after.radio_peer == before.radio_peer &&
         after.wifi_ssid == before.wifi_ssid);
  assert(after.admin_token_sha256 == before.admin_token_sha256 && !after.gnss_enabled &&
         !after.local_input);
  auto p = ug_portable_default(UG_BOARD_S3);
  p.tx = 17;
  p.rx = 16;
  char out[513];
  assert(ug_portable_json(&p, out, sizeof(out)));
  assert(ImportPortableSettings(out, before, caps, after, error));
  assert(after.gnss_tx_pin == 17 && after.gnss_rx_pin == 16);
  p.channel = 2;
  assert(ug_portable_json(&p, out, sizeof(out)));
  assert(!ImportPortableSettings(out, before, caps, after, error));
  for (int pin = 0; pin <= 48; ++pin)
  {
    p.tx = pin;
    assert(ug_portable_valid(&p) == (ug_portable_pin(UG_BOARD_S3, pin) && pin != 16));
  }
  std::cout << "portable schema, privacy, CRC, board conflicts and atomic preview PASS\n";
}
