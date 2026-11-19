// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
#include "ug_telemetry_operation.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include <cassert>
#include <set>
using namespace universal_gnss_driver;
int main()
{
  using Family = ReceiverDetectedFamily;
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kNmea, "ZED-F9P") == Family::kUblox);
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kUnknown, "ZED-F9P") == Family::kUblox);
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kNmea, "UM982") == Family::kUnicore);
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kNmea, "auto") == Family::kNmea);
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kNmea, "NMEA") == Family::kNmea);
  assert(ug::embedded::SelectReadOnlyQueryFamily(Family::kUnicore, "ZED-F9P") == Family::kUnicore);
  assert(ug::embedded::InspectReceiverPlans("ZED-F9P", true).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("ZED-F9P", false).disable_base);
  assert(ug::embedded::InspectReceiverPlans("UM982", true).disable_base);
  assert(ug::embedded::InspectReceiverPlans("UM980", true).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("UM982", false).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("UM981", true).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("NMEA", true).disable_base);

  // A restart is a bounded runtime command, never a cold/factory reset or retry.
  std::string restart_error;
  assert(ug::embedded::DecodeReceiverRestart(R"({"confirm":"RESTART_GNSS"})", restart_error));
  for (auto text : {R"({})",
                    R"({"confirm":"REBOOT_ESP32"})",
                    R"({"confirm":"RESTART_GNSS","cold":true})",
                    R"({"confirm":"RESTART_GNSS","raw":"FRESET"})"})
    assert(!ug::embedded::DecodeReceiverRestart(text, restart_error));
  for (auto model : {"UM980", "UM982", "NMEA", "ZED-F9P"})
    assert(!ug::embedded::BuildReceiverRestart(model, false).ok());
  assert(!ug::embedded::BuildReceiverRestart("UM982", true).ok());
  auto restart = ug::embedded::BuildReceiverRestart("ZED-F9P", true);
  assert(restart.ok() && restart.commands.size() == 1);
  const auto& reset = restart.commands[0];
  const std::vector<std::uint8_t> hot_gnss{0xb5, 0x62, 6, 4, 4, 0, 0, 0, 2, 0, 0x10, 0x68};
  assert(reset.payload.binary == hot_gnss);
  assert(reset.kind == ReceiverCommandKind::kReset && reset.explicit_safety_confirmation);
  assert(reset.safety_level == ReceiverCommandSafetyLevel::kRuntime);
  assert(reset.expected_response == ReceiverResponseKind::kNone && !reset.retry_policy.max_retries);
  universal_gnss_transport::MemoryByteSink restart_sink;
  ug::embedded::Receiver restarting(restart_sink);
  assert(restarting.Start(restart.commands, 1, ug::embedded::CommandPurpose::kRestart));
  restarting.Step(2);
  assert(restart_sink.written_bytes() == hot_gnss);
  assert(restarting.application().state() == ReceiverConfigApplicationState::kCompleted);
  restarting.Step(10000000000LL);
  assert(restart_sink.written_bytes() == hot_gnss);  // No ACK wait and no resend.
  assert(!restarting.indeterminate());

  ug::embedded::TelemetryOperation op, before;
  std::string error;
  assert(ug::embedded::DecodeDetectionOperation(R"({"confirm":"DETECT_RECEIVER"})", error));
  for (auto text : {R"({})",
                    R"({"confirm":"RESET_RECEIVER"})",
                    R"({"confirm":"DETECT_RECEIVER","raw":"FRESET"})"})
    assert(!ug::embedded::DecodeDetectionOperation(text, error));
  const auto f9p = ug::embedded::InspectReceiverPlans("ZED-F9P", true);
  assert(f9p.telemetry && f9p.fixed && f9p.survey_in && !f9p.time_optimization);
  for (auto model : {"UM980", "UM982"})
  {
    const auto unicore = ug::embedded::InspectReceiverPlans(model, true);
    assert(unicore.telemetry && unicore.fixed && !unicore.survey_in && unicore.time_optimization);
  }
  const auto um981 = ug::embedded::InspectReceiverPlans("UM981", true);
  assert(um981.telemetry && !um981.fixed && !um981.survey_in && !um981.time_optimization);
  assert(!ug::embedded::InspectReceiverPlans("ZED-F9P", false).fixed);
  assert(!ug::embedded::InspectReceiverPlans("NMEA", true).telemetry);
  assert(ug::embedded::DecodeTelemetryOperation(
      R"({"rate_hz":5,"confirm":"APPLY_GNSS_TELEMETRY"})", op, error));
  for (const auto* model : {"ZED-F9P", "UM980", "UM981", "UM982"})
  {
    auto plan = ug::embedded::BuildTelemetryPlan(op, model, true);
    assert(plan.ok());
    universal_gnss_transport::MemoryByteSink sink;
    ug::embedded::Receiver receiver(sink);
    assert(receiver.Start(plan.commands, 1));
    for (const auto& command : plan.commands)
    {
      assert(HasSafeDispatchApproval(command) &&
             command.failure_policy == ReceiverCommandFailurePolicy::kAbortOnFailure);
      if (command.payload.kind == ReceiverCommandPayloadKind::kText)
      {
        assert(command.payload.text.find("MODE ") == std::string::npos);
        assert(command.payload.text.find("RESET") == std::string::npos);
        assert(command.payload.text.find("CONFIG COM") == std::string::npos);
        assert(command.payload.text.find("CONFIG SIGNALGROUP") == std::string::npos);
        assert(command.payload.text.find("SAVECONFIG") == std::string::npos);
      }
    }
  }
  assert(!ug::embedded::BuildTelemetryPlan(op, "UM980", false).ok());
  assert(!ug::embedded::BuildTelemetryPlan(op, "generic", true).ok());
  // Only explicit constellation changes generate signal enable keys; no
  // convenience ROVER profile may silently change a BASE or its constellations.
  const auto plain = ug::embedded::BuildTelemetryPlan(op, "ZED-F9P", true);
  for (const auto& command : plain.commands)
  {
    universal_gnss_protocols::UbxFrameFramer framer;
    universal_gnss_protocols::ParserResult<universal_gnss_protocols::UbxFrame> framed;
    for (auto byte : command.payload.binary)
      framed = framer.PushByte(byte);
    assert(framed.record && framed.record->class_id == 6 && framed.record->message_id == 0x8a);
    const auto& p = framed.record->payload;
    const auto key = std::uint32_t(p[4]) | (std::uint32_t(p[5]) << 8) |
                     (std::uint32_t(p[6]) << 16) | (std::uint32_t(p[7]) << 24);
    assert((key >> 16) != 0x1031);  // No implicit constellation switch.
  }
  op.signal_groups[0] = 2;
  op.signal_group_count = 1;
  for (const auto* model : {"UM980", "UM981", "UM982", "ZED-F9P"})
    for (bool persist : {false, true})
    {
      op.persist = persist;
      op.confirm_persistent = true;
      auto blocked = ug::embedded::BuildTelemetryPlan(op, model, true);
      assert(!blocked.ok() && blocked.commands.empty());
      assert(blocked.error.find("automatic persistence/reset") != std::string::npos);
      universal_gnss_transport::MemoryByteSink sink;
      ug::embedded::Receiver receiver(sink);
      assert(!receiver.Start(blocked.commands, 1) && sink.written_bytes().empty());
    }
  op.signal_groups[0] = 255;
  assert(!ug::embedded::BuildTelemetryPlan(op, "UM980", true).ok());
  op = before;
  op.set_constellations = true;
  op.constellations[0] = true;
  assert(ug::embedded::BuildTelemetryPlan(op, "ZED-F9P", true).ok());
  assert(!ug::embedded::BuildTelemetryPlan(op, "UM982", true).ok());
  op = before;
  op.persist = true;
  assert(!ug::embedded::BuildTelemetryPlan(op, "ZED-F9P", true).ok());
  op.confirm_persistent = true;
  auto persisted = ug::embedded::BuildTelemetryPlan(op, "UM982", true);
  assert(persisted.ok() && persisted.commands.back().payload.text == "SAVECONFIG\r\n" &&
         HasSafeDispatchApproval(persisted.commands.back()));
  for (const auto* bad : {R"({})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","rate_hz":3})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","rate_hz":1.5})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","signal_groups":[1,1]})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","persist":true})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","raw":"MODE ROVER"})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","receiver_port":0})",
                          R"({"confirm":"APPLY_GNSS_TELEMETRY","constellations":{"gps":true}})"})
  {
    op = before;
    assert(!ug::embedded::DecodeTelemetryOperation(bad, op, error));
    assert(op.rate_hz == before.rate_hz && !op.persist);
  }
}
