#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_auto_config.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/config_apply.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverPortSource;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::ReceiverTransportType;
using universal_gnss_driver::TryGetUbxCommandMessageIdentity;
using universal_gnss_tools::ConfigApplyOptions;
using universal_gnss_tools::ConfigApplyStatus;
using universal_gnss_tools::ConfigApplyTransportHooks;
using universal_gnss_tools::ConfigPlanOptions;
using universal_gnss_tools::ExecuteConfigApply;
using universal_gnss_tools::PrepareConfigApply;
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::MemoryByteDuplex;
using universal_gnss_transport::ReadResult;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;
using universal_gnss_transport::WriteResult;

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

ReceiverProbeResult MakeDiscoveryResult(const std::string& path,
                                        const std::uint32_t baud,
                                        const ReceiverDetectedFamily family)
{
  ReceiverProbeResult result;
  result.path = path;
  result.transport_type = ReceiverTransportType::kSerial;
  result.source = ReceiverPortSource::kExplicitPath;
  result.selected_baud = baud;
  result.detected_family = family;
  result.confidence = family == ReceiverDetectedFamily::kNmea ? ReceiverProbeConfidence::kMedium
                                                              : ReceiverProbeConfidence::kHigh;
  result.discovery_score = family == ReceiverDetectedFamily::kNmea ? 20 : 100;
  result.reason = family == ReceiverDetectedFamily::kUblox     ? "valid_ubx_frame:+100"
                  : family == ReceiverDetectedFamily::kUnicore ? "PVTSLNA:+100"
                  : family == ReceiverDetectedFamily::kNmea    ? "valid_GGA:+20"
                                                               : "no_data";
  result.note = result.reason;
  return result;
}

std::vector<std::uint8_t> BuildUbxFrame(std::uint8_t class_id,
                                        std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes;
  bytes.reserve(6u + payload.size() + 2u);
  bytes.push_back(0xB5u);
  bytes.push_back(0x62u);
  bytes.push_back(class_id);
  bytes.push_back(message_id);
  bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu));
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildAckFramesForPlan(
    const universal_gnss_tools::ConfigApplyResult& prepared)
{
  std::vector<std::uint8_t> bytes;
  for (const auto& plan_command : prepared.plan.commands)
  {
    const auto identity = TryGetUbxCommandMessageIdentity(plan_command.command);
    if (!identity.has_value())
    {
      std::cerr << "FAILED: test setup could not derive a UBX command identity\n";
      std::exit(EXIT_FAILURE);
    }

    const auto ack = BuildUbxFrame(0x05u, 0x01u, {identity->class_id, identity->message_id});
    bytes.insert(bytes.end(), ack.begin(), ack.end());
  }

  return bytes;
}

std::string BuildRepeatedUnicoreOkResponses(const std::size_t count)
{
  std::string text;
  for (std::size_t index = 0; index < count; ++index)
  {
    text += "<OK\r\n";
  }
  return text;
}

std::string BuildUnicoreVersionResponse()
{
  return "#VERSIONA,UM982*00\r\n";
}

std::string BuildUnicoreConfigResponse(const std::string& signalgroup,
                                       const std::uint32_t com1_baud = 921600u)
{
  return "$command,CONFIG,response: OK*54\r\n$CONFIG,SIGNALGROUP,CONFIG SIGNALGROUP " +
         signalgroup + "*00\r\n$CONFIG,COM1,CONFIG COM1 " + std::to_string(com1_baud) + "*00\r\n";
}

std::string BuildUnicoreSignalGroupConfigDump(const std::string& signalgroup)
{
  return "$CONFIG,SIGNALGROUP,CONFIG SIGNALGROUP " + signalgroup + "*00\r\n";
}

std::string BuildUnicoreResponsesWithSingleError(const std::size_t count,
                                                 const std::size_t error_index,
                                                 const std::string& error_line)
{
  std::string text;
  for (std::size_t index = 0u; index < count; ++index)
  {
    if (index == error_index)
    {
      text += error_line;
      if (text.empty() || text.back() != '\n')
      {
        text += "\r\n";
      }
      continue;
    }
    text += "<OK\r\n";
  }
  return text;
}

std::optional<std::size_t> FindTextCommandIndex(
    const universal_gnss_tools::ConfigApplyResult& prepared, const std::string& command_text)
{
  for (std::size_t index = 0u; index < prepared.plan.commands.size(); ++index)
  {
    const auto& command = prepared.plan.commands[index];
    if (command.command.payload.kind == universal_gnss_driver::ReceiverCommandPayloadKind::kText &&
        command.command.payload.text.find(command_text) != std::string::npos)
    {
      return index;
    }
  }

  return std::nullopt;
}

