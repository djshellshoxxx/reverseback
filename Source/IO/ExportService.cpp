#include "ExportService.h"

#include "SignalTools.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace reverseback
{
namespace
{
constexpr int kSincRadius = 8;
constexpr double kPi = 3.141592653589793238462643383279502884;

double sinc(double x)
{
    if (std::abs(x) < 1.0e-12)
        return 1.0;
    const auto px = kPi * x;
    return std::sin(px) / px;
}

double lanczos(double x)
{
    const auto ax = std::abs(x);
    if (ax >= static_cast<double>(kSincRadius))
        return 0.0;
    return sinc(x) * sinc(x / static_cast<double>(kSincRadius));
}

float sampleSinc(const AudioClip& clip,
                 std::size_t channel,
                 Selection selection,
                 double sourceFrame)
{
    const auto rounded = std::llround(sourceFrame);
    if (std::abs(sourceFrame - static_cast<double>(rounded)) < 1.0e-12)
    {
        const auto index = static_cast<Frame>(std::clamp<long long>(
            rounded,
            static_cast<long long>(selection.begin),
            static_cast<long long>(selection.end - 1)));
        return clip.sample(channel, index);
    }

    const auto lower = static_cast<long long>(std::floor(sourceFrame));
    long double sum = 0.0;
    long double weights = 0.0;

    for (int tap = -kSincRadius + 1; tap <= kSincRadius; ++tap)
    {
        const auto index = std::clamp<long long>(
            lower + tap,
            static_cast<long long>(selection.begin),
            static_cast<long long>(selection.end - 1));
        const auto weight = lanczos(sourceFrame - static_cast<double>(index));
        sum += static_cast<long double>(
                   clip.sample(channel, static_cast<Frame>(index))) * weight;
        weights += weight;
    }

    if (std::abs(static_cast<double>(weights)) < 1.0e-15)
        return 0.0f;

    return static_cast<float>(sum / weights);
}

struct RenderPlan
{
    double targetRate{48000.0};
    std::uint64_t outputFrames{0};
    std::uint64_t fadeFrames{0};
    double sourceFramesPerOutputFrame{1.0};
};

RenderPlan makePlan(const AudioClip& clip,
                    Selection selection,
                    const ExportSettings& settings)
{
    const auto targetRate = settings.targetSampleRate > 0.0
        ? settings.targetSampleRate
        : clip.sampleRate();

    const auto sourceFrames = static_cast<long double>(selection.end - selection.begin);
    const auto outputFrames = static_cast<std::uint64_t>(std::llround(
        sourceFrames / settings.speed *
        static_cast<long double>(targetRate / clip.sampleRate())));

    const auto fadeFrames = static_cast<std::uint64_t>(std::llround(
        static_cast<long double>(settings.fadeFrames) *
        targetRate / clip.sampleRate()));

    return {
        targetRate,
        std::max<std::uint64_t>(1, outputFrames),
        fadeFrames,
        settings.speed * clip.sampleRate() / targetRate
    };
}

double edgeGain(std::uint64_t frame,
                std::uint64_t total,
                std::uint64_t requestedFade)
{
    if (requestedFade == 0 || total <= 1)
        return 1.0;

    const auto fade = std::min<std::uint64_t>(requestedFade, total / 2);
    if (fade == 0)
        return 1.0;

    if (frame < fade)
        return static_cast<double>(frame) / static_cast<double>(fade);

    const auto remaining = total - 1 - frame;
    if (remaining < fade)
        return static_cast<double>(remaining) / static_cast<double>(fade);

    return 1.0;
}

double sourcePosition(Selection selection,
                      Direction direction,
                      std::uint64_t outputFrame,
                      double sourceFramesPerOutputFrame)
{
    const auto progress =
        static_cast<double>(outputFrame) * sourceFramesPerOutputFrame;

    if (direction == Direction::Forward)
        return std::min(
            static_cast<double>(selection.end - 1),
            static_cast<double>(selection.begin) + progress);

    return std::max(
        static_cast<double>(selection.begin),
        static_cast<double>(selection.end - 1) - progress);
}
}

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

    if (!std::isfinite(settings.speed) ||
        settings.speed < 0.5 || settings.speed > 2.0 ||
        (settings.bitDepth != 24 && settings.bitDepth != 32) ||
        (settings.targetSampleRate > 0.0 &&
         settings.targetSampleRate != 44100.0 &&
         settings.targetSampleRate != 48000.0))
    {
        result.error = Error::InvalidSettings;
        result.message = "Invalid export settings.";
        return result;
    }

    if (destination.existsAsFile() && !settings.allowOverwrite)
    {
        result.error = Error::DestinationExists;
        result.message = "Destination exists and overwrite was not confirmed.";
        return result;
    }

    const auto plan = makePlan(clip, selection, settings);
    constexpr double normalizeTarget = 0.8912509381337456;
    double normalization = 1.0;

    auto renderSample = [&](std::size_t channel, std::uint64_t outputFrame)
    {
        const auto source = sourcePosition(
            selection, settings.direction, outputFrame,
            plan.sourceFramesPerOutputFrame);
        const auto fade = edgeGain(outputFrame, plan.outputFrames, plan.fadeFrames);
        return static_cast<double>(sampleSinc(clip, channel, selection, source)) * fade;
    };

    if (settings.normalizeToMinusOneDb)
    {
        double renderedPeak = 0.0;

        for (std::uint64_t frame = 0; frame < plan.outputFrames; ++frame)
        {
            if ((frame & 0x3fffU) == 0 &&
                cancelled != nullptr && cancelled->load())
            {
                result.error = Error::Cancelled;
                result.message = "Export cancelled.";
                return result;
            }

            for (std::size_t channel = 0; channel < clip.channels(); ++channel)
                renderedPeak = std::max(
                    renderedPeak,
                    std::abs(renderSample(channel, frame)));
        }

        if (renderedPeak > 0.0)
            normalization = normalizeTarget / renderedPeak;
    }

    const auto temp = destination.getSiblingFile(
        destination.getFileName() + ".reverseback.tmp");
    temp.deleteFile();

    auto fileStream = temp.createOutputStream();
    if (fileStream == nullptr)
    {
        result.error = Error::CreateFailed;
        result.message = "Unable to create temporary export file.";
        return result;
    }
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);

    juce::WavAudioFormat wav;
    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(plan.targetRate)
                     .withNumChannels(static_cast<int>(clip.channels()))
                     .withBitsPerSample(settings.bitDepth)
                     .withSampleFormat(
                         settings.bitDepth == 32
                             ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                             : juce::AudioFormatWriterOptions::SampleFormat::integral);

    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        result.error = Error::CreateFailed;
        result.message = "Unable to create WAV writer.";
        temp.deleteFile();
        return result;
    }

    constexpr std::size_t blockFrames = 4096;
    juce::AudioBuffer<float> block(
        static_cast<int>(clip.channels()),
        static_cast<int>(blockFrames));

    std::minstd_rand ditherGenerator(0x52BACCU);
    constexpr double pcm24Lsb = 1.0 / 8388607.0;

    for (std::uint64_t base = 0; base < plan.outputFrames; base += blockFrames)
    {
        if (cancelled != nullptr && cancelled->load())
        {
            writer.reset();
            temp.deleteFile();
            result.error = Error::Cancelled;
            result.message = "Export cancelled.";
            return result;
        }

        const auto count = static_cast<int>(std::min<std::uint64_t>(
            blockFrames, plan.outputFrames - base));
        block.setSize(
            static_cast<int>(clip.channels()), count,
            false, false, true);

        for (std::size_t channel = 0; channel < clip.channels(); ++channel)
        {
            auto* target = block.getWritePointer(static_cast<int>(channel));

            for (int i = 0; i < count; ++i)
            {
                auto sample = renderSample(
                    channel, base + static_cast<std::uint64_t>(i)) * normalization;

                if (settings.bitDepth == 24)
                {
                    if (!settings.normalizeToMinusOneDb && std::abs(sample) > 1.0)
                    {
                        writer.reset();
                        temp.deleteFile();
                        result.error = Error::WouldClipPcm;
                        result.message =
                            "24-bit PCM export would clip; use float or enable normalization.";
                        return result;
                    }

                    const auto u1 = std::generate_canonical<double, 24>(ditherGenerator);
                    const auto u2 = std::generate_canonical<double, 24>(ditherGenerator);
                    sample += (u1 - u2) * pcm24Lsb;
                    sample = std::clamp(sample, -1.0, 1.0);
                }

                target[i] = sanitizeSample(static_cast<float>(sample));
            }
        }

        if (!writer->writeFromAudioSampleBuffer(block, 0, count))
        {
            writer.reset();
            temp.deleteFile();
            result.error = Error::WriteFailed;
            result.message = "WAV export failed.";
            return result;
        }

        result.framesWritten += static_cast<std::uint64_t>(count);
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
