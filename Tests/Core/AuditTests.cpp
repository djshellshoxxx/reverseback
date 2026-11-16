// Regression tests for the defects found by the pre-release audit (engine and transports).
#include "Rig.h"
#include "Stages.h"

using namespace rbt;

namespace
{
Settings recordSettings (double capture, double wait)
{
    Settings s;
    s.captureSeconds = capture;
    s.waitSeconds = wait;
    s.exactSamples = true;
    return s;
}

Frame frames (double seconds) { return static_cast<Frame> (seconds * 48000.0); }
}  // namespace

// ---- commands the engine rejects must be handed back, never freed on the audio thread --------------
RB_TEST (audit_rejected_commands_are_retired_not_freed_on_the_audio_thread)
{
    Rig r;
    r.apply (recordSettings (1.0, 0.0));
    r.startRecord();                 // accepted
    r.run (1000);

    // second start while busy: rejected -> Busy, buffer must survive until the control thread drains it
    std::weak_ptr<AudioClip> second;
    {
        Command c;
        c.type = CommandType::StartRecord;
        c.take = AudioClip::create (48000.0, 1, frames (1.0));
        second = c.take;
        r.send (std::move (c));      // Rig::send fails the test if anything is freed or allocated inside handle()
    }
    CHECK (! second.expired());      // still referenced by the retire queue
    r.service();                     // control side: reads the Busy error and drains the retire queue
    CHECK (r.hasError (ErrorCode::Busy));
    CHECK (second.expired());        // freed on the control side

    // wrong mode: a live storage handed to a Record-mode engine
    std::weak_ptr<LiveStorage> storage;
    {
        Command c;
        c.type = CommandType::StartLive;
        c.live = LiveStorage::create (48000.0, 1, 24000, 0, 0);
        storage = c.live;
        r.send (std::move (c));
    }
    CHECK (! storage.expired());
    r.eng.events().drainRetired();
    CHECK (storage.expired());
}

RB_TEST (audit_undersized_take_is_rejected_instead_of_silently_truncating)
{
    Rig r;
    r.apply (recordSettings (2.0, 0.0));
    Command c;
    c.type = CommandType::StartRecord;
    c.take = AudioClip::create (48000.0, 1, frames (1.0));   // prepared for 1 s, engine wants 2 s
    r.send (std::move (c));
    r.run (4800);
    CHECK (r.hasError (ErrorCode::TakeTooSmall));
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
    r.run (frames (3.0));
    CHECK_EQ (r.takes().size(), std::size_t { 0 });
}

// ---- Repeat Session must never keep recording by itself after it was cancelled or re-prepared ---------
RB_TEST (audit_repeat_session_does_not_resurrect_after_prepare)
{
    Rig r;
    Settings s = recordSettings (1.0, 0.0);
    s.repeatSession = true;
    s.tailGapSeconds = 0.5;
    r.apply (s);
    r.startRecord();
    r.run (frames (1.2));           // capture done, playing the first take
    CHECK_EQ (r.takes().size(), std::size_t { 1 });

    r.eng.prepare (48000.0, 1024);  // device restart in the middle of the session
    r.apply (s);
    r.events.clear();
    r.cmd (CommandType::Replay);    // a plain replay of the old take
    r.run (frames (6.0));
    CHECK_EQ (r.count (EventType::NeedSpareTake), 0);
    CHECK (! r.hasError (ErrorCode::SpareTakeUnavailable));
    CHECK_EQ (r.takes().size(), std::size_t { 0 });   // no capture started on its own
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
}