bool ContainsProgressLine(const universal_gnss_tools::ConfigApplyResult& result,
                          const std::string& needle)
{
  for (const auto& line : result.progress_log)
  {
    if (line.find(needle) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

std::string ToLowerCopy(std::string text)
{
  std::transform(text.begin(),
                 text.end(),
                 text.begin(),
                 [](const unsigned char c)
                 {
                   return static_cast<char>(std::tolower(c));
                 });
  return text;
}

bool ContainsProgressLineCaseInsensitive(const universal_gnss_tools::ConfigApplyResult& result,
                                         const std::string& needle)
{
  const auto lowered_needle = ToLowerCopy(needle);
  for (const auto& line : result.progress_log)
  {
    if (ToLowerCopy(line).find(lowered_needle) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

constexpr std::array<std::uint32_t, 8u> kUnicoreFactoryResetScanBauds{
    9600u, 19200u, 38400u, 57600u, 115200u, 230400u, 460800u, 921600u};

bool IsUnicoreCom1BaudCommand(const universal_gnss_tools::ConfigPlanCommand& command)
{
  return command.command.payload.kind == universal_gnss_driver::ReceiverCommandPayloadKind::kText &&
         command.command.payload.text.rfind("CONFIG COM1 ", 0u) == 0u;
}

std::size_t CountUnicoreBaudPhaseCommands(const universal_gnss_tools::ConfigApplyResult& prepared)
{
  for (std::size_t index = 0u; index < prepared.plan.commands.size(); ++index)
  {
    if (IsUnicoreCom1BaudCommand(prepared.plan.commands[index]))
    {
      return index + 1u;
    }
  }

  std::cerr << "FAILED: test setup could not find CONFIG COM1 in the prepared Unicore plan\n";
  std::exit(EXIT_FAILURE);
}

std::size_t CountUnicoreProfilePhaseCommands(
    const universal_gnss_tools::ConfigApplyResult& prepared)
{
  const auto baud_phase_commands = CountUnicoreBaudPhaseCommands(prepared);
  return prepared.plan.summary.commands_total - baud_phase_commands;
}

class ScriptedByteDuplex final : public ByteDuplex
{
public:
  explicit ScriptedByteDuplex(std::vector<std::uint8_t> input = {}) : input_(std::move(input))
  {
  }

  ReadResult Read(std::uint8_t* destination, const std::size_t capacity) override
  {
    if (!open_)
    {
      return ReadResult{0u, TransportStatus::kClosed, TransportError::kClosed};
    }

    if (capacity == 0u)
    {
      return ReadResult{};
    }

    if (destination == nullptr)
    {
      return ReadResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
    }

    if (pending_signalgroup_disconnect_)
    {
      const auto bytes_to_copy = std::min(capacity, signalgroup_response_.size());
      std::copy_n(reinterpret_cast<const std::uint8_t*>(signalgroup_response_.data()),
                  static_cast<std::ptrdiff_t>(bytes_to_copy),
                  destination);
      pending_signalgroup_disconnect_ = false;
      stale_after_signalgroup_ = true;
      open_ = false;
      return ReadResult{bytes_to_copy, TransportStatus::kOk, TransportError::kNone};
    }

    if (read_offset_ >= input_.size())
    {
      if (auto_text_ok_responses_)
      {
        static constexpr std::string_view kTextOk = "<OK\r\n";
        const auto bytes_to_copy = std::min(capacity, kTextOk.size());
        std::copy_n(reinterpret_cast<const std::uint8_t*>(kTextOk.data()),
                    static_cast<std::ptrdiff_t>(bytes_to_copy),
                    destination);
        return ReadResult{bytes_to_copy, TransportStatus::kOk, TransportError::kNone};
      }
      return ReadResult{0u, TransportStatus::kEndOfStream, TransportError::kNone};
    }

    const auto available = input_.size() - read_offset_;
    const auto bytes_to_copy = std::min(capacity, available);
    std::copy_n(input_.data() + static_cast<std::ptrdiff_t>(read_offset_),
                static_cast<std::ptrdiff_t>(bytes_to_copy),
                destination);
    read_offset_ += bytes_to_copy;
    return ReadResult{bytes_to_copy, TransportStatus::kOk, TransportError::kNone};
  }

  WriteResult Write(const std::uint8_t* data, const std::size_t size) override
  {
    if (!open_)
    {
      if (stale_after_signalgroup_)
      {
        stale_write_attempted_ = true;
      }
      return WriteResult{0u, TransportStatus::kClosed, TransportError::kClosed};
    }

    if (size == 0u)
    {
      return WriteResult{};
    }

    if (data == nullptr)
    {
      return WriteResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
    }

    written_.insert(written_.end(), data, data + static_cast<std::ptrdiff_t>(size));
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (disconnect_after_signalgroup_response_ &&
        text.find("CONFIG SIGNALGROUP ") != std::string_view::npos)
    {
      pending_signalgroup_disconnect_ = true;
    }
    return WriteResult{size, TransportStatus::kOk, TransportError::kNone};
  }

  bool IsOpen() const override
  {
    return open_;
  }

  void Close() override
  {
    open_ = false;
  }

  void Reopen(std::vector<std::uint8_t> input)
  {
    input_ = std::move(input);
    read_offset_ = 0u;
    open_ = true;
    pending_signalgroup_disconnect_ = false;
    stale_after_signalgroup_ = false;
  }

  void DisconnectAfterSignalGroupResponse()
  {
    disconnect_after_signalgroup_response_ = true;
  }

  void ProvideTextOkResponses()
  {
    auto_text_ok_responses_ = true;
  }

  void SetSignalGroupResponse(std::string response)
  {
    signalgroup_response_ = std::move(response);
  }

  bool stale_write_attempted() const
  {
    return stale_write_attempted_;
  }

  const std::vector<std::uint8_t>& written_bytes() const
  {
    return written_;
  }

private:
  std::vector<std::uint8_t> input_{};
  std::vector<std::uint8_t> written_{};
  std::size_t read_offset_{0u};
  bool open_{true};
  bool disconnect_after_signalgroup_response_{false};
  bool pending_signalgroup_disconnect_{false};
  bool stale_after_signalgroup_{false};
  bool stale_write_attempted_{false};
  bool auto_text_ok_responses_{false};
  std::string signalgroup_response_{"<OK\r\n"};
};

class ScriptedConfigApplyHooks final : public ConfigApplyTransportHooks
{
public:
  struct ProbeStep
  {
    std::string device_path{};
    std::vector<std::uint32_t> baud_candidates{};
    ReceiverProbeResult result{};
  };

  struct ReopenStep
  {
    std::string device_path{};
    std::uint32_t baud_rate{0u};
    std::uint32_t read_timeout_ms{0u};
    std::vector<std::uint8_t> input{};
  };

  explicit ScriptedConfigApplyHooks(ScriptedByteDuplex& transport) : transport_(transport)
  {
  }

  bool ProbeReceiverPath(const std::string& device_path,
                         const std::vector<std::uint32_t>& baud_candidates,
                         const std::uint32_t read_timeout_ms,
                         ReceiverProbeResult& probe_result,
                         std::string& error_message) override
  {
    (void)read_timeout_ms;

    if (probe_index_ >= probe_steps_.size())
    {
      failure_ = "unexpected probe request";
      error_message = failure_;
      return false;
    }

    const auto& expected = probe_steps_[probe_index_++];
    if (device_path != expected.device_path || baud_candidates != expected.baud_candidates)
    {
      failure_ = "probe request did not match the scripted recovery workflow";
      error_message = failure_;
      return false;
    }

    probe_result = expected.result;
    error_message.clear();
    return true;
  }

  bool ReopenTransport(ByteDuplex& transport,
                       const std::string& device_path,
                       const std::uint32_t baud_rate,
                       const std::uint32_t read_timeout_ms,
                       std::string& error_message) override
  {
    if (&transport != &transport_)
    {
      failure_ = "reopen request targeted an unexpected transport instance";
      error_message = failure_;
      return false;
    }

    if (reopen_index_ >= reopen_steps_.size())
    {
      failure_ = "unexpected transport reopen request";
      error_message = failure_;
      return false;
    }

    const auto& expected = reopen_steps_[reopen_index_++];
    if (device_path != expected.device_path || baud_rate != expected.baud_rate ||
        (expected.read_timeout_ms != 0u && read_timeout_ms != expected.read_timeout_ms))
    {
      failure_ = "reopen request did not match the scripted recovery workflow";
      error_message = failure_;
      return false;
    }

    transport_.Reopen(expected.input);
    error_message.clear();
    return true;
  }

  void AddProbeStep(ProbeStep step)
  {
    probe_steps_.push_back(std::move(step));
  }

  void AddReopenStep(ReopenStep step)
  {
    reopen_steps_.push_back(std::move(step));
  }

  bool AllStepsConsumed() const
  {
    return probe_index_ == probe_steps_.size() && reopen_index_ == reopen_steps_.size();
  }

  const std::string& failure() const
  {
    return failure_;
  }

private:
  ScriptedByteDuplex& transport_;
  std::vector<ProbeStep> probe_steps_{};
  std::vector<ReopenStep> reopen_steps_{};
  std::size_t probe_index_{0u};
  std::size_t reopen_index_{0u};
  std::string failure_{};
};

void AddUnicoreFactoryResetScanSteps(ScriptedConfigApplyHooks& hooks,
                                     const std::string& device_path,
                                     const std::optional<std::uint32_t> responsive_baud)
{
  for (const auto baud_rate : kUnicoreFactoryResetScanBauds)
  {
    if (responsive_baud.has_value() && baud_rate == *responsive_baud)
    {
      const std::string version_response = BuildUnicoreVersionResponse();
      hooks.AddReopenStep(
          {device_path,
           baud_rate,
           100u,
           std::vector<std::uint8_t>(version_response.begin(), version_response.end())});
      return;
    }

    hooks.AddReopenStep({device_path, baud_rate, 100u, {}});
  }
}

void AddUm982SignalGroupProfileSteps(ScriptedConfigApplyHooks& hooks,
                                     const std::string& device_path,
                                     const std::uint32_t baud_rate,
                                     const std::string& current_signalgroup,
                                     const std::string& verified_signalgroup,
                                     const std::size_t pre_command_responses,
                                     const std::size_t post_command_responses)
{
  const std::string initial_phase_input =
      BuildUnicoreConfigResponse(current_signalgroup, baud_rate);
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(initial_phase_input.begin(), initial_phase_input.end())});

  const std::string signalgroup_apply_input = "<OK\r\n";
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(signalgroup_apply_input.begin(), signalgroup_apply_input.end())});

  const std::string verification_probe_input = BuildUnicoreVersionResponse();
  hooks.AddReopenStep({device_path,
                       baud_rate,
                       100u,
                       std::vector<std::uint8_t>(verification_probe_input.begin(),
                                                 verification_probe_input.end())});

  const std::string verification_input =
      BuildUnicoreConfigResponse(verified_signalgroup, baud_rate);
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(verification_input.begin(), verification_input.end())});

  const std::string pre_input = BuildRepeatedUnicoreOkResponses(pre_command_responses);
  hooks.AddReopenStep({device_path,
                       baud_rate,
                       100u,
                       std::vector<std::uint8_t>(pre_input.begin(), pre_input.end())});

  const std::string post_input = BuildRepeatedUnicoreOkResponses(post_command_responses);
  hooks.AddReopenStep({device_path,
                       baud_rate,
                       100u,
                       std::vector<std::uint8_t>(post_input.begin(), post_input.end())});
}

