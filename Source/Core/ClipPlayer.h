#pragma once

#include "AudioClip.h"

#include <cstddef>
#include <cstdint>

namespace reverseback
{
class ClipPlayer
{
public:
    void prepare(const AudioClip& clip,
                 Selection selection,
                 Direction direction,
                 double speed,
                 LoopPattern loopPattern,
                 Frame fadeFrames);

    [[nodiscard]] std::size_t process(AudioBuffer& output, std::size_t frames);
    void stop() noexcept;
    void setDirectionAtCurrentPosition(Direction direction) noexcept;
    void setSpeedAtCurrentPosition(double speed);
    void setLoopPattern(LoopPattern loopPattern) noexcept { loopPattern_ = loopPattern; }

    [[nodiscard]] Direction activeDirection() const noexcept { return activeDirection_; }
    [[nodiscard]] double speed() const noexcept { return speed_; }
    [[nodiscard]] Frame currentSourceFrame() const noexcept;
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] Frame renderedFrames() const noexcept { return renderedFrames_; }
    [[nodiscard]] Frame passLengthFrames() const noexcept;

private:
    [[nodiscard]] float sampleAt(std::size_t channel, double sourceFrame) const;
    [[nodiscard]] double mappedSourcePosition(Frame outputFrame) const noexcept;
    [[nodiscard]] float edgeGain(Frame outputFrame) const noexcept;
    void advancePass() noexcept;

    const AudioClip* clip_{nullptr};
    Selection selection_{};
    Direction direction_{Direction::Reverse};
    Direction activeDirection_{Direction::Reverse};
    LoopPattern loopPattern_{LoopPattern::Once};
    double speed_{1.0};
    Frame fadeFrames_{0};
    Frame renderedFrames_{0};
    Frame frameInPass_{0};
    Frame transitionFrames_{0};
    Frame transitionRemaining_{0};
    bool playing_{false};
};
}
