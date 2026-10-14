// Host-visible parameters (PLUGIN_FORMATS.md section 5) and their mapping to engine Settings.
#pragma once

#include "Engine.h"
#include "Stages.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace rb::ids
{
inline constexpr const char* mode = "mode";
inline constexpr const char* capture = "capture";
inline constexpr const char* wait = "wait";
inline constexpr const char* chunk = "chunk";
inline constexpr const char* delay = "delay";
inline constexpr const char* inGain = "inGain";
inline constexpr const char* outVol = "outVol";
inline constexpr const char* edgeFade = "edgeFade";
inline constexpr const char* liveFade = "liveFade";
inline constexpr const char* exact = "exact";
inline constexpr const char* speed = "speed";
inline constexpr const char* direction = "direction";
inline constexpr const char* loop = "loop";
inline constexpr const char* repeat = "repeat";
inline constexpr const char* tailGap = "tailGap";
inline constexpr const char* countdown = "countdown";
inline constexpr const char* autoStart = "autoStart";
inline constexpr const char* threshold = "threshold";
inline constexpr const char* inChan = "inChan";
inline constexpr const char* monitor = "monitor";
inline constexpr const char* trgStart = "trgStart";
inline constexpr const char* trgHold = "trgHold";
inline constexpr const char* trgReplay = "trgReplay";
inline constexpr const char* trgFreeze = "trgFreeze";
}  // namespace rb::ids

namespace rb
{
// Parameters saved with the plugin state (monitor and trigger parameters are deliberately excluded).
const std::vector<const char*>& persistentParameterIds();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (bool standalone);

// Lock-free handles used on the audio thread.
struct ParamRefs
{
    explicit ParamRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* mode;
    std::atomic<float>* capture;
    std::atomic<float>* wait;
    std::atomic<float>* chunk;
    std::atomic<float>* delay;
    std::atomic<float>* inGain;
    std::atomic<float>* outVol;
    std::atomic<float>* edgeFade;
    std::atomic<float>* liveFade;
    std::atomic<float>* exact;
    std::atomic<float>* speed;
    std::atomic<float>* direction;
    std::atomic<float>* loop;
    std::atomic<float>* repeat;
    std::atomic<float>* tailGap;
    std::atomic<float>* countdown;
    std::atomic<float>* autoStart;
    std::atomic<float>* threshold;
    std::atomic<float>* inChan;
    std::atomic<float>* monitor;
    std::atomic<float>* trgStart;
    std::atomic<float>* trgHold;
    std::atomic<float>* trgReplay;
    std::atomic<float>* trgFreeze;

    // Reads every parameter into an engine Settings value (no allocation).
    Settings readSettings() const noexcept;
    InputChannelMode readChannelMode() const noexcept;
};

constexpr double kCountdownChoices[] = { 0.0, 1.0, 3.0, 5.0 };
}  // namespace rb
