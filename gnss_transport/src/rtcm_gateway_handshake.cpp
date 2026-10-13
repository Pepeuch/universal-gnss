#include "universal_gnss_transport/rtcm_gateway_handshake.hpp"

#include <utility>

namespace universal_gnss_transport
{

std::optional<RtcmGatewayLineEnding> ParseRtcmGatewayLineEnding(const std::string_view text)
{
  if (text == "crlf")
  {
    return RtcmGatewayLineEnding::kCrLf;
  }
  if (text == "lf")
  {
    return RtcmGatewayLineEnding::kLf;
  }
  if (text == "cr")
  {
    return RtcmGatewayLineEnding::kCr;
  }
  if (text == "none")
  {
    return RtcmGatewayLineEnding::kNone;
  }
  return std::nullopt;
}

const char* ToString(const RtcmGatewayLineEnding line_ending)
{
  switch (line_ending)
  {
    case RtcmGatewayLineEnding::kCrLf:
      return "crlf";
    case RtcmGatewayLineEnding::kLf:
      return "lf";
    case RtcmGatewayLineEnding::kCr:
      return "cr";
    case RtcmGatewayLineEnding::kNone:
      return "none";
  }
  return "crlf";
}

std::string_view LineEndingBytes(const RtcmGatewayLineEnding line_ending)
{
  switch (line_ending)
  {
    case RtcmGatewayLineEnding::kCrLf:
      return "\r\n";
    case RtcmGatewayLineEnding::kLf:
      return "\n";
    case RtcmGatewayLineEnding::kCr:
      return "\r";
    case RtcmGatewayLineEnding::kNone:
      return {};
  }
  return "\r\n";
}

const char* ToString(const RtcmGatewayState state)
{
  switch (state)
  {
    case RtcmGatewayState::kOff:
      return "off";
    case RtcmGatewayState::kIdle:
      return "idle";
    case RtcmGatewayState::kEnabling:
      return "enabling";
    case RtcmGatewayState::kActive:
      return "active";
    case RtcmGatewayState::kLost:
      return "lost";
  }
  return "off";
}

bool RtcmGatewayConfig::enabled() const
{
  return !enable_commands.empty() || !keepalive_command.empty() || !disable_commands.empty();
}

bool RtcmGatewayConfig::acknowledgement_required() const
{
  return !ack_pattern.empty();
}

bool IsValidRtcmGatewayCommand(const std::string_view command)
{
  return !command.empty() && command.find_first_of("\r\n") == std::string_view::npos;
}

RtcmGatewayHandshake::RtcmGatewayHandshake(RtcmGatewayConfig config) : config_(std::move(config))
{
  state_ = config_.enabled() ? RtcmGatewayState::kIdle : RtcmGatewayState::kOff;
}

bool RtcmGatewayHandshake::enabled() const
{
  return state_ != RtcmGatewayState::kOff;
}

RtcmGatewayState RtcmGatewayHandshake::state() const
{
  return state_;
}

const RtcmGatewayMetrics& RtcmGatewayHandshake::metrics() const
{
  return metrics_;
}

const RtcmGatewayConfig& RtcmGatewayHandshake::config() const
{
  return config_;
}

RtcmGatewayHandshake::FrameDecision RtcmGatewayHandshake::OnRtcmFrame(const TimePoint now,
                                                                      Commands& commands)
{
  if (!enabled())
  {
    return FrameDecision::kForward;
  }

  // A frame arriving after the demand lapsed (before the next Poll) ends the
  // previous demand period first, so disable/enable stay strictly paired.
  Poll(now, commands);
  last_demand_time_ = now;

  if (state_ == RtcmGatewayState::kIdle)
  {
    EmitEnableSequence(now, commands);
    state_ = config_.acknowledgement_required() ? RtcmGatewayState::kEnabling
                                                : RtcmGatewayState::kActive;
  }
  if (config_.acknowledgement_required() && state_ != RtcmGatewayState::kActive)
  {
    ++metrics_.frames_withheld;
    return FrameDecision::kWithhold;
  }
  return FrameDecision::kForward;
}

void RtcmGatewayHandshake::Poll(const TimePoint now, Commands& commands)
{
  if (!Engaged())
  {
    return;
  }

  if (!last_demand_time_.has_value() || now - *last_demand_time_ >= config_.demand_timeout)
  {
    EmitDisableSequence(commands);
    state_ = RtcmGatewayState::kIdle;
    return;
  }

  if (config_.acknowledgement_required() &&
      (state_ == RtcmGatewayState::kEnabling || state_ == RtcmGatewayState::kLost) &&
      last_enable_time_.has_value() && now - *last_enable_time_ >= config_.ack_timeout)
  {
    ++metrics_.ack_timeouts;
    EmitEnableSequence(now, commands);
    state_ = RtcmGatewayState::kLost;
    return;
  }

  if (!config_.keepalive_command.empty() &&
      config_.keepalive_interval > RtcmGatewayConfig::Duration::zero() &&
      (!last_keepalive_time_.has_value() ||
       now - *last_keepalive_time_ >= config_.keepalive_interval))
  {
    Emit(config_.keepalive_command, commands);
    ++metrics_.keepalives_sent;
    last_keepalive_time_ = now;
  }
}

bool RtcmGatewayHandshake::observes_inbound() const
{
  return enabled() && (config_.acknowledgement_required() || !config_.nack_pattern.empty());
}

void RtcmGatewayHandshake::ObserveInboundBytes(const std::uint8_t* data,
                                               const std::size_t size,
                                               const TimePoint now,
                                               Commands& commands)
{
  if (!observes_inbound() || data == nullptr)
  {
    return;
  }
  for (std::size_t index = 0u; index < size; ++index)
  {
    const char byte = static_cast<char>(data[index]);
    if (byte == '\n')
    {
      if (!inbound_line_overflow_)
      {
        ProcessInboundLine(now, commands);
      }
      inbound_line_.clear();
      inbound_line_overflow_ = false;
      continue;
    }
    if (inbound_line_.size() < kMaxInboundLineLength)
    {
      inbound_line_.push_back(byte);
    }
    else
    {
      inbound_line_overflow_ = true;
    }
  }
}

void RtcmGatewayHandshake::ProcessInboundLine(const TimePoint now, Commands& commands)
{
  if (!inbound_line_.empty() && inbound_line_.back() == '\r')
  {
    inbound_line_.pop_back();
  }

  if (config_.acknowledgement_required() &&
      inbound_line_.find(config_.ack_pattern) != std::string::npos)
  {
    ++metrics_.acks_seen;
    metrics_.last_ack_time = now;
    if (state_ == RtcmGatewayState::kEnabling || state_ == RtcmGatewayState::kLost)
    {
      state_ = RtcmGatewayState::kActive;
    }
    return;
  }

  if (!config_.nack_pattern.empty() &&
      inbound_line_.find(config_.nack_pattern) != std::string::npos)
  {
    ++metrics_.nacks_seen;
    // Only an ON->OFF edge of a confirmed gateway is a drop-out. While enabling
    // or recovering, OFF reports are expected and the ack timeout re-sends.
    if (config_.acknowledgement_required() && state_ == RtcmGatewayState::kActive)
    {
      for (const auto& command : config_.reset_commands)
      {
        Emit(command, commands);
      }
      if (!config_.reset_commands.empty())
      {
        ++metrics_.reset_sequences_sent;
      }
      EmitEnableSequence(now, commands);
      state_ = RtcmGatewayState::kLost;
    }
  }
}

void RtcmGatewayHandshake::Stop(Commands& commands)
{
  if (!Engaged())
  {
    return;
  }
  EmitDisableSequence(commands);
  state_ = RtcmGatewayState::kIdle;
}

void RtcmGatewayHandshake::OnTransportLost()
{
  if (Engaged())
  {
    state_ = RtcmGatewayState::kIdle;
  }
  // A partial line must not join bytes from the next transport incarnation.
  inbound_line_.clear();
  inbound_line_overflow_ = false;
}

bool RtcmGatewayHandshake::Engaged() const
{
  return state_ == RtcmGatewayState::kEnabling || state_ == RtcmGatewayState::kActive ||
         state_ == RtcmGatewayState::kLost;
}

void RtcmGatewayHandshake::Emit(const std::string& command, Commands& commands)
{
  std::string line = command;
  line.append(LineEndingBytes(config_.line_ending));
  commands.push_back(std::move(line));
  ++metrics_.command_lines_emitted;
}

void RtcmGatewayHandshake::EmitEnableSequence(const TimePoint now, Commands& commands)
{
  for (const auto& command : config_.enable_commands)
  {
    Emit(command, commands);
  }
  if (config_.enable_commands.empty() && !config_.keepalive_command.empty())
  {
    Emit(config_.keepalive_command, commands);
  }
  ++metrics_.enable_sequences_sent;
  // The enable sequence starts the keep-alive interval and the ack timeout.
  last_keepalive_time_ = now;
  last_enable_time_ = now;
}

void RtcmGatewayHandshake::EmitDisableSequence(Commands& commands)
{
  if (config_.disable_commands.empty())
  {
    return;
  }
  for (const auto& command : config_.disable_commands)
  {
    Emit(command, commands);
  }
  ++metrics_.disable_sequences_sent;
}

}  // namespace universal_gnss_transport
