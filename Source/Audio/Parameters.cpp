#include "Parameters.h"

namespace rb
{
namespace
{
using juce::AudioParameterBool;
using juce::AudioParameterBoolAttributes;
using juce::AudioParameterChoice;
using juce::AudioParameterChoiceAttributes;
using juce::AudioParameterFloat;
using juce::AudioParameterFloatAttributes;
using juce::NormalisableRange;
using juce::ParameterID;

constexpr int kVersion = 1;

std::unique_ptr<AudioParameterFloat> floatParam (const char* id, const char* name, float lo, float hi, float step, float def,
                                                 const char* unit, bool automatable = true)
{
    return std::make_unique<AudioParameterFloat> (ParameterID { id, kVersion }, name, NormalisableRange<float> (lo, hi, step), def,
                                                  AudioParameterFloatAttributes().withLabel (unit).withAutomatable (automatable));
}

std::unique_ptr<AudioParameterBool> boolParam (const char* id, const char* name, bool def, bool automatable = true)
{
    return std::make_unique<AudioParameterBool> (ParameterID { id, kVersion }, name, def,
                                                 AudioParameterBoolAttributes().withAutomatable (automatable));
}

std::unique_ptr<AudioParameterChoice> choiceParam (const char* id, const char* name, juce::StringArray choices, int def)
{
    return std::make_unique<AudioParameterChoice> (ParameterID { id, kVersion }, name, std::move (choices), def);
}
}  // namespace

const std::vector<const char*>& persistentParameterIds()
{
    static const std::vector<const char*> ids = { ids::mode,     ids::capture,  ids::wait,     ids::chunk,     ids::delay,
                                                  ids::inGain,   ids::outVol,   ids::edgeFade, ids::liveFade,  ids::exact,
                                                  ids::speed,    ids::direction, ids::loop,    ids::repeat,    ids::tailGap,
                                                  ids::countdown, ids::autoStart, ids::threshold, ids::inChan };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (bool standalone)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (choiceParam (ids::mode, "Mode", { "Record & Reverse", "Live Reverse", "Reverse File" }, 0));
    layout.add (floatParam (ids::capture, "Record Length", 0.25f, 60.0f, 0.01f, 5.0f, " s"));
    layout.add (floatParam (ids::wait, "Wait Before Playback", 0.0f, 30.0f, 0.01f, 2.0f, " s"));
    layout.add (floatParam (ids::chunk, "Live Chunk", 0.1f, 5.0f, 0.01f, 0.5f, " s"));
    layout.add (floatParam (ids::delay, "Live Extra Delay", 0.0f, 30.0f, 0.01f, 2.0f, " s"));
    layout.add (floatParam (ids::inGain, "Input Gain", -24.0f, 24.0f, 0.1f, 0.0f, " dB"));
    layout.add (floatParam (ids::outVol, "Output Volume", -60.0f, 0.0f, 0.1f, standalone ? -12.0f : 0.0f, " dB"));
    layout.add (floatParam (ids::edgeFade, "Edge Fade", 0.0f, 10.0f, 0.1f, 3.0f, " ms"));
    layout.add (floatParam (ids::liveFade, "Live Edge Fade", 0.0f, 10.0f, 0.1f, 2.0f, " ms"));
    layout.add (boolParam (ids::exact, "Exact Samples", false));
    layout.add (floatParam (ids::speed, "Speed", 0.5f, 2.0f, 0.01f, 1.0f, " x"));
    layout.add (choiceParam (ids::direction, "Direction", { "Forward", "Backward" }, 1));
    layout.add (choiceParam (ids::loop, "Loop", { "Once", "Loop", "Ping-pong" }, 0));
    layout.add (boolParam (ids::repeat, "Repeat Session", false));
    layout.add (floatParam (ids::tailGap, "Repeat Gap", 0.0f, 2.0f, 0.01f, 0.5f, " s"));
    layout.add (choiceParam (ids::countdown, "Countdown", { "Off", "1 s", "3 s", "5 s" }, 0));
    layout.add (boolParam (ids::autoStart, "Auto Start on Voice", false));
    layout.add (floatParam (ids::threshold, "Voice Threshold", -65.0f, -15.0f, 0.5f, -45.0f, " dBFS"));
    layout.add (choiceParam (ids::inChan, "Input Channels", { "Input 1", "Input 2", "Mix 1+2", "Stereo 1+2" }, 0));
    // Never persisted and never automatable: direct monitoring always starts off (V1 section 3).
    layout.add (floatParam (ids::monitor, "Input Monitor", 0.0f, 100.0f, 1.0f, 0.0f, " %", false));
    // Momentary triggers act on a rising edge only; state restore cannot produce one.
    layout.add (boolParam (ids::trgStart, "Start / Stop (trigger)", false));
    layout.add (boolParam (ids::trgHold, "Hold to Record (trigger)", false));
    layout.add (boolParam (ids::trgReplay, "Replay (trigger)", false));
    layout.add (boolParam (ids::trgFreeze, "Freeze / Resume (trigger)", false));
    return layout;
}

ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& s)
    : mode (s.getRawParameterValue (ids::mode)), capture (s.getRawParameterValue (ids::capture)),
      wait (s.getRawParameterValue (ids::wait)), chunk (s.getRawParameterValue (ids::chunk)),
      delay (s.getRawParameterValue (ids::delay)), inGain (s.getRawParameterValue (ids::inGain)),
      outVol (s.getRawParameterValue (ids::outVol)), edgeFade (s.getRawParameterValue (ids::edgeFade)),
      liveFade (s.getRawParameterValue (ids::liveFade)), exact (s.getRawParameterValue (ids::exact)),
      speed (s.getRawParameterValue (ids::speed)), direction (s.getRawParameterValue (ids::direction)),
      loop (s.getRawParameterValue (ids::loop)), repeat (s.getRawParameterValue (ids::repeat)),
      tailGap (s.getRawParameterValue (ids::tailGap)), countdown (s.getRawParameterValue (ids::countdown)),
      autoStart (s.getRawParameterValue (ids::autoStart)), threshold (s.getRawParameterValue (ids::threshold)),
      inChan (s.getRawParameterValue (ids::inChan)), monitor (s.getRawParameterValue (ids::monitor)),
      trgStart (s.getRawParameterValue (ids::trgStart)), trgHold (s.getRawParameterValue (ids::trgHold)),
      trgReplay (s.getRawParameterValue (ids::trgReplay)), trgFreeze (s.getRawParameterValue (ids::trgFreeze))
{
}

