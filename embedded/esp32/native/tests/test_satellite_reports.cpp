// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include <cassert>
#include <cstdio>
using namespace ug::embedded;
using namespace universal_gnss_protocols;
static NmeaGsvRecord Part(unsigned index, unsigned total = 2, unsigned visible = 2)
{
  NmeaGsvRecord p;
  p.timestamp_ns = index;
  p.message_index = index;
  p.total_messages = total;
  p.satellites_in_view = visible;
  p.satellite_count = 1;
  p.satellites[0].prn = index;
  p.satellites[0].cn0_db_hz = 45.5f;
  return p;
}
static std::string Sentence(const std::string& payload)
{
  char checksum[8];
  std::snprintf(checksum, sizeof(checksum), "*%02X\r\n", ComputeNmeaChecksum(payload));
  return "$" + payload + checksum;
}
int main()
{
  SatelliteReports reports;
  reports.Observe(Part(1), "GP", 1);
  assert(!reports.Latest(1).receipt_ns);
  // Signal and talker scopes can interleave; their fragments never join.
  reports.Observe(Part(1), "GP", 2);
  reports.Observe(Part(2), "GP", 1);
  auto out = reports.Latest(2);
  assert(out.signal_id == 1 && out.count == 2 && out.receipt_ns == 1 &&
         out.entries[0].cn0 == 45.5f);
  assert(!(out.entries[0].flags & SatelliteEntry::kUsedKnown));
  reports.Observe(Part(2), "GP", 2);
  assert(reports.Latest(2).signal_id == 2);
  reports.Reset();
  reports.Observe(Part(2), "GP");
  assert(!reports.Latest(2).receipt_ns);
  reports.Observe(Part(1), "GP");
  auto late = Part(2);
  late.timestamp_ns = 3000000002LL;
  reports.Observe(late, "GP");
  assert(!reports.Latest(3000000002LL).receipt_ns);
  reports.Observe(Part(1), "GP");
  auto changed = Part(2);
  changed.satellites_in_view = 3;
  reports.Observe(changed, "GP");
  assert(!reports.Latest(2).receipt_ns);
  // Incomplete/reordered cycles cannot qualify a new complete view.
  reports.Observe(Part(1, 3, 3), "GP");
  reports.Observe(Part(3, 3, 3), "GP");
  assert(!reports.Latest(3).receipt_ns);
  // Capacity is explicit: retain 64 entries, never silently claim 128 listed.
  for (unsigned i = 1; i <= 32; ++i)
  {
    auto p = Part(i, 32, 128);
    p.satellite_count = 4;
    for (unsigned j = 0; j < 4; ++j)
      p.satellites[j].prn = (i - 1) * 4 + j + 1;
    reports.Observe(p, "GN");
  }
  out = reports.Latest(32);
  assert(out.count == 64 && out.truncated && out.reported_total == 128);
  UbxNavSatRecord ubx;
  ubx.timestamp_ns = 100;
  ubx.i_tow_ms = 10;
  ubx.num_svs = 1;
  ubx.satellite_count = 1;
  ubx.satellites[0].sv_id = 7;
  ubx.satellites[0].gnss_id = 2;
  ubx.satellites[0].used_in_navigation = true;
  reports.Observe(ubx);
  assert(reports.Latest(100).protocol == SatelliteProtocol::kUbxNavSat);
  ubx.timestamp_ns = 1000000000LL;
  reports.Observe(ubx);
  assert(reports.Latest(1000000000LL).receipt_ns == 100);
  UnicoreSatsInfoRecord unicore;
  unicore.header.timestamp_ns = 200;
  unicore.header.time_reference = UnicoreTimeReference::kGps;
  unicore.header.time_status = UnicoreTimeStatus::kFine;
  unicore.header.gps_week = 2000;
  unicore.header.gps_millis_of_week = 10;
  unicore.parsed_satellite_count = 1;
  unicore.satellites[0].satellite_id = 8;
  reports.Observe(unicore);
  ubx.timestamp_ns = 2000000000LL;
  reports.Observe(ubx);  // Other protocol didn't erase its epoch cache.
  out = reports.Latest(2000000000LL);
  assert(out.receipt_ns == 200 && out.protocol == SatelliteProtocol::kUnicoreSatsInfo);
  assert(!SatelliteReportFresh(out, 3000000201LL));
  assert(!SatelliteReportFresh(out, 199));
  unicore.header.time_reference = UnicoreTimeReference::kBds;
  unicore.header.timestamp_ns = 201;
  reports.Observe(unicore);
  assert(reports.Latest(201).receipt_ns == 201);
  reports.Reset();
  assert(!reports.Latest(0).receipt_ns);
  reports.Observe(Part(1, 1, 1), "<>");
  assert(!reports.Latest(1).receipt_ns);
  // Real UG framing/parsing, including bad CRC rejection, through native owner.
  universal_gnss_transport::MemoryByteSink sink;
  Receiver receiver(sink);
  const auto first = Sentence("GPGSV,2,1,02,01,45,180,45,1");
  const auto second = Sentence("GPGSV,2,2,02,02,30,090,40,1");
  assert(receiver.Feed(reinterpret_cast<const std::uint8_t*>(first.data()), first.size(), 1));
  assert(!receiver.satellites(1).receipt_ns);
  auto bad = second;
  bad[10] ^= 1;
  assert(receiver.Feed(reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size(), 2));
  assert(!receiver.satellites(2).receipt_ns);
  assert(receiver.Feed(reinterpret_cast<const std::uint8_t*>(second.data()), second.size(), 3));
  assert(receiver.satellites(3).count == 2 && receiver.satellites(3).signal_id == 1);
  assert(!receiver.state().fix_valid);  // Sky view never fabricates a position/fix.
  NmeaSentence scope;
  scope.sentence_type = "GSV";
  scope.checksum_status = ChecksumStatus::kValid;
  std::optional<std::uint8_t> signal;
  scope.payload_text = "GPGSV,1,1,01,01,45,180,45,B";
  assert(GsvSignalScope(scope, signal) && signal == 11);
  scope.payload_text = "GPGSV,1,1,01,01,45,180,45,XYZ";
  assert(!GsvSignalScope(scope, signal));
  receiver.InvalidateObservations();
  assert(!receiver.satellites(4).receipt_ns);
  std::vector<std::uint8_t> frame{0xb5, 0x62, 1, 0x35, 20, 0};
  frame.resize(26);
  frame[6] = 1;
  frame[10] = 1;
  frame[11] = 1;
  frame[14] = 0;
  frame[15] = 7;
  frame[16] = 45;
  frame[17] = 30;
  frame[18] = 90;
  frame[22] = 0x18;
  const auto ck = ComputeUbxChecksum(frame.data() + 2, frame.size() - 2);
  frame.push_back(ck.ck_a);
  frame.push_back(ck.ck_b);
  assert(receiver.Feed(frame.data(), frame.size(), 4));
  assert(receiver.satellites(4).protocol == SatelliteProtocol::kUbxNavSat &&
         receiver.satellites(4).count == 1);
  assert(receiver.satellites(4).entries[0].flags & SatelliteEntry::kUsed);
  frame.back() ^= 1;
  assert(receiver.Feed(frame.data(), frame.size(), 5));
  assert(receiver.satellites(5).receipt_ns == 4);
  receiver.InvalidateObservations();
  std::string unicore_line = "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
                             "3,2,0,0,0,63,2,302,51,0,45,0,2,0,42,9,2,"
                             "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,5,225,14,1,50,0,1";
  const auto crc = ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(unicore_line.data() + 1), unicore_line.size() - 1);
  char tail[16];
  std::snprintf(tail, sizeof(tail), "*%08X\r\n", crc);
  unicore_line += tail;
  assert(receiver.Feed(
      reinterpret_cast<const std::uint8_t*>(unicore_line.data()), unicore_line.size(), 6));
  assert(receiver.satellites(6).protocol == SatelliteProtocol::kUnicoreSatsInfo &&
         receiver.satellites(6).count == 3);
  assert(!(receiver.satellites(6).entries[0].flags & SatelliteEntry::kUsedKnown));
}
