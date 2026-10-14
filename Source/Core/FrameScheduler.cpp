#include "FrameScheduler.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace reverseback
{
FrameScheduler::FrameScheduler(std::uint32_t sampleRate)
    : sampleRate_(sampleRate)
{
    if (sampleRate_ == 0)
        throw std::invalid_argument("Sample rate must be greater than zero");
}

void FrameScheduler::configureRecord(double captureSeconds, double waitSeconds)
{
    if (captureSeconds <= 0.0)
        throw std::invalid_argument("Capture duration must be greater than zero");
    if (waitSeconds < 0.0)
        throw std::invalid_argument("Wait duration cannot be negative");

    captureFrames_ = secondsToFrames(captureSeconds);
    waitFrames_ = secondsToFrames(waitSeconds);
}

std::uint64_t FrameScheduler::captureFrames() const noexcept
{
    return captureFrames_;
}

std::uint64_t FrameScheduler::waitFrames() const noexcept
{
    return waitFrames_;
}

std::uint64_t FrameScheduler::playbackStartFrame() const noexcept
{
    return captureFrames_ + waitFrames_;
}

std::uint64_t FrameScheduler::playbackFrames() const noexcept
{
    return captureFrames_;
}

std::vector<FrameScheduler::Event> FrameScheduler::eventsInBlock(
    std::uint64_t blockStartFrame,
    std::uint32_t blockFrameCount) const
{
    std::vector<Event> events;
    if (blockFrameCount == 0 || captureFrames_ == 0)
        return events;

    const auto blockEndFrame = blockStartFrame + static_cast<std::uint64_t>(blockFrameCount);
    const auto playbackStart = playbackStartFrame();
    const auto playbackEnd = playbackStart + playbackFrames();

    const auto addIfInside = [&](EventType type, std::uint64_t frame)
    {
        if (frame >= blockStartFrame && frame < blockEndFrame)
            events.push_back({type, frame});
    };

    addIfInside(EventType::CaptureEnd, captureFrames_);
    addIfInside(EventType::PlaybackStart, playbackStart);
    addIfInside(EventType::PlaybackEnd, playbackEnd);
    return events;
}

std::uint64_t FrameScheduler::secondsToFrames(double seconds) const
{
    if (!std::isfinite(seconds))
        throw std::invalid_argument("Duration must be finite");

    const long double frames = static_cast<long double>(seconds) *
                               static_cast<long double>(sampleRate_);

    if (frames > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::overflow_error("Duration is too large");

    return static_cast<std::uint64_t>(std::llround(frames));
}
}
