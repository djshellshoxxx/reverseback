// Offline, non-destructive selection helpers for the Reverse File editor.
#pragma once
#include "AudioClip.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rb::selectionAssist
{
inline Selection pad (Selection s, Frame total, Frame amount) noexcept
{
    s.begin = std::min (s.begin, total);
    s.end = std::min (s.end, total);
    if (s.empty()) return {};
    return { amount > s.begin ? 0 : s.begin - amount,
             s.end > total - std::min (amount, total) ? total : s.end + amount };
}

// Find the quietest crossing in a short neighborhood of each boundary.
// Never crosses or collapses the selection; considers BOTH channels.
inline Selection snapToZero (const AudioClip& clip, Selection s, Frame radius) noexcept
{
    const Frame total = clip.frameCount();
    if (s.empty() || s.end > total || total < 2) return s;
    auto nearest = [&clip] (Frame edge, Frame lo, Frame hi)
    {
        Frame best = edge;
        double bestEnergy = std::numeric_limits<double>::infinity();
        bool foundCrossing = false;
        for (Frame i = lo; i <= hi; ++i)
        {
            // Index i denotes the boundary between samples i-1 and i.
            // Prefer a real zero/sign crossing on all available channels, then
            // minimize discontinuity energy. If none exists, use the quietest edge.
            double energy = 0.0;
            bool crosses = true;
            for (int c = 0; c < clip.channels(); ++c)
            {
                const float* plane = clip.channel (c);
                const Frame right = std::min (i, clip.frameCount() - 1);
                const double a = plane[right == 0 ? 0 : right - 1];
                const double b = plane[right];
                energy += std::abs (a) + std::abs (b);
                crosses = crosses && (a == 0.0 || b == 0.0 || (a < 0.0 && b > 0.0) || (a > 0.0 && b < 0.0));
            }
            if ((crosses && ! foundCrossing) || (crosses == foundCrossing && energy < bestEnergy))
            {
                best = i;
                bestEnergy = energy;
                foundCrossing = crosses;
            }
        }
        return best;
    };
    const Frame bLo = s.begin > radius ? s.begin - radius : 0;
    const Frame bHi = std::min (s.end - 1, s.begin + std::min (radius, total - s.begin));
    const Frame eLo = std::max (s.begin + 1, s.end > radius ? s.end - radius : 0);
    const Frame eHi = std::min (total, s.end + std::min (radius, total - s.end));
    return {nearest (s.begin, bLo, bHi), nearest (s.end, eLo, eHi)};
}

// Focus on the most energetic short window near the selection midpoint.
// Search is capped so an enormous recording cannot freeze the editor.
inline Selection loudestWindow (const AudioClip& clip, Selection s, Frame width, Frame searchLimit) noexcept
{
    const Frame total = clip.frameCount();
    if (s.empty() || s.end > total || width == 0) return s;
    width = std::min (width, s.length());
    if (width == s.length()) return s;
    const Frame limit = std::max (searchLimit, width);
    if (s.length() > limit)
    {
        const Frame mid = s.begin + s.length() / 2;
        const Frame half = limit / 2;
        s.begin = mid > half ? mid - half : 0;
        s.end = std::min (total, s.begin + limit);
    }
    auto amplitude = [&clip] (Frame i)
    {
        double a = 0;
        for (int c = 0; c < clip.channels(); ++c)
        {
            const double x = clip.channel(c)[i];
            if (std::isfinite (x)) a += x * x;
        }
        return a;
    };
    double sum = 0;
    for (Frame i = s.begin; i < s.begin + width; ++i) sum += amplitude (i);
    double bestSum = sum;
    Frame best = s.begin;
    for (Frame start = s.begin + 1; start <= s.end - width; ++start)
    {
        sum += amplitude (start + width - 1) - amplitude (start - 1);
        if (sum > bestSum) { bestSum = sum; best = start; }
    }
    return {best, best + width};
}
} // namespace rb::selectionAssist
