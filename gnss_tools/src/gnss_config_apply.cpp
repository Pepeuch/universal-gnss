#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_tools/config_apply.hpp"

#if defined(__linux__)
#include "universal_gnss_transport/posix_serial_transport.hpp"
#endif

namespace
{

using universal_gnss_driver::DiscoverReceivers;
using universal_gnss_driver::MakeExplicitReceiverPortCandidate;
using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverProbeConfig;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::ProbeReceiverPort;

#if defined(__linux__)
using universal_gnss_tools::ConfigApplyTransportHooks;
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::PosixSerialConfig;
using universal_gnss_transport::PosixSerialTransport;
using universal_gnss_transport::TransportError;
#endif

struct CliOptions
{
  bool json_output{false};
  bool discover_receiver{false};
  bool baud_auto{false};
  std::optional<std::string> family_text{};
  std::optional<std::string> profile_text{};
  universal_gnss_tools::ConfigApplyOptions apply{};
};

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " [--json] [--receiver auto] [--device <path>] [--baud <value|auto>] [--config-baud <value>]\n"
      << "       [--family <auto|ublox|unicore|nmea>] --profile <runtime_only|rover_high_precision|rover_high_precision_debug|factory_reset>\n"
      << "       [--apply-mode <dry-run|runtime-only|persistent>]\n"
      << "       [--signal-profile <balanced|high_precision|all_signals|minimal|custom>]"
      << " [--signal-group <\"2\"|\"3 6\"|...>]"
      << " [--model <UM960|UM980|UM981|UM982|UB9A0>]"
      << " [--output-port <usb|uart1|uart2|all|auto>] [--rate-hz <value>]\n"
      << "       [--timeout-ms <value>] [--confirm|--yes]\n"
      << "Legacy aliases: --port, --execute, --persistent, --confirm-runtime,\n"
      << "                --confirm-persistent, and positional <family> <profile>\n"
      << "Examples:\n"
      << "  " << program_name
      << " --receiver auto --device /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud auto --profile rover_high_precision --apply-mode runtime-only\n"
      << "  " << program_name
      << " --receiver auto --device /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud auto --profile rover_high_precision --output-port auto --apply-mode runtime-only\n"
      << "  " << program_name
      << " --receiver auto --device /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 --baud auto --profile rover_high_precision --apply-mode runtime-only --confirm\n"
      << "  " << program_name
      << " --receiver auto --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud auto --profile rover_high_precision_debug --apply-mode runtime-only --confirm --timeout-ms 5000\n"
      << "  " << program_name
      << " --family unicore --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 921600 --profile rover_high_precision --apply-mode persistent --config-baud 460800 --confirm\n"
      << "  " << program_name
      << " --family unicore --model UM982 --device /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 921600 --profile rover_high_precision --signal-profile high_precision --apply-mode runtime-only --confirm\n"
      << "  " << program_name
      << " --family unicore --model UM980 --device /dev/ttyAMA4 --baud 921600 --profile rover_high_precision --signal-group 2 --apply-mode persistent --confirm\n"
      << "  " << program_name
      << " --family unicore --model UM981 --device /dev/ttyAMA4 --baud 921600 --profile rover_high_precision --apply-mode runtime-only --confirm\n"
      << "  " << program_name
      << " --family nmea --profile runtime_only\n"
      << "Notes:\n"
      << "  no live writes occur unless --confirm or --yes is present\n"
      << "  Unicore persistent/factory_reset apply uses a reset/reprobe workflow; other persistent workflows remain guarded\n"
      << "  --baud selects the current transport baud; --config-baud selects the post-reset receiver baud\n"
      << "  prefer /dev/serial/by-id/* paths when available\n";
}

bool ParseUnsigned(const std::string& text, std::uint32_t& value)
{
  std::size_t parsed = 0u;
  try
  {
    const auto numeric = std::stoul(text, &parsed, 10);
    if (parsed != text.size())
    {
      return false;
    }
    value = static_cast<std::uint32_t>(numeric);
    return true;
  }
  catch (...)
  {
    return false;
  }
}

bool ParseDouble(const std::string& text, double& value)
{
  std::size_t parsed = 0u;
  try
  {
    value = std::stod(text, &parsed);
    return parsed == text.size();
  }
  catch (...)
  {
    return false;
  }
}

