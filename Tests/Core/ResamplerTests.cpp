// Resampler quality (V1 section 5.2 "bounded, tested" speed conversion).
#include "TestUtil.h"

#include "Resampler.h"

using namespace rbt;

namespace
{
// Renders `clip` at the given speed/outRate and returns the output.
std::vector<float> convert (const AudioClip& clip, double speed, double outRate, Direction dir = Direction::Forward)
{
    ClipPlayer p;
    p.configure (2048, 16.0);
    p.setOutputRate (outRate);
    p.setSource (&clip, { 0, clip.frameCount() });
    p.setDirection (dir);
    p.setSpeed (speed);
    p.play (true);
    return renderPlayer (p, 1, 1000)[0];
}

// Best-fit sinusoid residual in dB relative to the fitted amplitude (THD+N style).
double residualDb (const std::vector<float>& x, double freqCyclesPerSample, std::size_t from, std::size_t to)
{
    double sc = 0, ss = 0, n = 0;
    for (std::size_t i = from; i < to; ++i)
    {
        const double ph = 2.0 * 3.14159265358979323846 * freqCyclesPerSample * static_cast<double> (i);
        sc += x[i] * std::cos (ph);
        ss += x[i] * std::sin (ph);
        n += 1.0;
    }
    const double a = 2.0 * sc / n, b = 2.0 * ss / n;
    double err = 0, sig = 0;
    for (std::size_t i = from; i < to; ++i)
    {
        const double ph = 2.0 * 3.14159265358979323846 * freqCyclesPerSample * static_cast<double> (i);
        const double fit = a * std::cos (ph) + b * std::sin (ph);
        err += (x[i] - fit) * (x[i] - fit);
        sig += fit * fit;
    }
    return 10.0 * std::log10 (std::max (err, 1e-30) / std::max (sig, 1e-30));
}
}  // namespace

RB_TEST (T_RES_sine_speed_2x_is_clean)
{
    auto clip = makeSineClip (48000.0, 96000, 1000.0);
    auto out = convert (*clip, 2.0, 48000.0);
    CHECK_EQ (out.size(), std::size_t { 48000 });
    // 1 kHz at 2x -> 2 kHz in a 48 kHz stream; ignore the edges where the selection ends.
    const double db = residualDb (out, 2000.0 / 48000.0, 2000, 46000);
    std::printf ("      speed 2x residual: %.1f dB\n", db);
    CHECK (db < -75.0);
}

RB_TEST (T_RES_sine_speed_half_is_clean)
{
    auto clip = makeSineClip (48000.0, 48000, 3000.0);
    auto out = convert (*clip, 0.5, 48000.0);
    CHECK_EQ (out.size(), std::size_t { 96000 });
    const double db = residualDb (out, 1500.0 / 48000.0, 4000, 92000);
    std::printf ("      speed 0.5x residual: %.1f dB\n", db);
    CHECK (db < -75.0);
}

RB_TEST (T_RES_rate_conversion_96k_to_48k_and_44k1_to_48k)
{
    auto a = makeSineClip (96000.0, 96000, 5000.0);
    auto outA = convert (*a, 1.0, 48000.0, Direction::Backward);
    CHECK_EQ (outA.size(), std::size_t { 48000 });
    const double dbA = residualDb (outA, 5000.0 / 48000.0, 3000, 45000);
    std::printf ("      96k->48k residual: %.1f dB\n", dbA);
    CHECK (dbA < -75.0);

    auto b = makeSineClip (44100.0, 44100, 1000.0);
    auto outB = convert (*b, 1.0, 48000.0);
    CHECK_EQ (outB.size(), std::size_t { 48000 });
    const double dbB = residualDb (outB, 1000.0 / 48000.0, 3000, 45000);
    std::printf ("      44.1k->48k residual: %.1f dB\n", dbB);
    CHECK (dbB < -75.0);
}

RB_TEST (T_RES_alias_rejection_when_downsampling_by_4)
{
    // 20 kHz tone at 96 kHz played at 192k->... use speed 2 on 48k: 20 kHz would alias; it must be removed.
    auto clip = makeSineClip (48000.0, 96000, 20000.0, 0.8);
    auto out = convert (*clip, 2.0, 48000.0);
    CHECK (rms (out, 4000, 44000) < 0.8 * 0.7071 * std::pow (10.0, -70.0 / 20.0));
}

RB_TEST (T_RES_dc_gain_is_unity_at_any_phase)
{
    auto clip = makeClip (48000.0, { std::vector<float> (20000, 0.5f) });
    for (double speed : { 0.5, 0.77, 1.0, 1.31, 1.999 })
    {
        auto out = convert (*clip, speed, 48000.0);
        double worst = 0;
        for (std::size_t i = 100; i + 100 < out.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (out[i]) - 0.5));
        CHECK (worst < 1e-5);
    }
}

RB_TEST (T_RES_backward_is_mirror_of_forward)
{
    auto clip = makeNoiseClip (48000.0, 6000, 1, 5);
    // reversed clip played forward must equal the clip played backward at fractional ratios
    std::vector<float> rev (clip->channel (0), clip->channel (0) + 6000);
    std::reverse (rev.begin(), rev.end());
    auto mirrored = makeClip (48000.0, { rev });
    auto a = convert (*clip, 1.37, 48000.0, Direction::Backward);
    auto b = convert (*mirrored, 1.37, 48000.0, Direction::Forward);
    CHECK_EQ (a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK_NEAR (a[i], b[i], 1e-6);
}
