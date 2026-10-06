#include "AudioDeviceAdapter.h"

#include "SignalTools.h"

#include <algorithm>
#include <cmath>

namespace reverseback
{
AudioDeviceAdapter::AudioDeviceAdapter()
    : peakProtector_(-1.0f)
{
}

AudioDeviceAdapter::~AudioDeviceAdapter()
{
    shutdown();
}

juce::String AudioDeviceAdapter::initialise()
{
    auto error = deviceManager_.initialise(2, 2, nullptr, true);

    if (error.isNotEmpty())
        error = deviceManager_.initialise(1, 2, nullptr, true);

    if (error.isNotEmpty())
        error = deviceManager_.initialise(0, 2, nullptr, true);

    if (error.isNotEmpty())
    {
        lastDeviceError_ = error;
        return error;
    }

    deviceManager_.addAudioCallback(this);
    callbackAttached_ = true;
    return {};
}

void AudioDeviceAdapter::shutdown()
{
    if (callbackAttached_)
    {
        deviceManager_.removeAudioCallback(this);
        callbackAttached_ = false;
    }
    deviceManager_.closeAudioDevice();
}

AudioDeviceAdapter::Snapshot AudioDeviceAdapter::snapshot() const noexcept
{
    Snapshot s;
    s.mode = static_cast<Mode>(publishedMode_.load());
    s.state = publishedState_.load();
    s.inputPeak = inputPeak_.load();
    s.outputPeak = outputPeak_.load();
    s.sampleRate = sampleRate_.load();
    s.bufferSize = bufferSize_.load();
    s.inputChannels = inputChannels_.load();
    s.outputChannels = outputChannels_.load();
    s.hasTake = hasTake_.load();
    s.hasFrozenChunk = hasFrozen_.load();
    s.hasFile = hasFile_.load();
    return s;
}

void AudioDeviceAdapter::setMode(Mode mode)
{
    suspendCallback([&]
    {
        record_.stop();
        live_.stop();
        filePlayer_.stop();
        mode_ = mode;
        publishState();
    });
}

void AudioDeviceAdapter::startRecord(const RecordTransport::RecordSettings& settings, bool held)
{
    suspendCallback([&]
    {
        mode_ = Mode::Record;
        record_.prepare(settings);
        record_.startPrepared(held);
        live_.stop();
        filePlayer_.stop();
        publishState();
    });
}

RecordTransport::FinishResult AudioDeviceAdapter::finishRecordEarly()
{
    RecordTransport::FinishResult result = RecordTransport::FinishResult::NotRecording;
    suspendCallback([&]
    {
        result = record_.finishEarly();
        publishState();
    });
    return result;
}

bool AudioDeviceAdapter::replayRecord()
{
    bool result = false;
    suspendCallback([&]
    {
        mode_ = Mode::Record;
        result = record_.replay();
        publishState();
    });
    return result;
}

void AudioDeviceAdapter::startLive(double chunkSeconds, double delaySeconds)
{
    suspendCallback([&]
    {
        const auto rate = currentSampleRate();
        const auto channels = std::max<std::size_t>(1, activeInputChannels());
        const auto chunk = static_cast<std::uint64_t>(std::llround(rate * chunkSeconds));
        const auto delay = static_cast<std::uint64_t>(std::llround(rate * delaySeconds));

        mode_ = Mode::Live;
        record_.stop();
        filePlayer_.stop();
        live_.start(channels, std::max<std::uint64_t>(1, chunk), delay);
        publishState();
    });
}

void AudioDeviceAdapter::freezeLive()
{
    suspendCallback([&]
    {
        live_.requestFreeze();
        publishState();
    });
}

void AudioDeviceAdapter::resumeLive()
{
    suspendCallback([&]
    {
        live_.resume();
        publishState();
    });
}

void AudioDeviceAdapter::stop()
{
    suspendCallback([&]
    {
        record_.stop();
        live_.stop();
        filePlayer_.stop();
        publishState();
    });
}

void AudioDeviceAdapter::setFileClip(std::shared_ptr<const AudioClip> clip)
{
    suspendCallback([&]
    {
        filePlayer_.stop();
        fileClip_ = std::move(clip);
        hasFile_.store(fileClip_ != nullptr);
        publishState();
    });
}

void AudioDeviceAdapter::playFile(Direction direction, double speed, LoopPattern loop, double fadeMs)
{
    suspendCallback([&]
    {
        if (fileClip_ == nullptr || fileClip_->frameCount() == 0)
            return;

        mode_ = Mode::File;
        record_.stop();
        live_.stop();
        const auto fade = static_cast<Frame>(std::llround(
            fileClip_->sampleRate() * std::clamp(fadeMs, 0.0, 10.0) / 1000.0));
        filePlayer_.prepare(*fileClip_, {0, fileClip_->frameCount()}, direction, speed, loop, fade);
        publishState();
    });
}

std::shared_ptr<const AudioClip> AudioDeviceAdapter::copyRetainedTake()
{
    std::shared_ptr<const AudioClip> result;
    suspendCallback([&]
    {
        if (record_.hasRetainedTake())
            result = std::make_shared<const AudioClip>(
                currentSampleRate(), record_.retainedTake());
    });
    return result;
}

std::shared_ptr<const AudioClip> AudioDeviceAdapter::copyFrozenChunk()
{
    std::shared_ptr<const AudioClip> result;
    suspendCallback([&]
    {
        if (live_.hasFrozenChunk())
            result = std::make_shared<const AudioClip>(
                currentSampleRate(), live_.frozenChunk());
    });
    return result;
}

void AudioDeviceAdapter::setInputGainDb(double db) noexcept
{
    requestedInputGainDb_.store(std::clamp(db, -24.0, 24.0));
}

void AudioDeviceAdapter::setOutputVolumeDb(double db) noexcept
{
    requestedOutputGainDb_.store(std::clamp(db, -60.0, 0.0));
}

void AudioDeviceAdapter::setInputMonitor(bool enabled) noexcept
{
    inputMonitor_.store(enabled);
}

void AudioDeviceAdapter::triggerTestTone(double frequencyHz, double seconds)
{
    suspendCallback([&]
    {
        record_.stop();
        live_.stop();
        filePlayer_.stop();
        testToneFrequencyHz_.store(std::clamp(frequencyHz, 40.0, 4000.0));
        testTonePhase_ = 0.0;
        testToneFramesRemaining_.store(static_cast<std::uint64_t>(
            std::llround(currentSampleRate() * std::clamp(seconds, 0.05, 5.0))));
        publishState();
    });
}

void AudioDeviceAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto rate = device->getCurrentSampleRate();
    const auto block = std::max(1, device->getCurrentBufferSizeSamples());
    const auto ins = std::max(1, device->getActiveInputChannels().countNumberOfSetBits());
    const auto outs = std::max(1, device->getActiveOutputChannels().countNumberOfSetBits());

