// Input channel mapping / gain and output volume / monitor / limiter (ENGINE_DESIGN section 10).
#pragma once

#include "SignalTools.h"

#include <atomic>

namespace rb
{
enum class InputChannelMode : std::uint8_t { Input1, Input2, Mix, Stereo };

class InputStage
{
public:
    void prepare (double rate, std::size_t maxBlock);
    void setGainDb (float db) noexcept;
    void setMode (InputChannelMode m) noexcept { mode_ = m; }
    int outputChannels() const noexcept { return mode_ == InputChannelMode::Stereo ? 2 : 1; }

    // Maps host input to 1 or 2 planar channels, sanitises, applies the smoothed gain. frames <= maxBlock.
    // The returned pointers stay valid until the next call.
    const float* const* process (const float* const* hostIn, std::size_t hostChannels, std::size_t frames) noexcept;

    std::uint32_t sanitizedCount() const noexcept { return sanitized_; }

private:
    double rate_ = 48000.0;
    std::size_t maxBlock_ = 0;
    InputChannelMode mode_ = InputChannelMode::Input1;
    GainSmoother gain_;
    std::vector<float> store_;
    float* mapped_[kMaxChannels] = {};
    std::uint32_t sanitized_ = 0;
};

// Transparent look-ahead peak limiter: ceiling -1 dBFS, no make-up gain, latency = lookahead frames.
class OutputLimiter
{
public:
    static constexpr float kCeiling = 0.8912509f;   // -1 dBFS

    void prepare (double rate, std::size_t channels);
    void reset() noexcept;
    void process (float* const* io, std::size_t channels, std::size_t frames) noexcept;
    std::size_t latencyFrames() const noexcept { return lookahead_; }

private:
    std::size_t lookahead_ = 48;
    std::size_t channels_ = 2;
    float releaseCoef_ = 0.0f;
    std::size_t pos_ = 0;
    float held_ = 1.0f;
    std::vector<float> delay_[kMaxChannels];   // lookahead+1 samples
    std::vector<float> target_;                // lookahead+1 per-sample target gains
    std::vector<float> smoothed_;              // lookahead+1 release-smoothed gains
};

class OutputStage
{
public:
    void prepare (double rate, std::size_t maxBlock, bool enableLimiter);
    void reset() noexcept;
    void setVolumeDb (float db) noexcept;
    void setMonitor (float amount01) noexcept;   // 0..1 linear; always starts at 0

    // io holds the engine's stereo output and is modified in place. monitorIn may be null.
    // Returns false if a non-finite sample had to be silenced (DSP fault).
    bool process (float* const* io, const float* const* monitorIn, std::size_t monitorChannels, bool muteMonitor,
                  std::size_t frames) noexcept;

    std::size_t latencyFrames() const noexcept { return limiterEnabled_ ? limiter_.latencyFrames() : 0; }

private:
    double rate_ = 48000.0;
    bool limiterEnabled_ = false;
    GainSmoother volume_, monitor_, monitorMute_;
    OutputLimiter limiter_;
};
}  // namespace rb
