#include "ExportService.h"

#include "Stages.h"

namespace rb
{
namespace
{
ExportOutcome failure (ExportError e, juce::String m)
{
    ExportOutcome o;
    o.error = e;
    o.message = std::move (m);
    return o;
}

RenderSpec toRenderSpec (const ExportRequest& r)
{
    RenderSpec s;
    s.source = r.source.get();
    s.selection = r.selection;
    s.direction = r.direction;
    s.speed = r.speed;
    s.outRate = r.settings.sampleRate > 0 ? static_cast<double> (r.settings.sampleRate) : 0.0;
    s.fadeFrames = r.fadeFrames;
    return s;
}

bool valid (const ExportRequest& r)
{
    return r.source != nullptr && ! r.selection.empty() && r.selection.end <= r.source->frameCount()
           && r.speed >= kMinSpeed && r.speed <= kMaxSpeed;
}

float lsbTpdf (juce::Random& rng, float lsb)
{
    return (rng.nextFloat() + rng.nextFloat() - 1.0f) * lsb;
}
}  // namespace

ExportOutcome analyzeExport (const ExportRequest& req, const std::atomic<bool>* cancel)
{
    if (! valid (req))
        return failure (ExportError::Invalid, "There is nothing to export.");

    ExportOutcome out;
    out.analysis.channels = std::min (req.source->channels(), static_cast<int> (kMaxChannels));
    out.analysis.outRate = req.settings.sampleRate > 0 ? static_cast<double> (req.settings.sampleRate) : req.source->sampleRate();
    float peak = 0.0f;
    Frame frames = 0;
    const auto res = renderOffline (toRenderSpec (req), [&] (const float* const* p, std::size_t ch, std::size_t n)
    {
        for (std::size_t c = 0; c < ch; ++c)
            for (std::size_t i = 0; i < n; ++i)
                peak = std::max (peak, std::abs (p[c][i]));
        frames += n;
        return true;
    }, cancel);

    if (res == RenderResult::Cancelled)
        return failure (ExportError::Cancelled, "Export cancelled.");
    if (res == RenderResult::ReadFailed)
        return failure (ExportError::ReadFailed, "The audio could not be read fast enough from the temporary cache.");
    out.analysis.frames = frames;
    out.analysis.peak = peak;
    return out;
}

ExportOutcome writeExport (const ExportRequest& req, const std::atomic<bool>* cancel, const std::function<void (float)>& progress)
{
    // Ask about replacing first: every later question the caller may ask ("export anyway?", "normalise?")
    // re-runs with overwrite already decided, so an existing file is never replaced without a clear yes.
    if (valid (req) && req.destination.existsAsFile() && ! req.overwrite)
        return failure (ExportError::DestinationExists, req.destination.getFileName() + " already exists.");

    ExportOutcome analysis = analyzeExport (req, cancel);
    if (! analysis.ok())
        return analysis;

    const bool pcm = req.settings.format == ExportSettings::Format::Pcm24;
    if (pcm && analysis.analysis.peak > 1.0f && ! req.settings.normalize)
    {
        analysis.error = ExportError::WouldClip;
        analysis.message = "This export is louder than full scale and would clip in 24-bit PCM. "
                           "Export as 32-bit float or normalise to -1 dBFS.";
        return analysis;
    }
    const juce::File dir = req.destination.getParentDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
    {
        analysis.error = ExportError::WriteFailed;
        analysis.message = "The destination folder does not exist and could not be created.";
        return analysis;
    }
    const juce::File tmp = dir.getNonexistentChildFile ("." + req.destination.getFileNameWithoutExtension() + ".rb-tmp", ".wav", false);

    auto cleanup = [&] { tmp.deleteFile(); };
    auto stream = tmp.createOutputStream (65536);
    if (stream == nullptr)
    {
        analysis.error = ExportError::WriteFailed;
        analysis.message = "Could not create a file in " + dir.getFullPathName() + ". Choose another folder.";
        return analysis;
    }
    std::unique_ptr<juce::OutputStream> base (std::move (stream));

    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (analysis.analysis.outRate)
                       .withNumChannels (analysis.analysis.channels)
                       .withBitsPerSample (pcm ? 24 : 32)
                       .withSampleFormat (pcm ? juce::AudioFormatWriterOptions::SampleFormat::integral
                                              : juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor (base, options);
    if (writer == nullptr)
    {
        base.reset();
        cleanup();
        analysis.error = ExportError::WriteFailed;
        analysis.message = "The WAV writer could not be opened.";
        return analysis;
    }

    const float gain = req.settings.normalize && analysis.analysis.peak > 0.0f ? OutputLimiter::kCeiling / analysis.analysis.peak : 1.0f;
    const bool dither = pcm && req.settings.dither;
    juce::Random rng (0x5eed1234);
    constexpr float lsb24 = 1.0f / 8388608.0f;
    Frame written = 0;
    bool writeOk = true;
    std::vector<float> scratch[kMaxChannels];

    const auto res = renderOffline (toRenderSpec (req), [&] (const float* const* p, std::size_t ch, std::size_t n)
    {
        const float* planes[kMaxChannels] = { p[0], p[ch - 1] };
        if (gain != 1.0f || dither)
        {
            for (std::size_t c = 0; c < ch; ++c)
            {
                scratch[c].assign (p[c], p[c] + n);
                for (auto& v : scratch[c])
                    v = v * gain + (dither ? lsbTpdf (rng, lsb24) : 0.0f);
                planes[c] = scratch[c].data();
            }
        }
        if (req.debugFailAfterFrames >= 0 && static_cast<long long> (written + n) > req.debugFailAfterFrames)
        {
            writeOk = false;
            return false;
        }
        if (! writer->writeFromFloatArrays (planes, static_cast<int> (ch), static_cast<int> (n)))
        {
            writeOk = false;
            return false;
        }
        written += n;
        if (progress && analysis.analysis.frames > 0)
            progress (static_cast<float> (static_cast<double> (written) / static_cast<double> (analysis.analysis.frames)));
        return true;
    }, cancel);

    writer.reset();   // flush and close
    base.reset();

    if (res == RenderResult::Cancelled)
    {
        cleanup();
        analysis.error = ExportError::Cancelled;
        analysis.message = "Export cancelled.";
        return analysis;
    }
    const juce::int64 expected = static_cast<juce::int64> (analysis.analysis.frames) * analysis.analysis.channels * (pcm ? 3 : 4);
    if (res != RenderResult::Done || ! writeOk || tmp.getSize() < expected)
    {
        cleanup();
        analysis.error = ExportError::WriteFailed;
        analysis.message = "Writing the file failed (the disk may be full). Your recording is still in memory; try another location.";
        return analysis;
    }

    // replaceFileIn is a single rename (ReplaceFile on Windows): the old file is never missing in between
    const bool moved = req.destination.existsAsFile() ? tmp.replaceFileIn (req.destination) : tmp.moveFileTo (req.destination);
    if (! moved)
    {
        cleanup();
        analysis.error = ExportError::WriteFailed;
        analysis.message = "Could not replace " + req.destination.getFileName() + ".";
        return analysis;
    }
    analysis.written = req.destination;
    return analysis;
}

juce::String exportSummary (const ExportRequest& req, double sourceRate, int sourceChannels)
{
    const double outRate = req.settings.sampleRate > 0 ? static_cast<double> (req.settings.sampleRate) : sourceRate;
    const double ratio = sourceRate / outRate * req.speed;
    const double seconds = static_cast<double> (req.selection.length()) / sourceRate / req.speed;
    juce::ignoreUnused (ratio);
    juce::StringArray lines;
    lines.add (juce::String (req.direction == Direction::Backward ? "Backwards" : "Forwards") + " at " + juce::String (req.speed, 2) + "x, "
               + juce::String (seconds, 2) + " s");
    lines.add (juce::String (sourceChannels == 1 ? "Mono" : "Stereo") + ", " + juce::String (outRate / 1000.0, 1) + " kHz, "
               + (req.settings.format == ExportSettings::Format::Float32 ? "32-bit float" : "24-bit PCM"));
    lines.add (req.fadeFrames == 0 ? "Exact samples (no edge fades)"
                                   : "Edge fades of " + juce::String (static_cast<double> (req.fadeFrames) / sourceRate * 1000.0, 1) + " ms");
    if (req.settings.normalize)
        lines.add ("Peak normalised to -1 dBFS");
    return lines.joinIntoString ("\n");
}

// ------------------------------------------------------------------ ExportService
struct ExportService::Job : public juce::Thread
{
    Job (ExportRequest r, std::function<void (float)> p, std::function<void (ExportOutcome)> d, std::atomic<bool>& runningFlag)
        : juce::Thread ("ReverseBack export"), req (std::move (r)), progress (std::move (p)), done (std::move (d)), running (runningFlag)
    {
    }
    ~Job() override
    {
        cancelled.store (true);
        stopThread (10000);
    }
    void run() override
    {
        auto token = alive;
        ExportOutcome out = writeExport (req, &cancelled, [&] (float p)
        {
            juce::MessageManager::callAsync ([token, cb = progress, p]
            {
                if (token->load() && cb)
                    cb (p);
            });
        });
        if (! alive->load())
            return;
        juce::MessageManager::callAsync ([token, flag = &running, cb = done, o = std::move (out)]() mutable
        {
            if (! token->load())
                return;
            flag->store (false);
            if (cb)
                cb (std::move (o));
        });
    }

    ExportRequest req;
    std::function<void (float)> progress;
    std::function<void (ExportOutcome)> done;
    std::atomic<bool>& running;
    std::atomic<bool> cancelled { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

ExportService::ExportService() = default;

ExportService::~ExportService() { cancel(); }

void ExportService::cancel()
{
    if (job_ != nullptr)
    {
        job_->alive->store (false);
        job_->cancelled.store (true);
        job_.reset();
    }
    running_.store (false);
}

void ExportService::run (ExportRequest req, std::function<void (float)> progress, std::function<void (ExportOutcome)> done)
{
    cancel();
    running_.store (true);
    job_ = std::make_unique<Job> (std::move (req), std::move (progress), std::move (done), running_);
    job_->startThread();
}
}  // namespace rb
