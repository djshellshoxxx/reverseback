// A01, A10 (core part), A11, A12: exact reversal, selection mapping, speed lengths, fades, loops.
#include "TestUtil.h"

using namespace rbt;

namespace
{
ClipPlayer makePlayer (const ClipSource& clip, Selection sel, Direction dir, double speed = 1.0, double outRate = 0.0,
                       Frame fade = 0, LoopPattern loop = LoopPattern::Once, std::size_t maxBlock = 1024)
{
    ClipPlayer p;
    p.configure (maxBlock, 16.0);
    p.setOutputRate (outRate > 0.0 ? outRate : clip.sampleRate());
    p.setSource (&clip, sel);
    p.setDirection (dir);
    p.setSpeed (speed);
    p.setFadeFrames (fade);
    p.setLoop (loop);
    return p;
}
}  // namespace

RB_TEST (A01_reverse_1234)
{
    auto clip = makeClip (48000.0, { { 1, 2, 3, 4 } });
    auto p = makePlayer (*clip, { 0, 4 }, Direction::Backward);
    p.play (true);
    auto out = renderPlayer (p, 2, 16);
    CHECK_EQ (out[0].size(), std::size_t { 4 });
    const float expect[4] = { 4, 3, 2, 1 };
    for (int i = 0; i < 4; ++i)
        CHECK_EQ (out[0][static_cast<std::size_t> (i)], expect[i]);
    // mono feeds both channels equally
    CHECK (out[0] == out[1]);
}

RB_TEST (A01_stereo_channels_keep_identity)
{
    auto clip = makeClip (48000.0, { { 1, 2, 3, 4, 5 }, { -10, -20, -30, -40, -50 } });
    auto p = makePlayer (*clip, { 0, 5 }, Direction::Backward);
    p.play (true);
    auto out = renderPlayer (p, 2, 3);
    CHECK_EQ (out[0].size(), std::size_t { 5 });
    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK_EQ (out[0][i], static_cast<float> (5 - i));
        CHECK_EQ (out[1][i], -10.0f * static_cast<float> (5 - i));
    }
}

RB_TEST (forward_copy_and_selection_subrange)
{
    auto clip = makeRampClip (44100.0, 1000);
    auto p = makePlayer (*clip, { 100, 300 }, Direction::Forward);
    p.play (true);
    auto out = renderPlayer (p, 1, 64);
    CHECK_EQ (out[0].size(), std::size_t { 200 });
    CHECK_EQ (out[0].front(), 101.0f);
    CHECK_EQ (out[0].back(), 300.0f);

    auto q = makePlayer (*clip, { 100, 300 }, Direction::Backward);
    q.play (true);
    auto rev = renderPlayer (q, 1, 77);
    CHECK_EQ (rev[0].size(), std::size_t { 200 });
    CHECK_EQ (rev[0].front(), 300.0f);   // original last selected sample plays first
    CHECK_EQ (rev[0].back(), 101.0f);
}

RB_TEST (A11_double_reversal_reconstructs_original)
{
    auto clip = makeNoiseClip (48000.0, 5001, 2);
    auto p = makePlayer (*clip, { 0, 5001 }, Direction::Backward);
    p.play (true);
    auto once = renderPlayer (p, 2, 512);
    auto mid = makeClip (48000.0, { once[0], once[1] });
    auto q = makePlayer (*mid, { 0, 5001 }, Direction::Backward);
    q.play (true);
    auto twice = renderPlayer (q, 2, 333);
    CHECK (twice[0] == std::vector<float> (clip->channel (0), clip->channel (0) + 5001));
    CHECK (twice[1] == std::vector<float> (clip->channel (1), clip->channel (1) + 5001));
}