Settings ParamRefs::readSettings() const noexcept
{
    Settings s;
    s.mode = static_cast<Mode> (juce::jlimit (0, 2, juce::roundToInt (mode->load())));
    s.captureSeconds = static_cast<double> (capture->load());
    s.waitSeconds = static_cast<double> (wait->load());
    s.liveChunkSeconds = static_cast<double> (chunk->load());
    s.liveDelaySeconds = static_cast<double> (delay->load());
    s.tailGapSeconds = static_cast<double> (tailGap->load());
    s.countdownSeconds = kCountdownChoices[juce::jlimit (0, 3, juce::roundToInt (countdown->load()))];
    s.autoStart = autoStart->load() >= 0.5f;
    s.thresholdDb = static_cast<double> (threshold->load());
    s.repeatSession = repeat->load() >= 0.5f;
    s.speed = static_cast<double> (speed->load());
    s.direction = juce::roundToInt (direction->load()) == 0 ? Direction::Forward : Direction::Backward;
    s.loop = static_cast<LoopPattern> (juce::jlimit (0, 2, juce::roundToInt (loop->load())));
    s.edgeFadeMs = static_cast<double> (edgeFade->load());
    s.liveFadeMs = static_cast<double> (liveFade->load());
    s.exactSamples = exact->load() >= 0.5f;
    return s;
}

InputChannelMode ParamRefs::readChannelMode() const noexcept
{
    return static_cast<InputChannelMode> (juce::jlimit (0, 3, juce::roundToInt (inChan->load())));
}
}  // namespace rb
