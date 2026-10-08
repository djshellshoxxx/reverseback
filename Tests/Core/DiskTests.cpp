// A14 (core part): disk-backed source, prefetch and underrun handling.
#include "DiskClipSource.h"
#include "OfflineRender.h"
#include "TestUtil.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <thread>

using namespace rbt;

namespace
{
struct TempCache
{
    TempCache()
    {
        path = (std::filesystem::temp_directory_path() / ("rb-disk-test-" + std::to_string (counter++) + ".pcm")).string();
    }
    ~TempCache() { std::remove (path.c_str()); }
    std::string path;
    static inline int counter = 0;
};

// Writes ramp audio (value = frame index + 1 on channel 0, negative on channel 1) in cache layout.
std::shared_ptr<DiskClipSource> makeDiskRamp (const TempCache& t, Frame frames, int channels, double rate = 48000.0)
{
    DiskCacheWriter w;
    CHECK (w.open (t.path, channels));
    std::vector<float> a (DiskCacheWriter::kBlockFrames), b (DiskCacheWriter::kBlockFrames);
    for (Frame start = 0; start < frames; start += DiskCacheWriter::kBlockFrames)
    {
        const std::size_t n = static_cast<std::size_t> (std::min<Frame> (DiskCacheWriter::kBlockFrames, frames - start));
        for (std::size_t i = 0; i < n; ++i)
        {
            a[i] = static_cast<float> (start + i + 1);
            b[i] = -a[i];
        }
        const float* planes[2] = { a.data(), b.data() };
        CHECK (w.appendBlock (planes, n));
    }
    CHECK (w.finish());
    return DiskClipSource::open (t.path, rate, channels, frames);
}
}  // namespace

RB_TEST (disk_source_reads_match_written_data_across_blocks)
{
    TempCache t;
    const Frame frames = 3 * DiskCacheWriter::kBlockFrames + 1234;
    auto src = makeDiskRamp (t, frames, 2);
    CHECK (src != nullptr);
    CHECK (src->prime (0, false, 2000));
    std::vector<float> a (1000), b (1000);
    float* dst[2] = { a.data(), b.data() };
    const std::int64_t first = static_cast<std::int64_t> (DiskCacheWriter::kBlockFrames) - 500;   // straddles blocks 0 and 1
    CHECK (src->prime (static_cast<Frame> (first), false, 2000));
    CHECK (src->read (first, 1000, dst));
    CHECK_EQ (a[0], static_cast<float> (first + 1));
    CHECK_EQ (a[999], static_cast<float> (first + 1000));
    CHECK_EQ (b[500], -static_cast<float> (first + 501));
    // outside the clip is zero filled, even before the start
    CHECK (src->read (-10, 20, dst));
    CHECK_EQ (a[0], 0.0f);
    CHECK_EQ (a[9], 0.0f);
    CHECK_EQ (a[10], 1.0f);
}

RB_TEST (A14_disk_source_miss_returns_false_and_prefetch_recovers)
{
    TempCache t;
    const Frame frames = 40 * DiskCacheWriter::kBlockFrames;   // > kSlots blocks, so far blocks are not resident
    auto src = makeDiskRamp (t, frames, 1);
    CHECK (src->prime (0, false, 2000));
    std::vector<float> a (256);
    float* dst[1] = { a.data() };
    const std::int64_t far = static_cast<std::int64_t> (35 * DiskCacheWriter::kBlockFrames);
    CHECK (! src->read (far, 256, dst));   // never blocks: reports a miss
    CHECK (src->misses() > 0);
    CHECK (src->prime (static_cast<Frame> (far), false, 3000));
    CHECK (src->read (far, 256, dst));
    CHECK_EQ (a[0], static_cast<float> (far + 1));
}

RB_TEST (A14_player_pauses_on_underrun_keeps_position_and_resumes_after_prime)
{
    TempCache t;
    const Frame frames = 40 * DiskCacheWriter::kBlockFrames;
    auto src = makeDiskRamp (t, frames, 1);
    CHECK (src->prime (frames - 1, true, 3000));   // backward playback starts at the end
    ClipPlayer p;
    p.configure (512, 16.0);
    p.setOutputRate (48000.0);
    p.setSource (src.get(), { 0, frames });
    p.setDirection (Direction::Backward);
    p.setFadeFrames (0);
    p.play (true);
    std::vector<float> a (512, 0.0f);
    float* o[1] = { a.data() };
    CHECK_EQ (p.process (o, 1, 512), std::size_t { 512 });
    CHECK_EQ (a[0], static_cast<float> (frames));

    // jump the playhead far away behind the prefetcher's back: restart from a distant selection
    p.setSource (src.get(), { 0, 30 * DiskCacheWriter::kBlockFrames });
    p.play (true);   // needs blocks around frame 30*65536 that are not resident
    std::fill (a.begin(), a.end(), 0.0f);
    const std::size_t got = p.process (o, 1, 512);
    CHECK_EQ (got, std::size_t { 0 });
    CHECK (p.underrun());
    CHECK (! p.isActive());

    CHECK (src->prime (30 * DiskCacheWriter::kBlockFrames - 1, true, 3000));
    p.play (false);
    CHECK (! p.underrun());
    const std::size_t again = p.process (o, 1, 512);
    CHECK_EQ (again, std::size_t { 512 });
}

