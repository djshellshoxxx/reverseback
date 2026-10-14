// Regression tests for the defects found by the pre-release audit (processor, state, threading).
#include "ProcHarness.h"
#include "SafeParse.h"

#include <atomic>
#include <thread>

using namespace rbt;
using RS = rb::RecordTransport::State;

namespace
{
void exact (ProcHarness& h)
{
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::outVol, 0);
}
}  // namespace

RB_TEST (audit_file_loaded_while_no_device_runs_reaches_the_engine_at_prepare)
{
    ProcHarness h;
    exact (h);
    h.p->releaseResources();   // the output device went away (or never started)
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("f.wav"), wav, 48000.0, 1, 32, 30000, true));
    h.set (rb::ids::mode, 2);
    h.p->loadFile (t.file ("f.wav"));
    CHECK (pumpUntil ([&] { return h.p->fileAsset() != nullptr; }));   // the UI now shows the file

    h.p->prepareToPlay (48000.0, 512);   // device (re)started: the engine must receive what the UI shows
    h.run (512);
    const auto base = h.outL.size();
    h.p->playFile (true);
    h.run (40000);
    CHECK_EQ (h.outL[base], testSample (0, 29999));   // used to stay silent: the SetFile command had been dropped
}

RB_TEST (audit_state_saved_from_another_thread_while_a_file_loads_is_consistent)
{
    ProcHarness h;
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("a.wav"), wav, 48000.0, 1, 24, 20000));
    CHECK (writeFixture (t.file ("b.wav"), wav, 48000.0, 1, 24, 40000));
    h.set (rb::ids::mode, 2);

    std::atomic<bool> stop { false };
    std::atomic<int> broken { 0 };
    std::thread saver ([&]
    {
        while (! stop.load())
        {
            juce::MemoryBlock block;
            h.p->getStateInformation (block);          // what a host does when it saves a project
            if (auto xml = rb::xmlFromStateBlob (block.getData(), static_cast<int> (block.getSize())))
                if (auto* f = xml->getChildByName ("File"))
                {
                    const auto path = f->getStringAttribute ("path");
                    const auto end = f->getStringAttribute ("end").getLargeIntValue();
                    const bool a = path.endsWith ("a.wav") && end == 20000;
                    const bool b = path.endsWith ("b.wav") && end == 40000;
                    if (! a && ! b)
                        ++broken;   // path of one file with the selection of the other
                }
        }
    });
    for (int i = 0; i < 6; ++i)
    {
        h.p->loadFile (t.file (i % 2 == 0 ? "a.wav" : "b.wav"));
        CHECK (pumpUntil ([&] { return h.p->fileAsset() != nullptr && h.p->fileAsset()->file.getFileName() == (i % 2 == 0 ? "a.wav" : "b.wav"); }));
    }
    stop = true;
    saver.join();
    CHECK_EQ (broken.load(), 0);
}

RB_TEST (audit_prepare_waits_for_a_block_in_flight_and_never_overlaps_it)
{
    ProcHarness h;
    exact (h);
    h.set (rb::ids::capture, 0.2);
    h.set (rb::ids::wait, 0.0);
    h.p->actionStart();
    std::atomic<bool> stop { false };
    std::atomic<int> processed { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        while (! stop.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, 0.25f);
            h.p->processBlock (buf, midi);
            ++processed;
        }
    });
    // A host re-preparing (rate / block size change) while the audio thread keeps running used to race
    // on the transports, the command queue and the snapshot lock. Under TSan/ASan this test is the check.
    for (int i = 0; i < 40; ++i)
    {
        h.p->prepareToPlay (i % 2 == 0 ? 44100.0 : 48000.0, i % 3 == 0 ? 256 : 1024);
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    stop = true;
    audio.join();
    CHECK (processed.load() > 10);
}

