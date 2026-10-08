// Helpers shared by the core tests.
#pragma once

#include "AudioClip.h"
#include "ClipPlayer.h"
#include "TestHarness.h"

#include <array>
#include <memory>
#include <random>
#include <vector>

namespace rbt
{
using namespace rb;

using Planar = std::array<std::vector<float>, 2>;

inline std::shared_ptr<AudioClip> makeClip (double rate, const std::vector<std::vector<float>>& chans)
{
    auto c = AudioClip::create (rate, static_cast<int> (chans.size()), chans[0].size());
    for (std::size_t ch = 0; ch < chans.size(); ++ch)
        for (std::size_t i = 0; i < chans[ch].size(); ++i)
            c->channel (static_cast<int> (ch))[i] = chans[ch][i];
    c->seal (chans[0].size());
    return c;
}

inline std::shared_ptr<AudioClip> makeRampClip (double rate, Frame frames, int channels = 1)
{
    auto c = AudioClip::create (rate, channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (Frame i = 0; i < frames; ++i)
            c->channel (ch)[i] = static_cast<float> (i + 1) * (ch == 0 ? 1.0f : -1.0f);
    c->seal (frames);
    return c;
}

inline std::shared_ptr<AudioClip> makeNoiseClip (double rate, Frame frames, int channels, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-0.9f, 0.9f);
    auto c = AudioClip::create (rate, channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (Frame i = 0; i < frames; ++i)
            c->channel (ch)[i] = d (rng);
    c->seal (frames);
    return c;
}

inline std::shared_ptr<AudioClip> makeSineClip (double rate, Frame frames, double freq, double amp = 0.5)
{
    auto c = AudioClip::create (rate, 1, frames);
    for (Frame i = 0; i < frames; ++i)
        c->channel (0)[i] = static_cast<float> (amp * std::sin (2.0 * 3.14159265358979323846 * freq * static_cast<double> (i) / rate));
    c->seal (frames);
    return c;
}

// Runs a player to completion (or `maxFrames`) with the given block size; returns what it produced.
inline Planar renderPlayer (ClipPlayer& p, std::size_t outCh, std::size_t block, std::size_t maxFrames = 100000000)
{
    Planar out;
    std::vector<float> a (block), b (block);
    std::size_t total = 0;
    while (total < maxFrames)
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        float* ptrs[2] = { a.data(), b.data() };
        const std::size_t want = std::min (block, maxFrames - total);
        const std::size_t got = p.process (ptrs, outCh, want);
        out[0].insert (out[0].end(), a.begin(), a.begin() + static_cast<long> (got));
        if (outCh > 1)
            out[1].insert (out[1].end(), b.begin(), b.begin() + static_cast<long> (got));
        total += got;
        if (got < want)
            break;
    }
    return out;
}

inline double rms (const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t> (-1))
{
    to = std::min (to, v.size());
    if (to <= from)
        return 0.0;
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return std::sqrt (s / static_cast<double> (to - from));
}
}  // namespace rbt
