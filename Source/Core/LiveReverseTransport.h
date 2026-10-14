#pragma once

#include "ReverseBuffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reverseback
{
class LiveReverseTransport
{
public:
    enum class State
    {
        Ready,
        Filling,
        Running,
        Frozen
    };

    void start(std::size_t channels,
               std::uint64_t chunkFrames,
               std::uint64_t delayFrames);

    void stop() noexcept;
    void requestFreeze() noexcept;
    void resume();

    [[nodiscard]] AudioBuffer processBlock(const AudioBuffer& input,
                                           std::uint32_t frameCount);
    void processBlockInto(const AudioBuffer& input,
                          std::uint32_t frameCount,
                          AudioBuffer& output);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] std::uint64_t absoluteFrame() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] bool hasFrozenChunk() const noexcept;
    [[nodiscard]] const AudioBuffer& frozenChunk() const noexcept;

private:
    struct Slot
    {
        AudioBuffer audio;
        std::uint64_t startFrame{0};
    };

    void completeChunk();
    void adoptFrozenChunk();
    [[nodiscard]] Slot& writeSlot();
    [[nodiscard]] Slot& readSlot();

    State state_{State::Ready};
    std::size_t channels_{0};
    std::uint64_t chunkFrames_{0};
    std::uint64_t delayFrames_{0};
    std::uint64_t absoluteFrame_{0};
    std::uint64_t captureOffset_{0};
    std::uint64_t playbackOffset_{0};

    AudioBuffer capture_;
    AudioBuffer frozenChunk_;
    std::vector<Slot> slots_;
    std::size_t readIndex_{0};
    std::size_t writeIndex_{0};
    std::size_t queued_{0};
    bool freezeRequested_{false};
};
}
