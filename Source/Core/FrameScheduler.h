#pragma once

#include <cstdint>
#include <vector>

namespace reverseback
{
class FrameScheduler
{
public:
    enum class EventType
    {
        CaptureEnd,
        PlaybackStart,
        PlaybackEnd
    };

    struct Event
    {
        EventType type;
        std::uint64_t absoluteFrame;
    };

    explicit FrameScheduler(std::uint32_t sampleRate);

    void configureRecord(double captureSeconds, double waitSeconds);

    [[nodiscard]] std::uint64_t captureFrames() const noexcept;
    [[nodiscard]] std::uint64_t waitFrames() const noexcept;
    [[nodiscard]] std::uint64_t playbackStartFrame() const noexcept;
    [[nodiscard]] std::uint64_t playbackFrames() const noexcept;

    [[nodiscard]] std::vector<Event> eventsInBlock(
        std::uint64_t blockStartFrame,
        std::uint32_t blockFrameCount) const;

private:
    [[nodiscard]] std::uint64_t secondsToFrames(double seconds) const;

    std::uint32_t sampleRate_;
    std::uint64_t captureFrames_{0};
    std::uint64_t waitFrames_{0};
};
}
