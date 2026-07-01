#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "testdata_utils.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_tools/gnss_replay.hpp"

namespace
{

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload,
                                            const bool valid_checksum = true)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  if (!valid_checksum)
  {
    checksum ^= 0x01u;
  }

  constexpr char kHexDigits[] = "0123456789ABCDEF";
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[(checksum >> 4u) & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[checksum & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>('\r'));
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return bytes;
}

void WriteLeU2(std::vector<std::uint8_t>& payload,
               const std::size_t offset,
               const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload,
               const std::size_t offset,
               const std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

void WriteLeI4(std::vector<std::uint8_t>& payload,
               const std::size_t offset,
               const std::int32_t value)
{
  WriteLeU4(payload, offset, static_cast<std::uint32_t>(value));
}

void WriteLeI2(std::vector<std::uint8_t>& payload,
               const std::size_t offset,
               const std::int16_t value)
{
  WriteLeU2(payload, offset, static_cast<std::uint16_t>(value));
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload,
                                        const bool valid_checksum = true)
{
  std::vector<std::uint8_t> bytes = {
      0xB5u,
      0x62u,
      class_id,
      message_id,
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(valid_checksum ? checksum.ck_a
                                 : static_cast<std::uint8_t>(checksum.ck_a ^ 0x01u));
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type,
                                         const bool valid_crc = true)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {
      0xD3u,
      0x00u,
      static_cast<std::uint8_t>(payload.size()),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc = universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  if (!valid_crc)
  {
    crc ^= 0x01u;
  }

  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void WriteLittleEndian32(std::vector<std::uint8_t>& bytes,
                         const std::size_t offset,
                         const std::uint32_t value)
{
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLittleEndianFloat32(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const float value)
{
  std::uint32_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  WriteLittleEndian32(bytes, offset, raw);
}

void WriteLittleEndianFloat64(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const double value)
{
  std::uint64_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((raw >> 24) & 0xFFu);
  bytes[offset + 4u] = static_cast<std::uint8_t>((raw >> 32) & 0xFFu);
  bytes[offset + 5u] = static_cast<std::uint8_t>((raw >> 40) & 0xFFu);
  bytes[offset + 6u] = static_cast<std::uint8_t>((raw >> 48) & 0xFFu);
  bytes[offset + 7u] = static_cast<std::uint8_t>((raw >> 56) & 0xFFu);
}

std::vector<std::uint8_t> BuildUnicoreBinaryFrame(const std::uint16_t message_id,
                                                  const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame = {0xAAu, 0x44u, 0xB5u, 97u};
  AppendLittleEndian16(frame, message_id);
  AppendLittleEndian16(frame, static_cast<std::uint16_t>(payload.size()));
  frame.push_back(0u);
  frame.push_back(1u);
  AppendLittleEndian16(frame, 2190u);
  AppendLittleEndian32(frame, 364536000u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(13u);
  AppendLittleEndian16(frame, 0u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);
  return frame;
}

std::vector<std::uint8_t> MakeNavPvtPayload()
{
  std::vector<std::uint8_t> payload(92u, 0u);

  WriteLeU4(payload, 0u, 345000u);
  WriteLeU2(payload, 4u, 2025u);
  payload[6u] = 5u;
  payload[7u] = 28u;
  payload[8u] = 12u;
  payload[9u] = 34u;
  payload[10u] = 56u;
  payload[11u] = 0x07u;
  WriteLeI4(payload, 16u, 123456789);

  payload[20u] = 3u;
  payload[21u] = 0x01u;
  payload[23u] = 18u;

  WriteLeI4(payload, 24u, 231234567);
  WriteLeI4(payload, 28u, 485678901);
  WriteLeI4(payload, 32u, 123450);
  WriteLeI4(payload, 36u, 120000);
  WriteLeU4(payload, 40u, 250u);
  WriteLeU4(payload, 44u, 500u);
  return payload;
}

std::vector<std::uint8_t> MakeNavSatPayload()
{
  std::vector<std::uint8_t> payload(8u + (3u * 12u), 0u);
  WriteLeU4(payload, 0u, 456000u);
  payload[4u] = 0x01u;
  payload[5u] = 3u;

  payload[8u] = 0u;
  payload[9u] = 4u;
  payload[10u] = 45u;
  payload[11u] = static_cast<std::uint8_t>(30);
  WriteLeI2(payload, 12u, 120);
  WriteLeI2(payload, 14u, 0);
  WriteLeU4(payload, 16u, 0x0000001Cu);

  payload[20u] = 2u;
  payload[21u] = 12u;
  payload[22u] = 38u;
  payload[23u] = static_cast<std::uint8_t>(15);
  WriteLeI2(payload, 24u, 220);
  WriteLeI2(payload, 26u, 0);
  WriteLeU4(payload, 28u, 0x00000004u);

  payload[32u] = 0u;
  payload[33u] = 18u;
  payload[34u] = 0u;
  payload[35u] = static_cast<std::uint8_t>(5);
  WriteLeI2(payload, 36u, 300);
  WriteLeI2(payload, 38u, 0);
  WriteLeU4(payload, 40u, 0x0000002Cu);

  return payload;
}

std::vector<std::uint8_t> MakeNavDopPayload()
{
  std::vector<std::uint8_t> payload(18u, 0u);
  WriteLeU4(payload, 0u, 654321u);
  WriteLeU2(payload, 4u, 145u);
  WriteLeU2(payload, 6u, 123u);
  WriteLeU2(payload, 8u, 99u);
  WriteLeU2(payload, 10u, 87u);
  WriteLeU2(payload, 12u, 65u);
  WriteLeU2(payload, 14u, 111u);
  WriteLeU2(payload, 16u, 109u);
  return payload;
}

std::string NormalizeUnicoreAsciiLine(std::string line)
{
  if (line.empty() || line.front() != '#')
  {
    return line;
  }

  while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
  {
    line.pop_back();
  }

  if (const std::size_t star = line.rfind('*'); star != std::string::npos)
  {
    line.resize(star);
  }

  const auto crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(line.data() + 1u), line.size() - 1u);

  std::ostringstream stream;
  stream << line << '*' << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << crc
         << "\r\n";
  return stream.str();
}

std::vector<std::uint8_t> BuildUnicoreLine(const std::string& line)
{
  const std::string normalized = NormalizeUnicoreAsciiLine(line);
  return std::vector<std::uint8_t>(normalized.begin(), normalized.end());
}

std::vector<std::uint8_t> MakeBestNavBinaryPayload()
{
  std::vector<std::uint8_t> payload(120u, 0u);
  WriteLittleEndian32(payload, 0u, 0u);
  WriteLittleEndian32(payload, 4u, 34u);
  WriteLittleEndianFloat64(payload, 8u, 40.0789588272);
  WriteLittleEndianFloat64(payload, 16u, 116.2365102982);
  WriteLittleEndianFloat64(payload, 24u, 65.8312);
  WriteLittleEndianFloat32(payload, 40u, 1.2221f);
  WriteLittleEndianFloat32(payload, 44u, 1.1053f);
  WriteLittleEndianFloat32(payload, 48u, 2.1970f);
  WriteLittleEndianFloat32(payload, 56u, 0.4f);
  payload[64u] = 50u;
  payload[65u] = 28u;
  return payload;
}

std::vector<std::uint8_t> MakePvtslnBinaryPayload()
{
  std::vector<std::uint8_t> payload(224u, 0u);
  WriteLittleEndian32(payload, 0u, 50u);
  WriteLittleEndianFloat32(payload, 4u, 60.5060f);
  WriteLittleEndianFloat64(payload, 8u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 16u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 24u, 0.2000f);
  WriteLittleEndianFloat32(payload, 28u, 0.1500f);
  WriteLittleEndianFloat32(payload, 32u, 0.1800f);
  WriteLittleEndianFloat32(payload, 36u, 0.9000f);
  payload[68u] = 46u;
  payload[69u] = 28u;
  WriteLittleEndian32(payload, 96u, 0u);
  WriteLittleEndianFloat32(payload, 104u, 182.2500f);
  WriteLittleEndianFloat32(payload, 124u, 0.6840f);
  return payload;
}

std::vector<std::uint8_t> BuildUnicoreBinaryReplayStream()
{
  std::vector<std::uint8_t> stream;
  Append(stream, BuildUnicoreBinaryFrame(2118u, MakeBestNavBinaryPayload()));
  Append(stream, BuildUnicoreBinaryFrame(1021u, MakePvtslnBinaryPayload()));
  Append(stream,
         BuildUnicoreLine("#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
                          "3,2,0,0,0,63,"
                          "2,302,51,0,45,0,2,0,42,9,2,"
                          "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
                          "5,225,14,1,50,0,1*abcdef12\r\n"));
  Append(stream,
         BuildUnicoreLine(
             "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0*e31418ea\r\n"));
  return stream;
}

std::vector<std::uint8_t> BuildMixedReplayStream()
{
  std::vector<std::uint8_t> stream = {0x00u, 0x13u, 0xFFu};

  Append(stream,
         BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  Append(stream, BuildNmeaSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"));
  Append(stream,
         BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"));

  Append(stream, BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()));
  Append(stream, BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload()));

  Append(stream,
         BuildUnicoreLine(
             "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
             "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
             "1.1053,"
             "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
             "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));
  Append(stream,
         BuildUnicoreLine("#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
                          "3,2,0,0,0,63,"
                          "2,302,51,0,45,0,2,0,42,9,2,"
                          "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
                          "5,225,14,1,50,0,1*abcdef12\r\n"));

  Append(stream, BuildRtcmFrame(1005u));
  Append(stream, BuildRtcmFrame(1077u));
  Append(stream, BuildUbxFrame(0x01u, 0x03u, std::vector<std::uint8_t>(16u, 0u), false));

  const auto truncated = BuildRtcmFrame(1230u);
  stream.insert(stream.end(), truncated.begin(), truncated.begin() + 4);
  return stream;
}

void TestReplayNmeaGstEnrichesAccuracy(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  Append(bytes, BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 2u && result.summary.runtime_updates == 2u &&
                 result.summary.counts_by_protocol.at("nmea") == 2u &&
                 result.summary.counts_by_nmea_sentence_type.at("GGA") == 1u &&
                 result.summary.counts_by_nmea_sentence_type.at("GST") == 1u,
             "replay should recognize both GGA and GST as NMEA runtime updates");
  ctx.Expect(final_state.fix_valid && final_state.fix_type == universal_gnss::GnssFixType::kFix &&
                 final_state.latitude_deg.has_value() && *final_state.latitude_deg == 48.1173 &&
                 final_state.longitude_deg.has_value() &&
                 std::fabs(*final_state.longitude_deg - 11.5166667) < 1e-6 &&
                 final_state.altitude_m.has_value() &&
                 std::fabs(*final_state.altitude_m - 545.4) < 1e-6,
             "GST replay should preserve position and fix from GGA");
  ctx.Expect(final_state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 final_state.vertical_accuracy_m == std::optional<float>(1.1f),
             "GST replay should enrich the final state with conservative accuracy values");

  const std::string text = universal_gnss_tools::FormatGnssReplayText(result);
  ctx.Expect(text.find("id=GPGST") != std::string::npos &&
                 text.find("h_acc=0.60") != std::string::npos &&
                 text.find("v_acc=1.10") != std::string::npos,
             "replay text output should surface GST accuracy updates");
}

void TestReplayNmeaGstOnlyDoesNotInventFixOrPosition(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 1u && result.summary.runtime_updates == 1u,
             "GST-only replay should still report one recognized runtime update");
  ctx.Expect(!final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kUnknown &&
                 !final_state.latitude_deg.has_value() && !final_state.longitude_deg.has_value(),
             "GST-only replay should not invent fix or position");
  ctx.Expect(final_state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 final_state.vertical_accuracy_m == std::optional<float>(1.1f),
             "GST-only replay should still preserve accuracy information");
}

void TestReplayUbxNavDopEnrichesDopOnly(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()));
  Append(bytes, BuildUbxFrame(0x01u, 0x04u, MakeNavDopPayload()));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 2u && result.summary.runtime_updates == 2u &&
                 result.summary.counts_by_protocol.at("ubx") == 2u &&
                 result.summary.counts_by_ubx_message.at("01:04") == 1u,
             "replay should recognize NAV-DOP as a UBX runtime update");
  ctx.Expect(final_state.fix_valid && final_state.fix_type == universal_gnss::GnssFixType::kFix &&
                 final_state.latitude_deg.has_value() &&
                 std::fabs(*final_state.latitude_deg - 48.5678901) < 1e-6 &&
                 final_state.longitude_deg.has_value() &&
                 std::fabs(*final_state.longitude_deg - 23.1234567) < 1e-6,
             "NAV-DOP replay should preserve existing fix and position from NAV-PVT");
  ctx.Expect(final_state.hdop.has_value() && std::fabs(*final_state.hdop - 0.65f) < 1e-6f &&
                 final_state.vdop.has_value() && std::fabs(*final_state.vdop - 0.87f) < 1e-6f,
             "NAV-DOP replay should enrich the final state with receiver-native DOP");
}

void TestReplayUnicoreJammingEnrichesRfStateOnly(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes,
         BuildUnicoreLine(
             "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
             "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
             "1.1053,"
             "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
             "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));
  Append(bytes,
         BuildUnicoreLine(
             "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0*e31418ea\r\n"));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 2u && result.summary.runtime_updates == 2u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u,
             "replay should recognize BESTNAVA and JAMSTATUSA as Unicore runtime updates");
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.latitude_deg == std::optional<double>(40.0789588272) &&
                 final_state.longitude_deg == std::optional<double>(116.2365102982),
             "Unicore jamming replay should preserve the existing BESTNAVA position state");
  ctx.Expect(final_state.interference_detected == std::optional<bool>(true) &&
                 final_state.jamming_detected == std::optional<bool>(true),
             "JAMSTATUSA replay should enrich the final state with interference/jamming only");
}

void TestReplayEmbeddedUnicoreTextResyncRecoversBestNav(TestContext& ctx)
{
  const std::string corrupted_prefix = "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;TRUNCATED";
  std::vector<std::uint8_t> bytes(corrupted_prefix.begin(), corrupted_prefix.end());
  Append(bytes,
         BuildUnicoreLine(
             "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
             "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
             "1.1053,"
             "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
             "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 1u && result.summary.runtime_updates == 1u &&
                 result.summary.malformed_events == 1u &&
                 result.summary.noise_bytes == corrupted_prefix.size(),
             "replay should resynchronize past a truncated embedded Unicore prefix");
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.latitude_deg == std::optional<double>(40.0789588272) &&
                 final_state.longitude_deg == std::optional<double>(116.2365102982),
             "replay should preserve the recovered BESTNAVA runtime state after embedded resync");
}

void TestReplayUnicoreBestSatEnrichesSatelliteCountsOnly(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes,
         BuildUnicoreLine(
             "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
             "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
             "1.1053,"
             "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
             "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));
  Append(bytes,
         BuildUnicoreLine("#BESTSATA,79,GPS,FINE,2203,226245800,0,0,18,22;"
                          "4,GPS,2,GOOD,00000013,GLONASS,2-4,GOOD,00000010,GALILEO,5,GOOD,00000001,"
                          "BEIDOU,20,GOOD,00000000*12345678\r\n"));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 2u && result.summary.runtime_updates == 2u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u &&
                 result.summary.counts_by_unicore_message.at("BESTSATA") == 1u,
             "replay should recognize BESTSATA as a Unicore runtime update");
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.latitude_deg == std::optional<double>(40.0789588272) &&
                 final_state.longitude_deg == std::optional<double>(116.2365102982),
             "BESTSATA replay should preserve existing BESTNAVA fix and position state");
  ctx.Expect(final_state.satellites_tracked == std::optional<std::uint16_t>(4u) &&
                 final_state.satellites_used == std::optional<std::uint16_t>(2u) &&
                 !final_state.satellites_visible.has_value() &&
                 !final_state.mean_cn0_db_hz.has_value() && !final_state.max_cn0_db_hz.has_value(),
             "BESTSATA replay should enrich only tracked and used counts, not visibility or CN0");
}

void TestReplayUnicoreBinaryAndAsciiRouting(TestContext& ctx)
{
  const auto bytes = BuildUnicoreBinaryReplayStream();
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.recognized_records == 4u && result.summary.runtime_updates == 4u &&
                 result.summary.counts_by_protocol.at("unicore") == 4u &&
                 result.summary.counts_by_unicore_message.at("BESTNAVB") == 1u &&
                 result.summary.counts_by_unicore_message.at("PVTSLNB") == 1u &&
                 result.summary.counts_by_unicore_message.at("SATSINFOA") == 1u &&
                 result.summary.counts_by_unicore_message.at("JAMSTATUSA") == 1u,
             "replay should route both Unicore binary and ASCII runtime updates");
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFixed &&
                 final_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                             universal_gnss::GnssRtkMode::kFixed) &&
                 final_state.latitude_deg.has_value() &&
                 std::fabs(*final_state.latitude_deg - 40.07898130522) < 1e-6 &&
                 final_state.longitude_deg.has_value() &&
                 std::fabs(*final_state.longitude_deg - 116.23663134427) < 1e-6 &&
                 final_state.altitude_m.has_value() &&
                 std::fabs(*final_state.altitude_m - 60.5060) < 1e-4,
             "binary replay should preserve the routed Unicore position and RTK state");
  ctx.Expect(
      final_state.horizontal_accuracy_m.has_value() &&
          std::fabs(*final_state.horizontal_accuracy_m - 0.18f) < 1e-6f &&
          final_state.vertical_accuracy_m.has_value() &&
          std::fabs(*final_state.vertical_accuracy_m - 0.2f) < 1e-6f &&
          final_state.hdop.has_value() && std::fabs(*final_state.hdop - 0.684f) < 1e-6f &&
          final_state.satellites_used == std::optional<std::uint16_t>(28u) &&
          final_state.satellites_tracked == std::optional<std::uint16_t>(3u) &&
          final_state.mean_cn0_db_hz == std::optional<float>(46.0f) &&
          final_state.max_cn0_db_hz == std::optional<float>(50.0f) &&
          final_state.correction_age_s == std::optional<float>(0.9f) &&
          final_state.heading_deg.has_value() &&
          std::fabs(*final_state.heading_deg - 182.25) < 1e-6 &&
          final_state.interference_detected == std::optional<bool>(true) &&
          final_state.jamming_detected == std::optional<bool>(true),
      "mixed Unicore replay should expose accuracy, DOP, satellites, CN0, heading, and RF state");
}

