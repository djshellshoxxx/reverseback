#include "MainComponent.h"

#include "SignalTools.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace reverseback
{
namespace
{
std::filesystem::path settingsPath()
{
    const auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("ReverseBack");
    dir.createDirectory();
    return std::filesystem::path(
        dir.getChildFile("settings.cfg").getFullPathName().toStdString());
}

void setupSlider(juce::Slider& slider,
                 double min,
                 double max,
                 double step,
                 double value,
                 const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 24);
    slider.setRange(min, max, step);
    slider.setValue(value, juce::dontSendNotification);
    slider.setTextValueSuffix(suffix);
}

juce::String modeName(AudioDeviceAdapter::Mode mode)
{
    switch (mode)
    {
        case AudioDeviceAdapter::Mode::Record: return "Record & Reverse";
        case AudioDeviceAdapter::Mode::Live: return "Live Reverse";
        case AudioDeviceAdapter::Mode::File: return "Reverse File";
    }
    return {};
}
}

MainComponent::MainComponent()
    : settingsStore_(settingsPath())
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    settings_ = settingsStore_.load();

    configureControls();
    applyTheme();

    for (auto* component : std::initializer_list<juce::Component*>{
             &title_, &recordMode_, &liveMode_, &fileMode_, &settingsButton_,
             &sourceLabel_, &inputMeter_, &waveform_, &fileInfo_,
             &selectionStart_, &selectionEnd_, &status_, &detail_,
             &captureLength_, &waitLength_, &repeatSession_,
             &liveChunk_, &liveDelay_, &liveLatency_, &headphonesHint_,
             &openFile_, &primary_, &hold_, &stop_, &freeze_, &replay_, &save_,
             &direction_, &loop_, &speed_, &volume_,
             &advancedToggle_, &advanced_, &preset_, &savePreset_, &surprise_})
        addAndMakeVisible(component);

    for (auto* component : std::initializer_list<juce::Component*>{
             &countdown_, &autoStart_, &triggerThreshold_, &repeatGap_,
             &inputGain_, &monitor_, &edgeFade_, &exactSamples_,
             &trim_, &undoTrim_, &exportDepth_, &exportRate_, &normalize_})
        advanced_.addAndMakeVisible(component);

    deviceError_ = adapter_.initialise();
    adapter_.setInputGainDb(settings_.inputGainDb);
    adapter_.setOutputVolumeDb(settings_.outputVolumeDb);
    adapter_.setInputMonitor(false);

    mode_ = static_cast<AudioDeviceAdapter::Mode>(settings_.mode);
    adapter_.setMode(mode_);
    updateModeVisibility();
    refreshPresetList();

    setSize(960, 700);
    startTimerHz(20);
}

MainComponent::~MainComponent()
{
    stopTimer();
    loadCancelled_.store(true);
    exportCancelled_.store(true);
    if (loadThread_.joinable())
        loadThread_.request_stop();
    if (exportThread_.joinable())
        exportThread_.request_stop();

    persistSettings();
    adapter_.shutdown();
}