void AddUnicoreSignalGroupRecoverySteps(ScriptedConfigApplyHooks& hooks,
                                        const std::string& device_path,
                                        const std::uint32_t baud_rate,
                                        const std::string& verified_signalgroup,
                                        const std::size_t pre_command_responses,
                                        const std::size_t post_command_responses)
{
  const std::string signalgroup_apply_input = "<OK\r\n";
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(signalgroup_apply_input.begin(), signalgroup_apply_input.end())});

  const std::string active_probe_input = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(active_probe_input.begin(), active_probe_input.end())});

  const std::string verification_input = BuildUnicoreSignalGroupConfigDump(verified_signalgroup);
  hooks.AddReopenStep(
      {device_path,
       baud_rate,
       100u,
       std::vector<std::uint8_t>(verification_input.begin(), verification_input.end())});

  const std::string pre_input = BuildRepeatedUnicoreOkResponses(pre_command_responses);
  hooks.AddReopenStep({device_path,
                       baud_rate,
                       100u,
                       std::vector<std::uint8_t>(pre_input.begin(), pre_input.end())});

  const std::string post_input = BuildRepeatedUnicoreOkResponses(post_command_responses);
  hooks.AddReopenStep({device_path,
                       baud_rate,
                       100u,
                       std::vector<std::uint8_t>(post_input.begin(), post_input.end())});
}

void TestDryRunDoesNotWrite(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;

  MemoryByteDuplex transport({});
  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.dry_run &&
                 !result.execute_requested && !result.executed &&
                 result.plan.summary.commands_total == 13u,
             "dry-run auto-config apply should succeed without dispatching commands");
  ctx.Expect(transport.written_bytes().empty(),
             "dry-run auto-config apply must not write to the transport");
}

void TestRuntimeOnlyNoOpNeedsNoConfirmation(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea);
  options.profile = ReceiverAutoConfigProfile::kRuntimeOnly;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;

  const auto prepared = PrepareConfigApply(options);
  ctx.Expect(prepared.status == ConfigApplyStatus::kOk && !prepared.requires_runtime_confirmation &&
                 prepared.plan.summary.commands_total == 0u,
             "runtime_only no-op apply should not require confirmation when no commands exist");

  MemoryByteDuplex closed_transport({});
  closed_transport.Close();
  const auto executed = ExecuteConfigApply(closed_transport, options);
  ctx.Expect(executed.status == ConfigApplyStatus::kOk && !executed.dry_run && executed.executed &&
                 executed.execution_summary.final_status == "ok",
             "runtime_only no-op execution should complete without needing an open transport");
}

void TestRuntimeOnlyRequiresConfirmation(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyACM0", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kSafetyRejected &&
                 result.requires_runtime_confirmation && !result.execution_confirmed &&
                 result.error_message.find("--confirm") != std::string::npos,
             "runtime-only live apply should require explicit operator confirmation");
}

void TestUnknownReceiverRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB99", 9600u, ReceiverDetectedFamily::kUnknown);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kUnsupportedReceiver &&
                 result.plan.unsupported_reason == "no_data",
             "unknown discovery results should be rejected before any live apply");
}

void TestNmeaWriteProfileRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kUnsupportedProfile &&
                 result.plan.error_message.find("runtime_only") != std::string::npos,
             "generic NMEA receivers should reject write-side portable profiles for apply");
}

void TestPersistentRecoveryWorkflowPreparesSuccessfully(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
  options.receiver_model = "UM982";
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.requires_runtime_confirmation &&
                 result.requires_persistent_confirmation && result.execution_confirmed &&
                 result.plan.summary.commands_total == 17u &&
                 result.plan.summary.factory_reset_commands == 1u &&
                 result.plan.summary.persistent_commands == 1u,
             "persistent Unicore apply should prepare a confirmed reset-first recovery workflow");
}

void TestPersistentRecoveryWorkflowWithTargetBaudPreparesSuccessfully(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
  options.receiver_model = "UM982";
  options.config_baud = 460800u;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);
  const std::string text = universal_gnss_tools::FormatConfigApplyText(result);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.plan.baud == std::optional<std::uint32_t>{460800u} &&
                 result.plan.summary.commands_total == 17u &&
                 result.plan.summary.runtime_commands == 15u &&
                 result.plan.summary.persistent_commands == 1u &&
                 result.plan.summary.factory_reset_commands == 1u &&
                 result.plan.commands.size() > 1u &&
                 result.plan.commands[1].command.payload.text.find("CONFIG COM1 460800 8 n 1") !=
                     std::string::npos,
             "persistent Unicore apply should preserve a distinct target config baud override in "
             "the prepared plan");
  ctx.Expect(text.find("Detected receiver baud: 921600") != std::string::npos &&
                 text.find("Current transport baud: 921600") != std::string::npos &&
                 text.find("Factory reset baud: 115200") != std::string::npos &&
                 text.find("Target configured baud: 460800") != std::string::npos &&
                 text.find("Config baud override: 460800") != std::string::npos,
             "prepared Unicore apply text should distinguish detected, current, factory, and "
             "target baud values");
}