    preparedBlockSize_ = block;
    preparedInputChannels_ = std::min(2, ins);
    preparedOutputChannels_ = std::min(2, outs);

    inputScratch_.assign(
        static_cast<std::size_t>(preparedInputChannels_),
        std::vector<float>(static_cast<std::size_t>(block), 0.0f));
    wetScratch_.assign(
        static_cast<std::size_t>(std::max(preparedInputChannels_, preparedOutputChannels_)),
        std::vector<float>(static_cast<std::size_t>(block), 0.0f));

    appliedInputGainDb_ = requestedInputGainDb_.load();
    appliedOutputGainDb_ = requestedOutputGainDb_.load();
    inputGain_.prepare(rate, 0.020, appliedInputGainDb_);
    outputGain_.prepare(rate, 0.020, appliedOutputGainDb_);

    sampleRate_.store(rate);
    bufferSize_.store(block);
    inputChannels_.store(preparedInputChannels_);
    outputChannels_.store(preparedOutputChannels_);
    publishState();
}

void AudioDeviceAdapter::audioDeviceStopped()
{
    sampleRate_.store(0.0);
    bufferSize_.store(0);
    inputPeak_.store(0.0f);
    outputPeak_.store(0.0f);
}

void AudioDeviceAdapter::audioDeviceError(const juce::String& errorMessage)
{
    lastDeviceError_ = errorMessage;
}

