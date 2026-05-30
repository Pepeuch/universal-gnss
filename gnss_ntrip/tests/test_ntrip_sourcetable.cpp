#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_ntrip/ntrip_sourcetable.hpp"

namespace
{

using universal_gnss_ntrip::FilterRtcmStreams;
using universal_gnss_ntrip::FindMountpoint;
using universal_gnss_ntrip::IsRtcmStream;
using universal_gnss_ntrip::NtripSourcetableIssueCode;
using universal_gnss_ntrip::ParseNtripSourcetable;
using universal_gnss_ntrip::RequiresNmea;
using universal_gnss_ntrip::SupportsMsm;

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

void TestParseSingleStreamRecord(TestContext& ctx)
{
  const auto sourcetable = ParseNtripSourcetable(
      "STR;NEAR;Paris RTCM3;RTCM 3.2;1005(10),1077(1),1087(1),1230(10);2;GPS+GLO;FRANCE;FRA;"
      "48.8566;2.3522;1;1;BKG;none;B;N;9600\r\n"
      "ENDSOURCETABLE\r\n");

  ctx.Expect(sourcetable.has_end_marker &&
                 sourcetable.streams.size() == 1u &&
                 sourcetable.casters.empty() &&
                 sourcetable.networks.empty() &&
                 sourcetable.issues.empty(),
             "a valid single STR line should parse cleanly and honor ENDSOURCETABLE");
  if (sourcetable.streams.empty())
  {
    return;
  }

  const auto& stream = sourcetable.streams.front();
  ctx.Expect(stream.mountpoint == "NEAR" &&
                 stream.identifier == std::optional<std::string>("Paris RTCM3") &&
                 stream.format == std::optional<std::string>("RTCM 3.2") &&
                 stream.format_details ==
                     std::optional<std::string>("1005(10),1077(1),1087(1),1230(10)") &&
                 stream.carrier == std::optional<std::uint32_t>(2u) &&
                 stream.nav_system == std::optional<std::string>("GPS+GLO") &&
                 stream.network == std::optional<std::string>("FRANCE") &&
                 stream.country == std::optional<std::string>("FRA") &&
                 stream.latitude_deg == std::optional<double>(48.8566) &&
                 stream.longitude_deg == std::optional<double>(2.3522) &&
                 stream.nmea_required == std::optional<bool>(true) &&
                 stream.solution == std::optional<std::uint32_t>(1u) &&
                 stream.generator == std::optional<std::string>("BKG") &&
                 stream.compression == std::optional<std::string>("none") &&
                 stream.authentication == std::optional<std::string>("B") &&
                 stream.fee == std::optional<bool>(false) &&
                 stream.bitrate == std::optional<std::uint32_t>(9600u),
             "STR parsing should extract the common sourcetable fields with typed optionals");
  ctx.Expect(IsRtcmStream(stream) &&
                 RequiresNmea(stream) &&
                 SupportsMsm(stream),
             "RTCM STR helpers should recognize RTCM, NMEA-required, and MSM-capable streams");
}

void TestParseMultipleStreamsAndHelpers(TestContext& ctx)
{
  const auto sourcetable = ParseNtripSourcetable(
      "STR;RTCM3;Station A;RTCM 3.1;1005(10),1074(1),1084(1);2;GPS+GLO;NETA;FRA;48.0;2.0;0;1;;;;;9600\n"
      "STR;CMR;Station B;CMR+;legacy;1;GPS;NETB;USA;40.0;-74.0;0;0;;;;;4800\n"
      "ENDSOURCETABLE\n");

  const auto rtcm_streams = FilterRtcmStreams(sourcetable);
  const auto* found_plain = FindMountpoint(sourcetable, "RTCM3");
  const auto* found_slash = FindMountpoint(sourcetable, "/RTCM3");

  ctx.Expect(sourcetable.streams.size() == 2u &&
                 rtcm_streams.size() == 1u &&
                 found_plain != nullptr &&
                 found_slash != nullptr &&
                 found_plain == found_slash &&
                 found_plain->mountpoint == "RTCM3",
             "multiple STR lines should support RTCM filtering and normalized mountpoint lookup");
  ctx.Expect(!RequiresNmea(sourcetable.streams[0u]) &&
                 !SupportsMsm(sourcetable.streams[1u]) &&
                 !IsRtcmStream(sourcetable.streams[1u]),
             "helper behavior should stay false for non-NMEA or non-RTCM streams");
}

void TestParseCasterNetworkAndEndMarker(TestContext& ctx)
{
  const auto sourcetable = ParseNtripSourcetable(
      "CAS;caster.example.org;2101;PRIMARY;Example operator\n"
      "NET;FRANCE;National network\n"
      "ENDSOURCETABLE\n"
      "STR;IGNORED;After end;RTCM 3.2;;;;\n");

  ctx.Expect(sourcetable.has_end_marker &&
                 sourcetable.casters.size() == 1u &&
                 sourcetable.networks.size() == 1u &&
                 sourcetable.streams.empty(),
             "CAS and NET records should parse minimally and ENDSOURCETABLE should stop parsing");
  if (sourcetable.casters.empty() || sourcetable.networks.empty())
  {
    return;
  }

  const auto& caster = sourcetable.casters.front();
  const auto& network = sourcetable.networks.front();
  ctx.Expect(caster.host == std::optional<std::string>("caster.example.org") &&
                 caster.port == std::optional<std::uint16_t>(2101u) &&
                 caster.identifier == std::optional<std::string>("PRIMARY"),
             "CAS parsing should expose the common host/port/identifier fields");
  ctx.Expect(network.identifier == std::optional<std::string>("FRANCE"),
             "NET parsing should expose the network identifier");
}

void TestMalformedLineHandling(TestContext& ctx)
{
  const auto sourcetable = ParseNtripSourcetable(
      "BOGUS;unsupported\n"
      "STR;;Missing mountpoint;RTCM 3.2;;;;\n"
      "STR;BADLAT;Bad latitude;RTCM 3.2;;;;;;not-a-number;2.0;;;;;;\n"
      "CAS;caster.example.org;not-a-port;PRIMARY\n");

  ctx.Expect(sourcetable.streams.empty() &&
                 sourcetable.casters.empty() &&
                 sourcetable.issues.size() == 4u,
             "malformed or unknown sourcetable lines should be reported and skipped");
  if (sourcetable.issues.size() != 4u)
  {
    return;
  }

  ctx.Expect(sourcetable.issues[0u].code == NtripSourcetableIssueCode::kUnknownRecord &&
                 sourcetable.issues[1u].code == NtripSourcetableIssueCode::kMissingMountpoint &&
                 sourcetable.issues[2u].code == NtripSourcetableIssueCode::kInvalidLatitude &&
                 sourcetable.issues[3u].code == NtripSourcetableIssueCode::kInvalidCasterPort,
             "malformed-line reporting should preserve stable issue codes per failure mode");
}

void TestMsmKeywordDetection(TestContext& ctx)
{
  const auto sourcetable = ParseNtripSourcetable(
      "STR;MSMKEY;Keyword stream;RTCM 3.3;MSM7;2;GPS+GAL;NET;DEU;52.0;13.0;0;1;;;;;9600\n"
      "ENDSOURCETABLE\n");

  ctx.Expect(sourcetable.streams.size() == 1u &&
                 SupportsMsm(sourcetable.streams.front()),
             "MSM helper should also recognize sourcetable details that advertise MSM by name");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestParseSingleStreamRecord(ctx);
  TestParseMultipleStreamsAndHelpers(ctx);
  TestParseCasterNetworkAndEndMarker(ctx);
  TestMalformedLineHandling(ctx);
  TestMsmKeywordDetection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip sourcetable tests passed\n";
  return EXIT_SUCCESS;
}
