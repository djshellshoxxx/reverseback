// A17 (presets), A19 (trim), input/output stage behaviour, limiter guarantees, offline render.
#include "OfflineRender.h"
#include "Presets.h"
#include "Rig.h"
#include "Stages.h"

using namespace rbt;

RB_TEST (gain_smoother_ramps_linearly_over_20ms)
{
    GainSmoother g;
    g.reset (1.0f);
    g.setTarget (2.0f, 960);
    float prev = 1.0f;
    for (int i = 0; i < 960; ++i)
    {
        const float v = g.next();
        CHECK (v > prev);
        prev = v;
    }
    CHECK_EQ (prev, 2.0f);
    CHECK_EQ (g.next(), 2.0f);
}

RB_TEST (input_stage_channel_mapping_gain_and_sanitising)
{
    InputStage st;
    st.prepare (48000.0, 256);
    std::vector<float> l (256, 0.5f), r (256, -0.25f);
    const float* host[2] = { l.data(), r.data() };

    st.setMode (InputChannelMode::Input1);
    CHECK_EQ (st.process (host, 2, 256)[0][10], 0.5f);
    st.setMode (InputChannelMode::Input2);
    CHECK_EQ (st.process (host, 2, 256)[0][10], -0.25f);
    st.setMode (InputChannelMode::Mix);
    CHECK_EQ (st.process (host, 2, 256)[0][10], 0.125f);
    st.setMode (InputChannelMode::Stereo);
    auto m = st.process (host, 2, 256);
    CHECK_EQ (m[0][10], 0.5f);
    CHECK_EQ (m[1][10], -0.25f);
    CHECK_EQ (st.outputChannels(), 2);

    // mono host: Input 2 and Stereo fall back to the only channel
    const float* mono[1] = { l.data() };
    st.setMode (InputChannelMode::Input2);
    CHECK_EQ (st.process (mono, 1, 256)[0][10], 0.5f);

    // gain change is smoothed over 20 ms, not applied as a step
    st.setMode (InputChannelMode::Input1);
    st.setGainDb (6.0206f);
    std::vector<float> one (2000, 1.0f);
    const float* h1[1] = { one.data() };
    InputStage g;
    g.prepare (48000.0, 2000);
    g.setMode (InputChannelMode::Input1);
    g.setGainDb (6.0206f);
    auto out = g.process (h1, 1, 2000);
    CHECK_NEAR (out[0][0], 1.0, 0.01);
    CHECK (out[0][480] > 1.4f && out[0][480] < 1.6f);
    CHECK_NEAR (out[0][1000], 2.0, 0.001);

    // non-finite samples become silence and are counted
    l[3] = std::numeric_limits<float>::quiet_NaN();
    l[4] = std::numeric_limits<float>::infinity();
    InputStage s2;
    s2.prepare (48000.0, 256);
    auto o2 = s2.process (host, 2, 256);
    CHECK_EQ (o2[0][3], 0.0f);
    CHECK_EQ (o2[0][4], 0.0f);
    CHECK_EQ (s2.sanitizedCount(), std::uint32_t { 2 });
}

RB_TEST (limiter_holds_ceiling_is_transparent_below_it_and_has_stated_latency)
{
    OutputLimiter lim;
    lim.prepare (48000.0, 2);
    CHECK_EQ (lim.latencyFrames(), std::size_t { 48 });

    // below the ceiling: bit-exact, delayed by the latency, no make-up gain
    std::vector<float> a (4000), b (4000);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        a[i] = 0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
        b[i] = -a[i];
    }
    std::vector<float> a0 = a;
    float* io[2] = { a.data(), b.data() };
    lim.process (io, 2, a.size());
    for (std::size_t i = 48; i < a.size(); ++i)
        CHECK_EQ (a[i], a0[i - 48]);

    // loud: never exceeds the -1 dBFS ceiling
    lim.reset();
    std::vector<float> loud (48000), loud2 (48000);
    for (std::size_t i = 0; i < loud.size(); ++i)
    {
        const float v = 4.0f * static_cast<float> (std::sin (2.0 * 3.14159265 * 200.0 * static_cast<double> (i) / 48000.0));
        loud[i] = i < 24000 ? v : 0.3f * v / 4.0f;
        loud2[i] = loud[i];
    }
    float* io2[2] = { loud.data(), loud2.data() };
    lim.process (io2, 2, loud.size());
    float peak = 0.0f;
    for (float v : loud)
        peak = std::max (peak, std::abs (v));
    CHECK (peak <= OutputLimiter::kCeiling + 1e-6f);
    CHECK (peak > 0.85f);   // and it is not over-attenuating
    // after the burst the gain recovers to exactly unity
    CHECK_NEAR (std::abs (loud[47000]), std::abs (0.3f * std::sin (2.0 * 3.14159265 * 200.0 * 46952.0 / 48000.0)), 1e-3);
}

