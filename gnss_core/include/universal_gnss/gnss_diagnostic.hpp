#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_types.hpp"

namespace universal_gnss
{

enum class GnssDiagnosticSeverity : std::uint8_t
{
  kOk = 0,
  kInfo = 1,
  kWarning = 2,
  kError = 3,
  kStale = 4,
  kUnknown = 5,
};

enum class GnssDiagnosticCategory : std::uint8_t
{
  kRuntime = 0,
  kParser = 1,
  kTransport = 2,
  kCorrection = 3,
  kReceiver = 4,
  kConfiguration = 5,
  kTiming = 6,
};

using GnssDiagnosticCode = std::string;

struct GnssDiagnosticEvent
{
  GnssDiagnosticSeverity severity{GnssDiagnosticSeverity::kUnknown};
  GnssDiagnosticCategory category{GnssDiagnosticCategory::kRuntime};
  GnssDiagnosticCode code{};
  std::string message{};
  std::optional<GnssTimestampNs> timestamp_ns{};
  std::optional<std::string> source{};
};

using GnssDiagnosticEvents = std::vector<GnssDiagnosticEvent>;

constexpr std::uint8_t DiagnosticSeverityRank(GnssDiagnosticSeverity severity)
{
  switch (severity)
  {
    case GnssDiagnosticSeverity::kOk:
      return 0;
    case GnssDiagnosticSeverity::kInfo:
      return 1;
    case GnssDiagnosticSeverity::kUnknown:
      return 2;
    case GnssDiagnosticSeverity::kWarning:
      return 3;
    case GnssDiagnosticSeverity::kStale:
      return 4;
    case GnssDiagnosticSeverity::kError:
      return 5;
  }

  return 0;
}

constexpr GnssDiagnosticSeverity CombineDiagnosticSeverities(
    GnssDiagnosticSeverity lhs,
    GnssDiagnosticSeverity rhs)
{
  return DiagnosticSeverityRank(lhs) >= DiagnosticSeverityRank(rhs) ? lhs : rhs;
}

inline GnssDiagnosticSeverity ComputeOverallDiagnosticSeverity(
    const GnssDiagnosticEvents& events,
    GnssDiagnosticSeverity base_severity = GnssDiagnosticSeverity::kOk)
{
  GnssDiagnosticSeverity overall = base_severity;
  for (const auto& event : events)
  {
    overall = CombineDiagnosticSeverities(overall, event.severity);
  }
  return overall;
}

constexpr bool IsDiagnosticError(GnssDiagnosticSeverity severity)
{
  return severity == GnssDiagnosticSeverity::kError;
}

constexpr bool IsDiagnosticWarningOrWorse(GnssDiagnosticSeverity severity)
{
  switch (severity)
  {
    case GnssDiagnosticSeverity::kWarning:
    case GnssDiagnosticSeverity::kError:
    case GnssDiagnosticSeverity::kStale:
    case GnssDiagnosticSeverity::kUnknown:
      return true;
    case GnssDiagnosticSeverity::kOk:
    case GnssDiagnosticSeverity::kInfo:
      return false;
  }

  return false;
}

constexpr bool IsStaleDiagnosticSeverity(GnssDiagnosticSeverity severity)
{
  return severity == GnssDiagnosticSeverity::kStale;
}

inline bool HasDiagnosticErrors(const GnssDiagnosticEvents& events)
{
  for (const auto& event : events)
  {
    if (IsDiagnosticError(event.severity))
    {
      return true;
    }
  }

  return false;
}

inline bool HasDiagnosticWarnings(const GnssDiagnosticEvents& events)
{
  for (const auto& event : events)
  {
    if (IsDiagnosticWarningOrWorse(event.severity))
    {
      return true;
    }
  }

  return false;
}

inline bool HasStaleDiagnosticEvents(const GnssDiagnosticEvents& events)
{
  for (const auto& event : events)
  {
    if (IsStaleDiagnosticSeverity(event.severity))
    {
      return true;
    }
  }

  return false;
}

}  // namespace universal_gnss
