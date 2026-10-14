// A15: queue/seqlock correctness, threaded control/audio stress and randomised command fuzzing.
#include "Presets.h"
#include "Rig.h"

#include <atomic>
#include <thread>

using namespace rbt;

RB_TEST (spsc_queue_preserves_order_across_threads)
{
    SpscQueue<std::uint64_t, 1024> q;
    constexpr std::uint64_t kCount = 400000;
    std::thread producer ([&]
    {
        for (std::uint64_t i = 0; i < kCount;)
        {
            std::uint64_t v = i;
            if (q.push (std::move (v)))
                ++i;
        }
    });
    std::uint64_t expect = 0;
    bool ordered = true;
    while (expect < kCount)
    {
        std::uint64_t v;
        if (q.pop (v))
        {
            ordered = ordered && v == expect;
            ++expect;
        }
    }
    producer.join();
    CHECK (ordered);
}

RB_TEST (seqlock_readers_never_see_torn_snapshots)
{
    struct Payload { std::uint64_t a, b, c, d, e; };
    SeqLock<Payload> cell;
    cell.store ({ 0, 0, 0, 0, 0 });
    std::atomic<bool> stop { false };
    std::atomic<long> torn { 0 };
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i)
        readers.emplace_back ([&]
        {
            while (! stop.load())
            {
                const Payload p = cell.load();
                if (p.a != p.b || p.b != p.c || p.c != p.d || p.d != p.e)
                    torn.fetch_add (1);
            }
        });
    for (std::uint64_t i = 1; i < 300000; ++i)
        cell.store ({ i, i, i, i, i });
    stop = true;
    for (auto& t : readers)
        t.join();
    CHECK_EQ (torn.load(), 0L);
}

RB_TEST (A15_threaded_control_and_audio_with_rapid_asset_replacement)
{
    Engine eng;
    eng.prepare (48000.0, 256);
    CommandQueue<Command, 256> commands;
    SeqLock<Settings> settingsCell;
    Settings init;
    init.exactSamples = false;
    settingsCell.store (init);
    std::atomic<bool> stop { false };
    std::atomic<long> finite { 1 };
    std::atomic<std::uint64_t> blocks { 0 };

    std::thread audio ([&]
    {
        std::vector<float> in (256, 0.2f), oL (256), oR (256);
        const float* inp[1] = { in.data() };
        float* o[2] = { oL.data(), oR.data() };
        while (! stop.load())
        {
            eng.applySettings (settingsCell.load());
            Command c;
            while (commands.pop (c))
                eng.handle (std::move (c));
            eng.process (inp, 1, o, 256);
            for (std::size_t i = 0; i < 256; ++i)
                if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i]))
                    finite.store (0);
            blocks.fetch_add (1);
            if ((blocks.load() & 0x3F) == 0)
                std::this_thread::yield();
        }
    });

    SimpleRng rng (99);
    Settings s = init;
    int rejected = 0;
    for (int i = 0; i < 4000; ++i)
    {
        Command c;
        switch (rng.below (9))
        {
            case 0: s.mode = Mode::Record; break;
            case 1: s.mode = Mode::Live; s.liveChunkSeconds = 0.1; s.liveDelaySeconds = 0.2; break;
            case 2: s.mode = Mode::File; break;
            case 3:
                c.type = CommandType::SetFile;
                c.source = makeNoiseClip (44100.0 + 4000.0 * rng.below (3), 3000 + rng.below (30000), 1 + static_cast<int> (rng.below (2)));
                c.a = 0;
                c.b = c.source->frameCount();
                break;
            case 4: c.type = CommandType::PlayFile; c.flag = rng.below (2) == 0; break;
            case 5:
                c.type = CommandType::StartRecord;
                s.captureSeconds = 0.3;
                s.waitSeconds = 0.05;
                c.take = AudioClip::create (48000.0, 1, framesFor (0.3, 48000.0));
                break;
            case 6:
                c.type = CommandType::StartLive;
                c.live = LiveStorage::create (48000.0, 1, 4800, 9600);
                break;
            case 7: c.type = rng.below (2) ? CommandType::Freeze : CommandType::Resume; break;
            default: c.type = CommandType::Stop; break;
        }
        s.speed = 0.5 + 0.1 * static_cast<double> (rng.below (16));
        s.direction = rng.below (2) ? Direction::Forward : Direction::Backward;
        s.loop = static_cast<LoopPattern> (rng.below (3));
        settingsCell.store (s);
        if (c.type != CommandType::Stop || rng.below (3) == 0)
            if (! commands.push (std::move (c)))
                ++rejected;

        Event e;
        while (eng.events().poll (e))
            e = Event {};
        eng.events().drainRetired();
        (void) eng.snapshot();
        if ((i & 15) == 0)
            std::this_thread::yield();
    }
    stop = true;
    audio.join();
    CHECK (finite.load() == 1);
    CHECK (blocks.load() > 100);
    CHECK_EQ (eng.events().retireOverflow(), std::uint32_t { 0 });
}

RB_TEST (A15_random_command_fuzz_keeps_output_finite_and_never_allocates_on_audio_path)
{
    Rig r (48000.0, 512);
    r.input = [] (Frame i) { return 0.3f * static_cast<float> (std::sin (0.03 * static_cast<double> (i))); };
    r.autoSpare = true;
    SimpleRng rng (2024);
    Settings s = r.s;
    s.exactSamples = false;
    s.captureSeconds = 0.4;
    s.waitSeconds = 0.1;
    s.liveChunkSeconds = 0.1;
    s.liveDelaySeconds = 0.1;
    r.apply (s);
    auto clip = makeNoiseClip (48000.0, 30000, 2);
    for (int step = 0; step < 3000; ++step)
    {
        switch (rng.below (14))
        {
            case 0: s.mode = Mode::Record; r.apply (s); break;
            case 1: s.mode = Mode::Live; r.apply (s); break;
            case 2: s.mode = Mode::File; r.apply (s); break;
            case 3: r.startRecord (rng.below (2) == 0); break;
            case 4: r.startLive(); break;
            case 5: r.cmd (CommandType::Freeze); break;
            case 6: r.cmd (CommandType::Resume); break;
            case 7: r.cmd (CommandType::Stop); break;
            case 8: r.cmd (CommandType::ReleaseHold); break;
            case 9: r.cmd (CommandType::FinishEarly); break;
            case 10: r.cmd (CommandType::Replay); break;
            case 11: { Command c; c.type = CommandType::SetFile; c.source = clip; c.b = clip->frameCount(); r.send (std::move (c)); break; }
            case 12: r.cmd (CommandType::PlayFile, rng.below (2) == 0); break;
            default:
                s.speed = 0.5 + 0.1 * static_cast<double> (rng.below (16));
                s.direction = rng.below (2) ? Direction::Forward : Direction::Backward;
                s.loop = static_cast<LoopPattern> (rng.below (3));
                s.autoStart = rng.below (4) == 0;
                s.repeatSession = rng.below (3) == 0;
                s.countdownSeconds = rng.below (5) == 0 ? 0.05 : 0.0;
                r.apply (s);
                break;
        }
        const std::size_t block = 1 + rng.below (700);
        const std::size_t before = r.out[0].size();
        r.run (block, 1 + rng.below (300));
        for (std::size_t i = before; i < r.out[0].size(); ++i)
            if (! std::isfinite (r.out[0][i]) || std::abs (r.out[0][i]) > 2.0f)
            {
                CHECK (false);
                return;
            }
        if (r.out[0].size() > 4000000)
        {
            r.out[0].clear();
            r.out[1].clear();
        }
    }
    CHECK (true);
}