RB_TEST (output_stage_volume_monitor_mute_and_fault_detection)
{
    OutputStage os;
    os.prepare (48000.0, 512, false);
    os.setVolumeDb (-6.0206f);
    std::vector<float> l (4800, 1.0f), r (4800, 1.0f), mon (4800, 0.5f);
    float* io[2] = { l.data(), r.data() };
    const float* monitor[1] = { mon.data() };
    CHECK (os.process (io, monitor, 1, false, 4800));
    CHECK_NEAR (l[4000], 0.5, 1e-3);    // monitor defaults off: volume only

    os.setMonitor (1.0f);
    std::fill (l.begin(), l.end(), 1.0f);
    std::fill (r.begin(), r.end(), 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        std::fill (l.begin(), l.end(), 1.0f);
        os.process (io, monitor, 1, false, 4800);
    }
    CHECK_NEAR (l[4000], 1.0, 1e-3);    // 0.5 wet + 0.5 monitor
    for (int i = 0; i < 3; ++i)
    {
        std::fill (l.begin(), l.end(), 1.0f);
        os.process (io, monitor, 1, true, 4800);   // Record-mode playback mutes the monitor
    }
    CHECK_NEAR (l[4000], 0.5, 1e-3);

    l[10] = std::numeric_limits<float>::quiet_NaN();
    CHECK (! os.process (io, monitor, 1, true, 4800));
    CHECK (std::isfinite (l[10]) && l[10] == 0.0f);
}

RB_TEST (A18_voice_trigger_requires_sustained_crossing)
{
    VoiceTrigger t;
    t.prepare (48000.0);
    t.arm (-45.0);
    const float loud = 0.1f * 0.1f;
    for (int i = 0; i < 100000; ++i)
        CHECK (! t.processSquare (0.001f * 0.001f));   // -60 dBFS background never triggers
    // a 30 ms burst is not enough, and a dip resets the count
    bool fired = false;
    for (int i = 0; i < 1440; ++i)
        fired = fired || t.processSquare (loud);
    for (int i = 0; i < 5000; ++i)
        fired = fired || t.processSquare (0.0f);
    CHECK (! fired);
    // 50 ms sustained: the RMS crosses at the 2nd loud frame, so the trigger fires on the 2401st
    for (int i = 0; i < 2400; ++i)
        CHECK (! t.processSquare (loud));
    CHECK (t.processSquare (loud));
    // a single very loud click does not trigger (no long release tail)
    VoiceTrigger c;
    c.prepare (48000.0);
    c.arm (-45.0);
    bool clickFired = c.processSquare (1.0f);
    for (int i = 0; i < 20000; ++i)
        clickFired = clickFired || c.processSquare (0.0f);
    CHECK (! clickFired);
}

RB_TEST (preroll_ring_returns_chronological_tail)
{
    PreRollRing ring;
    ring.configure (1000.0, 2, 0.2);   // 200 frames
    for (int i = 0; i < 350; ++i)
    {
        const float f[2] = { static_cast<float> (i), static_cast<float> (-i) };
        ring.push (f);
    }
    CHECK_EQ (ring.available(), Frame { 200 });
    std::vector<float> a (50), b (50);
    float* dst[2] = { a.data(), b.data() };
    ring.copyOut (dst, 50);
    CHECK_EQ (a[0], 300.0f);
    CHECK_EQ (a[49], 349.0f);
    CHECK_EQ (b[0], -300.0f);
}

