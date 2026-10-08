// Audio-thread -> control-thread notifications and deferred destruction.
#pragma once

#include "AudioClip.h"
#include "Queues.h"

#include <array>
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

// Owned by the engine. post()/retire()/service() are called from the audio thread only; poll()/drain()
// from the control thread only. When a queue is full (the control thread has stalled) items wait in a
// small audio-side spill and are retried every block, so a stall never frees memory on the audio thread
// and never loses a completed take. Only a stall far beyond the spill capacity falls back to dropping.
class EventSink
{
public:
    void post (Event&& e) noexcept
    {
        service();
        if (spillEventCount_ == 0 && events_.push (std::move (e)))
            return;
        if (spillEventCount_ < kSpillEvents)
        {
            spillEvents_[(spillEventHead_ + spillEventCount_) % kSpillEvents] = std::move (e);
            ++spillEventCount_;
            return;
        }
        eventsDropped_.fetch_add (1, std::memory_order_relaxed);
        if (e.clip)
            retire (std::move (e.clip));
    }

    // Moves a reference out of the audio thread so the control thread frees it.
    void retire (std::shared_ptr<const void> p) noexcept
    {
        if (! p)
            return;
        service();
        if (spillRetiredCount_ == 0 && retired_.push (std::move (p)))
            return;
        if (spillRetiredCount_ < kSpillRetired)
        {
            spillRetired_[(spillRetiredHead_ + spillRetiredCount_) % kSpillRetired] = std::move (p);
            ++spillRetiredCount_;
            return;
        }
        retireOverflow_.fetch_add (1, std::memory_order_relaxed);   // last resort: released here
    }

    // Retries spilled items; called once per block and from post()/retire().
    void service() noexcept
    {
        while (spillRetiredCount_ > 0 && retired_.push (std::move (spillRetired_[spillRetiredHead_])))
        {
            spillRetiredHead_ = (spillRetiredHead_ + 1) % kSpillRetired;
            --spillRetiredCount_;
        }
        while (spillEventCount_ > 0 && events_.push (std::move (spillEvents_[spillEventHead_])))
        {
            spillEventHead_ = (spillEventHead_ + 1) % kSpillEvents;
            --spillEventCount_;
        }
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
    static constexpr std::size_t kSpillEvents = 32, kSpillRetired = 64;

    SpscQueue<Event, 256> events_;
    SpscQueue<std::shared_ptr<const void>, 256> retired_;
    std::atomic<std::uint32_t> eventsDropped_ { 0 };
    std::atomic<std::uint32_t> retireOverflow_ { 0 };

    // audio-thread only
    std::array<Event, kSpillEvents> spillEvents_ {};
    std::array<std::shared_ptr<const void>, kSpillRetired> spillRetired_ {};
    std::size_t spillEventHead_ = 0, spillEventCount_ = 0, spillRetiredHead_ = 0, spillRetiredCount_ = 0;
};
}  // namespace rb
