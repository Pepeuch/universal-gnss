#pragma once

#include "universal_gnss_ntrip/gga_sentence_builder.hpp"

namespace universal_gnss_ntrip
{

using GgaGenerationError = GgaSentenceBuildError;
using GgaGenerationResult = GgaSentenceBuildResult;

inline GgaGenerationResult GenerateGgaFromRuntimeState(
    const universal_gnss::GnssRuntimeState& state)
{
  return BuildNmeaGgaSentence(state);
}

}  // namespace universal_gnss_ntrip