void TestSignalProfilePreparationFlowsIntoApplyPlan(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.signal_profile = universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal;
  options.rate_hz = 1.0;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);
  const std::string text = universal_gnss_tools::FormatConfigApplyText(result);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.plan.signal_profile ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile>{
                         universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal} &&
                 result.plan.summary.commands_total == 10u,
             "prepared apply plans should preserve the minimal signal-profile override");
  ctx.Expect(text.find("Signal profile override: minimal") != std::string::npos &&
                 text.find("BESTNAVA 1") != std::string::npos &&
                 text.find("GPGSV") == std::string::npos,
             "prepared apply text should surface the reduced minimal signal-profile command set");
}

void TestRoverPolicyPreparationMatchesPlanSemantics(TestContext& ctx)
{
  ConfigApplyOptions apply_options;
  apply_options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  apply_options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  apply_options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  apply_options.receiver_model = "UM980";
  apply_options.rover_dynamic_mode_override =
      universal_gnss_driver::ReceiverAutoConfigRoverDynamicMode::kSurveyMow;
  apply_options.unicore_rtk_timeout_s_override = 45u;
  apply_options.unicore_dgps_timeout_s_override = 90u;
  apply_options.confirm = true;

  ConfigPlanOptions plan_options;
  plan_options.vendor = "unicore";
  plan_options.profile = "rover_high_precision";
  plan_options.receiver_model = "UM980";
  plan_options.rover_dynamic_mode_override = apply_options.rover_dynamic_mode_override;
  plan_options.unicore_rtk_timeout_s_override = apply_options.unicore_rtk_timeout_s_override;
  plan_options.unicore_dgps_timeout_s_override = apply_options.unicore_dgps_timeout_s_override;

  const auto apply = PrepareConfigApply(apply_options);
  const auto plan = universal_gnss_tools::BuildConfigPlan(plan_options);
  bool commands_match = apply.plan.commands.size() == plan.commands.size();
  if (commands_match)
  {
    for (std::size_t index = 0u; index < plan.commands.size(); ++index)
    {
      if (apply.plan.commands[index].command.payload.kind !=
              plan.commands[index].command.payload.kind ||
          apply.plan.commands[index].command.payload.text !=
              plan.commands[index].command.payload.text ||
          apply.plan.commands[index].command.payload.binary !=
              plan.commands[index].command.payload.binary)
      {
        commands_match = false;
        break;
      }
    }
  }

  ctx.Expect(apply.status == ConfigApplyStatus::kOk &&
                 plan.status == universal_gnss_tools::ConfigPlanStatus::kOk && commands_match,
             "config APPLY and config PLAN should produce equivalent rover policy command "
             "semantics from the same canonical overrides");
}

void TestRuntimeOnlyUnicoreSameBaudOverrideSkipsConfigCom1(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 460800u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.config_baud = 460800u;
  options.signal_group_override = std::vector<std::uint8_t>{3u, 6u};
  options.confirm = true;

  const auto result = PrepareConfigApply(options);
  const std::string text = universal_gnss_tools::FormatConfigApplyText(result);

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.plan.summary.commands_total == 14u &&
                 FindTextCommandIndex(result, "CONFIG COM1 460800 8 n 1\r\n") == std::nullopt &&
                 FindTextCommandIndex(result, "CONFIG SIGNALGROUP 3 6\r\n") != std::nullopt,
             "runtime-only Unicore apply preparation should skip CONFIG COM1 when the requested "
             "config baud already matches the live transport baud");
  ctx.Expect(text.find("Target configured baud:") == std::string::npos,
             "prepared runtime-only apply text should not advertise a target configured baud when "
             "no COM1 baud switch command is planned");
}

void TestRuntimeOnlyUnicoreDifferentBaudOverrideKeepsConfigCom1(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.config_baud = 460800u;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 FindTextCommandIndex(result, "CONFIG COM1 460800 8 n 1\r\n") != std::nullopt,
             "runtime-only Unicore apply preparation should still emit CONFIG COM1 when the "
             "requested config baud differs from the live transport baud");
}

void TestRuntimeOnlyUnicoreExplicitCurrentBaudSkipsConfigCom1WithoutDiscovery(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.receiver_family = ReceiverDetectedFamily::kUnicore;
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.transport_baud_rate = 460800u;
  options.config_baud = 460800u;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 FindTextCommandIndex(result, "CONFIG COM1 460800 8 n 1\r\n") == std::nullopt,
             "runtime-only Unicore apply preparation should skip CONFIG COM1 when explicit "
             "--baud matches --config-baud even without discovery");
}

void TestRuntimeOnlyUnicoreExplicitCurrentBaudEmitsConfigCom1WhenDifferent(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.receiver_family = ReceiverDetectedFamily::kUnicore;
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.transport_baud_rate = 921600u;
  options.config_baud = 460800u;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 FindTextCommandIndex(result, "CONFIG COM1 460800 8 n 1\r\n") != std::nullopt,
             "runtime-only Unicore apply preparation should emit CONFIG COM1 when explicit "
             "--baud differs from --config-baud without discovery");
}

void TestRuntimeOnlyUnicoreUnknownCurrentBaudKeepsConservativeConfigCom1(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.receiver_family = ReceiverDetectedFamily::kUnicore;
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.config_baud = 460800u;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 FindTextCommandIndex(result, "CONFIG COM1 460800 8 n 1\r\n") != std::nullopt,
             "runtime-only Unicore apply preparation should keep CONFIG COM1 when no current "
             "baud source is known");
}

void TestKnownNonBaselineUnicoreModelPreparation(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM981";
  options.confirm = true;

  const auto result = PrepareConfigApply(options);
  const std::string text = universal_gnss_tools::FormatConfigApplyText(result);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.plan.receiver_model == std::optional<std::string>{"UM981"} &&
                 result.plan.summary.commands_total == 13u &&
                 text.find("Receiver model: UM981") != std::string::npos &&
                 text.find("safe generic non-baseline fallback") == std::string::npos,
             "config apply should accept UM981 as a known non-baseline Unicore model without "
             "falling back to the unknown-model path");
}

void TestFactoryResetRecoveryWorkflowPreparesSuccessfully(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kFactoryReset;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(
      result.status == ConfigApplyStatus::kOk && result.requires_runtime_confirmation &&
          result.requires_persistent_confirmation && result.execution_confirmed &&
          result.plan.summary.commands_total == 16u &&
          result.plan.summary.factory_reset_commands == 1u &&
          result.plan.summary.runtime_commands == 15u,
      "factory_reset live apply should prepare the explicit reset/reprobe recovery sequence");
}

