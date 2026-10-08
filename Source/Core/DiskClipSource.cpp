#include "DiskClipSource.h"

#include <chrono>
#include <filesystem>

namespace rb
{
// ------------------------------------------------------------------ DiskCacheWriter
bool DiskCacheWriter::open (const std::filesystem::path& path, int channels)
{
    channels_ = channels;
    zeroPad_.assign (kBlockFrames, 0.0f);
    out_.open (path, std::ios::binary | std::ios::trunc);
    return out_.is_open();
}

bool DiskCacheWriter::appendBlock (const float* const* planes, std::size_t validFrames)
{
    if (! out_.is_open() || validFrames > kBlockFrames)
        return false;
    for (int c = 0; c < channels_; ++c)
    {
        out_.write (reinterpret_cast<const char*> (planes[c]), static_cast<std::streamsize> (validFrames * sizeof (float)));
        if (validFrames < kBlockFrames)
            out_.write (reinterpret_cast<const char*> (zeroPad_.data()),
                        static_cast<std::streamsize> ((kBlockFrames - validFrames) * sizeof (float)));
    }
    return out_.good();
}

bool DiskCacheWriter::finish()
{
    out_.flush();
    const bool ok = out_.good();
    out_.close();
    return ok;
}

// ------------------------------------------------------------------ DiskClipSource
std::shared_ptr<DiskClipSource> DiskClipSource::open (const std::filesystem::path& path, double sampleRate, int channels, Frame frames)
{
    if (channels < 1 || channels > static_cast<int> (kMaxChannels) || frames == 0)
        return nullptr;
    std::ifstream probe (path, std::ios::binary);
    if (! probe.is_open())
        return nullptr;

    std::shared_ptr<DiskClipSource> s (new DiskClipSource());
    s->path_ = path;
    s->rate_ = sampleRate;
    s->channels_ = channels;
    s->frames_ = frames;
    s->blockCount_ = static_cast<std::int64_t> ((frames + kBlockFrames - 1) / kBlockFrames);
    for (auto& slot : s->slots_)
    {
        slot.data = std::make_unique<std::atomic<float>[]> (static_cast<std::size_t> (channels) * kBlockFrames);
        slot.tag.store (-1);
    }
    s->thread_ = std::thread ([raw = s.get()] { raw->worker(); });
    return s;
}

DiskClipSource::~DiskClipSource()
{
    quit_.store (true);
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
    if (removeOnDestroy_)
    {
        std::error_code ec;
        std::filesystem::remove (path_, ec);
        std::filesystem::remove (path_.parent_path(), ec);   // only succeeds if the directory is empty
    }
}

bool DiskClipSource::blockResident (std::int64_t block) const noexcept
{
    return block < 0 || block >= blockCount_
           || slots_[static_cast<std::size_t> (block) % kSlots].tag.load (std::memory_order_acquire) == block;
}

bool DiskClipSource::isResident (Frame frame) const noexcept
{
    return blockResident (static_cast<std::int64_t> (frame / kBlockFrames));
}

void DiskClipSource::hint (Frame frame, bool backward) const noexcept
{
    focus_.store (static_cast<std::int64_t> (frame / kBlockFrames), std::memory_order_relaxed);
    dir_.store (backward ? -1 : 1, std::memory_order_relaxed);
    wake_.notify_one();
}

bool DiskClipSource::prime (Frame frame, bool backward, int timeoutMs) const
{
    hint (frame, backward);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    const std::int64_t b0 = static_cast<std::int64_t> (frame / kBlockFrames);
    const std::int64_t step = backward ? -1 : 1;
    for (;;)
    {
        bool ready = true;
        for (std::int64_t i = 0; i < 3; ++i)
            ready = ready && blockResident (b0 + step * i);
        if (ready)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

void DiskClipSource::noteMiss (std::int64_t block, std::int64_t dir) const noexcept
{
    misses_.fetch_add (1, std::memory_order_relaxed);
    focus_.store (block, std::memory_order_relaxed);
    if (dir != 0)
        dir_.store (static_cast<int> (dir), std::memory_order_relaxed);
    wake_.notify_one();
}

bool DiskClipSource::read (std::int64_t first, std::size_t count, float* const* dst) const noexcept
{
    const std::int64_t total = static_cast<std::int64_t> (frames_);
    std::size_t done = 0;
    std::int64_t pos = first;
    const std::int64_t prevFocus = focus_.load (std::memory_order_relaxed);
    int guess = 0;

    while (done < count)
    {
        if (pos < 0 || pos >= total)
        {
            const std::size_t n = pos < 0 ? static_cast<std::size_t> (std::min<std::int64_t> (-pos, static_cast<std::int64_t> (count - done)))
                                          : count - done;
            for (int c = 0; c < channels_; ++c)
                std::fill (dst[c] + done, dst[c] + done + n, 0.0f);
            done += n;
            pos += static_cast<std::int64_t> (n);
            continue;
        }

        const std::int64_t block = pos / static_cast<std::int64_t> (kBlockFrames);
        const std::size_t off = static_cast<std::size_t> (pos % static_cast<std::int64_t> (kBlockFrames));
        const std::size_t n = std::min<std::size_t> ({ count - done, kBlockFrames - off, static_cast<std::size_t> (total - pos) });
        if (guess == 0 && block != prevFocus)
            guess = block > prevFocus ? 1 : -1;

        const Slot& slot = slots_[static_cast<std::size_t> (block) % kSlots];
        if (slot.tag.load (std::memory_order_acquire) != block)
        {
            noteMiss (block, guess);
            return false;
        }
        for (int c = 0; c < channels_; ++c)
        {
            const std::atomic<float>* src = slot.data.get() + static_cast<std::size_t> (c) * kBlockFrames + off;
            for (std::size_t i = 0; i < n; ++i)
                dst[c][done + i] = src[i].load (std::memory_order_relaxed);
        }
        if (slot.tag.load (std::memory_order_acquire) != block)   // replaced while copying
        {
            noteMiss (block, guess);
            return false;
        }
        done += n;
        pos += static_cast<std::int64_t> (n);
    }

    if (guess != 0)
        dir_.store (guess, std::memory_order_relaxed);
    focus_.store (std::clamp<std::int64_t> (first, 0, total - 1) / static_cast<std::int64_t> (kBlockFrames), std::memory_order_relaxed);
    return true;
}

bool DiskClipSource::loadBlock (std::int64_t block, std::vector<float>& scratch)
{
    std::ifstream in (path_, std::ios::binary);
    if (! in.is_open())
        return false;
    const std::size_t blockFloats = static_cast<std::size_t> (channels_) * kBlockFrames;
    in.seekg (static_cast<std::streamoff> (block) * static_cast<std::streamoff> (blockFloats * sizeof (float)));
    scratch.resize (blockFloats);
    in.read (reinterpret_cast<char*> (scratch.data()), static_cast<std::streamsize> (blockFloats * sizeof (float)));
    if (static_cast<std::size_t> (in.gcount()) != blockFloats * sizeof (float))
        return false;

    Slot& slot = slots_[static_cast<std::size_t> (block) % kSlots];
    slot.tag.store (-1, std::memory_order_release);   // invalidate before touching the data
    for (std::size_t i = 0; i < blockFloats; ++i)
        slot.data[i].store (scratch[i], std::memory_order_relaxed);
    slot.tag.store (block, std::memory_order_release);
    return true;
}

void DiskClipSource::worker()
{
    std::vector<float> scratch;
    while (! quit_.load())
    {
        const std::int64_t focus = std::clamp<std::int64_t> (focus_.load (std::memory_order_relaxed), 0, blockCount_ - 1);
        const int dir = dir_.load (std::memory_order_relaxed);
        const std::int64_t step = dir < 0 ? -1 : 1;

        bool loaded = false;
        // Prefer the focus block, then the blocks ahead in the direction of travel, then a couple behind.
        for (std::int64_t i = 0; i <= kAhead + 2 && ! loaded && ! quit_.load(); ++i)
        {
            const std::int64_t b = i <= kAhead ? focus + step * i : focus - step * (i - kAhead);
            if (b < 0 || b >= blockCount_ || blockResident (b))
                continue;
            loadBlock (b, scratch);
            loaded = true;
        }
        if (! loaded)
        {
            std::unique_lock<std::mutex> lock (mutex_);
            wake_.wait_for (lock, std::chrono::milliseconds (5));
        }
    }
}
}  // namespace rb
