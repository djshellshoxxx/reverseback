// A02, A03, A06, A07, A08, A18: Record & Reverse transport.
#include "Rig.h"

using namespace rbt;

namespace
{
Settings recordSettings (double capture, double wait)
{
    Settings s;
    s.mode = Mode::Record;
    s.captureSeconds = capture;
    s.waitSeconds = wait;
    s.exactSamples = true;
    return s;
}
}  // namespace

RB_TEST (A02_A03_timeline_is_exact_for_every_block_size)
{
    for (std::size_t block : { 1u, 64u, 127u, 256u, 512u, 1024u })
    {
        Rig r;
        r.apply (recordSettings (5.0, 2.0));
        r.startRecord();
        r.run(600000, block);
        CHECK_EQ (r.out[0].size(), std::size_t { 600000 });

        CHECK (allZero (r.out[0], 0, 336000));   // capture + wait: silent
        bool exact = true;
        for (std::size_t n = 0; n < 240000 && exact; ++n)
            exact = r.out[0][336000 + n] == rampInput (239999 - n);   // original last sample plays first
        CHECK (exact);
        CHECK (allZero (r.out[0], 576000, 600000));
        CHECK (r.out[0] == r.out[1]);

        auto takes = r.takes();
        CHECK_EQ (takes.size(), std::size_t { 1 });
        CHECK_EQ (takes[0]->frameCount(), Frame { 240000 });
        CHECK_EQ (r.count (EventType::PlaybackFinished), 1);
        CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
    }
}

RB_TEST (A02_zero_wait_starts_playback_on_the_next_sample)
{
    Rig r;
    r.apply (recordSettings (1.0, 0.0));
    r.startRecord();
    r.run (100000, 777);
    CHECK_EQ (r.out[0][48000], rampInput (47999));
    CHECK_EQ (r.out[0][47999], 0.0f);
}

RB_TEST (A07_input_ignored_during_wait_and_playback_and_repeat_starts_fresh_capture)
{
    Rig r;
    Settings s = recordSettings (1.0, 0.5);
    s.repeatSession = true;
    s.tailGapSeconds = 0.5;
    r.apply (s);
    r.startRecord();
    r.run (48000 * 5, 300);

    // cycle: capture [0,1) wait [1,1.5) play [1.5,2.5) gap [2.5,3.0) capture [3.0,4.0) wait play [5.0,..)
    auto takes = r.takes();
    CHECK (takes.size() >= 2);
    CHECK_EQ (takes[0]->channel (0)[0], rampInput (0));
    CHECK_EQ (takes[1]->channel (0)[0], rampInput (3 * 48000));   // the second take begins at the cycle's second capture
    CHECK_EQ (takes[1]->channel (0)[48000 - 1], rampInput (4 * 48000 - 1));
    // playback of take 1 is reversed take 1 only (no microphone bleed-through during playback)
    CHECK_EQ (r.out[0][72000], rampInput (47999));
    CHECK_EQ (r.out[0][72000 + 47999], rampInput (0));
    CHECK (allZero (r.out[0], 120000, 144000));   // ready gap is silent
    CHECK (r.count (EventType::NeedSpareTake) >= 2);
}

RB_TEST (A06_stop_in_every_state_silences_and_preserves_previous_take)
{
    struct Probe { const char* name; Frame runBeforeStop; Settings settings; };
    Settings base = recordSettings (1.0, 0.5);
    Settings countdown = base;  countdown.countdownSeconds = 3.0;
    Settings armed = base;      armed.autoStart = true;
    Settings repeat = base;     repeat.repeatSession = true; repeat.tailGapSeconds = 1.0;
    const Probe probes[] = { { "countdown", 24000, countdown }, { "armed", 24000, armed }, { "recording", 24000, base },
                             { "waiting", 48000 + 12000, base }, { "playing", 48000 + 24000 + 12000, base },
                             { "gap", 48000 + 24000 + 48000 + 12000, repeat } };

    for (const auto& p : probes)
    {
        Rig r;
        r.input = [] (Frame) { return 0.001f; };   // stays below the -45 dBFS trigger, so Armed really stays Armed
        // First, produce a retained take that must survive.
        r.apply (recordSettings (0.5, 0.0));
        r.startRecord();
        r.run (48000, 256);
        CHECK_EQ (r.takes().size(), std::size_t { 1 });

        r.apply (p.settings);
        r.startRecord();
        r.run (p.runBeforeStop, 256);
        r.cmd (CommandType::Stop);
        const std::size_t stopAt = r.out[0].size();
        r.run (48000 * 3, 256);

        // within the 10 ms stop fade + 1 block the output is silent and stays silent: no queued playback
        CHECK (allZero (r.out[0], stopAt + 480 + 256, r.out[0].size()));
        CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));

        // previous complete take preserved: Replay plays it back
        const std::size_t before = r.out[0].size();
        r.cmd (CommandType::Replay);
        r.run (30000, 256);
        bool heard = false;
        for (std::size_t i = before; i < r.out[0].size(); ++i)
            heard = heard || r.out[0][i] != 0.0f;
        CHECK (heard);
        if (! heard)
            std::printf ("      (state under test: %s)\n", p.name);
    }
}