void MainComponent::configureControls()
{
    title_.setText("ReverseBack", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));

    recordMode_.onClick = [this] { changeMode(AudioDeviceAdapter::Mode::Record); };
    liveMode_.onClick = [this] { changeMode(AudioDeviceAdapter::Mode::Live); };
    fileMode_.onClick = [this] { changeMode(AudioDeviceAdapter::Mode::File); };
    settingsButton_.onClick = [this] { showAudioSettings(); };

    sourceLabel_.setText("Audio device: starting...", juce::dontSendNotification);
    sourceLabel_.setJustificationType(juce::Justification::centredLeft);

    fileInfo_.setJustificationType(juce::Justification::centredLeft);
    fileInfo_.setVisible(false);

    selectionStart_.setTextToShowWhenEmpty("Start seconds", juce::Colours::grey);
    selectionEnd_.setTextToShowWhenEmpty("End seconds", juce::Colours::grey);
    selectionStart_.setInputRestrictions(12, "0123456789.");
    selectionEnd_.setInputRestrictions(12, "0123456789.");
    selectionStart_.onReturnKey = [this] { applySelectionEditors(); };
    selectionEnd_.onReturnKey = [this] { applySelectionEditors(); };

    waveform_.onSelectionChanged = [this](Selection s)
    {
        setCurrentSelection(s);
        updateSelectionEditors();
    };

    status_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    detail_.setFont(juce::Font(juce::FontOptions(14.0f)));
    detail_.setJustificationType(juce::Justification::centredLeft);

    setupSlider(captureLength_, 0.25, 60.0, 0.05, settings_.captureSeconds, " s");
    captureLength_.setName("Record length");
    setupSlider(waitLength_, 0.0, 30.0, 0.05, settings_.waitSeconds, " s");
    waitLength_.setName("Wait before playback");
    repeatSession_.setToggleState(settings_.repeatSession, juce::dontSendNotification);

    setupSlider(liveChunk_, 0.10, 5.0, 0.01, settings_.liveChunkSeconds, " s");
    liveChunk_.setName("Reverse chunk");
    setupSlider(liveDelay_, 0.0, 30.0, 0.05, settings_.liveDelaySeconds, " s");
    liveDelay_.setName("Extra delay");
    liveLatency_.setJustificationType(juce::Justification::centredLeft);
    headphonesHint_.setText("Headphones recommended for Live Reverse", juce::dontSendNotification);

    openFile_.onClick = [this] { openAudioFile(); };
    primary_.onClick = [this] { startPrimary(); };
    stop_.onClick = [this] { stopAll(); };
    freeze_.onClick = [this] { freezeOrResume(); };
    replay_.onClick = [this] { replayCurrent(); };
    save_.onClick = [this] { saveCurrentAsset(); };

    hold_.onStateChange = [this]
    {
        if (hold_.getState() == juce::Button::buttonDown)
            startHold();
        else if (holdActive_)
            finishHold();
    };

    direction_.addItem("Backwards", 1);
    direction_.addItem("Forward", 2);
    direction_.setSelectedId(settings_.direction == 0 ? 2 : 1, juce::dontSendNotification);
    direction_.setTooltip("Choose forward or backwards playback for retained clips and file selections.");
    direction_.onChange = [this]
    {
        adapter_.setPreviewDirection(selectedDirection());
    };

    loop_.addItem("Once", 1);
    loop_.addItem("Loop", 2);
    loop_.addItem("Ping-pong", 3);
    loop_.setSelectedId(settings_.loopPattern + 1, juce::dontSendNotification);
    loop_.onChange = [this]
    {
        adapter_.setPreviewLoop(selectedLoop());
        if (loop_.getSelectedId() != 1)
            repeatSession_.setToggleState(false, juce::sendNotification);
    };

    setupSlider(speed_, 0.5, 2.0, 0.01, settings_.speed, "x");
    speed_.setName("Tape speed");
    speed_.onValueChange = [this]
    {
        if (mode_ != AudioDeviceAdapter::Mode::Live)
            adapter_.setPreviewSpeed(speed_.getValue());
    };

    setupSlider(volume_, -60.0, 0.0, 0.5, settings_.outputVolumeDb, " dB");
    volume_.setName("Output volume");
    volume_.onValueChange = [this]
    {
        adapter_.setOutputVolumeDb(volume_.getValue());
    };

    repeatSession_.onClick = [this]
    {
        if (repeatSession_.getToggleState())
            loop_.setSelectedId(1, juce::sendNotification);
    };

    advancedToggle_.onClick = [this]
    {
        advancedVisible_ = !advancedVisible_;
        advanced_.setVisible(advancedVisible_);
        advancedToggle_.setButtonText(advancedVisible_ ? "Advanced ▲" : "Advanced ▼");
        resized();
    };
    advanced_.setVisible(false);

    countdown_.addItem("Countdown Off", 1);
    countdown_.addItem("Countdown 1 s", 2);
    countdown_.addItem("Countdown 3 s", 3);
    countdown_.addItem("Countdown 5 s", 4);
    if (settings_.countdownSeconds >= 4.0)
        countdown_.setSelectedId(4);
    else if (settings_.countdownSeconds >= 2.0)
        countdown_.setSelectedId(3);
    else if (settings_.countdownSeconds > 0.0)
        countdown_.setSelectedId(2);
    else
        countdown_.setSelectedId(1);

    autoStart_.setToggleState(settings_.autoStart, juce::dontSendNotification);
    autoStart_.onClick = [this]
    {
        triggerThreshold_.setEnabled(
            mode_ == AudioDeviceAdapter::Mode::Record &&
            autoStart_.getToggleState());
    };

    setupSlider(triggerThreshold_, -65.0, -15.0, 1.0,
                settings_.triggerThresholdDb, " dBFS");
    triggerThreshold_.setName("Voice trigger threshold");

    setupSlider(repeatGap_, 0.0, 2.0, 0.05,
                settings_.repeatGapSeconds, " s");
    repeatGap_.setName("Repeat session ready gap");

    setupSlider(inputGain_, -24.0, 24.0, 0.5, settings_.inputGainDb, " dB");
    inputGain_.setName("Input gain");
    inputGain_.onValueChange = [this]
    {
        adapter_.setInputGainDb(inputGain_.getValue());
    };

    monitor_.setToggleState(false, juce::dontSendNotification);
    monitor_.onClick = [this]
    {
        adapter_.setInputMonitor(monitor_.getToggleState());
    };

    setupSlider(edgeFade_, 0.0, 10.0, 0.1, settings_.edgeFadeMs, " ms");
    edgeFade_.setName("Edge fade");
    exactSamples_.setToggleState(settings_.exactSamples, juce::dontSendNotification);
    exactSamples_.onClick = [this]
    {
        edgeFade_.setEnabled(!exactSamples_.getToggleState());
    };
    edgeFade_.setEnabled(!exactSamples_.getToggleState());

    trim_.onClick = [this] { trimCurrentTake(); };
    undoTrim_.onClick = [this] { undoTrim(); };

    exportDepth_.addItem("32-bit float WAV", 1);
    exportDepth_.addItem("24-bit PCM WAV", 2);
    exportDepth_.setSelectedId(settings_.exportBitDepth == 24 ? 2 : 1);
    exportRate_.addItem("Source sample rate", 1);
    exportRate_.addItem("44.1 kHz", 2);
    exportRate_.addItem("48 kHz", 3);
    exportRate_.setSelectedId(settings_.exportSampleRate == 44100 ? 2 :
                              settings_.exportSampleRate == 48000 ? 3 : 1);
    normalize_.setToggleState(settings_.normalizeExport, juce::dontSendNotification);

    preset_.setTextWhenNothingSelected("Session preset");
    preset_.onChange = [this]
    {
        const auto id = preset_.getSelectedId();
        if (id >= 1 && id <= 4)
        {
            applyBuiltInPreset(id);
            return;
        }

        if (id >= 1000)
        {
            const auto presets = settingsStore_.loadPresets();
            const auto name = preset_.getText().toStdString();
            if (const auto it = presets.find(name); it != presets.end())
            {
                const auto& p = it->second;
                captureLength_.setValue(p.captureSeconds);
                waitLength_.setValue(p.waitSeconds);
                liveChunk_.setValue(p.liveChunkSeconds);
                liveDelay_.setValue(p.liveDelaySeconds);
                speed_.setValue(p.speed);
                edgeFade_.setValue(p.edgeFadeMs);
                inputGain_.setValue(p.inputGainDb);
                volume_.setValue(p.outputVolumeDb);
                repeatSession_.setToggleState(p.repeatSession, juce::dontSendNotification);
                autoStart_.setToggleState(p.autoStart, juce::dontSendNotification);
                triggerThreshold_.setValue(p.triggerThresholdDb);
                repeatGap_.setValue(p.repeatGapSeconds);
                exactSamples_.setToggleState(p.exactSamples, juce::dontSendNotification);
                exportDepth_.setSelectedId(p.exportBitDepth == 24 ? 2 : 1);
                exportRate_.setSelectedId(p.exportSampleRate == 44100 ? 2 :
                                          p.exportSampleRate == 48000 ? 3 : 1);
                normalize_.setToggleState(p.normalizeExport, juce::dontSendNotification);
                direction_.setSelectedId(p.direction == 0 ? 2 : 1);
                loop_.setSelectedId(p.loopPattern + 1);
                changeMode(static_cast<AudioDeviceAdapter::Mode>(p.mode));
                monitor_.setToggleState(false, juce::dontSendNotification);
                adapter_.setInputMonitor(false);
            }
        }
    };

    savePreset_.onClick = [this] { saveUserPreset(); };
    surprise_.onClick = [this] { applySurprise(); };

    liveDelay_.onValueChange = [this]
    {
        liveLatency_.setText(
            "First sound after " +
            juce::String(liveChunk_.getValue() + liveDelay_.getValue(), 2) +
            " s + device buffering",
            juce::dontSendNotification);
    };
    liveChunk_.onValueChange = liveDelay_.onValueChange;
    liveDelay_.onValueChange();

    undoTrim_.setEnabled(false);
}