void TestUnicoreRuntimeApplyStillWorks(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const std::string responses =
      BuildRepeatedUnicoreOkResponses(prepared.plan.summary.commands_total);
  MemoryByteDuplex transport(std::vector<std::uint8_t>(responses.begin(), responses.end()));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk && !result.dry_run && result.executed &&
                 result.execution_summary.commands_total == 13u &&
                 result.execution_summary.commands_completed == 13u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 13u &&
                 result.execution_summary.final_status == "ok",
             "confirmed runtime-only Unicore apply should complete against the in-memory duplex");
  ctx.Expect(!transport.written_bytes().empty(),
             "runtime-only Unicore apply should write command bytes to the transport");
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());
  ctx.Expect(written.find("VERSIONA\r\n") == std::string::npos,
             "runtime-only Unicore apply should not probe VERSIONA when the current baud already "
             "matches the target baud");
  ctx.Expect(written.find("CONFIG SIGNALGROUP") == std::string::npos,
             "runtime-only Unicore apply should not emit CONFIG SIGNALGROUP without an explicit "
             "override");
}

void TestUnicoreRuntimeApplyReturnsPartialSuccessWhenOptionalOutputFails(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto gpgga_index = FindTextCommandIndex(prepared, "GPGGA 1\r\n");
  if (!gpgga_index.has_value())
  {
    std::cerr << "FAILED: test setup could not find required Unicore commands\n";
    std::exit(EXIT_FAILURE);
  }

  ctx.Expect(!prepared.plan.commands[*gpgga_index].required &&
                 FindTextCommandIndex(prepared, "CONFIG SIGNALGROUP 3 6\r\n") == std::nullopt,
             "prepared runtime-only Unicore plans should keep telemetry optional without a "
             "default SIGNALGROUP command");

  const std::string responses =
      BuildUnicoreResponsesWithSingleError(prepared.plan.summary.commands_total,
                                           *gpgga_index,
                                           "PARSING FAILED GRAMMAR ERROR,*73");
  MemoryByteDuplex transport(std::vector<std::uint8_t>(responses.begin(), responses.end()));

  const auto result = ExecuteConfigApply(transport, options);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kPartialSuccess && result.executed &&
                 result.execution_summary.commands_total == prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_completed ==
                     prepared.plan.summary.commands_total - 1u &&
                 result.execution_summary.commands_failed == 1u &&
                 result.execution_summary.required_commands_failed == 0u &&
                 result.execution_summary.optional_commands_failed == 1u &&
                 result.execution_summary.final_status == "partial_success",
             "optional Unicore output failures should return partial_success instead of aborting");
  ctx.Expect(ContainsProgressLine(result, "failed (optional, continuing)") &&
                 ContainsProgressLineCaseInsensitive(result, "grammar error"),
             "progress log should record optional Unicore output failures while continuing");
  ctx.Expect(written.find("FRESET\r\n") == std::string::npos &&
                 written.find("SAVECONFIG\r\n") == std::string::npos,
             "runtime-only partial-success apply should still avoid FRESET and SAVECONFIG");
}

void TestUnicoreRuntimeApplyStillAbortsWhenCriticalCommandFails(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto mode_index = FindTextCommandIndex(prepared, "MODE ROVER SURVEY MOW\r\n");
  if (!mode_index.has_value())
  {
    std::cerr << "FAILED: test setup could not find MODE ROVER SURVEY MOW\n";
    std::exit(EXIT_FAILURE);
  }

  const std::string responses =
      BuildUnicoreResponsesWithSingleError(prepared.plan.summary.commands_total,
                                           *mode_index,
                                           "PARSING FAILED GRAMMAR ERROR,*73");
  MemoryByteDuplex transport(std::vector<std::uint8_t>(responses.begin(), responses.end()));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kRejected &&
                 result.execution_summary.commands_failed == 1u &&
                 result.execution_summary.required_commands_failed == 1u &&
                 result.execution_summary.optional_commands_failed == 0u &&
                 result.execution_summary.final_status == "rejected",
             "critical Unicore command failures should still abort the apply sequence");
  ctx.Expect(!ContainsProgressLine(result, "failed (optional, continuing)") &&
                 ContainsProgressLine(result, "failed"),
             "critical command failures should be reported as blocking failures");
}

void TestUnicoreRuntimeApplyReturnsPartialSuccessWhenOptionalSignalGroupFails(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.signal_group_override = std::vector<std::uint8_t>{2u, 0u};
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto signalgroup_index = FindTextCommandIndex(prepared, "CONFIG SIGNALGROUP 2 0\r\n");
  if (!signalgroup_index.has_value())
  {
    std::cerr << "FAILED: test setup could not find CONFIG SIGNALGROUP 2 0\n";
    std::exit(EXIT_FAILURE);
  }

  ctx.Expect(!prepared.plan.commands[*signalgroup_index].required,
             "explicit SIGNALGROUP overrides should be optional in runtime-only Unicore plans");

  const std::string initial_input = BuildUnicoreSignalGroupConfigDump("4 5");
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(initial_input.begin(), initial_input.end()));
  transport.ProvideTextOkResponses();
  transport.DisconnectAfterSignalGroupResponse();
  transport.SetSignalGroupResponse("PARSING FAILED GRAMMAR ERROR,*73\r\n");
  ScriptedConfigApplyHooks hooks(transport);

  const std::string rejected_signalgroup_input = "PARSING FAILED GRAMMAR ERROR,*73\r\n";
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       921600u,
                       100u,
                       std::vector<std::uint8_t>(rejected_signalgroup_input.begin(),
                                                 rejected_signalgroup_input.end())});
  const std::string active_probe_input = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(active_probe_input.begin(), active_probe_input.end())});
  const std::string verification_input = BuildUnicoreSignalGroupConfigDump("4 5");
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(verification_input.begin(), verification_input.end())});
  const std::string pre_input = BuildRepeatedUnicoreOkResponses(*signalgroup_index);
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       921600u,
                       100u,
                       std::vector<std::uint8_t>(pre_input.begin(), pre_input.end())});
  const std::string post_input = BuildRepeatedUnicoreOkResponses(
      prepared.plan.summary.commands_total - *signalgroup_index - 1u);
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       921600u,
                       100u,
                       std::vector<std::uint8_t>(post_input.begin(), post_input.end())});

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kPartialSuccess && result.executed &&
                 result.execution_summary.commands_total == prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_completed ==
                     prepared.plan.summary.commands_total - 1u &&
                 result.execution_summary.commands_failed == 1u &&
                 result.execution_summary.required_commands_failed == 0u &&
                 result.execution_summary.optional_commands_failed == 1u &&
                 result.execution_summary.final_status == "partial_success",
             "optional SIGNALGROUP overrides should not abort the apply sequence when the "
             "receiver rejects them");
  ctx.Expect(written.find("CONFIG SIGNALGROUP 2 0\r\n") != std::string::npos &&
                 written.find("CONFIG\r\n") != std::string::npos &&
                 written.find("GPGGA 1\r\n") != std::string::npos && hooks.AllStepsConsumed() &&
                 hooks.failure().empty() && !transport.stale_write_attempted() &&
                 ContainsProgressLine(result, "failed (optional, continuing)"),
             "optional SIGNALGROUP rejection should continue only after the dedicated recovery "
             "and verification boundary");
}

