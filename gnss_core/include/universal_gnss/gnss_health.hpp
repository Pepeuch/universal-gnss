#pragma once

#include <utility>

#include "universal_gnss/gnss_diagnostic.hpp"

namespace universal_gnss
{

struct GnssHealthSummary
{
  GnssDiagnosticSeverity overall_severity{GnssDiagnosticSeverity::kUnknown};

  bool fix_valid{false};
  bool rtk_available{false};
  bool correction_available{false};
  bool receiver_healthy{false};
  bool transport_healthy{false};
  bool parser_healthy{false};
  bool stale_data{false};

  GnssDiagnosticEvents events{};

  void AddEvent(GnssDiagnosticEvent event)
  {
    events.emplace_back(std::move(event));
    overall_severity = ComputeOverallDiagnosticSeverity(events);
    stale_data = stale_data || IsStaleDiagnosticSeverity(events.back().severity);
  }

  bool HasErrors() const
  {
    return HasDiagnosticErrors(events);
  }

  bool HasWarnings() const
  {
    return HasDiagnosticWarnings(events);
  }

  void Clear()
  {
    *this = GnssHealthSummary{};
  }
};

}  // namespace universal_gnss
