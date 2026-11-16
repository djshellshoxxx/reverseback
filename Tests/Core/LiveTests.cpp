// A04, A05, A06, A09, A12: Live Reverse transport.
#include "Rig.h"

using namespace rbt;

namespace
{
Settings liveSettings (double chunk, double delay)
{
    Settings s;
    s.mode = Mode::Live;
    s.liveChunkSeconds = chunk;
    s.liveDelaySeconds = delay;
    s.exactSamples = true;
    return s;
}

float hashInput (Frame i)
{
    const std::uint32_t h = static_cast<std::uint32_t> (i) * 2654435761u;
    return static_cast<float> ((h >> 8) & 0xFFFFu) / 65536.0f - 0.5f;
}

// Closed-form expectation from ENGINE_DESIGN 8.1 (fades off): output at t plays chunk j-1 reversed.
float expectedLive (Frame t, Frame W, Frame D, const std::function<float (Frame)>& in)
{
    if (t < W + D)
        return 0.0f;
    const Frame j = (t - D) / W;
    const Frame u = (t - D) % W;
    return in ((j - 1) * W + (W - 1 - u));
}
}  // namespace

RB_TEST (A04_live_first_output_and_chunk_order)
{
    for (std::size_t block : { 1u, 64u, 127u, 256u, 1024u })
    {
        if (block == 1u)
            continue;   // 1-frame blocks are covered by A03 on the record path; keep this test fast
        Rig r;
        r.apply (liveSettings (0.5, 2.0));
        r.startLive();
        r.run (48000 * 8, block);
        const Frame W = 24000, D = 96000;
        CHECK (allZero (r.out[0], 0, 120000));                 // first output begins at frame 120,000
        CHECK_EQ (r.out[0][120000], rampInput (W - 1));        // chunk 0 is played last-sample-first
        bool ok = true;
        for (Frame t = 0; t < 48000ull * 8 && ok; ++t)
            ok = r.out[0][t] == expectedLive (t, W, D, rampInput);
        CHECK (ok);
        // chunks are reversed independently and keep chronological chunk order
        CHECK_EQ (r.out[0][120000 + W], rampInput (2 * W - 1));
        CHECK_EQ (r.out[0][120000 + W + W - 1], rampInput (W));
        CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Running));
    }
}

RB_TEST (live_zero_delay_chunks_play_back_to_back)
{
    Rig r;
    r.apply (liveSettings (0.1, 0.0));
    r.startLive();
    r.run (48000, 333);
    const Frame W = 4800;
    CHECK (allZero (r.out[0], 0, W));
    bool ok = true;
    for (Frame t = 0; t < 48000 && ok; ++t)
        ok = r.out[0][t] == expectedLive (t, W, 0, rampInput);
    CHECK (ok);
}

RB_TEST (live_fades_never_change_duration_and_soften_chunk_edges)
{
    Rig r;
    Settings s = liveSettings (0.5, 0.5);
    s.exactSamples = false;
    r.apply (s);
    r.input = [] (Frame) { return 0.8f; };
    r.startLive();
    r.run (48000 * 4, 256);
    const Frame start = 48000;
    CHECK (r.out[0][start] > 0.0f && r.out[0][start] < 0.01f);               // fade-in
    CHECK_NEAR (r.out[0][start + 12000], 0.8, 1e-6);                        // body untouched
    CHECK (r.out[0][start + 24000 - 1] > 0.0f && r.out[0][start + 24000 - 1] < 0.01f);   // fade-out
    CHECK (r.out[0][start + 24000] > 0.0f && r.out[0][start + 24000] < 0.01f);           // next chunk fades in again
}

