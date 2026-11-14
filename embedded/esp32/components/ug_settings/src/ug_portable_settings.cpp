// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_portable_settings.hpp"
#include "ug_portable.h"
#include <cstring>
namespace ug::embedded
{
bool ExportPortableSettings(const Settings& s,
                            const Capabilities& caps,
                            std::string& json,
                            std::string& error)
{
  if (caps.board != "waveshare-s3-eth" || !caps.local_gnss)
  {
    error = "portable configuration requires the supported S3 gateway or C3 rover";
    return false;
  }
  auto p = ug_portable_default(UG_BOARD_S3);
  p.tx = s.gnss_tx_pin;
  p.rx = s.gnss_rx_pin;
  p.baud = s.gnss_baud;
  p.channel = s.radio_channel;
  p.model = 0;
  while (p.model < 7 && s.gnss_model != ug_portable_model(p.model))
    ++p.model;
  char data[UG_PORTABLE_JSON_MAX + 1];
  if (!ug_portable_json(&p, data, sizeof(data)))
  {
    error = "current hardware settings are not portable";
    return false;
  }
  json = data;
  error.clear();
  return true;
}
bool ImportPortableSettings(std::string_view json,
                            const Settings& before,
                            const Capabilities& caps,
                            Settings& out,
                            std::string& error)
{
  ug_portable_t p;
  error = "invalid or incompatible portable configuration";
  if (caps.board != "waveshare-s3-eth" || !caps.local_gnss ||
      !ug_portable_parse(json.data(), json.size(), &p) || p.board != UG_BOARD_S3)
    return false;
  if ((before.radio_enabled || !before.radio_key.empty()) && p.channel != before.radio_channel)
  {
    error = "paired channel is private association state; use explicit pairing first";
    return false;
  }
  if (before.revision == UINT64_MAX)
  {
    error = "configuration revision exhausted";
    return false;
  }
  Settings next = before;
  next.gnss_tx_pin = p.tx;
  next.gnss_rx_pin = p.rx;
  next.gnss_baud = p.baud;
  next.gnss_model = ug_portable_model(p.model);
  next.radio_channel = p.channel;
  // Imported wiring is staged disabled: no receiver mutation or stale correction route.
  next.gnss_enabled = false;
  next.local_input = false;
  next.upstream.send_gga = false;
  next.revision = before.revision + 1;
  if (!ValidateSettings(next, caps, error))
    return false;
  out = std::move(next);
  error.clear();
  return true;
}
}  // namespace ug::embedded
