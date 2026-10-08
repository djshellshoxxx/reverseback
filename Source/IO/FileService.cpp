#include "FileService.h"

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <errno.h>
 #include <signal.h>
 #include <unistd.h>
#endif

namespace rb
{
namespace
{
constexpr const char* kCachePrefix = "ReverseBack-cache-";

int currentProcessId()
{
#if JUCE_WINDOWS
    return _getpid();
#else
    return static_cast<int> (getpid());
#endif
}

bool processIsAlive (int pid)
{
#if JUCE_WINDOWS
    juce::ignoreUnused (pid);
    return false;   // cleaned up by age instead (see cleanupAbandonedCaches)
#else
    return pid > 0 && (kill (pid, 0) == 0 || errno == EPERM);
#endif
}

LoadOutcome fail (LoadError e, juce::String message)
{
    LoadOutcome o;
    o.error = e;
    o.message = std::move (message);
    return o;
}
}  // namespace

FileAsset::~FileAsset()
{
    // Disk-backed sources delete their own cache file; remove the directory if anything is left.
    if (cacheDir != juce::File() && cacheDir.isDirectory() && disk == nullptr)
        cacheDir.deleteRecursively();
}

juce::String describe (LoadError e)
{
    switch (e)
    {
        case LoadError::None: return {};
        case LoadError::NotFound: return "The file could not be found.";
        case LoadError::Unreadable: return "The file could not be opened.";
        case LoadError::Unsupported: return "This is not a WAV, AIFF or FLAC file ReverseBack can read.";
        case LoadError::TooManyChannels: return "Only mono and stereo files are supported.";
        case LoadError::TooLong: return "Files longer than 30 minutes are not supported.";
        case LoadError::RateTooHigh: return "Sample rates above 192 kHz are not supported.";
        case LoadError::TooLarge: return "The decoded audio would be larger than 3 GiB.";
        case LoadError::DiskFull: return "There is not enough free disk space for the temporary cache.";
        case LoadError::Cancelled: return "Loading was cancelled.";
        case LoadError::Corrupt: return "The file is damaged or ended unexpectedly.";
    }
    return {};
}

void cleanupAbandonedCaches (const juce::File& root)
{
    for (const auto& entry : juce::RangedDirectoryIterator (root, false, juce::String (kCachePrefix) + "*", juce::File::findDirectories))
    {
        const juce::File dir = entry.getFile();
        const juce::String name = dir.getFileName().substring (static_cast<int> (std::strlen (kCachePrefix)));
        const int pid = name.upToFirstOccurrenceOf ("-", false, false).getIntValue();
        const bool old = dir.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (2);
        if (pid != currentProcessId() && (! processIsAlive (pid) || old))
            dir.deleteRecursively();
    }
}

LoadOutcome loadAudioFile (const juce::File& file, const LoadOptions& options, const std::atomic<bool>* cancel,
                           const std::function<void (float)>& progress)
{
    if (! file.existsAsFile())
        return fail (LoadError::NotFound, describe (LoadError::NotFound));

    juce::AudioFormatManager formats;   // only the formats we advertise and test
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
    formats.registerFormat (new juce::FlacAudioFormat(), false);

    if (! file.hasReadAccess())
        return fail (LoadError::Unreadable, describe (LoadError::Unreadable));
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return fail (LoadError::Unsupported, describe (LoadError::Unsupported));

    const int channels = static_cast<int> (reader->numChannels);
    const Frame frames = static_cast<Frame> (std::max<juce::int64> (0, reader->lengthInSamples));
    const double rate = reader->sampleRate;
    if (channels < 1 || frames == 0 || rate <= 0.0)
        return fail (LoadError::Corrupt, describe (LoadError::Corrupt));
    if (channels > static_cast<int> (kMaxChannels))
        return fail (LoadError::TooManyChannels, juce::String (channels) + " channels: " + describe (LoadError::TooManyChannels));
    if (rate > options.maxRate + 0.5)
        return fail (LoadError::RateTooHigh, describe (LoadError::RateTooHigh));
    if (static_cast<double> (frames) / rate > options.maxSeconds + 1.0e-6)
        return fail (LoadError::TooLong, describe (LoadError::TooLong));

    // Uncompressed WAV/AIFF headers state the length; a shorter file is truncated (JUCE would silently zero-fill it).
    if (! reader->usesFloatingPointData || reader->bitsPerSample == 32)
        if (file.hasFileExtension ("wav;aif;aiff"))
        {
            const std::uint64_t needed = static_cast<std::uint64_t> (frames) * static_cast<std::uint64_t> (channels)
                                         * static_cast<std::uint64_t> ((reader->bitsPerSample + 7) / 8);
            if (static_cast<std::uint64_t> (std::max<juce::int64> (0, file.getSize())) < needed)
                return fail (LoadError::Corrupt, describe (LoadError::Corrupt));
        }

    const std::uint64_t decodedBytes = static_cast<std::uint64_t> (frames) * static_cast<std::uint64_t> (channels) * sizeof (float);
    if (decodedBytes > options.maxDecodedBytes)
        return fail (LoadError::TooLarge, describe (LoadError::TooLarge));

    const bool useDisk = decodedBytes > options.ramLimitBytes;
    constexpr std::size_t kBlock = DiskCacheWriter::kBlockFrames;

    std::shared_ptr<AudioClip> ram;
    DiskCacheWriter writer;
    juce::File cacheDir, cachePath;
    auto cleanupPartial = [&]
    {
        if (cacheDir != juce::File())
            cacheDir.deleteRecursively();
    };

    try
    {
        if (useDisk)
        {
            const juce::File root = options.cacheRoot != juce::File() ? options.cacheRoot
                                                                       : juce::File::getSpecialLocation (juce::File::tempDirectory);
            if (root.getBytesFreeOnVolume() > 0 && static_cast<std::uint64_t> (root.getBytesFreeOnVolume()) < decodedBytes + 128ull * 1024 * 1024)
                return fail (LoadError::DiskFull, describe (LoadError::DiskFull));
            cacheDir = root.getChildFile (juce::String (kCachePrefix) + juce::String (currentProcessId()) + "-"
                                          + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            if (! cacheDir.createDirectory().wasOk())
                return fail (LoadError::DiskFull, describe (LoadError::DiskFull));
            cachePath = cacheDir.getChildFile ("pcm.f32");
            if (! writer.open (cachePath.getFullPathName().toStdString(), channels))
            {
                cleanupPartial();
                return fail (LoadError::DiskFull, describe (LoadError::DiskFull));
            }
        }
        else
        {
            ram = AudioClip::create (rate, channels, frames);
        }

        WaveformOverview::Builder overview (frames, rate);
        std::vector<float> l (kBlock), r (kBlock);
        float* planes[2] = { l.data(), r.data() };
        float* readPlanes[2] = { l.data(), channels > 1 ? r.data() : nullptr };

        for (Frame start = 0; start < frames; start += kBlock)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            {
                writer.finish();
                cleanupPartial();
                return fail (LoadError::Cancelled, describe (LoadError::Cancelled));
            }
            const std::size_t n = static_cast<std::size_t> (std::min<Frame> (kBlock, frames - start));
            if (! reader->read (readPlanes, channels, static_cast<juce::int64> (start), static_cast<int> (n)))
            {
                writer.finish();
                cleanupPartial();
                return fail (LoadError::Corrupt, describe (LoadError::Corrupt));
            }
            for (int c = 0; c < channels; ++c)
                for (std::size_t i = 0; i < n; ++i)
                    if (! std::isfinite (planes[c][i]))
                        planes[c][i] = 0.0f;   // non-finite decoder output becomes silence (V1 section 6)

            overview.add (planes, channels, start, n);
            if (ram)
                for (int c = 0; c < channels; ++c)
                    std::memcpy (ram->channel (c) + start, planes[c], n * sizeof (float));
            else if (! writer.appendBlock (planes, n))
            {
                cleanupPartial();
                return fail (LoadError::DiskFull, describe (LoadError::DiskFull));
            }
            if (progress)
                progress (static_cast<float> (static_cast<double> (start + n) / static_cast<double> (frames)));
        }

        auto asset = std::make_shared<FileAsset>();
        asset->file = file;
        asset->name = file.getFileName();
        asset->formatName = reader->getFormatName();
        asset->sampleRate = rate;
        asset->channels = channels;
        asset->frames = frames;
        asset->overview = overview.finish();

        if (ram)
        {
            ram->seal (frames);
            asset->ram = ram;
            asset->source = ram;
        }
        else
        {
            if (! writer.finish())
            {
                cleanupPartial();
                return fail (LoadError::DiskFull, describe (LoadError::DiskFull));
            }
            auto disk = DiskClipSource::open (cachePath.getFullPathName().toStdString(), rate, channels, frames);
            if (disk == nullptr)
            {
                cleanupPartial();
                return fail (LoadError::Unreadable, describe (LoadError::Unreadable));
            }
            disk->deleteFileWhenDestroyed();
            asset->cacheDir = cacheDir;
            asset->disk = disk;
            asset->source = disk;
        }

        LoadOutcome ok;
        ok.asset = std::move (asset);
        return ok;
    }
    catch (const std::bad_alloc&)
    {
        cleanupPartial();
        return fail (LoadError::TooLarge, "Not enough memory to open this file.");
    }
}

// ------------------------------------------------------------------ FileService
struct FileService::Job : public juce::Thread
{
    Job (juce::File f, LoadOptions o, std::function<void (float)> p, std::function<void (LoadOutcome)> d, std::atomic<bool>& loadingFlag)
        : juce::Thread ("ReverseBack file load"), file (std::move (f)), options (std::move (o)), progress (std::move (p)),
          done (std::move (d)), loading (loadingFlag)
    {
    }

