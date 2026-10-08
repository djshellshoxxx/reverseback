// P01-P11: host-facing behaviour of the shared processor (VST3 / CLAP / Standalone wrappers use it unchanged).
#include "ProcHarness.h"

using namespace rbt;
using RS = rb::RecordTransport::State;

namespace
{
using CS = juce::AudioChannelSet;
}

RB_TEST (P01_bus_layouts_mono_and_stereo_accepted_others_rejected)
{
    TempDir t;
    rb::ReverseBackProcessor p (t.file ("s.json"));
    struct Case { CS in, out; bool ok; };
    const Case cases[] = { { CS::stereo(), CS::stereo(), true }, { CS::mono(), CS::stereo(), true }, { CS::stereo(), CS::mono(), true },
                           { CS::mono(), CS::mono(), true }, { CS::disabled(), CS::stereo(), true }, { CS::disabled(), CS::mono(), true },
                           { CS::stereo(), CS::disabled(), false }, { CS::stereo(), CS::create5point1(), false }, { CS::create5point1(), CS::stereo(), false } };
    for (const auto& c : cases)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (c.in);
        l.outputBuses.add (c.out);
        CHECK_EQ (p.checkBusesLayoutSupported (l), c.ok);
    }
}

RB_TEST (P02_parameters_match_the_published_table)
{
    TempDir t;
    rb::ReverseBackProcessor p (t.file ("s.json"));
    struct Row { const char* id; double lo, hi, def; };
    const Row rows[] = { { "capture", 0.25, 60, 5 }, { "wait", 0, 30, 2 }, { "chunk", 0.1, 5, 0.5 }, { "delay", 0, 30, 2 },
                         { "inGain", -24, 24, 0 }, { "outVol", -60, 0, 0 }, { "edgeFade", 0, 10, 3 }, { "liveFade", 0, 10, 2 },
                         { "speed", 0.5, 2, 1 }, { "tailGap", 0, 2, 0.5 }, { "threshold", -65, -15, -45 }, { "monitor", 0, 100, 0 } };
    for (const auto& r : rows)
    {
        auto* prm = p.apvts.getParameter (r.id);
        CHECK (prm != nullptr);
        if (prm == nullptr)
            continue;
        CHECK_NEAR (prm->convertFrom0to1 (0.0f), r.lo, 1e-4);
        CHECK_NEAR (prm->convertFrom0to1 (1.0f), r.hi, 1e-4);
        CHECK_NEAR (prm->convertFrom0to1 (prm->getDefaultValue()), r.def, 1e-3);
        const auto text = prm->getText (0.5f, 64);
        CHECK (std::abs (static_cast<double> (prm->getValueForText (text)) - 0.5) < 0.01);   // text round trip
    }
    const struct { const char* id; int count, def; } choices[] = { { "mode", 3, 0 }, { "direction", 2, 1 }, { "loop", 3, 0 }, { "countdown", 4, 0 }, { "inChan", 4, 0 } };
    for (const auto& c : choices)
    {
        auto* prm = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (c.id));
        CHECK (prm != nullptr);
        if (prm != nullptr)
        {
            CHECK_EQ (prm->choices.size(), c.count);
            CHECK_EQ (prm->getIndex(), c.def);
        }
    }
    for (const char* b : { "exact", "repeat", "autoStart", "trgStart", "trgHold", "trgReplay", "trgFreeze" })
        CHECK (p.apvts.getParameter (b) != nullptr && p.apvts.getParameter (b)->getDefaultValue() == 0.0f);
    CHECK (! p.apvts.getParameter ("monitor")->isAutomatable());
    CHECK_EQ (p.getParameters().size(), 24);
}

