#pragma once

#include "AudioClip.h"
#include "ClipPlayer.h"
#include "LiveReverseTransport.h"
#include "RealtimeTools.h"
#include "RecordTransport.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>

namespace reverseback
{
class AudioDeviceAdapter final : public juce::AudioIODeviceCallback
{
public:
    enum class Mode
    {
        Record,
        Live,
        File
    };

    struct Snapshot
    {
        Mode mode{Mode::Record};
        int state{0};
        float inputPeak{0.0f};
        float outputPeak{0.0f};
        double sampleRate{0.0};
        int bufferSize{0};
        int inputChannels{0};
        int outputChannels{0};
        bool hasTake{false};
        bool hasFrozenChunk{false};
        bool hasFile{false};
    };

    AudioDeviceAdapter();
    ~AudioDeviceAdapter() override;

    [[nodiscard]] juce::String initialise();
    void shutdown();

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept { return deviceManager_; }
    [[nodiscard]] Snapshot snapshot() const noexcept;

    void setMode(Mode mode);
    void startRecord(const RecordTransport::RecordSettings& settings, bool held = false);
    RecordTransport::FinishResult finishRecordEarly();
    bool replayRecord();
    void startLive(double chunkSeconds, double delaySeconds);
    void freezeLive();
    void resumeLive();
    void stop();

    void setFileClip(std::shared_ptr<const AudioClip> clip);
    void playFile(Direction direction, double speed, LoopPattern loop, double fadeMs);
    [[nodiscard]] std::shared_ptr<const AudioClip> fileClip() const noexcept { return fileClip_; }

    [[nodiscard]] std::shared_ptr<const AudioClip> copyRetainedTake();
    [[nodiscard]] std::shared_ptr<const AudioClip> copyFrozenChunk();

    void setInputGainDb(double db) noexcept;
    void setOutputVolumeDb(double db) noexcept;
    void setInputMonitor(bool enabled) noexcept;
    void triggerTestTone(double frequencyHz = 440.0, double seconds = 1.0);

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    void suspendCallback(const std::function<void()>& fn);
    [[nodiscard]] double currentSampleRate() const noexcept;
    [[nodiscard]] std::size_t activeInputChannels() const noexcept;
    void publishState() noexcept;

    juce::AudioDeviceManager deviceManager_;
    bool callbackAttached_{false};

    Mode mode_{Mode::Record};
    RecordTransport record_;
    LiveReverseTransport live_;
    ClipPlayer filePlayer_;
    std::shared_ptr<const AudioClip> fileClip_;

    AudioBuffer inputScratch_;
    AudioBuffer wetScratch_;
    int preparedBlockSize_{0};
    int preparedInputChannels_{0};
    int preparedOutputChannels_{0};

    SmoothedGain inputGain_;
    SmoothedGain outputGain_;
    PeakProtector peakProtector_;

    std::atomic<bool> inputMonitor_{false};
    std::atomic<double> requestedInputGainDb_{0.0};
    std::atomic<double> requestedOutputGainDb_{-12.0};
    double appliedInputGainDb_{0.0};
    double appliedOutputGainDb_{-12.0};

    std::atomic<std::uint64_t> testToneFramesRemaining_{0};
    std::atomic<double> testToneFrequencyHz_{440.0};
    double testTonePhase_{0.0};

    std::atomic<int> publishedMode_{0};
    std::atomic<int> publishedState_{0};
    std::atomic<float> inputPeak_{0.0f};
    std::atomic<float> outputPeak_{0.0f};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> bufferSize_{0};
    std::atomic<int> inputChannels_{0};
    std::atomic<int> outputChannels_{0};
    std::atomic<bool> hasTake_{false};
    std::atomic<bool> hasFrozen_{false};
    std::atomic<bool> hasFile_{false};

    juce::String lastDeviceError_;
};
}
