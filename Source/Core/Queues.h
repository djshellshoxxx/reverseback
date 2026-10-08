// Wait-free bounded queues and a sequence-locked snapshot cell. No allocation after construction.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <utility>

namespace rb
{
// Single-producer single-consumer ring. N must be a power of two. T must be default
// constructible and move assignable; elements are moved in and out (never copied).
template <class T, std::size_t N>
class SpscQueue
{
    static_assert ((N & (N - 1)) == 0, "capacity must be a power of two");

public:
    bool push (T&& v) noexcept (std::is_nothrow_move_assignable_v<T>)
    {
        const std::size_t t = tail_.load (std::memory_order_relaxed);
        const std::size_t h = head_.load (std::memory_order_acquire);
        if (t - h == N)
            return false;
        buf_[t & (N - 1)] = std::move (v);
        tail_.store (t + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept (std::is_nothrow_move_assignable_v<T>)
    {
        const std::size_t h = head_.load (std::memory_order_relaxed);
        const std::size_t t = tail_.load (std::memory_order_acquire);
        if (h == t)
            return false;
        out = std::move (buf_[h & (N - 1)]);
        head_.store (h + 1, std::memory_order_release);
        return true;
    }

    std::size_t sizeApprox() const noexcept
    {
        return tail_.load (std::memory_order_acquire) - head_.load (std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    alignas (64) std::atomic<std::size_t> head_ { 0 };
    alignas (64) std::atomic<std::size_t> tail_ { 0 };
    std::array<T, N> buf_ {};
};

// Control-thread -> audio-thread command queue. Several control threads may push (they share a
// mutex that the audio thread never touches); the audio thread is the only consumer.
template <class T, std::size_t N>
class CommandQueue
{
public:
    bool push (T&& v)
    {
        std::lock_guard<std::mutex> lock (producerMutex_);
        return queue_.push (std::move (v));
    }

    bool pop (T& out) noexcept { return queue_.pop (out); }

private:
    std::mutex producerMutex_;
    SpscQueue<T, N> queue_;
};

// Single writer, many readers. T must be trivially copyable. Readers never block the writer and
// always see a consistent copy. Implemented with word-sized atomics so there is no data race.
template <class T>
class SeqLock
{
    static_assert (std::is_trivially_copyable_v<T>, "SeqLock payload must be trivially copyable");
    static constexpr std::size_t kWords = (sizeof (T) + 7) / 8;

public:
    void store (const T& value) noexcept
    {
        std::uint64_t tmp[kWords] = {};
        std::memcpy (tmp, &value, sizeof (T));
        const std::uint32_t s = seq_.load (std::memory_order_relaxed);
        seq_.store (s + 1, std::memory_order_relaxed);
        for (std::size_t i = 0; i < kWords; ++i)
            words_[i].store (tmp[i], std::memory_order_release);
        seq_.store (s + 2, std::memory_order_release);
    }

    T load() const noexcept
    {
        std::uint64_t tmp[kWords];
        for (;;)
        {
            const std::uint32_t s1 = seq_.load (std::memory_order_acquire);
            if (s1 & 1u)
                continue;
            for (std::size_t i = 0; i < kWords; ++i)
                tmp[i] = words_[i].load (std::memory_order_acquire);
            const std::uint32_t s2 = seq_.load (std::memory_order_acquire);
            if (s1 == s2)
                break;
        }
        T out;
        std::memcpy (&out, tmp, sizeof (T));
        return out;
    }

private:
    std::atomic<std::uint32_t> seq_ { 0 };
    std::array<std::atomic<std::uint64_t>, kWords> words_ {};
};
}  // namespace rb