RB_TEST (P03_state_round_trip_contains_no_audio_and_excludes_monitor_and_triggers)
{
    ProcHarness h;
    h.set (rb::ids::capture, 9.5);
    h.set (rb::ids::speed, 1.25);
    h.set (rb::ids::loop, 2);
    h.set (rb::ids::inChan, 3);
    h.set (rb::ids::monitor, 80);
    h.set (rb::ids::mode, 1);
    h.set (rb::ids::mode, 0);
    h.set (rb::ids::capture, 0.5);
    h.set (rb::ids::wait, 0);
    h.p->actionStart();
    h.run (60000);
    CHECK (h.p->currentTake() != nullptr);
    h.set (rb::ids::capture, 9.5);

    juce::MemoryBlock state;
    h.p->getStateInformation (state);
    CHECK (state.getSize() < 2048);   // parameters only: the take is not in there
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    CHECK (xml != nullptr);
    CHECK (! xml->hasAttribute ("monitor"));
    CHECK (! xml->hasAttribute ("trgStart"));
    CHECK (xml->getChildByName ("File") == nullptr);

    ProcHarness g;
    g.p->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    g.run (2048);
    CHECK_NEAR (g.get (rb::ids::capture), 9.5, 1e-2);
    CHECK_NEAR (g.get (rb::ids::speed), 1.25, 1e-2);
    CHECK_EQ (juce::roundToInt (g.get (rb::ids::loop)), 2);
    CHECK_EQ (juce::roundToInt (g.get (rb::ids::inChan)), 3);
    CHECK_NEAR (g.get (rb::ids::monitor), 0.0, 1e-6);   // monitor never persists
    CHECK (g.p->currentTake() == nullptr);                // takes never persist
    CHECK (g.recordState (RS::Ready));

    // damaged / foreign state must not throw or change anything important
    const char junk[] = "not a state at all";
    g.p->setStateInformation (junk, static_cast<int> (sizeof (junk)));
    g.run (512);
    CHECK_NEAR (g.get (rb::ids::capture), 9.5, 1e-2);
}

RB_TEST (P04_restoring_state_with_arm_or_trigger_flags_never_starts_capture)
{
    ProcHarness h;
    juce::XmlElement xml ("ReverseBackState");
    xml.setAttribute ("version", 1);
    xml.setAttribute (rb::ids::autoStart, 1.0);
    xml.setAttribute (rb::ids::countdown, 2.0);
    xml.setAttribute (rb::ids::trgStart, 1.0);
    xml.setAttribute (rb::ids::trgHold, 1.0);
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (xml, mb);
    h.p->setStateInformation (mb.getData(), static_cast<int> (mb.getSize()));
    h.run (48000);
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->currentTake() == nullptr);
    CHECK (allSilent (h.outL, 0, h.outL.size()));
}

RB_TEST (P05_missing_file_in_state_leaves_file_slot_empty_with_a_message)
{
    ProcHarness h;
    juce::XmlElement xml ("ReverseBackState");
    xml.setAttribute ("version", 1);
    xml.setAttribute (rb::ids::mode, 2.0);
    auto* f = xml.createNewChildElement ("File");
    f->setAttribute ("path", "/nonexistent/folder/voice.wav");
    f->setAttribute ("begin", "0");
    f->setAttribute ("end", "1000");
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (xml, mb);
    h.p->setStateInformation (mb.getData(), static_cast<int> (mb.getSize()));
    CHECK (pumpUntil ([&] { return h.p->banner().kind != rb::BannerKind::None; }, 3000));
    CHECK (h.p->banner().text.contains ("File not found: voice.wav"));
    CHECK (h.p->fileAsset() == nullptr);
    h.run (2048);

    // an existing file is restored with its selection (path and selection only, never audio)
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("here.wav"), wav, 48000.0, 1, 16, 96000));
    xml.getChildByName ("File")->setAttribute ("path", t.file ("here.wav").getFullPathName());
    xml.getChildByName ("File")->setAttribute ("begin", "12000");
    xml.getChildByName ("File")->setAttribute ("end", "60000");
    juce::MemoryBlock mb2;
    juce::AudioProcessor::copyXmlToBinary (xml, mb2);
    h.p->setStateInformation (mb2.getData(), static_cast<int> (mb2.getSize()));
    CHECK (pumpUntil ([&] { return h.p->fileAsset() != nullptr; }));
    CHECK_EQ (h.p->fileSelection().begin, rb::Frame { 12000 });
    CHECK_EQ (h.p->fileSelection().end, rb::Frame { 60000 });
}

