// WAV export through the playback renderer (ENGINE_DESIGN section 11, V1 section 5.5).
#pragma once

#include "OfflineRender.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace rb
{
struct ExportSettings
{
    enum class Format { Float32, Pcm24 };
    Format format = Format::Float32;
    int sampleRate = 0;        // 0 = source rate; otherwise 44100 or 48000
    bool dither = false;       // 24-bit only
    bool normalize = false;    // peak normalise to -1 dBFS (explicit, off by default)
};

struct ExportRequest
{
    std::shared_ptr<const ClipSource> source;
    Selection selection;
    Direction direction = Direction::Backward;
    double speed = 1.0;
    Frame fadeFrames = 0;      // 0 = Exact Samples
    ExportSettings settings;
    juce::File destination;
    bool overwrite = false;    // the UI sets this only after the user confirmed
    int debugFailAfterFrames = -1;   // test hook: simulate a write failure (disk full)
};

enum class ExportError { None, Invalid, Cancelled, ReadFailed, DestinationExists, WouldClip, WriteFailed };

struct ExportAnalysis
{
    Frame frames = 0;
    double outRate = 0.0;
    int channels = 1;
    float peak = 0.0f;         // linear sample peak of the rendered signal
    double seconds() const noexcept { return outRate > 0.0 ? static_cast<double> (frames) / outRate : 0.0; }
};

struct ExportOutcome
{
    ExportError error = ExportError::None;
    juce::String message;
    juce::File written;
    ExportAnalysis analysis;
    bool ok() const noexcept { return error == ExportError::None; }
};

// Pass 1: frame count and peak of exactly what would be written. Never touches the destination.
ExportOutcome analyzeExport (const ExportRequest& req, const std::atomic<bool>* cancel);

// Pass 2: refuses PCM that would clip (unless normalising), writes a temp file next to the destination,
// then renames it over the destination. Any failure leaves the destination as it was.
ExportOutcome writeExport (const ExportRequest& req, const std::atomic<bool>* cancel, const std::function<void (float)>& progress = {});

// Human readable summary of what will be written (shown in the export sheet).
juce::String exportSummary (const ExportRequest& req, double sourceRate, int sourceChannels);

class ExportService
{
public:
    ExportService();
    ~ExportService();

    // Callbacks arrive on the message thread. A new run cancels the previous one.
    void run (ExportRequest req, std::function<void (float)> progress, std::function<void (ExportOutcome)> done);
    void cancel();
    bool isRunning() const noexcept { return running_.load(); }

private:
    struct Job;
    std::unique_ptr<Job> job_;
    std::atomic<bool> running_ { false };
};
}  // namespace rb
