#include "ReverseBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace reverseback
{
AudioBuffer reverseFrames(const AudioBuffer& input)
{
    if (input.empty())
        return {};

    const auto frameCount = input.front().size();
    for (const auto& channel : input)
    {
        if (channel.size() != frameCount)
            throw std::invalid_argument("All audio channels must have the same frame count");
    }

    AudioBuffer output;
    output.reserve(input.size());

    for (const auto& channel : input)
        output.emplace_back(channel.rbegin(), channel.rend());

    return output;
}
}
