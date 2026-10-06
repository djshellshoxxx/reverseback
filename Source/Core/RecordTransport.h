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
        Countdown,
        Armed,
        Recording,
        Waiting,
        Playing,
        ReadyGap
    };

    enum class FinishResult
    {
        NotRecording,
        CancelledTooShort,
        Accepted
    };

    struct RecordSettings
    {
        double sampleRate{48000.0};
        std::size_t channels{1};
        double captureSeconds{5.0};
        double waitSeconds{2.0};
        double countdownSeconds{0.0};
        bool autoStart{false};
        double triggerThresholdDb{-45.0};
        double triggerSustainSeconds{0.05};
        double preRollSeconds{0.2};
        bool repeatSession{false};
        double readyGapSeconds{0.5};
    };

    void prepare(const RecordSettings& settings);
    void startPrepared(bool held = false);

    void start(std::size_t channels,
               std::uint64_t captureFrames,
               std::uint64_t waitFrames);

    void stop() noexcept;

    [[nodiscard]] FinishResult finishEarly();
    [[nodiscard]] FinishResult releaseHold() { return finishEarly(); }
    [[nodiscard]] bool replay() noexcept;

    [[nodiscard]] AudioBuffer processBlock(const AudioBuffer& input,
                                           std::uint32_t frameCount);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool hasRetainedTake() const noexcept;
    [[nodiscard]] const AudioBuffer& retainedTake() const noexcept;
    [[nodiscard]] std::uint64_t capturedFrames() const noexcept { return capturedFrames_; }

private:
    void beginCapture();
    void beginArmed();
    void promoteCompletedCapture();
    void beginPlayback() noexcept;
    void finishPlaybackPass() noexcept;
    void beginNextRepeatCycle();
    void pushPreRollFrame(const AudioBuffer& input, std::uint32_t frame);
    [[nodiscard]] bool triggerFrame(const AudioBuffer& input, std::uint32_t frame) noexcept;
    void copyPreRollIntoCapture();

    State state_{State::Ready};
    RecordSettings settings_{};
    AudioBuffer retainedTake_;
    AudioBuffer capture_;
    AudioBuffer preRoll_;

    std::uint64_t targetCaptureFrames_{0};
    std::uint64_t capturedFrames_{0};
    std::uint64_t targetWaitFrames_{0};
    std::uint64_t waitedFrames_{0};
    std::uint64_t playbackOffset_{0};
    std::uint64_t countdownFrames_{0};
    std::uint64_t countdownElapsed_{0};
    std::uint64_t readyGapFrames_{0};
    std::uint64_t readyGapElapsed_{0};
    std::uint64_t minValidFrames_{1};
    std::uint64_t triggerSustainFrames_{1};
    std::uint64_t triggerAboveFrames_{0};
    std::uint64_t preRollFrames_{0};
    std::uint64_t preRollWrite_{0};
    std::uint64_t preRollCount_{0};
    std::size_t activeChannels_{0};
    bool heldMode_{false};
    bool prepared_{false};
};
}