void TestReplayMergesMixedRuntimeState(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "replay should report the total bytes read");
  ctx.Expect(result.summary.recognized_records == 10u,
             "replay should recognize all supported frames and sentences");
  ctx.Expect(result.summary.runtime_updates == 7u,
             "replay should count only semantic runtime updates");
  ctx.Expect(result.summary.malformed_events == 1u && result.summary.truncated_records == 1u,
             "replay should report the trailing truncated frame");
  ctx.Expect(result.summary.noise_bytes == 3u && result.summary.noise_spans == 1u,
             "replay should preserve leading-noise statistics");

  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 3u &&
                 result.summary.counts_by_protocol.at("ubx") == 3u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 2u,
             "replay should count records by protocol");
  ctx.Expect(result.summary.counts_by_rtcm_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1077u) == 1u,
             "replay should keep RTCM message-type counts");

  ctx.Expect(result.events.size() == 10u,
             "replay should retain a per-record event timeline by default");
  ctx.Expect(result.events[0].identity == "GPGGA" && result.events[0].produced_runtime_update,
             "GGA should produce the first runtime update");
  ctx.Expect(result.events[5].identity == "BESTNAVA" && result.events[5].produced_runtime_update,
             "BESTNAVA should appear as a Unicore runtime update");
  ctx.Expect(result.events[7].identity == "1005" && !result.events[7].produced_runtime_update,
             "RTCM events should stay metadata-only for now");

  const auto& final_state = result.final_state;
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                             universal_gnss::GnssRtkMode::kFloat),
             "final state should reflect the last Unicore RTK float position update");
  ctx.Expect(final_state.latitude_deg.has_value() && *final_state.latitude_deg == 40.0789588272 &&
                 final_state.longitude_deg.has_value() &&
                 *final_state.longitude_deg == 116.2365102982 &&
                 final_state.altitude_m.has_value() && *final_state.altitude_m == 65.8312,
             "final state should preserve BESTNAVA coordinates");
  ctx.Expect(
      final_state.satellites_used == std::optional<std::uint16_t>(28u) &&
          final_state.satellites_tracked == std::optional<std::uint16_t>(3u) &&
          final_state.satellites_visible == std::optional<std::uint16_t>(3u),
      "final state should merge used, tracked, and visible satellite counts across protocols");
  ctx.Expect(final_state.mean_cn0_db_hz == std::optional<float>(46.0f) &&
                 final_state.max_cn0_db_hz == std::optional<float>(50.0f),
             "final state should preserve the last CN0 summary update");
  ctx.Expect(final_state.correction_age_s == std::optional<float>(0.4f),
             "final state should preserve BESTNAVA differential age");
}