RB_TEST (P06_P11_timeline_is_identical_for_any_block_size_and_in_offline_mode)
{
    std::vector<std::vector<float>> results;
    for (int blk : { 1, 7, 64, 513, 8192 })
    {
        ProcHarness h (48000.0, 8192);
        h.set (rb::ids::exact, 1);
        h.set (rb::ids::outVol, 0);
        h.set (rb::ids::capture, 0.25);
        h.set (rb::ids::wait, 0.125);
        if (blk == 513)
            h.p->setNonRealtime (true);   // offline render must be indistinguishable
        h.p->actionStart();
        h.run (31000, blk);   // 12000 capture + 6000 wait + 12000 playback
        results.push_back (h.outL);
        CHECK (h.recordState (RS::Ready));
    }
    for (std::size_t i = 1; i < results.size(); ++i)
        CHECK (results[i] == results[0]);
    CHECK (! allSilent (results[0], 0, results[0].size()));
}

RB_TEST (P08_bypass_cancels_capture_keeps_the_previous_take_and_passes_audio_through)
{
    ProcHarness h;
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::capture, 0.25);
    h.set (rb::ids::wait, 0);
    h.p->actionStart();
    h.run (30000);
    const auto take = h.p->currentTake();
    CHECK (take != nullptr);
    h.p->actionStart();
    h.run (2048);
    CHECK (h.recordState (RS::Recording));

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 512; ++i)
    {
        buf.setSample (0, i, 0.25f);
        buf.setSample (1, i, 0.25f);
    }
    h.p->processBlockBypassed (buf, midi);
    CHECK_EQ (buf.getSample (0, 100), 0.25f);   // host pass-through
    h.run (4096);
    CHECK (h.recordState (RS::Ready));
    CHECK_EQ (h.p->currentTake().get(), take.get());
}

RB_TEST (P09_reported_latency_is_zero_in_plugin_builds)
{
    ProcHarness h;
    CHECK_EQ (h.p->getLatencySamples(), 0);
    CHECK (! h.p->isStandalone());
    CHECK (h.p->getTailLengthSeconds() == 0.0);
}

RB_TEST (trigger_parameters_act_on_rising_edges_only)
{
    ProcHarness h;
    h.set (rb::ids::capture, 0.25);
    h.set (rb::ids::wait, 0);
    h.run (1024);
    CHECK (h.recordState (RS::Ready));
    h.set (rb::ids::trgStart, 1);
    h.run (2048);
    CHECK (h.recordState (RS::Recording));
    h.set (rb::ids::trgStart, 0);   // falling edge: no action
    h.run (30000);
    CHECK (h.recordState (RS::Ready));
    CHECK_EQ (h.p->currentTake()->id, 1u);

    h.set (rb::ids::trgHold, 1);
    h.run (4096);
    CHECK (h.recordState (RS::Recording));
    h.set (rb::ids::trgHold, 0);   // releasing the trigger closes the held take
    h.run (4096);
    CHECK_EQ (h.p->currentTake()->id, 2u);

    h.set (rb::ids::trgReplay, 1);
    h.run (30000);
    CHECK (! allSilent (h.outL, h.outL.size() - 30000, h.outL.size()));
}

RB_TEST (mono_input_and_mono_output_layouts_process_correctly)
{
    ProcHarness h (48000.0, 512, 1, 1);
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::outVol, 0);
    h.set (rb::ids::capture, 0.25);
    h.set (rb::ids::wait, 0);
    h.p->actionStart();
    h.run (24000);
    CHECK (h.p->currentTake() != nullptr);
    CHECK_EQ (h.p->currentTake()->clip->channels(), 1);
    CHECK_NEAR (h.outL[12000], h.p->currentTake()->clip->channel (0)[11999], 1e-6);   // mono mix of the identical L/R engine output
}
