#include "SignalTools.h"

#include <algorithm>
#include <cmath>

namespace reverseback
{
float dbToLinear(double db) noexcept
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

float sanitizeSample(float sample) noexcept
{
    return std::isfinite(sample) ? sample : 0.0f;
}

Selection findNonSilentSelection(const AudioClip& clip,
                                 double thresholdDb,
                                 double windowSeconds,
                                 double paddingSeconds)
{
    const auto total = clip.frameCount();
    if (total == 0 || clip.channels() == 0)
        return {0, total};

    const auto window = std::max<Frame>(
        1, static_cast<Frame>(std::llround(windowSeconds * clip.sampleRate())));
    const auto padding = static_cast<Frame>(
        std::llround(std::max(0.0, paddingSeconds) * clip.sampleRate()));
    const auto threshold = dbToLinear(thresholdDb);

    bool found = false;
    Frame first = total;
    Frame last = 0;

    for (Frame begin = 0; begin < total; begin += window)
    {
        const auto end = std::min<Frame>(total, begin + window);
        long double sumSquares = 0.0;
        std::uint64_t count = 0;

        for (std::size_t channel = 0; channel < clip.channels(); ++channel)
        {
            for (Frame frame = begin; frame < end; ++frame)
            {
                const auto sample = sanitizeSample(clip.sample(channel, frame));
                sumSquares += static_cast<long double>(sample) * sample;
                ++count;
            }
        }

        const auto rms = count == 0
            ? 0.0
            : std::sqrt(static_cast<double>(sumSquares / static_cast<long double>(count)));

        if (rms >= threshold)
        {
            found = true;
            first = std::min(first, begin);
            last = std::max(last, end);
        }
    }

    if (!found)
        return {0, total};

    return {
        first > padding ? first - padding : 0,
        std::min<Frame>(total, last + padding)
    };
}
