// Record & Reverse transport: exact frame-counted capture -> wait -> playback (ENGINE_DESIGN section 7).
#pragma once

#include "ClipPlayer.h"
#include "Events.h"
#include "SignalTools.h"

namespace rb
{
struct RecordSettings
{
    Frame captureFrames = 0;
    Frame waitFrames = 0;
    Frame countdownFrames = 0;
    Frame tailGapFrames = 0;
    bool autoStart = false;
    bool repeat = false;
    double thresholdDb = -45.0;
};

class RecordTransport
{
public:
    enum class State : std::uint8_t { Ready, Countdown, Armed, Recording, Waiting, Playing, ReadyGap };

    // Non-real-time.
    void prepare (double rate, std::size_t maxBlock, EventSink* sink);

    // ----- real-time (audio thread) -----
    // `take` must be a zeroed, unsealed clip whose capacity covers the capture (or the hold maximum).
    bool start (const RecordSettings& s, std::shared_ptr<AudioClip> take, bool held) noexcept;
    void provideSpare (std::shared_ptr<AudioClip> take) noexcept;
    void finishEarly() noexcept;   // timed capture: close with what has been recorded
    void releaseHold() noexcept;
    bool replay() noexcept;
    void stop() noexcept;
    void stopImmediate() noexcept;
    void setTakeSelection (Selection sel) noexcept;   // {0,0} = whole take (Trim silence / Undo)

    void process (const float* const* in, std::size_t inChannels, float* const* out, std::size_t frames) noexcept;

    ClipPlayer& player() noexcept { return player_; }

    State state() const noexcept { return state_; }
    bool busy() const noexcept { return state_ != State::Ready || player_.isActive(); }
    Frame stateFrame() const noexcept;
    Frame stateLength() const noexcept;
    Frame countdownLeft() const noexcept { return state_ == State::Countdown ? cdLeft_ : 0; }
    float playhead() const noexcept { return player_.positionNorm(); }
    std::uint32_t takeId() const noexcept { return takeId_; }
    Frame takeFrames() const noexcept { return retained_ ? retained_->frameCount() : 0; }
    bool hasTake() const noexcept { return retained_ != nullptr; }
    const std::shared_ptr<const AudioClip>& retained() const noexcept { return retained_; }

private:
    void requestSpare() noexcept;
    void beginArmedOrRecording() noexcept;
    void beginRecording() noexcept;
    void onTrigger() noexcept;
    void closeCapture() noexcept;
    void cancelCapture (bool tooShort) noexcept;
    void beginPlaying() noexcept;
    void onPlayFinished() noexcept;
    void discardCurrent() noexcept;
    void copyInput (const float* const* in, std::size_t inCh, std::size_t pos, std::size_t m) noexcept;

    double rate_ = 48000.0;
    EventSink* sink_ = nullptr;
    State state_ = State::Ready;
    RecordSettings cfg_ {};

    std::shared_ptr<AudioClip> current_, spare_;
    std::shared_ptr<const AudioClip> retained_;
    Selection takeSel_ {};
    std::uint32_t takeId_ = 0;

    bool held_ = false, repeat_ = false;
    Frame captured_ = 0, target_ = 0, minFrames_ = 0, maxHoldFrames_ = 0;
    Frame cdLeft_ = 0, waitLeft_ = 0, gapLeft_ = 0, gapExtra_ = 0;
    int takeCh_ = 1;
    Frame spareCapacity_ = 0;
    int spareChannels_ = 1;
    double spareRate_ = 48000.0;
    const AudioClip* playingTake_ = nullptr;

    PreRollRing ring_;
    VoiceTrigger trigger_;
    ClipPlayer player_;
};
}  // namespace rb
