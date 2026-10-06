#include "ClipPlayer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace reverseback
{
namespace
{
constexpr int kSincRadius = 4;

double sinc(double x)
{
    if (std::abs(x) < 1.0e-12)
        return 1.0;
    const auto px = std::numbers::pi_v<double> * x;
    return std::sin(px) / px;
}

double lanczos(double x)
{
    const auto ax = std::abs(x);
    if (ax >= static_cast<double>(kSincRadius))
        return 0.0;
    return sinc(x) * sinc(x / static_cast<double>(kSincRadius));
}
}

void ClipPlayer::prepare(const AudioClip& clip,
                         Selection selection,
                         Direction direction,
                         double speed,
                         LoopPattern loopPattern,
                         Frame fadeFrames)
{
    if (selection.begin >= selection.end || selection.end > clip.frameCount())
        throw std::invalid_argument("Invalid clip selection");
    if (!std::isfinite(speed) || speed < 0.5 || speed > 2.0)
        throw std::invalid_argument("Speed must be within 0.5x to 2x");

    clip_ = &clip;
    selection_ = selection;
    direction_ = direction;
    activeDirection_ = direction;
    speed_ = speed;
    loopPattern_ = loopPattern;
    fadeFrames_ = fadeFrames;
    renderedFrames_ = 0;
    frameInPass_ = 0;
    transitionFrames_ = std::max<Frame>(1, fadeFrames_);
    transitionRemaining_ = 0;
    playing_ = true;
}

std::size_t ClipPlayer::process(AudioBuffer& output, std::size_t frames)
{
    if (!playing_ || clip_ == nullptr || output.empty())
        return 0;

    if (output.size() < clip_->channels())
        throw std::invalid_argument("Output has fewer channels than clip");

    for (std::size_t channel = 0; channel < clip_->channels(); ++channel)
    {
        if (output[channel].size() < frames)
            throw std::invalid_argument("Output channel is shorter than requested frame count");
    }

    std::size_t written = 0;
    for (; written < frames && playing_; ++written)
    {
        const auto src = mappedSourcePosition(frameInPass_);
        const auto gain = edgeGain(frameInPass_);

        for (std::size_t channel = 0; channel < clip_->channels(); ++channel)
            output[channel][written] = sampleAt(channel, src) * gain;

        ++frameInPass_;
        ++renderedFrames_;
        if (transitionRemaining_ > 0)
            --transitionRemaining_;

        if (frameInPass_ >= passLengthFrames())
            advancePass();
    }

    return written;
}

void ClipPlayer::stop() noexcept
{
    playing_ = false;
    frameInPass_ = 0;
    transitionRemaining_ = 0;
}

void ClipPlayer::setDirectionAtCurrentPosition(Direction direction) noexcept
{
    if (clip_ == nullptr || direction == activeDirection_)
        return;

    const auto source = static_cast<double>(currentSourceFrame());
    activeDirection_ = direction;

    if (activeDirection_ == Direction::Forward)
        frameInPass_ = static_cast<Frame>(std::llround(
            std::max(0.0, source - static_cast<double>(selection_.begin)) / speed_));
    else
        frameInPass_ = static_cast<Frame>(std::llround(
            std::max(0.0, static_cast<double>(selection_.end - 1) - source) / speed_));

    frameInPass_ = std::min(frameInPass_, passLengthFrames() > 0 ? passLengthFrames() - 1 : 0);
    transitionRemaining_ = transitionFrames_;
}

void ClipPlayer::setSpeedAtCurrentPosition(double speed)
{
    if (!std::isfinite(speed) || speed < 0.5 || speed > 2.0)
        throw std::invalid_argument("Speed must be within 0.5x to 2x");

    if (clip_ == nullptr)
    {
        speed_ = speed;
        return;
    }

    const auto source = static_cast<double>(currentSourceFrame());
    speed_ = speed;

    if (activeDirection_ == Direction::Forward)
        frameInPass_ = static_cast<Frame>(std::llround(
            std::max(0.0, source - static_cast<double>(selection_.begin)) / speed_));
    else
        frameInPass_ = static_cast<Frame>(std::llround(
            std::max(0.0, static_cast<double>(selection_.end - 1) - source) / speed_));

    frameInPass_ = std::min(frameInPass_, passLengthFrames() > 0 ? passLengthFrames() - 1 : 0);
    transitionRemaining_ = transitionFrames_;
}

Frame ClipPlayer::currentSourceFrame() const noexcept
{
    if (clip_ == nullptr || selection_.end <= selection_.begin)
        return 0;

    const auto mapped = mappedSourcePosition(frameInPass_);
    return static_cast<Frame>(std::clamp<double>(
        std::llround(mapped),
        static_cast<double>(selection_.begin),
        static_cast<double>(selection_.end - 1)));
}

Frame ClipPlayer::passLengthFrames() const noexcept
{
    if (selection_.end <= selection_.begin)
        return 0;

    const auto sourceFrames = static_cast<double>(selection_.end - selection_.begin);
    return static_cast<Frame>(std::llround(sourceFrames / speed_));
}

float ClipPlayer::sampleAt(std::size_t channel, double sourceFrame) const
{
    const auto lower = static_cast<long long>(std::floor(sourceFrame));
    double sum = 0.0;
    double weights = 0.0;

    for (int tap = -kSincRadius + 1; tap <= kSincRadius; ++tap)
    {
        auto index = lower + tap;
        index = std::clamp<long long>(
            index,
            static_cast<long long>(selection_.begin),
            static_cast<long long>(selection_.end - 1));

        const auto weight = lanczos(sourceFrame - static_cast<double>(index));
        sum += static_cast<double>(clip_->sample(channel, static_cast<Frame>(index))) * weight;
        weights += weight;
    }

    if (std::abs(weights) < 1.0e-12)
        return clip_->sample(channel, static_cast<Frame>(std::clamp<long long>(
            static_cast<long long>(std::llround(sourceFrame)),
            static_cast<long long>(selection_.begin),
            static_cast<long long>(selection_.end - 1))));

    return static_cast<float>(sum / weights);
}

double ClipPlayer::mappedSourcePosition(Frame outputFrame) const noexcept
{
    const auto sourceOffset = static_cast<double>(outputFrame) * speed_;

    if (activeDirection_ == Direction::Forward)
        return std::min<double>(
            static_cast<double>(selection_.end - 1),
            static_cast<double>(selection_.begin) + sourceOffset);

    return std::max<double>(
        static_cast<double>(selection_.begin),
        static_cast<double>(selection_.end - 1) - sourceOffset);
}

float ClipPlayer::edgeGain(Frame outputFrame) const noexcept
{
    float transitionGain = 1.0f;
    if (transitionRemaining_ > 0 && transitionFrames_ > 0)
        transitionGain = 1.0f - static_cast<float>(transitionRemaining_) /
                                   static_cast<float>(transitionFrames_);

    if (fadeFrames_ == 0)
        return transitionGain;

    const auto length = passLengthFrames();
    if (length <= 1)
        return 1.0f;

    const auto usableFade = std::min<Frame>(fadeFrames_, length / 2);
    if (usableFade == 0)
        return 1.0f;

    if (outputFrame < usableFade)
        return transitionGain * static_cast<float>(outputFrame) / static_cast<float>(usableFade);

    const auto remaining = length - 1 - outputFrame;
    if (remaining < usableFade)
        return transitionGain * static_cast<float>(remaining) / static_cast<float>(usableFade);

    return transitionGain;
}

void ClipPlayer::advancePass() noexcept
{
    frameInPass_ = 0;

    switch (loopPattern_)
    {
        case LoopPattern::Once:
            playing_ = false;
            break;
        case LoopPattern::Loop:
            activeDirection_ = direction_;
            break;
        case LoopPattern::PingPong:
            activeDirection_ = activeDirection_ == Direction::Forward
                ? Direction::Reverse
                : Direction::Forward;
            break;
    }
}
}
