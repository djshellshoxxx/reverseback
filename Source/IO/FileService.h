// Cancellable decode of WAV / AIFF / FLAC files with the V1 limits (ENGINE_DESIGN section 11).
#pragma once

#include "DiskClipSource.h"
#include "WaveformOverview.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace rb
{
enum class LoadError
{
    None,
    NotFound,
    Unreadable,
    Unsupported,
    TooManyChannels,
    TooLong,
    RateTooHigh,
    TooLarge,
    DiskFull,
    Cancelled,
    Corrupt
};

// A loaded, immutable file. Destroying the asset removes its disk cache (if any).
struct FileAsset
{
    std::shared_ptr<const ClipSource> source;
    std::shared_ptr<const AudioClip> ram;          // set when decoded into memory
    std::shared_ptr<DiskClipSource> disk;          // set when disk backed
    std::shared_ptr<const WaveformOverview> overview;
    juce::File file;
    juce::String name, formatName;
    double sampleRate = 48000.0;
    int channels = 1;
    Frame frames = 0;
    juce::File cacheDir;

    double durationSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }
    ~FileAsset();
};

struct LoadOutcome
{
    LoadError error = LoadError::None;
    juce::String message;
    std::shared_ptr<FileAsset> asset;
    bool ok() const noexcept { return error == LoadError::None && asset != nullptr; }
};

struct LoadOptions
{
    std::uint64_t ramLimitBytes = 256ull * 1024 * 1024;        // above this, decode into a disk cache
    std::uint64_t maxDecodedBytes = 3ull * 1024 * 1024 * 1024;  // V1 section 5.4
    double maxSeconds = 30.0 * 60.0;
    double maxRate = 192000.0;
    juce::File cacheRoot;                                       // default: system temp directory
};

juce::String describe (LoadError e);

// Synchronous decode (runs on a worker thread in the app, directly in tests).
LoadOutcome loadAudioFile (const juce::File& file, const LoadOptions& options, const std::atomic<bool>* cancel,
                           const std::function<void (float)>& progress = {});

// Removes ReverseBack cache directories left behind by processes that no longer exist.
void cleanupAbandonedCaches (const juce::File& root);

// Owns one background load at a time. Callbacks arrive on the message thread; a newer load or
// destruction cancels the running one, and a cancelled load never calls back.
class FileService
{
public:
    explicit FileService (LoadOptions options = {});
    ~FileService();

    void load (const juce::File& file, std::function<void (float)> progress, std::function<void (LoadOutcome)> done);
    void cancel();
    bool isLoading() const noexcept { return loading_.load(); }

private:
    struct Job;
    LoadOptions options_;
    std::unique_ptr<Job> job_;
    std::atomic<bool> loading_ { false };
};
}  // namespace rb
