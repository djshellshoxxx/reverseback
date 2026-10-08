#include "Stages.h"

namespace rb
{
// ------------------------------------------------------------------ InputStage
void InputStage::prepare (double rate, std::size_t maxBlock)
{
    rate_ = rate;
    maxBlock_ = std::max<std::size_t> (1, maxBlock);
    store_.assign (kMaxChannels * maxBlock_, 0.0f);
    for (std::size_t c = 0; c < kMaxChannels; ++c)
        mapped_[c] = store_.data() + c * maxBlock_;
    gain_.reset (1.0f);
    sanitized_ = 0;
}

void InputStage::setGainDb (float db) noexcept
{
    gain_.setTarget (dbToGain (static_cast<double> (db)), framesFor (0.02, rate_));
}

const float* const* InputStage::process (const float* const* hostIn, std::size_t hostCh, std::size_t frames) noexcept
{
    frames = std::min (frames, maxBlock_);
    const bool have0 = hostIn != nullptr && hostCh >= 1 && hostIn[0] != nullptr;
    const bool have1 = hostIn != nullptr && hostCh >= 2 && hostIn[1] != nullptr;
    const float* ch0 = have0 ? hostIn[0] : nullptr;
    const float* ch1 = have1 ? hostIn[1] : ch0;   // a mono host feeds Input 2 / Stereo from its only channel

    for (std::size_t i = 0; i < frames; ++i)
    {
        float l = ch0 != nullptr ? ch0[i] : 0.0f;
        float r = ch1 != nullptr ? ch1[i] : 0.0f;
        if (! std::isfinite (l)) { l = 0.0f; ++sanitized_; }
        if (! std::isfinite (r)) { r = 0.0f; ++sanitized_; }

        const float g = gain_.next();
        switch (mode_)
        {
            case InputChannelMode::Input1: mapped_[0][i] = l * g; break;
            case InputChannelMode::Input2: mapped_[0][i] = r * g; break;
            case InputChannelMode::Mix:    mapped_[0][i] = have1 ? 0.5f * (l + r) * g : l * g; break;
            case InputChannelMode::Stereo: mapped_[0][i] = l * g; mapped_[1][i] = r * g; break;
        }
    }
    return mapped_;
}

// ------------------------------------------------------------------ OutputLimiter
void OutputLimiter::prepare (double rate, std::size_t channels)
{
    channels_ = std::min<std::size_t> (channels, kMaxChannels);
    lookahead_ = static_cast<std::size_t> (std::max<Frame> (1, framesFor (0.001, rate)));
    releaseCoef_ = static_cast<float> (1.0 - std::exp (-1.0 / (rate * 0.08)));
    for (auto& d : delay_)
        d.assign (lookahead_ + 1, 0.0f);
    target_.assign (lookahead_ + 1, 1.0f);
    smoothed_.assign (lookahead_ + 1, 1.0f);
    reset();
}

void OutputLimiter::reset() noexcept
{
    for (auto& d : delay_)
        std::fill (d.begin(), d.end(), 0.0f);
    std::fill (target_.begin(), target_.end(), 1.0f);
    std::fill (smoothed_.begin(), smoothed_.end(), 1.0f);
    pos_ = 0;
    held_ = 1.0f;
}

void OutputLimiter::process (float* const* io, std::size_t channels, std::size_t frames) noexcept
{
    const std::size_t ring = lookahead_ + 1;
    channels = std::min (channels, channels_);
    for (std::size_t i = 0; i < frames; ++i)
    {
        float peak = 0.0f;
        for (std::size_t c = 0; c < channels; ++c)
            peak = std::max (peak, std::abs (io[c][i]));
        const float t = peak > kCeiling ? kCeiling / peak : 1.0f;

        // slot pos_ now holds the newest sample; the oldest (lookahead frames ago) sits at pos_+1.
        target_[pos_] = t;
        float m = 1.0f;
        for (std::size_t k = 0; k < ring; ++k)
            m = std::min (m, target_[k]);

        held_ = std::min (m, held_ + (1.0f - held_) * releaseCoef_);
        smoothed_[pos_] = held_;
        float sum = 0.0f;
        for (std::size_t k = 0; k < ring; ++k)
            sum += smoothed_[k];
        float g = sum / static_cast<float> (ring);
        if (g > 0.99999994f)
            g = 1.0f;

        const std::size_t oldest = (pos_ + 1) % ring;
        for (std::size_t c = 0; c < channels; ++c)
        {
            delay_[c][pos_] = io[c][i];
            io[c][i] = delay_[c][oldest] * g;
        }
        pos_ = oldest;
    }
}

// ------------------------------------------------------------------ OutputStage
void OutputStage::prepare (double rate, std::size_t, bool enableLimiter)
{
    rate_ = rate;
    limiterEnabled_ = enableLimiter;
    if (enableLimiter)
        limiter_.prepare (rate, 2);
    volume_.reset (1.0f);
    monitor_.reset (0.0f);
    monitorMute_.reset (1.0f);
}

void OutputStage::reset() noexcept
{
    monitor_.reset (0.0f);
    if (limiterEnabled_)
        limiter_.reset();
}

void OutputStage::setVolumeDb (float db) noexcept
{
    volume_.setTarget (db <= -60.0f ? 0.0f : dbToGain (static_cast<double> (db)), framesFor (0.02, rate_));
}

void OutputStage::setMonitor (float amount01) noexcept
{
    monitor_.setTarget (std::clamp (amount01, 0.0f, 1.0f), framesFor (0.02, rate_));
}

bool OutputStage::process (float* const* io, const float* const* monitorIn, std::size_t monitorCh, bool muteMonitor,
                           std::size_t frames) noexcept
{
    monitorMute_.setTarget (muteMonitor ? 0.0f : 1.0f, framesFor (0.02, rate_));
    bool ok = true;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float v = volume_.next();
        const float mon = monitor_.next() * monitorMute_.next();
        for (std::size_t c = 0; c < 2; ++c)
        {
            float s = io[c][i] * v;
            if (mon > 0.0f && monitorIn != nullptr && monitorCh > 0)
                s += monitorIn[std::min(c, monitorCh - 1)][i] * mon;
            if (! std::isfinite (s))
            {
                s = 0.0f;
                ok = false;
            }
            io[c][i] = s;
        }
    }
    if (limiterEnabled_)
        limiter_.process (io, 2, frames);
    return ok;
}
}  // namespace rb
