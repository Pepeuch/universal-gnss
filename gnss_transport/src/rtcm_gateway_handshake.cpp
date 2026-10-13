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
    state_ = RtcmGatewayState::kActive;
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
  // The enable sequence starts the keep-alive interval.
  last_keepalive_time_ = now;
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
