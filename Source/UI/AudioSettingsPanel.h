#pragma once

#include "AudioDeviceAdapter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace reverseback
{
class AudioSettingsPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit AudioSettingsPanel(AudioDeviceAdapter& adapter);

    void resized() override;

private:
    void timerCallback() override;

    AudioDeviceAdapter& adapter_;
    juce::AudioDeviceSelectorComponent selector_;
    juce::Label status_;
    juce::TextButton testSound_{"Test Output"};
};
}
