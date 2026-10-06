#pragma once

#include "AudioDeviceAdapter.h"
#include "AudioSettingsPanel.h"
#include "ExportService.h"
#include "FileService.h"
#include "SettingsStore.h"
#include "WaveformView.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <memory>
#include <thread>

namespace reverseback
{
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            public juce::FileDragAndDropTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;

    void configureControls();
    void applyTheme();
    void changeMode(AudioDeviceAdapter::Mode mode);
    void updateModeVisibility();
    void updateStatus();
    void updateWaveformAsset();

    void startPrimary();
    void startHold();
    void finishHold();
    void stopAll();
    void replayCurrent();
    void freezeOrResume();
    void openAudioFile();
    void loadAudioFileAsync(const juce::File& file);
    void saveCurrentAsset();
    void exportAsync(const juce::File& destination, bool allowOverwrite);

    void applyBuiltInPreset(int presetId);
    void refreshPresetList();
    void saveUserPreset();
    void applySurprise();

    void trimCurrentTake();
    void undoTrim();
    void updateSelectionEditors();
    void applySelectionEditors();

    [[nodiscard]] RecordTransport::RecordSettings makeRecordSettings() const;
    [[nodiscard]] Direction selectedDirection() const;
    [[nodiscard]] LoopPattern selectedLoop() const;
    [[nodiscard]] std::shared_ptr<const AudioClip> currentAsset() const;
    [[nodiscard]] Selection currentSelection() const;
    void setCurrentSelection(Selection selection);

    void showAudioSettings();
    [[nodiscard]] bool textEntryHasFocus() const;
    void persistSettings();

    AudioDeviceAdapter adapter_;
    FileService fileService_;
    ExportService exportService_;
    SettingsStore settingsStore_;
    AppSettings settings_;

    AudioDeviceAdapter::Mode mode_{AudioDeviceAdapter::Mode::Record};
    std::shared_ptr<const AudioClip> recordTake_;
    std::shared_ptr<const AudioClip> frozenChunk_;
    std::shared_ptr<const AudioClip> loadedFile_;
    Selection recordSelection_{};
    Selection recordPreTrimSelection_{};
    bool recordTrimmed_{false};
    Selection fileSelection_{};

    std::atomic_bool loadCancelled_{false};
    std::atomic_bool exportCancelled_{false};
    std::jthread loadThread_;
    std::jthread exportThread_;
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::Label title_;
    juce::TextButton recordMode_{"Record & Reverse"};
    juce::TextButton liveMode_{"Live Reverse"};
    juce::TextButton fileMode_{"Reverse File"};
    juce::TextButton settingsButton_{"Audio Settings"};

    juce::Label sourceLabel_;
    double inputMeterValue_{0.0};
    juce::ProgressBar inputMeter_{inputMeterValue_};

    WaveformView waveform_;
    juce::Label fileInfo_;
    juce::TextEditor selectionStart_;
    juce::TextEditor selectionEnd_;

    juce::Label status_;
    juce::Label detail_;

    juce::Slider captureLength_;
    juce::Slider waitLength_;
    juce::ToggleButton repeatSession_{"Repeat session"};

    juce::Slider liveChunk_;
    juce::Slider liveDelay_;
    juce::Label liveLatency_;
    juce::Label headphonesHint_;

    juce::TextButton openFile_{"Open File"};
    juce::TextButton primary_{"Record & Reverse"};
    juce::TextButton hold_{"Hold to Record"};
    juce::TextButton stop_{"Stop"};
    juce::TextButton freeze_{"Freeze"};
    juce::TextButton replay_{"Replay"};
    juce::TextButton save_{"Save WAV"};

    juce::ComboBox direction_;
    juce::ComboBox loop_;
    juce::Slider speed_;
    juce::Slider volume_;

    juce::TextButton advancedToggle_{"Advanced"};
    juce::Component advanced_;
    bool advancedVisible_{false};

    juce::ComboBox countdown_;
    juce::ToggleButton autoStart_{"Auto Start on Voice"};
    juce::Slider triggerThreshold_;
    juce::Slider repeatGap_;
    juce::Slider inputGain_;
    juce::ToggleButton monitor_{"Input monitor"};
    juce::Slider edgeFade_;
    juce::ToggleButton exactSamples_{"Exact Samples"};
    juce::TextButton trim_{"Trim Silence"};
    juce::TextButton undoTrim_{"Undo Trim"};
    juce::ComboBox exportDepth_;
    juce::ComboBox exportRate_;
    juce::ToggleButton normalize_{"Peak normalize to -1 dBFS"};

    juce::ComboBox preset_;
    juce::TextButton savePreset_{"Save Preset"};
    juce::TextButton surprise_{"Surprise Settings"};

    bool holdActive_{false};
    bool previousHasTake_{false};
    bool previousHasFrozen_{false};
    int previousState_{-1};
    juce::String deviceError_;
};
}
