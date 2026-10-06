#pragma once

#include "AudioClip.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace reverseback
{
struct ExportSettings
{
    Direction direction{Direction::Reverse};
    double speed{1.0};
    Frame fadeFrames{0};
    int bitDepth{32};
    double targetSampleRate{0.0};
    bool normalizeToMinusOneDb{false};
    bool allowOverwrite{false};
};

class ExportService
{
public:
    enum class Error
    {
        None,
        InvalidSelection,
        DestinationExists,
        Cancelled,
        WouldClipPcm,
        CreateFailed,
        WriteFailed,
        RenameFailed
    };

    struct Result
    {
        Error error{Error::None};
        juce::String message;
        std::uint64_t framesWritten{0};

        [[nodiscard]] bool ok() const noexcept { return error == Error::None; }
    };

    [[nodiscard]] Result writeWav(const AudioClip& clip,
                                  Selection selection,
                                  const juce::File& destination,
                                  const ExportSettings& settings,
                                  const std::atomic_bool* cancelled = nullptr) const;
};
}
