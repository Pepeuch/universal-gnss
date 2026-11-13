// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_settings.hpp"
#include "cJSON.h"
#include "ug_json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
namespace ug::embedded
{
namespace
{
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
bool Text(const std::string& s, std::size_t limit, bool spaces = true)
{
  return s.size() <= limit && std::none_of(s.begin(), s.end(), [&](unsigned char c) {
           return c < 32 || c == 127 || (!spaces && c == ' ');
         });
}
bool Endpoint(const universal_gnss_ntrip::NtripConfig& c, bool enabled)
{
  if ((enabled && (c.host.empty() || !c.port)) || !Text(c.host, 253, false) ||
      !Text(c.mountpoint, 128, false) || !Text(c.username, 128) || !Text(c.password, 128) ||
      c.username.find(':') != std::string::npos || !c.tls_verify_peer || c.gga_interval_s == 0 ||
      c.gga_interval_s > 3600)
    return false;
  for (const auto* path :
       {&c.tls_ca_file, &c.tls_client_certificate_file, &c.tls_client_private_key_file})
    if (!path->empty() && (!Text(*path, 128, false) || path->find("/spiffs/certs/") != 0 ||
                           path->find("..") != std::string::npos))
      return false;
  return c.tls_client_certificate_file.empty() == c.tls_client_private_key_file.empty();
}
bool JsonBudget(std::string_view s)
{
  if (s.empty() || s.size() > 8192 || s.find('\0') != std::string_view::npos ||
      s.find("\\u0000") != std::string_view::npos)
    return false;
  bool quoted = false, escaped = false;
  unsigned depth = 0, nodes = 1;
  for (unsigned char c : s)
  {
    if (quoted)
    {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[')
    {
      if (++depth > 12 || ++nodes > 512)
        return false;
    }
    else if (c == '}' || c == ']')
    {
      if (!depth)
        return false;
      --depth;
    }
    else if (c == ',' || c == ':')
    {
      if (++nodes > 512)
        return false;
    }
  }
  return !quoted && !depth;
}
bool Unique(const cJSON* n)
{
  if (cJSON_IsObject(n))
    for (auto* a = n->child; a; a = a->next)
      for (auto* b = a->next; b; b = b->next)
        if (a->string && b->string && std::strcmp(a->string, b->string) == 0)
          return false;
  for (auto* child = n->child; child; child = child->next)
    if (!Unique(child))
      return false;
  return true;
}
struct Reader
{
  const cJSON* root;
  std::string& error;
  bool ok{true};
  bool Keys(std::initializer_list<const char*> keys)
  {
    if (!cJSON_IsObject(root))
      return ok = false;
    for (auto* c = root->child; c; c = c->next)
      if (!c->string || std::none_of(keys.begin(), keys.end(), [&](const char* k) {
            return std::strcmp(c->string, k) == 0;
          }))
        return ok = false;
    return true;
  }
  const cJSON* Get(const char* key)
  {
    return cJSON_GetObjectItemCaseSensitive(root, key);
  }
  void String(const char* key, std::string& value, std::size_t limit)
  {
    auto* item = Get(key);
    if (!item)
      return;
    if (!cJSON_IsString(item) || !item->valuestring || std::strlen(item->valuestring) > limit)
    {
      ok = false;
      return;
    }
    value = item->valuestring;
  }
  void Bool(const char* key, bool& value)
  {
    auto* n = Get(key);
    if (n)
    {
      if (!cJSON_IsBool(n))
        ok = false;
      else
        value = cJSON_IsTrue(n);
    }
  }
  template <class T>
  void Integer(const char* key, T& value, double lo, double hi)
  {
    auto* n = Get(key);
    if (!n)
      return;
    if (!cJSON_IsNumber(n) || !std::isfinite(n->valuedouble) || n->valuedouble < lo ||
        n->valuedouble > hi || std::floor(n->valuedouble) != n->valuedouble)
    {
      ok = false;
      return;
    }
    value = static_cast<T>(n->valuedouble);
  }
};
bool ReadEndpoint(const cJSON* root, universal_gnss_ntrip::NtripConfig& out, std::string& error)
{
  Reader r{root, error};
  if (!r.Keys({"host",
               "port",
               "mountpoint",
               "username",
               "password",
               "password_set",
               "tls",
               "ca_file",
               "client_cert_file",
               "client_key_file",
               "send_gga",
               "gga_interval_s"}))
    return false;
  r.String("host", out.host, 253);
  r.Integer("port", out.port, 1, 65535);
  r.String("mountpoint", out.mountpoint, 128);
  r.String("username", out.username, 128);
  r.String("password", out.password, 128);
  bool had_secret = false;
  r.Bool("password_set", had_secret);
  if (had_secret && !r.Get("password") && out.password.empty())
    return false;
  r.Bool("tls", out.tls_enabled);
  r.String("ca_file", out.tls_ca_file, 128);
  r.String("client_cert_file", out.tls_client_certificate_file, 128);
  r.String("client_key_file", out.tls_client_private_key_file, 128);
  r.Bool("send_gga", out.send_gga);
  r.Integer("gga_interval_s", out.gga_interval_s, 1, 3600);
  return r.ok;
}
struct Builder
{
  bool ok{true};
  cJSON* Object(cJSON* parent, const char* key)
  {
    auto* n = parent ? cJSON_AddObjectToObject(parent, key) : nullptr;
    ok = ok && n;
    return n;
  }
  void String(cJSON* n, const char* key, const std::string& value)
  {
    if (!n || !cJSON_AddStringToObject(n, key, value.c_str()))
      ok = false;
  }
  void Bool(cJSON* n, const char* key, bool value)
  {
    if (!n || !cJSON_AddBoolToObject(n, key, value))
      ok = false;
  }
  void Number(cJSON* n, const char* key, double value)
  {
    if (!n || !cJSON_AddNumberToObject(n, key, value))
      ok = false;
  }
  void Endpoint(cJSON* n, const universal_gnss_ntrip::NtripConfig& c, bool secrets)
  {
    String(n, "host", c.host);
    Number(n, "port", c.port);
    String(n, "mountpoint", c.mountpoint);
    String(n, "username", c.username);
    if (secrets)
      String(n, "password", c.password);
    else
      Bool(n, "password_set", !c.password.empty());
    Bool(n, "tls", c.tls_enabled);
    String(n, "ca_file", c.tls_ca_file);
    String(n, "client_cert_file", c.tls_client_certificate_file);
    String(n, "client_key_file", c.tls_client_private_key_file);
    Bool(n, "send_gga", c.send_gga);
    Number(n, "gga_interval_s", c.gga_interval_s);
  }
};
}  // namespace
JsonDocument ParseBoundedObject(std::string_view json, std::string& error)
{
  error = "invalid, duplicate or over-budget JSON document";
  if (!JsonBudget(json))
    return JsonDocument(nullptr, cJSON_Delete);
  const char* end = nullptr;
  JsonDocument root(cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), cJSON_Delete);
  if (!root || !end || !cJSON_IsObject(root.get()) || !Unique(root.get()))
    return JsonDocument(nullptr, cJSON_Delete);
  while (end < json.data() + json.size() &&
         (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
    ++end;
  if (end != json.data() + json.size())
    return JsonDocument(nullptr, cJSON_Delete);
  error.clear();
  return root;
}
bool OnlyKeys(const cJSON* node, std::initializer_list<const char*> keys)
{
  std::string error;
  return Reader{node, error}.Keys(keys);
}
Settings DefaultSettings(const Capabilities& c)
{
  Settings s;
  s.gnss_enabled = c.local_gnss;
  s.local_input = c.local_gnss;
  return s;
}
bool ValidateSettings(const Settings& s, const Capabilities& c, std::string& error)
{
  auto fail = [&](const char* text) {
    error = text;
    return false;
  };
  if (!Text(s.device_name, 32, false) || s.device_name.empty() || !Text(s.ntp_server, 253, false))
    return fail("invalid device/NTP name");
  if ((s.gnss_enabled || s.local_input) && !c.local_gnss)
    return fail("profile excludes local GNSS");
  if (s.local_input && !s.gnss_enabled)
    return fail("local corrections require the configured GNSS source");
  if (s.gnss_baud < 9600 || s.gnss_baud > 921600)
    return fail("invalid GNSS baud");
  if (s.gnss_model != "auto" && s.gnss_model != "NMEA" && s.gnss_model != "UM980" &&
      s.gnss_model != "UM981" && s.gnss_model != "UM982" && s.gnss_model != "ZED-F9P")
    return fail("unsupported GNSS model selector");
  if (!Endpoint(s.upstream, s.upstream_enabled))
    return fail("invalid upstream endpoint or trust policy");
  for (std::size_t i = 0; i < s.publishers.size(); ++i)
  {
    const auto& p = s.publishers[i];
    if (!Endpoint(p.endpoint, p.enabled) ||
        (p.enabled && (i >= c.publisher_slots || p.endpoint.password.empty() ||
                       !Text(p.endpoint.password, 128, false))))
      return fail("invalid publisher slot or profile limit");
  }
  if (s.caster_enabled && (!c.caster || !s.caster_port || s.caster_mountpoint.empty()))
    return fail("local caster unavailable in this profile");
  if (!Text(s.caster_mountpoint, 128, false) || !Text(s.caster_user, 128) ||
      !Text(s.caster_password, 128) || s.caster_user.empty() != s.caster_password.empty())
    return fail("invalid caster credentials");
  if (s.radio_channel < 1 || s.radio_channel > 11 || !Text(s.radio_peer, 17, false))
    return fail("invalid fixed radio settings");
  if (!s.radio_peer.empty())
  {
    unsigned mac[6];
    int end = 0;
    if (std::sscanf(s.radio_peer.c_str(),
                    "%2x:%2x:%2x:%2x:%2x:%2x%n",
                    &mac[0],
                    &mac[1],
                    &mac[2],
                    &mac[3],
                    &mac[4],
                    &mac[5],
                    &end) != 6 ||
        end != 17 || (mac[0] & 1) || (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0)
      return fail("radio peer must be a unicast MAC");
  }
  if (!Text(s.wifi_ssid, 32) || !Text(s.wifi_password, 63) || !Text(s.ap_ssid, 32) ||
      !Text(s.ap_password, 63))
    return fail("invalid Wi-Fi field");
  if ((s.wifi_sta_enabled &&
       (s.wifi_ssid.empty() || (!s.wifi_password.empty() && s.wifi_password.size() < 8))) ||
      (s.wifi_ap_enabled && (s.ap_ssid.empty() || s.ap_password.size() < 12)))
    return fail("invalid Wi-Fi credentials");
  // Fixed ESP-NOW channel is not silently replaced by an access point's channel.
  if (s.wifi_sta_enabled && s.radio_enabled)
    return fail("STA association and fixed-channel ESP-NOW require separate qualified profiles");
  if (!s.admin_token_sha256.empty() &&
      (s.admin_token_sha256.size() != 64 ||
       !std::all_of(s.admin_token_sha256.begin(), s.admin_token_sha256.end(), [](unsigned char b) {
         return (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f');
       })))
    return fail("invalid administrator token hash");
  if (s.lora_enabled && !c.lora)
    return fail("LoRa not available in selected profile");
  if (s.lora_sf < 5 || s.lora_sf > 12 || s.lora_coding_rate < 5 || s.lora_coding_rate > 8 ||
      s.lora_power_dbm < -9 || s.lora_power_dbm > 22 ||
      (s.lora_bandwidth_hz != 125000 && s.lora_bandwidth_hz != 250000 &&
       s.lora_bandwidth_hz != 500000))
    return fail("invalid LoRa modem parameters");
  error.clear();
  return true;
}
bool DecodeSettings(std::string_view json,
                    const Settings& prior,
                    const Capabilities& capabilities,
                    Settings& result,
                    std::string& error)
{
  error = "invalid, duplicate, unknown or over-budget configuration field";
  auto root = ParseBoundedObject(json, error);
  if (!root)
    return false;
  error = "invalid or unknown configuration field";
  Settings s = prior;
  Reader r{root.get(), error};
  if (!r.Keys({"schema",
               "name",
               "input",
               "gnss",
               "upstream",
               "publishers",
               "caster",
               "wifi",
               "radio",
               "admin_token_sha256",
               "admin_configured",
               "ntp_server",
               "lora"}))
    return false;
  unsigned schema = Settings::kSchema;
  r.Integer("schema", schema, Settings::kSchema, Settings::kSchema);
  bool admin_configured = false;
  r.Bool("admin_configured", admin_configured);
  if (admin_configured && !r.Get("admin_token_sha256") && s.admin_token_sha256.empty())
    return false;
  r.String("name", s.device_name, 32);
  r.String("ntp_server", s.ntp_server, 253);
  r.String("admin_token_sha256", s.admin_token_sha256, 64);
  if (auto* input = r.Get("input"))
  {
    if (!cJSON_IsString(input) || !input->valuestring)
      return false;
    std::string mode = input->valuestring;
    if (mode != "local" && mode != "ntrip")
      return false;
    s.local_input = mode == "local";
  }
  if (auto* gnss = r.Get("gnss"))
  {
    Reader g{gnss, error};
    if (!g.Keys({"enabled", "baud", "model"}))
      return false;
    g.Bool("enabled", s.gnss_enabled);
    g.Integer("baud", s.gnss_baud, 9600, 921600);
    g.String("model", s.gnss_model, 24);
    if (!g.ok)
      return false;
  }
  if (auto* upstream = r.Get("upstream"))
  {
    Reader u{upstream, error};
    if (!u.Keys({"enabled", "endpoint"}))
      return false;
    u.Bool("enabled", s.upstream_enabled);
    if (auto* endpoint = u.Get("endpoint"))
      if (!ReadEndpoint(endpoint, s.upstream, error))
        return false;
    if (!u.ok)
      return false;
  }
  if (auto* array = r.Get("publishers"))
  {
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > 5)
      return false;
    std::array<bool, 5> seen{};
    for (auto* item = array->child; item; item = item->next)
    {
      Reader p{item, error};
      if (!p.Keys({"slot", "enabled", "endpoint"}))
        return false;
      unsigned slot = 99;
      p.Integer("slot", slot, 0, 4);
      if (!p.ok || slot > 4 || seen[slot])
        return false;
      seen[slot] = true;
      p.Bool("enabled", s.publishers[slot].enabled);
      if (auto* e = p.Get("endpoint"))
        if (!ReadEndpoint(e, s.publishers[slot].endpoint, error))
          return false;
      if (!p.ok)
        return false;
    }
  }
  if (auto* node = r.Get("caster"))
  {
    Reader c{node, error};
    if (!c.Keys({"enabled", "port", "mountpoint", "username", "password", "password_set"}))
      return false;
    c.Bool("enabled", s.caster_enabled);
    c.Integer("port", s.caster_port, 1, 65535);
    c.String("mountpoint", s.caster_mountpoint, 128);
    c.String("username", s.caster_user, 128);
    c.String("password", s.caster_password, 128);
    bool set = false;
    c.Bool("password_set", set);
    if (set && !c.Get("password") && s.caster_password.empty())
      return false;
    if (!c.ok)
      return false;
  }
  if (auto* node = r.Get("wifi"))
  {
    Reader w{node, error};
    if (!w.Keys({"sta",
                 "ssid",
                 "password",
                 "password_set",
                 "ap",
                 "ap_ssid",
                 "ap_password",
                 "ap_password_set"}))
      return false;
    w.Bool("sta", s.wifi_sta_enabled);
    w.String("ssid", s.wifi_ssid, 32);
    w.String("password", s.wifi_password, 63);
    w.Bool("ap", s.wifi_ap_enabled);
    w.String("ap_ssid", s.ap_ssid, 32);
    w.String("ap_password", s.ap_password, 63);
    bool set = false;
    w.Bool("password_set", set);
    if (set && !w.Get("password") && s.wifi_password.empty())
      return false;
    set = false;
    w.Bool("ap_password_set", set);
    if (set && !w.Get("ap_password") && s.ap_password.empty())
      return false;
    if (!w.ok)
      return false;
  }
  if (auto* node = r.Get("radio"))
  {
    Reader a{node, error};
    if (!a.Keys({"enabled", "channel", "peer"}))
      return false;
    a.Bool("enabled", s.radio_enabled);
    a.Integer("channel", s.radio_channel, 1, 11);
    a.String("peer", s.radio_peer, 17);
    if (!a.ok)
      return false;
  }
  if (auto* node = r.Get("lora"))
  {
    Reader a{node, error};
    if (!a.Keys({"enabled",
                 "region",
                 "frequency_hz",
                 "sf",
                 "bandwidth_hz",
                 "coding_rate",
                 "power_dbm"}))
      return false;
    a.Bool("enabled", s.lora_enabled);
    a.String("region", s.lora_region, 16);
    a.Integer("frequency_hz", s.lora_frequency_hz, 100000000, 1000000000);
    a.Integer("sf", s.lora_sf, 5, 12);
    a.Integer("bandwidth_hz", s.lora_bandwidth_hz, 125000, 500000);
    a.Integer("coding_rate", s.lora_coding_rate, 5, 8);
    a.Integer("power_dbm", s.lora_power_dbm, -9, 22);
    if (!a.ok)
      return false;
  }
  if (!r.ok || !ValidateSettings(s, capabilities, error))
    return false;
  if (prior.revision == UINT64_MAX)
  {
    error = "configuration generation exhausted";
    return false;
  }
  s.revision = prior.revision + 1;
  result = std::move(s);
  return true;
}
bool EncodeSettings(const Settings& s, std::string& output, const bool secrets)
{
  Json root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return false;
  Builder b;
  auto* n = root.get();
  b.Number(n, "schema", Settings::kSchema);
  b.String(n, "name", s.device_name);
  b.String(n, "input", s.local_input ? "local" : "ntrip");
  b.String(n, "ntp_server", s.ntp_server);
  if (secrets)
    b.String(n, "admin_token_sha256", s.admin_token_sha256);
  else
    b.Bool(n, "admin_configured", !s.admin_token_sha256.empty());
  auto* g = b.Object(n, "gnss");
  b.Bool(g, "enabled", s.gnss_enabled);
  b.Number(g, "baud", s.gnss_baud);
  b.String(g, "model", s.gnss_model);
  auto* u = b.Object(n, "upstream");
  b.Bool(u, "enabled", s.upstream_enabled);
  b.Endpoint(b.Object(u, "endpoint"), s.upstream, secrets);
  auto* arr = cJSON_AddArrayToObject(n, "publishers");
  if (!arr)
    return false;
  for (std::size_t i = 0; i < s.publishers.size(); ++i)
  {
    auto* p = cJSON_CreateObject();
    if (!p)
      return false;
    if (!cJSON_AddItemToArray(arr, p))
    {
      cJSON_Delete(p);
      return false;
    }
    b.Number(p, "slot", i);
    b.Bool(p, "enabled", s.publishers[i].enabled);
    b.Endpoint(b.Object(p, "endpoint"), s.publishers[i].endpoint, secrets);
  }
  auto* c = b.Object(n, "caster");
  b.Bool(c, "enabled", s.caster_enabled);
  b.Number(c, "port", s.caster_port);
  b.String(c, "mountpoint", s.caster_mountpoint);
  b.String(c, "username", s.caster_user);
  if (secrets)
    b.String(c, "password", s.caster_password);
  else
    b.Bool(c, "password_set", !s.caster_password.empty());
  auto* w = b.Object(n, "wifi");
  b.Bool(w, "sta", s.wifi_sta_enabled);
  b.String(w, "ssid", s.wifi_ssid);
  b.Bool(w, "ap", s.wifi_ap_enabled);
  b.String(w, "ap_ssid", s.ap_ssid);
  if (secrets)
  {
    b.String(w, "password", s.wifi_password);
    b.String(w, "ap_password", s.ap_password);
  }
  else
  {
    b.Bool(w, "password_set", !s.wifi_password.empty());
    b.Bool(w, "ap_password_set", !s.ap_password.empty());
  }
  auto* a = b.Object(n, "radio");
  b.Bool(a, "enabled", s.radio_enabled);
  b.Number(a, "channel", s.radio_channel);
  b.String(a, "peer", s.radio_peer);
  auto* l = b.Object(n, "lora");
  b.Bool(l, "enabled", s.lora_enabled);
  b.String(l, "region", s.lora_region);
  b.Number(l, "frequency_hz", s.lora_frequency_hz);
  b.Number(l, "sf", s.lora_sf);
  b.Number(l, "bandwidth_hz", s.lora_bandwidth_hz);
  b.Number(l, "coding_rate", s.lora_coding_rate);
  b.Number(l, "power_dbm", s.lora_power_dbm);
  if (!b.ok)
    return false;
  char* text = cJSON_PrintUnformatted(n);
  if (!text)
    return false;
  const auto size = std::strlen(text);
  bool ok = size <= 8192;
  if (ok)
    output.assign(text, size);
  cJSON_free(text);
  return ok;
}
}  // namespace ug::embedded
