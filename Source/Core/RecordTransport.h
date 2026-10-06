#pragma once

#include "ReverseBuffer.h"

#include <cstddef>
#include <cstdint>

namespace reverseback
{
class RecordTransport
{
public:
    enum class State
    {
        Ready,
        Recording,
        Waiting,
        Playing
    };

    void start(std::size_t channels,
               std::uint64_t captureFrames,
               std::uint64_t waitFrames);

    void stop() noexcept;

    [[nodiscard]] bool replay() noexcept;

    [[nodiscard]] AudioBuffer processBlock(const AudioBuffer& input,
                                           std::uint32_t frameCount);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool hasRetainedTake() const noexcept;
    [[nodiscard]] const AudioBuffer& retainedTake() const noexcept;

private:
    void promoteCompletedCapture();
    void beginPlayback() noexcept;

    State state_{State::Ready};
    AudioBuffer retainedTake_;
    AudioBuffer capture_;

    std::uint64_t targetCaptureFrames_{0};
    std::uint64_t capturedFrames_{0};
    std::uint64_t targetWaitFrames_{0};
    std::uint64_t waitedFrames_{0};
    std::uint64_t playbackOffset_{0};
    std::size_t activeChannels_{0};
};
}
