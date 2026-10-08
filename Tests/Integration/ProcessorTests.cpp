// A08, A13, A14, A16, A17, A20 at processor level: real parameters, commands, state and assets.
#include "ProcHarness.h"

using namespace rbt;
using RS = rb::RecordTransport::State;
using LS = rb::LiveTransport::State;

namespace
{
void exactNoVolume (ProcHarness& h)
{
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::outVol, 0);
}
}  // namespace

RB_TEST (A16_actions_drive_the_engine_through_real_parameters)
{
    ProcHarness h;
    exactNoVolume (h);
    h.set (rb::ids::capture, 0.5);
    h.set (rb::ids::wait, 0.25);
    CHECK (h.p->mode() == rb::Mode::Record);
    h.p->actionStart();
    h.run (4800);
    CHECK (h.recordState (RS::Recording));
    CHECK (h.p->isBusy());
    h.run (60000);   // 0.5 s capture + 0.25 s wait + 0.5 s playback
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->currentTake() != nullptr);
    CHECK_EQ (h.p->currentTake()->clip->frameCount(), rb::Frame { 24000 });

    // playback happened: reversed take starts after 0.5 s + 0.25 s
    const auto& take = *h.p->currentTake()->clip;
    CHECK_EQ (h.outL[24000 + 12000], take.channel (0)[23999]);
    CHECK_EQ (h.outL[24000 + 12000 + 23999], take.channel (0)[0]);

    // Replay plays the retained take with no new recording
    const auto before = h.outL.size();
    h.p->actionReplay();
    h.run (30000);
    CHECK_EQ (h.outL[before], take.channel (0)[23999]);
    CHECK_EQ (h.p->currentTake()->id, 1u);

    // parameters change engine behaviour: speed 2x halves the replay length
    h.set (rb::ids::speed, 2.0);
    const auto b2 = h.outL.size();
    h.p->actionReplay();
    h.run (30000);
    std::size_t last = 0;
    for (std::size_t i = b2; i < h.outL.size(); ++i)
        if (h.outL[i] != 0.0f)
            last = i - b2;
    CHECK (last > 11900 && last < 12001);
}

RB_TEST (A08_hold_stop_and_key_repeat_at_processor_level)
{
    ProcHarness h;
    exactNoVolume (h);
    h.p->actionHoldDown();
    h.run (512);
    h.p->actionHoldDown();   // key repeat: must not start a second capture or replace the buffer
    h.p->actionHoldDown();
    h.run (512 * 10);
    CHECK (h.recordState (RS::Recording));
    h.p->actionHoldUp();
    h.run (512 * 4);
    CHECK_EQ (h.p->currentTake()->id, 1u);
    CHECK_EQ (h.p->currentTake()->clip->frameCount(), rb::Frame { 512 * 11 });

    // a tap shorter than 50 ms keeps the previous take and says why
    h.p->actionStop();
    h.run (2048);
    h.p->actionHoldDown();
    h.run (512);
    h.p->actionHoldUp();
    h.run (512 * 2);
    CHECK_EQ (h.p->currentTake()->id, 1u);
    CHECK (h.p->banner().text.contains ("Hold longer"));
}

RB_TEST (A13_no_input_device_shows_inline_error_and_file_mode_still_works)
{
    ProcHarness h (48000.0, 512, 0, 2);   // input bus disabled: no microphone / nothing routed
    exactNoVolume (h);
    h.p->actionStart();
    h.run (2048);
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->banner().kind == rb::BannerKind::Device);
    CHECK (h.p->banner().text.contains ("No input"));

    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("f.wav"), wav, 48000.0, 1, 32, 30000, true));
    h.set (rb::ids::mode, 2);
    h.p->loadFile (t.file ("f.wav"));
    CHECK (pumpUntil ([&] { return h.p->fileAsset() != nullptr; }));
    h.run (512);
    const auto base = h.outL.size();
    h.p->playFile (true);
    h.run (40000);
    CHECK_EQ (h.outL[base], testSample (0, 29999));
    CHECK_EQ (h.outL[base + 29999], testSample (0, 0));
}