void TestUnicoreRuntimeSignalGroupOverrideUsesRecoveryBoundary(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.signal_group_override = std::vector<std::uint8_t>{2u, 0u};
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto signalgroup_index = FindTextCommandIndex(prepared, "CONFIG SIGNALGROUP 2 0\r\n");
  const auto gpgga_index = FindTextCommandIndex(prepared, "GPGGA 1\r\n");
  if (!signalgroup_index.has_value() || !gpgga_index.has_value())
  {
    std::cerr << "FAILED: test setup could not find the runtime SIGNALGROUP/GPGGA commands\n";
    std::exit(EXIT_FAILURE);
  }

  const std::string initial_input = BuildUnicoreSignalGroupConfigDump("4 5");
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(initial_input.begin(), initial_input.end()));
  transport.ProvideTextOkResponses();
  transport.DisconnectAfterSignalGroupResponse();
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreSignalGroupRecoverySteps(hooks,
                                     "/dev/ttyUSB0",
                                     921600u,
                                     "2 0",
                                     *signalgroup_index,
                                     prepared.plan.summary.commands_total - *signalgroup_index -
                                         1u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());
  const auto signalgroup_position = written.find("CONFIG SIGNALGROUP 2 0\r\n");
  const auto gpgga_position = written.find("GPGGA 1\r\n");

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.executed &&
                 result.execution_summary.commands_completed ==
                     prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_failed == 0u,
             "accepted runtime SIGNALGROUP override should complete only after recovery and "
             "verification (status=" +
                 std::to_string(static_cast<int>(result.status)) +
                 ", error=" + result.error_message + ")");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 !transport.stale_write_attempted() && signalgroup_position != std::string::npos &&
                 gpgga_position != std::string::npos && signalgroup_position < gpgga_position,
             "post-SIGNALGROUP output commands must wait for the reopened, verified transport "
             "(hooks=" +
                 std::to_string(hooks.AllStepsConsumed()) + ", hook_failure=" + hooks.failure() +
                 ", stale=" + std::to_string(transport.stale_write_attempted()) + ")");
}

void TestUnicoreRuntimeSignalGroupVerificationFailureStopsProfilePhase(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.signal_group_override = std::vector<std::uint8_t>{2u, 0u};
  options.confirm = true;

  const std::string initial_input = BuildUnicoreSignalGroupConfigDump("4 5");
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(initial_input.begin(), initial_input.end()));
  transport.ProvideTextOkResponses();
  transport.DisconnectAfterSignalGroupResponse();
  ScriptedConfigApplyHooks hooks(transport);

  const std::string signalgroup_apply_input = "<OK\r\n";
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(signalgroup_apply_input.begin(), signalgroup_apply_input.end())});
  const std::string active_probe_input = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(active_probe_input.begin(), active_probe_input.end())});
  const std::string missing_signalgroup_input = "$CONFIG,COM1,CONFIG COM1 921600 8 n 1*00\r\n";
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       921600u,
                       100u,
                       std::vector<std::uint8_t>(missing_signalgroup_input.begin(),
                                                 missing_signalgroup_input.end())});

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kRejected && result.executed &&
                 result.error_message.find("did not report CONFIG SIGNALGROUP") !=
                     std::string::npos,
             "failed SIGNALGROUP verification must terminate the apply safely");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 !transport.stale_write_attempted() &&
                 written.find("GPGGA 1\r\n") == std::string::npos,
             "failed recovery/reprobe must prevent every post-SIGNALGROUP profile command");
}

void TestUnicoreRuntimeApplySwitchesToTargetBaudWhenConfigCom1BecomesLive(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 115200u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM981";
  options.config_baud = 921600u;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto baud_phase_commands = CountUnicoreBaudPhaseCommands(prepared);
  const auto profile_phase_commands = CountUnicoreProfilePhaseCommands(prepared);
  const std::string first_phase_responses = BuildRepeatedUnicoreOkResponses(baud_phase_commands);
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(first_phase_responses.begin(), first_phase_responses.end()));
  ScriptedConfigApplyHooks hooks(transport);
  hooks.AddReopenStep({"/dev/ttyUSB0", 115200u, 100u, {}});
  const std::string target_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(target_probe_response.begin(), target_probe_response.end())});
  const std::string target_profile_responses =
      BuildRepeatedUnicoreOkResponses(profile_phase_commands);
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       921600u,
                       100u,
                       std::vector<std::uint8_t>(target_profile_responses.begin(),
                                                 target_profile_responses.end())});

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.transport_baud_rate == 921600u &&
                 result.execution_summary.commands_total == prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_completed ==
                     prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.final_status == "ok",
             "runtime-only Unicore apply should continue at the target baud when CONFIG COM1 "
             "becomes active live");
  ctx.Expect(
      hooks.AllStepsConsumed() && hooks.failure().empty() &&
          written.find("CONFIG COM1 921600 8 n 1\r\n") != std::string::npos &&
          written.find("VERSIONA\r\n") != std::string::npos &&
          written.find("SAVECONFIG\r\n") == std::string::npos,
      "runtime-only Unicore live baud-switch recovery should probe VERSIONA and avoid SAVECONFIG");
}

void TestUnicoreRuntimeApplyContinuesAtOldBaudWhenConfigCom1DoesNotSwitchLive(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 115200u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM980";
  options.config_baud = 921600u;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto baud_phase_commands = CountUnicoreBaudPhaseCommands(prepared);
  const auto profile_phase_commands = CountUnicoreProfilePhaseCommands(prepared);
  const std::string first_phase_responses = BuildRepeatedUnicoreOkResponses(baud_phase_commands);
  const std::string old_probe_response = BuildUnicoreVersionResponse();
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(first_phase_responses.begin(), first_phase_responses.end()));
  ScriptedConfigApplyHooks hooks(transport);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(old_probe_response.begin(), old_probe_response.end())});
  hooks.AddReopenStep({"/dev/ttyUSB0", 921600u, 100u, {}});
  const std::string remaining_profile_responses =
      BuildRepeatedUnicoreOkResponses(profile_phase_commands);
  hooks.AddReopenStep({"/dev/ttyUSB0",
                       115200u,
                       100u,
                       std::vector<std::uint8_t>(remaining_profile_responses.begin(),
                                                 remaining_profile_responses.end())});

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());
  bool warning_found = false;
  for (const auto& warning : result.plan.warnings)
  {
    if (warning.find("did not become active live") != std::string::npos)
    {
      warning_found = true;
      break;
    }
  }

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.transport_baud_rate == 115200u &&
                 result.execution_summary.commands_total == prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_completed ==
                     prepared.plan.summary.commands_total &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.final_status == "ok",
             "runtime-only Unicore apply should continue at the old baud when CONFIG COM1 does not "
             "switch the live transport");
  ctx.Expect(
      hooks.AllStepsConsumed() && hooks.failure().empty() && warning_found &&
          written.find("CONFIG COM1 921600 8 n 1\r\n") != std::string::npos &&
          written.find("VERSIONA\r\n") != std::string::npos &&
          written.find("SAVECONFIG\r\n") == std::string::npos,
      "runtime-only Unicore fallback should warn, keep the old live baud, and avoid SAVECONFIG");
}

