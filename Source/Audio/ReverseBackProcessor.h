// JUCE AudioProcessor shared by the Standalone, VST3 and CLAP builds. The audio thread only touches
// the engine and lock-free state; every allocation, file operation and UI interaction happens on the
// message thread (see PLUGIN_FORMATS.md and ENGINE_DESIGN.md section 9).
#pragma once

#include "Engine.h"
#include "ExportService.h"
#include "FileService.h"
#include "Parameters.h"
#include "Presets.h"
#include "SettingsStore.h"
#include "Stages.h"
#include "WaveformOverview.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <limits>
#include <mutex>

namespace rb
{
// Implemented by the standalone application so the shared editor can show and manage devices.
struct HostServices
{
    virtual ~HostServices() = default;
    virtual juce::AudioDeviceManager* getDeviceManager() = 0;
    virtual juce::String getDeviceSummary() = 0;   // e.g. "ALSA - default - 48000 Hz - 256 frames"
    virtual juce::String getDeviceError() = 0;     // empty when devices are healthy
    virtual bool hasInputDevice() = 0;
    virtual void retryDevices() = 0;
};

struct TakeAsset
{
    std::shared_ptr<const AudioClip> clip;
    std::shared_ptr<const WaveformOverview> overview;
    Selection trim;      // empty = whole take
    std::uint32_t id = 0;
    Selection effective() const noexcept { return trim.empty() ? Selection { 0, clip ? clip->frameCount() : 0 } : trim; }
};

enum class BannerKind { None, Device, File, Engine, Info };

struct Banner
{
    BannerKind kind = BannerKind::None;
    juce::String text;
    bool canRetry = false;
    bool canOpenSettings = false;
    std::uint32_t id = 0;
    double autoDismissSeconds = 0.0;   // 0 = stays until dismissed or resolved
    juce::File reveal;                 // when set, the banner offers "Show in folder"
};

// What the Save WAV button would export right now.
struct ExportSource
{
    std::shared_ptr<const ClipSource> source;
    Selection selection;
    double sampleRate = 48000.0;
    int channels = 1;
    juce::String suggestedName;
    bool valid() const noexcept { return source != nullptr && ! selection.empty(); }
};

class ReverseBackProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    ReverseBackProcessor();
    explicit ReverseBackProcessor (const juce::File& settingsFile);
    ~ReverseBackProcessor() override;