RB_TEST (audit_stale_spare_buffer_from_an_earlier_session_is_ignored)
{
    Rig r;
    r.autoSpare = false;
    Settings s = recordSettings (1.0, 0.0);
    s.repeatSession = true;
    s.tailGapSeconds = 0.25;
    r.apply (s);
    r.startRecord();
    r.run (1000);
    r.cmd (CommandType::Stop);
    r.run (2000);

    std::weak_ptr<AudioClip> stale;
    {
        Command c;
        c.type = CommandType::ProvideSpareTake;
        c.take = AudioClip::create (48000.0, 1, 24000);   // half the capture length
        stale = c.take;
        r.send (std::move (c));
    }
    r.eng.events().drainRetired();
    CHECK (stale.expired());        // not adopted, handed back

    r.events.clear();
    r.startRecord();                // new session, no spare is ever answered
    r.run (frames (5.0));            // capture 1 + playback 1 + gap 0.25 + the 2 s grace period
    CHECK (r.hasError (ErrorCode::SpareTakeUnavailable));
    for (const auto& t : r.takes())
        CHECK_EQ (t->frameCount(), frames (1.0));   // never a short take from a stale buffer
}

RB_TEST (audit_repeat_session_applies_a_changed_gap_from_the_next_cycle)
{
    Rig r;
    Settings s = recordSettings (0.5, 0.0);
    s.repeatSession = true;
    s.tailGapSeconds = 0.5;
    r.apply (s);
    r.startRecord();
    r.run (frames (0.25));
    s.tailGapSeconds = 0.1;          // shorter gap while the first cycle is still recording
    r.apply (s);
    r.run (frames (4.0));
    const auto t = r.takes();
    CHECK (t.size() >= 2);
    if (t.size() >= 2)
    {
        // second capture starts after: capture 0.5 + playback 0.5 + gap 0.1 = 1.1 s (not 1.5 s)
        CHECK_EQ (t[1]->channel (0)[0], rampInput (frames (1.1)));
    }
}

RB_TEST (audit_finish_early_before_50ms_is_ignored_and_the_capture_continues)
{
    Rig r;
    r.apply (recordSettings (1.0, 0.0));
    r.startRecord();
    r.run (1024);
    r.cmd (CommandType::FinishEarly);
    r.run (frames (1.2));
    CHECK_EQ (r.count (EventType::TooShort), 1);
    CHECK_EQ (r.takes().size(), std::size_t { 1 });
    if (! r.takes().empty())
        CHECK_EQ (r.takes()[0]->frameCount(), frames (1.0));
}

// ---- the event/retire queues must not lose takes or free memory when the control thread stalls -------
RB_TEST (audit_stalled_control_thread_neither_loses_events_nor_frees_on_the_audio_side)
{
    EventSink sink;
    std::vector<std::weak_ptr<const void>> refs;
    for (int i = 0; i < 300; ++i)
    {
        auto p = std::make_shared<int> (i);
        refs.push_back (p);
        CHECK_NO_ALLOC (sink.retire (std::move (p)));
    }
    CHECK_EQ (sink.retireOverflow(), 0u);
    for (const auto& w : refs)
        CHECK (! w.expired());       // nothing was released on the "audio" side
    for (int round = 0; round < 4; ++round)
    {
        sink.drainRetired();
        sink.service();
    }
    for (const auto& w : refs)
        CHECK (w.expired());

    // events: every TakeCompleted arrives, in order, even after a long stall
    for (int i = 0; i < 280; ++i)
    {
        Event e;
        e.type = EventType::TakeCompleted;
        e.a = static_cast<std::uint64_t> (i);
        sink.post (std::move (e));
    }
    std::uint64_t expected = 0;
    Event e;
    for (int round = 0; round < 8; ++round)
    {
        while (sink.poll (e))
            CHECK_EQ (e.a, expected++);
        sink.service();
    }
    CHECK_EQ (expected, std::uint64_t { 280 });
    CHECK_EQ (sink.eventsDropped(), 0u);
}

// ---- engine robustness ----------------------------------------------------------------------------
RB_TEST (audit_missing_second_input_plane_is_treated_as_mono)
{
    Rig r;
    r.apply (recordSettings (0.1, 0.0));
    r.startRecord();
    std::vector<float> a (512, 0.25f), l (512), rr (512);
    const float* in[2] = { a.data(), nullptr };
    float* out[2] = { l.data(), rr.data() };
    for (int i = 0; i < 20; ++i)
        r.eng.process (in, 2, out, 512);    // used to dereference the null plane
    r.service();
    CHECK_EQ (r.takes().size(), std::size_t { 1 });
}

