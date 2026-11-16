// Drives a ReverseBackProcessor exactly like a host would (planar blocks, in-place buffers).
#pragma once

#include "ReverseBackProcessor.h"
#include "TestSupport.h"

namespace rbt
{
struct ProcHarness
{
    explicit ProcHarness (double sampleRate = 48000.0, int blockSize = 512, int ins = 2, int outs = 2) : rate (sampleRate), block (blockSize)
    {
        p = std::make_unique<rb::ReverseBackProcessor> (tmp.file ("settings.json"));
        configure (ins, outs);
    }

    void configure (int ins, int outs)
    {
        if (ins != 2 || outs != 2)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (ins == 0 ? juce::AudioChannelSet::disabled() : (ins == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo()));
            layout.outputBuses.add (outs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            p->setBusesLayout (layout);
        }
        p->setPlayConfigDetails (ins, outs, rate, block);
        p->prepareToPlay (rate, block);
        numIn = ins;
        numOut = outs;
    }

    void set (const char* id, double plain)
    {
        auto* prm = p->apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (static_cast<float> (plain)));
    }
    double get (const char* id) { return static_cast<double> (p->apvts.getRawParameterValue (id)->load()); }

    void run (juce::int64 frames, int blk = 512)
    {
        juce::AudioBuffer<float> buf (std::max (2, std::max (numIn, numOut)), blk);
        juce::MidiBuffer midi;
        while (frames > 0)
        {
            const int n = static_cast<int> (std::min<juce::int64> (blk, frames));
            buf.setSize (buf.getNumChannels(), n, false, false, true);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample (ch, i, ch < numIn ? input (pos + i) * (ch == 0 ? 1.0f : 0.5f) : 0.0f);
            p->processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
            {
                outL.push_back (buf.getSample (0, i));
                outR.push_back (numOut > 1 ? buf.getSample (1, i) : buf.getSample (0, i));
            }
            pos += n;
            frames -= n;
            p->serviceNow();
        }
    }

    rb::Snapshot snap() const { return p->snapshot(); }
    bool recordState (rb::RecordTransport::State s) const { return snap().recordState == static_cast<std::uint8_t> (s); }
    bool liveState (rb::LiveTransport::State s) const { return snap().liveState == static_cast<std::uint8_t> (s); }

    TempDir tmp;
    std::unique_ptr<rb::ReverseBackProcessor> p;
    double rate;
    int block, numIn = 2, numOut = 2;
    juce::int64 pos = 0;
    std::function<float (juce::int64)> input = [] (juce::int64 i) { return 0.5f * static_cast<float> (std::sin (0.01 * static_cast<double> (i))); };
    std::vector<float> outL, outR;
};

inline bool allSilent (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < std::min (to, v.size()); ++i)
        if (v[i] != 0.0f)
            return false;
    return true;
}
}  // namespace rbt
