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
// Optionally the gateway's own inbound status lines can be observed: an
// acknowledgement pattern confirms the gateway accepted host corrections (RTCM
// is withheld until then) and a drop-out pattern reports it fell back, which
// triggers the reset commands and a fresh enable sequence. A pattern matches
// when it occurs as a substring of one inbound LF-terminated line (a trailing
// CR is ignored; lines longer than kMaxInboundLineLength never match).
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

  // Acknowledgement option. When ack_pattern is set, RTCM frames are withheld
  // (discarded and counted) until an inbound line contains it; the enable
  // sequence is re-sent every ack_timeout until it does.
  std::string ack_pattern{};
  Duration ack_timeout{std::chrono::seconds(5)};
  // Inbound report that the gateway dropped out. Only meaningful with
  // ack_pattern: an active gateway reporting it emits reset_commands, then the
  // enable sequence again.
  std::string nack_pattern{};
  std::vector<std::string> reset_commands{};

  // True when at least one command is configured.
  bool enabled() const;
  bool acknowledgement_required() const;
};

// A command is valid when it is non-empty and contains no CR or LF byte; the
// configured line ending is the only terminator ever written.
bool IsValidRtcmGatewayCommand(std::string_view command);

struct RtcmGatewayMetrics
{
  std::size_t enable_sequences_sent{0u};
  std::size_t keepalives_sent{0u};
  std::size_t disable_sequences_sent{0u};
  std::size_t reset_sequences_sent{0u};
  std::size_t command_lines_emitted{0u};
  std::size_t acks_seen{0u};
  std::size_t nacks_seen{0u};
  std::size_t ack_timeouts{0u};
  std::size_t frames_withheld{0u};
  std::optional<std::chrono::steady_clock::time_point> last_ack_time{};
};

class RtcmGatewayHandshake
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Commands = std::vector<std::string>;
  static constexpr std::size_t kMaxInboundLineLength{256u};

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

  // Periodic tick: keep-alive cadence, demand lapse and acknowledgement timeout.
  void Poll(TimePoint now, Commands& commands);

  // Bytes read from the receiver link. Scans for acknowledgement / drop-out
  // lines when either pattern is configured; never alters the input.
  void ObserveInboundBytes(const std::uint8_t* data,
                           std::size_t size,
                           TimePoint now,
                           Commands& commands);
  bool observes_inbound() const;

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
  void ProcessInboundLine(TimePoint now, Commands& commands);

  RtcmGatewayConfig config_;
  RtcmGatewayState state_{RtcmGatewayState::kOff};
  RtcmGatewayMetrics metrics_{};
  std::optional<TimePoint> last_demand_time_{};
  std::optional<TimePoint> last_keepalive_time_{};
  std::optional<TimePoint> last_enable_time_{};
  std::string inbound_line_{};
  bool inbound_line_overflow_{false};
};

}  // namespace universal_gnss_transport