void TestReplaySummaryOnlyAndFormatting(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  const auto summary_only_result = universal_gnss_tools::ReplayGnssBytes(bytes, false);
  ctx.Expect(summary_only_result.events.empty(),
             "replay should omit event retention when include_events is false");
  ctx.Expect(summary_only_result.final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat,
             "summary-only replay should still compute the final runtime state");

  const auto full_result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const std::string text = universal_gnss_tools::FormatGnssReplayText(full_result);
  const std::string summary = universal_gnss_tools::FormatGnssReplayText(full_result, true);
  const std::string json = universal_gnss_tools::FormatGnssReplayJson(full_result);

  ctx.Expect(text.find("proto=nmea") != std::string::npos &&
                 text.find("id=GPGGA") != std::string::npos &&
                 text.find("proto=unicore") != std::string::npos &&
                 text.find("id=BESTNAVA") != std::string::npos,
             "text output should include mixed protocol timeline entries");
  ctx.Expect(text.find("update=yes") != std::string::npos &&
                 text.find("final_state fix=rtk_float") != std::string::npos,
             "text output should include update markers and final state");
  ctx.Expect(text.find("lat=40.078958827") != std::string::npos &&
                 text.find("lon=116.236510298") != std::string::npos,
             "text output should preserve high-precision coordinate summaries");
  ctx.Expect(summary.find("runtime_updates=7") != std::string::npos &&
                 summary.find("protocols nmea=3 rtcm3=2 ubx=3 unicore=2") != std::string::npos &&
                 summary.find("rtcm_types 1005=1 1077=1") != std::string::npos,
             "summary output should include aggregate counters and RTCM counts");
  ctx.Expect(json.find("\"protocol\":\"unicore\"") != std::string::npos &&
                 json.find("\"identity\":\"BESTNAVA\"") != std::string::npos &&
                 json.find("\"runtime_updates\":7") != std::string::npos &&
                 json.find("\"fix_type\":\"rtk_float\"") != std::string::npos &&
                 json.find("\"latitude_deg\":40.078958827") != std::string::npos &&
                 json.find("\"longitude_deg\":116.236510298") != std::string::npos &&
                 json.find("\"counts_by_rtcm_message_type\":{\"1005\":1,\"1077\":1}") !=
                     std::string::npos,
             "JSON output should include replay events, summary counts, and final state");
}

