#pragma once

#include "ReverseBuffer.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace reverseback
{
using Frame = std::uint64_t;

struct Selection
{
    Frame begin{0};
    Frame end{0};
};

enum class Direction
{
    Forward,
    Reverse
};

enum class LoopPattern
{
    Once,
    Loop,
    PingPong
};

class AudioClip
{
public:
    AudioClip() = default;

    AudioClip(double sampleRate, AudioBuffer audio)
        : sampleRate_(sampleRate), audio_(std::move(audio))
    {
        if (sampleRate_ <= 0.0)
            throw std::invalid_argument("Sample rate must be positive");

        const auto frames = audio_.empty() ? 0U : audio_.front().size();
        for (const auto& channel : audio_)
        {
            if (channel.size() != frames)
                throw std::invalid_argument("All clip channels must have equal frame counts");
        }
    }

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t channels() const noexcept { return audio_.size(); }
    [[nodiscard]] Frame frameCount() const noexcept
    {
        return audio_.empty() ? 0 : static_cast<Frame>(audio_.front().size());
    }

    [[nodiscard]] float sample(std::size_t channel, Frame frame) const
    {
        return audio_.at(channel).at(static_cast<std::size_t>(frame));
    }

    [[nodiscard]] const AudioBuffer& data() const noexcept { return audio_; }

private:
    double sampleRate_{48000.0};
    AudioBuffer audio_;
};
}