std::string ToLowerCopy(std::string text)
{
  std::transform(
      text.begin(),
      text.end(),
      text.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool ParseFamily(const std::string& text, ReceiverDetectedFamily& family)
{
  const std::string normalized = ToLowerCopy(text);
  if (normalized == "ublox")
  {
    family = ReceiverDetectedFamily::kUblox;
    return true;
  }
  if (normalized == "unicore")
  {
    family = ReceiverDetectedFamily::kUnicore;
    return true;
  }
  if (normalized == "nmea")
  {
    family = ReceiverDetectedFamily::kNmea;
    return true;
  }
  return false;
}

bool ParseProfile(const std::string& text, ReceiverAutoConfigProfile& profile)
{
  const auto parsed = universal_gnss_driver::ParseReceiverAutoConfigProfile(text);
  if (!parsed.has_value())
  {
    return false;
  }
  profile = *parsed;
  return true;
}

bool ParseApplyMode(const std::string& text, ReceiverAutoConfigApplyMode& apply_mode)
{
  const std::string normalized = ToLowerCopy(text);
  if (normalized == "dry-run" || normalized == "dry_run")
  {
    apply_mode = ReceiverAutoConfigApplyMode::kDryRun;
    return true;
  }
  if (normalized == "runtime-only" || normalized == "runtime_only")
  {
    apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
    return true;
  }
  if (normalized == "persistent")
  {
    apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
    return true;
  }
  return false;
}

void PrintResult(const universal_gnss_tools::ConfigApplyResult& result,
                 const bool json_output)
{
  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatConfigApplyJson(result);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatConfigApplyText(result);
  }
}

ReceiverProbeResult MakeUnknownDiscoveryResult(
    const std::optional<std::string>& explicit_device,
    const std::optional<std::uint32_t>& explicit_baud,
    const std::string& reason)
{
  ReceiverProbeResult result;
  if (explicit_device.has_value())
  {
    result.path = *explicit_device;
  }
  if (explicit_baud.has_value())
  {
    result.selected_baud = *explicit_baud;
  }
  result.detected_family = ReceiverDetectedFamily::kUnknown;
  result.note = reason;
  result.reason = reason;
  return result;
}

std::optional<ReceiverProbeResult> DiscoverRequestedReceiver(
    const CliOptions& cli_options)
{
  ReceiverProbeConfig config;
  if (!cli_options.baud_auto && cli_options.apply.transport_baud_rate != 0u)
  {
    config.baud_candidates = {cli_options.apply.transport_baud_rate};
  }

  const std::optional<std::string> explicit_device =
      cli_options.apply.device_path.empty()
          ? std::nullopt
          : std::optional<std::string>{cli_options.apply.device_path};
  auto results = DiscoverReceivers(config, explicit_device);
  if (results.empty())
  {
    return MakeUnknownDiscoveryResult(
        explicit_device,
        cli_options.apply.transport_baud_rate != 0u
            ? std::optional<std::uint32_t>{cli_options.apply.transport_baud_rate}
            : std::nullopt,
        explicit_device.has_value() ? "no_recognizable_receiver_frames"
                                    : "no_receiver_candidates_found");
  }

  return results.front();
}

bool LiveApplyRequested(const universal_gnss_tools::ConfigApplyOptions& options)
{
  return options.apply_mode != ReceiverAutoConfigApplyMode::kDryRun;
}

#if defined(__linux__)

class PosixSerialConfigApplyHooks final : public ConfigApplyTransportHooks
{
public:
  bool ProbeReceiverPath(const std::string& device_path,
                         const std::vector<std::uint32_t>& baud_candidates,
                         const std::uint32_t read_timeout_ms,
                         ReceiverProbeResult& probe_result,
                         std::string& error_message) override
  {
    ReceiverProbeConfig config;
    config.baud_candidates = baud_candidates;
    config.read_timeout_ms = read_timeout_ms;

    probe_result =
        ProbeReceiverPort(MakeExplicitReceiverPortCandidate(device_path), config);
    error_message.clear();
    return true;
  }

  bool ReopenTransport(ByteDuplex& transport,
                       const std::string& device_path,
                       const std::uint32_t baud_rate,
                       const std::uint32_t read_timeout_ms,
                       std::string& error_message) override
  {
    auto* posix_transport = dynamic_cast<PosixSerialTransport*>(&transport);
    if (posix_transport == nullptr)
    {
      error_message =
          "recovery workflow requires a POSIX serial transport instance";
      return false;
    }

    posix_transport->Close();

    PosixSerialConfig serial_config;
    serial_config.device_path = device_path;
    serial_config.baud_rate = baud_rate;
    serial_config.read_timeout_ms = read_timeout_ms;
    const auto open_error = posix_transport->Open(serial_config);
    if (open_error != TransportError::kNone)
    {
      error_message =
          "failed to reopen serial transport during the recovery workflow";
      return false;
    }

    error_message.clear();
    return true;
  }
};

#endif

}  // namespace

