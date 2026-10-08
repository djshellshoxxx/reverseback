// A10, A11, A14, A17 (settings), A20: decode limits, disk cache, export, settings persistence.
#include "TestSupport.h"

#include "ExportService.h"
#include "FileService.h"
#include "SettingsStore.h"
#include "TestUtil.h"

#if JUCE_WINDOWS
 #include <process.h>
 #define rb_getpid _getpid
#else
 #include <unistd.h>
 #define rb_getpid getpid
#endif

using namespace rbt;

namespace
{
rb::LoadOutcome load (const juce::File& f, rb::LoadOptions o = {}, const std::atomic<bool>* cancel = nullptr)
{
    return rb::loadAudioFile (f, o, cancel);
}

bool nearlyEqual (const std::vector<float>& a, const std::function<float (juce::int64)>& expect, double tol)
{
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs (static_cast<double> (a[i]) - static_cast<double> (expect (static_cast<juce::int64> (i)))) > tol)
            return false;
    return true;
}
}  // namespace

RB_TEST (A10_import_wav_aiff_flac_at_44k1_48k_96k_192k_mono_and_stereo)
{
    TempDir t;
    struct Case { const char* name; double rate; int ch, bits; };
    const Case cases[] = { { "a.wav", 44100.0, 2, 24 }, { "b.wav", 48000.0, 1, 32 }, { "c.wav", 96000.0, 2, 16 }, { "d.wav", 192000.0, 1, 24 },
                           { "e.aiff", 44100.0, 1, 16 }, { "f.aiff", 96000.0, 2, 24 }, { "g.flac", 48000.0, 2, 24 }, { "h.flac", 192000.0, 1, 16 } };
    for (const auto& c : cases)
    {
        const juce::File f = t.file (c.name);
        std::unique_ptr<juce::AudioFormat> fmt;
        if (f.hasFileExtension ("wav")) fmt = std::make_unique<juce::WavAudioFormat>();
        else if (f.hasFileExtension ("aiff")) fmt = std::make_unique<juce::AiffAudioFormat>();
        else fmt = std::make_unique<juce::FlacAudioFormat>();
        CHECK (writeFixture (f, *fmt, c.rate, c.ch, c.bits, 20000));
        auto out = load (f);
        CHECK (out.ok());
        if (! out.ok())
            continue;
        CHECK_EQ (out.asset->frames, rb::Frame { 20000 });
        CHECK_EQ (out.asset->channels, c.ch);
        CHECK_NEAR (out.asset->sampleRate, c.rate, 0.01);
        CHECK (out.asset->ram != nullptr);
        const double tol = c.bits == 16 ? 2.0 / 32768.0 : (c.bits == 24 ? 1.0e-6 : 1.0e-7);
        std::vector<float> ch0 (out.asset->ram->channel (0), out.asset->ram->channel (0) + 20000);
        CHECK (nearlyEqual (ch0, [] (juce::int64 i) { return testSample (0, i); }, tol * 2));
        CHECK (out.asset->overview != nullptr && out.asset->overview->size() > 0);
    }
}

RB_TEST (A14_import_rejects_bad_files_with_typed_errors)
{
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (load (t.file ("missing.wav")).error == rb::LoadError::NotFound);

    juce::File junk = t.file ("junk.wav");
    junk.replaceWithText ("this is not a wav file at all");
    CHECK (load (junk).error == rb::LoadError::Unsupported);

    // multichannel: a clear error, never a silent downmix
    {
        std::unique_ptr<juce::OutputStream> s (t.file ("six.wav").createOutputStream());
        auto w = wav.createWriterFor (s, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withChannelLayout (juce::AudioChannelSet::create5point1()).withBitsPerSample (16));
        CHECK (w != nullptr);
        std::vector<float> z (1000, 0.0f);
        const float* p[6] = { z.data(), z.data(), z.data(), z.data(), z.data(), z.data() };
        w->writeFromFloatArrays (p, 6, 1000);
    }
    CHECK (load (t.file ("six.wav")).error == rb::LoadError::TooManyChannels);

    CHECK (writeFixture (t.file ("hi.wav"), wav, 384000.0, 1, 16, 1000));
    CHECK (load (t.file ("hi.wav")).error == rb::LoadError::RateTooHigh);

    CHECK (writeFixture (t.file ("long.wav"), wav, 8000.0, 1, 16, 8000ll * 60 * 31));
    CHECK (load (t.file ("long.wav")).error == rb::LoadError::TooLong);

    CHECK (writeFixture (t.file ("ok.wav"), wav, 48000.0, 2, 16, 48000));
    rb::LoadOptions small;
    small.maxDecodedBytes = 1000;
    CHECK (load (t.file ("ok.wav"), small).error == rb::LoadError::TooLarge);

    // truncated file: fewer frames than the header promises
    {
        const juce::File ok = t.file ("ok.wav");
        juce::MemoryBlock mb;
        ok.loadFileAsData (mb);
        t.file ("trunc.wav").replaceWithData (mb.getData(), mb.getSize() / 2);
        CHECK (load (t.file ("trunc.wav")).error == rb::LoadError::Corrupt);   // truncated data is never silently zero-filled
    }
}