void MainComponent::applyTheme()
{
    const auto text = juce::Colour(0xffecf2fa);
    const auto muted = juce::Colour(0xffa8b7cd);
    const auto panel = juce::Colour(0xff253042);
    const auto accent = juce::Colour(0xff6454dc);

    for (auto* label : std::initializer_list<juce::Label*>{
             &title_, &sourceLabel_, &fileInfo_, &status_, &detail_,
             &liveLatency_, &headphonesHint_})
        label->setColour(juce::Label::textColourId, text);

    detail_.setColour(juce::Label::textColourId, muted);
    fileInfo_.setColour(juce::Label::textColourId, muted);
    liveLatency_.setColour(juce::Label::textColourId, muted);
    headphonesHint_.setColour(juce::Label::textColourId, juce::Colour(0xffffbd69));

    for (auto* button : std::initializer_list<juce::TextButton*>{
             &recordMode_, &liveMode_, &fileMode_, &settingsButton_, &openFile_,
             &primary_, &hold_, &stop_, &freeze_, &replay_, &save_,
             &advancedToggle_, &trim_, &undoTrim_, &savePreset_, &surprise_})
    {
        button->setColour(juce::TextButton::buttonColourId, panel);
        button->setColour(juce::TextButton::textColourOffId, text);
    }

    primary_.setColour(juce::TextButton::buttonColourId, accent);
    stop_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8d3f4c));
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101621));

    if (advancedVisible_)
    {
        g.setColour(juce::Colour(0xff182231));
        g.fillRoundedRectangle(advanced_.getBounds().toFloat(), 10.0f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(24);
    const int gap = 10;

    auto top = area.removeFromTop(42);
    title_.setBounds(top.removeFromLeft(250));
    settingsButton_.setBounds(top.removeFromRight(140));
    top.removeFromRight(gap);

    auto modes = area.removeFromTop(48);
    const int third = (modes.getWidth() - gap * 2) / 3;
    recordMode_.setBounds(modes.removeFromLeft(third));
    modes.removeFromLeft(gap);
    liveMode_.setBounds(modes.removeFromLeft(third));
    modes.removeFromLeft(gap);
    fileMode_.setBounds(modes);

    area.removeFromTop(gap);
    auto source = area.removeFromTop(34);
    sourceLabel_.setBounds(source.removeFromLeft(source.getWidth() - 260));
    inputMeter_.setBounds(source.reduced(4));

    area.removeFromTop(gap);
    const int advancedHeight = advancedVisible_ ? 150 : 0;
    const int waveformHeight = std::max(110, std::min(220, area.getHeight() - 250 - advancedHeight));
    waveform_.setBounds(area.removeFromTop(waveformHeight));

    auto fileRow = area.removeFromTop(fileInfo_.isVisible() ? 34 : 0);
    if (fileInfo_.isVisible())
    {
        fileInfo_.setBounds(fileRow.removeFromLeft(std::max(200, fileRow.getWidth() - 300)));
        selectionStart_.setBounds(fileRow.removeFromLeft(140).reduced(3));
        selectionEnd_.setBounds(fileRow.removeFromLeft(140).reduced(3));
    }

    area.removeFromTop(gap);
    auto modeControls = area.removeFromTop(48);
    if (mode_ == AudioDeviceAdapter::Mode::Record)
    {
        captureLength_.setBounds(modeControls.removeFromLeft(modeControls.getWidth() / 3));
        waitLength_.setBounds(modeControls.removeFromLeft(modeControls.getWidth() / 3));
        repeatSession_.setBounds(modeControls.reduced(8));
    }
    else if (mode_ == AudioDeviceAdapter::Mode::Live)
    {
        liveChunk_.setBounds(modeControls.removeFromLeft(modeControls.getWidth() / 3));
        liveDelay_.setBounds(modeControls.removeFromLeft(modeControls.getWidth() / 3));
        liveLatency_.setBounds(modeControls.reduced(4));
    }
    else
    {
        openFile_.setBounds(modeControls.removeFromLeft(150).reduced(3));
        fileInfo_.setBounds(fileInfo_.getBounds());
    }

    auto transport = area.removeFromTop(56);
    const int buttonWidth = std::max(95, (transport.getWidth() - gap * 5) / 6);
    primary_.setBounds(transport.removeFromLeft(buttonWidth).reduced(2));
    hold_.setBounds(transport.removeFromLeft(buttonWidth).reduced(2));
    freeze_.setBounds(transport.removeFromLeft(buttonWidth).reduced(2));
    stop_.setBounds(transport.removeFromLeft(buttonWidth).reduced(2));
    replay_.setBounds(transport.removeFromLeft(buttonWidth).reduced(2));
    save_.setBounds(transport.reduced(2));

    auto common = area.removeFromTop(48);
    direction_.setBounds(common.removeFromLeft(150).reduced(3));
    loop_.setBounds(common.removeFromLeft(140).reduced(3));
    speed_.setBounds(common.removeFromLeft(std::max(180, common.getWidth() / 3)).reduced(3));
    volume_.setBounds(common.removeFromLeft(std::max(180, common.getWidth() / 2)).reduced(3));
    advancedToggle_.setBounds(common.reduced(3));

    auto stateRow = area.removeFromTop(48);
    status_.setBounds(stateRow.removeFromLeft(190));
    detail_.setBounds(stateRow);

    auto presetRow = area.removeFromTop(42);
    preset_.setBounds(presetRow.removeFromLeft(260).reduced(3));
    savePreset_.setBounds(presetRow.removeFromLeft(130).reduced(3));
    surprise_.setBounds(presetRow.removeFromLeft(160).reduced(3));
    headphonesHint_.setBounds(presetRow.reduced(4));

    if (advancedVisible_)
    {
        auto adv = area.removeFromTop(std::min(150, area.getHeight()));
        advanced_.setBounds(adv);
        auto inner = advanced_.getLocalBounds().reduced(8);
        const int rowH = 34;

        auto r1 = inner.removeFromTop(rowH);
        countdown_.setBounds(r1.removeFromLeft(170).reduced(2));
        autoStart_.setBounds(r1.removeFromLeft(180).reduced(2));
        triggerThreshold_.setBounds(r1.removeFromLeft(260).reduced(2));
        repeatGap_.setBounds(r1.reduced(2));

        auto r2 = inner.removeFromTop(rowH);
        inputGain_.setBounds(r2.removeFromLeft(260).reduced(2));
        monitor_.setBounds(r2.removeFromLeft(150).reduced(2));
        edgeFade_.setBounds(r2.removeFromLeft(250).reduced(2));
        exactSamples_.setBounds(r2.reduced(2));

        auto r3 = inner.removeFromTop(rowH);
        trim_.setBounds(r3.removeFromLeft(130).reduced(2));
        undoTrim_.setBounds(r3.removeFromLeft(120).reduced(2));
        exportDepth_.setBounds(r3.removeFromLeft(165).reduced(2));
        exportRate_.setBounds(r3.removeFromLeft(160).reduced(2));
        normalize_.setBounds(r3.reduced(2));
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (!settings_.shortcutsEnabled || textEntryHasFocus())
        return false;

    if (key == juce::KeyPress::escapeKey)
    {
        stopAll();
        return true;
    }

    if (key.getModifiers().isCtrlDown())
    {
        if (key.getTextCharacter() == 'o' || key.getTextCharacter() == 'O')
        {
            openAudioFile();
            return true;
        }
        if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
        {
            saveCurrentAsset();
            return true;
        }
    }

    if (key == juce::KeyPress::spaceKey)
    {
        const auto s = adapter_.snapshot();
        const bool busy = s.state != 0;
        if (busy)
            stopAll();
        else
            startPrimary();
        return true;
    }

    const auto ch = key.getTextCharacter();
    if (ch == 'r' || ch == 'R')
    {
        replayCurrent();
        return true;
    }

    if (ch == 'f' || ch == 'F')
    {
        direction_.setSelectedId(direction_.getSelectedId() == 1 ? 2 : 1,
                                 juce::sendNotification);
        return true;
    }

    if ((ch == 'h' || ch == 'H') && mode_ == AudioDeviceAdapter::Mode::Record)
    {
        if (!holdActive_)
            startHold();
        return true;
    }

    return false;
}

bool MainComponent::keyStateChanged(bool)
{
    if (holdActive_ &&
        !juce::KeyPress::isKeyCurrentlyDown('H') &&
        !juce::KeyPress::isKeyCurrentlyDown('h'))
    {
        finishHold();
        return true;
    }
    return false;
}

void MainComponent::focusLost(juce::Component::FocusChangeType)
{
    if (holdActive_)
        finishHold();
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.size() != 1)
        return false;

    const auto lower = files[0].toLowerCase();
    return lower.endsWith(".wav") || lower.endsWith(".aif") ||
           lower.endsWith(".aiff") || lower.endsWith(".flac");
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag(files))
        loadAudioFileAsync(juce::File(files[0]));
}