RB_TEST (A13_device_stop_or_rate_change_stops_transport_and_keeps_takes)
{
    ProcHarness h;
    exactNoVolume (h);
    h.set (rb::ids::capture, 0.5);
    h.set (rb::ids::wait, 0.0);
    h.p->actionStart();
    h.run (50000);   // capture 24000 + playback 24000
    const auto take1 = h.p->currentTake();
    CHECK (take1 != nullptr);

    // start a second recording, then the device disappears mid-capture
    h.p->actionStart();
    h.run (4000);
    CHECK (h.recordState (RS::Recording));
    h.p->releaseResources();
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    buf.clear();
    h.p->processBlock (buf, midi);
    CHECK_EQ (buf.getMagnitude (0, 512), 0.0f);   // silenced immediately

    // reconnect at another rate: transports are Ready, nothing resumes, the take survived
    h.rate = 44100.0;
    h.p->setPlayConfigDetails (2, 2, 44100.0, 512);
    h.p->prepareToPlay (44100.0, 512);
    h.run (20000);
    CHECK (h.recordState (RS::Ready));
    CHECK_EQ (h.p->currentTake().get(), take1.get());
    CHECK_NEAR (h.snap().sampleRate, 44100.0, 0);
    h.p->actionReplay();
    const auto b = h.outL.size();
    h.run (40000);
    std::size_t last = 0;
    for (std::size_t i = b; i < h.outL.size(); ++i)
        if (h.outL[i] != 0.0f)
            last = i - b;
    CHECK_NEAR (static_cast<double> (last + 1), 24000.0 * 44100.0 / 48000.0, 3.0);
}

RB_TEST (A14_failed_load_keeps_the_previous_file_and_selection_rules_hold)
{
    ProcHarness h;
    exactNoVolume (h);
    h.set (rb::ids::mode, 2);
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("good.wav"), wav, 48000.0, 2, 24, 96000));
    t.file ("bad.wav").replaceWithText ("garbage");
    h.p->loadFile (t.file ("good.wav"));
    CHECK (pumpUntil ([&] { return h.p->fileAsset() != nullptr; }));
    const auto good = h.p->fileAsset();

    h.p->loadFile (t.file ("bad.wav"));
    pumpUntil ([&] { return h.p->banner().kind == rb::BannerKind::File; });
    CHECK (h.p->banner().text.contains ("Could not load"));
    CHECK_EQ (h.p->fileAsset().get(), good.get());   // still the good file

    CHECK (! h.p->setFileSelection ({ 1000, 1100 }));          // shorter than 50 ms
    CHECK (! h.p->setFileSelection ({ 0, 96001 }));            // outside the file
    CHECK (h.p->setFileSelection ({ 4800, 48000 }));
    CHECK_EQ (h.p->fileSelection().begin, rb::Frame { 4800 });

    h.run (512);
    const auto base = h.outL.size();
    h.p->playFile (true);
    h.run (50000);
    CHECK_NEAR (h.outL[base], testSample (0, 47999), 2.0e-6);
    CHECK_NEAR (h.outL[base + 43199], testSample (0, 4800), 2.0e-6);
    CHECK (allSilent (h.outL, base + 43200 + 1, h.outL.size()));
}

RB_TEST (A17_presets_surprise_and_state_restore_never_record_or_touch_gain)
{
    ProcHarness h;
    h.set (rb::ids::inGain, 6.0);
    h.set (rb::ids::outVol, -20.0);
    h.p->applyPreset (rb::builtinPresets()[2].values);   // Backwards Conversation: Live, 1 s chunks, 0.5 s delay
    h.run (4096);
    CHECK (h.p->mode() == rb::Mode::Live);
    CHECK_NEAR (h.get (rb::ids::chunk), 1.0, 1e-3);
    CHECK_NEAR (h.get (rb::ids::delay), 0.5, 1e-3);
    CHECK (h.liveState (LS::Ready));                        // applying a preset does not start anything
    CHECK_NEAR (h.get (rb::ids::inGain), 6.0, 1e-3);        // gain untouched
    CHECK_NEAR (h.get (rb::ids::outVol), -20.0, 1e-3);

    h.set (rb::ids::mode, 0);
    h.p->actionStart();
    h.run (2048);
    CHECK (h.recordState (RS::Recording));
    h.p->applyPreset (rb::builtinPresets()[0].values);      // a preset mid-session stops it and stays Ready
    h.run (4096);
    CHECK (h.recordState (RS::Ready));

    for (int i = 0; i < 50; ++i)
    {
        const double cap = h.get (rb::ids::capture), vol = h.get (rb::ids::outVol);
        h.p->applySurprise();
        CHECK_NEAR (h.get (rb::ids::capture), cap, 1e-6);
        CHECK_NEAR (h.get (rb::ids::outVol), vol, 1e-6);
        CHECK (h.get (rb::ids::speed) >= 0.5 && h.get (rb::ids::speed) <= 2.0);
    }
    h.run (2048);
    CHECK (h.recordState (RS::Ready));   // Surprise never starts a recording

    // state containing triggers, monitor and recording instructions is applied only for persistent parameters
    juce::XmlElement xml ("ReverseBackState");
    xml.setAttribute ("version", 1);
    xml.setAttribute (rb::ids::capture, 12.5);
    xml.setAttribute (rb::ids::trgStart, 1.0);
    xml.setAttribute (rb::ids::monitor, 100.0);
    h.p->setStateFromXml (xml, false);
    h.run (4096);
    CHECK_NEAR (h.get (rb::ids::capture), 12.5, 1e-3);
    CHECK_NEAR (h.get (rb::ids::monitor), 0.0, 1e-6);
    CHECK_NEAR (h.get (rb::ids::trgStart), 0.0, 1e-6);
    CHECK (h.recordState (RS::Ready));
}

