// Live Reverse transport: fixed-size chunk slots, closed-form schedule, Freeze/Resume
// (ENGINE_DESIGN section 8).
#pragma once

#include "Events.h"
#include "SignalTools.h"

#include <vector>

namespace rb
{
// Preallocated chunk storage created on the control thread and handed to the audio thread.
struct LiveStorage
{
    static constexpr std::size_t kEnvPoints = 256;

    double rate = 48000.0;
    int channels = 1;
    Frame W = 0;            // chunk length in frames
    Frame D = 0;            // extra delay in frames
    Frame slots = 0;        // ceil(D / W) + 4
    std::vector<float> data;            // slots * channels * W
    std::vector<std::int64_t> tags;     // chunk index held by each slot, -1 = none
    std::vector<float> env;             // slots * kEnvPoints * 2 (min, max) display envelope

    // Returns nullptr if the request is out of range or too large. `minSlots` lets tests force a tight pool.
    static std::shared_ptr<LiveStorage> create (double rate, int channels, Frame chunkFrames, Frame delayFrames, Frame forceSlots = 0);

    float* plane (Frame slot, int ch) noexcept
    {
        return data.data() + (static_cast<std::size_t> (slot) * static_cast<std::size_t> (channels) + static_cast<std::size_t> (ch)) * static_cast<std::size_t> (W);
    }
    const float* plane (Frame slot, int ch) const noexcept
    {
        return data.data() + (static_cast<std::size_t> (slot) * static_cast<std::size_t> (channels) + static_cast<std::size_t> (ch)) * static_cast<std::size_t> (W);
    }
    const float* envelope (Frame slot) const noexcept { return env.data() + static_cast<std::size_t> (slot) * kEnvPoints * 2; }
};

class LiveTransport
{
public:
    enum class State : std::uint8_t { Ready, Filling, Running, Freezing, Frozen };

    void prepare (double rate, std::size_t maxBlock, EventSink* sink);

    // ----- real-time -----
    bool start (std::shared_ptr<LiveStorage> storage, Frame fadeFrames) noexcept;
    void freeze() noexcept;
    void resume() noexcept;
    void stop() noexcept;
    void stopImmediate() noexcept;
    void setFadeFrames (Frame f) noexcept { fade_ = f; }
    void setStopFadeFrames (Frame f) noexcept { stopFade_ = std::max<Frame> (1, f); }

    void process (const float* const* in, std::size_t inChannels, float* const* out, std::size_t frames) noexcept;

    State state() const noexcept { return state_; }
    bool busy() const noexcept { return state_ != State::Ready || tail_.active; }
    Frame time() const noexcept { return t_; }
    float fillProgress() const noexcept;
    std::int64_t playingChunk() const noexcept { return playingChunk_; }
    std::int64_t captureChunk() const noexcept { return state_ == State::Ready || W_ == 0 ? -1 : static_cast<std::int64_t> (t_ / W_); }
    Frame captureOffset() const noexcept { return W_ == 0 ? 0 : t_ % W_; }
    Frame chunkFrames() const noexcept { return W_; }
    std::uint32_t frozenGeneration() const noexcept { return frozenGen_; }
    Frame frozenSlot() const noexcept { return frozenSlot_; }
    // Slot the display should show: the frozen chunk when Frozen, else the chunk now playing (-1 = none).
    std::int32_t displaySlot() const noexcept
    {
        if (state_ == State::Frozen)
            return static_cast<std::int32_t> (frozenSlot_);
        return playingChunk_ >= 0 ? static_cast<std::int32_t> (slotOf (playingChunk_)) : -1;
    }
    std::int32_t captureSlot() const noexcept
    {
        return (state_ == State::Ready || W_ == 0) ? -1 : static_cast<std::int32_t> (slotOf (static_cast<std::int64_t> (t_ / W_)));
    }
    const std::shared_ptr<LiveStorage>& storage() const noexcept { return storage_; }

private:
    struct Tail
    {
        bool active = false;
        std::shared_ptr<LiveStorage> storage;
        Frame slot = 0, u = 0, remaining = 0, total = 0;
        bool loop = false;
    };

    Frame slotOf (std::int64_t chunk) const noexcept { return (base_ + static_cast<Frame> (chunk)) % S_; }
    float chunkGain (Frame u) const noexcept;
    void captureInto (const float* const* in, std::size_t inCh, std::size_t pos, std::size_t m) noexcept;
    bool renderChunk (float* const* out, std::size_t pos, std::size_t m) noexcept;
    void renderFrozen (float* const* out, std::size_t pos, std::size_t m) noexcept;
    void renderTail (float* const* out, std::size_t frames) noexcept;
    void beginTail (bool loop, Frame slot, Frame u, Frame maxFrames) noexcept;
    void fault (ErrorCode code) noexcept;
    void invalidateTags() noexcept;
    bool outputActive() const noexcept { return t_ >= W_ + D_; }

    double rate_ = 48000.0;
    EventSink* sink_ = nullptr;
    State state_ = State::Ready;
    std::shared_ptr<LiveStorage> storage_;
    Tail tail_;

    Frame W_ = 0, D_ = 0, S_ = 1, base_ = 0, t_ = 0;
    Frame fade_ = 0, stopFade_ = 480;
    int ch_ = 1;

    // Freeze bookkeeping
    bool captureDone_ = false;
    Frame tc_ = 0, adopt_ = 0, frozenSlot_ = 0;
    std::uint32_t frozenGen_ = 0;
    std::int64_t playingChunk_ = -1;
};
}  // namespace rb
