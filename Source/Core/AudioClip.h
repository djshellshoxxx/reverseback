// Immutable-after-seal planar audio and the read interface used by playback/export.
#pragma once

#include "Types.h"

#include <cstring>
#include <memory>
#include <vector>

namespace rb
{
// Random-access source of planar float audio. read() must be real-time safe.
class ClipSource
{
public:
    virtual ~ClipSource() = default;
    virtual int channels() const noexcept = 0;
    virtual double sampleRate() const noexcept = 0;
    virtual Frame frameCount() const noexcept = 0;

    // Copies `count` frames starting at `first` into dst[c][0..count). Frames outside
    // [0, frameCount) are zero filled. Returns false if data was not available (cache miss).
    virtual bool read (std::int64_t first, std::size_t count, float* const* dst) const noexcept = 0;

    // Same contract for non-real-time callers (export): may block on I/O but never reports a cache miss.
    // Sources that are always resident just forward to read().
    virtual bool readOffline (std::int64_t first, std::size_t count, float* const* dst) const noexcept
    {
        return read (first, count, dst);
    }
};

// Planar in-memory audio. Created mutable (capture), then sealed; after seal() it is never written.
class AudioClip final : public ClipSource
{
public:
    static std::shared_ptr<AudioClip> create (double sampleRate, int channels, Frame capacityFrames)
    {
        auto c = std::shared_ptr<AudioClip> (new AudioClip());
        c->rate_ = sampleRate;
        c->channels_ = channels < 1 ? 1 : (channels > static_cast<int> (kMaxChannels) ? static_cast<int> (kMaxChannels) : channels);
        c->capacity_ = capacityFrames;
        c->frames_ = capacityFrames;
        c->data_.assign (static_cast<std::size_t> (c->channels_) * static_cast<std::size_t> (capacityFrames), 0.0f);
        return c;
    }

    int channels() const noexcept override { return channels_; }
    double sampleRate() const noexcept override { return rate_; }
    Frame frameCount() const noexcept override { return frames_; }
    Frame capacity() const noexcept { return capacity_; }
    bool sealed() const noexcept { return sealed_; }
    std::size_t byteSize() const noexcept { return data_.size() * sizeof (float); }

    float* channel (int c) noexcept { return data_.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (capacity_); }
    const float* channel (int c) const noexcept { return data_.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (capacity_); }

    // Fixes the final length (<= capacity). The clip must not be written afterwards.
    void seal (Frame finalFrames) noexcept
    {
        frames_ = std::min (finalFrames, capacity_);
        sealed_ = true;
    }

    bool read (std::int64_t first, std::size_t count, float* const* dst) const noexcept override
    {
        const bool inside = first >= 0 && static_cast<Frame> (first) + count <= frames_;
        for (int c = 0; c < channels_; ++c)
        {
            float* d = dst[c];
            const float* s = channel (c);
            if (inside)
            {
                std::memcpy (d, s + first, count * sizeof (float));
                continue;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::int64_t k = first + static_cast<std::int64_t> (i);
                d[i] = (k >= 0 && static_cast<Frame> (k) < frames_) ? s[k] : 0.0f;
            }
        }
        return true;
    }

private:
    AudioClip() = default;
    double rate_ = 48000.0;
    int channels_ = 1;
    Frame capacity_ = 0;
    Frame frames_ = 0;
    bool sealed_ = false;
    std::vector<float> data_;
};
}  // namespace rb
