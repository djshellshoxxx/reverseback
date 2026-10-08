// Disk-backed ClipSource for decoded files larger than the RAM limit (ENGINE_DESIGN section 11).
// The audio thread only ever copies from resident cache blocks; a worker thread prefetches blocks
// around the playback position in the direction of travel. A miss makes read() return false so the
// player can fade out and pause ("underrun") instead of blocking.
#pragma once

#include "AudioClip.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace rb
{
// Writes the planar block cache file: block b holds [ch0: kBlockFrames floats][ch1: ...]; the last
// block is zero padded.
class DiskCacheWriter
{
public:
    static constexpr std::size_t kBlockFrames = 65536;

    bool open (const std::filesystem::path& path, int channels);
    // `planes[c]` hold `validFrames` (<= kBlockFrames) samples each. Returns false on a write error (disk full).
    bool appendBlock (const float* const* planes, std::size_t validFrames);
    bool finish();

private:
    std::ofstream out_;
    int channels_ = 1;
    std::vector<float> zeroPad_;
};

class DiskClipSource final : public ClipSource
{
public:
    static constexpr std::size_t kBlockFrames = DiskCacheWriter::kBlockFrames;
    static constexpr std::size_t kSlots = 16;
    static constexpr std::int64_t kAhead = 6;

    static std::shared_ptr<DiskClipSource> open (const std::filesystem::path& path, double sampleRate, int channels, Frame frames);
    ~DiskClipSource() override;

    int channels() const noexcept override { return channels_; }
    double sampleRate() const noexcept override { return rate_; }
    Frame frameCount() const noexcept override { return frames_; }
    bool read (std::int64_t first, std::size_t count, float* const* dst) const noexcept override;
    // Reads straight from the cache file (own handle, independent of the playback prefetcher) so an
    // export never depends on which blocks happen to be resident.
    bool readOffline (std::int64_t first, std::size_t count, float* const* dst) const noexcept override;

    // Any thread: tell the prefetcher where playback is about to happen.
    void hint (Frame frame, bool backward) const noexcept;
    bool isResident (Frame frame) const noexcept;
    // Waits (up to timeoutMs) until `frame` and a few following blocks in the hinted direction are resident.
    bool prime (Frame frame, bool backward, int timeoutMs) const;
    std::uint32_t misses() const noexcept { return misses_.load (std::memory_order_relaxed); }
    // Number of times the prefetcher failed to load a block (cache file vanished or unreadable); for diagnostics and tests.
    std::uint32_t loadFailures() const noexcept { return loadFailures_.load (std::memory_order_relaxed); }

    // The source then owns its cache file: it is deleted (and its directory, if empty) when the last
    // reference goes away, after the prefetch thread has stopped.
    void deleteFileWhenDestroyed() noexcept { removeOnDestroy_ = true; }

private:
    struct Slot
    {
        std::atomic<std::int64_t> tag { -1 };
        std::unique_ptr<std::atomic<float>[]> data;
    };

    DiskClipSource() = default;
    void worker();
    bool loadBlock (std::int64_t block, std::vector<float>& scratch);
    bool blockResident (std::int64_t block) const noexcept;
    void noteMiss (std::int64_t block, std::int64_t dir) const noexcept;

    std::filesystem::path path_;
    double rate_ = 48000.0;
    int channels_ = 1;
    Frame frames_ = 0;
    std::int64_t blockCount_ = 0;

    mutable std::mutex directMutex_;
    mutable std::ifstream direct_;

    mutable Slot slots_[kSlots];
    mutable std::atomic<std::int64_t> focus_ { 0 };
    mutable std::atomic<int> dir_ { 0 };
    mutable std::atomic<std::uint32_t> misses_ { 0 };
    std::atomic<std::uint32_t> loadFailures_ { 0 };

    std::thread thread_;
    mutable std::mutex mutex_;
    mutable std::condition_variable wake_;
    std::atomic<bool> quit_ { false };
    bool removeOnDestroy_ = false;
};
}  // namespace rb
