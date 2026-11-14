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
  assert(ug::embedded::InspectReceiverPlans("ZED-F9P", true).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("ZED-F9P", false).disable_base);
  assert(!ug::embedded::InspectReceiverPlans("UM982", true).disable_base);

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
  assert(ug::embedded::BuildTelemetryPlan(op, "UM980", true).ok());
  assert(
      !ug::embedded::BuildTelemetryPlan(op, "UM981", true).ok());  // Model has no qualified groups.
  assert(!ug::embedded::BuildTelemetryPlan(op, "ZED-F9P", true).ok());
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
