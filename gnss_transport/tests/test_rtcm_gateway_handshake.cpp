#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "universal_gnss_transport/rtcm_gateway_handshake.hpp"

namespace
{

using universal_gnss_transport::RtcmGatewayConfig;
using universal_gnss_transport::RtcmGatewayHandshake;
using universal_gnss_transport::RtcmGatewayLineEnding;
using universal_gnss_transport::RtcmGatewayState;
using Commands = RtcmGatewayHandshake::Commands;
using Decision = RtcmGatewayHandshake::FrameDecision;
using TimePoint = RtcmGatewayHandshake::TimePoint;
using std::chrono::milliseconds;
using std::chrono::seconds;

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

const TimePoint kT0{seconds(1000)};

RtcmGatewayConfig GatewayConfig()
{
  RtcmGatewayConfig config;
  config.enable_commands = {"$GW,ON*00"};
  config.keepalive_command = "$GW,KEEP*00";
  config.keepalive_interval = seconds(4);
  config.disable_commands = {"$GW,OFF*00"};
  config.demand_timeout = seconds(5);
  return config;
}

void TestOffIsANoOp(TestContext& ctx)
{
  RtcmGatewayHandshake gateway;
  Commands commands;
  ctx.Expect(!gateway.enabled() && gateway.state() == RtcmGatewayState::kOff,
             "default configuration leaves the gateway off");
  ctx.Expect(gateway.OnRtcmFrame(kT0, commands) == Decision::kForward,
             "off gateway forwards every frame");
  gateway.Poll(kT0 + seconds(60), commands);
  gateway.Stop(commands);
  gateway.OnTransportLost();
  ctx.Expect(commands.empty() && gateway.metrics().command_lines_emitted == 0u &&
                 gateway.state() == RtcmGatewayState::kOff,
             "off gateway never emits a command");

  RtcmGatewayConfig interval_only;
  interval_only.keepalive_interval = seconds(4);
  ctx.Expect(!RtcmGatewayHandshake(interval_only).enabled(),
             "an interval without any command does not enable the gateway");
}

void TestEnableOnFirstActivation(TestContext& ctx)
{
  auto config = GatewayConfig();
  config.enable_commands = {"FIRST", "SECOND"};
  RtcmGatewayHandshake gateway(config);
  ctx.Expect(gateway.enabled() && gateway.state() == RtcmGatewayState::kIdle,
             "configured gateway starts idle");

  Commands commands;
  gateway.Poll(kT0, commands);
  ctx.Expect(commands.empty(), "idle gateway does not emit before forwarding demand");

  ctx.Expect(gateway.OnRtcmFrame(kT0, commands) == Decision::kForward,
             "without acknowledgement the first frame is forwarded");
  ctx.Expect(commands == Commands{"FIRST\r\n", "SECOND\r\n"},
             "enable commands are emitted in order with the line ending on first activation");
  ctx.Expect(gateway.state() == RtcmGatewayState::kActive &&
                 gateway.metrics().enable_sequences_sent == 1u,
             "first activation enters active and counts one enable sequence");

  commands.clear();
  ctx.Expect(gateway.OnRtcmFrame(kT0 + milliseconds(200), commands) == Decision::kForward &&
                 commands.empty(),
             "later frames in the same demand period emit nothing");
}

void TestKeepaliveOnlyIsSentImmediately(TestContext& ctx)
{
  RtcmGatewayConfig config;
  config.keepalive_command = "$GW,ON*00";
  config.keepalive_interval = seconds(4);
  RtcmGatewayHandshake gateway(config);

  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  ctx.Expect(commands == Commands{"$GW,ON*00\r\n"} && gateway.metrics().keepalives_sent == 0u &&
                 gateway.metrics().enable_sequences_sent == 1u,
             "with no enable command the keep-alive line doubles as the enable sequence");
}

void TestKeepaliveCadence(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(GatewayConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();

  TimePoint now = kT0;
  std::size_t keepalives = 0u;
  for (int tick = 1; tick <= 120; ++tick)
  {
    now = kT0 + milliseconds(100 * tick);
    // Keep demand alive with a frame every second.
    if (tick % 10 == 0)
    {
      gateway.OnRtcmFrame(now, commands);
    }
    gateway.Poll(now, commands);
  }
  for (const auto& line : commands)
  {
    keepalives += line == "$GW,KEEP*00\r\n" ? 1u : 0u;
  }
  ctx.Expect(keepalives == 3u && commands.size() == 3u && gateway.metrics().keepalives_sent == 3u,
             "keep-alive is emitted once per interval (t=4,8,12 s) and nothing else is emitted");

  RtcmGatewayConfig no_interval = GatewayConfig();
  no_interval.keepalive_interval = RtcmGatewayConfig::Duration::zero();
  RtcmGatewayHandshake disabled_keepalive(no_interval);
  commands.clear();
  disabled_keepalive.OnRtcmFrame(kT0, commands);
  disabled_keepalive.OnRtcmFrame(kT0 + seconds(3), commands);
  disabled_keepalive.Poll(kT0 + seconds(4), commands);
  ctx.Expect(commands == Commands{"$GW,ON*00\r\n"},
             "a zero keep-alive interval never re-emits the keep-alive line");
}

void TestDisableOnLossAndReenable(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(GatewayConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();

  gateway.Poll(kT0 + milliseconds(4900), commands);
  ctx.Expect(commands == Commands{"$GW,KEEP*00\r\n"} &&
                 gateway.state() == RtcmGatewayState::kActive,
             "demand is still current just before the timeout");
  commands.clear();

  gateway.Poll(kT0 + seconds(5), commands);
  ctx.Expect(commands == Commands{"$GW,OFF*00\r\n"} && gateway.state() == RtcmGatewayState::kIdle &&
                 gateway.metrics().disable_sequences_sent == 1u,
             "demand lapse emits the disable commands and returns to idle");
  commands.clear();

  gateway.Poll(kT0 + seconds(30), commands);
  ctx.Expect(commands.empty(), "idle gateway emits no keep-alive after the demand lapsed");

  gateway.OnRtcmFrame(kT0 + seconds(31), commands);
  ctx.Expect(commands == Commands{"$GW,ON*00\r\n"} &&
                 gateway.state() == RtcmGatewayState::kActive &&
                 gateway.metrics().enable_sequences_sent == 2u,
             "forwarding resuming after a lapse re-sends the enable commands");
}

void TestLateFrameClosesPreviousDemandFirst(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(GatewayConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();

  // No Poll ran between the frames; the demand had lapsed anyway.
  gateway.OnRtcmFrame(kT0 + seconds(9), commands);
  ctx.Expect(commands == Commands{"$GW,OFF*00\r\n", "$GW,ON*00\r\n"},
             "a frame after an unpolled lapse emits disable then enable, never a stale keep-alive");
}

void TestStopAndTransportLoss(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(GatewayConfig());
  Commands commands;
  gateway.Stop(commands);
  ctx.Expect(commands.empty(), "stop while idle emits nothing");

  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();
  gateway.Stop(commands);
  ctx.Expect(commands == Commands{"$GW,OFF*00\r\n"} && gateway.state() == RtcmGatewayState::kIdle,
             "stop while engaged emits the disable commands");
  commands.clear();
  gateway.Poll(kT0 + seconds(4), commands);
  ctx.Expect(commands.empty(), "keep-alive stops after shutdown");

  gateway.OnRtcmFrame(kT0 + seconds(10), commands);
  commands.clear();
  gateway.OnTransportLost();
  gateway.Poll(kT0 + seconds(14), commands);
  ctx.Expect(commands.empty() && gateway.state() == RtcmGatewayState::kIdle,
             "transport loss returns to idle without emitting and stops the keep-alive");
  gateway.OnRtcmFrame(kT0 + seconds(15), commands);
  ctx.Expect(commands == Commands{"$GW,ON*00\r\n"},
             "the next frame on a new transport incarnation re-enables the gateway");

  RtcmGatewayConfig no_disable = GatewayConfig();
  no_disable.disable_commands.clear();
  RtcmGatewayHandshake silent(no_disable);
  commands.clear();
  silent.OnRtcmFrame(kT0, commands);
  commands.clear();
  silent.Poll(kT0 + seconds(6), commands);
  ctx.Expect(commands.empty() && silent.state() == RtcmGatewayState::kIdle &&
                 silent.metrics().disable_sequences_sent == 0u,
             "without disable commands the lapse is silent so a timeout gateway falls back alone");
}

void TestLineEndings(TestContext& ctx)
{
  using universal_gnss_transport::ParseRtcmGatewayLineEnding;
  ctx.Expect(ParseRtcmGatewayLineEnding("crlf") == RtcmGatewayLineEnding::kCrLf &&
                 ParseRtcmGatewayLineEnding("lf") == RtcmGatewayLineEnding::kLf &&
                 ParseRtcmGatewayLineEnding("cr") == RtcmGatewayLineEnding::kCr &&
                 ParseRtcmGatewayLineEnding("none") == RtcmGatewayLineEnding::kNone &&
                 !ParseRtcmGatewayLineEnding("\\r\\n").has_value() &&
                 !ParseRtcmGatewayLineEnding("CRLF").has_value(),
             "line ending names parse exactly");

  const std::vector<std::pair<RtcmGatewayLineEnding, std::string>> cases{
      {RtcmGatewayLineEnding::kCrLf, "CMD\r\n"},
      {RtcmGatewayLineEnding::kLf, "CMD\n"},
      {RtcmGatewayLineEnding::kCr, "CMD\r"},
      {RtcmGatewayLineEnding::kNone, "CMD"},
  };
  for (const auto& [ending, expected] : cases)
  {
    RtcmGatewayConfig config;
    config.enable_commands = {"CMD"};
    config.line_ending = ending;
    RtcmGatewayHandshake gateway(config);
    Commands commands;
    gateway.OnRtcmFrame(kT0, commands);
    ctx.Expect(commands == Commands{expected},
               std::string("command is written verbatim with line ending ") +
                   universal_gnss_transport::ToString(ending));
  }

  using universal_gnss_transport::IsValidRtcmGatewayCommand;
  ctx.Expect(IsValidRtcmGatewayCommand("$GNRTK,ON*69") && !IsValidRtcmGatewayCommand("") &&
                 !IsValidRtcmGatewayCommand("A\r\n") && !IsValidRtcmGatewayCommand("A\nB"),
             "commands must be non-empty and free of CR/LF");
}

void Feed(RtcmGatewayHandshake& gateway,
          const std::string& text,
          const TimePoint now,
          Commands& commands)
{
  gateway.ObserveInboundBytes(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), now, commands);
}

RtcmGatewayConfig AckConfig()
{
  // Mirrors the documented example: the keep-alive line is the enable command.
  RtcmGatewayConfig config;
  config.enable_commands = {"$GNRTK,ON*69"};
  config.keepalive_command = "$GNRTK,ON*69";
  config.keepalive_interval = seconds(4);
  config.ack_pattern = "$GNRTK,ON";
  config.ack_timeout = seconds(2);
  config.nack_pattern = "$GNRTK,OFF";
  config.reset_commands = {"RTKRESET"};
  config.demand_timeout = seconds(5);
  return config;
}

void TestAckGatingHoldsThenReleases(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(AckConfig());
  Commands commands;
  ctx.Expect(gateway.observes_inbound(), "an acknowledgement pattern enables inbound observation");

  ctx.Expect(gateway.OnRtcmFrame(kT0, commands) == Decision::kWithhold,
             "the first frame is withheld until the gateway acknowledges");
  ctx.Expect(commands == Commands{"$GNRTK,ON*69\r\n"} &&
                 gateway.state() == RtcmGatewayState::kEnabling,
             "enable is sent and the gateway waits for acknowledgement");
  commands.clear();
  ctx.Expect(gateway.OnRtcmFrame(kT0 + milliseconds(500), commands) == Decision::kWithhold &&
                 commands.empty() && gateway.metrics().frames_withheld == 2u,
             "frames keep being withheld and counted while enabling");

  // The receiver's own NMEA and an OFF report while enabling change nothing.
  Feed(gateway, "$GNGGA,1,2,3*00\r\n$GNRTK,OFF*27\r\n", kT0 + milliseconds(600), commands);
  ctx.Expect(commands.empty() && gateway.state() == RtcmGatewayState::kEnabling &&
                 gateway.metrics().nacks_seen == 1u,
             "an OFF report while enabling is not a drop-out edge");

  // Acknowledgement split across reads, preceded by binary noise on the line.
  Feed(gateway, std::string("\xD3\x00\x13$GNRT", 8), kT0 + milliseconds(700), commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kEnabling, "a partial line does not match");
  Feed(gateway, "K,ON*69\r\n", kT0 + milliseconds(800), commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kActive && gateway.metrics().acks_seen == 1u &&
                 gateway.metrics().last_ack_time == kT0 + milliseconds(800),
             "a line containing the acknowledgement releases the gateway");
  ctx.Expect(gateway.OnRtcmFrame(kT0 + milliseconds(900), commands) == Decision::kForward &&
                 commands.empty(),
             "frames are forwarded once acknowledged");
}

void TestAckTimeoutResendsEnable(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(AckConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();

  gateway.OnRtcmFrame(kT0 + milliseconds(1900), commands);
  gateway.Poll(kT0 + milliseconds(1900), commands);
  ctx.Expect(commands.empty() && gateway.metrics().ack_timeouts == 0u,
             "no re-send before the acknowledgement timeout");

  gateway.Poll(kT0 + seconds(2), commands);
  ctx.Expect(
      commands == Commands{"$GNRTK,ON*69\r\n"} && gateway.state() == RtcmGatewayState::kLost &&
          gateway.metrics().ack_timeouts == 1u && gateway.metrics().enable_sequences_sent == 2u,
      "acknowledgement timeout re-sends the enable commands and reports lost");
  commands.clear();

  gateway.OnRtcmFrame(kT0 + seconds(3), commands);
  ctx.Expect(gateway.OnRtcmFrame(kT0 + seconds(3), commands) == Decision::kWithhold,
             "frames stay withheld while the gateway is lost");
  Feed(gateway, "$GNRTK,ON*69\r\n", kT0 + seconds(3), commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kActive && commands.empty(),
             "a late acknowledgement recovers the gateway");
}

void TestNackEdgeResetsAndReenables(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(AckConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  Feed(gateway, "$GNRTK,ON*69\r\n", kT0 + milliseconds(100), commands);
  commands.clear();

  Feed(gateway, "$GNRTK,ON*69\r\n", kT0 + seconds(1), commands);
  ctx.Expect(commands.empty() && gateway.state() == RtcmGatewayState::kActive,
             "repeated acknowledgements while active emit nothing");

  Feed(gateway, "$GNRTK,OFF*27\r\n", kT0 + seconds(2), commands);
  ctx.Expect(commands == Commands{"RTKRESET\r\n", "$GNRTK,ON*69\r\n"} &&
                 gateway.state() == RtcmGatewayState::kLost &&
                 gateway.metrics().reset_sequences_sent == 1u,
             "an ON->OFF edge emits the reset commands before re-enabling");
  commands.clear();

  ctx.Expect(gateway.OnRtcmFrame(kT0 + seconds(2), commands) == Decision::kWithhold,
             "frames are withheld after a drop-out until re-acknowledged");
  Feed(gateway, "$GNRTK,OFF*27\r\n", kT0 + milliseconds(2500), commands);
  ctx.Expect(commands.empty() && gateway.metrics().reset_sequences_sent == 1u,
             "further OFF reports while recovering do not repeat the reset");
  Feed(gateway, "$GNRTK,ON*69\r\n", kT0 + seconds(3), commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kActive, "re-acknowledgement recovers");

  RtcmGatewayConfig no_reset = AckConfig();
  no_reset.reset_commands.clear();
  RtcmGatewayHandshake plain(no_reset);
  commands.clear();
  plain.OnRtcmFrame(kT0, commands);
  Feed(plain, "$GNRTK,ON*69\n", kT0, commands);
  commands.clear();
  Feed(plain, "$GNRTK,OFF*27\n", kT0 + seconds(1), commands);
  ctx.Expect(commands == Commands{"$GNRTK,ON*69\r\n"} && plain.metrics().reset_sequences_sent == 0u,
             "without reset commands a drop-out only re-sends the enable sequence");
}

void TestInboundLineBounds(TestContext& ctx)
{
  RtcmGatewayHandshake gateway(AckConfig());
  Commands commands;
  gateway.OnRtcmFrame(kT0, commands);
  commands.clear();

  const std::string oversized(RtcmGatewayHandshake::kMaxInboundLineLength + 10u, 'x');
  Feed(gateway, oversized + "$GNRTK,ON*69\r\n", kT0, commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kEnabling && gateway.metrics().acks_seen == 0u,
             "an over-long line never matches");
  Feed(gateway, "$GNRTK,ON*69\r\n", kT0, commands);
  ctx.Expect(gateway.state() == RtcmGatewayState::kActive,
             "matching resumes on the next line after an over-long one");

  RtcmGatewayHandshake reconnecting(AckConfig());
  reconnecting.OnRtcmFrame(kT0, commands);
  Feed(reconnecting, "$GNRTK,O", kT0, commands);
  reconnecting.OnTransportLost();
  Feed(reconnecting, "N*69\r\n", kT0, commands);
  ctx.Expect(reconnecting.metrics().acks_seen == 0u,
             "a partial line does not survive a transport incarnation change");

  RtcmGatewayHandshake no_patterns(GatewayConfig());
  commands.clear();
  no_patterns.OnRtcmFrame(kT0, commands);
  commands.clear();
  Feed(no_patterns, "$GNRTK,ON*69\r\n$GNRTK,OFF*27\r\n", kT0, commands);
  ctx.Expect(!no_patterns.observes_inbound() && commands.empty() &&
                 no_patterns.metrics().acks_seen == 0u && no_patterns.metrics().nacks_seen == 0u,
             "without patterns inbound traffic is ignored");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestOffIsANoOp(ctx);
  TestEnableOnFirstActivation(ctx);
  TestKeepaliveOnlyIsSentImmediately(ctx);
  TestKeepaliveCadence(ctx);
  TestDisableOnLossAndReenable(ctx);
  TestLateFrameClosesPreviousDemandFirst(ctx);
  TestStopAndTransportLoss(ctx);
  TestLineEndings(ctx);
  TestAckGatingHoldsThenReleases(ctx);
  TestAckTimeoutResendsEnable(ctx);
  TestNackEdgeResetsAndReenables(ctx);
  TestInboundLineBounds(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All RTCM gateway handshake tests passed\n";
  return EXIT_SUCCESS;
}