RB_TEST (A14_cancelled_load_returns_cancelled_and_leaves_no_cache)
{
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("big.wav"), wav, 48000.0, 2, 16, 48000ll * 20));
    rb::LoadOptions o;
    o.ramLimitBytes = 1024 * 1024;   // force the disk cache path
    o.cacheRoot = t.dir;
    std::atomic<bool> cancel { false };
    int calls = 0;
    auto out = rb::loadAudioFile (t.file ("big.wav"), o, &cancel, [&] (float) { if (++calls == 3) cancel = true; });
    CHECK (out.error == rb::LoadError::Cancelled);
    CHECK (out.asset == nullptr);
    CHECK_EQ (t.dir.findChildFiles (juce::File::findDirectories, false, "ReverseBack-cache-*").size(), 0);
}

RB_TEST (A14_disk_backed_file_reads_back_exactly_and_cleans_up)
{
    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("big.wav"), wav, 48000.0, 2, 24, 48000ll * 12));
    rb::LoadOptions o;
    o.ramLimitBytes = 1024 * 1024;
    o.cacheRoot = t.dir;
    juce::File cacheDir;
    {
        auto out = load (t.file ("big.wav"), o);
        CHECK (out.ok());
        CHECK (out.asset->disk != nullptr && out.asset->ram == nullptr);
        cacheDir = out.asset->cacheDir;
        CHECK (cacheDir.isDirectory());
        CHECK_EQ (t.dir.findChildFiles (juce::File::findDirectories, false, "ReverseBack-cache-*").size(), 1);
        // random access reads after priming are exact
        const rb::Frame pos = 48000ull * 7 + 123;
        CHECK (out.asset->disk->prime (pos, false, 3000));
        std::vector<float> a (256), b (256);
        float* dst[2] = { a.data(), b.data() };
        CHECK (out.asset->source->read (static_cast<std::int64_t> (pos), 256, dst));
        for (int i = 0; i < 256; i += 31)
            CHECK_NEAR (a[static_cast<std::size_t> (i)], testSample (0, static_cast<juce::int64> (pos) + i), 2.0e-6);
        // playing the whole file backwards through the engine's player works when primed
        rb::ClipPlayer p;
        p.configure (1024, 16.0);
        p.setOutputRate (48000.0);
        p.setSource (out.asset->source.get(), { 0, out.asset->frames });
        p.setDirection (rb::Direction::Backward);
        p.setFadeFrames (0);
        out.asset->disk->prime (out.asset->frames - 1, true, 3000);
        p.play (true);
        std::vector<float> l (1024), r (1024);
        float* o2[2] = { l.data(), r.data() };
        CHECK_EQ (p.process (o2, 2, 1024), std::size_t { 1024 });
        CHECK_NEAR (l[0], testSample (0, static_cast<juce::int64> (out.asset->frames) - 1), 2.0e-6);
    }
    CHECK (! cacheDir.exists());   // closing removes the temporary cache
    CHECK_EQ (t.dir.findChildFiles (juce::File::findDirectories, false, "ReverseBack-cache-*").size(), 0);
}

RB_TEST (startup_cleanup_removes_only_abandoned_caches)
{
    TempDir t;
    const auto dead = t.file ("ReverseBack-cache-99999999-abcd");
    const auto alive = t.file ("ReverseBack-cache-" + juce::String (static_cast<int> (rb_getpid())) + "-feed");
    const auto other = t.file ("not-ours");
    dead.createDirectory();
    alive.createDirectory();
    other.createDirectory();
    // a live owner's cache keeps its creation time for the whole session: age alone must never delete it
    alive.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (30));
    rb::cleanupAbandonedCaches (t.dir);
    CHECK (! dead.exists());
    CHECK (alive.exists());
    CHECK (other.exists());
}

namespace
{
rb::ExportRequest makeRequest (std::shared_ptr<const rb::ClipSource> src, rb::Selection sel, const juce::File& dest)
{
    rb::ExportRequest r;
    r.source = std::move (src);
    r.selection = sel;
    r.direction = rb::Direction::Backward;
    r.destination = dest;
    return r;
}
}  // namespace