    // ------------------------------------------------------------ juce::AudioProcessor
    const juce::String getName() const override { return "ReverseBack"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    // A device stop/disconnect: silence immediately; prepareToPlay rebuilds everything (takes are kept).
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Output is generated independently of the input (wait, playback, live delay): never let a host stop calling us.
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // ------------------------------------------------------------ control API (message thread only)
    juce::AudioProcessorValueTreeState apvts;

    bool isStandalone() const noexcept { return standalone_; }
    void setHostServices (HostServices* h) noexcept { host_ = h; sendChangeMessage(); }
    HostServices* hostServices() const noexcept { return host_; }
    double currentSampleRate() const noexcept { return rate_.load(); }

    Snapshot snapshot() const noexcept { return engine_.snapshot(); }
    Mode mode() const noexcept { return static_cast<Mode> (juce::jlimit (0, 2, juce::roundToInt (params_.mode->load()))); }
    bool isBusy() const;

    // Transport actions. All of them are safe to call at any time; none ever starts recording implicitly.
    void actionStartStop();
    void actionStart();
    void actionStop();
    void actionHoldDown();
    void actionHoldUp();
    void actionFinishEarly();
    void actionReplay();
    void actionFreezeResume();

    // File mode
    void loadFile (const juce::File& file, bool keepSelection = false, Selection selection = {});
    void cancelLoad();
    bool isLoadingFile() const noexcept { return files_.isLoading(); }
    float loadProgress() const noexcept { return loadProgress_; }
    std::shared_ptr<FileAsset> fileAsset() const noexcept { return fileAsset_; }
    Selection fileSelection() const noexcept { return fileSel_; }
    bool setFileSelection (Selection sel);   // false if shorter than 50 ms or out of range
    void playFile (bool fromStart);

    // Take tools (Record mode)
    std::shared_ptr<const TakeAsset> currentTake() const noexcept { return take_; }
    bool trimSilence();     // false: "No speech or sound detected"
    void undoTrim();

    // Live mode
    std::shared_ptr<LiveStorage> liveStorage() const noexcept { return liveStorage_; }
    std::shared_ptr<AudioClip> copyFrozenChunk() const;

    // Presets
    void applyPreset (const PresetValues& values);   // stops, applies, never records
    void applySurprise();
    std::vector<UserPreset> userPresets() const { return settings_.userPresets; }
    void saveUserPreset (const juce::String& name);
    void deleteUserPreset (const juce::String& name);
    PresetValues currentPresetValues() const;

    // Export
    ExportSource exportSource() const;
    ExportRequest makeExportRequest (const ExportSource& src, const ExportSettings& settings, const juce::File& destination,
                                     bool overwrite) const;
    ExportService& exporter() noexcept { return exporter_; }

    // UI helpers
    const Banner& banner() const noexcept { return banner_; }
    void showSavedBanner (const juce::File& saved);
    void dismissBanner();
    void retryBanner();
    void showBanner (BannerKind kind, const juce::String& text, bool retry = false, bool settings = false, double autoDismiss = 0.0);
    std::uint32_t takeVersion() const noexcept { return takeVersion_; }
    std::uint32_t fileVersion() const noexcept { return fileVersion_; }
    std::uint32_t frozenVersion() const noexcept { return frozenVersion_; }
    StoredSettings& storedSettings() noexcept { return settings_; }
    void saveStoredSettings();
    juce::String lastFolder() const { return settings_.lastFolder; }
    void rememberFolder (const juce::File& f);

    // State (also used by the standalone to persist the last parameter values)
    void setStateFromXml (const juce::XmlElement& xml, bool restoreFile);
    std::unique_ptr<juce::XmlElement> getStateAsXml() const;

    // Used by tests and the standalone self-test: run the control-thread service pass immediately.
    void serviceNow() { service(); }
    Engine& engineForTest() noexcept { return engine_; }
    ParamRefs& paramRefs() noexcept { return params_; }

private:
    void timerCallback() override { service(); }
    void service();
    void handleEvent (Event&& e);
    bool post (Command&& c);
    void quiesce();
    void publishFileState();
    void refreshPresetsFromDisk();
    bool isStartPending() const;
    void markStartPosted();
    void startRecording (bool hold);
    void startLive();
    bool takeBudgetOk (Frame frames, int channels, bool repeat) const;
    int captureChannels() const;
    void setParam (const char* id, double plainValue);
    void enforceLoopRepeatExclusivity();
    void detectTriggers (const ParamRefs& p) noexcept;
    void finishLoad (LoadOutcome outcome, bool keepSelection, Selection selection);

    // ---- real-time side
    ParamRefs params_;
    Engine engine_;
    InputStage inputStage_;
    OutputStage outputStage_;
    CommandQueue<Command, 256> commands_;
    std::vector<float> outStore_;
    float* out_[2] = { nullptr, nullptr };
    std::size_t maxBlock_ = 0;
    std::atomic<double> rate_ { 0.0 };
    std::atomic<bool> prepared_ { false };
    std::atomic<bool> faultFlag_ { false };
    std::atomic<std::uint32_t> triggerBits_ { 0 };
    bool prevTrig_[4] = { false, false, false, false };
    bool standalone_ = false;
    std::atomic<int> inProcess_ { 0 };   // processBlock calls in flight; prepare/release wait for 0
    std::atomic<bool> startPending_ { false };
    std::atomic<std::uint32_t> startPendingBlocks_ { 0 };

    // What the engine should have and what a host saves: written on the message thread, read from any thread.
    struct PublishedFile
    {
        juce::File path;
        std::shared_ptr<const ClipSource> source;
        Selection sel;
    };
    mutable std::mutex fileStateMutex_;
    PublishedFile published_;
    juce::String dismissedDeviceError_;

    // ---- control side (message thread)
    HostServices* host_ = nullptr;
    SettingsStore store_;
    StoredSettings settings_;
    FileService files_;
    ExportService exporter_;
    std::shared_ptr<FileAsset> fileAsset_;
    Selection fileSel_ {};
    std::shared_ptr<const TakeAsset> take_;
    std::shared_ptr<LiveStorage> liveStorage_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
    Banner banner_;
    std::uint32_t bannerId_ = 0, takeVersion_ = 0, fileVersion_ = 0, frozenVersion_ = 0, lastFrozenGen_ = 0;
    double bannerShownAt_ = 0.0;
    float loadProgress_ = 0.0f;
    bool wasRepeat_ = false, wasLooping_ = false;
    int lastBannerRetryMode_ = 0;
    Frame lastPlayFrame_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseBackProcessor)
};

// Defined next to the processor; creates the editor for any wrapper.
juce::AudioProcessorEditor* createReverseBackEditor (ReverseBackProcessor&);
}  // namespace rb