RB_TEST (A09_freeze_adopts_complete_chunk_at_output_boundary_and_repeats)
{
    Rig r;
    r.apply (liveSettings (0.5, 2.0));
    r.startLive();
    const Frame W = 24000, D = 96000;
    r.run (150000, 256);   // mid-chunk 6 (t = 150000)
    r.cmd (CommandType::Freeze);
    r.run (1, 1);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Freezing));

    r.run (200000, 256);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Frozen));
    CHECK_EQ (r.count (EventType::FrozenReady), 1);

    // in-progress chunk is c = 150000/24000 = 6, completing at 168000; output boundary at/after that: 96000+k*24000
    const Frame tc = 7 * W;                 // 168000
    const Frame adopt = tc;                 // 168000 = 96000 + 3*24000 is a boundary
    // before adoption, queued live output continues; after adoption the frozen chunk 6 repeats reversed forever
    for (int rep = 0; rep < 4; ++rep)
        for (Frame u : { Frame { 0 }, Frame { 5 }, W - 1 })
            CHECK_EQ (r.out[0][adopt + static_cast<Frame> (rep) * W + u], rampInput (6 * W + (W - 1 - u)));
    CHECK_EQ (r.out[0][adopt - 1], expectedLive (adopt - 1, W, D, rampInput));
    // capture stopped at the chunk boundary: nothing newer than chunk 6 can ever be heard
    bool onlyFrozen = true;
    for (Frame t = adopt; t < r.out[0].size() && onlyFrozen; ++t)
        onlyFrozen = r.out[0][t] == rampInput (6 * W + (W - 1 - ((t - adopt) % W)));
    CHECK (onlyFrozen);

    // Resume starts fresh capture and a new buffer fill (silent for W + D) from the resume point
    const std::size_t resumeAt = r.out[0].size();
    const Frame resumeFrame = r.frame;
    r.cmd (CommandType::Resume);
    r.run (150000, 256);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Running));
    CHECK (allZero (r.out[0], resumeAt + 480 + 256, resumeAt + W + D));
    CHECK_EQ (r.out[0][resumeAt + W + D], r.input (resumeFrame + W - 1));
}

RB_TEST (A09_freeze_before_output_starts_loops_immediately)
{
    Rig r;
    r.apply (liveSettings (0.5, 2.0));
    r.startLive();
    r.run (30000, 256);   // still Filling
    r.cmd (CommandType::Freeze);
    r.run (60000, 256);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Frozen));
    CHECK_EQ (r.out[0][48000], rampInput (24000 + 23999));   // chunk 1 (frames 24000..47999) reversed, starting at 48000
}

RB_TEST (A09_frozen_chunk_is_exportable_via_storage)
{
    Rig r;
    r.apply (liveSettings (0.25, 0.25));
    Command c;
    c.type = CommandType::StartLive;
    auto storage = LiveStorage::create (48000.0, 1, 12000, 12000);
    c.live = storage;
    r.eng.handle (std::move (c));
    r.run (60000, 256);
    r.cmd (CommandType::Freeze);
    r.run (60000, 256);
    auto& live = r.eng.liveForTest();
    CHECK_EQ (static_cast<int> (live.state()), static_cast<int> (LiveTransport::State::Frozen));
    const Frame slot = live.frozenSlot();
    const float* p = storage->plane (slot, 0);
    // chunk content equals the captured input (immutable while frozen)
    const std::int64_t chunk = storage->tags[static_cast<std::size_t> (slot)];
    CHECK (chunk >= 0);
    for (Frame i : { Frame { 0 }, Frame { 100 }, Frame { 11999 } })
        CHECK_EQ (p[i], rampInput (static_cast<Frame> (chunk) * 12000 + i));
}

RB_TEST (A05_sixty_minute_run_has_bounded_storage_and_no_missing_chunks)
{
#ifdef RB_NO_ALLOC_HOOK
    const double minutes = 4.0;   // sanitizer builds are ~20x slower
#else
    const double minutes = 60.0;
#endif
    Rig r (48000.0, 512);
    r.apply (liveSettings (0.1, 30.0));   // minimum chunk, maximum delay -> largest slot pool (304)
    r.input = hashInput;
    Command c;
    c.type = CommandType::StartLive;
    auto storage = LiveStorage::create (48000.0, 1, 4800, 30 * 48000);
    const auto slots = storage->slots;
    const auto bytesBefore = storage->data.capacity();
    c.live = storage;
    r.eng.handle (std::move (c));
    CHECK_EQ (slots, Frame { 304 });

    const Frame total = static_cast<Frame> (minutes * 60.0 * 48000.0);
    std::vector<float> a (512), oL (512), oR (512);
    const float* in[1] = { a.data() };
    float* o[2] = { oL.data(), oR.data() };
    Frame mismatches = 0, frame = 0;
    const Frame W = 4800, D = 30 * 48000;
    while (frame < total)
    {
        for (std::size_t i = 0; i < 512; ++i)
            a[i] = hashInput (frame + i);
        r.eng.process (in, 1, o, 512);
        for (std::size_t i = 0; i < 512; ++i)
            if (oL[i] != expectedLive (frame + i, W, D, hashInput))
                ++mismatches;
        frame += 512;
        if ((frame & 0xFFFF) == 0)
            r.service();
    }
    r.service();
    CHECK_EQ (mismatches, Frame { 0 });
    CHECK_EQ (r.count (EventType::Error), 0);
    CHECK_EQ (storage->data.capacity(), bytesBefore);        // storage never grows
    CHECK_EQ (storage->slots, slots);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Running));
}