void TestUnicoreRuntimeApplyFailsFastWhenNeitherBaudRespondsAfterConfigCom1(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 115200u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM960";
  options.config_baud = 921600u;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const auto baud_phase_commands = CountUnicoreBaudPhaseCommands(prepared);
  const std::string first_phase_responses = BuildRepeatedUnicoreOkResponses(baud_phase_commands);
  ScriptedByteDuplex transport(
      std::vector<std::uint8_t>(first_phase_responses.begin(), first_phase_responses.end()));
  ScriptedConfigApplyHooks hooks(transport);
  hooks.AddReopenStep({"/dev/ttyUSB0", 115200u, 100u, {}});
  hooks.AddReopenStep({"/dev/ttyUSB0", 921600u, 100u, {}});
  hooks.AddReopenStep({"/dev/ttyUSB0", 115200u, 100u, {}});
  hooks.AddReopenStep({"/dev/ttyUSB0", 921600u, 100u, {}});
  hooks.AddReopenStep({"/dev/ttyUSB0", 115200u, 100u, {}});
  hooks.AddReopenStep({"/dev/ttyUSB0", 921600u, 100u, {}});

  const auto result = ExecuteConfigApply(transport, options, &hooks);

  ctx.Expect(result.status == ConfigApplyStatus::kTransportUnavailable &&
                 result.execution_summary.final_status == "transport_unavailable" &&
                 result.error_message.find("no receiver response on probed baud rates") !=
                     std::string::npos,
             "runtime-only Unicore apply should fail fast when neither the old nor target baud "
             "responds after CONFIG COM1");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty(),
             "bounded neither-baud-responsive test should consume the scripted reopen attempts "
             "without looping indefinitely");
}

void TestUnicoreFactoryResetRecoveryApplyWorks(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kFactoryReset;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", 115200u);
  const std::string first_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(first_probe_response.begin(), first_probe_response.end())});
  const std::string baud_recovery_responses = BuildRepeatedUnicoreOkResponses(1u);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(baud_recovery_responses.begin(), baud_recovery_responses.end())});
  const std::string second_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(second_probe_response.begin(), second_probe_response.end())});
  AddUm982SignalGroupProfileSteps(hooks, "/dev/ttyUSB0", 921600u, "4 5", "3 6", 5u, 8u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_total == 16u &&
                 result.execution_summary.commands_completed == 16u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 14u &&
                 result.execution_summary.final_status == "ok",
             "factory_reset live apply should complete across the reset/reprobe recovery workflow");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 written.find("VERSIONA\r\n") != std::string::npos &&
                 written.find("FRESET\r\n") != std::string::npos &&
                 written.find("VERSIONA\r\n") < written.find("FRESET\r\n") &&
                 written.find("CONFIG COM1 921600 8 n 1\r\n") != std::string::npos &&
                 written.find("CONFIG SIGNALGROUP 3 6\r\n") != std::string::npos &&
                 written.find("SAVECONFIG") == std::string::npos,
             "factory_reset recovery apply should write reset and COM1 recovery commands without "
             "persisting the temporary rover profile");
}

void TestUnicoreFactoryResetPreflightScanFinds38400BeforeSendingFreset(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kFactoryReset;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", 38400u);
  const std::string first_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(first_probe_response.begin(), first_probe_response.end())});
  const std::string baud_recovery_responses = BuildRepeatedUnicoreOkResponses(1u);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(baud_recovery_responses.begin(), baud_recovery_responses.end())});
  const std::string second_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(second_probe_response.begin(), second_probe_response.end())});
  AddUm982SignalGroupProfileSteps(hooks, "/dev/ttyUSB0", 921600u, "4 5", "3 6", 5u, 8u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_completed == 16u,
             "factory_reset preflight should support receivers that are live at 38400 bps");
  ctx.Expect(
      hooks.AllStepsConsumed() && hooks.failure().empty() &&
          written.find("VERSIONA\r\n") != std::string::npos &&
          written.find("FRESET\r\n") != std::string::npos &&
          written.find("VERSIONA\r\n") < written.find("FRESET\r\n"),
      "factory_reset preflight scan should find 38400 in the fixed baud order before FRESET");
}

void TestUnicoreFactoryResetPreflightScanFinds921600BeforeSendingFreset(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 115200u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kFactoryReset;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", 921600u);
  const std::string first_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(first_probe_response.begin(), first_probe_response.end())});
  const std::string baud_recovery_responses = BuildRepeatedUnicoreOkResponses(1u);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(baud_recovery_responses.begin(), baud_recovery_responses.end())});
  const std::string second_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(second_probe_response.begin(), second_probe_response.end())});
  AddUm982SignalGroupProfileSteps(hooks, "/dev/ttyUSB0", 921600u, "4 5", "3 6", 5u, 8u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_completed == 16u,
             "factory_reset preflight should support receivers that are live at 921600 bps");
  ctx.Expect(
      hooks.AllStepsConsumed() && hooks.failure().empty() &&
          written.find("VERSIONA\r\n") != std::string::npos &&
          written.find("FRESET\r\n") != std::string::npos &&
          written.find("VERSIONA\r\n") < written.find("FRESET\r\n"),
      "factory_reset preflight scan should reach 921600 in the fixed baud order before FRESET");
}

void TestUnicoreFactoryResetPreflightAbortWhenNoBaudResponds(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kFactoryReset;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.receiver_model = "UM982";
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", std::nullopt);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kTransportUnavailable &&
                 result.execution_summary.commands_completed == 0u &&
                 result.execution_summary.final_status == "transport_unavailable" &&
                 result.error_message ==
                     "receiver did not answer VERSIONA on any Unicore baud; factory reset not sent",
             "factory_reset preflight should abort cleanly when no known Unicore baud answers");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 written.find("VERSIONA\r\n") != std::string::npos &&
                 written.find("FRESET\r\n") == std::string::npos,
             "factory_reset preflight should never send FRESET before a successful VERSIONA scan");
}