// ---- limiter ----------------------------------------------------------------------------------
RB_TEST (audit_limiter_returns_to_exact_unity_after_a_burst)
{
    for (double rate : { 44100.0, 48000.0, 192000.0 })
    {
        OutputLimiter lim;
        lim.prepare (rate, 2);
        const std::size_t total = static_cast<std::size_t> (rate * 25.0);
        const std::size_t latency = lim.latencyFrames();
        std::vector<float> in (total), l (total), r (total);
        for (std::size_t i = 0; i < total; ++i)
            in[i] = i < 100 ? 1.5f : 0.1f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
        l = in;
        r = in;
        float* io[2] = { l.data(), r.data() };
        for (std::size_t pos = 0; pos < total; pos += 480)
        {
            float* p[2] = { io[0] + pos, io[1] + pos };
            lim.process (p, 2, std::min<std::size_t> (480, total - pos));
        }
        std::size_t different = 0;
        for (std::size_t i = total - static_cast<std::size_t> (rate * 2.0); i < total; ++i)
            different += l[i] != in[i - latency] ? 1 : 0;
        CHECK_EQ (different, std::size_t { 0 });   // bit-exact again, not stuck at 0.9999
    }
}

RB_TEST (audit_limiter_never_exceeds_the_ceiling)
{
    for (double rate : { 48000.0, 192000.0 })
    {
        OutputLimiter lim;
        lim.prepare (rate, 2);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> d (-3.0f, 3.0f);
        std::vector<float> l (200000), r (200000);
        for (auto& v : l) v = d (rng);
        for (auto& v : r) v = d (rng);
        float* io[2] = { l.data(), r.data() };
        lim.process (io, 2, l.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < l.size(); ++i)
            worst = std::max ({ worst, std::abs (l[i]), std::abs (r[i]) });
        CHECK (worst <= OutputLimiter::kCeiling);
    }
}

// ---- clip player ------------------------------------------------------------------------------
namespace
{
struct PlayerRig
{
    explicit PlayerRig (Frame len)
    {
        clip = makeRampClip (48000.0, len);
        p.configure (512, 16.0);
        p.setOutputRate (48000.0);
        p.setFadeFrames (0);
        p.setSource (clip.get(), { 0, len });
    }
    std::shared_ptr<AudioClip> clip;
    ClipPlayer p;
};
}  // namespace

RB_TEST (audit_ping_pong_direction_does_not_leak_into_the_next_playback)
{
    PlayerRig r (1000);
    r.p.setDirection (Direction::Backward);
    r.p.setLoop (LoopPattern::PingPong);
    r.p.play (true);
    renderPlayer (r.p, 1, 128, 1500);      // inside the second (forward) pass
    r.p.stopImmediate();
    r.p.setLoop (LoopPattern::Once);
    r.p.setSource (r.clip.get(), { 0, 1000 });
    r.p.play (true);
    const auto o = renderPlayer (r.p, 1, 128, 10);
    CHECK_EQ (o[0][0], 1000.0f);           // starts backward, as the Direction setting says
    CHECK (r.p.direction() == Direction::Backward);

    // a restart (Replay) in the middle of a flipped pass also goes back to the user's direction
    PlayerRig q (1000);
    q.p.setDirection (Direction::Backward);
    q.p.setLoop (LoopPattern::PingPong);
    q.p.play (true);
    renderPlayer (q.p, 1, 128, 1500);
    q.p.play (true);
    const auto o2 = renderPlayer (q.p, 1, 128, 600);
    // the restart lands after the declick ramp; from there the ramp must run downwards again (backward)
    CHECK (o2[0][400] < o2[0][300]);
    CHECK (o2[0][500] < o2[0][400]);
    CHECK (q.p.direction() == Direction::Backward);
}

