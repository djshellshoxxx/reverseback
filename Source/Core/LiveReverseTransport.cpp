#include "LiveReverseTransport.h"

#include <algorithm>
#include <stdexcept>

namespace reverseback
{
void LiveReverseTransport::start(std::size_t channels,
                                 std::uint64_t chunkFrames,
                                 std::uint64_t delayFrames)
{
    if (channels == 0)
        throw std::invalid_argument("At least one audio channel is required");
    if (chunkFrames == 0)
        throw std::invalid_argument("Live chunk length must be greater than zero");

    channels_ = channels;
    chunkFrames_ = chunkFrames;
    delayFrames_ = delayFrames;
    absoluteFrame_ = 0;
    captureOffset_ = 0;
    playbackOffset_ = 0;
    readIndex_ = 0;
    writeIndex_ = 0;
    queued_ = 0;

    capture_.assign(channels_, std::vector<float>(static_cast<std::size_t>(chunkFrames_), 0.0f));

    const auto delayChunks =
        static_cast<std::size_t>((delayFrames_ + chunkFrames_ - 1) / chunkFrames_);
    const auto slotCount = delayChunks + 4;

    slots_.assign(slotCount, {});
    for (auto& slot : slots_)
        slot.audio.assign(channels_, std::vector<float>(static_cast<std::size_t>(chunkFrames_), 0.0f));

    state_ = State::Filling;
}

void LiveReverseTransport::stop() noexcept
{
    state_ = State::Ready;
    channels_ = 0;
    chunkFrames_ = 0;
    delayFrames_ = 0;
    captureOffset_ = 0;
    playbackOffset_ = 0;
    queued_ = 0;
    readIndex_ = 0;
    writeIndex_ = 0;
}

AudioBuffer LiveReverseTransport::processBlock(const AudioBuffer& input,
                                               std::uint32_t frameCount)
{
    const auto outputChannels = channels_ != 0 ? channels_ : input.size();
    AudioBuffer output(outputChannels, std::vector<float>(frameCount, 0.0f));

    if (state_ == State::Ready)
    {
        absoluteFrame_ += frameCount;
        return output;
    }

    if (input.size() < channels_)
        throw std::invalid_argument("Input has fewer channels than active Live Reverse");

    for (std::size_t channel = 0; channel < channels_; ++channel)
    {
        if (input[channel].size() < frameCount)
            throw std::invalid_argument("Input channel is shorter than frameCount");
    }

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        for (std::size_t channel = 0; channel < channels_; ++channel)
            capture_[channel][static_cast<std::size_t>(captureOffset_)] = input[channel][frame];

        ++captureOffset_;
        if (captureOffset_ == chunkFrames_)
            completeChunk();

        if (queued_ > 0)
        {
            auto& slot = readSlot();

            if (absoluteFrame_ >= slot.startFrame)
            {
                const auto sourceIndex = chunkFrames_ - 1 - playbackOffset_;
                for (std::size_t channel = 0; channel < channels_; ++channel)
                    output[channel][frame] = slot.audio[channel][static_cast<std::size_t>(sourceIndex)];

                ++playbackOffset_;
                state_ = State::Running;

                if (playbackOffset_ == chunkFrames_)
                {
                    playbackOffset_ = 0;
                    readIndex_ = (readIndex_ + 1) % slots_.size();
                    --queued_;
                }
            }
        }

        ++absoluteFrame_;
    }

    return output;
}

LiveReverseTransport::State LiveReverseTransport::state() const noexcept
{
    return state_;
}

std::uint64_t LiveReverseTransport::absoluteFrame() const noexcept
{
    return absoluteFrame_;
}

std::size_t LiveReverseTransport::capacity() const noexcept
{
    return slots_.size();
}

void LiveReverseTransport::completeChunk()
{
    if (queued_ == slots_.size())
        throw std::runtime_error("Audio processing fell behind");

    auto& slot = writeSlot();
    for (std::size_t channel = 0; channel < channels_; ++channel)
        std::copy(capture_[channel].begin(), capture_[channel].end(), slot.audio[channel].begin());

    slot.startFrame = absoluteFrame_ + 1 + delayFrames_;

    writeIndex_ = (writeIndex_ + 1) % slots_.size();
    ++queued_;
    captureOffset_ = 0;
}

LiveReverseTransport::Slot& LiveReverseTransport::writeSlot()
{
    return slots_[writeIndex_];
}

LiveReverseTransport::Slot& LiveReverseTransport::readSlot()
{
    return slots_[readIndex_];
}
}