    ~Job() override
    {
        cancelled.store (true);
        stopThread (10000);
    }

    void run() override
    {
        float last = -1.0f;
        auto token = alive;
        LoadOutcome outcome = loadAudioFile (file, options, &cancelled, [&] (float p)
        {
            if (p - last < 0.01f && p < 1.0f)
                return;
            last = p;
            juce::MessageManager::callAsync ([token, cb = progress, p]
            {
                if (token->load() && cb)
                    cb (p);
            });
        });
        if (cancelled.load())
            return;   // superseded or cancelled: stay silent
        juce::MessageManager::callAsync ([token, flag = &loading, cb = done, o = std::move (outcome)]() mutable
        {
            if (! token->load())
                return;
            flag->store (false);
            if (cb)
                cb (std::move (o));
        });
    }

    juce::File file;
    LoadOptions options;
    std::function<void (float)> progress;
    std::function<void (LoadOutcome)> done;
    std::atomic<bool>& loading;
    std::atomic<bool> cancelled { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

FileService::FileService (LoadOptions options) : options_ (std::move (options)) {}

FileService::~FileService() { cancel(); }

void FileService::cancel()
{
    if (job_ != nullptr)
    {
        job_->alive->store (false);
        job_->cancelled.store (true);
        job_.reset();   // joins
    }
    loading_.store (false);
}

void FileService::load (const juce::File& file, std::function<void (float)> progress, std::function<void (LoadOutcome)> done)
{
    cancel();
    loading_.store (true);
    job_ = std::make_unique<Job> (file, options_, std::move (progress), std::move (done), loading_);
    job_->startThread();
}
}  // namespace rb
