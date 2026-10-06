#include "RecordTransport.h"

#include <algorithm>
#include <stdexcept>

namespace reverseback
{
void RecordTransport::start(std::size_t channels,
                            std::uint64_t captureFrames,
                            std::uint64_t waitFrames)
{
    if (channels == 0)
        throw std::invalid_argument("At least one audio channel is required");
    if (captureFrames == 0)
        throw std::invalid_argument("Capture length must be greater than zero");

    activeChannels_ = channels;
    targetCaptureFrames_ = captureFrames;
    capturedFrames_ = 0;
    targetWaitFrames_ = waitFrames;
    waitedFrames_ = 0;
    playbackOffset_ = 0;

    capture_.assign(channels, {});
    for (auto& channel : capture_)
        channel.reserve(static_cast<std::size_t>(captureFrames));

    state_ = State::Recording;
}

void RecordTransport::stop() noexcept
{
    capture_.clear();
    capturedFrames_ = 0;
    waitedFrames_ = 0;
    playbackOffset_ = 0;
    targetCaptureFrames_ = 0;
    targetWaitFrames_ = 0;
    activeChannels_ = 0;
    state_ = State::Ready;
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

    if (state_ == State::Recording)
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

            case State::Recording:
            {
                for (std::size_t channel = 0; channel < activeChannels_; ++channel)
                    capture_[channel].push_back(input[channel][frame]);

                ++capturedFrames_;

                if (capturedFrames_ == targetCaptureFrames_)
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
                if (waitedFrames_ == targetWaitFrames_)
                    beginPlayback();
                break;

            case State::Playing:
            {
                const auto frameCountInTake = retainedTake_.front().size();
                const auto sourceIndex = frameCountInTake - 1 - playbackOffset_;

                for (std::size_t channel = 0; channel < retainedTake_.size(); ++channel)
                    output[channel][frame] = retainedTake_[channel][sourceIndex];

                ++playbackOffset_;
                if (playbackOffset_ == frameCountInTake)
                {
                    playbackOffset_ = 0;
                    state_ = State::Ready;
                }
                break;
            }
        }
    }

    return output;
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

void RecordTransport::promoteCompletedCapture()
{
    retainedTake_ = std::move(capture_);
    capture_.clear();
    capturedFrames_ = targetCaptureFrames_;
}

void RecordTransport::beginPlayback() noexcept
{
    playbackOffset_ = 0;
    state_ = State::Playing;
}
}