void MainComponent::timerCallback()
{
    const auto s = adapter_.snapshot();
    inputMeterValue_ = std::clamp(static_cast<double>(s.inputPeak), 0.0, 1.0);

    if (s.hasTake && (s.state != previousState_ || !previousHasTake_))
    {
        if (auto take = adapter_.copyRetainedTake())
        {
            recordTake_ = std::move(take);
            recordSelection_ = {0, recordTake_->frameCount()};
            recordTrimmed_ = false;
            undoTrim_.setEnabled(false);
            if (mode_ == AudioDeviceAdapter::Mode::Record)
                waveform_.setClip(recordTake_);
        }
    }

    if (s.hasFrozenChunk && (!previousHasFrozen_ ||
                            s.state == static_cast<int>(LiveReverseTransport::State::Frozen)))
    {
        if (!previousHasFrozen_ || s.state != previousState_)
        {
            if (auto frozen = adapter_.copyFrozenChunk())
            {
                frozenChunk_ = std::move(frozen);
                if (mode_ == AudioDeviceAdapter::Mode::Live)
                    waveform_.setClip(frozenChunk_);
            }
        }
    }

    previousHasTake_ = s.hasTake;
    previousHasFrozen_ = s.hasFrozenChunk;
    previousState_ = s.state;

    updateStatus();
    replay_.setEnabled(currentAsset() != nullptr);
    save_.setEnabled(currentAsset() != nullptr);

    const bool busy = s.state != 0;
    captureLength_.setEnabled(!busy);
    waitLength_.setEnabled(!busy);
    liveChunk_.setEnabled(!busy);
    liveDelay_.setEnabled(!busy);

    repaint();
}

