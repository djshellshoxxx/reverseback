#pragma once

#include "ReverseBuffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reverseback
{
class SmoothedGain
{
public:
    void prepare(double sampleRate, double smoothingSeconds, double initialDb);
    void setTargetDb(double db) noexcept;
    [[nodiscard]] float nextGain() noexcept;

private:
    float current_{1.0f};
    float target_{1.0f};
    float step_{0.0f};
    std::uint64_t framesRemaining_{0};
    std::uint64_t smoothingFrames_{1};
};

class VoiceTrigger
{
public:
    void prepare(double sampleRate,
                 std::size_t channels,
                 double thresholdDb = -45.0,
                 double sustainSeconds = 0.05,
                 double preRollSeconds = 0.2);

    void reset() noexcept;
    [[nodiscard]] bool processFrame(const float* samples) noexcept;
    [[nodiscard]] AudioBuffer preRoll() const;

private:
    std::size_t channels_{0};
    float thresholdLinear_{0.0f};
    std::uint64_t sustainFrames_{0};
    std::uint64_t aboveFrames_{0};
    std::uint64_t preRollFrames_{0};
    std::uint64_t preRollWrite_{0};
    std::uint64_t preRollCount_{0};
    AudioBuffer ring_;
};

class PeakProtector
{
public:
    explicit PeakProtector(float ceilingDb = -1.0f);
    [[nodiscard]] float process(float sample) const noexcept;
    [[nodiscard]] float ceilingLinear() const noexcept { return ceiling_; }

private:
    float ceiling_{0.89125094f};
};
}
