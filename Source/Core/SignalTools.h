// Small real-time-safe DSP helpers (ENGINE_DESIGN section 6).
#pragma once

#include "AudioClip.h"

#include <vector>

namespace rb
{
// Raised-cosine fade shape for 0-based frame idx of a fade F frames long. Never exactly 0 or 1.
inline float fadeShape (Frame idx, Frame F) noexcept
{
    return 0.5f * (1.0f - std::cos (3.14159265358979323846f * static_cast<float> (idx + 1) / static_cast<float> (F + 1)));
}

// Linear ramp to a target over a fixed number of frames (20 ms for gain controls).
class GainSmoother
{
public:
    void reset (float v) noexcept { cur_ = target_ = v; remaining_ = 0; step_ = 0.0f; }

    void setTarget (float t, Frame rampFrames) noexcept
    {
        if (t == target_)
            return;
        target_ = t;
        if (rampFrames == 0)
        {
            cur_ = t;
            remaining_ = 0;
            return;
        }
        remaining_ = rampFrames;
        step_ = (t - cur_) / static_cast<float> (rampFrames);
    }

    float next() noexcept
    {
        if (remaining_ > 0)
        {
            cur_ += step_;
            if (--remaining_ == 0)
                cur_ = target_;
        }
        return cur_;
    }

    float current() const noexcept { return cur_; }
    bool smoothing() const noexcept { return remaining_ > 0; }

private:
    float cur_ = 1.0f, target_ = 1.0f, step_ = 0.0f;
    Frame remaining_ = 0;
};

// Peak envelope with instant attack and exponential release.
class LevelFollower
{
public:
    void configure (double rate, double releaseSeconds) noexcept
    {
        decay_ = static_cast<float> (std::exp (-1.0 / (rate * releaseSeconds)));
        env_ = 0.0f;
    }
    float process (float x) noexcept
    {
        env_ = std::max (std::abs (x), env_ * decay_);
        return env_;
    }
    void reset() noexcept { env_ = 0.0f; }

private:
    float decay_ = 0.999f, env_ = 0.0f;
};

// Fires once the short-term RMS (10 ms window) has stayed at or above the threshold for `sustain`
// consecutive frames (50 ms). A 10 ms window rides through the gaps between voice periods but a
// brief click cannot satisfy a 50 ms sustain.
class VoiceTrigger
{
public:
    // Non-real-time: allocates the window.
    void prepare (double rate)
    {
        rate_ = rate;
        window_ = static_cast<std::size_t> (framesFor (0.01, rate, 1));
        sq_.assign (window_, 0.0f);
        sustain_ = framesFor (kVoiceSustainSeconds, rate, 1);
        arm (-45.0);
    }

    // Real-time safe.
    void arm (double thresholdDb) noexcept
    {
        const double g = static_cast<double> (dbToGain (thresholdDb));
        thresholdSum_ = g * g * static_cast<double> (window_);
        reset();
    }

    void reset() noexcept
    {
        std::fill (sq_.begin(), sq_.end(), 0.0f);
        sum_ = 0.0;
        pos_ = 0;
        run_ = 0;
    }

    // msq = mean square of the frame across channels. Returns true once the sustain has been met.
    bool processSquare (float msq) noexcept
    {
        sum_ += static_cast<double> (msq) - static_cast<double> (sq_[pos_]);
        sq_[pos_] = msq;
        if (++pos_ == window_)
            pos_ = 0;
        if (sum_ < 0.0)
            sum_ = 0.0;
        run_ = sum_ >= thresholdSum_ ? run_ + 1 : 0;
        return run_ >= sustain_;
    }

private:
    double rate_ = 48000.0;
    std::vector<float> sq_;
    std::size_t window_ = 1, pos_ = 0;
    double sum_ = 0.0, thresholdSum_ = 0.0;
    Frame sustain_ = 1, run_ = 0;
};

// Rolling record of the most recent audio (200 ms) used by Auto Start on Voice.
class PreRollRing
{
public:
    void configure (double rate, int channels, double seconds)
    {
        capacity_ = framesFor (seconds, rate, 1);
        channels_ = channels;
        for (auto& v : buf_)
            v.assign (static_cast<std::size_t> (capacity_), 0.0f);
        clear();
    }
    void clear() noexcept { write_ = 0; filled_ = 0; }
    void setChannels (int c) noexcept { channels_ = c; }

    // frame[c] for c < channels
    void push (const float* frame) noexcept
    {
        for (int c = 0; c < channels_; ++c)
            buf_[static_cast<std::size_t> (c)][write_] = frame[c];
        write_ = (write_ + 1) % static_cast<std::size_t> (capacity_);
        filled_ = std::min<Frame> (filled_ + 1, capacity_);
    }

    Frame available() const noexcept { return filled_; }

    // Copies the most recent `count` frames (count <= available) in chronological order.
    void copyOut (float* const* dst, Frame count) const noexcept
    {
        const std::size_t cap = static_cast<std::size_t> (capacity_);
        const std::size_t start = (write_ + cap - static_cast<std::size_t> (count)) % cap;
        for (int c = 0; c < channels_; ++c)
            for (std::size_t i = 0; i < static_cast<std::size_t> (count); ++i)
                dst[c][i] = buf_[static_cast<std::size_t> (c)][(start + i) % cap];
    }

private:
    std::vector<float> buf_[kMaxChannels];
    Frame capacity_ = 1, filled_ = 0;
    std::size_t write_ = 0;
    int channels_ = 1;
};

// Non-destructive silence analysis: returns the padded span containing non-silent audio, or an
// empty selection if none is found. Allocates; call off the audio thread.
Selection findNonSilentSelection (const ClipSource& src,
                                  double thresholdDb = -50.0,
                                  double windowSeconds = 0.02,
                                  double paddingSeconds = 0.05);
}  // namespace rb
