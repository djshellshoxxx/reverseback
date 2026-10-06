#include "ExportService.h"

#include "ClipPlayer.h"
#include "SignalTools.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace reverseback
{
ExportService::Result ExportService::writeWav(const AudioClip& clip,
                                               Selection selection,
                                               const juce::File& destination,
                                               const ExportSettings& settings,
                                               const std::atomic_bool* cancelled) const
{
    Result result;

    if (selection.begin >= selection.end || selection.end > clip.frameCount())
    {
        result.error = Error::InvalidSelection;
        result.message = "Invalid export selection.";
        return result;
    }

    if (destination.existsAsFile() && !settings.allowOverwrite)
    {
        result.error = Error::DestinationExists;
        result.message = "Destination exists and overwrite was not confirmed.";
        return result;
    }

    const auto temp = destination.getSiblingFile(destination.getFileName() + ".reverseback.tmp");
    temp.deleteFile();

    auto stream = temp.createOutputStream();
    if (stream == nullptr)
    {
        result.error = Error::CreateFailed;
        result.message = "Unable to create temporary export file.";
        return result;
    }

    double peak = 0.0;
    for (std::size_t channel = 0; channel < clip.channels(); ++channel)
    {
        for (Frame frame = selection.begin; frame < selection.end; ++frame)
            peak = std::max(peak, std::abs(static_cast<double>(clip.sample(channel, frame))));
    }

    double normalization = 1.0;
    constexpr double target = 0.8912509381337456; // -1 dBFS
    if (settings.normalizeToMinusOneDb && peak > 0.0)
        normalization = target / peak;

    if (settings.bitDepth == 24 && !settings.normalizeToMinusOneDb && peak > 1.0)
    {
        result.error = Error::WouldClipPcm;
        result.message = "24-bit PCM export would clip; use float or enable normalization.";
        temp.deleteFile();
        return result;
    }

    juce::WavAudioFormat wav;
    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(clip.sampleRate())
                     .withNumChannels(static_cast<int>(clip.channels()))
                     .withBitsPerSample(settings.bitDepth);

    if (settings.bitDepth == 32)
        options = options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    else
        options = options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral);

    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        result.error = Error::CreateFailed;
        result.message = "Unable to create WAV writer.";
        temp.deleteFile();
        return result;
    }

    ClipPlayer player;
    player.prepare(clip, selection, settings.direction, settings.speed,
                   LoopPattern::Once, settings.fadeFrames);

    constexpr std::size_t blockFrames = 4096;
    AudioBuffer rendered(clip.channels(), std::vector<float>(blockFrames, 0.0f));
    juce::AudioBuffer<float> juceBlock(static_cast<int>(clip.channels()),
                                       static_cast<int>(blockFrames));

    while (player.playing())
    {
        if (cancelled != nullptr && cancelled->load())
        {
            writer.reset();
            temp.deleteFile();
            result.error = Error::Cancelled;
            result.message = "Export cancelled.";
            return result;
        }

        for (auto& channel : rendered)
            std::fill(channel.begin(), channel.end(), 0.0f);

        const auto written = player.process(rendered, blockFrames);
        if (written == 0)
            break;

        juceBlock.setSize(static_cast<int>(clip.channels()), static_cast<int>(written), false, false, true);
        for (std::size_t channel = 0; channel < clip.channels(); ++channel)
        {
            auto* targetSamples = juceBlock.getWritePointer(static_cast<int>(channel));
            for (std::size_t frame = 0; frame < written; ++frame)
                targetSamples[frame] = static_cast<float>(rendered[channel][frame] * normalization);
        }

        if (!writer->writeFromAudioSampleBuffer(juceBlock, 0, static_cast<int>(written)))
        {
            writer.reset();
            temp.deleteFile();
            result.error = Error::WriteFailed;
            result.message = "WAV export failed.";
            return result;
        }

        result.framesWritten += written;
    }

    writer.reset();

    if (destination.existsAsFile() && settings.allowOverwrite)
        destination.deleteFile();

    if (!temp.moveFileTo(destination))
    {
        temp.deleteFile();
        result.error = Error::RenameFailed;
        result.message = "Unable to move temporary export into place.";
        return result;
    }

    return result;
}
}
