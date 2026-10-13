#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace universal_gnss_transport
{

// Optional handshake for receivers that sit behind a correction-input gateway:
// a radio modem, MCU board, or multiplexer on the receiver link that must be told
// to accept host-supplied RTCM and, for some gateways, kept alive while it does.
//
// The class is a pure, clock-injected state machine. It never touches a
// transport: every operation returns the command lines (line ending already
// appended) that the owner must write through the same serialised path as RTCM,
// in order, before any RTCM frame submitted in the same call.
//
// Forwarding demand is driven by RTCM frames submitted for forwarding. Demand
// lapses when no frame has been submitted for `demand_timeout`.
//
// Commands are opaque operator-supplied text written verbatim; checksums or any
// other framing inside a command are the operator's responsibility.

enum class RtcmGatewayLineEnding : std::uint8_t
{
  kCrLf,
  kLf,
  kCr,
  kNone,
};

std::optional<RtcmGatewayLineEnding> ParseRtcmGatewayLineEnding(std::string_view text);
const char* ToString(RtcmGatewayLineEnding line_ending);
std::string_view LineEndingBytes(RtcmGatewayLineEnding line_ending);

enum class RtcmGatewayState : std::uint8_t
{
  // No gateway behaviour is configured; the handshake never emits anything.
  kOff,
  // Configured, but RTCM forwarding is not currently demanded.
  kIdle,
  // Enable commands were sent and an acknowledgement is still awaited.
  kEnabling,
  // Enable commands were sent (and acknowledged when acknowledgement is configured).
  kActive,
  // The gateway reported dropping out, or the acknowledgement timed out, while
  // forwarding is still demanded; enable commands are being re-sent.
  kLost,
};

const char* ToString(RtcmGatewayState state);

struct RtcmGatewayConfig
{
  using Duration = std::chrono::steady_clock::duration;

  // Written in order when forwarding demand starts.
  std::vector<std::string> enable_commands{};
  // Re-written every keepalive_interval while forwarding is demanded. When no
  // enable command is configured it is also written immediately on demand start.
  std::string keepalive_command{};
  Duration keepalive_interval{Duration::zero()};
  // Written in order when forwarding demand ends (lapse or explicit Stop()).
  std::vector<std::string> disable_commands{};
  RtcmGatewayLineEnding line_ending{RtcmGatewayLineEnding::kCrLf};
  // Forwarding demand lapses after this long without a submitted RTCM frame.
  Duration demand_timeout{std::chrono::seconds(5)};

  // True when at least one command is configured.
  bool enabled() const;
};

// A command is valid when it is non-empty and contains no CR or LF byte; the
// configured line ending is the only terminator ever written.
bool IsValidRtcmGatewayCommand(std::string_view command);

struct RtcmGatewayMetrics
{
  std::size_t enable_sequences_sent{0u};
  std::size_t keepalives_sent{0u};
  std::size_t disable_sequences_sent{0u};
  std::size_t command_lines_emitted{0u};
};

class RtcmGatewayHandshake
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Commands = std::vector<std::string>;

  enum class FrameDecision : std::uint8_t
  {
    kForward,
    kWithhold,
  };

  explicit RtcmGatewayHandshake(RtcmGatewayConfig config = {});

  bool enabled() const;
  RtcmGatewayState state() const;
  const RtcmGatewayMetrics& metrics() const;
  const RtcmGatewayConfig& config() const;

  // Called for every valid RTCM frame about to be queued for the receiver.
  // Appends any command lines that must precede the frame.
  FrameDecision OnRtcmFrame(TimePoint now, Commands& commands);

  // Periodic tick: keep-alive cadence and demand lapse.
  void Poll(TimePoint now, Commands& commands);

  // Orderly shutdown: emits the disable commands when the gateway is engaged.
  void Stop(Commands& commands);

  // The receiver transport incarnation ended; nothing can be written to it.
  // Return to idle without emitting so the next demand re-enables the gateway
  // on the new incarnation.
  void OnTransportLost();

private:
  bool Engaged() const;
  void Emit(const std::string& command, Commands& commands);
  void EmitEnableSequence(TimePoint now, Commands& commands);
  void EmitDisableSequence(Commands& commands);

  RtcmGatewayConfig config_;
  RtcmGatewayState state_{RtcmGatewayState::kOff};
  RtcmGatewayMetrics metrics_{};
  std::optional<TimePoint> last_demand_time_{};
  std::optional<TimePoint> last_keepalive_time_{};
};

}  // namespace universal_gnss_transport
