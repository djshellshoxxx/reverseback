#include "SignalTools.h"

namespace rb
{
Selection findNonSilentSelection (const ClipSource& src, double thresholdDb, double windowSeconds, double paddingSeconds)
{
    const Frame total = src.frameCount();
    const double rate = src.sampleRate();
    const Frame win = framesFor (windowSeconds, rate, 1);
    const Frame pad = framesFor (paddingSeconds, rate);
    if (total == 0)
        return {};

    const double thr = static_cast<double> (dbToGain (thresholdDb));
    const double thrSq = thr * thr;
    const int ch = std::min (src.channels(), static_cast<int> (kMaxChannels));

    std::vector<float> a (static_cast<std::size_t> (win)), b (static_cast<std::size_t> (win));
    float* ptr[kMaxChannels] = { a.data(), b.data() };

    bool found = false;
    Frame firstWin = 0, lastWin = 0;
    for (Frame w = 0; w * win < total; ++w)
    {
        const Frame start = w * win;
        const std::size_t n = static_cast<std::size_t> (std::min (win, total - start));
        src.read (static_cast<std::int64_t> (start), n, ptr);
        double sum = 0.0;
        for (int c = 0; c < ch; ++c)
            for (std::size_t i = 0; i < n; ++i)
                sum += static_cast<double> (ptr[c][i]) * static_cast<double> (ptr[c][i]);
        const double meanSq = sum / (static_cast<double> (n) * ch);
        if (meanSq >= thrSq)
        {
            if (! found)
                firstWin = w;
            lastWin = w;
            found = true;
        }
    }
    if (! found)
        return {};

    const Frame begin = firstWin * win > pad ? firstWin * win - pad : 0;
    const Frame end = std::min (total, (lastWin + 1) * win + pad);
    return { begin, end };
}
}  // namespace rb