RB_TEST (A11_A20_float_export_is_exact_reversal_and_preserves_over_unity)
{
    TempDir t;
    auto clip = rbt::makeNoiseClip (48000.0, 10000, 2, 5);
    clip->channel (0)[1234] = 1.75f;    // over-unity sample must survive float export
    clip->channel (1)[77] = -2.5f;
    auto req = makeRequest (clip, { 0, 10000 }, t.file ("out.wav"));
    const auto res = rb::writeExport (req, nullptr);
    CHECK (res.ok());
    const auto back = readFile (t.file ("out.wav"));
    CHECK (back.ok && back.floatingPoint && back.channels == 2 && back.data[0].size() == 10000);
    for (std::size_t i = 0; i < 10000; ++i)
    {
        CHECK_EQ (back.data[0][i], clip->channel (0)[9999 - i]);
        CHECK_EQ (back.data[1][i], clip->channel (1)[9999 - i]);
    }
    CHECK_EQ (back.data[0][9999 - 1234], 1.75f);

    // double reversal reconstructs the original exactly
    auto reread = rb::AudioClip::create (48000.0, 2, 10000);
    for (int c = 0; c < 2; ++c)
        std::copy (back.data[static_cast<std::size_t> (c)].begin(), back.data[static_cast<std::size_t> (c)].end(), reread->channel (c));
    reread->seal (10000);
    auto req2 = makeRequest (reread, { 0, 10000 }, t.file ("again.wav"));
    CHECK (rb::writeExport (req2, nullptr).ok());
    const auto twice = readFile (t.file ("again.wav"));
    for (std::size_t i = 0; i < 10000; i += 7)
        CHECK_EQ (twice.data[0][i], clip->channel (0)[i]);
}

RB_TEST (A20_pcm_export_refuses_to_clip_unless_normalised_and_normalise_hits_minus_1dBFS)
{
    TempDir t;
    auto clip = rbt::makeSineClip (48000.0, 24000, 440.0, 1.6);
    auto req = makeRequest (clip, { 0, 24000 }, t.file ("pcm.wav"));
    req.settings.format = rb::ExportSettings::Format::Pcm24;
    auto refused = rb::writeExport (req, nullptr);
    CHECK (refused.error == rb::ExportError::WouldClip);
    CHECK (! t.file ("pcm.wav").exists());

    req.settings.normalize = true;
    auto ok = rb::writeExport (req, nullptr);
    CHECK (ok.ok());
    const auto back = readFile (t.file ("pcm.wav"));
    float peak = 0;
    for (float v : back.data[0])
        peak = std::max (peak, std::abs (v));
    CHECK_NEAR (peak, 0.8912509, 2.0e-4);
    CHECK_EQ (back.bits, 24u);

    // quiet material exports as 24-bit PCM without normalisation and with dither stays within 2 LSB
    auto quiet = rbt::makeSineClip (48000.0, 24000, 440.0, 0.25);
    auto q = makeRequest (quiet, { 0, 24000 }, t.file ("q.wav"));
    q.settings.format = rb::ExportSettings::Format::Pcm24;
    q.settings.dither = true;
    q.direction = rb::Direction::Forward;
    CHECK (rb::writeExport (q, nullptr).ok());
    const auto qb = readFile (t.file ("q.wav"));
    double worst = 0;
    for (std::size_t i = 0; i < 24000; ++i)
        worst = std::max (worst, std::abs (static_cast<double> (qb.data[0][i]) - static_cast<double> (quiet->channel (0)[i])));
    CHECK (worst < 3.0 / 8388608.0);
}

RB_TEST (A10_export_length_rate_and_speed_follow_the_playback_mapping)
{
    TempDir t;
    struct Case { double srcRate; int outRate; double speed; rb::Frame sel; };
    const Case cases[] = { { 44100.0, 0, 0.5, 20001 }, { 44100.0, 0, 2.0, 20001 }, { 96000.0, 48000, 1.0, 30000 }, { 192000.0, 44100, 1.0, 40000 },
                           { 48000.0, 44100, 1.5, 33333 }, { 44100.0, 48000, 0.75, 22050 } };
    int i = 0;
    for (const auto& c : cases)
    {
        auto clip = rbt::makeNoiseClip (c.srcRate, c.sel + 100, 1 + (i % 2), 9);
        auto req = makeRequest (clip, { 50, 50 + c.sel }, t.file ("e" + juce::String (i++) + ".wav"));
        req.speed = c.speed;
        req.settings.sampleRate = c.outRate;
        const auto res = rb::writeExport (req, nullptr);
        CHECK (res.ok());
        const auto back = readFile (req.destination);
        const double outRate = c.outRate > 0 ? c.outRate : c.srcRate;
        const double ratio = c.srcRate / outRate * c.speed;
        CHECK_EQ (back.data[0].size(), static_cast<std::size_t> (std::llround (static_cast<double> (c.sel) / ratio)));
        CHECK_NEAR (back.rate, outRate, 0.01);
        CHECK_EQ (back.channels, clip->channels());
        CHECK_EQ (rb::renderFrameCount (rb::RenderSpec { clip.get(), req.selection, req.direction, req.speed, outRate, 0 }), static_cast<rb::Frame> (back.data[0].size()));
    }
}