RB_TEST (A19_trim_selection_pads_clamps_and_handles_silence)
{
    std::vector<float> v (48000 * 3, 0.0f);
    for (std::size_t i = 48000; i < 72000; ++i)
        v[i] = 0.3f * static_cast<float> (std::sin (0.2 * static_cast<double> (i)));
    auto clip = makeClip (48000.0, { v });
    auto sel = findNonSilentSelection (*clip);
    CHECK_EQ (sel.begin, Frame { 48000 - 2400 });
    CHECK_EQ (sel.end, Frame { 72000 + 2400 });

    // the analysis never modifies the audio
    CHECK_EQ (clip->channel (0)[48100], v[48100]);

    // sound at the very start clamps the padding to the clip
    std::vector<float> w (48000, 0.0f);
    for (std::size_t i = 0; i < 4800; ++i)
        w[i] = 0.5f;
    auto edge = makeClip (48000.0, { w });
    auto s2 = findNonSilentSelection (*edge);
    CHECK_EQ (s2.begin, Frame { 0 });
    CHECK_EQ (s2.end, Frame { 4800 + 2400 });

    // a completely silent take returns an empty selection (UI keeps the original: "No speech or sound detected")
    auto silent = makeClip (48000.0, { std::vector<float> (48000, 0.0f) });
    CHECK (findNonSilentSelection (*silent).empty());
    auto quiet = makeClip (48000.0, { std::vector<float> (48000, 0.001f) });   // -60 dBFS, below -50
    CHECK (findNonSilentSelection (*quiet).empty());
}

RB_TEST (A17_presets_match_the_spec_and_surprise_is_bounded)
{
    const auto& p = builtinPresets();
    CHECK (std::string (p[0].name) == "Say Something");
    CHECK (p[0].values.mode == Mode::Record);
    CHECK_NEAR (p[0].values.captureSeconds, 5.0, 0);
    CHECK_NEAR (p[0].values.waitSeconds, 2.0, 0);
    CHECK (! p[0].values.repeatSession);
    CHECK (p[1].values.mode == Mode::Live);
    CHECK_NEAR (p[1].values.liveChunkSeconds, 0.25, 0);
    CHECK_NEAR (p[1].values.liveDelaySeconds, 0.25, 0);
    CHECK_NEAR (p[2].values.liveChunkSeconds, 1.0, 0);
    CHECK_NEAR (p[2].values.liveDelaySeconds, 0.5, 0);
    CHECK_NEAR (p[3].values.captureSeconds, 10.0, 0);
    CHECK_NEAR (p[3].values.waitSeconds, 2.0, 0);

    SimpleRng a (42), b (42);
    for (int i = 0; i < 1000; ++i)
    {
        auto x = makeSurprise (a);
        auto y = makeSurprise (b);
        CHECK_EQ (x.speed, y.speed);                       // deterministic for a seed
        CHECK (x.speed >= kMinSpeed && x.speed <= kMaxSpeed);
    }
}

RB_TEST (offline_render_matches_player_and_supports_cancel)
{
    auto clip = makeNoiseClip (44100.0, 30000, 2, 11);
    RenderSpec spec;
    spec.source = clip.get();
    spec.selection = { 1000, 29000 };
    spec.direction = Direction::Backward;
    spec.speed = 1.0;
    std::vector<float> l, r;
    auto res = renderOffline (spec, [&] (const float* const* p, std::size_t ch, std::size_t n)
    {
        l.insert (l.end(), p[0], p[0] + n);
        r.insert (r.end(), p[ch - 1], p[ch - 1] + n);
        return true;
    }, nullptr, 1000);
    CHECK (res == RenderResult::Done);
    CHECK_EQ (l.size(), std::size_t { 28000 });
    CHECK_EQ (renderFrameCount (spec), Frame { 28000 });
    for (std::size_t i = 0; i < 28000; i += 997)
        CHECK_EQ (l[i], clip->channel (0)[28999 - i]);

    spec.speed = 2.0;
    spec.outRate = 48000.0;
    CHECK_EQ (renderFrameCount (spec), static_cast<Frame> (std::llround (28000.0 / (44100.0 / 48000.0 * 2.0))));

    std::atomic<bool> cancel { true };
    auto c = renderOffline (spec, [] (const float* const*, std::size_t, std::size_t) { return true; }, &cancel);
    CHECK (c == RenderResult::Cancelled);
}
