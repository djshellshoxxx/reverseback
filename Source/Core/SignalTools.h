#pragma once

#include "AudioClip.h"

namespace reverseback
{
[[nodiscard]] Selection findNonSilentSelection(const AudioClip& clip,
                                               double thresholdDb,
                                               double windowSeconds,
                                               double paddingSeconds);

[[nodiscard]] float dbToLinear(double db) noexcept;
[[nodiscard]] float sanitizeSample(float sample) noexcept;
}