RB_TEST (audit_start_pressed_twice_before_the_audio_thread_sees_it_queues_one_take)
{
    ProcHarness h;
    exact (h);
    h.set (rb::ids::capture, 1.0);
    h.p->actionStart();
    h.p->actionStart();          // double click: the first command has not been processed yet
    h.p->actionStart();
    h.run (512);
    CHECK (! h.recordState (RS::Ready));   // recording, not toggled off again by the second press
    h.run (60000);
    CHECK_EQ (h.p->currentTake()->id, 1u);   // exactly one take
}

RB_TEST (audit_bypass_hands_back_start_commands_and_leaves_the_ui_ready)
{
    ProcHarness h;
    exact (h);
    h.set (rb::ids::capture, 1.0);
    h.p->actionStart();
    h.run (4096);
    CHECK (h.recordState (RS::Recording));

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    buf.clear();
    h.p->processBlockBypassed (buf, midi);
    CHECK (h.recordState (RS::Ready));       // the UI sees the cancelled recording at once
    for (int i = 0; i < 3; ++i)
    {
        h.p->actionStart();                  // presses while bypassed must not start or pile up anything
        h.p->processBlockBypassed (buf, midi);
    }
    CHECK (h.recordState (RS::Ready));
    h.p->serviceNow();
    h.run (2048);                            // back to normal processing: nothing starts by itself
    CHECK (h.recordState (RS::Ready));
}

RB_TEST (audit_trigger_parameter_left_high_is_not_a_new_edge_after_a_device_restart)
{
    ProcHarness h;
    exact (h);
    h.set (rb::ids::trgStart, 1);            // a host automation lane parked the trigger high
    h.run (2048);
    h.p->serviceNow();
    h.p->actionStop();
    h.run (2048);
    CHECK (h.recordState (RS::Ready));
    h.p->releaseResources();
    h.p->prepareToPlay (48000.0, 512);       // device restart: used to forget the previous value and see an edge
    h.run (4096);
    CHECK (h.recordState (RS::Ready));
}

RB_TEST (audit_hostile_state_blobs_are_refused_without_crashing)
{
    ProcHarness h;
    // NaN parameters are ignored
    juce::XmlElement xml ("ReverseBackState");
    xml.setAttribute ("capture", "nan");
    xml.setAttribute ("speed", "inf");
    xml.setAttribute ("wait", 3.5);
    h.p->setStateFromXml (xml, false);
    CHECK (std::isfinite (h.get ("capture")) && h.get ("capture") > 0.0);
    CHECK (std::isfinite (h.get ("speed")));
    CHECK_NEAR (h.get ("wait"), 3.5, 1e-3);

    // absurd nesting is refused before it reaches the recursive parser
    juce::String deep = juce::String::repeatedString ("<a>", 200000);
    juce::MemoryBlock block;
    block.setSize (8 + static_cast<size_t> (deep.getNumBytesAsUTF8()));
    auto* d = static_cast<char*> (block.getData());
    d[0] = 0x56; d[1] = 0x43; d[2] = 0x32; d[3] = 0x21;   // magic, little endian
    const int len = static_cast<int> (deep.getNumBytesAsUTF8());
    std::memcpy (d + 4, &len, 4);
    std::memcpy (d + 8, deep.toRawUTF8(), static_cast<size_t> (len));
    h.p->setStateInformation (block.getData(), static_cast<int> (block.getSize()));   // must simply return
    CHECK (h.recordState (RS::Ready));
}

RB_TEST (audit_presets_saved_by_two_instances_are_both_kept)
{
    TempDir t;
    const juce::File settings = t.file ("settings.json");
    rb::ReverseBackProcessor a (settings), b (settings);   // two plugin instances, same user
    a.saveUserPreset ("From A");
    b.saveUserPreset ("From B");                            // b started before a saved: its copy is stale
    a.saveStoredSettings();                                 // an unrelated save from a must not drop B's preset either
    rb::SettingsStore store (settings);
    const auto disk = store.load();
    CHECK_EQ (disk.userPresets.size(), std::size_t { 2 });
    CHECK_EQ (a.userPresets().size(), std::size_t { 2 });
}
