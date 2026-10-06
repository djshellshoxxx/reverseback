#pragma once

#include <vector>

namespace reverseback
{
using AudioBuffer = std::vector<std::vector<float>>;

// Reverse frame order while preserving channel identity.
// All channels must contain the same number of frames.
AudioBuffer reverseFrames(const AudioBuffer& input);
}