RB_TEST (block_size_independence_exact_and_resampled)
{
    auto clip = makeNoiseClip (48000.0, 9000, 2, 7);
    for (double speed : { 1.0, 0.75, 1.5 })
    {
        std::vector<Planar> results;
        for (std::size_t block : { 1u, 64u, 127u, 256u, 1024u })
        {
            auto p = makePlayer (*clip, { 10, 8900 }, Direction::Backward, speed, 0.0, 0, LoopPattern::Once, 1024);
            p.play (true);
            results.push_back (renderPlayer (p, 2, block));
        }
        for (std::size_t k = 1; k < results.size(); ++k)
        {
            CHECK_EQ (results[k][0].size(), results[0][0].size());
            CHECK (results[k][0] == results[0][0]);
            CHECK (results[k][1] == results[0][1]);
        }
    }
}

RB_TEST (A10_output_length_is_round_N_over_ratio)
{
    struct Case { double srcRate, outRate, speed; Frame sel; };
    const Case cases[] = { { 48000, 48000, 0.5, 10001 }, { 48000, 48000, 2.0, 10001 }, { 44100, 48000, 1.0, 44100 },
                           { 96000, 48000, 1.0, 96000 }, { 192000, 44100, 1.0, 192000 }, { 48000, 48000, 1.5, 12345 },
                           { 44100, 48000, 0.75, 44101 } };
    for (const auto& c : cases)
    {
        auto clip = makeNoiseClip (c.srcRate, c.sel + 50, 1, 3);
        auto p = makePlayer (*clip, { 25, 25 + c.sel }, Direction::Backward, c.speed, c.outRate);
        p.play (true);
        auto out = renderPlayer (p, 1, 997);
        const double ratio = c.srcRate / c.outRate * c.speed;
        const auto expect = static_cast<std::size_t> (std::llround (static_cast<double> (c.sel) / ratio));
        CHECK_EQ (out[0].size(), expect);
    }
}

RB_TEST (A12_fades_keep_duration_and_shape_edges)
{
    auto clip = makeClip (48000.0, { std::vector<float> (2000, 1.0f) });
    auto p = makePlayer (*clip, { 0, 2000 }, Direction::Backward, 1.0, 0.0, 144);
    p.play (true);
    auto out = renderPlayer (p, 1, 300);
    CHECK_EQ (out[0].size(), std::size_t { 2000 });
    CHECK (out[0][0] > 0.0f && out[0][0] < 0.01f);
    CHECK (out[0][1999] > 0.0f && out[0][1999] < 0.01f);
    CHECK_NEAR (out[0][1000], 1.0, 1e-6);
    for (std::size_t i = 1; i < 144; ++i)
        CHECK (out[0][i] >= out[0][i - 1]);
}

RB_TEST (A12_tiny_selection_and_fade_larger_than_clip)
{
    auto clip = makeClip (48000.0, { std::vector<float> (5000, 0.5f) });
    auto p = makePlayer (*clip, { 100, 105 }, Direction::Forward, 1.0, 0.0, 480);
    p.play (true);
    auto out = renderPlayer (p, 1, 64);
    CHECK_EQ (out[0].size(), std::size_t { 5 });
    for (float v : out[0])
        CHECK (std::isfinite (v) && v <= 0.5f);
}

RB_TEST (loop_repeats_and_pingpong_alternates)
{
    auto clip = makeClip (48000.0, { { 1, 2, 3, 4 } });
    auto loop = makePlayer (*clip, { 0, 4 }, Direction::Forward, 1.0, 0.0, 0, LoopPattern::Loop);
    loop.play (true);
    auto out = renderPlayer (loop, 1, 5, 12);
    const std::vector<float> expectLoop { 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4 };
    CHECK (out[0] == expectLoop);

    auto pp = makePlayer (*clip, { 0, 4 }, Direction::Forward, 1.0, 0.0, 0, LoopPattern::PingPong);
    pp.play (true);
    auto o2 = renderPlayer (pp, 1, 3, 16);
    const std::vector<float> expectPing { 1, 2, 3, 4, 4, 3, 2, 1, 1, 2, 3, 4, 4, 3, 2, 1 };
    CHECK (o2[0] == expectPing);
}