RB_TEST (audit_direction_change_requested_at_the_end_of_a_pass_is_not_lost)
{
    PlayerRig r (10000);
    r.p.setDirection (Direction::Forward);
    r.p.play (true);
    renderPlayer (r.p, 1, 256, 9950);
    r.p.setDirection (Direction::Backward);    // lands inside the last declick ramp
    renderPlayer (r.p, 1, 256);
    r.p.play (true);
    const auto o = renderPlayer (r.p, 1, 256, 100);
    CHECK_EQ (o[0][0], 10000.0f);              // backward from the end

    PlayerRig q (10000);
    q.p.setDirection (Direction::Forward);
    q.p.play (true);
    renderPlayer (q.p, 1, 256, 9950);
    q.p.play (true);                           // Replay right at the end: must restart, not vanish
    const auto o2 = renderPlayer (q.p, 1, 256, 20000);
    CHECK (o2[0].size() > 5000);
}

RB_TEST (audit_selection_change_while_playing_keeps_playing_without_a_click)
{
    Rig r;
    Settings s = r.s;
    s.mode = Mode::File;
    s.direction = Direction::Backward;
    s.exactSamples = false;
    r.apply (s);
    Command c;
    c.type = CommandType::SetFile;
    c.source = makeSineClip (48000.0, 96000, 440.0, 0.5);
    c.a = 0;
    c.b = 80000;
    r.send (std::move (c));
    r.cmd (CommandType::PlayFile, true);
    r.run (frames (0.5), 128);
    r.cmd (CommandType::SetSelection, false, 0, 90000);   // drag the end handle while playing
    r.run (frames (0.5), 128);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().filePlaying), 1);
    float worstStep = 0.0f;
    for (std::size_t i = 1; i < r.out[0].size(); ++i)
        worstStep = std::max (worstStep, std::abs (r.out[0][i] - r.out[0][i - 1]));
    CHECK (worstStep < 0.08f);                              // a 440 Hz sine at 0.5 steps by at most ~0.03
    CHECK (rms (r.out[0], frames (0.6), frames (0.9)) > 0.2);
}

// ---- live transport --------------------------------------------------------------------------
namespace
{
void setupLive (Rig& r, bool exact)
{
    Settings s = r.s;
    s.mode = Mode::Live;
    s.liveChunkSeconds = 0.5;
    s.liveDelaySeconds = 0.0;
    s.liveFadeMs = 2.0;
    s.exactSamples = exact;
    r.apply (s);
    r.input = [] (Frame) { return 0.5f; };
}

float worstStep (const std::vector<float>& v, std::size_t from, std::size_t to = static_cast<std::size_t> (-1))
{
    float w = 0.0f;
    for (std::size_t i = std::max<std::size_t> (1, from); i < std::min (v.size(), to); ++i)
        w = std::max (w, std::abs (v[i] - v[i - 1]));
    return w;
}
}  // namespace

RB_TEST (audit_live_stop_inside_the_chunk_fade_does_not_click)
{
    for (Frame offset : { Frame { 20 }, Frame { 24000 - 20 } })
    {
        Rig r;
        setupLive (r, false);
        r.startLive();
        r.run (24000 + offset, 64);        // first output starts at W + D = 24000
        r.cmd (CommandType::Stop);
        r.run (2000, 64);
        CHECK (worstStep (r.out[0], 1) < 0.02f);
        CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Ready));
    }
}

RB_TEST (audit_live_fade_change_waits_for_the_next_chunk_boundary)
{
    Rig r;
    setupLive (r, false);
    r.startLive();
    r.run (24000 + 24000 - 50, 50);        // 50 frames before the end of the first chunk (inside its fade-out)
    const std::size_t at = r.out[0].size();
    Settings s = r.s;
    s.exactSamples = true;                 // switch the fades off in the middle of the chunk
    r.apply (s);
    r.run (500, 50);
    // the rest of the current chunk keeps its fade-out; the new setting starts with the next chunk
    CHECK (worstStep (r.out[0], at - 20, at + 49) < 0.02f);
}