RB_TEST (A20_export_level_does_not_depend_on_output_volume)
{
    ProcHarness h;
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::capture, 0.5);
    h.set (rb::ids::wait, 0.0);
    h.p->actionStart();
    h.run (40000);
    CHECK (h.p->currentTake() != nullptr);

    TempDir t;
    float peaks[2] = {};
    int k = 0;
    for (double vol : { 0.0, -40.0 })
    {
        h.set (rb::ids::outVol, vol);
        const auto src = h.p->exportSource();
        CHECK (src.valid());
        auto req = h.p->makeExportRequest (src, {}, t.file ("v" + juce::String (k) + ".wav"), false);
        CHECK (rb::writeExport (req, nullptr).ok());
        const auto back = readFile (req.destination);
        for (float v : back.data[0])
            peaks[k] = std::max (peaks[k], std::abs (v));
        ++k;
    }
    CHECK (peaks[0] > 0.2f);
    CHECK_EQ (peaks[0], peaks[1]);
}

RB_TEST (memory_budget_refuses_oversized_recordings_with_a_banner)
{
    ProcHarness h (192000.0, 8192);
    h.set (rb::ids::capture, 60);
    h.set (rb::ids::wait, 0);
    h.set (rb::ids::inChan, 3);   // stereo: 92 MB per take
    h.p->actionStart();
    h.run (192000ll * 122, 8192);   // 60 s capture + 60 s playback
    CHECK (h.p->currentTake() != nullptr);
    CHECK (h.p->banner().kind == rb::BannerKind::None);
    h.set (rb::ids::repeat, 1);   // two buffers plus the retained take would exceed 256 MiB
    h.p->actionStart();
    h.run (8192);
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->banner().kind == rb::BannerKind::Engine);
    CHECK (h.p->banner().text.contains ("memory"));
}

RB_TEST (C12_loop_and_repeat_session_exclude_each_other)
{
    ProcHarness h;
    h.set (rb::ids::repeat, 1);
    h.run (1024);
    h.set (rb::ids::loop, 1);
    h.run (1024);
    CHECK (h.get (rb::ids::loop) == 1.0 ? h.get (rb::ids::repeat) < 0.5 : true);
    h.set (rb::ids::loop, 0);
    h.set (rb::ids::repeat, 1);
    h.run (1024);
    h.set (rb::ids::loop, 2);
    h.run (1024);
    CHECK (! (h.get (rb::ids::loop) > 0.5 && h.get (rb::ids::repeat) > 0.5));
}

RB_TEST (live_actions_freeze_and_frozen_chunk_export)
{
    ProcHarness h;
    h.set (rb::ids::exact, 1);
    h.set (rb::ids::mode, 1);
    h.set (rb::ids::chunk, 0.25);
    h.set (rb::ids::delay, 0.25);
    h.p->actionStart();
    h.run (30000);
    CHECK (h.liveState (LS::Running) || h.liveState (LS::Filling));
    h.p->actionFreezeResume();
    h.run (30000);
    CHECK (h.liveState (LS::Frozen));
    auto chunk = h.p->copyFrozenChunk();
    CHECK (chunk != nullptr);
    CHECK_EQ (chunk->frameCount(), rb::Frame { 12000 });
    const auto src = h.p->exportSource();
    CHECK (src.valid());
    h.p->actionFreezeResume();   // Resume
    h.run (2048);
    CHECK (h.liveState (LS::Filling));
    h.p->actionStop();
    h.run (2048);
    CHECK (h.liveState (LS::Ready));
}