RB_TEST (A06_stop_during_playback_fades_within_stop_fade)
{
    Rig r;
    r.input = [] (Frame) { return 0.5f; };
    r.apply (recordSettings (1.0, 0.0));
    r.startRecord();
    r.run (48000 + 4800, 256);
    const std::size_t stopAt = r.out[0].size();
    r.cmd (CommandType::Stop);
    r.run (4800, 256);
    CHECK_NEAR (r.out[0][stopAt - 1], 0.5, 1e-6);
    CHECK (r.out[0][stopAt + 100] < 0.5f);
    CHECK_EQ (r.out[0][stopAt + 480], 0.0f);
    CHECK (allZero (r.out[0], stopAt + 480, r.out[0].size()));
}

RB_TEST (A08_hold_release_short_tap_key_repeat_and_limit)
{
    // release closes a valid take of exactly the held duration (block aligned)
    {
        Rig r;
        r.apply (recordSettings (5.0, 0.0));
        r.startRecord (true);
        r.run (14400, 128);
        r.cmd (CommandType::ReleaseHold);
        r.run (20000, 128);
        auto t = r.takes();
        CHECK_EQ (t.size(), std::size_t { 1 });
        CHECK_EQ (t[0]->frameCount(), Frame { 14400 });
        CHECK_EQ (r.out[0][14400], rampInput (14399));   // reversed hold take plays right away (wait 0)
    }
    // key repeat: a second start while recording is refused and does not allocate another capture
    {
        Rig r;
        r.apply (recordSettings (5.0, 0.0));
        r.startRecord (true);
        r.run (4800, 128);
        r.startRecord (true);
        r.startRecord (true);
        r.run (4800, 128);
        r.cmd (CommandType::ReleaseHold);
        r.run (200, 128);
        CHECK_EQ (r.takes().size(), std::size_t { 1 });
        CHECK_EQ (r.takes()[0]->frameCount(), Frame { 9600 });
        CHECK (r.hasError (ErrorCode::Busy));
    }
    // short tap (< 50 ms) never replaces the previous take
    {
        Rig r;
        r.apply (recordSettings (0.5, 0.0));
        r.startRecord();
        r.run (48000, 256);
        CHECK_EQ (r.takes().size(), std::size_t { 1 });
        r.startRecord (true);
        r.run (1280, 128);   // ~26 ms
        r.cmd (CommandType::ReleaseHold);
        r.run (256, 128);
        CHECK_EQ (r.count (EventType::TooShort), 1);
        CHECK_EQ (r.takes().size(), std::size_t { 1 });
        CHECK_EQ (r.eng.snapshot().takeFrames, Frame { 24000 });
    }
    // hold limit closes the take at exactly the 60 s maximum
    {
        Rig r;
        r.apply (recordSettings (5.0, 0.0));
        r.startRecord (true);
        r.run (48000ull * 61, 1024);
        auto t = r.takes();
        CHECK_EQ (t.size(), std::size_t { 1 });
        CHECK_EQ (t[0]->frameCount(), Frame { 48000ull * 60 });
    }
}

RB_TEST (finish_early_uses_recorded_frames_if_at_least_50ms)
{
    Rig r;
    r.apply (recordSettings (5.0, 0.0));
    r.startRecord();
    r.run (48000, 256);
    r.cmd (CommandType::FinishEarly);
    r.run (60000, 256);
    CHECK_EQ (r.takes().size(), std::size_t { 1 });
    CHECK_EQ (r.takes()[0]->frameCount(), Frame { 48000 });

    Rig q;
    q.apply (recordSettings (5.0, 0.0));
    q.startRecord();
    q.run (1024, 256);
    q.cmd (CommandType::FinishEarly);
    q.run (2048, 256);
    CHECK_EQ (q.takes().size(), std::size_t { 0 });
    CHECK_EQ (q.count (EventType::TooShort), 1);
}

RB_TEST (countdown_precedes_capture_and_is_silent)
{
    Rig r;
    Settings s = recordSettings (1.0, 0.0);
    s.countdownSeconds = 3.0;
    r.apply (s);
    r.startRecord();
    r.run (48000 * 6, 300);
    CHECK_EQ (r.takes()[0]->channel (0)[0], rampInput (3 * 48000));
    CHECK (allZero (r.out[0], 0, 4 * 48000));
    CHECK_EQ (r.out[0][4 * 48000], rampInput (4 * 48000 - 1));
}

