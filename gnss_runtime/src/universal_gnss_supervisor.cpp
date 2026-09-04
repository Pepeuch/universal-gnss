#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_runtime/posix_serial_factory.hpp"
#include "universal_gnss_runtime/receiver_supervisor.hpp"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int) { g_stop_requested = true; }

struct Options
{
  std::string device{};
  std::uint32_t baud{0u};
  std::string ntrip_host{};
  std::uint32_t ntrip_port{2101u};
  std::string ntrip_mountpoint{};
  std::string ntrip_username{};
  std::string ntrip_password{};
  bool ntrip_send_gga{false};
  universal_gnss_driver::ReceiverSessionKind receiver_family{
      universal_gnss_driver::ReceiverSessionKind::kAutoDetect};
};

void PrintUsage(const char* program)
{
  std::cout << "Usage: " << program
            << " --device <path> --baud <rate> --receiver-family auto|ublox|unicore|nmea\n";
  std::cout << "       [--ntrip-host <host> --ntrip-port <port> --ntrip-mountpoint <path>"
            << " --ntrip-username <user> --ntrip-password <password> [--ntrip-send-gga]]\n";
}

std::optional<std::uint32_t> ParseBaud(const std::string& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0u || parsed > 0xFFFFFFFFul)
    {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&)
  {
    return std::nullopt;
  }
}

std::optional<universal_gnss_driver::ReceiverSessionKind>
ParseReceiverFamily(const std::string& value)
{
  using universal_gnss_driver::ReceiverSessionKind;
  if (value == "auto")
    return ReceiverSessionKind::kAutoDetect;
  if (value == "ublox")
    return ReceiverSessionKind::kUblox;
  if (value == "unicore")
    return ReceiverSessionKind::kUnicore;
  if (value == "nmea")
    return ReceiverSessionKind::kNmea;
  return std::nullopt;
}

bool ParseOptions(const int argc, char** argv, Options& options)
{
  for (int index = 1; index < argc; ++index)
  {
    const std::string argument(argv[index]);
    if (argument == "--help")
    {
      PrintUsage(argv[0]);
      return false;
    }
    if (argument == "--ntrip-send-gga")
    {
      options.ntrip_send_gga = true;
      continue;
    }
    if (argument == "--device" || argument == "--baud" || argument == "--receiver-family" ||
        argument == "--ntrip-host" || argument == "--ntrip-port" ||
        argument == "--ntrip-mountpoint" || argument == "--ntrip-username" ||
        argument == "--ntrip-password")
    {
      if (++index == argc)
      {
        std::cerr << "error: missing value for " << argument << '\n';
        return false;
      }
      const std::string value(argv[index]);
      if (argument == "--device")
        options.device = value;
      if (argument == "--baud")
      {
        const auto baud = ParseBaud(value);
        if (!baud.has_value())
        {
          std::cerr << "error: invalid --baud\n";
          return false;
        }
        options.baud = *baud;
      }
      if (argument == "--receiver-family")
      {
        const auto family = ParseReceiverFamily(value);
        if (!family.has_value())
        {
          std::cerr << "error: invalid --receiver-family\n";
          return false;
        }
        options.receiver_family = *family;
      }
      if (argument == "--ntrip-host")
        options.ntrip_host = value;
      if (argument == "--ntrip-port")
      {
        const auto port = ParseBaud(value);
        if (!port.has_value() || *port > 65535u)
        {
          std::cerr << "error: invalid --ntrip-port\n";
          return false;
        }
        options.ntrip_port = *port;
      }
      if (argument == "--ntrip-mountpoint")
        options.ntrip_mountpoint = value;
      if (argument == "--ntrip-username")
        options.ntrip_username = value;
      if (argument == "--ntrip-password")
        options.ntrip_password = value;
      continue;
    }
    std::cerr << "error: unknown argument: " << argument << '\n';
    return false;
  }
  const bool ntrip_requested = !options.ntrip_host.empty() || !options.ntrip_mountpoint.empty() ||
                               !options.ntrip_username.empty() || !options.ntrip_password.empty() ||
                               options.ntrip_send_gga;
  if (ntrip_requested && (options.ntrip_host.empty() || options.ntrip_mountpoint.empty()))
  {
    std::cerr << "error: NTRIP requires --ntrip-host and --ntrip-mountpoint\n";
    return false;
  }
  return !options.device.empty() && options.baud != 0u;
}

void PrintStatus(const universal_gnss_runtime::ReceiverSupervisorSnapshot& snapshot)
{
  std::cout << "lifecycle=" << universal_gnss_runtime::ToString(snapshot.lifecycle)
            << " connected=" << (snapshot.connected ? "true" : "false")
            << " incarnation=" << snapshot.session_incarnation
            << " reconnects=" << snapshot.reconnect_attempt_count;
  if (snapshot.session_metrics.has_value())
  {
    std::cout << " runtime_updates=" << snapshot.session_metrics->runtime_updates;
  }
  if (!snapshot.last_terminal_error.empty())
  {
    std::cout << " last_error=" << snapshot.last_terminal_error;
  }
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  if (snapshot.ntrip_enabled)
  {
    std::cout << " ntrip_connected=" << (snapshot.ntrip_metrics.connected ? "true" : "false")
              << " ntrip_reconnects=" << snapshot.ntrip_metrics.reconnect_count
              << " forwarding_active=" << (snapshot.forwarding_active ? "true" : "false")
              << " forward_queue=" << snapshot.rtcm_forward_queue_depth
              << " forward_overflows=" << snapshot.rtcm_forward_queue_overflows
              << " ntrip_error=" << snapshot.ntrip_last_error;
  }
#endif
  std::cout << '\n';
}

} // namespace

int main(const int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--help")
  {
    PrintUsage(argv[0]);
    return EXIT_SUCCESS;
  }

  Options options;
  if (!ParseOptions(argc, argv, options))
  {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  universal_gnss_runtime::ReceiverSupervisorConfig config;
  config.session.kind = options.receiver_family;
  config.transport_factory = universal_gnss_runtime::MakePosixSerialTransportFactory(
      universal_gnss_transport::PosixSerialConfig{options.device, options.baud, false, 0u});
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  if (!options.ntrip_host.empty())
  {
    universal_gnss_runtime::NtripSupervisorConfig ntrip;
    ntrip.ntrip.host = options.ntrip_host;
    ntrip.ntrip.port = static_cast<std::uint16_t>(options.ntrip_port);
    ntrip.ntrip.mountpoint = options.ntrip_mountpoint;
    ntrip.ntrip.username = options.ntrip_username;
    ntrip.ntrip.password = options.ntrip_password;
    ntrip.ntrip.send_gga = options.ntrip_send_gga;
    config.ntrip = std::move(ntrip);
  }
#endif
  universal_gnss_runtime::ReceiverSupervisor supervisor(std::move(config));
  if (!supervisor.Start())
  {
    std::cerr << "error: failed to start supervisor\n";
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  while (!g_stop_requested)
  {
    PrintStatus(supervisor.Snapshot());
    for (int index = 0; index < 10 && !g_stop_requested; ++index)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  supervisor.Stop();
  PrintStatus(supervisor.Snapshot());
  return EXIT_SUCCESS;
}
