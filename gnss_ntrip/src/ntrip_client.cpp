#include "universal_gnss_ntrip/ntrip_client.hpp"

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "universal_gnss_protocols/parser_status.hpp"

namespace universal_gnss_ntrip
{

namespace
{

constexpr std::size_t kMaxResponseHeaderBytes = 8192u;

bool StartsWith(const std::string_view text, const std::string_view prefix)
{
  return text.size() >= prefix.size() && text.substr(0u, prefix.size()) == prefix;
}

std::optional<std::pair<std::size_t, std::size_t>> FindResponseHeaderTerminator(
    const std::string_view response_bytes)
{
  if (const std::size_t crlfcrlf = response_bytes.find("\r\n\r\n");
      crlfcrlf != std::string_view::npos)
  {
    return std::make_pair(crlfcrlf, std::size_t{4u});
  }

  if (const std::size_t lflf = response_bytes.find("\n\n");
      lflf != std::string_view::npos)
  {
    return std::make_pair(lflf, std::size_t{2u});
  }

  return std::nullopt;
}

std::string_view ExtractStatusLine(const std::string_view header)
{
  const std::size_t crlf = header.find("\r\n");
  if (crlf != std::string_view::npos)
  {
    return header.substr(0u, crlf);
  }

  const std::size_t lf = header.find('\n');
  if (lf != std::string_view::npos)
  {
    return header.substr(0u, lf);
  }

  return header;
}

bool IsAcceptedNtripStatusLine(const std::string_view status_line)
{
  return StartsWith(status_line, "ICY 200") ||
         StartsWith(status_line, "HTTP/1.0 200") ||
         StartsWith(status_line, "HTTP/1.1 200");
}

bool IsRecognizedNtripStatusLine(const std::string_view status_line)
{
  return StartsWith(status_line, "ICY ") ||
         StartsWith(status_line, "HTTP/1.0 ") ||
         StartsWith(status_line, "HTTP/1.1 ");
}

NtripGgaSendError MapClientErrorToGgaSendError(const NtripClientError error)
{
  switch (error)
  {
    case NtripClientError::kTimeout:
      return NtripGgaSendError::kTimeout;
    case NtripClientError::kDisconnected:
      return NtripGgaSendError::kDisconnected;
    case NtripClientError::kNone:
    case NtripClientError::kConfiguration:
    case NtripClientError::kAuthentication:
    case NtripClientError::kHttp:
    case NtripClientError::kProtocol:
    case NtripClientError::kUnknown:
      return NtripGgaSendError::kWriteFailure;
  }

  return NtripGgaSendError::kWriteFailure;
}

NtripGgaSendError MapTransportErrorToGgaSendError(
    const universal_gnss_transport::TransportError error)
{
  using universal_gnss_transport::TransportError;

  switch (error)
  {
    case TransportError::kTimeout:
      return NtripGgaSendError::kTimeout;
    case TransportError::kClosed:
    case TransportError::kConnectFailure:
      return NtripGgaSendError::kDisconnected;
    case TransportError::kNone:
    case TransportError::kInvalidArgument:
    case TransportError::kReadFailure:
    case TransportError::kWriteFailure:
    case TransportError::kOverflow:
    case TransportError::kUnsupported:
    case TransportError::kUnknown:
      return NtripGgaSendError::kWriteFailure;
  }

  return NtripGgaSendError::kWriteFailure;
}

bool ShouldTrackReconnectFailure(const NtripClientState state, const NtripClientError error)
{
  if (state == NtripClientState::kDisconnected || state == NtripClientState::kFailed)
  {
    return false;
  }

  return error != NtripClientError::kNone &&
         error != NtripClientError::kConfiguration;
}

NtripClientError ParseNtripResponseStatus(const std::string_view header)
{
  const std::string_view status_line = ExtractStatusLine(header);
  if (status_line.empty())
  {
    return NtripClientError::kProtocol;
  }

  if (IsAcceptedNtripStatusLine(status_line))
  {
    return NtripClientError::kNone;
  }

  return IsRecognizedNtripStatusLine(status_line)
             ? NtripClientError::kHttp
             : NtripClientError::kProtocol;
}

NtripClientError MapTransportError(const universal_gnss_transport::TransportError error)
{
  using universal_gnss_transport::TransportError;

  switch (error)
  {
    case TransportError::kNone:
      return NtripClientError::kNone;
    case TransportError::kInvalidArgument:
      return NtripClientError::kConfiguration;
    case TransportError::kTimeout:
      return NtripClientError::kTimeout;
    case TransportError::kClosed:
    case TransportError::kConnectFailure:
    case TransportError::kReadFailure:
    case TransportError::kWriteFailure:
      return NtripClientError::kDisconnected;
    case TransportError::kOverflow:
    case TransportError::kUnsupported:
    case TransportError::kUnknown:
      return NtripClientError::kUnknown;
  }

  return NtripClientError::kUnknown;
}

}  // namespace

bool NtripGgaSendResult::sent() const
{
  return status == NtripGgaSendStatus::kSent;
}

bool NtripGgaSendResult::skipped() const
{
  return status == NtripGgaSendStatus::kSkippedDisabled ||
         status == NtripGgaSendStatus::kSkippedInterval ||
         status == NtripGgaSendStatus::kSkippedPositionRequired;
}

bool NtripGgaSendResult::ok() const
{
  return status != NtripGgaSendStatus::kError;
}

NtripClient::NtripClient(NtripConfig config)
    : config_(std::move(config)),
      gga_injection_policy_(BuildGgaInjectionPolicy(config_))
{
}

NtripClient::NtripClient(NtripConfig config,
                         universal_gnss_transport::TcpClientConfig tcp_config)
    : config_(std::move(config)),
      tcp_config_(std::move(tcp_config)),
      gga_injection_policy_(BuildGgaInjectionPolicy(config_))
{
}

void NtripClient::set_config(NtripConfig config)
{
  config_ = std::move(config);
  gga_injection_policy_ = BuildGgaInjectionPolicy(config_);
}

const NtripConfig& NtripClient::config() const
{
  return config_;
}

void NtripClient::set_tcp_config(universal_gnss_transport::TcpClientConfig config)
{
  tcp_config_ = std::move(config);
}

const universal_gnss_transport::TcpClientConfig& NtripClient::tcp_config() const
{
  return tcp_config_;
}

NtripClientError NtripClient::Connect()
{
  universal_gnss_transport::TcpClientConfig transport_config = tcp_config_;
  transport_config.host = config_.host;
  transport_config.port = config_.port;
  return ConnectWithTransport(transport_config);
}

NtripClientError NtripClient::AdoptConnectedSocket(const int fd)
{
  transport_.Close();
  ResetSessionState();
  ResetSessionMetrics();

  state_ = NtripClientState::kConnecting;
  const auto adopt_error = transport_.AdoptConnectedSocket(fd, tcp_config_);
  if (adopt_error != universal_gnss_transport::TransportError::kNone)
  {
    return FailWith(MapTransportError(adopt_error));
  }

  state_ = NtripClientState::kConnected;
  MarkConnected(metrics_);
  ClearLastError(metrics_);
  RecordReconnectSuccess(std::nullopt);
  return NtripClientError::kNone;
}

void NtripClient::Disconnect(const NtripClientError error)
{
  transport_.Close();
  state_ = NtripClientState::kDisconnected;
  MarkDisconnected(metrics_, error);
}

NtripClientError NtripClient::SendRequest()
{
  if (!transport_.IsOpen() || state_ == NtripClientState::kDisconnected)
  {
    return FailWith(NtripClientError::kDisconnected);
  }

  if (state_ == NtripClientState::kFailed)
  {
    return metrics_.last_error;
  }

  if (metrics_.request_sent)
  {
    return NtripClientError::kNone;
  }

  request_ = BuildNtripGetRequest(config_);

  std::size_t offset = 0u;
  const std::string& request_text = request_.request_text;
  while (offset < request_text.size())
  {
    const auto write_result = transport_.Write(
        reinterpret_cast<const std::uint8_t*>(request_text.data()) +
            static_cast<std::ptrdiff_t>(offset),
        request_text.size() - offset);

    if (write_result.status != universal_gnss_transport::TransportStatus::kOk)
    {
      return FailWith(MapTransportError(write_result.error));
    }

    if (write_result.bytes_written == 0u)
    {
      return FailWith(NtripClientError::kTimeout);
    }

    NoteSentBytes(metrics_, write_result.bytes_written);
    offset += write_result.bytes_written;
  }

  MarkRequestSent(metrics_);
  return NtripClientError::kNone;
}

NtripGgaSendResult NtripClient::SendGga(const universal_gnss::GnssRuntimeState& state,
                                        const universal_gnss::GnssTimestampNs now_timestamp_ns)
{
  if (!transport_.IsOpen() || state_ == NtripClientState::kDisconnected)
  {
    const NtripClientError client_error =
        FailWith(NtripClientError::kDisconnected, now_timestamp_ns);
    return MakeGgaSendErrorResult(NtripGgaSendError::kDisconnected, client_error);
  }

  if (state_ == NtripClientState::kFailed)
  {
    return MakeGgaSendErrorResult(MapClientErrorToGgaSendError(metrics_.last_error),
                                  metrics_.last_error);
  }

  const auto generated = GenerateGgaFromRuntimeState(state);
  if (!generated.ok())
  {
    return MakeGgaSendErrorResult(NtripGgaSendError::kGenerationFailed,
                                  NtripClientError::kNone,
                                  generated.error);
  }

  std::size_t offset = 0u;
  const std::string& sentence = generated.sentence;
  while (offset < sentence.size())
  {
    const auto write_result = transport_.Write(
        reinterpret_cast<const std::uint8_t*>(sentence.data()) +
            static_cast<std::ptrdiff_t>(offset),
        sentence.size() - offset);

    if (write_result.status != universal_gnss_transport::TransportStatus::kOk)
    {
      const NtripClientError client_error =
          FailWith(MapTransportError(write_result.error), now_timestamp_ns);
      return MakeGgaSendErrorResult(MapTransportErrorToGgaSendError(write_result.error),
                                    client_error);
    }

    if (write_result.bytes_written == 0u)
    {
      const NtripClientError client_error = FailWith(NtripClientError::kTimeout, now_timestamp_ns);
      return MakeGgaSendErrorResult(NtripGgaSendError::kTimeout, client_error);
    }

    NoteSentBytes(metrics_, write_result.bytes_written);
    offset += write_result.bytes_written;
  }

  NoteGgaSent(metrics_, now_timestamp_ns);
  MarkGgaInjected(gga_injection_policy_, now_timestamp_ns);
  return NtripGgaSendResult{
      NtripGgaSendStatus::kSent,
      NtripClientError::kNone,
      std::nullopt,
      std::nullopt};
}

NtripGgaSendResult NtripClient::MaybeSendGga(const universal_gnss::GnssRuntimeState& state,
                                             const universal_gnss::GnssTimestampNs now_timestamp_ns)
{
  if (!gga_injection_policy_.enabled)
  {
    return NtripGgaSendResult{
        NtripGgaSendStatus::kSkippedDisabled,
        NtripClientError::kNone,
        std::nullopt,
        std::nullopt};
  }

  if (gga_injection_policy_.source_position_requirement ==
          GgaSourcePositionRequirement::kRequirePositionFix &&
      !state.fix_valid)
  {
    return NtripGgaSendResult{
        NtripGgaSendStatus::kSkippedPositionRequired,
        NtripClientError::kNone,
        std::nullopt,
        std::nullopt};
  }

  if (!ShouldInjectGga(gga_injection_policy_, state.fix_valid, now_timestamp_ns))
  {
    return NtripGgaSendResult{
        NtripGgaSendStatus::kSkippedInterval,
        NtripClientError::kNone,
        std::nullopt,
        std::nullopt};
  }

  return SendGga(state, now_timestamp_ns);
}

NtripClientReadResult NtripClient::Read(
    std::uint8_t* destination,
    const std::size_t capacity,
    const std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns)
{
  NtripClientReadResult result;

  if (capacity == 0u)
  {
    return result;
  }

  if (destination == nullptr)
  {
    result.transport_status = universal_gnss_transport::TransportStatus::kError;
    result.transport_error = universal_gnss_transport::TransportError::kInvalidArgument;
    result.client_error = NtripClientError::kConfiguration;
    return result;
  }

  if (!transport_.IsOpen() || state_ == NtripClientState::kDisconnected)
  {
    result.transport_status = universal_gnss_transport::TransportStatus::kClosed;
    result.transport_error = universal_gnss_transport::TransportError::kClosed;
    result.client_error = NtripClientError::kDisconnected;
    return result;
  }

  if (state_ == NtripClientState::kFailed)
  {
    result.transport_status = universal_gnss_transport::TransportStatus::kError;
    result.transport_error = universal_gnss_transport::TransportError::kUnknown;
    result.client_error = metrics_.last_error;
    return result;
  }

  if (!metrics_.request_sent)
  {
    result.transport_status = universal_gnss_transport::TransportStatus::kError;
    result.transport_error = universal_gnss_transport::TransportError::kInvalidArgument;
    result.client_error = NtripClientError::kProtocol;
    return result;
  }

  std::vector<std::uint8_t> read_buffer(capacity, 0u);
  const auto read_result = transport_.Read(read_buffer.data(), read_buffer.size());
  result.transport_status = read_result.status;
  result.transport_error = read_result.error;

  if (read_result.bytes_read == 0u)
  {
    if (read_result.status == universal_gnss_transport::TransportStatus::kOk)
    {
      return result;
    }

    if (read_result.status == universal_gnss_transport::TransportStatus::kEndOfStream ||
        read_result.status == universal_gnss_transport::TransportStatus::kClosed)
    {
      result.client_error = FailWith(NtripClientError::kDisconnected, timestamp_ns);
      return result;
    }

    result.client_error = FailWith(MapTransportError(read_result.error), timestamp_ns);
    return result;
  }

  NoteReceivedBytes(metrics_, read_result.bytes_read);

  if (state_ == NtripClientState::kStreaming)
  {
    std::memcpy(destination, read_buffer.data(), read_result.bytes_read);
    result.bytes_read = read_result.bytes_read;
    FeedRtcmMonitor(destination, result.bytes_read, timestamp_ns);
    return result;
  }

  std::size_t payload_bytes_written = 0u;
  result.client_error = HandleResponseBytes(read_buffer.data(),
                                            read_result.bytes_read,
                                            destination,
                                            capacity,
                                            payload_bytes_written,
                                            timestamp_ns);
  result.bytes_read = payload_bytes_written;
  if (result.client_error != NtripClientError::kNone)
  {
    FailWith(result.client_error, timestamp_ns);
  }

  return result;
}

std::size_t NtripClient::FeedRtcmMonitor(
    const std::uint8_t* data,
    const std::size_t size,
    const std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns)
{
  if (data == nullptr || size == 0u)
  {
    return 0u;
  }

  std::size_t frames_seen = 0u;
  for (std::size_t index = 0u; index < size; ++index)
  {
    const auto parsed = rtcm_framer_.PushByte(data[index], timestamp_ns);
    switch (parsed.status)
    {
      case universal_gnss_protocols::ParserStatus::kIdle:
      case universal_gnss_protocols::ParserStatus::kNeedMoreData:
      case universal_gnss_protocols::ParserStatus::kSkipped:
        break;

      case universal_gnss_protocols::ParserStatus::kRecordReady:
      {
        ++frames_seen;
        if (!parsed.record.has_value())
        {
          NoteRtcmFrame(metrics_, std::nullopt, false);
          correction_monitor_.ObserveInvalidFrame(timestamp_ns);
          break;
        }

        const bool valid_frame =
            parsed.record->checksum_status == universal_gnss_protocols::ChecksumStatus::kValid;
        NoteRtcmFrame(metrics_, parsed.record->message_type, valid_frame);
        correction_monitor_.ObserveFrame(*parsed.record);
        break;
      }

      case universal_gnss_protocols::ParserStatus::kInvalidData:
      case universal_gnss_protocols::ParserStatus::kOverflow:
      case universal_gnss_protocols::ParserStatus::kTruncated:
        ++frames_seen;
        NoteRtcmFrame(metrics_, std::nullopt, false);
        correction_monitor_.ObserveInvalidFrame(timestamp_ns);
        break;
    }
  }

  return frames_seen;
}

universal_gnss::GnssHealthSummary NtripClient::BuildCorrectionHealth(
    const universal_gnss_protocols::RtcmCorrectionHealthOptions& options) const
{
  return universal_gnss_protocols::BuildRtcmCorrectionHealth(correction_monitor_, options);
}

NtripClientState NtripClient::state() const
{
  return state_;
}

bool NtripClient::IsConnected() const
{
  return state_ == NtripClientState::kConnected ||
         state_ == NtripClientState::kStreaming;
}

const NtripReconnectState& NtripClient::reconnect_state() const
{
  return reconnect_state_;
}

const GgaInjectionPolicy& NtripClient::gga_injection_policy() const
{
  return gga_injection_policy_;
}

const NtripRequest& NtripClient::request() const
{
  return request_;
}

const std::string& NtripClient::response_header() const
{
  return response_header_;
}

const NtripConnectionMetrics& NtripClient::metrics() const
{
  return metrics_;
}

const universal_gnss_protocols::RtcmCorrectionMonitor& NtripClient::correction_monitor() const
{
  return correction_monitor_;
}

NtripClientError NtripClient::ConnectWithTransport(
    const universal_gnss_transport::TcpClientConfig& transport_config)
{
  transport_.Close();
  ResetSessionState();
  ResetSessionMetrics();

  if (transport_config.host.empty() || transport_config.port == 0u)
  {
    return FailWith(NtripClientError::kConfiguration);
  }

  state_ = NtripClientState::kConnecting;
  const auto connect_error = transport_.Open(transport_config);
  if (connect_error != universal_gnss_transport::TransportError::kNone)
  {
    return FailWith(MapTransportError(connect_error));
  }

  state_ = NtripClientState::kConnected;
  MarkConnected(metrics_);
  ClearLastError(metrics_);
  RecordReconnectSuccess(std::nullopt);
  return NtripClientError::kNone;
}

NtripClientError NtripClient::FailWith(
    const NtripClientError error,
    const std::optional<universal_gnss::GnssTimestampNs> timestamp_ns)
{
  if (ShouldTrackReconnectFailure(state_, error))
  {
    RecordReconnectFailure(timestamp_ns);
  }

  transport_.Close();
  state_ = NtripClientState::kFailed;
  MarkDisconnected(metrics_, error);
  return error;
}

void NtripClient::ResetSessionState()
{
  request_ = NtripRequest{};
  response_buffer_.clear();
  response_header_.clear();
  rtcm_framer_.Reset();
  correction_monitor_.Reset();
}

void NtripClient::ResetSessionMetrics()
{
  const std::uint32_t reconnect_count = metrics_.reconnect_count;
  metrics_ = NtripConnectionMetrics{};
  metrics_.reconnect_count = reconnect_count;
}

NtripGgaSendResult NtripClient::MakeGgaSendErrorResult(
    const NtripGgaSendError error,
    const NtripClientError client_error,
    const std::optional<GgaGenerationError> generation_error)
{
  NoteGgaSendError(metrics_, error);
  return NtripGgaSendResult{
      NtripGgaSendStatus::kError,
      client_error,
      error,
      generation_error};
}

void NtripClient::RecordReconnectFailure(
    const std::optional<universal_gnss::GnssTimestampNs> timestamp_ns)
{
  const auto decision =
      config_.reconnect_policy.OnFailure(reconnect_state_, timestamp_ns.value_or(0));
  if (decision.scheduled)
  {
    NoteReconnect(metrics_);
  }
}

void NtripClient::RecordReconnectSuccess(
    const std::optional<universal_gnss::GnssTimestampNs> timestamp_ns)
{
  if (!timestamp_ns.has_value())
  {
    if (config_.reconnect_policy.reset_after_success)
    {
      reconnect_state_.Reset();
    }
    else
    {
      reconnect_state_.next_attempt_time_ns.reset();
    }
    return;
  }

  config_.reconnect_policy.OnSuccess(reconnect_state_, timestamp_ns.value_or(0));
}

NtripClientError NtripClient::HandleResponseBytes(
    const std::uint8_t* data,
    const std::size_t size,
    std::uint8_t* destination,
    const std::size_t capacity,
    std::size_t& payload_bytes_written,
    const std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns)
{
  response_buffer_.append(reinterpret_cast<const char*>(data), size);
  const auto header_terminator = FindResponseHeaderTerminator(response_buffer_);
  if (!header_terminator.has_value())
  {
    if (response_buffer_.size() > kMaxResponseHeaderBytes)
    {
      return NtripClientError::kProtocol;
    }

    return NtripClientError::kNone;
  }

  const std::size_t header_size = header_terminator->first + header_terminator->second;
  const std::string header_text = response_buffer_.substr(0u, header_size);
  const NtripClientError header_error = ParseNtripResponseStatus(header_text);
  if (header_error != NtripClientError::kNone)
  {
    response_header_ = header_text;
    return header_error;
  }

  response_header_ = header_text;
  MarkResponseReceived(metrics_);
  state_ = NtripClientState::kStreaming;

  const std::size_t payload_size = response_buffer_.size() - header_size;
  payload_bytes_written = std::min(payload_size, capacity);
  if (payload_bytes_written > 0u)
  {
    std::memcpy(destination,
                response_buffer_.data() + static_cast<std::ptrdiff_t>(header_size),
                payload_bytes_written);
    FeedRtcmMonitor(destination, payload_bytes_written, timestamp_ns);
  }

  response_buffer_.clear();
  return NtripClientError::kNone;
}

}  // namespace universal_gnss_ntrip

#endif
