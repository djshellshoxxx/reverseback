// Min/max display envelope for a clip, built once (RAM clips) or while decoding (large files).
#pragma once

#include "AudioClip.h"

#include <memory>
#include <vector>

namespace rb
{
struct WaveformOverview
{
    static constexpr std::size_t kBuckets = 8192;

    std::vector<float> mins, maxs;   // one entry per bucket, covering the channel envelope
    Frame frames = 0;
    double sampleRate = 48000.0;

    std::size_t size() const noexcept { return mins.size(); }

    // Min/max over the normalised range [t0, t1) of the clip.
    void query (double t0, double t1, float& mn, float& mx) const noexcept;

    // Builds from an in-memory source (uses ClipSource::read, so only for RAM clips).
    static std::shared_ptr<const WaveformOverview> build (const ClipSource& src, std::size_t buckets = kBuckets);

    // Incremental builder used while decoding.
    class Builder
    {
    public:
        Builder (Frame totalFrames, double sampleRate, std::size_t buckets = kBuckets);
        void add (const float* const* planes, int channels, Frame firstFrame, std::size_t count);
        std::shared_ptr<const WaveformOverview> finish();

    private:
        std::shared_ptr<WaveformOverview> o_;
    };
};
}  // namespace rb