void TestReplayCountsRtcm1230WithoutChangingRuntimeState(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildRtcmFrame(1230u));

  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);

  ctx.Expect(result.summary.recognized_records == 1u &&
                 result.summary.runtime_updates == 0u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1230u) == 1u,
             "replay should preserve RTCM 1230 as a first-class RTCM message");
  ctx.Expect(result.events.size() == 1u &&
                 result.events[0].identity == "1230" &&
                 !result.events[0].produced_runtime_update,
             "RTCM 1230 replay events should remain metadata-only");
  ctx.Expect(!result.final_state.fix_valid &&
                 result.final_state.fix_type == universal_gnss::GnssFixType::kUnknown,
             "RTCM 1230 replay should not inject direct-navigation runtime state");
}

void TestReplayStreamInput(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  std::string input_bytes(bytes.begin(), bytes.end());
  std::istringstream input(input_bytes);

  const auto result = universal_gnss_tools::ReplayGnssStream(input);
  ctx.Expect(result.summary.recognized_records == 10u && result.summary.runtime_updates == 7u &&
                 result.final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat,
             "stream replay should match byte-vector replay");
}

void TestFileBackedReplay(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("mixed/nmea_ubx_rtcm_unicore.bin");
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "file-backed replay should report the file byte size");
  ctx.Expect(result.summary.recognized_records == 10u && result.summary.runtime_updates == 7u,
             "file-backed replay should preserve the expected recognized-record and update counts");
  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 3u &&
                 result.summary.counts_by_protocol.at("ubx") == 3u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 2u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u,
             "file-backed replay should count protocols correctly");
  ctx.Expect(result.summary.counts_by_rtcm_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1077u) == 1u,
             "file-backed replay should retain RTCM type counts");

  const auto& final_state = result.final_state;
  ctx.Expect(final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                             universal_gnss::GnssRtkMode::kFloat),
             "file-backed replay should end with the expected RTK float state");
  ctx.Expect(final_state.latitude_deg == std::optional<double>(40.0789588272) &&
                 final_state.longitude_deg == std::optional<double>(116.2365102982) &&
                 final_state.altitude_m == std::optional<double>(65.8312),
             "file-backed replay should preserve the final sanitized BESTNAVA position");
  ctx.Expect(final_state.satellites_used == std::optional<std::uint16_t>(28u) &&
                 final_state.satellites_tracked == std::optional<std::uint16_t>(3u) &&
                 final_state.satellites_visible == std::optional<std::uint16_t>(3u),
             "file-backed replay should merge satellite counts across protocols");
}