RB_TEST (A18_voice_trigger_includes_preroll_and_total_length_is_exact)
{
    constexpr Frame onset = 48000 * 2;   // loud step at 2 s, quiet before
    Rig r;
    r.input = [] (Frame i) { return i < onset ? 0.0005f : 0.1f + 1e-6f * static_cast<float> (i - onset); };
    Settings s = recordSettings (1.0, 0.0);
    s.autoStart = true;
    s.thresholdDb = -45.0;
    r.apply (s);
    r.startRecord();

    r.run (onset - 100, 256);   // quiet input keeps it Armed
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Armed));
    CHECK_EQ (r.takes().size(), std::size_t { 0 });

    r.run (48000 * 3, 256);
    auto t = r.takes();
    CHECK_EQ (t.size(), std::size_t { 1 });
    CHECK_EQ (t[0]->frameCount(), Frame { 48000 });                 // exactly the configured duration
    const Frame fireAt = onset + 2400;                              // RMS crosses on frame 2, then 50 ms sustained
    const Frame pre = 9600;                                         // 200 ms of available pre-roll
    const Frame firstIdx = fireAt + 1 - pre;
    CHECK_EQ (t[0]->channel (0)[0], r.input (firstIdx));
    CHECK_EQ (t[0]->channel (0)[pre - 1], r.input (fireAt));
    CHECK_EQ (t[0]->channel (0)[47999], r.input (firstIdx + 47999));
}

RB_TEST (A18_armed_can_be_cancelled_and_short_arming_limits_preroll)
{
    Rig r;
    r.input = [] (Frame i) { return i < 4800 ? 0.0f : 0.2f; };   // loud 100 ms after arming
    Settings s = recordSettings (0.5, 0.0);
    s.autoStart = true;
    r.apply (s);
    r.startRecord();
    r.run (48000, 128);
    auto t = r.takes();
    CHECK_EQ (t.size(), std::size_t { 1 });
    CHECK_EQ (t[0]->frameCount(), Frame { 24000 });
    // only 4800+2400 frames existed before the trigger fired, so pre-roll is limited to what was available
    CHECK_EQ (t[0]->channel (0)[0], 0.0f);
    CHECK_EQ (t[0]->channel (0)[4800 + 2400], 0.2f);

    Rig q;
    q.input = [] (Frame) { return 0.0f; };
    q.apply (s);
    q.startRecord();
    q.run (48000, 128);
    CHECK_EQ (static_cast<int> (q.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Armed));
    q.cmd (CommandType::Stop);
    q.run (256, 128);
    CHECK_EQ (static_cast<int> (q.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
    CHECK_EQ (q.takes().size(), std::size_t { 0 });
}

RB_TEST (take_selection_trim_and_undo_apply_to_replay)
{
    Rig r;
    r.apply (recordSettings (1.0, 0.0));
    r.startRecord();
    r.run (48000 + 48000, 256);
    r.cmd (CommandType::SetTakeSelection, false, 12000, 36000);
    const std::size_t before = r.out[0].size();
    r.cmd (CommandType::Replay);
    r.run (30000, 256);
    CHECK_EQ (r.out[0][before], rampInput (35999));
    CHECK_EQ (r.out[0][before + 23999], rampInput (12000));
    CHECK (allZero (r.out[0], before + 24000, before + 30000));
    // undo restores the whole take
    r.cmd (CommandType::SetTakeSelection, false, 0, 0);
    const std::size_t before2 = r.out[0].size();
    r.cmd (CommandType::Replay);
    r.run (50000, 256);
    CHECK_EQ (r.out[0][before2], rampInput (47999));
    CHECK_EQ (r.out[0][before2 + 47999], rampInput (0));
}

RB_TEST (record_process_does_not_allocate_in_any_state)
{
    Rig r;
    Settings s = recordSettings (0.5, 0.25);
    s.countdownSeconds = 1.0;
    s.autoStart = true;
    s.exactSamples = false;
    r.apply (s);
    r.input = [] (Frame i) { return i < 60000 ? 0.0f : 0.3f; };
    r.startRecord();
    std::vector<float> a (256, 0.1f), oL (256), oR (256);
    const float* in[1] = { a.data() };
    float* o[2] = { oL.data(), oR.data() };
    for (int i = 0; i < 1200; ++i)
    {
        if (i == 400)
            std::fill (a.begin(), a.end(), 0.5f);
        CHECK_NO_ALLOC (r.eng.process (in, 1, o, 256));
        r.service();
    }
}
