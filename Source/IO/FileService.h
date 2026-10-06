#pragma once

#include "AudioClip.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace reverseback
{
class FileService
{
public:
    enum class Error
    {
        None,
        Missing,
        Unsupported,
        Corrupt,
        TooManyChannels,
        TooLong,
        SampleRateTooHigh,
        DecodedSizeTooLarge,
        Cancelled,
        AllocationFailed
    };

    struct Result
    {
        std::shared_ptr<const AudioClip> clip;
        juce::File file;
        Error error{Error::None};
        juce::String message;

        [[nodiscard]] bool ok() const noexcept { return clip != nullptr && error == Error::None; }
    };

    FileService();

    [[nodiscard]] Result load(const juce::File& file,
                              const std::atomic_bool* cancelled = nullptr);

private:
    juce::AudioFormatManager formats_;
};
}
