#include "RecordTransport.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace reverseback
{
namespace
{
std::uint64_t toFrames(double seconds, double sampleRate)
{
    return static_cast<std::uint64_t>(std::llround(seconds * sampleRate));
}

float dbToLinearLocal(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}
}

void RecordTransport::prepare(const RecordSettings& settings)
{
    if (settings.sampleRate <= 0.0)
        throw std::invalid_argument("Sample rate must be positive");
    if (settings.channels == 0)
        throw std::invalid_argument("At least one channel is required");
    if (settings.captureSeconds < 0.25 || settings.captureSeconds > 60.0)
        throw std::invalid_argument("Capture duration must be 0.25 to 60 seconds");
    if (settings.waitSeconds < 0.0 || settings.waitSeconds > 30.0)
        throw std::invalid_argument("Wait must be 0 to 30 seconds");
    if (settings.countdownSeconds < 0.0)
        throw std::invalid_argument("Countdown cannot be negative");
    if (settings.readyGapSeconds < 0.0)
        throw std::invalid_argument("Ready gap cannot be negative");

    settings_ = settings;
    activeChannels_ = settings.channels;
    targetCaptureFrames_ = toFrames(settings.captureSeconds, settings.sampleRate);
    targetWaitFrames_ = toFrames(settings.waitSeconds, settings.sampleRate);
    countdownFrames_ = toFrames(settings.countdownSeconds, settings.sampleRate);
    readyGapFrames_ = toFrames(settings.readyGapSeconds, settings.sampleRate);
    minValidFrames_ = std::max<std::uint64_t>(1, toFrames(0.05, settings.sampleRate));
    triggerSustainFrames_ = std::max<std::uint64_t>(
        1, toFrames(settings.triggerSustainSeconds, settings.sampleRate));
    preRollFrames_ = std::max<std::uint64_t>(
        1, toFrames(settings.preRollSeconds, settings.sampleRate));

    preRoll_.assign(activeChannels_, std::vector<float>(
        static_cast<std::size_t>(preRollFrames_), 0.0f));

    if (capture_.size() != activeChannels_)
        capture_.assign(activeChannels_, {});
    for (auto& channel : capture_)
        channel.reserve(static_cast<std::size_t>(targetCaptureFrames_));

    previousRetainedArchived_ = false;
    if (hasRetainedTake() && retainedTake_.size() != activeChannels_)
    {
        previousRetainedTake_ = retainedTake_;
        previousRetainedArchived_ = true;
        retainedTake_.assign(activeChannels_, {});
    }
    else if (retainedTake_.empty())
    {
        retainedTake_.assign(activeChannels_, {});
    }

    for (auto& channel : retainedTake_)
        channel.reserve(static_cast<std::size_t>(targetCaptureFrames_));

    prepared_ = true;
}

void RecordTransport::startPrepared(bool held)
{
    if (!prepared_)
        throw std::logic_error("RecordTransport must be prepared before startPrepared");

    heldMode_ = held;
    capturedFrames_ = 0;
    waitedFrames_ = 0;
    playbackOffset_ = 0;
    countdownElapsed_ = 0;
    readyGapElapsed_ = 0;
    triggerAboveFrames_ = 0;
    preRollWrite_ = 0;
    preRollCount_ = 0;
    capture_.clear();

    if (heldMode_)
    {
        beginCapture();
        return;
    }

    if (countdownFrames_ > 0)
        state_ = State::Countdown;
    else if (settings_.autoStart)
        beginArmed();
    else
        beginCapture();
}

void RecordTransport::start(std::size_t channels,
                            std::uint64_t captureFrames,
                            std::uint64_t waitFrames)
{
    if (channels == 0)
        throw std::invalid_argument("At least one audio channel is required");
    if (captureFrames == 0)
        throw std::invalid_argument("Capture length must be greater than zero");

    prepared_ = false;
    settings_ = {};
    activeChannels_ = channels;
    targetCaptureFrames_ = captureFrames;
    targetWaitFrames_ = waitFrames;
    capturedFrames_ = 0;
    waitedFrames_ = 0;
    playbackOffset_ = 0;
    countdownFrames_ = 0;
    readyGapFrames_ = 0;
    minValidFrames_ = 1;
    heldMode_ = false;
    beginCapture();
}

void RecordTransport::stop() noexcept
{
    if (previousRetainedArchived_)
    {
        retainedTake_.swap(previousRetainedTake_);
        previousRetainedTake_.clear();
        previousRetainedArchived_ = false;
    }

    for (auto& channel : capture_)
        channel.clear();
    capturedFrames_ = 0;
    waitedFrames_ = 0;
    playbackOffset_ = 0;
    countdownElapsed_ = 0;
    readyGapElapsed_ = 0;
    triggerAboveFrames_ = 0;
    preRollCount_ = 0;
    heldMode_ = false;
    state_ = State::Ready;
}

RecordTransport::FinishResult RecordTransport::finishEarly()
{
    if (state_ != State::Recording)
        return FinishResult::NotRecording;

    if (capturedFrames_ < minValidFrames_)
    {
        capture_.clear();
        capturedFrames_ = 0;
        state_ = State::Ready;
        heldMode_ = false;
        return FinishResult::CancelledTooShort;
    }

    promoteCompletedCapture();
    heldMode_ = false;

    if (targetWaitFrames_ == 0)
        beginPlayback();
    else
        state_ = State::Waiting;

    return FinishResult::Accepted;
}

bool RecordTransport::replay() noexcept
{
    if (retainedTake_.empty() || retainedTake_.front().empty() || state_ != State::Ready)
        return false;

    activeChannels_ = retainedTake_.size();
    beginPlayback();
    return true;
}

AudioBuffer RecordTransport::processBlock(const AudioBuffer& input,
                                          std::uint32_t frameCount)
{
    const std::size_t outputChannels =
        activeChannels_ != 0 ? activeChannels_
                             : (!retainedTake_.empty() ? retainedTake_.size() : input.size());
    AudioBuffer output(outputChannels, std::vector<float>(frameCount, 0.0f));
    processBlockInto(input, frameCount, output);
    return output;
}

void RecordTransport::processBlockInto(const AudioBuffer& input,
                                       std::uint32_t frameCount,
                                       AudioBuffer& output)
{
    const std::size_t outputChannels =
        activeChannels_ != 0 ? activeChannels_
                             : (!retainedTake_.empty() ? retainedTake_.size() : input.size());

    if (output.size() < outputChannels)
        throw std::invalid_argument("Output has fewer channels than active transport");
    for (std::size_t channel = 0; channel < outputChannels; ++channel)
    {
        if (output[channel].size() < frameCount)
            throw std::invalid_argument("Output channel is shorter than frameCount");
        std::fill_n(output[channel].begin(), frameCount, 0.0f);
    }

    const bool needsInput = state_ == State::Recording || state_ == State::Armed;
    if (needsInput)
    {
        if (input.size() < activeChannels_)
            throw std::invalid_argument("Input has fewer channels than the active recording");

        for (std::size_t channel = 0; channel < activeChannels_; ++channel)
        {
            if (input[channel].size() < frameCount)
                throw std::invalid_argument("Input channel is shorter than frameCount");
        }
    }

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        switch (state_)
        {
            case State::Ready:
                break;

            case State::Countdown:
                ++countdownElapsed_;
                if (countdownElapsed_ >= countdownFrames_)
                {
                    countdownElapsed_ = 0;
                    if (settings_.autoStart)
                        beginArmed();
                    else
                        beginCapture();
                }
                break;

            case State::Armed:
                pushPreRollFrame(input, frame);
                if (triggerFrame(input, frame))
                {
                    copyPreRollIntoCapture();
                    state_ = State::Recording;
                    if (capturedFrames_ >= targetCaptureFrames_)
                    {
                        promoteCompletedCapture();
                        if (targetWaitFrames_ == 0)
                            beginPlayback();
                        else
                            state_ = State::Waiting;
                    }
                }
                break;

            case State::Recording:
            {
                for (std::size_t channel = 0; channel < activeChannels_; ++channel)
                    capture_[channel].push_back(input[channel][frame]);

                ++capturedFrames_;

                if (capturedFrames_ >= targetCaptureFrames_)
                {
                    promoteCompletedCapture();
                    if (targetWaitFrames_ == 0)
                        beginPlayback();
                    else
                        state_ = State::Waiting;
                }
                break;
            }

            case State::Waiting:
                ++waitedFrames_;
                if (waitedFrames_ >= targetWaitFrames_)
                    beginPlayback();
                break;

            case State::Playing:
            {
                const auto frameCountInTake = retainedTake_.front().size();
                const auto sourceIndex = frameCountInTake - 1 - playbackOffset_;

                for (std::size_t channel = 0; channel < retainedTake_.size(); ++channel)
                    output[channel][frame] = retainedTake_[channel][sourceIndex];

                ++playbackOffset_;
                if (playbackOffset_ >= frameCountInTake)
                    finishPlaybackPass();
                break;
            }

            case State::ReadyGap:
                ++readyGapElapsed_;
                if (readyGapElapsed_ >= readyGapFrames_)
                    beginNextRepeatCycle();
                break;
        }
    }

}