void MainComponent::changeMode(AudioDeviceAdapter::Mode mode)
{
    stopAll();
    mode_ = mode;
    adapter_.setMode(mode_);
    updateModeVisibility();
    updateWaveformAsset();
}

void MainComponent::updateModeVisibility()
{
    const bool record = mode_ == AudioDeviceAdapter::Mode::Record;
    const bool live = mode_ == AudioDeviceAdapter::Mode::Live;
    const bool file = mode_ == AudioDeviceAdapter::Mode::File;

    captureLength_.setVisible(record);
    waitLength_.setVisible(record);
    repeatSession_.setVisible(record);
    hold_.setVisible(record);

    liveChunk_.setVisible(live);
    liveDelay_.setVisible(live);
    liveLatency_.setVisible(live);
    freeze_.setVisible(live);
    headphonesHint_.setVisible(live);

    openFile_.setVisible(file);
    fileInfo_.setVisible(file);
    selectionStart_.setVisible(file && loadedFile_ != nullptr);
    selectionEnd_.setVisible(file && loadedFile_ != nullptr);

    speed_.setEnabled(!live);
    direction_.setEnabled(!live);
    loop_.setEnabled(!live);

    autoStart_.setEnabled(record);
    countdown_.setEnabled(record);
    triggerThreshold_.setEnabled(record && autoStart_.getToggleState());
    repeatGap_.setEnabled(record);
    trim_.setEnabled(record && recordTake_ != nullptr);
    undoTrim_.setEnabled(record && recordTrimmed_);

    primary_.setButtonText(record ? "Record & Reverse" :
                           live ? "Start Live Reverse" :
                                  "Play Selection");

    recordMode_.setToggleState(record, juce::dontSendNotification);
    liveMode_.setToggleState(live, juce::dontSendNotification);
    fileMode_.setToggleState(file, juce::dontSendNotification);

    resized();
}

void MainComponent::updateStatus()
{
    const auto s = adapter_.snapshot();
    juce::String state = "Ready";
    juce::String detail;

    if (deviceError_.isNotEmpty())
    {
        sourceLabel_.setText("Audio device: " + deviceError_ + " — File mode remains available",
                             juce::dontSendNotification);
    }
    else
    {
        auto deviceName = juce::String("System default");
        if (auto* device = adapter_.deviceManager().getCurrentAudioDevice())
            deviceName = device->getName();

        sourceLabel_.setText(
            deviceName + "  •  " + juce::String(s.sampleRate, 0) + " Hz  •  " +
            juce::String(s.bufferSize) + " samples",
            juce::dontSendNotification);
    }

    if (s.state == 100)
    {
        state = selectedDirection() == Direction::Reverse
            ? "Playing backwards"
            : "Playing forward";
    }
    else if (mode_ == AudioDeviceAdapter::Mode::Record)
    {
        switch (static_cast<RecordTransport::State>(s.state))
        {
            case RecordTransport::State::Ready: state = "Ready"; break;
            case RecordTransport::State::Countdown: state = "Countdown"; break;
            case RecordTransport::State::Armed: state = "Armed — waiting for sound"; break;
            case RecordTransport::State::Recording: state = "Recording"; break;
            case RecordTransport::State::Waiting: state = "Waiting"; break;
            case RecordTransport::State::Playing: state = "Playing backwards"; break;
            case RecordTransport::State::ReadyGap: state = "Ready gap"; break;
        }
        detail = "Record " + juce::String(captureLength_.getValue(), 2) +
                 " s, wait " + juce::String(waitLength_.getValue(), 2) + " s.";
    }
    else if (mode_ == AudioDeviceAdapter::Mode::Live)
    {
        switch (static_cast<LiveReverseTransport::State>(s.state))
        {
            case LiveReverseTransport::State::Ready: state = "Ready"; break;
            case LiveReverseTransport::State::Filling: state = "Filling buffer"; break;
            case LiveReverseTransport::State::Running: state = "Live Reverse running"; break;
            case LiveReverseTransport::State::Frozen: state = "Frozen chunk looping"; break;
        }
        detail = "Startup latency is chunk + extra delay; source age varies within each reversed chunk.";
        freeze_.setButtonText(
            s.state == static_cast<int>(LiveReverseTransport::State::Frozen)
                ? "Resume" : "Freeze");
    }
    else
    {
        state = loadedFile_ != nullptr ? "File ready" : "Drop or open WAV, AIFF or FLAC";
        if (loadedFile_ != nullptr)
            detail = "Drag the waveform handles or type selection times.";
    }

    status_.setText("● " + state, juce::dontSendNotification);
    detail_.setText(detail, juce::dontSendNotification);
}

void MainComponent::updateWaveformAsset()
{
    waveform_.setClip(currentAsset());
    if (currentAsset() != nullptr)
        waveform_.setSelection(currentSelection());
    updateSelectionEditors();
}

