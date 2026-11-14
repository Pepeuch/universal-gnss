// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_satellite_reports.hpp"
#include <algorithm>
#include <charconv>
namespace ug::embedded
{
bool GsvSignalScope(const universal_gnss_protocols::NmeaSentence& sentence,
                    std::optional<std::uint8_t>& out)
{
  out.reset();
  if (sentence.sentence_type != "GSV" ||
      sentence.checksum_status != universal_gnss_protocols::ChecksumStatus::kValid)
    return false;
  const auto& payload = sentence.payload_text;
  const auto fields = 1 + std::count(payload.begin(), payload.end(), ',');
  if (fields < 4 || fields > 21)
    return false;
  const auto remainder = (fields - 4) % 4;
  if (remainder == 0)
    return true;  // Pre-4.10 shape, no signal ID.
  if (remainder != 1)
    return false;
  const auto last = std::string_view(payload).substr(payload.rfind(',') + 1);
  if (last.empty())
    return true;  // Explicitly unknown, never guessed.
  unsigned id = 0;
  const auto parsed = std::from_chars(last.data(), last.data() + last.size(), id, 16);
  if (last.size() > 2 || parsed.ec != std::errc{} || parsed.ptr != last.data() + last.size() ||
      id > 255)
    return false;
  out = static_cast<std::uint8_t>(id);
  return true;
}
void SatelliteReports::PublishVendor(SatelliteReport&& next)
{
  auto& vendor_ = vendors_[next.protocol == SatelliteProtocol::kUbxNavSat ? 0 : 1];
  if (next.epoch_known && vendor_.epoch_known && vendor_.protocol == next.protocol &&
      vendor_.tow_ms == next.tow_ms && vendor_.week == next.week &&
      vendor_.time_reference == next.time_reference)
    next.receipt_ns = vendor_.receipt_ns;
  vendor_ = std::move(next);
}
void SatelliteReports::Observe(const universal_gnss_protocols::UbxNavSatRecord& record)
{
  if (!record.timestamp_ns || *record.timestamp_ns < 0 ||
      record.satellite_count > SatelliteReport::kCapacity)
    return;
  SatelliteReport next;
  next.protocol = SatelliteProtocol::kUbxNavSat;
  next.receipt_ns = record.timestamp_ns;
  next.tow_ms = record.i_tow_ms;
  next.epoch_known = record.i_tow_ms < 604800000u;
  next.reported_total = record.num_svs;
  for (std::size_t i = 0; i < record.satellite_count; ++i)
  {
    const auto& value = record.satellites[i];
    auto& out = next.entries[next.count++];
    out.id = value.sv_id;
    out.raw_system_id = value.gnss_id;
    out.elevation = value.elevation_deg;
    out.azimuth = value.azimuth_deg;
    out.cn0 = value.cno_db_hz;
    out.flags = SatelliteEntry::kElevation | SatelliteEntry::kAzimuth | SatelliteEntry::kCn0 |
                SatelliteEntry::kUsedKnown;
    if (value.used_in_navigation)
      out.flags |= SatelliteEntry::kUsed;
    if (value.healthy)
    {
      out.flags |= SatelliteEntry::kHealthKnown;
      if (*value.healthy)
        out.flags |= SatelliteEntry::kHealthy;
    }
  }
  PublishVendor(std::move(next));
}
void SatelliteReports::Observe(const universal_gnss_protocols::UnicoreSatsInfoRecord& record)
{
  if (!record.header.timestamp_ns || *record.header.timestamp_ns < 0 ||
      record.parsed_satellite_count > SatelliteReport::kCapacity)
    return;
  SatelliteReport next;
  next.protocol = SatelliteProtocol::kUnicoreSatsInfo;
  next.receipt_ns = record.header.timestamp_ns;
  next.tow_ms = record.header.gps_millis_of_week;
  next.week = record.header.gps_week;
  next.time_reference = static_cast<std::uint8_t>(record.header.time_reference);
  next.epoch_known =
      record.header.time_status == universal_gnss_protocols::UnicoreTimeStatus::kFine &&
      record.header.time_reference != universal_gnss_protocols::UnicoreTimeReference::kUnknown &&
      record.header.gps_millis_of_week < 604800000u;
  next.reported_total = record.tracked_satellite_count;
  for (std::size_t i = 0; i < record.parsed_satellite_count; ++i)
  {
    const auto& value = record.satellites[i];
    auto& out = next.entries[next.count++];
    out.id = value.satellite_id;
    out.raw_system_id = value.system_id;
    out.elevation = value.elevation_deg;
    out.azimuth = value.azimuth_deg;
    out.cn0 = value.cn0_db_hz;
    out.frequency_status = value.frequency_status;
    out.frequency_count = value.frequency_count;
    out.flags = SatelliteEntry::kElevation | SatelliteEntry::kAzimuth | SatelliteEntry::kCn0;
    // Frequency presence is not proof that a satellite is used in navigation.
  }
  PublishVendor(std::move(next));
}
void SatelliteReports::Observe(const universal_gnss_protocols::NmeaGsvRecord& record,
                               std::string_view talker,
                               std::optional<std::uint8_t> signal_id)
{
  if (!record.timestamp_ns || *record.timestamp_ns < 0 || talker.size() != 2 ||
      record.message_index == 0 || record.message_index > record.total_messages ||
      record.total_messages > 32 || record.satellite_count > 4)
    return;
  for (const auto c : talker)
    if (c < 'A' || c > 'Z')
      return;
  auto same = [&](const Cycle& c) {
    return c.partial.talker[0] == talker[0] && c.partial.talker[1] == talker[1] &&
           c.partial.signal_id == signal_id;
  };
  auto cycle = std::find_if(cycles_.begin(), cycles_.end(), same);
  if (record.message_index == 1)
  {
    if (cycle == cycles_.end())
      cycle = std::find_if(cycles_.begin(), cycles_.end(), [&](const Cycle& c) {
        return !c.active || !SatelliteReportFresh(c.partial, *record.timestamp_ns);
      });
    if (cycle == cycles_.end())
      return;  // All eight bounded talker cycles are busy.
    *cycle = {};
    cycle->active = true;
    cycle->total = record.total_messages;
    cycle->expected = 1;
    cycle->partial.protocol = SatelliteProtocol::kNmeaGsv;
    cycle->partial.talker = {talker[0], talker[1], 0};
    cycle->partial.signal_id = signal_id;
    cycle->partial.receipt_ns = record.timestamp_ns;
    cycle->partial.reported_total = record.satellites_in_view;
  }
  if (cycle == cycles_.end() || !cycle->active)
    return;
  if (!SatelliteReportFresh(cycle->partial, *record.timestamp_ns) ||
      cycle->total != record.total_messages || cycle->expected != record.message_index ||
      cycle->partial.reported_total != record.satellites_in_view)
  {
    cycle->active = false;
    return;
  }
  for (std::size_t i = 0; i < record.satellite_count; ++i)
  {
    const auto& value = record.satellites[i];
    if (!value.prn)
      continue;
    for (std::size_t j = 0; j < cycle->partial.count; ++j)
      if (cycle->partial.entries[j].id == *value.prn)
      {
        cycle->active = false;
        return;
      }
    if (cycle->partial.count == SatelliteReport::kCapacity)
    {
      cycle->partial.truncated = true;
      continue;
    }
    auto& out = cycle->partial.entries[cycle->partial.count++];
    out.id = *value.prn;
    if (value.elevation_deg)
    {
      out.elevation = *value.elevation_deg;
      out.flags |= SatelliteEntry::kElevation;
    }
    if (value.azimuth_deg)
    {
      out.azimuth = *value.azimuth_deg;
      out.flags |= SatelliteEntry::kAzimuth;
    }
    if (value.cn0_db_hz)
    {
      out.cn0 = *value.cn0_db_hz;
      out.flags |= SatelliteEntry::kCn0;
    }
  }
  ++cycle->expected;
  if (record.message_index == cycle->total)
  {
    nmea_ = cycle->partial;
    cycle->active = false;
  }
}
const SatelliteReport& SatelliteReports::Latest(std::int64_t now) const
{
  // Prefer a fresh native vendor report; never merge protocol ID namespaces,
  // per-signal GSV cycles or unrelated measurement clocks.
  const SatelliteReport* fresh = nullptr;
  const SatelliteReport* latest = &nmea_;
  for (const auto& report : vendors_)
  {
    if (report.receipt_ns && (!latest->receipt_ns || *report.receipt_ns > *latest->receipt_ns))
      latest = &report;
    if (SatelliteReportFresh(report, now) && (!fresh || *report.receipt_ns > *fresh->receipt_ns))
      fresh = &report;
  }
  if (fresh)
    return *fresh;
  if (SatelliteReportFresh(nmea_, now))
    return nmea_;
  return *latest;
}

void SatelliteReports::Reset()
{
  for (auto& report : vendors_)
    report = {};
  nmea_ = {};
  for (auto& cycle : cycles_)
    cycle = {};
}
}  // namespace ug::embedded