RecordTransport::State RecordTransport::state() const noexcept
{
    return state_;
}

bool RecordTransport::hasRetainedTake() const noexcept
{
    return !retainedTake_.empty() && !retainedTake_.front().empty();
}

const AudioBuffer& RecordTransport::retainedTake() const noexcept
{
    return retainedTake_;
}

void RecordTransport::beginCapture()
{
    if (capture_.size() != activeChannels_)
        capture_.assign(activeChannels_, {});

    for (auto& channel : capture_)
    {
        channel.clear();
        if (channel.capacity() < static_cast<std::size_t>(targetCaptureFrames_))
            channel.reserve(static_cast<std::size_t>(targetCaptureFrames_));
    }

    capturedFrames_ = 0;
    waitedFrames_ = 0;
    playbackOffset_ = 0;
    state_ = State::Recording;
}

void RecordTransport::beginArmed()
{
    triggerAboveFrames_ = 0;
    preRollWrite_ = 0;
    preRollCount_ = 0;
    for (auto& channel : preRoll_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    state_ = State::Armed;
}

void RecordTransport::promoteCompletedCapture()
{
    if (retainedTake_.size() != activeChannels_)
        retainedTake_.assign(activeChannels_, {});

    for (std::size_t channel = 0; channel < activeChannels_; ++channel)
        retainedTake_[channel].assign(capture_[channel].begin(), capture_[channel].end());

    previousRetainedTake_.clear();
    previousRetainedArchived_ = false;
    capturedFrames_ = static_cast<std::uint64_t>(retainedTake_.front().size());
    waitedFrames_ = 0;
}

void RecordTransport::beginPlayback() noexcept
{
    playbackOffset_ = 0;
    state_ = State::Playing;
}

void RecordTransport::finishPlaybackPass() noexcept
{
    playbackOffset_ = 0;

    if (prepared_ && settings_.repeatSession)
    {
        readyGapElapsed_ = 0;
        state_ = readyGapFrames_ == 0 ? State::Recording : State::ReadyGap;
        if (readyGapFrames_ == 0)
            beginCapture();
    }
    else
    {
        state_ = State::Ready;
    }
}

void RecordTransport::beginNextRepeatCycle()
{
    beginCapture();
}

void RecordTransport::pushPreRollFrame(const AudioBuffer& input, std::uint32_t frame)
{
    for (std::size_t channel = 0; channel < activeChannels_; ++channel)
        preRoll_[channel][static_cast<std::size_t>(preRollWrite_)] = input[channel][frame];

    preRollWrite_ = (preRollWrite_ + 1) % preRollFrames_;
    preRollCount_ = std::min<std::uint64_t>(preRollCount_ + 1, preRollFrames_);
}

bool RecordTransport::triggerFrame(const AudioBuffer& input, std::uint32_t frame) noexcept
{
    const auto threshold = dbToLinearLocal(settings_.triggerThresholdDb);
    float peak = 0.0f;

    for (std::size_t channel = 0; channel < activeChannels_; ++channel)
        peak = std::max(peak, std::abs(input[channel][frame]));

    if (peak >= threshold)
        ++triggerAboveFrames_;
    else
        triggerAboveFrames_ = 0;

    return triggerAboveFrames_ >= triggerSustainFrames_;
}

void RecordTransport::copyPreRollIntoCapture()
{
    if (capture_.size() != activeChannels_)
        capture_.assign(activeChannels_, {});
    for (auto& channel : capture_)
    {
        channel.clear();
        if (channel.capacity() < static_cast<std::size_t>(targetCaptureFrames_))
            channel.reserve(static_cast<std::size_t>(targetCaptureFrames_));
    }

    const auto count = std::min<std::uint64_t>(preRollCount_, targetCaptureFrames_);
    const auto start = (preRollWrite_ + preRollFrames_ - count) % preRollFrames_;

    for (std::size_t channel = 0; channel < activeChannels_; ++channel)
    {
        for (std::uint64_t i = 0; i < count; ++i)
        {
            const auto source = (start + i) % preRollFrames_;
            capture_[channel].push_back(preRoll_[channel][static_cast<std::size_t>(source)]);
        }
    }

    capturedFrames_ = count;
}
}
