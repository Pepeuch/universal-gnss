#include "universal_gnss_driver/nmea_session.hpp"

#include <string_view>
#include <utility>

#include "universal_gnss_protocols/nmea_parser.hpp"
#include "universal_gnss_protocols/parser_status.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::ParserStatus;

template <typename ParseFn, typename MapFn>
void ParseAndMergeSentence(const NmeaSentence& sentence,
                           ParseFn&& parse_fn,
                           MapFn&& map_fn,
                           const bool enable_runtime_updates,
                           universal_gnss::GnssRuntimeAggregator& aggregator,
                           NmeaSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(sentence);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return;
  }

  ++metrics.records_parsed;
  if (enable_runtime_updates && aggregator.Merge(std::forward<MapFn>(map_fn)(*parsed.record)))
  {
    ++metrics.runtime_updates;
  }
}

template <typename ParseFn>
void ParseSemanticOnlySentence(const NmeaSentence& sentence,
                               ParseFn&& parse_fn,
                               NmeaSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(sentence);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return;
  }

  ++metrics.records_parsed;
  ++metrics.semantic_only_records;
}

bool IsSupportedSentenceType(const NmeaSentence& sentence)
{
  return universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "RMC") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSA") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSV") ||
         universal_gnss_protocols::IsNmeaGst(sentence) ||
         universal_gnss_protocols::IsNmeaVtg(sentence) ||
         universal_gnss_protocols::IsNmeaZda(sentence);
}

}  // namespace

NmeaSession::NmeaSession(NmeaSessionConfig config)
    : config_(config), framer_(config.max_sentence_length_bytes)
{
}

void NmeaSession::FeedBytes(const std::uint8_t* data,
                            const std::size_t size,
                            const std::optional<std::int64_t> timestamp_ns)
{
  if (data == nullptr || size == 0u)
  {
    return;
  }

  metrics_.bytes_seen += size;
  for (std::size_t index = 0u; index < size; ++index)
  {
    HandleFramerResult(framer_.PushByte(data[index], timestamp_ns));
  }
}

void NmeaSession::FeedBytes(const std::vector<std::uint8_t>& bytes,
                            const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(bytes.data(), bytes.size(), timestamp_ns);
}

void NmeaSession::FeedString(const std::string_view text,
                             const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), timestamp_ns);
}

void NmeaSession::Finalize()
{
  HandleFramerResult(framer_.Finalize());
}

void NmeaSession::Reset()
{
  framer_.Reset();
  aggregator_.Reset();
  metrics_ = NmeaSessionMetrics{};
}

const universal_gnss::GnssRuntimeState& NmeaSession::current_state() const
{
  return aggregator_.state();
}

const NmeaSessionMetrics& NmeaSession::metrics() const
{
  return metrics_;
}

const NmeaSessionConfig& NmeaSession::config() const
{
  return config_;
}

void NmeaSession::HandleFramerResult(
    const universal_gnss_protocols::ParserResult<universal_gnss_protocols::NmeaSentence>& result)
{
  switch (result.status)
  {
    case ParserStatus::kRecordReady:
      ++metrics_.sentences_seen;
      if (!result.record.has_value() ||
          result.record->checksum_status != ChecksumStatus::kValid)
      {
        ++metrics_.malformed_sentences;
        return;
      }

      HandleSentence(*result.record);
      return;

    case ParserStatus::kOverflow:
    case ParserStatus::kTruncated:
    case ParserStatus::kInvalidData:
      ++metrics_.malformed_sentences;
      return;

    case ParserStatus::kIdle:
    case ParserStatus::kNeedMoreData:
    case ParserStatus::kSkipped:
      return;
  }
}

void NmeaSession::HandleSentence(const NmeaSentence& sentence)
{
  if (!IsSupportedSentenceType(sentence))
  {
    ++metrics_.unknown_sentences;
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA"))
  {
    ParseAndMergeSentence(sentence,
                          universal_gnss_protocols::ParseNmeaGga,
                          universal_gnss_protocols::NmeaGgaToRuntimeState,
                          config_.enable_runtime_updates,
                          aggregator_,
                          metrics_);
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "RMC"))
  {
    ParseAndMergeSentence(sentence,
                          universal_gnss_protocols::ParseNmeaRmc,
                          universal_gnss_protocols::NmeaRmcToRuntimeState,
                          config_.enable_runtime_updates,
                          aggregator_,
                          metrics_);
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSA"))
  {
    ParseAndMergeSentence(sentence,
                          universal_gnss_protocols::ParseNmeaGsa,
                          universal_gnss_protocols::NmeaGsaToRuntimeState,
                          config_.enable_runtime_updates,
                          aggregator_,
                          metrics_);
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSV"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGsv(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.records_rejected;
      return;
    }

    ++metrics_.records_parsed;
    if (config_.enable_runtime_updates)
    {
      universal_gnss::GnssRuntimeState update;
      universal_gnss_protocols::MergeNmeaGsvIntoRuntimeState(*parsed.record, update);
      if (aggregator_.Merge(update))
      {
        ++metrics_.runtime_updates;
      }
    }
    return;
  }

  if (universal_gnss_protocols::IsNmeaGst(sentence))
  {
    ParseAndMergeSentence(sentence,
                          universal_gnss_protocols::ParseNmeaGst,
                          universal_gnss_protocols::NmeaGstToRuntimeState,
                          config_.enable_runtime_updates,
                          aggregator_,
                          metrics_);
    return;
  }

  if (universal_gnss_protocols::IsNmeaVtg(sentence))
  {
    ParseSemanticOnlySentence(sentence, universal_gnss_protocols::ParseNmeaVtg, metrics_);
    return;
  }

  ParseSemanticOnlySentence(sentence, universal_gnss_protocols::ParseNmeaZda, metrics_);
}

}  // namespace universal_gnss_driver
