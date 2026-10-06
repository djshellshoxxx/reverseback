#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace reverseback
{
template <typename T, std::size_t Capacity>
class CommandQueue
{
    static_assert(Capacity >= 2);

public:
    bool push(const T& value) noexcept
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = (write + 1) % Capacity;
        if (next == read_.load(std::memory_order_acquire))
            return false;

        items_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire))
            return std::nullopt;

        auto value = items_[read];
        read_.store((read + 1) % Capacity, std::memory_order_release);
        return value;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return read_.load(std::memory_order_acquire) ==
               write_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> items_{};
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> write_{0};
};
}