RB_TEST (A14_export_failures_never_touch_the_destination_or_leave_temp_files)
{
    TempDir t;
    auto clip = rbt::makeNoiseClip (48000.0, 20000, 1, 3);
    const auto dest = t.file ("keep.wav");
    dest.replaceWithText ("precious");

    auto req = makeRequest (clip, { 0, 20000 }, dest);
    CHECK (rb::writeExport (req, nullptr).error == rb::ExportError::DestinationExists);   // needs explicit consent
    CHECK_EQ (dest.loadFileAsString(), juce::String ("precious"));

    req.overwrite = true;
    req.debugFailAfterFrames = 5000;   // simulated disk-full mid-write
    CHECK (rb::writeExport (req, nullptr).error == rb::ExportError::WriteFailed);
    CHECK_EQ (dest.loadFileAsString(), juce::String ("precious"));
    CHECK_EQ (t.dir.findChildFiles (juce::File::findFiles, false, ".*rb-tmp*").size(), 0);

    std::atomic<bool> cancel { true };
    req.debugFailAfterFrames = -1;
    CHECK (rb::writeExport (req, &cancel).error == rb::ExportError::Cancelled);
    CHECK_EQ (dest.loadFileAsString(), juce::String ("precious"));

    CHECK (rb::writeExport (req, nullptr).ok());   // consented overwrite succeeds
    CHECK (readFile (dest).ok);
    CHECK_EQ (t.dir.findChildFiles (juce::File::findFiles, false, ".*rb-tmp*").size(), 0);
}

RB_TEST (A17_settings_roundtrip_corruption_and_future_versions)
{
    TempDir t;
    rb::SettingsStore store (t.file ("settings.json"));
    CHECK_EQ (store.load().userPresets.size(), std::size_t { 0 });   // missing file -> defaults

    rb::StoredSettings s;
    s.lastFolder = "/tmp/x";
    s.ui.holdKey = "g";
    s.ui.reducedMotion = 1;
    s.ui.advancedOpen = true;
    s.userPresets.push_back ({ "Mine", { rb::Mode::Live, 3.0, 1.0, 0.75, 0.5, 1.5, rb::LoopPattern::Loop, rb::Direction::Forward, false } });
    CHECK (store.save (s));
    const auto back = store.load();
    CHECK_EQ (back.lastFolder, juce::String ("/tmp/x"));
    CHECK_EQ (back.ui.holdKey, juce::String ("g"));
    CHECK_EQ (back.ui.reducedMotion, 1);
    CHECK (back.ui.advancedOpen);
    CHECK_EQ (back.userPresets.size(), std::size_t { 1 });
    CHECK (back.userPresets[0].values.mode == rb::Mode::Live);
    CHECK_NEAR (back.userPresets[0].values.liveChunkSeconds, 0.75, 1e-9);
    CHECK_EQ (t.dir.findChildFiles (juce::File::findFiles, false, "*.tmp").size(), 0);   // atomic save leaves no temp file

    t.file ("settings.json").replaceWithText ("{ this is not json");
    const auto bad = store.load();
    CHECK_EQ (bad.userPresets.size(), std::size_t { 0 });
    CHECK (t.file ("settings.bad").existsAsFile());   // the unreadable file is kept, not destroyed

    t.file ("settings.json").replaceWithText (R"({"version": 9, "future": [1,2,3], "ui": {"holdKey": "z", "shortcutsEnabled": false},
        "userPresets": [{"name": "X", "values": {"mode": 99, "capture": 9999, "wait": -4, "chunk": 0.001, "speed": 77}}]})");
    const auto fut = store.load();
    CHECK_EQ (fut.ui.holdKey, juce::String ("z"));
    CHECK (! fut.ui.shortcutsEnabled);
    CHECK_EQ (fut.userPresets.size(), std::size_t { 1 });
    CHECK_NEAR (fut.userPresets[0].values.captureSeconds, 60.0, 1e-9);   // values are clamped into range
    CHECK_NEAR (fut.userPresets[0].values.speed, 2.0, 1e-9);
    CHECK_NEAR (fut.userPresets[0].values.liveChunkSeconds, 0.1, 1e-9);
    CHECK (t.file ("settings.v9.bak").existsAsFile());   // our next save would drop the unknown fields: the original is kept
    CHECK (t.file ("settings.v9.bak").loadFileAsString().contains ("future"));

    // hostile input: NaN values and absurd nesting must neither crash nor leak NaN into the engine
    t.file ("settings.json").replaceWithText (R"({"version": 1, "userPresets": [{"name": "N", "values": {"capture": "nan", "wait": "inf", "speed": "nan"}}]})");
    const auto nan = store.load();
    CHECK_EQ (nan.userPresets.size(), std::size_t { 1 });
    CHECK (std::isfinite (nan.userPresets[0].values.captureSeconds) && nan.userPresets[0].values.captureSeconds > 0.0);
    CHECK (std::isfinite (nan.userPresets[0].values.waitSeconds));
    CHECK (std::isfinite (nan.userPresets[0].values.speed) && nan.userPresets[0].values.speed >= 0.25);
    t.file ("settings.json").replaceWithText (juce::String::repeatedString ("[", 200000));
    CHECK_EQ (store.load().userPresets.size(), std::size_t { 0 });   // refused by the structure check, no stack overflow
}

