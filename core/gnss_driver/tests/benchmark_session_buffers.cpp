// SPDX-License-Identifier: LGPL-3.0-only
// Host-only benchmark: -fno-access-control reads private capacities without
// modifying production layouts or adding a diagnostics API. Not installed.
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <malloc.h>
#include <new>
#include <optional>
#include <sstream>
#include <type_traits>
#include <vector>
namespace allocation
{
std::size_t live{}, peak{}, count{}, volume{};
bool enabled{};
bool instrument = true;
void* New(std::size_t size)
{
  void* p = std::malloc(size ? size : 1);
  if (!p)
    throw std::bad_alloc();
  if (!instrument)
    return p;
  live += malloc_usable_size(p);
  if (enabled)
  {
    ++count;
    volume += size;
    peak = std::max(peak, live);
  }
  return p;
}
void Delete(void* p)
{
  if (p)
  {
    if (instrument)
      live -= malloc_usable_size(p);
    std::free(p);
  }
}
}  // namespace allocation
void* operator new(std::size_t n)
{
  return allocation::New(n);
}
void* operator new[](std::size_t n)
{
  return allocation::New(n);
}
void operator delete(void* p) noexcept
{
  allocation::Delete(p);
}
void operator delete[](void* p) noexcept
{
  allocation::Delete(p);
}
void operator delete(void* p, std::size_t) noexcept
{
  allocation::Delete(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
  allocation::Delete(p);
}
using namespace universal_gnss_driver;
namespace
{
template <class T>
void Dump(std::ostream& s, const T& v)
{
  if constexpr (std::is_enum_v<T>)
    s << static_cast<unsigned>(v);
  else
    s << +v;
}
template <class T>
void Dump(std::ostream& s, const std::optional<T>& v);
template <class K, class V>
void Dump(std::ostream& s, const std::map<K, V>& v)
{
  s << "[";
  for (auto& p : v)
  {
    Dump(s, p.first);
    s << ":";
    Dump(s, p.second);
    s << ",";
  }
  s << "]";
}
void Dump(std::ostream& s, const universal_gnss::GnssRuntimeState& v);
void Dump(std::ostream& s, const universal_gnss::GnssUtcDate& v);
void Dump(std::ostream& s, const universal_gnss::GnssUtcTime& v);
void Dump(std::ostream& s, const universal_gnss_driver::ReceiverEpoch& v);
void Dump(std::ostream& s, const universal_gnss_driver::PositionPayloadFreshnessMetrics& v);
void Dump(std::ostream& s, const universal_gnss_driver::ReceiverSessionMetrics& v);
void Dump(std::ostream& s, const universal_gnss_driver::UbloxSessionMetrics& v);
void Dump(std::ostream& s, const universal_gnss_driver::UnicoreSessionMetrics& v);
void Dump(std::ostream& s, const universal_gnss::GnssRuntimeState& v)
{
  s << "GnssRuntimeState{";
  s << "timestamp_ns=";
  Dump(s, v.timestamp_ns);
  s << ";";
  s << "fix_valid=";
  Dump(s, v.fix_valid);
  s << ";";
  s << "fix_type=";
  Dump(s, v.fix_type);
  s << ";";
  s << "rtk_mode=";
  Dump(s, v.rtk_mode);
  s << ";";
  s << "latitude_deg=";
  Dump(s, v.latitude_deg);
  s << ";";
  s << "longitude_deg=";
  Dump(s, v.longitude_deg);
  s << ";";
  s << "altitude_m=";
  Dump(s, v.altitude_m);
  s << ";";
  s << "horizontal_accuracy_m=";
  Dump(s, v.horizontal_accuracy_m);
  s << ";";
  s << "vertical_accuracy_m=";
  Dump(s, v.vertical_accuracy_m);
  s << ";";
  s << "hdop=";
  Dump(s, v.hdop);
  s << ";";
  s << "vdop=";
  Dump(s, v.vdop);
  s << ";";
  s << "satellites_used=";
  Dump(s, v.satellites_used);
  s << ";";
  s << "satellites_visible=";
  Dump(s, v.satellites_visible);
  s << ";";
  s << "satellites_tracked=";
  Dump(s, v.satellites_tracked);
  s << ";";
  s << "mean_cn0_db_hz=";
  Dump(s, v.mean_cn0_db_hz);
  s << ";";
  s << "max_cn0_db_hz=";
  Dump(s, v.max_cn0_db_hz);
  s << ";";
  s << "correction_age_s=";
  Dump(s, v.correction_age_s);
  s << ";";
  s << "utc_date=";
  Dump(s, v.utc_date);
  s << ";";
  s << "utc_time=";
  Dump(s, v.utc_time);
  s << ";";
  s << "speed_over_ground_m_s=";
  Dump(s, v.speed_over_ground_m_s);
  s << ";";
  s << "course_over_ground_deg=";
  Dump(s, v.course_over_ground_deg);
  s << ";";
  s << "heading_deg=";
  Dump(s, v.heading_deg);
  s << ";";
  s << "heading_accuracy_deg=";
  Dump(s, v.heading_accuracy_deg);
  s << ";";
  s << "differential_corrections=";
  Dump(s, v.differential_corrections);
  s << ";";
  s << "corrections_active=";
  Dump(s, v.corrections_active);
  s << ";";
  s << "dual_antenna_heading=";
  Dump(s, v.dual_antenna_heading);
  s << ";";
  s << "dual_antenna_baseline=";
  Dump(s, v.dual_antenna_baseline);
  s << ";";
  s << "baseline_azimuth_deg=";
  Dump(s, v.baseline_azimuth_deg);
  s << ";";
  s << "baseline_pitch_deg=";
  Dump(s, v.baseline_pitch_deg);
  s << ";";
  s << "baseline_length_m=";
  Dump(s, v.baseline_length_m);
  s << ";";
  s << "baseline_solution_status=";
  Dump(s, v.baseline_solution_status);
  s << ";";
  s << "interference_detected=";
  Dump(s, v.interference_detected);
  s << ";";
  s << "jamming_detected=";
  Dump(s, v.jamming_detected);
  s << ";";
  s << "capability_flags=";
  Dump(s, v.capability_flags);
  s << ";";
  s << "value_flags=";
  Dump(s, v.value_flags);
  s << ";";
  s << "clear_value_flags=";
  Dump(s, v.clear_value_flags);
  s << ";";
  s << "clear_direct_value_flags=";
  Dump(s, v.clear_direct_value_flags);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss::GnssUtcDate& v)
{
  s << "GnssUtcDate{";
  s << "year=";
  Dump(s, v.year);
  s << ";";
  s << "month=";
  Dump(s, v.month);
  s << ";";
  s << "day=";
  Dump(s, v.day);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss::GnssUtcTime& v)
{
  s << "GnssUtcTime{";
  s << "hour=";
  Dump(s, v.hour);
  s << ";";
  s << "minute=";
  Dump(s, v.minute);
  s << ";";
  s << "second=";
  Dump(s, v.second);
  s << ";";
  s << "nanosecond=";
  Dump(s, v.nanosecond);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::ReceiverEpoch& v)
{
  s << "ReceiverEpoch{";
  s << "domain=";
  Dump(s, v.domain);
  s << ";";
  s << "value=";
  Dump(s, v.value);
  s << ";";
  s << "rollover_period=";
  Dump(s, v.rollover_period);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::PositionPayloadFreshnessMetrics& v)
{
  s << "PositionPayloadFreshnessMetrics{";
  s << "valid_position_payload_observations=";
  Dump(s, v.valid_position_payload_observations);
  s << ";";
  s << "invalid_position_observations=";
  Dump(s, v.invalid_position_observations);
  s << ";";
  s << "position_payload_changes=";
  Dump(s, v.position_payload_changes);
  s << ";";
  s << "consecutive_identical_position_observations=";
  Dump(s, v.consecutive_identical_position_observations);
  s << ";";
  s << "last_position_observation_timestamp_ns=";
  Dump(s, v.last_position_observation_timestamp_ns);
  s << ";";
  s << "last_position_payload_change_timestamp_ns=";
  Dump(s, v.last_position_payload_change_timestamp_ns);
  s << ";";
  s << "unchanged_observation_span_ns=";
  Dump(s, v.unchanged_observation_span_ns);
  s << ";";
  s << "receiver_epoch_observations=";
  Dump(s, v.receiver_epoch_observations);
  s << ";";
  s << "receiver_epoch_advances=";
  Dump(s, v.receiver_epoch_advances);
  s << ";";
  s << "consecutive_identical_receiver_epochs=";
  Dump(s, v.consecutive_identical_receiver_epochs);
  s << ";";
  s << "receiver_epoch_regressions=";
  Dump(s, v.receiver_epoch_regressions);
  s << ";";
  s << "last_receiver_epoch_relation=";
  Dump(s, v.last_receiver_epoch_relation);
  s << ";";
  s << "last_receiver_epoch=";
  Dump(s, v.last_receiver_epoch);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::ReceiverSessionMetrics& v)
{
  s << "ReceiverSessionMetrics{";
  s << "bytes_seen=";
  Dump(s, v.bytes_seen);
  s << ";";
  s << "selected_session_kind=";
  Dump(s, v.selected_session_kind);
  s << ";";
  s << "selection_locked=";
  Dump(s, v.selection_locked);
  s << ";";
  s << "sessions_switched=";
  Dump(s, v.sessions_switched);
  s << ";";
  s << "runtime_observations=";
  Dump(s, v.runtime_observations);
  s << ";";
  s << "position_observations=";
  Dump(s, v.position_observations);
  s << ";";
  s << "runtime_updates=";
  Dump(s, v.runtime_updates);
  s << ";";
  s << "malformed_records=";
  Dump(s, v.malformed_records);
  s << ";";
  s << "rejected_records=";
  Dump(s, v.rejected_records);
  s << ";";
  s << "parser_anomalies=";
  Dump(s, v.parser_anomalies);
  s << ";";
  s << "unknown_records=";
  Dump(s, v.unknown_records);
  s << ";";
  s << "position_payload_freshness=";
  Dump(s, v.position_payload_freshness);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::UbloxSessionMetrics& v)
{
  s << "UbloxSessionMetrics{";
  s << "bytes_seen=";
  Dump(s, v.bytes_seen);
  s << ";";
  s << "ubx_frames_seen=";
  Dump(s, v.ubx_frames_seen);
  s << ";";
  s << "nmea_sentences_seen=";
  Dump(s, v.nmea_sentences_seen);
  s << ";";
  s << "rtcm_frames_seen=";
  Dump(s, v.rtcm_frames_seen);
  s << ";";
  s << "frames_parsed=";
  Dump(s, v.frames_parsed);
  s << ";";
  s << "frames_rejected=";
  Dump(s, v.frames_rejected);
  s << ";";
  s << "runtime_observations=";
  Dump(s, v.runtime_observations);
  s << ";";
  s << "position_observations=";
  Dump(s, v.position_observations);
  s << ";";
  s << "runtime_updates=";
  Dump(s, v.runtime_updates);
  s << ";";
  s << "unknown_frames=";
  Dump(s, v.unknown_frames);
  s << ";";
  s << "malformed_frames=";
  Dump(s, v.malformed_frames);
  s << ";";
  s << "receiver_rtcm_messages_seen=";
  Dump(s, v.receiver_rtcm_messages_seen);
  s << ";";
  s << "receiver_rtcm_messages_used=";
  Dump(s, v.receiver_rtcm_messages_used);
  s << ";";
  s << "receiver_rtcm_messages_not_used=";
  Dump(s, v.receiver_rtcm_messages_not_used);
  s << ";";
  s << "receiver_rtcm_crc_failed=";
  Dump(s, v.receiver_rtcm_crc_failed);
  s << ";";
  s << "last_receiver_rtcm_message_type=";
  Dump(s, v.last_receiver_rtcm_message_type);
  s << ";";
  s << "rtcm_message_type_counts=";
  Dump(s, v.rtcm_message_type_counts);
  s << ";";
  s << "position_payload_freshness=";
  Dump(s, v.position_payload_freshness);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::UnicoreSessionMetrics& v)
{
  s << "UnicoreSessionMetrics{";
  s << "bytes_seen=";
  Dump(s, v.bytes_seen);
  s << ";";
  s << "lines_seen=";
  Dump(s, v.lines_seen);
  s << ";";
  s << "ascii_records_seen=";
  Dump(s, v.ascii_records_seen);
  s << ";";
  s << "binary_frames_seen=";
  Dump(s, v.binary_frames_seen);
  s << ";";
  s << "records_parsed=";
  Dump(s, v.records_parsed);
  s << ";";
  s << "records_rejected=";
  Dump(s, v.records_rejected);
  s << ";";
  s << "runtime_observations=";
  Dump(s, v.runtime_observations);
  s << ";";
  s << "position_observations=";
  Dump(s, v.position_observations);
  s << ";";
  s << "runtime_updates=";
  Dump(s, v.runtime_updates);
  s << ";";
  s << "unknown_records=";
  Dump(s, v.unknown_records);
  s << ";";
  s << "malformed_lines=";
  Dump(s, v.malformed_lines);
  s << ";";
  s << "malformed_frames=";
  Dump(s, v.malformed_frames);
  s << ";";
  s << "receiver_rtcm_status_messages_seen=";
  Dump(s, v.receiver_rtcm_status_messages_seen);
  s << ";";
  s << "receiver_rtcm_status_message_count=";
  Dump(s, v.receiver_rtcm_status_message_count);
  s << ";";
  s << "receiver_last_rtcm_message_type=";
  Dump(s, v.receiver_last_rtcm_message_type);
  s << ";";
  s << "receiver_last_rtcm_base_station_id=";
  Dump(s, v.receiver_last_rtcm_base_station_id);
  s << ";";
  s << "receiver_last_rtcm_satellites_in_message=";
  Dump(s, v.receiver_last_rtcm_satellites_in_message);
  s << ";";
  s << "position_payload_freshness=";
  Dump(s, v.position_payload_freshness);
  s << ";";
  s << "}";
}
void Dump(std::ostream& s, const universal_gnss_driver::NmeaSessionMetrics& v)
{
  s << "NmeaSessionMetrics{";
  s << "bytes_seen=";
  Dump(s, v.bytes_seen);
  s << ";";
  s << "sentences_seen=";
  Dump(s, v.sentences_seen);
  s << ";";
  s << "records_parsed=";
  Dump(s, v.records_parsed);
  s << ";";
  s << "records_rejected=";
  Dump(s, v.records_rejected);
  s << ";";
  s << "runtime_observations=";
  Dump(s, v.runtime_observations);
  s << ";";
  s << "position_observations=";
  Dump(s, v.position_observations);
  s << ";";
  s << "runtime_updates=";
  Dump(s, v.runtime_updates);
  s << ";";
  s << "semantic_only_records=";
  Dump(s, v.semantic_only_records);
  s << ";";
  s << "unknown_sentences=";
  Dump(s, v.unknown_sentences);
  s << ";";
  s << "malformed_sentences=";
  Dump(s, v.malformed_sentences);
  s << ";";
  s << "position_payload_freshness=";
  Dump(s, v.position_payload_freshness);
  s << ";";
  s << "}";
}
template <class T>
void Dump(std::ostream& s, const std::optional<T>& v)
{
  if (v)
    Dump(s, *v);
  else
    s << "none";
}
struct Storage
{
  std::size_t size{}, byte_capacity{}, metadata_capacity{}, retained_bytes{};
};
template <class T>
Storage Memory(const std::vector<T>& v)
{
  return {v.size(), v.capacity(), 0, v.capacity() * sizeof(T)};
}
#if __has_include("universal_gnss_driver/detail/timestamped_byte_buffer.hpp")
Storage Memory(const universal_gnss_driver::detail::TimestampedByteBuffer& v)
{
  return {v.size(), v.byte_capacity(), v.metadata_capacity(), v.retained_bytes()};
}
#endif
Storage Memory(const UbloxSession& s)
{
  return Memory(s.buffer_);
}
Storage Memory(const UnicoreSession& s)
{
  return Memory(s.buffer_);
}
Storage Memory(const ReceiverSession& s)
{
  auto a = Memory(s.pending_auto_detect_bytes_);
  auto b = Memory(s.ublox_session_);
  auto c = Memory(s.unicore_session_);
  return {a.size + b.size + c.size,
          a.byte_capacity + b.byte_capacity + c.byte_capacity,
          a.metadata_capacity + b.metadata_capacity + c.metadata_capacity,
          a.retained_bytes + b.retained_bytes + c.retained_bytes};
}
struct Meter
{
  std::size_t start = allocation::live;
  std::size_t calls{}, bytes{}, retained{}, peak{};
  void Begin()
  {
    allocation::count = allocation::volume = 0;
    allocation::peak = allocation::live;
    allocation::enabled = true;
  }
  void End()
  {
    allocation::enabled = false;
    calls += allocation::count;
    bytes += allocation::volume;
    peak = std::max(peak, allocation::peak - start);
    retained = allocation::live - start;
  }
};
template <class S>
void Trace(std::ostream& out, const S& s)
{
  if (!allocation::instrument)
    return;
  Dump(out, s.current_state());
  Dump(out, s.metrics());
  if constexpr (std::is_same_v<S, ReceiverSession>)
  {
    Dump(out, s.ublox_metrics());
    Dump(out, s.unicore_metrics());
    Dump(out, s.nmea_metrics());
  }
  out << "\n";
}
template <class S>
void Run(const std::string& name,
         const std::vector<std::uint8_t>& input,
         std::size_t chunk,
         unsigned stamp,
         std::ostream& trace,
         bool midway = false)
{
  S s = [] {
    if constexpr (std::is_same_v<S, UbloxSession>)
    {
      UbloxSessionConfig c;
      c.max_ubx_frame_length_bytes = 8192;
      return S(c);
    }
    else if constexpr (std::is_same_v<S, UnicoreSession>)
    {
      UnicoreSessionConfig c;
      c.max_frame_length_bytes = 8192;
      return S(c);
    }
    else
      return S();
  }();
  std::vector<long long> latency;
  latency.reserve((input.size() + chunk - 1) / chunk);
  long long elapsed = 0;
  trace << name << " chunk=" << chunk << " stamp=" << stamp << "\n";
  trace.flush();
  Meter meter;
  for (std::size_t at = 0, index = 0; at < input.size(); ++index)
  {
    auto n = std::min(chunk, input.size() - at);
    std::optional<std::int64_t> timestamp =
        stamp == 0 ? std::nullopt
                   : std::optional<std::int64_t>(stamp == 1 ? 123456789LL : 1000LL + index);
    if (stamp == 3 && index % 3 == 0)
      timestamp.reset();
    meter.Begin();
    auto begin = std::chrono::steady_clock::now();
    s.FeedBytes(input.data() + at, n, timestamp);
    auto end = std::chrono::steady_clock::now();
    meter.End();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    elapsed += ns;
    latency.push_back(ns);
    Trace(trace, s);
    at += n;
    if (midway && index == 2)
    {
      s.Reset();
      Trace(trace, s);
    }
  }
  auto before = Memory(s);
  auto retained = meter.retained;
  meter.Begin();
  s.Finalize();
  meter.End();
  Trace(trace, s);
  auto final = Memory(s);
  s.Reset();
  Trace(trace, s);
  auto reset = Memory(s);
  std::sort(latency.begin(), latency.end());
  std::cout << name << "," << chunk << "," << stamp << "," << input.size() << "," << meter.calls
            << "," << meter.bytes << "," << meter.peak << "," << retained << "," << before.size
            << "," << before.byte_capacity << "," << before.metadata_capacity << ","
            << before.retained_bytes << "," << final.retained_bytes << "," << reset.retained_bytes
            << "," << elapsed << "," << latency[latency.size() / 2] << ","
            << latency[(latency.size() - 1) * 95 / 100] << "," << sizeof(S) << "\n";
}
std::vector<std::uint8_t> Read(const std::string& name)
{
  std::ifstream in(std::string(UG_BUFFER_TESTDATA) + "/" + name, std::ios::binary);
  if (!in)
    throw std::runtime_error("fixture missing");
  return {std::istreambuf_iterator<char>(in), {}};
}
std::vector<std::uint8_t> Binary()
{
  // Same PVTSLN schema as test_receiver_session.cpp, all fields deterministic.
  std::vector<std::uint8_t> p(224, 0),
      v = {0xaa, 0x44, 0xb5, 97,   0xfd, 3, 224, 0, 0, 1,  0x8e, 8,
           0xc0, 0x32, 0xba, 0x15, 18,   0, 0,   0, 0, 13, 0,    0};
  auto put = [&](std::size_t at, auto value) {
    using T = decltype(value);
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i)
      p[at + i] = bytes[i];
  };
  put(0, std::uint32_t(50));
  put(4, 60.5060f);
  put(8, 40.07898130522);
  put(16, 116.23663134427);
  put(24, .2f);
  put(28, .15f);
  put(32, .18f);
  p[68] = 46;
  p[69] = 28;
  v.insert(v.end(), p.begin(), p.end());
  auto crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(v.data(), v.size());
  for (unsigned i = 0; i < 4; ++i)
    v.push_back(crc >> (8 * i));
  return v;
}
}  // namespace
int main(int argc, char** argv)
{
  if (argc != 2 && argc != 3)
    return 2;
  if (argc == 3)
  {
    if (std::string(argv[2]) != "--timing-only")
      return 2;
    allocation::instrument = false;
  }
  std::ofstream trace(argv[1]);
  trace << std::hexfloat;
  std::cout << "scenario,chunk,stamp,input_bytes,allocations,requested_volume,actual_peak,actual_"
               "retained,pending,byte_capacity,run_capacity,buffer_retained,finalized_retained,"
               "reset_retained,processing_ns,feed_p50_ns,feed_p95_ns,session_size\n";
  for (unsigned stamp : {0u, 1u, 3u})
  {
    std::vector<std::uint8_t> ubx(4096, 0);
    ubx[0] = 0xb5;
    ubx[1] = 0x62;
    ubx[2] = 1;
    ubx[3] = 7;
    ubx[4] = 0x40;
    ubx[5] = 0x1f;
    Run<UbloxSession>("pending-ubx", ubx, 256, stamp, trace);
    std::vector<std::uint8_t> ascii(4096, 'x');
    ascii[0] = '#';
    Run<UnicoreSession>("pending-unicore", ascii, 128, stamp, trace);
    std::vector<std::uint8_t> noise(4096, 0xff);
    Run<ReceiverSession>("pending-auto", noise, 256, stamp, trace);
  }

  std::vector<std::uint8_t> pending(4097, 0);
  pending[0] = 0xb5;
  pending[1] = 0x62;
  pending[2] = 1;
  pending[3] = 7;
  pending[4] = 0x40;
  pending[5] = 0x1f;
  Run<UbloxSession>("dense-ubx", pending, 1, 3, trace);
  std::vector<std::uint8_t> dense_ascii(4097, 'x');
  dense_ascii[0] = '#';
  Run<UnicoreSession>("dense-unicore", dense_ascii, 1, 3, trace);
  std::vector<std::uint8_t> dense_auto(4097, 0xff);
  Run<ReceiverSession>("dense-auto", dense_auto, 1, 3, trace);
  std::vector<std::uint8_t> burst(262144, 0xff);
  Run<UbloxSession>("burst-ubx", burst, burst.size(), 1, trace);
  Run<UnicoreSession>("burst-unicore", burst, burst.size(), 1, trace);
  Run<ReceiverSession>("burst-auto", burst, burst.size(), 1, trace);
  for (auto file : {"nmea/basic_fix.nmea",
                    "ubx/nav_pvt_sat_monrf.ubx",
                    "unicore/basic_ascii.log",
                    "mixed/nmea_ubx_rtcm_unicore.bin",
                    "rtcm/basic_msm.rtcm"})
  {
    auto v = Read(file);
    for (auto chunk : {1u, 7u, 64u, 4096u})
      for (unsigned stamp : {0u, 1u, 3u})
      {
        Run<UbloxSession>(std::string("ubx-") + file, v, chunk, stamp, trace);
        Run<UnicoreSession>(std::string("unicore-") + file, v, chunk, stamp, trace);
        Run<ReceiverSession>(std::string("auto-") + file, v, chunk, stamp, trace);
      }
  }
  auto binary = Binary();
  for (auto chunk : {1u, 7u, 64u, 4096u})
    for (unsigned stamp : {0u, 1u, 3u})
    {
      Run<UnicoreSession>("unicore-binary", binary, chunk, stamp, trace);
      Run<ReceiverSession>("auto-binary", binary, chunk, stamp, trace);
    }
  auto mixed = Read("mixed/nmea_ubx_rtcm_unicore.bin");
  mixed.insert(mixed.begin(), {0xb5, 0x62, 0xff, 0, 0xff, 0xff, '#', 'b', 'a', 'd', '\r', '\n'});
  mixed[mixed.size() / 2] ^= 0x81;
  Run<UbloxSession>("corrupt-reset-ubx", mixed, 7, 3, trace, true);
  Run<UnicoreSession>("corrupt-reset-unicore", mixed, 7, 3, trace, true);
  Run<ReceiverSession>("corrupt-reset-auto", mixed, 7, 3, trace, true);
  std::cerr << "legacy element bytes=" << sizeof(ReceiverSession::BufferedByte) << ","
            << sizeof(UnicoreBufferedByte) << "," << sizeof(UbloxSession::BufferedByte) << "\n";
}