void MainComponent::startPrimary()
{
    if (mode_ == AudioDeviceAdapter::Mode::Record)
    {
        adapter_.startRecord(makeRecordSettings());
    }
    else if (mode_ == AudioDeviceAdapter::Mode::Live)
    {
        adapter_.startLive(liveChunk_.getValue(), liveDelay_.getValue());
    }
    else if (loadedFile_ != nullptr)
    {
        adapter_.playFile(fileSelection_, selectedDirection(), speed_.getValue(),
                          selectedLoop(), exactSamples_.getToggleState() ? 0.0 : edgeFade_.getValue());
    }
}

void MainComponent::startHold()
{
    if (mode_ != AudioDeviceAdapter::Mode::Record || holdActive_)
        return;

    holdActive_ = true;
    adapter_.startRecord(makeRecordSettings(), true);
}

void MainComponent::finishHold()
{
    if (!holdActive_)
        return;

    holdActive_ = false;
    const auto result = adapter_.finishRecordEarly();
    if (result == RecordTransport::FinishResult::CancelledTooShort)
        detail_.setText("Hold longer to record.", juce::dontSendNotification);
}

void MainComponent::stopAll()
{
    holdActive_ = false;
    adapter_.stop();
}

void MainComponent::replayCurrent()
{
    auto asset = currentAsset();
    if (asset == nullptr)
        return;

    if (mode_ == AudioDeviceAdapter::Mode::Live)
    {
        adapter_.playClip(asset, {0, asset->frameCount()}, mode_,
                          Direction::Reverse, 1.0, LoopPattern::Loop,
                          exactSamples_.getToggleState() ? 0.0 : 2.0);
        return;
    }

    adapter_.playClip(asset, currentSelection(), mode_, selectedDirection(),
                      speed_.getValue(), selectedLoop(),
                      exactSamples_.getToggleState() ? 0.0 : edgeFade_.getValue());
}

void MainComponent::freezeOrResume()
{
    const auto s = adapter_.snapshot();
    if (s.state == static_cast<int>(LiveReverseTransport::State::Frozen))
        adapter_.resumeLive();
    else
        adapter_.freezeLive();
}

void MainComponent::openAudioFile()
{
    const auto initial = settings_.lastFolder.empty()
        ? juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        : juce::File(settings_.lastFolder);

    chooser_ = std::make_unique<juce::FileChooser>(
        "Open audio file", initial, "*.wav;*.aif;*.aiff;*.flac");

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safe->loadAudioFileAsync(file);
        });
}

void MainComponent::loadAudioFileAsync(const juce::File& file)
{
    loadCancelled_.store(true);
    if (loadThread_.joinable())
    {
        loadThread_.request_stop();
        loadThread_.join();
    }

    loadCancelled_.store(false);
    status_.setText("● Loading file...", juce::dontSendNotification);
    detail_.setText(file.getFullPathName(), juce::dontSendNotification);

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    loadThread_ = std::jthread([this, safe, file](std::stop_token token)
    {
        auto result = fileService_.load(file, &loadCancelled_);
        if (token.stop_requested())
            return;

        juce::MessageManager::callAsync([safe, result = std::move(result)]() mutable
        {
            if (safe == nullptr)
                return;

            if (!result.ok())
            {
                safe->status_.setText("● File load failed", juce::dontSendNotification);
                safe->detail_.setText(result.message, juce::dontSendNotification);
                return;
            }

            safe->loadedFile_ = result.clip;
            safe->fileSelection_ = {0, result.clip->frameCount()};
            safe->settings_.lastFolder = result.file.getParentDirectory().getFullPathName().toStdString();
            safe->adapter_.setFileClip(result.clip);
            safe->changeMode(AudioDeviceAdapter::Mode::File);
            safe->waveform_.setClip(result.clip);
            safe->waveform_.setSelection(safe->fileSelection_);
            safe->fileInfo_.setText(
                result.file.getFileName() + "  •  " +
                juce::String(result.clip->frameCount() / result.clip->sampleRate(), 2) + " s  •  " +
                juce::String(static_cast<int>(result.clip->channels())) + " ch  •  " +
                juce::String(result.clip->sampleRate(), 0) + " Hz",
                juce::dontSendNotification);
            safe->updateSelectionEditors();
            safe->updateModeVisibility();
        });
    });
}

void MainComponent::saveCurrentAsset()
{
    auto asset = currentAsset();
    if (asset == nullptr)
        return;

    const auto initialDir = settings_.lastFolder.empty()
        ? juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        : juce::File(settings_.lastFolder);

    chooser_ = std::make_unique<juce::FileChooser>(
        "Save ReverseBack WAV",
        initialDir.getChildFile("reverseback.wav"),
        "*.wav");

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;

            auto file = chooser.getResult();
            if (file == juce::File())
                return;
            if (!file.hasFileExtension("wav"))
                file = file.withFileExtension("wav");

            const bool overwrite = file.existsAsFile();
            safe->exportAsync(file, overwrite);
        });
}