void AudioDeviceAdapter::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }

    if (numSamples <= 0 || numSamples > preparedBlockSize_)
        return;

    const auto requestedIn = requestedInputGainDb_.load();
    if (requestedIn != appliedInputGainDb_)
    {
        appliedInputGainDb_ = requestedIn;
        inputGain_.setTargetDb(appliedInputGainDb_);
    }

    const auto requestedOut = requestedOutputGainDb_.load();
    if (requestedOut != appliedOutputGainDb_)
    {
        appliedOutputGainDb_ = requestedOut;
        outputGain_.setTargetDb(appliedOutputGainDb_);
    }

    float inPeak = 0.0f;

    for (int frame = 0; frame < numSamples; ++frame)
    {
        const auto gain = inputGain_.nextGain();
        for (int channel = 0; channel < preparedInputChannels_; ++channel)
        {
            auto& target = inputScratch_[static_cast<std::size_t>(channel)];
            const float* source = channel < numInputChannels ? inputChannelData[channel] : nullptr;
            const auto sample = source != nullptr ? sanitizeSample(source[frame]) : 0.0f;
            const auto gained = sample * gain;
            target[static_cast<std::size_t>(frame)] = gained;
            inPeak = std::max(inPeak, std::abs(gained));
        }
    }

    for (auto& channel : wetScratch_)
        std::fill_n(channel.begin(), numSamples, 0.0f);

    switch (mode_)
    {
        case Mode::Record:
            record_.processBlockInto(inputScratch_, static_cast<std::uint32_t>(numSamples), wetScratch_);
            break;

        case Mode::Live:
            live_.processBlockInto(inputScratch_, static_cast<std::uint32_t>(numSamples), wetScratch_);
            break;

        case Mode::File:
            if (filePlayer_.playing())
                filePlayer_.process(wetScratch_, static_cast<std::size_t>(numSamples));
            break;
    }

    const bool monitor =
        inputMonitor_.load() &&
        !(mode_ == Mode::Record && record_.state() == RecordTransport::State::Playing);

    float outPeak = 0.0f;
    for (int frame = 0; frame < numSamples; ++frame)
    {
        const auto outputGain = outputGain_.nextGain();

        float testTone = 0.0f;
        auto toneRemaining = testToneFramesRemaining_.load();
        if (toneRemaining > 0)
        {
            constexpr double twoPi = 6.28318530717958647692;
            testTone = static_cast<float>(0.125892541 * std::sin(testTonePhase_));
            testTonePhase_ += twoPi * testToneFrequencyHz_.load() / std::max(1.0, sampleRate_.load());
            if (testTonePhase_ >= twoPi)
                testTonePhase_ -= twoPi;
            testToneFramesRemaining_.store(toneRemaining - 1);
        }

        for (int outputChannel = 0; outputChannel < numOutputChannels; ++outputChannel)
        {
            const auto wetChannel = wetScratch_.size() == 1
                ? 0U
                : static_cast<std::size_t>(std::min(outputChannel, static_cast<int>(wetScratch_.size()) - 1));

            float sample =
                (wetScratch_[wetChannel][static_cast<std::size_t>(frame)] + testTone) * outputGain;

            if (monitor && !inputScratch_.empty())
            {
                const auto inputChannel = inputScratch_.size() == 1
                    ? 0U
                    : static_cast<std::size_t>(std::min(outputChannel, static_cast<int>(inputScratch_.size()) - 1));
                sample += inputScratch_[inputChannel][static_cast<std::size_t>(frame)];
            }

            sample = peakProtector_.process(sample);
            if (outputChannelData[outputChannel] != nullptr)
                outputChannelData[outputChannel][frame] = sample;
            outPeak = std::max(outPeak, std::abs(sample));
        }
    }

    inputPeak_.store(inPeak);
    outputPeak_.store(outPeak);
    publishState();
}

void AudioDeviceAdapter::suspendCallback(const std::function<void()>& fn)
{
    const bool wasAttached = callbackAttached_;
    if (wasAttached)
    {
        deviceManager_.removeAudioCallback(this);
        callbackAttached_ = false;
    }

    fn();

    if (wasAttached && deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        deviceManager_.addAudioCallback(this);
        callbackAttached_ = true;
    }
}

double AudioDeviceAdapter::currentSampleRate() const noexcept
{
    if (const auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return sampleRate_.load() > 0.0 ? sampleRate_.load() : 48000.0;
}

std::size_t AudioDeviceAdapter::activeInputChannels() const noexcept
{
    if (const auto* device = deviceManager_.getCurrentAudioDevice())
        return static_cast<std::size_t>(
            std::max(0, device->getActiveInputChannels().countNumberOfSetBits()));
    return static_cast<std::size_t>(std::max(0, inputChannels_.load()));
}

void AudioDeviceAdapter::publishState() noexcept
{
    publishedMode_.store(static_cast<int>(mode_));

    int state = 0;
    switch (mode_)
    {
        case Mode::Record: state = static_cast<int>(record_.state()); break;
        case Mode::Live: state = static_cast<int>(live_.state()); break;
        case Mode::File: state = filePlayer_.playing() ? 1 : 0; break;
    }

    publishedState_.store(state);
    hasTake_.store(record_.hasRetainedTake());
    hasFrozen_.store(live_.hasFrozenChunk());
    hasFile_.store(fileClip_ != nullptr);
}
}
