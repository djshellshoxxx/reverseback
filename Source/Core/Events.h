// Audio-thread -> control-thread notifications and deferred destruction.
#pragma once

#include "AudioClip.h"
#include "Queues.h"

#include <atomic>
#include <memory>

namespace rb
{
enum class EventType : std::uint8_t
{
    TakeCompleted,     // clip = sealed take
    CaptureCancelled,  // a = reason (0 stop, 1 mode change)
    TooShort,          // hold/finish-early shorter than 50 ms
    FrozenReady,       // live chunk frozen; a = generation
    PlaybackFinished,
    NeedSpareTake,     // a = frames, channels, rate
    Underrun,
    Error,             // code
    Message
};

struct Event
{
    EventType type = EventType::Message;
    ErrorCode code = ErrorCode::None;
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    double rate = 0.0;
    int channels = 0;
    std::shared_ptr<const AudioClip> clip;
};

// Owned by the engine. post()/retire() are called from the audio thread only; poll()/drain() from
// the control thread only.
class EventSink
{
public:
    void post (Event&& e) noexcept
    {
        if (! events_.push (std::move (e)))
            eventsDropped_.fetch_add (1, std::memory_order_relaxed);
    }

    // Moves a reference out of the audio thread so the control thread frees it. If the queue is
    // full (the control thread has stalled for 64 retirements) the reference is released here.
    void retire (std::shared_ptr<const void> p) noexcept
    {
        if (! p)
            return;
        if (! retired_.push (std::move (p)))
            retireOverflow_.fetch_add (1, std::memory_order_relaxed);
    }

    bool poll (Event& out) { return events_.pop (out); }

    std::size_t drainRetired()
    {
        std::size_t n = 0;
        std::shared_ptr<const void> p;
        while (retired_.pop (p))
        {
            p.reset();
            ++n;
        }
        return n;
    }

    std::uint32_t eventsDropped() const noexcept { return eventsDropped_.load (std::memory_order_relaxed); }
    std::uint32_t retireOverflow() const noexcept { return retireOverflow_.load (std::memory_order_relaxed); }

private:
    SpscQueue<Event, 128> events_;
    SpscQueue<std::shared_ptr<const void>, 64> retired_;
    std::atomic<std::uint32_t> eventsDropped_ { 0 };
    std::atomic<std::uint32_t> retireOverflow_ { 0 };
};
}  // namespace rb
