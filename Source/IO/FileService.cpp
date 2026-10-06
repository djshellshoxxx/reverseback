#include "FileService.h"

#include <new>

namespace reverseback
{
namespace
{
constexpr double kMaxDurationSeconds = 30.0 * 60.0;
constexpr double kMaxSampleRate = 192000.0;
constexpr std::uint64_t kMaxDecodedBytes = 3ULL * 1024ULL * 1024ULL * 1024ULL;
}

FileService::FileService()
{
    formats_.registerBasicFormats();
}

FileService::Result FileService::load(const juce::File& file,
                                      const std::atomic_bool* cancelled) const
{
    Result result;
    result.file = file;

    if (!file.existsAsFile())
    {
        result.error = Error::Missing;
        result.message = "File does not exist.";
        return result;
    }

    if (cancelled != nullptr && cancelled->load())
    {
        result.error = Error::Cancelled;
        result.message = "Load cancelled.";
        return result;
    }

    auto reader = formats_.createReaderFor(file);
    if (reader == nullptr)
    {
        result.error = Error::Unsupported;
        result.message = "Unsupported or unreadable audio file.";
        return result;
    }

    if (reader->numChannels == 0 || reader->numChannels > 2)
    {
        result.error = Error::TooManyChannels;
        result.message = "ReverseBack supports mono and stereo files only.";
        return result;
    }

    if (reader->sampleRate <= 0.0 || reader->sampleRate > kMaxSampleRate)
    {
        result.error = Error::SampleRateTooHigh;
        result.message = "File sample rate is unsupported.";
        return result;
    }

    const auto length = static_cast<std::uint64_t>(reader->lengthInSamples);
    const auto duration = static_cast<double>(length) / reader->sampleRate;
    if (duration > kMaxDurationSeconds)
    {
        result.error = Error::TooLong;
        result.message = "Files are limited to 30 minutes.";
        return result;
    }

    const auto decodedBytes =
        length * static_cast<std::uint64_t>(reader->numChannels) * sizeof(float);
    if (decodedBytes > kMaxDecodedBytes)
    {
        result.error = Error::DecodedSizeTooLarge;
        result.message = "Decoded audio exceeds the 3 GiB safety limit.";
        return result;
    }

    // In-memory path. A disk-backed cache is selected elsewhere for assets above
    // the 256 MiB live-memory budget; this loader deliberately refuses to
    // allocate beyond that threshold until the cache object is prepared.
    constexpr std::uint64_t kMemoryBudget = 256ULL * 1024ULL * 1024ULL;
    if (decodedBytes > kMemoryBudget)
    {
        result.error = Error::AllocationFailed;
        result.message = "This file requires the disk-backed cache path.";
        return result;
    }

    try
    {
        juce::AudioBuffer<float> buffer(
            static_cast<int>(reader->numChannels),
            static_cast<int>(length));

        constexpr int block = 1 << 20;
        std::int64_t offset = 0;
        while (offset < reader->lengthInSamples)
        {
            if (cancelled != nullptr && cancelled->load())
            {
                result.error = Error::Cancelled;
                result.message = "Load cancelled.";
                return result;
            }

            const auto remaining = reader->lengthInSamples - offset;
            const auto count = static_cast<int>(std::min<std::int64_t>(block, remaining));
            if (!reader->read(&buffer, static_cast<int>(offset), count, offset, true, true))
            {
                result.error = Error::Corrupt;
                result.message = "Audio decoding failed.";
                return result;
            }
            offset += count;
        }

        AudioBuffer audio(static_cast<std::size_t>(reader->numChannels));
        for (unsigned channel = 0; channel < reader->numChannels; ++channel)
        {
            auto& out = audio[channel];
            out.resize(static_cast<std::size_t>(length));
            const auto* source = buffer.getReadPointer(static_cast<int>(channel));
            std::copy(source, source + length, out.begin());
        }

        result.clip = std::make_shared<const AudioClip>(reader->sampleRate, std::move(audio));
        return result;
    }
    catch (const std::bad_alloc&)
    {
        result.error = Error::AllocationFailed;
        result.message = "Not enough memory to decode this file.";
        return result;
    }
    catch (...)
    {
        result.error = Error::Corrupt;
        result.message = "Audio decoding failed.";
        return result;
    }
}
}
