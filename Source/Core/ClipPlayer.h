// Plays one selection of a ClipSource in either direction with speed, loop pattern and fades.
// Everything except configure() is real-time safe. See ENGINE_DESIGN section 5.
#pragma once

#include "AudioClip.h"

#include <vector>

namespace rb
{
class ClipPlayer
{
public:
    // Non-real-time: allocates scratch for blocks up to maxBlock frames at ratios up to maxRatio.
    void configure (std::size_t maxBlock, double maxRatio = 16.0);

    // ----- real-time safe -----
    void setOutputRate (double rate) noexcept;
    void setSource (const ClipSource* src, Selection sel) noexcept;   // stops immediately, rewinds
    // Changes the selection of the current source. While playing the audio keeps going (declicked) from the
    // same source position when it lies inside the new selection, otherwise from the start of it.
    void changeSelection (Selection sel) noexcept;
    void setSpeed (double speed) noexcept;
    void setDirection (Direction d) noexcept;
    void setLoop (LoopPattern l) noexcept { loop_ = l; }
    void setFadeFrames (Frame f) noexcept { fade_ = f; }
    void setDeclickFrames (Frame f) noexcept { declick_ = std::max<Frame> (1, f); }
    void setStopFadeFrames (Frame f) noexcept { stopFade_ = std::max<Frame> (1, f); }

    void play (bool fromStart) noexcept;
    void stop() noexcept;            // fades out over the stop fade, keeps the position
    void stopImmediate() noexcept;   // silent now, keeps the position

    bool isActive() const noexcept { return state_ != State::Idle; }
    bool isPlaying() const noexcept { return state_ == State::Playing; }
    bool underrun() const noexcept { return underrun_; }
    Direction direction() const noexcept { return baseDir_; }   // the user's setting (ping-pong flips only the current pass)
    double ratio() const noexcept { return ratio_; }

    // Adds up to `frames` frames into out[0..outChannels). Returns the number of frames produced;
    // fewer than requested means playback ended (or paused on an underrun).
    std::size_t process (float* const* out, std::size_t outChannels, std::size_t frames) noexcept;

    // 0..1 along the selection, left to right regardless of playback direction.
    float positionNorm() const noexcept;
    Frame segmentFrames() const noexcept { return M_; }

private:
    enum class State : std::uint8_t { Idle, Playing, Stopping };

    double curU() const noexcept { return finished_ ? 0.0 : u0_ + static_cast<double> (n_) * ratio_; }
    void updateRatio() noexcept;
    void beginSegment (double u0) noexcept;
    bool advancePass() noexcept;
    void raisePending() noexcept;
    void applyPending() noexcept;
    bool renderRaw (std::size_t m) noexcept;
    float edgeGain (Frame passFrame, Frame n) const noexcept;

    const ClipSource* src_ = nullptr;
    Selection sel_ {};
    Frame len_ = 0;
    int clipChannels_ = 1;
    double srcRate_ = 48000.0;   // cached so an idle player never dereferences src_

    double outRate_ = 48000.0;
    double speed_ = 1.0;
    double ratio_ = 1.0;
    double maxRatio_ = 16.0;
    std::size_t maxBlock_ = 0;
    Direction dir_ = Direction::Backward;       // direction of the current pass
    Direction baseDir_ = Direction::Backward;   // direction setting; dir_ differs only on odd ping-pong passes
    bool flipped_ = false;
    LoopPattern loop_ = LoopPattern::Once;
    Frame fade_ = 0, declick_ = 96, stopFade_ = 480;

    State state_ = State::Idle;
    bool finished_ = true;
    bool underrun_ = false;

    double u0_ = 0.0;
    Frame n_ = 0, M_ = 0, passFrames_ = 0, fadeEff_ = 0;

    float gain_ = 1.0f, gainTarget_ = 1.0f, gainStep_ = 0.0f;

    bool pendingDir_ = false, pendingSpeed_ = false, pendingRestart_ = false, pendingSel_ = false;
    Direction newDir_ = Direction::Backward;
    double newSpeed_ = 1.0;
    Selection newSel_ {};

    std::vector<float> tmpStore_, winStore_;
    float* tmp_[kMaxChannels] = {};
    float* win_[kMaxChannels] = {};
    std::size_t winCapacity_ = 0;
};
}  // namespace rb