void TestUnicorePersistentApplyWorksThroughRecoveryWorkflow(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
  options.receiver_model = "UM982";
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", 115200u);
  const std::string first_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(first_probe_response.begin(), first_probe_response.end())});
  const std::string baud_recovery_responses = BuildRepeatedUnicoreOkResponses(1u);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(baud_recovery_responses.begin(), baud_recovery_responses.end())});
  const std::string second_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       921600u,
       100u,
       std::vector<std::uint8_t>(second_probe_response.begin(), second_probe_response.end())});
  AddUm982SignalGroupProfileSteps(hooks, "/dev/ttyUSB0", 921600u, "4 5", "3 6", 5u, 9u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(
      result.status == ConfigApplyStatus::kOk && result.execution_summary.commands_total == 17u &&
          result.execution_summary.commands_completed == 17u &&
          result.execution_summary.commands_failed == 0u &&
          result.execution_summary.responses_applied == 15u &&
          result.execution_summary.final_status == "ok",
      "persistent Unicore apply should complete across reset, baud recovery, and SAVECONFIG");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 written.find("FRESET\r\n") != std::string::npos &&
                 written.find("VERSIONA\r\n") != std::string::npos &&
                 written.find("CONFIG COM1 921600 8 n 1\r\n") != std::string::npos &&
                 written.find("CONFIG SIGNALGROUP 3 6\r\n") != std::string::npos &&
                 written.find("SAVECONFIG\r\n") != std::string::npos,
             "persistent Unicore recovery apply should restore COM1, replay the rover profile, and "
             "save it");
}

void TestUnicorePersistentApplyUsesOverriddenTargetBaud(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
  options.receiver_model = "UM982";
  options.config_baud = 460800u;
  options.confirm = true;

  ScriptedByteDuplex transport({});
  ScriptedConfigApplyHooks hooks(transport);
  AddUnicoreFactoryResetScanSteps(hooks, "/dev/ttyUSB0", 115200u);
  const std::string first_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(first_probe_response.begin(), first_probe_response.end())});
  const std::string baud_recovery_responses = BuildRepeatedUnicoreOkResponses(1u);
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       115200u,
       100u,
       std::vector<std::uint8_t>(baud_recovery_responses.begin(), baud_recovery_responses.end())});
  const std::string second_probe_response = BuildUnicoreVersionResponse();
  hooks.AddReopenStep(
      {"/dev/ttyUSB0",
       460800u,
       100u,
       std::vector<std::uint8_t>(second_probe_response.begin(), second_probe_response.end())});
  AddUm982SignalGroupProfileSteps(hooks, "/dev/ttyUSB0", 460800u, "4 5", "3 6", 5u, 9u);

  const auto result = ExecuteConfigApply(transport, options, &hooks);
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());

  ctx.Expect(result.status == ConfigApplyStatus::kOk && result.transport_baud_rate == 460800u &&
                 result.execution_summary.commands_total == 17u &&
                 result.execution_summary.commands_completed == 17u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 15u &&
                 result.execution_summary.final_status == "ok",
             "persistent Unicore apply should finish at the overridden target config baud");
  ctx.Expect(hooks.AllStepsConsumed() && hooks.failure().empty() &&
                 written.find("CONFIG COM1 460800 8 n 1\r\n") != std::string::npos &&
                 written.find("CONFIG COM1 921600 8 n 1\r\n") == std::string::npos &&
                 written.find("SAVECONFIG\r\n") != std::string::npos,
             "persistent Unicore recovery apply should switch COM1 to the overridden target baud "
             "before saving");
}

void TestUbloxRuntimeApplyStillWorks(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  MemoryByteDuplex transport(BuildAckFramesForPlan(prepared));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_total == 13u &&
                 result.execution_summary.commands_completed == 13u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 13u &&
                 result.execution_summary.final_status == "ok",
             "confirmed runtime-only u-blox apply should complete through the UBX router path");
  ctx.Expect(!transport.written_bytes().empty(),
             "runtime-only u-blox apply should write the planned UBX commands");
  const std::string written(transport.written_bytes().begin(), transport.written_bytes().end());
  ctx.Expect(written.find("VERSIONA\r\n") == std::string::npos,
             "non-Unicore runtime apply should not use Unicore VERSIONA probing assumptions");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDryRunDoesNotWrite(ctx);
  TestRuntimeOnlyNoOpNeedsNoConfirmation(ctx);
  TestRuntimeOnlyRequiresConfirmation(ctx);
  TestUnknownReceiverRejected(ctx);
  TestNmeaWriteProfileRejected(ctx);
  TestPersistentRecoveryWorkflowPreparesSuccessfully(ctx);
  TestPersistentRecoveryWorkflowWithTargetBaudPreparesSuccessfully(ctx);
  TestSignalProfilePreparationFlowsIntoApplyPlan(ctx);
  TestRoverPolicyPreparationMatchesPlanSemantics(ctx);
  TestRuntimeOnlyUnicoreSameBaudOverrideSkipsConfigCom1(ctx);
  TestRuntimeOnlyUnicoreDifferentBaudOverrideKeepsConfigCom1(ctx);
  TestRuntimeOnlyUnicoreExplicitCurrentBaudSkipsConfigCom1WithoutDiscovery(ctx);
  TestRuntimeOnlyUnicoreExplicitCurrentBaudEmitsConfigCom1WhenDifferent(ctx);
  TestRuntimeOnlyUnicoreUnknownCurrentBaudKeepsConservativeConfigCom1(ctx);
  TestKnownNonBaselineUnicoreModelPreparation(ctx);
  TestFactoryResetRecoveryWorkflowPreparesSuccessfully(ctx);
  TestUnicoreRuntimeApplyStillWorks(ctx);
  TestUnicoreRuntimeApplyReturnsPartialSuccessWhenOptionalOutputFails(ctx);
  TestUnicoreRuntimeApplyStillAbortsWhenCriticalCommandFails(ctx);
  TestUnicoreRuntimeApplyReturnsPartialSuccessWhenOptionalSignalGroupFails(ctx);
  TestUnicoreRuntimeSignalGroupOverrideUsesRecoveryBoundary(ctx);
  TestUnicoreRuntimeSignalGroupVerificationFailureStopsProfilePhase(ctx);
  TestUnicoreRuntimeApplySwitchesToTargetBaudWhenConfigCom1BecomesLive(ctx);
  TestUnicoreRuntimeApplyContinuesAtOldBaudWhenConfigCom1DoesNotSwitchLive(ctx);
  TestUnicoreRuntimeApplyFailsFastWhenNeitherBaudRespondsAfterConfigCom1(ctx);
  TestUnicoreFactoryResetRecoveryApplyWorks(ctx);
  TestUnicoreFactoryResetPreflightScanFinds38400BeforeSendingFreset(ctx);
  TestUnicoreFactoryResetPreflightScanFinds921600BeforeSendingFreset(ctx);
  TestUnicoreFactoryResetPreflightAbortWhenNoBaudResponds(ctx);
  TestUnicorePersistentApplyWorksThroughRecoveryWorkflow(ctx);
  TestUnicorePersistentApplyUsesOverriddenTargetBaud(ctx);
  TestUbloxRuntimeApplyStillWorks(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools config apply tests passed\n";
  return EXIT_SUCCESS;
}