int main(int argc, char** argv)
{
  CliOptions cli_options;
  std::vector<std::string> positional_arguments;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    auto require_value = [&](const char* flag_name) -> const char* {
      if (index + 1 >= argc)
      {
        std::cerr << "error: missing value for " << flag_name << '\n';
        PrintUsage(argv[0]);
        std::exit(EXIT_FAILURE);
      }
      ++index;
      return argv[index];
    };

    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (argument == "--json")
    {
      cli_options.json_output = true;
      continue;
    }
    if (argument == "--receiver")
    {
      const std::string value = require_value("--receiver");
      if (ToLowerCopy(value) != "auto")
      {
        std::cerr << "error: --receiver currently only supports 'auto'\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.discover_receiver = true;
      continue;
    }
    if (argument == "--device" || argument == "--port")
    {
      cli_options.apply.device_path = require_value(argument.c_str());
      continue;
    }
    if (argument == "--baud")
    {
      const std::string value = require_value("--baud");
      if (ToLowerCopy(value) == "auto")
      {
        cli_options.baud_auto = true;
        cli_options.discover_receiver = true;
        cli_options.apply.transport_baud_rate = 0u;
        continue;
      }

      std::uint32_t baud = 0u;
      if (!ParseUnsigned(value, baud))
      {
        std::cerr << "error: invalid --baud value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.transport_baud_rate = baud;
      continue;
    }
    if (argument == "--config-baud")
    {
      std::uint32_t baud = 0u;
      if (!ParseUnsigned(require_value("--config-baud"), baud))
      {
        std::cerr << "error: invalid --config-baud value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.config_baud = baud;
      continue;
    }
    if (argument == "--family")
    {
      const std::string value = require_value("--family");
      if (ToLowerCopy(value) == "auto")
      {
        cli_options.family_text = "auto";
        cli_options.discover_receiver = true;
        continue;
      }
      cli_options.family_text = value;
      continue;
    }
    if (argument == "--profile")
    {
      cli_options.profile_text = require_value("--profile");
      continue;
    }
    if (argument == "--apply-mode")
    {
      ReceiverAutoConfigApplyMode apply_mode{};
      if (!ParseApplyMode(require_value("--apply-mode"), apply_mode))
      {
        std::cerr << "error: invalid --apply-mode value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.apply_mode = apply_mode;
      continue;
    }
    if (argument == "--persistent")
    {
      cli_options.apply.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
      continue;
    }
    if (argument == "--execute")
    {
      if (cli_options.apply.apply_mode == ReceiverAutoConfigApplyMode::kDryRun)
      {
        cli_options.apply.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
      }
      continue;
    }
    if (argument == "--confirm" || argument == "--yes" ||
        argument == "--confirm-runtime" || argument == "--confirm-persistent")
    {
      cli_options.apply.confirm = true;
      continue;
    }
    if (argument == "--timeout-ms")
    {
      if (!ParseUnsigned(require_value("--timeout-ms"), cli_options.apply.timeout_ms))
      {
        std::cerr << "error: invalid --timeout-ms value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      continue;
    }
    if (argument == "--signal-profile")
    {
      const auto parsed = universal_gnss_driver::ParseReceiverAutoConfigSignalProfile(
          require_value("--signal-profile"));
      if (!parsed.has_value())
      {
        std::cerr << "error: invalid --signal-profile value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.signal_profile = *parsed;
      continue;
    }
    if (argument == "--signal-group")
    {
      const auto parsed = universal_gnss_driver::ParseUnicoreSignalGroupOverride(
          require_value("--signal-group"));
      if (!parsed.has_value())
      {
        std::cerr << "error: invalid --signal-group value (expected one or two "
                     "integers 0-255, e.g. \"2\" or \"3 6\")\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.signal_group_override = *parsed;
      continue;
    }

    if (argument == "--model")
    {
      cli_options.apply.receiver_model = require_value("--model");
      continue;
    }

    if (argument == "--output-port")
    {
      const auto parsed =
          universal_gnss_driver::ParseReceiverAutoConfigOutputPort(
              require_value("--output-port"));
      if (!parsed.has_value())
      {
        std::cerr << "error: invalid --output-port value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      cli_options.apply.output_port = *parsed;
      continue;
    }
    if (argument == "--rate-hz")
    {
      double rate_hz = 0.0;
      if (!ParseDouble(require_value("--rate-hz"), rate_hz))
      {
        std::cerr << "error: invalid --rate-hz value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }
      cli_options.apply.rate_hz = rate_hz;
      continue;
    }

    positional_arguments.push_back(argument);
  }

  if (!positional_arguments.empty() && !cli_options.family_text.has_value())
  {
    cli_options.family_text = positional_arguments.front();
  }
  if (positional_arguments.size() > 1u && !cli_options.profile_text.has_value())
  {
    cli_options.profile_text = positional_arguments[1u];
  }
  if (positional_arguments.size() > 2u)
  {
    std::cerr << "error: unexpected extra argument: " << positional_arguments[2u] << '\n';
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!cli_options.profile_text.has_value())
  {
    std::cerr << "error: --profile is required\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!ParseProfile(*cli_options.profile_text, cli_options.apply.profile))
  {
    std::cerr << "error: unsupported --profile value\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (cli_options.family_text.has_value() && ToLowerCopy(*cli_options.family_text) != "auto")
  {
    if (!ParseFamily(*cli_options.family_text, cli_options.apply.receiver_family))
    {
      std::cerr << "error: unsupported receiver family\n";
      PrintUsage(argv[0]);
      return EXIT_FAILURE;
    }
  }
  else
  {
    cli_options.discover_receiver = true;
  }

  if (cli_options.discover_receiver)
  {
    cli_options.apply.discovery_result = DiscoverRequestedReceiver(cli_options);
  }

  const auto prepared = universal_gnss_tools::PrepareConfigApply(cli_options.apply);
  if (!LiveApplyRequested(cli_options.apply) ||
      prepared.status != universal_gnss_tools::ConfigApplyStatus::kOk)
  {
    PrintResult(prepared, cli_options.json_output);
    return prepared.status == universal_gnss_tools::ConfigApplyStatus::kOk ? EXIT_SUCCESS
                                                                            : EXIT_FAILURE;
  }

#if defined(__linux__)
  if (prepared.device_path.empty())
  {
    auto failed = prepared;
    failed.status = universal_gnss_tools::ConfigApplyStatus::kInvalidArgument;
    failed.error_message =
        "live apply requires a resolved device path; use --device or --receiver auto";
    failed.execution_summary.final_status = "invalid_argument";
    PrintResult(failed, cli_options.json_output);
    return EXIT_FAILURE;
  }

  if (prepared.transport_baud_rate == 0u)
  {
    auto failed = prepared;
    failed.status = universal_gnss_tools::ConfigApplyStatus::kInvalidArgument;
    failed.error_message =
        "live apply requires a resolved transport baud; use --baud <value> or --baud auto";
    failed.execution_summary.final_status = "invalid_argument";
    PrintResult(failed, cli_options.json_output);
    return EXIT_FAILURE;
  }

  PosixSerialTransport transport;
  PosixSerialConfig serial_config;
  serial_config.device_path = prepared.device_path;
  serial_config.baud_rate = prepared.transport_baud_rate;
  serial_config.read_timeout_ms = cli_options.apply.timeout_ms > 100u
                                      ? 100u
                                      : (cli_options.apply.timeout_ms == 0u
                                             ? 1u
                                             : cli_options.apply.timeout_ms);

  const auto open_error = transport.Open(serial_config);
  if (open_error != universal_gnss_transport::TransportError::kNone)
  {
    auto failed = prepared;
    failed.status = universal_gnss_tools::ConfigApplyStatus::kTransportUnavailable;
    failed.error_message = "failed to open configured serial transport";
    failed.execution_summary.final_status = "transport_unavailable";
    PrintResult(failed, cli_options.json_output);
    return EXIT_FAILURE;
  }

  PosixSerialConfigApplyHooks hooks;
  const auto result =
      universal_gnss_tools::ExecuteConfigApply(transport, cli_options.apply, &hooks);
  PrintResult(result, cli_options.json_output);
  return result.status == universal_gnss_tools::ConfigApplyStatus::kOk ? EXIT_SUCCESS
                                                                        : EXIT_FAILURE;
#else
  auto unsupported = prepared;
  unsupported.status = universal_gnss_tools::ConfigApplyStatus::kTransportUnavailable;
  unsupported.error_message = "gnss_config_apply requires Linux POSIX serial support";
  unsupported.execution_summary.final_status = "transport_unavailable";
  PrintResult(unsupported, cli_options.json_output);
  return EXIT_FAILURE;
#endif
}