RB_TEST (disk_source_concurrent_reads_while_prefetching_are_consistent)
{
    TempCache t;
    const Frame frames = 24 * DiskCacheWriter::kBlockFrames;
    auto src = makeDiskRamp (t, frames, 2);
    std::vector<float> a (2048), b (2048);
    float* dst[2] = { a.data(), b.data() };
    Frame pos = 0;
    long reads = 0, bad = 0;
    // Bounded by time, not iterations: on a loaded machine the prefetch thread may be slow to get going.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (20);
    while (reads < 400 && std::chrono::steady_clock::now() < deadline)
    {
        if (src->read (static_cast<std::int64_t> (pos), 2048, dst))
        {
            ++reads;
            bad += (a[0] != static_cast<float> (pos + 1) || a[2047] != static_cast<float> (pos + 2048) || b[100] != -a[100]) ? 1 : 0;
            pos = (pos + 2048) % (frames - 2048);
        }
        else
        {
            src->hint (pos, false);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    }
    CHECK (reads >= 400);
    CHECK_EQ (bad, 0L);
}

// ---- audit regressions ---------------------------------------------------------------------------
namespace
{
std::vector<std::vector<float>> collect (const RenderSpec& spec, RenderResult& result)
{
    std::vector<std::vector<float>> out (2);
    result = renderOffline (spec, [&] (const float* const* p, std::size_t ch, std::size_t n)
    {
        for (std::size_t c = 0; c < ch; ++c)
            out[c].insert (out[c].end(), p[c], p[c] + n);
        return true;
    });
    return out;
}
}  // namespace

RB_TEST (audit_export_of_a_disk_backed_clip_does_not_depend_on_the_prefetcher)
{
    TempCache t;
    const Frame frames = 40 * DiskCacheWriter::kBlockFrames + 777;   // far more blocks than cache slots
    auto disk = makeDiskRamp (t, frames, 2);
    auto ram = makeRampClip (48000.0, frames, 2);
    for (Direction d : { Direction::Backward, Direction::Forward })
        for (double speed : { 1.0, 1.5 })
        {
            RenderSpec a;
            a.source = disk.get();
            a.selection = { 1000, frames - 500 };
            a.direction = d;
            a.speed = speed;
            RenderSpec b = a;
            b.source = ram.get();
            RenderResult ra = RenderResult::Aborted, rb = RenderResult::Aborted;
            const auto fromDisk = collect (a, ra);   // nothing primed: used to fail on the first cache miss
            const auto fromRam = collect (b, rb);
            CHECK (ra == RenderResult::Done);
            CHECK (rb == RenderResult::Done);
            CHECK_EQ (fromDisk[0].size(), fromRam[0].size());
            CHECK (fromDisk[0] == fromRam[0]);
            CHECK (fromDisk[1] == fromRam[1]);
        }
}

RB_TEST (audit_prefetch_thread_backs_off_when_the_cache_file_disappears)
{
    TempCache t;
    auto src = makeDiskRamp (t, 8 * DiskCacheWriter::kBlockFrames, 1);
    std::remove (t.path.c_str());            // a temp cleaner removes the cache under a running session
    src->hint (4 * DiskCacheWriter::kBlockFrames, false);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));   // let the worker hit the failure
    const std::clock_t c0 = std::clock();
    const auto w0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for (std::chrono::milliseconds (600));
    const double cpu = static_cast<double> (std::clock() - c0) / CLOCKS_PER_SEC;
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - w0).count();
    CHECK (cpu < 0.25 * wall);               // it used to burn a whole core (~1.0)
    float buf[16];
    float* dst[1] = { buf };
    CHECK (! src->read (4 * static_cast<std::int64_t> (DiskCacheWriter::kBlockFrames), 16, dst));   // reports a miss, no crash
    CHECK (! src->readOffline (0, 16, dst));
}