void TestFileBackedBasicNmeaReplayIncludesGstAccuracy(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const auto& final_state = result.final_state;

  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 5u &&
                 result.summary.counts_by_nmea_sentence_type.at("GST") == 1u,
             "file-backed basic NMEA replay should include the synthetic GST sentence");
  ctx.Expect(final_state.fix_valid && final_state.fix_type == universal_gnss::GnssFixType::kFix &&
                 final_state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 final_state.vertical_accuracy_m == std::optional<float>(1.1f),
             "file-backed basic NMEA replay should carry GST accuracy into the final state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestReplayNmeaGstEnrichesAccuracy(ctx);
  TestReplayNmeaGstOnlyDoesNotInventFixOrPosition(ctx);
  TestReplayUbxNavDopEnrichesDopOnly(ctx);
  TestReplayUnicoreJammingEnrichesRfStateOnly(ctx);
  TestReplayEmbeddedUnicoreTextResyncRecoversBestNav(ctx);
  TestReplayUnicoreBestSatEnrichesSatelliteCountsOnly(ctx);
  TestReplayUnicoreBinaryAndAsciiRouting(ctx);
  TestReplayMergesMixedRuntimeState(ctx);
  TestReplaySummaryOnlyAndFormatting(ctx);
  TestReplayCountsRtcm1230WithoutChangingRuntimeState(ctx);
  TestReplayStreamInput(ctx);
  TestFileBackedReplay(ctx);
  TestFileBackedBasicNmeaReplayIncludesGstAccuracy(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools replay tests passed\n";
  return EXIT_SUCCESS;
}