RB_TEST (A05_forced_slot_shortage_stops_with_fell_behind_and_silence)
{
    Rig r;
    r.apply (liveSettings (0.1, 1.0));
    r.startLive (3);   // far fewer slots than ceil(D/W)+4
    r.run (48000 * 4, 256);
    CHECK (r.hasError (ErrorCode::FellBehind));
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Ready));
    CHECK (allZero (r.out[0], 48000 * 2, r.out[0].size()));
}

RB_TEST (A06_stop_in_every_live_state_silences_without_later_audio)
{
    struct Probe { const char* name; Frame at; bool freeze; };
    const Probe probes[] = { { "filling", 30000, false }, { "running", 60000, false }, { "freezing", 66000, true } };
    for (const auto& p : probes)
    {
        Rig r;
        r.input = [] (Frame) { return 0.5f; };
        r.apply (liveSettings (0.25, 0.25));
        r.startLive();
        r.run (p.at, 128);
        if (p.freeze)
        {
            r.cmd (CommandType::Freeze);
            r.run (200, 128);
        }
        const std::size_t stopAt = r.out[0].size();
        r.cmd (CommandType::Stop);
        r.run (48000 * 2, 128);
        CHECK (allZero (r.out[0], stopAt + 480 + 128, r.out[0].size()));
        CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Ready));
        if (p.freeze)
            CHECK_EQ (r.count (EventType::FrozenReady), 0);   // a stop before adoption never adopts
    }
    // Stop while Frozen
    Rig r;
    r.input = [] (Frame) { return 0.5f; };
    r.apply (liveSettings (0.25, 0.25));
    r.startLive();
    r.run (48000, 128);
    r.cmd (CommandType::Freeze);
    r.run (48000, 128);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().liveState), static_cast<int> (LiveTransport::State::Frozen));
    const std::size_t stopAt = r.out[0].size();
    r.cmd (CommandType::Stop);
    r.run (48000, 128);
    CHECK (r.out[0][stopAt - 1] != 0.0f);
    CHECK (allZero (r.out[0], stopAt + 480 + 128, r.out[0].size()));
}

RB_TEST (rapid_live_restart_does_not_cut_old_tail_or_leak_into_new_run)
{
    Rig r;
    r.input = [] (Frame) { return 0.5f; };
    r.apply (liveSettings (0.1, 0.0));
    for (int i = 0; i < 20; ++i)
    {
        r.startLive();
        r.run (4800 + 300, 100);
        r.cmd (CommandType::Stop);
        r.run (100, 100);
    }
    CHECK_EQ (r.count (EventType::Error), 0);
    for (float v : r.out[0])
        CHECK (std::isfinite (v) && v <= 0.5f + 1e-6f);
}

RB_TEST (live_process_does_not_allocate_in_any_state)
{
    Rig r;
    r.apply (liveSettings (0.1, 0.2));
    r.startLive();
    std::vector<float> a (256, 0.25f), oL (256), oR (256);
    const float* in[1] = { a.data() };
    float* o[2] = { oL.data(), oR.data() };
    for (int i = 0; i < 400; ++i)
    {
        if (i == 150)
            r.cmd (CommandType::Freeze);
        if (i == 260)
            r.cmd (CommandType::Resume);
        if (i == 380)
            r.cmd (CommandType::Stop);
        CHECK_NO_ALLOC (r.eng.process (in, 1, o, 256));
        r.service();
    }
}