void MainComponent::exportAsync(const juce::File& destination, bool allowOverwrite)
{
    auto asset = currentAsset();
    if (asset == nullptr)
        return;

    exportCancelled_.store(true);
    if (exportThread_.joinable())
    {
        exportThread_.request_stop();
        exportThread_.join();
    }

    exportCancelled_.store(false);
    ExportSettings exportSettings;
    exportSettings.direction = mode_ == AudioDeviceAdapter::Mode::Live
        ? Direction::Reverse : selectedDirection();
    exportSettings.speed = mode_ == AudioDeviceAdapter::Mode::Live
        ? 1.0 : speed_.getValue();
    exportSettings.fadeFrames = exactSamples_.getToggleState()
        ? 0
        : static_cast<Frame>(std::llround(
              asset->sampleRate() * edgeFade_.getValue() / 1000.0));
    exportSettings.bitDepth = exportDepth_.getSelectedId() == 2 ? 24 : 32;
    exportSettings.targetSampleRate = exportRate_.getSelectedId() == 2 ? 44100.0 :
                                      exportRate_.getSelectedId() == 3 ? 48000.0 : 0.0;
    exportSettings.normalizeToMinusOneDb = normalize_.getToggleState();
    exportSettings.allowOverwrite = allowOverwrite;
    const auto selection = currentSelection();

    status_.setText("● Exporting...", juce::dontSendNotification);

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    exportThread_ = std::jthread(
        [this, safe, asset = std::move(asset), selection, destination, exportSettings]
        (std::stop_token token)
        {
            auto result = exportService_.writeWav(
                *asset, selection, destination, exportSettings, &exportCancelled_);
            if (token.stop_requested())
                return;

            juce::MessageManager::callAsync(
                [safe, destination, result]()
                {
                    if (safe == nullptr)
                        return;

                    if (result.ok())
                    {
                        safe->status_.setText("● Export complete", juce::dontSendNotification);
                        safe->detail_.setText(
                            destination.getFullPathName() + "  •  " +
                            juce::String(static_cast<juce::int64>(result.framesWritten)) +
                            " frames",
                            juce::dontSendNotification);
                    }
                    else
                    {
                        safe->status_.setText("● Export failed", juce::dontSendNotification);
                        safe->detail_.setText(result.message, juce::dontSendNotification);
                    }
                });
        });
}

void MainComponent::applyBuiltInPreset(int presetId)
{
    stopAll();

    switch (presetId)
    {
        case 1: // Say Something
            captureLength_.setValue(5.0);
            waitLength_.setValue(2.0);
            speed_.setValue(1.0);
            loop_.setSelectedId(1);
            changeMode(AudioDeviceAdapter::Mode::Record);
            break;
        case 2: // Tiny Syllables
            liveChunk_.setValue(0.25);
            liveDelay_.setValue(0.25);
            changeMode(AudioDeviceAdapter::Mode::Live);
            break;
        case 3: // Backwards Conversation
            liveChunk_.setValue(1.0);
            liveDelay_.setValue(0.5);
            changeMode(AudioDeviceAdapter::Mode::Live);
            break;
        case 4: // Long Phrase
            captureLength_.setValue(10.0);
            waitLength_.setValue(2.0);
            changeMode(AudioDeviceAdapter::Mode::Record);
            break;
        default:
            break;
    }
}

void MainComponent::refreshPresetList()
{
    const auto selectedText = preset_.getText();
    preset_.clear(juce::dontSendNotification);
    preset_.addItem("Say Something", 1);
    preset_.addItem("Tiny Syllables", 2);
    preset_.addItem("Backwards Conversation", 3);
    preset_.addItem("Long Phrase", 4);
    preset_.addSeparator();

    int id = 1000;
    for (const auto& [name, settings] : settingsStore_.loadPresets())
    {
        (void)settings;
        preset_.addItem(juce::String(name), id++);
    }

    if (selectedText.isNotEmpty())
        preset_.setText(selectedText, juce::dontSendNotification);
}

void MainComponent::saveUserPreset()
{
    auto* window = new juce::AlertWindow(
        "Save ReverseBack preset",
        "Name this session setup.",
        juce::AlertWindow::NoIcon);
    window->addTextEditor("name", {}, "Preset name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, safeWindow](int result)
            {
                if (result != 1 || safeThis == nullptr || safeWindow == nullptr)
                    return;

                const auto name = safeWindow->getTextEditorContents("name").trim();
                if (name.isEmpty())
                    return;

                safeThis->persistSettings();
                auto presetSettings = safeThis->settings_;
                presetSettings.inputMonitor = false;
                safeThis->settingsStore_.savePreset(name.toStdString(), presetSettings);
                safeThis->refreshPresetList();
                safeThis->preset_.setText(name, juce::dontSendNotification);
            }),
        true);
}

void MainComponent::applySurprise()
{
    if (mode_ == AudioDeviceAdapter::Mode::Live || adapter_.snapshot().state != 0)
        return;

    static constexpr double speeds[]{0.5, 0.75, 1.0, 1.5, 2.0};
    auto& random = juce::Random::getSystemRandom();
    speed_.setValue(speeds[random.nextInt(5)]);
    direction_.setSelectedId(random.nextBool() ? 1 : 2);
    loop_.setSelectedId(1 + random.nextInt(3));
}

void MainComponent::trimCurrentTake()
{
    if (recordTake_ == nullptr)
        return;

    recordPreTrimSelection_ = recordSelection_;
    const auto trimmed = findNonSilentSelection(*recordTake_, -50.0, 0.020, 0.050);
    if (trimmed.begin == 0 && trimmed.end == recordTake_->frameCount())
    {
        detail_.setText("No speech or sound detected, or no removable edge silence found.",
                        juce::dontSendNotification);
        return;
    }

    recordSelection_ = trimmed;
    recordTrimmed_ = true;
    undoTrim_.setEnabled(true);
    waveform_.setSelection(recordSelection_);
}

void MainComponent::undoTrim()
{
    if (!recordTrimmed_)
        return;

    recordSelection_ = recordPreTrimSelection_;
    recordTrimmed_ = false;
    undoTrim_.setEnabled(false);
    waveform_.setSelection(recordSelection_);
}

void MainComponent::updateSelectionEditors()
{
    if (mode_ != AudioDeviceAdapter::Mode::File || loadedFile_ == nullptr)
        return;

    selectionStart_.setText(
        juce::String(static_cast<double>(fileSelection_.begin) / loadedFile_->sampleRate(), 3),
        false);
    selectionEnd_.setText(
        juce::String(static_cast<double>(fileSelection_.end) / loadedFile_->sampleRate(), 3),
        false);
}