RB_TEST (audit_export_of_a_disk_backed_file_succeeds_in_both_directions)
{
    TempDir t;
    juce::WavAudioFormat wav;
    const juce::int64 frames = 48000ll * 12;
    CHECK (writeFixture (t.file ("big.wav"), wav, 48000.0, 2, 24, frames));
    rb::LoadOptions o;
    o.ramLimitBytes = 1024 * 1024;   // force the disk cache
    o.cacheRoot = t.dir;
    auto out = load (t.file ("big.wav"), o);
    CHECK (out.ok() && out.asset->disk != nullptr);
    if (! out.ok())
        return;
    for (auto dir : { rb::Direction::Backward, rb::Direction::Forward })
    {
        auto req = makeRequest (out.asset->source, { 0, out.asset->frames }, t.file ("export.wav"));
        req.direction = dir;
        const auto res = rb::writeExport (req, nullptr);   // nothing was primed: used to fail with "could not be read fast enough"
        CHECK (res.ok());
        const auto back = readFile (t.file ("export.wav"));
        CHECK (back.ok && back.data[0].size() == static_cast<std::size_t> (frames));
        for (std::size_t i = 0; i < back.data[0].size(); i += 9973)
        {
            const juce::int64 srcIndex = dir == rb::Direction::Backward ? frames - 1 - static_cast<juce::int64> (i) : static_cast<juce::int64> (i);
            CHECK_NEAR (back.data[0][i], testSample (0, srcIndex), 2.0e-6);
        }
        t.file ("export.wav").deleteFile();
    }
}

RB_TEST (audit_existing_destination_is_asked_about_before_any_clipping_question)
{
    TempDir t;
    auto clip = rbt::makeSineClip (48000.0, 24000, 440.0, 1.6);   // would clip in 24-bit PCM
    t.file ("keep.wav").replaceWithText ("precious");
    auto req = makeRequest (clip, { 0, 24000 }, t.file ("keep.wav"));
    req.settings.format = rb::ExportSettings::Format::Pcm24;

    // Not allowed to overwrite yet: the caller must be asked about the existing file first.
    auto first = rb::writeExport (req, nullptr);
    CHECK (first.error == rb::ExportError::DestinationExists);
    CHECK_EQ (t.file ("keep.wav").loadFileAsString(), juce::String ("precious"));

    // Replace confirmed: now the clipping question comes up, and the file is still untouched.
    req.overwrite = true;
    auto second = rb::writeExport (req, nullptr);
    CHECK (second.error == rb::ExportError::WouldClip);
    CHECK_EQ (t.file ("keep.wav").loadFileAsString(), juce::String ("precious"));

    // Both answered: the file is replaced in one step and no temporary file is left behind.
    req.settings.normalize = true;
    auto third = rb::writeExport (req, nullptr);
    CHECK (third.ok());
    CHECK (t.file ("keep.wav").getSize() > 1000);
    CHECK_EQ (t.dir.findChildFiles (juce::File::findFiles, false, "*rb-tmp*").size(), 0);
}
