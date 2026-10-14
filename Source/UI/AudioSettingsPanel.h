#pragma once

#include "AudioDeviceAdapter.h"

#include <juce_audio_utils/juce_audio_utils.h>

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