void MainComponent::applySelectionEditors()
{
    if (loadedFile_ == nullptr)
        return;

    const auto startSeconds = selectionStart_.getText().getDoubleValue();
    const auto endSeconds = selectionEnd_.getText().getDoubleValue();
    Selection selection{
        static_cast<Frame>(std::llround(std::max(0.0, startSeconds) * loadedFile_->sampleRate())),
        static_cast<Frame>(std::llround(std::max(0.0, endSeconds) * loadedFile_->sampleRate()))
    };
    waveform_.setSelection(selection);
    fileSelection_ = waveform_.selection();
    updateSelectionEditors();
}

RecordTransport::RecordSettings MainComponent::makeRecordSettings() const
{
    const auto snapshot = adapter_.snapshot();

    RecordTransport::RecordSettings s;
    s.sampleRate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 48000.0;
    s.channels = static_cast<std::size_t>(std::max(1, snapshot.inputChannels));
    s.captureSeconds = captureLength_.getValue();
    s.waitSeconds = waitLength_.getValue();

    switch (countdown_.getSelectedId())
    {
        case 2: s.countdownSeconds = 1.0; break;
        case 3: s.countdownSeconds = 3.0; break;
        case 4: s.countdownSeconds = 5.0; break;
        default: s.countdownSeconds = 0.0; break;
    }

    s.autoStart = autoStart_.getToggleState();
    s.triggerThresholdDb = triggerThreshold_.getValue();
    s.triggerSustainSeconds = 0.050;
    s.preRollSeconds = 0.200;
    s.repeatSession = repeatSession_.getToggleState();
    s.readyGapSeconds = repeatGap_.getValue();
    return s;
}

Direction MainComponent::selectedDirection() const
{
    return direction_.getSelectedId() == 2 ? Direction::Forward : Direction::Reverse;
}

LoopPattern MainComponent::selectedLoop() const
{
    switch (loop_.getSelectedId())
    {
        case 2: return LoopPattern::Loop;
        case 3: return LoopPattern::PingPong;
        default: return LoopPattern::Once;
    }
}

std::shared_ptr<const AudioClip> MainComponent::currentAsset() const
{
    switch (mode_)
    {
        case AudioDeviceAdapter::Mode::Record: return recordTake_;
        case AudioDeviceAdapter::Mode::Live: return frozenChunk_;
        case AudioDeviceAdapter::Mode::File: return loadedFile_;
    }
    return {};
}

Selection MainComponent::currentSelection() const
{
    const auto asset = currentAsset();
    if (asset == nullptr)
        return {};

    switch (mode_)
    {
        case AudioDeviceAdapter::Mode::Record:
            return recordSelection_.end > recordSelection_.begin
                ? recordSelection_ : Selection{0, asset->frameCount()};
        case AudioDeviceAdapter::Mode::Live:
            return {0, asset->frameCount()};
        case AudioDeviceAdapter::Mode::File:
            return fileSelection_.end > fileSelection_.begin
                ? fileSelection_ : Selection{0, asset->frameCount()};
    }
    return {};
}

void MainComponent::setCurrentSelection(Selection selection)
{
    if (mode_ == AudioDeviceAdapter::Mode::Record)
        recordSelection_ = selection;
    else if (mode_ == AudioDeviceAdapter::Mode::File)
        fileSelection_ = selection;
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions options;
    auto* panel = new AudioSettingsPanel(adapter_);
    panel->setSize(680, 520);
    options.content.setOwned(panel);
    options.dialogTitle = "ReverseBack Audio Settings";
    options.dialogBackgroundColour = juce::Colour(0xff101621);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

bool MainComponent::textEntryHasFocus() const
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return dynamic_cast<juce::TextEditor*>(focused) != nullptr;
}

void MainComponent::persistSettings()
{
    settings_.captureSeconds = captureLength_.getValue();
    settings_.waitSeconds = waitLength_.getValue();
    settings_.liveChunkSeconds = liveChunk_.getValue();
    settings_.liveDelaySeconds = liveDelay_.getValue();
    settings_.inputGainDb = inputGain_.getValue();
    settings_.outputVolumeDb = volume_.getValue();
    settings_.speed = speed_.getValue();
    settings_.edgeFadeMs = edgeFade_.getValue();
    settings_.mode = static_cast<int>(mode_);
    settings_.direction = selectedDirection() == Direction::Reverse ? 1 : 0;
    settings_.loopPattern = loop_.getSelectedId() - 1;

    switch (countdown_.getSelectedId())
    {
        case 2: settings_.countdownSeconds = 1.0; break;
        case 3: settings_.countdownSeconds = 3.0; break;
        case 4: settings_.countdownSeconds = 5.0; break;
        default: settings_.countdownSeconds = 0.0; break;
    }

    settings_.autoStart = autoStart_.getToggleState();
    settings_.triggerThresholdDb = triggerThreshold_.getValue();
    settings_.repeatSession = repeatSession_.getToggleState();
    settings_.repeatGapSeconds = repeatGap_.getValue();
    settings_.exactSamples = exactSamples_.getToggleState();
    settings_.exportBitDepth = exportDepth_.getSelectedId() == 2 ? 24 : 32;
    settings_.exportSampleRate = exportRate_.getSelectedId() == 2 ? 44100 :
                                 exportRate_.getSelectedId() == 3 ? 48000 : 0;
    settings_.normalizeExport = normalize_.getToggleState();
    settings_.inputMonitor = false;

    try
    {
        settingsStore_.save(settings_);
    }
    catch (...)
    {
    }
}
}