RB_TEST (direction_flip_mid_play_is_continuous_and_declicked)
{
    auto clip = makeRampClip (48000.0, 4000);
    auto p = makePlayer (*clip, { 0, 4000 }, Direction::Forward, 1.0, 0.0, 0);
    p.setDeclickFrames (96);
    p.play (true);
    std::vector<float> a (1000, 0.0f);
    float* ptr[1] = { a.data() };
    CHECK_EQ (p.process (ptr, 1, 1000), std::size_t { 1000 });
    p.setDirection (Direction::Backward);
    std::vector<float> b (3000, 0.0f);
    float* pb[1] = { b.data() };
    const std::size_t got = p.process (pb, 1, 3000);
    CHECK (got > 900 && got < 1300);   // about 1000 remaining frames plus the fade-out/in region
    // gain reaches zero somewhere in the first declick frames, then recovers
    float minGain = 1.0f;
    for (std::size_t i = 0; i < 300; ++i)
        minGain = std::min (minGain, std::abs (b[i]) / std::max (1.0f, std::abs (b[i])));
    CHECK (b[0] > 900.0f);                      // continues from where it was (frame ~1000)
    CHECK (std::abs (b[95]) < std::abs (b[0]));  // faded toward zero
}

RB_TEST (stop_fades_to_silence_within_stop_fade_and_keeps_position)
{
    auto clip = makeClip (48000.0, { std::vector<float> (48000, 1.0f) });
    auto p = makePlayer (*clip, { 0, 48000 }, Direction::Forward);
    p.setStopFadeFrames (480);
    p.play (true);
    std::vector<float> a (1000, 0.0f);
    float* ptr[1] = { a.data() };
    p.process (ptr, 1, 1000);
    p.stop();
    std::vector<float> b (2000, 0.0f);
    float* pb[1] = { b.data() };
    const std::size_t got = p.process (pb, 1, 2000);
    CHECK (got <= 481 && got >= 478);
    CHECK (! p.isActive());
    CHECK_NEAR (b[got - 1], 0.0, 0.01);
    CHECK_NEAR (p.positionNorm(), (1000.0 + static_cast<double> (got)) / 48000.0, 1e-6);
    // play() resumes from the stored position, play(true) restarts
    p.play (false);
    CHECK (p.positionNorm() > 0.02f);
    p.stopImmediate();
    p.play (true);
    CHECK_NEAR (p.positionNorm(), 0.0, 1e-9);
}

RB_TEST (speed_change_midplay_rebases_without_jump)
{
    auto clip = makeRampClip (48000.0, 48000);
    auto p = makePlayer (*clip, { 0, 48000 }, Direction::Forward);
    p.play (true);
    std::vector<float> a (2000, 0.0f);
    float* ptr[1] = { a.data() };
    p.process (ptr, 1, 2000);
    p.setSpeed (2.0);
    std::vector<float> b (500, 0.0f);
    float* pb[1] = { b.data() };
    p.process (pb, 1, 500);
    // after the declick, ramp slope must be ~2 samples per frame (bandlimited ramp)
    CHECK_NEAR (b[400] - b[399], 2.0, 0.2);
    CHECK_NEAR (p.ratio(), 2.0, 1e-12);
}

RB_TEST (allocation_free_process)
{
    auto clip = makeNoiseClip (48000.0, 20000, 2);
    auto p = makePlayer (*clip, { 0, 20000 }, Direction::Backward, 1.3, 0.0, 144, LoopPattern::PingPong);
    p.play (true);
    std::vector<float> a (512), b (512);
    float* ptr[2] = { a.data(), b.data() };
    CHECK_NO_ALLOC (for (int i = 0; i < 100; ++i) p.process (ptr, 2, 512));
    CHECK_NO_ALLOC (p.setDirection (Direction::Forward));
    CHECK_NO_ALLOC (p.setSpeed (0.7));
    CHECK_NO_ALLOC (for (int i = 0; i < 20; ++i) p.process (ptr, 2, 512));
    CHECK_NO_ALLOC (p.stop());
}
