#include "RealtimeTools.h"
#include "SignalTools.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace reverseback
{
void SmoothedGain::prepare(double sampleRate, double smoothingSeconds, double initialDb)
{
    if (sampleRate <= 0.0 || smoothingSeconds < 0.0)
        throw std::invalid_argument("Invalid gain smoothing configuration");

    smoothingFrames_ = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(sampleRate * smoothingSeconds)));
    current_ = dbToLinear(initialDb);
    target_ = current_;
    step_ = 0.0f;
    framesRemaining_ = 0;
}

void SmoothedGain::setTargetDb(double db) noexcept
{
    target_ = dbToLinear(db);
    framesRemaining_ = smoothingFrames_;
    step_ = (target_ - current_) / static_cast<float>(framesRemaining_);
}

float SmoothedGain::nextGain() noexcept
{
    if (framesRemaining_ > 0)
    {
        current_ += step_;
        --framesRemaining_;
        if (framesRemaining_ == 0)
            current_ = target_;
    }
    return current_;
}

void VoiceTrigger::prepare(double sampleRate,
                           std::size_t channels,
                           double thresholdDb,
                           double sustainSeconds,
                           double preRollSeconds)
{
    if (sampleRate <= 0.0 || channels == 0)
        throw std::invalid_argument("Invalid voice trigger configuration");

    channels_ = channels;
    thresholdLinear_ = dbToLinear(thresholdDb);
    sustainFrames_ = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(sampleRate * sustainSeconds)));
    preRollFrames_ = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(sampleRate * preRollSeconds)));
    ring_.assign(channels_, std::vector<float>(static_cast<std::size_t>(preRollFrames_), 0.0f));
    reset();
}

void VoiceTrigger::reset() noexcept
{
    aboveFrames_ = 0;
    preRollWrite_ = 0;
    preRollCount_ = 0;
    for (auto& channel : ring_)
        std::fill(channel.begin(), channel.end(), 0.0f);
}

bool VoiceTrigger::processFrame(const float* samples) noexcept
{
    float peak = 0.0f;
    for (std::size_t channel = 0; channel < channels_; ++channel)
    {
        const auto s = sanitizeSample(samples[channel]);
        ring_[channel][static_cast<std::size_t>(preRollWrite_)] = s;
        peak = std::max(peak, std::abs(s));
    }

    preRollWrite_ = (preRollWrite_ + 1) % preRollFrames_;
    preRollCount_ = std::min<std::uint64_t>(preRollCount_ + 1, preRollFrames_);

    if (peak >= thresholdLinear_)
        ++aboveFrames_;
    else
        aboveFrames_ = 0;

    return aboveFrames_ >= sustainFrames_;
}

AudioBuffer VoiceTrigger::preRoll() const
{
    AudioBuffer result(channels_, std::vector<float>(static_cast<std::size_t>(preRollCount_), 0.0f));
    if (preRollCount_ == 0)
        return result;

    const auto start = (preRollWrite_ + preRollFrames_ - preRollCount_) % preRollFrames_;
    for (std::size_t channel = 0; channel < channels_; ++channel)
    {
        for (std::uint64_t i = 0; i < preRollCount_; ++i)
        {
            const auto source = (start + i) % preRollFrames_;
            result[channel][static_cast<std::size_t>(i)] =
                ring_[channel][static_cast<std::size_t>(source)];
        }
    }
    return result;
}

PeakProtector::PeakProtector(float ceilingDb)
    : ceiling_(dbToLinear(ceilingDb))
{
}

float PeakProtector::process(float sample) const noexcept
{
    sample = sanitizeSample(sample);
    return std::clamp(sample, -ceiling_, ceiling_);
}
}
