#include "WaveformOverview.h"

namespace rb
{
void WaveformOverview::query (double t0, double t1, float& mn, float& mx) const noexcept
{
    mn = 0.0f;
    mx = 0.0f;
    const std::size_t n = mins.size();
    if (n == 0)
        return;
    t0 = std::clamp (t0, 0.0, 1.0);
    t1 = std::clamp (t1, t0, 1.0);
    const std::size_t b0 = std::min (n - 1, static_cast<std::size_t> (t0 * static_cast<double> (n)));
    const std::size_t b1 = std::min (n - 1, std::max (b0, static_cast<std::size_t> (std::ceil (t1 * static_cast<double> (n))) - (t1 > t0 ? 1 : 0)));
    mn = mins[b0];
    mx = maxs[b0];
    for (std::size_t b = b0 + 1; b <= b1; ++b)
    {
        mn = std::min (mn, mins[b]);
        mx = std::max (mx, maxs[b]);
    }
}

WaveformOverview::Builder::Builder (Frame totalFrames, double sampleRate, std::size_t buckets)
{
    o_ = std::make_shared<WaveformOverview>();
    o_->frames = totalFrames;
    o_->sampleRate = sampleRate;
    const std::size_t n = static_cast<std::size_t> (std::max<Frame> (1, std::min<Frame> (buckets, totalFrames)));
    o_->mins.assign (n, 0.0f);
    o_->maxs.assign (n, 0.0f);
}

void WaveformOverview::Builder::add (const float* const* planes, int channels, Frame firstFrame, std::size_t count)
{
    const std::size_t n = o_->mins.size();
    const double perBucket = static_cast<double> (o_->frames) / static_cast<double> (n);
    for (std::size_t i = 0; i < count; ++i)
    {
        float lo = planes[0][i], hi = planes[0][i];
        for (int c = 1; c < channels; ++c)
        {
            lo = std::min (lo, planes[c][i]);
            hi = std::max (hi, planes[c][i]);
        }
        const std::size_t b = std::min (n - 1, static_cast<std::size_t> (static_cast<double> (firstFrame + i) / perBucket));
        // A bucket's first sample initialises it (all buckets start at 0/0, which would otherwise pin the envelope to zero).
        const bool first = (static_cast<double> (firstFrame + i) < static_cast<double> (b) * perBucket + 1.0);
        if (first)
        {
            o_->mins[b] = lo;
            o_->maxs[b] = hi;
        }
        else
        {
            o_->mins[b] = std::min (o_->mins[b], lo);
            o_->maxs[b] = std::max (o_->maxs[b], hi);
        }
    }
}

std::shared_ptr<const WaveformOverview> WaveformOverview::Builder::finish()
{
    return std::move (o_);
}

std::shared_ptr<const WaveformOverview> WaveformOverview::build (const ClipSource& src, std::size_t buckets)
{
    Builder b (src.frameCount(), src.sampleRate(), buckets);
    const int ch = std::min (src.channels(), static_cast<int> (kMaxChannels));
    constexpr std::size_t kBlock = 8192;
    std::vector<float> l (kBlock), r (kBlock);
    float* planes[2] = { l.data(), r.data() };
    for (Frame start = 0; start < src.frameCount(); start += kBlock)
    {
        const std::size_t n = static_cast<std::size_t> (std::min<Frame> (kBlock, src.frameCount() - start));
        if (! src.read (static_cast<std::int64_t> (start), n, planes))
            break;
        b.add (planes, ch, start, n);
    }
    return b.finish();
}
}  // namespace rb
