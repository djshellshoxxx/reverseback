#include "AudioSettingsPanel.h"

namespace reverseback
{
AudioSettingsPanel::AudioSettingsPanel(AudioDeviceAdapter& adapter)
    : adapter_(adapter),
      selector_(adapter.deviceManager(),
                0, 2,
                1, 2,
                true, true,
                true, false)
{
    addAndMakeVisible(selector_);
    addAndMakeVisible(status_);
    addAndMakeVisible(testSound_);

    status_.setJustificationType(juce::Justification::centredLeft);
    testSound_.setTooltip("Play a short 440 Hz tone at the current ReverseBack output volume.");
    testSound_.onClick = [this] { adapter_.triggerTestTone(); };

    startTimerHz(4);
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto bottom = area.removeFromBottom(42);
    status_.setBounds(bottom.removeFromLeft(std::max(0, bottom.getWidth() - 140)));
    testSound_.setBounds(bottom.reduced(4));
    selector_.setBounds(area);
}

void AudioSettingsPanel::timerCallback()
{
    const auto s = adapter_.snapshot();
    status_.setText(
        juce::String(s.sampleRate, 0) + " Hz  |  " +
        juce::String(s.bufferSize) + " samples  |  " +
        juce::String(s.inputChannels) + " in / " +
        juce::String(s.outputChannels) + " out",
        juce::dontSendNotification);
}
}
